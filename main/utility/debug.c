#include <string.h>
#include <stdio.h>
#include <stdint.h>

void ipv4_to_readable(char* buffer, const uint8_t address[4]){
    sprintf(buffer, "%u.%u.%u.%u", (unsigned)address[0], (unsigned)address[1], (unsigned)address[2], (unsigned)address[3]);
}

void mac_to_readable(char* buffer, const uint8_t address[6]){
    sprintf(buffer, "%x:%x:%x:%x:%x:%x", (unsigned)address[0], (unsigned)address[1], (unsigned)address[2], (unsigned)address[3], (unsigned)address[4], (unsigned)address[5]);
}