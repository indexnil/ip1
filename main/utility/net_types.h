#pragma once

#include <stdint.h>

typedef struct {
    uint8_t bytes[6];
} mac_addr_t;

typedef struct {
    uint8_t bytes[4];
} ipv4_addr_t;


struct net_context_t {
    mac_addr_t mac;
    ipv4_addr_t ip;
};