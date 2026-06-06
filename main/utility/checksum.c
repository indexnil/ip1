#include <stdint.h>
#include <string.h>
#include "endian.h"

// Compute the standard internet checksum over a byte buffer.
uint16_t net_checksum(const uint8_t *header, uint16_t header_len_bytes){
    uint32_t sum = 0;

    uint16_t word_count = header_len_bytes / 2;
    for (uint16_t i = 0; i < word_count; i++){
        uint16_t word = 0;
        memcpy(&word, header + (i * 2), sizeof(word));
        sum += ntohs(word);
    }

    if ((header_len_bytes & 1) != 0){
        uint16_t word = 0;
        memcpy(&word, header + (word_count * 2), 1);
        sum += ntohs(word);
    }

    // Fold the 32 bit sum into 16 bits
    uint16_t upper = sum >> 16;
    while (upper != 0){
        sum = sum & 0xFFFF;
        sum = sum + (upper);
        upper = sum >> 16;
    }

    return ~sum;
}