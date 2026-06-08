#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "../utility/wifi_tx.h"

#define TAG "eth"

esp_err_t eth_tx(const uint8_t dst_mac[6], const uint8_t src_mac[6], uint16_t ethertype,
                 const void *payload, uint16_t payload_len){

    uint16_t eth_frame_len = 14 + payload_len;
    uint8_t *eth_frame = malloc(eth_frame_len);
    if (eth_frame == NULL) {
        ESP_LOGW(TAG, "Malloc on frame of size %d failed", eth_frame_len);
        return ESP_ERR_NO_MEM;
    }
    
    uint8_t* p = eth_frame;
    memcpy(p, dst_mac, 6); p+=6;
    memcpy(p, src_mac, 6); p+=6;
    memcpy(p, &ethertype, 2); p+=2;
    memcpy(p, payload, payload_len); p+=payload_len;

    esp_err_t result = wifi_tx_raw((void *)eth_frame, eth_frame_len);
    free(eth_frame);
    if (result != ESP_OK){
        ESP_LOGW(TAG, "wifi_tx_raw failed: %s", esp_err_to_name(result));
    }
    return result;
}