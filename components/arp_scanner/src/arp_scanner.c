#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"

#include "arp.h"
#include "debug.h"

static const char TAG[] = "arp_scanner";

static void begin_scan(){
    for (uint8_t i = 1; i<255; i++){
        ipv4_addr_t ip = {.bytes = {(uint8_t)192,(uint8_t)168,(uint8_t)50,i}};
        mac_addr_t mac;
        esp_err_t result = arp_get_mac_addr(&ip, &mac);

        char readable_ip[sizeof("ddd.ddd.ddd.ddd")];
        ipv4_to_readable(readable_ip, ip.bytes);

        if (result != ESP_OK){
            ESP_LOGI(TAG, "No mac found for ip: %s", readable_ip);
        } else {
            char readable_mac[sizeof("ff:ff:ff:ff:ff:ff")];
            mac_to_readable(readable_mac, mac.bytes);

            ESP_LOGI(TAG, "Found ip: %s with mac: %s", readable_ip, readable_mac);
        }
    }
}

static void wifi_connect_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    ESP_LOGI(TAG, "ARP scanner detected wifi connect event!");

    begin_scan();
}

void arp_scanner_init(void)
{
    ESP_LOGI(TAG, "ARP scanner initialized");
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &wifi_connect_event, NULL));
}