#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"

static const char TAG[] = "arp_scanner";

static void wifi_connect_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    ESP_LOGI(TAG, "ARP scanner detected wifi connect event!");
}

// Initialization function for this component. Call this from your application's app_main.
void arp_scanner_init(void)
{
    ESP_LOGI(TAG, "ARP scanner initialized");
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &wifi_connect_event, NULL));
}