#pragma once
#include <stdint.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "esp_err.h"
#include "../utility/net_types.h"

#define TCP_PROTOCOL_NUMBER 6
#define MAX_UNACKED 8

enum tcp_state_t {
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT_1,
    CLOSE_WAIT,
    FIN_WAIT_2,
    LAST_ACK,
    TIME_WAIT,
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
    union {
        uint8_t control; 
        struct tcp_flags_t flags;
    };
    bool isRetransmission;

    // Extra
    void* data;
    uint16_t data_len;
};

struct tcp_unacked_t {
    uint32_t seq_num;   // so you know what the ACK is acknowledging
    bool in_use;
    uint8_t* data;      // heap-allocated payload
    uint16_t data_len;
    TickType_t sent_at;   // xTaskGetTickCount() at send time
    union {
        uint8_t control; 
        struct tcp_flags_t flags;
    };
};

struct tcp_port_t {
    uint16_t self_port_num;
    uint16_t other_port_num;
    ipv4_addr_t other_ipv4_addr;
    enum tcp_state_t state;
    TickType_t entered_time_wait_at;   // xTaskGetTickCount()
    uint32_t ack_num;
    uint32_t seq_num;
    uint16_t self_window;
    uint16_t other_window;

    struct tcp_unacked_t unacked[MAX_UNACKED];
    SemaphoreHandle_t unacked_slots;

    void (*on_receive)(void* data, uint16_t data_len);
    void (*on_connect)(uint16_t other_port_num, ipv4_addr_t* other_ipv4_addr);
    void (*on_disconnect)();
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


esp_err_t input_tcp(void* buffer, uint16_t length, ipv4_addr_t* src_addr);

esp_err_t tcp_send(uint16_t src_port_num, uint16_t dst_port_num, ipv4_addr_t* dst_ipv4_addr, void* data, uint16_t len, TickType_t timeout);

esp_err_t tcp_connect_port(uint16_t src_port_num, uint16_t dst_port_num, ipv4_addr_t* dst_ipv4_addr, 
    void (*on_receive)(void* data, uint16_t data_len), 
    void (*on_connect)(uint16_t other_port_num, ipv4_addr_t* other_ipv4_addr), 
    void (*on_disconnect)()
);
esp_err_t tcp_listen_port(uint16_t src_port_num, 
    void (*on_receive)(void* data, uint16_t data_len), 
    void (*on_connect)(uint16_t other_port_num, ipv4_addr_t* other_ipv4_addr),
    void (*on_disconnect)()
);
esp_err_t tcp_close_port(uint16_t src_port_num, uint16_t dst_port_num, ipv4_addr_t* dst_ipv4_addr);

void tcp_set_self_net_context(const struct net_context_t *net);

esp_err_t init_tcp();