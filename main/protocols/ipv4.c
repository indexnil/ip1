#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "ipv4.h"
#include "eth.h"
#include "arp.h"
#include "tcp.h"
#include "icmpv4.h"
#include "esp_log.h"
#include "esp_err.h"
#include "../utility/endian.h"
#include "../utility/net_types.h"
#include "../utility/checksum.h"

#define HEADER_LENGTH 20
#define TAG "ipv4"

struct net_context_t self_net;

QueueHandle_t ipv4_transmit_queue = NULL;

void ipv4_set_self_net_context(const struct net_context_t *net){
    memcpy(&self_net, net, sizeof(struct net_context_t));
}

// This will transfer ownership of the data buffer to the ipv4 task.
// Ensure that the data is not freed (eg on the stack), as another task will be reading it later.
// Will return ESP_FAIL if the queue is full, and it wont wait at all.
esp_err_t send_ipv4_packet(struct ipv4_transmit_params_t* params){
    if (ipv4_transmit_queue == NULL){
        free(params->data);
        return ESP_FAIL;
    }
    BaseType_t result = xQueueSend(ipv4_transmit_queue, params, 0);
    if (result != pdTRUE){
        free(params->data);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t internal_send_ipv4_packet(struct ipv4_transmit_params_t* params){
    uint16_t packet_len = HEADER_LENGTH + params->data_len;
    uint8_t* packet_buf = malloc(packet_len);
    if (packet_buf == NULL){
        free(params->data);
        return ESP_ERR_NO_MEM;
    }
    uint8_t* p = packet_buf;

    // Version and header length
    uint8_t version = 4;
    uint8_t ihl = HEADER_LENGTH/4;
    *p++ = (version << 4) | ihl;

    // Differentiated services + congestion notification
    *p++ = 0; 

    // Total length
    uint16_t total_length = htons(HEADER_LENGTH+params->data_len);
    memcpy(p, &total_length, 2);
    p+=2;

    // Identification
    *p++ = 0; 
    *p++ = 0; 

    // Fragment offset (all zeroes) and the flags
    uint16_t fragmentation = htons(0b010 << 13);
    memcpy(p, &fragmentation, 2);
    p+=2;

    // Time to live
    *p++ = 128;

    // Protocol
    *p++ = params->protocol;

    // Header checksum (0 for now)
    *p++ = 0; 
    *p++ = 0; 

    // Source and destination ip address
    memcpy(p, params->src_ipv4_addr.bytes, 4);
    p+=4;
    memcpy(p, params->dst_ipv4_addr.bytes, 4);
    p+=4;

    // Go back and set header checksum
    uint16_t checksum = htons(net_checksum(packet_buf, HEADER_LENGTH));
    memcpy(p-10, &checksum, 2);

    // Copy data into packet
    memcpy(p, params->data, params->data_len);

    free(params->data);

    mac_addr_t dst_mac_addr;
    esp_err_t result = arp_get_mac_addr(&(params->dst_ipv4_addr), &dst_mac_addr);
    if (result != ESP_OK){
        // Warn here so we can see a more detailed reason why it failed.
        ESP_LOGW(TAG,"Failed to get mac address via ARP when sending a packet");
        free(packet_buf);
        return result;
    }
    //ESP_LOGI(TAG, "Sending ipv4 packet of %d bytes to data layer", HEADER_LENGTH+params->data_len);
    result = eth_tx(dst_mac_addr.bytes, self_net.mac.bytes, htons(IPV4_ETHERTYPE), packet_buf, packet_len);
    free(packet_buf);
    return result;
}

void ipv4_input(uint8_t* buffer, uint16_t buffer_len){
    if (buffer_len < sizeof(struct ipv4_header_t)) {
        return;
    }

    struct ipv4_header_t header;
    memcpy(&header, buffer, sizeof(struct ipv4_header_t));

    uint8_t version = header.version_and_ihl >> 4;
    if (version != 4){
        ESP_LOGI(TAG, "Invalid packet version");
        return;
    }

    uint8_t IHL = header.version_and_ihl & 0x0F; // in words
    uint16_t header_len = IHL*4; // convert to bytes

    uint16_t total_len = ntohs(header.total_length);
    if (total_len > buffer_len){
        // Buffer len includes ethernet padding, so long as we are under that we are fine
        ESP_LOGI(TAG, "Packet has invalid length field. Total length field says %d but esp says %d", total_len, buffer_len);
        return;
    }

    if (net_checksum(buffer, IHL*4) != 0){
        // Malformed header
        ESP_LOGI(TAG, "Invalid packet checksum");
        return;
    }

    uint8_t protocol = header.protocol;

    //ESP_LOGI(TAG, "Got packet with protocol %d", protocol);

    if (memcmp(header.dst_ipv4_addr, self_net.ip.bytes, sizeof(header.dst_ipv4_addr)) != 0){
        //ESP_LOGI(TAG, "Packet not destined for us");
        return;
    }

    
    if (header_len != sizeof(struct ipv4_header_t)) {
        // Dont accept any packets with options or malformed ones
        ESP_LOGI(TAG, "Invalid packet header length");
        return;
    }

    uint16_t data_len = total_len-header_len;

    ipv4_addr_t* src_ipv4_addr = (ipv4_addr_t*)header.src_ipv4_addr;

    switch (protocol)
    {
        case ICMP_PROTOCOL_NUMBER:
            //icmpv4_input(src_ipv4_addr, buffer + (IHL * 4), total_len - (IHL * 4));
            // icmpv4 is special because it relies on the ipv4 packet header fields as well
            icmpv4_input(buffer, total_len);
            break;
        case TCP_PROTOCOL_NUMBER:
            input_tcp(buffer+header_len, data_len, src_ipv4_addr);
            break;
        default:
            break;
    }
}

void ipv4_task(){
    struct ipv4_transmit_params_t params;
    while (1){
        BaseType_t result = xQueueReceive(ipv4_transmit_queue, &params, portMAX_DELAY);
        if (result == pdTRUE){
            //ESP_LOGI(TAG, "TX task stack high water mark: %u", (unsigned) uxTaskGetStackHighWaterMark(NULL));
            //ESP_LOGI(TAG,"Sending ipv4 packet!");
            internal_send_ipv4_packet(&params);
        }
    }
}

void ipv4_init(){
    if (ipv4_transmit_queue == NULL) {
        ipv4_transmit_queue = xQueueCreate(4, sizeof(struct ipv4_transmit_params_t));
        if (ipv4_transmit_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create ipv4_transmit_queue");
            return;
        }
    }

    BaseType_t xReturned = xTaskCreate(ipv4_task, "ipv4 task", 1500, NULL, 1, NULL);
    if (xReturned  != pdPASS){
        // If not pass, then xReturned is garuanteed by the docs to be a errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY.
        ESP_LOGE(TAG, "Could not create ipv4 task. Not enough memory!");
        return;
    }

    init_tcp();
}