#pragma once
#include <stdint.h>
#include "esp_err.h"

struct eth_hdr_t {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
};

/**
 * Transmit an Ethernet II frame with the given payload.
 * Constructs: [Ethernet Header (14 bytes)] [Payload]
 * 
 * @param dst_mac       Destination MAC address (6 bytes)
 * @param src_mac       Source MAC address (6 bytes)
 * @param ethertype     EtherType field (in network byte order)
 * @param payload       Pointer to payload data
 * @param payload_len   Length of payload in bytes
 * @return ESP_OK on success, others on fail
 */
esp_err_t eth_tx(const uint8_t dst_mac[6], const uint8_t src_mac[6], uint16_t ethertype,
                 const void *payload, uint16_t payload_len);