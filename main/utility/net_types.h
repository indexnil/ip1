#pragma once

#include <stdint.h>

typedef struct {
    uint8_t addr[6];
} mac_addr_t;

typedef struct {
    uint8_t addr[4];
} ip_addr_t;


struct net_context_t {
    mac_addr_t mac;
    ip_addr_t ip;
};