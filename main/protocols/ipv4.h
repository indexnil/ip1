#include <stdint.h>

#define IPV4_ETHERTYPE 0x800

extern uint8_t ipv4_address[4];

void ipv4_to_readable(char* buffer, const uint8_t address[4]);