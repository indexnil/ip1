#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "../utility/net_types.h"

struct udp_transmit_params_t {
    uint16_t src_port;
    uint16_t dst_port;
    ipv4_addr_t dst_ipv4_addr;
    void* data;
    uint16_t data_len;
};

struct udp_port_t {
    uint16_t port_number;
    void (*callback)(void* data, uint16_t data_len, ipv4_addr_t* src_addr);
};

struct udp_header_t {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;

} __attribute__((packed));

void udp_input(void* buffer, ipv4_addr_t* src_addr);

esp_err_t udp_send(struct udp_transmit_params_t* params);

esp_err_t udp_create_port(struct udp_port_t* port);

void udp_set_self_net_context(const struct net_context_t *net);

void udp_init();