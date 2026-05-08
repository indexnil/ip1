#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_supplicant/esp_wpa.h"
#include "esp_private/wifi.h"

#include "utility/debug.h"
#include "utility/net_types.h"
#include "utility/endian.h"
#include "protocols/arp.h"
#include "protocols/eth.h"

#include "secrets.h"

#define ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASSWORD CONFIG_ESP_WIFI_PASSWORD

esp_err_t mac_init(void);

static const char* TAG = "wifi station";

static struct net_context_t net_context;

int wifi_rxcb(void* buffer, uint16_t len, void* eb){
    struct eth_hdr_t* eth_hdr = buffer;

    // remove the ethernet 2 header before sending it to the upper protocols
    void* buffer_data = buffer + 14;
    uint16_t buffer_data_len = len - 14;

    char readable_src_mac[12+5+1];
    mac_to_readable(readable_src_mac, eth_hdr->src_mac);
    char readable_dst_mac[12+5+1];
    mac_to_readable(readable_dst_mac, eth_hdr->dst_mac);

    ESP_LOGI(TAG, "Wifi event recieved! Source MAC: %s Destination MAC: %s Ethertype: %x", readable_src_mac, readable_dst_mac, ntohs(eth_hdr->ethertype));

    switch(ntohs(eth_hdr->ethertype)) {
        case ARP_ETHERTYPE:
            arp_input(buffer_data, buffer_data_len);
            break;
        default: 
            break;
    }

    // Free the driver RX buffer handle (eb)
    if (eb) esp_wifi_internal_free_rx_buffer(eb);

    return 0;
}


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
    ESP_LOGI(TAG, "esp_wifi_init_internal finished");

    ESP_ERROR_CHECK(esp_supplicant_init());

    // MAC: 30:83:98:94:ec:1b
    ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, net_context.mac.addr));

    // No DHCP so once we have the MAC we can tell everyone what they need
    arp_set_local_info(net_context.mac.addr, net_context.ip.addr);

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

    ESP_LOGI(TAG, "Starting wifi... ");
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "esp_wifi_start finished");

    // Init protocols
    arp_init();

    esp_wifi_internal_reg_rxcb(ESP_IF_WIFI_STA, wifi_rxcb);
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_begin();
}