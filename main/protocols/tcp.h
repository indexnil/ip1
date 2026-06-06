#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "../utility/net_types.h"

#define TCP_PROTOCOL_NUMBER 6

enum tcp_state_t {
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    CLOSE_WAIT,
    LAST_ACK,
};

struct tcp_flags_t {
    uint8_t FIN : 1;
    uint8_t SYN : 1;
    uint8_t RST : 1;
    uint8_t PSH : 1;
    uint8_t ACK : 1;
    uint8_t URG : 1;
    uint8_t ECE : 1;
    uint8_t CWR : 1;
};

struct tcp_transmit_params_t {
    ipv4_addr_t dst_ipv4_addr;

    // From tcp_header_t
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    union {
        uint8_t control; 
        struct tcp_flags_t flags;
    };
    uint16_t window;

    // Extra
    void* data;
    uint16_t data_len;
};

struct tcp_port_t {
    uint16_t self_port_num;
    uint16_t other_port_num;
    ipv4_addr_t other_ipv4_addr;
    enum tcp_state_t state;
    uint32_t ack_num;
    uint32_t seq_num;
    uint16_t window;
    void (*callback)(void* data, uint16_t data_len, ipv4_addr_t* src_addr);
};

struct tcp_header_t {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t reserved : 4;
    uint8_t dOffset : 4;
    union {
        uint8_t control; 
        struct tcp_flags_t flags;
    };
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed));

struct ipv4_psuedo_header_t {
    uint8_t src_ipv4_addr[4];
    uint8_t dst_ipv4_addr[4];
    uint8_t zero;
    uint8_t protocol;
    uint16_t tcp_len;
} __attribute__((packed));

void input_tcp(void* buffer, uint16_t length, ipv4_addr_t* src_addr);

esp_err_t tcp_send(struct tcp_transmit_params_t* params);

esp_err_t tcp_create_port(struct tcp_port_t* port);

void tcp_set_self_net_context(const struct net_context_t *net);

void init_tcp();