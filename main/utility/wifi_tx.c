#include "wifi_tx.h"
#include "esp_err.h"
#include "esp_private/wifi.h"

/* Simple wrapper that calls the internal SDK transmit function.
 * Keep the heavy SDK include here so protocol files only include wifi_tx.h.
 */
esp_err_t wifi_tx_raw(const void *data, uint16_t len)
{
    /* choose interface appropriate for your app; using STA here */
    return esp_wifi_internal_tx(ESP_IF_WIFI_STA, (void *)data, len);
}
