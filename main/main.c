#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_supplicant/esp_wpa.h"
#include "esp_private/wifi.h"
#include "esp_wifi_types.h"

#include "utility/debug.h"
#include "utility/net_types.h"
#include "utility/endian.h"
#include "protocols/arp.h"
#include "protocols/eth.h"
#include "protocols/ipv4.h"
#include "protocols/icmpv4.h"

#include "secrets.h"
#include "arp_scanner.h"
#include "tcp_test.h"
#include "task_monitor.h"

#define ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASSWORD CONFIG_ESP_WIFI_PASSWORD

esp_err_t mac_init(void);

static const char* TAG = "wifi_station";
static volatile uint32_t wifi_rx_count = 0;
static volatile uint32_t wifi_buf_free_enter_count = 0;
static volatile uint32_t wifi_buf_free_exit_count = 0;
static volatile uint32_t wifi_buf_freed_count = 0;
static volatile uint32_t wifi_buf_queue_fail_count = 0;
static volatile uint32_t wifi_buf_eb_null_count = 0;
static volatile uint32_t wifi_buf_eb_non_null_count = 0;
static QueueHandle_t rx_buffer_free_queue = NULL;

typedef struct {
    void* buffer;
    void* eb;
} wifi_rx_free_item_t;

static struct net_context_t net_context = {
    .ip = {
        .bytes = {192, 168, 1, 230}
    }
};

int wifi_rxcb(void* buffer, uint16_t len, void* eb){
    struct eth_hdr_t* eth_hdr = buffer;

    char readable_src_mac[12+5+1];
    mac_to_readable(readable_src_mac, eth_hdr->src_mac);
    char readable_dst_mac[12+5+1];
    mac_to_readable(readable_dst_mac, eth_hdr->dst_mac);

    //ESP_LOGI(TAG, "Wifi event recieved! Source MAC: %s Destination MAC: %s Ethertype: %x", readable_src_mac, readable_dst_mac, ntohs(eth_hdr->ethertype));
    
    // remove the ethernet 2 header before sending it to the upper protocols
    void* buffer_data = buffer + 14;
    uint16_t buffer_data_len = len - 14;

    switch(ntohs(eth_hdr->ethertype)) {
        case ARP_ETHERTYPE:
            arp_input(buffer_data, buffer_data_len);
            break;
        case IPV4_ETHERTYPE:
            ipv4_input(buffer_data, buffer_data_len);
        default: 
            break;
    }

    wifi_rx_count++;

    if (eb == NULL) {
        wifi_buf_eb_null_count++;
    } else {
        wifi_buf_eb_non_null_count++;
    }

    if (eb != NULL) {
        esp_wifi_internal_free_rx_buffer(eb);
    } else {
        free(buffer);
    }

    /*
    // Queue the buffer handles to be freed by a separate task
    // Don't free it here to avoid blocking the RX callback
    wifi_rx_free_item_t item = {
        .buffer = buffer,
        .eb = eb
    };
    if (xQueueSendToBack(rx_buffer_free_queue, &item, 0) != pdPASS) {
        wifi_buf_queue_fail_count++;
        if (eb != NULL) {
            esp_wifi_internal_free_rx_buffer(eb);
        } else {
            free(buffer);
        }
    }
    */

    return 0;
}
/*
static void wifi_rx_buffer_free_task(void* arg)
{
    wifi_rx_free_item_t item = {0};

    ESP_LOGI(TAG, "wifi_rx_buffer_free_task started");

    while (1) {
        if (xQueueReceive(rx_buffer_free_queue, &item, portMAX_DELAY) == pdPASS) {
            wifi_buf_free_enter_count++;
            if (item.eb != NULL) {
                esp_wifi_internal_free_rx_buffer(item.eb);
            } else {
                free(item.buffer);
            }
            wifi_buf_free_exit_count++;
            wifi_buf_freed_count++;
        }
    }
}
*/
/*
static void wifi_rx_heartbeat_task(void* arg)
{
    uint32_t last_count = 0;
    uint32_t last_freed_count = 0;
    uint32_t last_eb_null_count = 0;
    uint32_t last_eb_non_null_count = 0;
    uint32_t last_free_heap = esp_get_free_heap_size();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        uint32_t current_count = wifi_rx_count;
        uint32_t current_freed = wifi_buf_freed_count;
        uint32_t current_free_enter = wifi_buf_free_enter_count;
        uint32_t current_free_exit = wifi_buf_free_exit_count;
        uint32_t current_queue_fail = wifi_buf_queue_fail_count;
        uint32_t current_eb_null = wifi_buf_eb_null_count;
        uint32_t current_eb_non_null = wifi_buf_eb_non_null_count;
        uint32_t current_free_heap = esp_get_free_heap_size();

        int32_t rx_diff = current_count-last_count;
        int32_t heap_diff = current_free_heap-last_free_heap;
        ESP_LOGI(TAG, "RX: total=%lu delta=%d | FREED: total=%lu delta=%lu | queued=%lu | qfail=%lu | EB NULL: total=%lu delta=%lu | EB NONNULL: total=%lu delta=%lu | FREE HEAP: total=%lu delta=%d delta/packet %d",
                 (unsigned long)current_count,
                 (int32_t)(rx_diff),
                 (unsigned long)current_freed,
                 (unsigned long)(current_freed - last_freed_count),
                 (unsigned long)(current_count - current_freed),
                 (unsigned long)current_queue_fail,
             (unsigned long)current_eb_null,
             (unsigned long)(current_eb_null - last_eb_null_count),
             (unsigned long)current_eb_non_null,
             (unsigned long)(current_eb_non_null - last_eb_non_null_count),
                 (unsigned long)(current_free_heap),
                 (int32_t)(heap_diff),
                 (int32_t)((rx_diff == 0) ? 0 : (heap_diff/rx_diff))
        );
        last_count = current_count;
        last_freed_count = current_freed;
        last_eb_null_count = current_eb_null;
        last_eb_non_null_count = current_eb_non_null;
        last_free_heap = current_free_heap;
    }
}
*/


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    ESP_LOGI(TAG, "Event id %d recieved", event_id);
    if (event_id == WIFI_EVENT_STA_START){
        ESP_LOGI(TAG, "Connecting to wifi... ");
        ESP_ERROR_CHECK(esp_wifi_connect());
        ESP_LOGI(TAG, "esp_wifi_connect finished");
    } else if (event_id == WIFI_EVENT_STA_CONNECTED){
        ESP_LOGI(TAG, "Connected to wifi!");
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = event_data;
        ESP_LOGW(TAG, "Disconnected from wifi! reason=%u", disconnected->reason);
    }
}

