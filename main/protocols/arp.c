// This implementation of ARP only handles mac and ipv4 addresses.

#include <stdint.h>
#include <string.h>
#include "arp.h"
#include "ipv4.h"
#include "eth.h"
#include "esp_log.h"
#include "esp_err.h"
#include "../utility/endian.h"
#include "../utility/net_types.h"
#include "queue.h"
#include "FreeRTOS.h"

#define IP_MAC_CACHE_LENGTH 16

static const char* TAG = "arp protocol";

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static ip_addr_t self_ip_addr;
static mac_addr_t self_mac_addr;

// First 4 bytes are the IP, the latter 6 bytes are the MAC
static uint8_t ip_mac_cache[IP_MAC_CACHE_LENGTH][10] = {};

static struct arp_pending_t arp_pending_tasks[MAX_PENDING_IPS];

static QueueHandle_t arp_request_queue = NULL;

void add_ip_mac_cache_entry(ip_addr_t ip, mac_addr_t mac){
    // First check for duplicates
    for (int i = 0; i<=IP_MAC_CACHE_LENGTH-1; i++){
        if (memcmp(ip.addr, ip_mac_cache[i], 4) == 0){
            // Duplicate found, simply update it and return
            memcpy(ip_mac_cache[i]+4, mac.addr, 6);
            return;
        }
    }

    // Only set to 0 on program start
    static uint8_t ip_mac_cache_top = 0;
    if (ip_mac_cache_top >= IP_MAC_CACHE_LENGTH) {
        // Overwrite old entries when the cache fills up, good enough for now.
        ip_mac_cache_top = 0; 
        //ESP_LOGW(TAG, "The ip_mac_cache array overflowed and will now overwrite old entries!");
    }
    memcpy(ip_mac_cache[ip_mac_cache_top], ip.addr, 4);
    memcpy(ip_mac_cache[ip_mac_cache_top]+4, mac.addr, 6);
    ip_mac_cache_top = (ip_mac_cache_top + 1)%IP_MAC_CACHE_LENGTH;

    struct arp_pending_t* p = find_pending(ip.addr);
    if (p == NULL){
        return;
    }
    for (uint8_t i = 0; i < p->waiting_task_count; i++){
        xTaskNotifyGive(p->waiting_tasks[i]);
    }

    free_pending(p);
}

