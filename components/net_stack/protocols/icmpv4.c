#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "icmpv4.h"
#include "ipv4.h"
#include "checksum.h"
#include "../utility/net_types.h"
#include "../utility/endian.h"

#define TAG "icmpv4"

struct icmpv4_header_t {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq_num;
};

struct net_context_t self_net;

void icmpv4_set_self_net_context(const struct net_context_t *net){
    memcpy(&self_net, net, sizeof(struct net_context_t));
}

esp_err_t send_icmpv4_ping_reply(ipv4_addr_t dst_ipv4_addr, uint16_t id, uint16_t seq_num, void* data, uint16_t data_len){
    ESP_LOGI(TAG, "Sending ping reply with %d data bytes!", data_len);
    ESP_LOGI(TAG, "RX-side stack high water mark: %u", (unsigned) uxTaskGetStackHighWaterMark(NULL));

    void* icmpv4_packet = malloc(sizeof(struct icmpv4_header_t) + data_len);
    if (icmpv4_packet == NULL){
        return ESP_ERR_NO_MEM;
    }

    struct icmpv4_header_t* header = icmpv4_packet;
    header->type = 0;
    header->code = 0;
    header->checksum = 0;
    header->id = htons(id);
    header->seq_num = htons(seq_num);

    memcpy(icmpv4_packet+sizeof(struct icmpv4_header_t), data, data_len);

    header->checksum = htons(net_checksum(icmpv4_packet, sizeof(struct icmpv4_header_t) + data_len));

    struct ipv4_transmit_params_t params = {
        .src_ipv4_addr = self_net.ip,
        .dst_ipv4_addr = dst_ipv4_addr,
        .protocol = ICMP_PROTOCOL_NUMBER,
        .data = icmpv4_packet,
        .data_len = sizeof(struct icmpv4_header_t) + data_len,
    };
    return send_ipv4_packet(&params);
}

// Takes a full ipv4 packet as input
void icmpv4_input(uint8_t* buffer, uint16_t buffer_len){
    if (buffer_len < sizeof(struct ipv4_header_t)+sizeof(struct icmpv4_header_t)){
        ESP_LOGI(TAG, "Invalid buffer length, got %d expected %d", buffer_len, sizeof(struct ipv4_header_t)+sizeof(struct icmpv4_header_t));
        return;
    }

    struct ipv4_header_t ipv4_header;
    memcpy(&ipv4_header, buffer, sizeof(struct ipv4_header_t));

    struct icmpv4_header_t header;
    memcpy(&header, buffer+sizeof(struct ipv4_header_t), sizeof(header));

    ipv4_addr_t src_ipv4_addr = {};
    memcpy(src_ipv4_addr.bytes, ipv4_header.src_ipv4_addr, 4);

    if (header.type == 8 && header.code == 0){
        // Request used in ping
        void* payload_pointer = buffer+sizeof(struct ipv4_header_t)+sizeof(struct icmpv4_header_t);
        uint16_t payload_len = ntohs(ipv4_header.total_length)
            - sizeof(struct ipv4_header_t)
            - sizeof(struct icmpv4_header_t);
        if (send_icmpv4_ping_reply(
            src_ipv4_addr, 
            ntohs(header.id), 
            ntohs(header.seq_num), 
            payload_pointer, 
            payload_len
        ) != ESP_OK){
            ESP_LOGW(TAG, "Failed to send ping reply");
        };
        return;
    }

    ESP_LOGI(TAG, "Not responding to icmpv4, not accepted type or code");

    return;
}