void wifi_begin(void)
{
    ESP_LOGI(TAG, "Initialising wifi...");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Not sure what this does but the default esp_wifi_init uses it.
    mac_init();

    esp_wifi_set_rx_pbuf_mem_type(WIFI_RX_PBUF_DRAM);

    ESP_ERROR_CHECK(esp_wifi_init_internal(&cfg));
    //ESP_LOGI(TAG, "esp_wifi_init_internal finished");

    ESP_ERROR_CHECK(esp_supplicant_init());

    // Should already be WIFI_PS_NONE by default, but just to be sure.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // MAC: 30:83:98:94:ec:1b
    ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, net_context.mac.bytes));

    // No DHCP so once we have the MAC we can tell everyone what they need
    arp_set_local_info(&net_context.mac, &net_context.ip);
    ipv4_set_self_net_context(&net_context);
    icmpv4_set_self_net_context(&net_context);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .threshold = {
                .authmode = WIFI_AUTH_WPA2_PSK
            }
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    //ESP_LOGI(TAG, "Starting wifi... ");
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wifi initialised!");

    // Init protocols
    arp_init();
    ipv4_init();
    

    // Create queue for deferred RX buffer freeing
    rx_buffer_free_queue = xQueueCreate(16, sizeof(wifi_rx_free_item_t));
    if (rx_buffer_free_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create rx_buffer_free_queue");
        return;
    }

    /*
    // Create task to free RX buffers (deferred from callback)
    if (xTaskCreate(wifi_rx_buffer_free_task, "wifi rx buf free", 2048, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create wifi_rx_buffer_free_task");
        return;
    }
    */

    esp_wifi_internal_reg_rxcb(ESP_IF_WIFI_STA, wifi_rxcb);

    //if (xTaskCreate(wifi_rx_heartbeat_task, "wifi rx heartbeat", 1024, NULL, 1, NULL) != pdPASS) {
    //    ESP_LOGE(TAG, "Failed to create wifi_rx_heartbeat_task");
    //    return;
    //}
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_begin();

    /* Initialize optional components */
    static const char *const watched[] = {"tcp task", "arp task", "ipv4 task", NULL };
    task_monitor_init(watched);
}