// Returns ESP_OK if found, and ESP_FAIL if its not cached
esp_err_t get_cached_mac_addr(ip_addr_t* ip, mac_addr_t* out_mac){
    for (int i = 0; i<=IP_MAC_CACHE_LENGTH-1; i++){
        if (memcmp(ip->addr, ip_mac_cache[i], 4) == 0){
            memcpy(out_mac->addr, ip_mac_cache[i]+4, 6);
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

/**
 * 
 * @returns ESP_OK if found, ESP_FAIL otherwise.
 */
esp_err_t send_arp_request(ip_addr_t* ip){
    // Check cache first
    //if (get_mac_addr(ip, out_mac) == ESP_OK){
    //    return ESP_OK;
    //}

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
    
    uint16_t arp_request_len = p - arp_request_buf;
    
    /* Transmit via Ethernet layer */
    esp_err_t result = eth_tx(BROADCAST_MAC, self_mac_addr.addr, htons(0x0806), 
                              arp_request_buf, arp_request_len);
    //if (result != ESP_OK) {
    //    ESP_LOGI(TAG, "Failed to send ARP request");
    //}

    return result;
}

void arp_set_local_info(const mac_addr_t *mac, const ip_addr_t *ip){
    memcpy(self_ip_addr.addr, ip->addr, 4);
    memcpy(self_mac_addr.addr, mac->addr, 6);
}

void arp_init(void){
    if (arp_request_queue == NULL) {
        arp_request_queue = xQueueCreate(4, sizeof(struct arp_request_t));
        if (arp_request_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create arp_request_queue");
        }
    }
}

void arp_reply(uint8_t mac_addr[6], uint8_t ip_addr[4]){
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
    
    /* Operation (2 bytes, network byte order) */
    uint16_t operation = htons(ARP_OP_REPLY);
    memcpy(p, &operation, 2); p += 2;
    
    /* Sender hardware address (6 bytes) */
    memcpy(p, self_mac_addr.addr, 6); p += 6;
    
    /* Sender protocol address (4 bytes) */
    memcpy(p, self_ip_addr.addr, 4); p += 4;
    
    /* Target hardware address (6 bytes) */
    memcpy(p, mac_addr, 6); p += 6;
    
    /* Target protocol address (4 bytes) */
    memcpy(p, ip_addr, 4); p += 4;
    
    uint16_t arp_reply_len = p - arp_reply_buf;
    
    /* Transmit via Ethernet layer */
    esp_err_t result = eth_tx(mac_addr, self_mac_addr.addr, htons(ARP_ETHERTYPE), 
                              arp_reply_buf, arp_reply_len);
    if (result != ESP_OK) {
        ESP_LOGI(TAG, "Failed to send ARP reply");
    }
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

    if (memcmp(buffer->dst_ip, self_ip_addr.addr, 4) != 0){
        // discard
        ESP_LOGI(TAG, "ARP request was not for our ip");
        return;
    }

    if (ntohs(buffer->operation) == ARP_OP_REPLY){
        // handle receiving a reply
        ESP_LOGI(TAG, "ARP reply detected for us, from IP: %s", readable_src_ip);
        ip_addr_t src_ip = {
            .addr = buffer->src_ip
        };
        mac_addr_t src_mac = {
            .addr = buffer->src_mac
        };
        add_ip_mac_cache_entry(src_ip, src_mac);
        return;
    }

    if (ntohs(buffer->operation) == ARP_OP_REQUEST){
        // handle receiving a request
        ESP_LOGI(TAG, "ARP request detected for us, from IP: %s", readable_src_ip);
        arp_reply(buffer->src_mac, buffer->src_ip);
    } 
}

struct arp_pending_t* find_pending(uint8_t ip[4]){
    for (uint8_t i = 0; i<MAX_PENDING_IPS; i++){
        struct arp_pending_t* p = &arp_pending_tasks[i];
        if (p->waiting_task_count == 0){
            continue;
        }
        if (memcmp(p->ip, ip, 4) == 0){
            return p;
        }
    }
    return NULL;
}

struct arp_pending_t* alloc_pending(uint8_t ip[4]){
    int8_t free_index = -1;
    for (int8_t i = 0; i<MAX_PENDING_IPS; i++){
        // Assume free'd arp_pending_tasks have zeroed waiting_task_count
        if (arp_pending_tasks[i].waiting_task_count == 0){
            free_index = i;
            break;
        }
    }
    if (free_index == -1){
        return NULL;
    }

    struct arp_pending_t pending = {
        .waiting_task_count = 0,
    };

    memcpy(pending.ip, ip, 4);

    arp_pending_tasks[free_index] = pending;

    return &arp_pending_tasks[free_index];
}

void free_pending(struct arp_pending_t* pending){
    // Assume free'd arp_pending_tasks have zeroed waiting_task_count
    pending->waiting_task_count = 0;
}

void arp_task(){
    struct arp_request_t request;
    while (1){
        xQueueReceive(arp_request_queue, &request, portMAX_DELAY);

        struct arp_pending_t* p = find_pending(request.ip);
        if (p == NULL){
            // First request for this ip
            p = alloc_pending(request.ip);
            if (p == NULL){
                ESP_LOGW(TAG, "Unable to allocate a new pending arp request");
                xTaskNotifyGive(request.task); // Simply continue the task to prevent freezing the program
                continue;
            }

            send_arp_request(request.ip);
        }
        if (p->waiting_task_count >= MAX_PENDING_PER_IP){
            ESP_LOGW(TAG, "Unable to add task to pending arp request");
            xTaskNotifyGive(request.task); // Simply continue the task to prevent freezing the program
            continue;
        }
        
        p->waiting_tasks[p->waiting_task_count++] = request.task;
    }
}

esp_err_t get_mac_addr(ip_addr_t ip, mac_addr_t *out_mac){
    struct arp_request_t request = {
        .task = xTaskGetCurrentTaskHandle(),
    };
    memcpy(request.ip, ip.addr, 4);
    xQueueSend(arp_request_queue, &request, portMAX_DELAY);

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    
    esp_err_t result = get_cached_mac_addr(&ip, out_mac);
    if (result != ESP_OK){
        ESP_LOGW(TAG, "Error getting mac address. ARP request resolved without a MAC.");
    }

    return result;
}