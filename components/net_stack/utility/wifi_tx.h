#pragma once
#include <stdint.h>
#include "esp_err.h"

/* Lightweight transmit helper for other protocols.
 * Implemented in wifi_tx.c and isolates callers from esp_private/wifi.h.
 */
esp_err_t wifi_tx_raw(const void *data, uint16_t len);
