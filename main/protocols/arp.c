#include <stdint.h>
#include <string.h>
#include "arp.h"
#include "ipv4.h"
#include "eth.h"
#include "esp_log.h"
#include "esp_err.h"
#include "../utility/endian.h"
#include "../utility/net_types.h"

// This implementation of ARP only handles mac and ipv4 addresses.

static const char* TAG = "arp protocol";

const uint8_t IP_MAC_CACHE_LENGTH = 16;

const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static ip_addr_t self_ip_addr;
static mac_addr_t self_mac_addr;

// First 4 bytes are the IP, the latter 6 bytes are the MAC
static uint8_t ip_mac_cache[10][IP_MAC_CACHE_LENGTH+1] = {};

void add_ip_mac_cache_entry(uint8_t ip[4], uint8_t mac[6]){
    // Only set to 0 on program start
    static uint8_t ip_mac_cache_top = 0;
    if (ip_mac_cache_top >= IP_MAC_CACHE_LENGTH-1) {
        // Overwrite old entries when the cache fills up, good enough for now.
        ip_mac_cache_top = 0; 
        ESP_LOGW(TAG, "The ip_mac_cache array overflowed and will now overwrite old entries!");
    }
    memcpy(ip_mac_cache[ip_mac_cache_top], ip, 4);
    memcpy(ip_mac_cache[ip_mac_cache_top]+4, mac, 6);
    ip_mac_cache_top++;
}

// Returns ESP_OK if found, and ESP_FAIL if its not cached
esp_err_t get_mac_addr(ip_addr_t* ip, mac_addr_t* out_mac){
    for (int i = 0; i<=IP_MAC_CACHE_LENGTH-1; i++){
        if (memcmp(ip->addr, ip_mac_cache[i], 4) == 0){
            memcpy(out_mac, ip_mac_cache[i]+4, 6);
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

/**
 * 
 * @returns ESP_OK if found, ESP_FAIL otherwise.
 */
esp_err_t request_mac_addr(ip_addr_t* ip, mac_addr_t* out_mac){
    // Check cache first
    if (get_mac_addr(ip, out_mac) == ESP_OK){
        return ESP_OK;
    }

        /* Serialize ARP reply into a byte buffer */
    uint8_t arp_request_buf[28];  /* Standard ARP payload size for IPv4 over Ethernet */
    uint8_t *p = arp_request_buf;
    
    /* Hardware type (2 bytes, network byte order) */
    uint16_t hw_type = htons(1);
    memcpy(p, &hw_type, 2); p += 2;
    
    /* Protocol type (2 bytes, network byte order) */
    uint16_t proto_type = htons(IPV4_ETHERTYPE);
    memcpy(p, &proto_type, 2); p += 2;
    
    /* Hardware address length (1 byte) */
    *p++ = 6;
    
    /* Protocol address length (1 byte) */
    *p++ = 4;
    
    /* Operation (2 bytes, network byte order) - 2 for reply */
    uint16_t operation = htons(1);
    memcpy(p, &operation, 2); p += 2;
    
    /* Sender hardware address (6 bytes) */
    memcpy(p, self_mac_addr.addr, 6); p += 6;
    
    /* Sender protocol address (4 bytes) */
    memcpy(p, self_ip_addr.addr, 4); p += 4;
    
    /* Empty target hardware address (6 bytes) */
    p += 6;
    
    /* Target protocol address (4 bytes) */
    memcpy(p, ip->addr, 4); p += 4;
    
    uint16_t arp_reply_len = p - arp_request_buf;
    
    /* Transmit via Ethernet layer */
    esp_err_t result = eth_tx(BROADCAST_MAC, self_mac_addr.addr, htons(0x0806), 
                              arp_request_buf, arp_reply_len);
    if (result != ESP_OK) {
        ESP_LOGI(TAG, "Failed to send ARP request");
        return result;
    }
}

void arp_set_local_info(const mac_addr_t *mac, const ip_addr_t *ip){
    memcpy(self_ip_addr.addr, ip, 4);
    memcpy(self_mac_addr.addr, mac, 6);
}

void arp_input(struct arp_data_t* buffer, uint16_t len){
    char readable_dst_ip[12+3+1];
    ipv4_to_readable(readable_dst_ip, buffer->dst_ip);

    char readable_src_ip[12+3+1];
    ipv4_to_readable(readable_src_ip, buffer->src_ip);


    ESP_LOGI(TAG, "ARP event received! Source IP: %s Destination IP: %s", readable_src_ip, readable_dst_ip);

    if (ntohs(buffer->hardware_type) != 1){
        // discard
        return;
    }
    if (ntohs(buffer->protocol_type) != IPV4_ETHERTYPE){
        // discard
        return;
    }

    if (memcmp(buffer->dst_ip, ipv4_address, 4) != 0){
        // discard
        ESP_LOGI(TAG, "ARP request was not for our ip");
        return;
    }

    if (ntohs(buffer->operation) != 1){
        // handle receiving a reply
        add_ip_mac_cache_entry(buffer->src_ip, buffer->src_mac);
        return;
    }

    /*if (memcmp(buffer->dst_mac, broadcast_mac, 6) != 0){
        ESP_LOGI(TAG, "ARP request was not a broadcast");
        return;
    }*/

    //char readable_ip[12+3+1];
    //ipv4_to_readable(readable_ip, buffer->src_ip);
    ESP_LOGI(TAG, "ARP request detected for us, from IP: %s", readable_src_ip);

    /* Serialize ARP reply into a byte buffer */
    uint8_t arp_reply_buf[28];  /* Standard ARP payload size for IPv4 over Ethernet */
    uint8_t *p = arp_reply_buf;
    
    /* Hardware type (2 bytes, network byte order) */
    uint16_t hw_type = htons(1);
    memcpy(p, &hw_type, 2); p += 2;
    
    /* Protocol type (2 bytes, network byte order) */
    uint16_t proto_type = htons(IPV4_ETHERTYPE);
    memcpy(p, &proto_type, 2); p += 2;
    
    /* Hardware address length (1 byte) */
    *p++ = 6;
    
    /* Protocol address length (1 byte) */
    *p++ = 4;
    
    /* Operation (2 bytes, network byte order) - 2 for reply */
    uint16_t operation = htons(2);
    memcpy(p, &operation, 2); p += 2;
    
    /* Sender hardware address (6 bytes) */
    memcpy(p, self_mac_addr.addr, 6); p += 6;
    
    /* Sender protocol address (4 bytes) */
    memcpy(p, self_ip_addr.addr, 4); p += 4;
    
    /* Target hardware address (6 bytes) */
    memcpy(p, buffer->src_mac, 6); p += 6;
    
    /* Target protocol address (4 bytes) */
    memcpy(p, buffer->src_ip, 4); p += 4;
    
    uint16_t arp_reply_len = p - arp_reply_buf;
    
    /* Transmit via Ethernet layer */
    esp_err_t result = eth_tx(buffer->src_mac, self_mac_addr.addr, htons(0x0806), 
                              arp_reply_buf, arp_reply_len);
    if (result != ESP_OK) {
        ESP_LOGI(TAG, "Failed to send ARP reply");
    }
}

