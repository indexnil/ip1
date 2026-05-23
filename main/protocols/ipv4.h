#include <stdint.h>
#include "../utility/net_types.h"

#define IPV4_ETHERTYPE 0x800

struct ipv4_transmit_params_t {
    ipv4_addr_t src_ipv4_addr;
    ipv4_addr_t dst_ipv4_addr;
    uint8_t protocol;
    void* data;
    uint16_t data_len;
};

void ipv4_to_readable(char* buffer, const uint8_t address[4]);

void ipv4_set_self_net_context(const struct net_context_t *net);
void ipv4_init();

void ipv4_input(uint8_t* buffer, uint16_t buffer_len);

uint16_t ipv4_checksum(const uint8_t *header, uint8_t header_len_words);

esp_err_t send_ipv4_packet(struct ipv4_transmit_params_t* params);