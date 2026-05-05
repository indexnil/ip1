#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "../utility/wifi_tx.h"


esp_err_t eth_tx(const uint8_t dst_mac[6], const uint8_t src_mac[6], uint16_t ethertype,
                 const void *payload, uint16_t payload_len){

    uint16_t eth_frame_len = 14 + payload_len;
    uint8_t eth_frame[eth_frame_len];
    
    uint8_t* p = eth_frame;
    memcpy(p, dst_mac, 6); p+=6;
    memcpy(p, src_mac, 6); p+=6;
    memcpy(p, &ethertype, 2); p+=2;
    memcpy(p, payload, payload_len); p+=payload_len;

    return wifi_tx_raw((void *)eth_frame, eth_frame_len);
}