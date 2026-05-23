#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "icmpv4.h"
#include "ipv4.h"
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

esp_err_t send_icmpv4_ping_reply(ipv4_addr_t dst_ipv4_addr, uint16_t id, uint16_t seq_num){
    //ESP_LOGI(TAG, "Sending ping reply!");
    // TODO verify malloc succeeded
    struct icmpv4_header_t* header = malloc(sizeof(struct icmpv4_header_t));
    header->type = 0;
    header->code = 0;
    header->checksum = 0;
    header->id = htons(id);
    header->seq_num = htons(seq_num);
    header->checksum = htons(ipv4_checksum((uint8_t*)header, sizeof(struct icmpv4_header_t)/2));
    struct ipv4_transmit_params_t params = {
        .src_ipv4_addr = self_net.ip,
        .dst_ipv4_addr = dst_ipv4_addr,
        .protocol = ICMP_PROTOCOL_NUMBER,
        .data = header,
        .data_len = sizeof(struct icmpv4_header_t),
    };
    return send_ipv4_packet(&params);
}

void icmpv4_input(ipv4_addr_t src_ipv4_addr, uint8_t* buffer, uint16_t buffer_len){
    ESP_LOGI(TAG, "Got input");

    if (buffer_len != sizeof(struct icmpv4_header_t)){
        //return;
    }

    struct icmpv4_header_t header;
    memcpy(&header, buffer, sizeof(header));

    if (header.type == 8 && header.code == 0){
        // Request used in ping
        if (send_icmpv4_ping_reply(src_ipv4_addr, ntohs(header.id), ntohs(header.seq_num)) != ESP_OK){
            ESP_LOGW(TAG, "Failed to send ping reply");
        };
        return;
    }

    ESP_LOGI(TAG, "Not responding to icmpv4, not accepted type or code");

    return;
}