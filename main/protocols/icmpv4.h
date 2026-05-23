#include <stdint.h>
#include "../utility/net_types.h"

#define ICMP_PROTOCOL_NUMBER 1

void icmpv4_input(ipv4_addr_t src_ipv4_addr, uint8_t* buffer, uint16_t buffer_len);

void icmpv4_set_self_net_context(const struct net_context_t *net);