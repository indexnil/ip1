#include <stdint.h>

/**
 * Network byte order conversion helpers.
 * 
 * Network byte order is big-endian (most significant byte first).
 * Most CPUs (including Xtensa in ESP8266) are little-endian.
 * 
 * Use these when reading/writing multi-byte values from/to network packets.
 */

/**
 * Convert 16-bit value from network byte order (big-endian) to host byte order.
 * Use when reading a uint16_t field from a packet (e.g., ethertype, port numbers).
 */
static inline uint16_t ntohs(uint16_t net_value) {
    return ((net_value & 0xFF) << 8) | ((net_value >> 8) & 0xFF);
}

/**
 * Convert 32-bit value from network byte order (big-endian) to host byte order.
 * Use when reading a uint32_t field from a packet (e.g., IP addresses, sequence numbers).
 */
static inline uint32_t ntohl(uint32_t net_value) {
    return ((net_value & 0xFF) << 24) | 
           (((net_value >> 8) & 0xFF) << 16) |
           (((net_value >> 16) & 0xFF) << 8) |
           ((net_value >> 24) & 0xFF);
}

/**
 * Convert 16-bit value from host byte order to network byte order (big-endian).
 * Use when writing a uint16_t field into a packet.
 */
static inline uint16_t htons(uint16_t host_value) {
    return ntohs(host_value);  // Byte-swapping is symmetric
}

/**
 * Convert 32-bit value from host byte order to network byte order (big-endian).
 * Use when writing a uint32_t field into a packet.
 */
static inline uint32_t htonl(uint32_t host_value) {
    return ntohl(host_value);  // Byte-swapping is symmetric
}
