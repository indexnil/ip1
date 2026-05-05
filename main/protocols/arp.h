#include "../utility/net_types.h"

#define ARP_ETHERTYPE 0x0806

struct arp_data_t {
    uint16_t hardware_type; // only deals with MAC
    uint16_t protocol_type; // only deals with IPv4
    uint8_t hardware_length;
    uint8_t protocol_length;
    uint16_t operation;
    uint8_t src_mac[6];
    uint8_t src_ip[4];
    uint8_t dst_mac[6];
    uint8_t dst_ip[4];
};

esp_err_t get_mac_addr(ip_addr_t* ip, mac_addr_t* out_mac);
esp_err_t request_mac_addr(ip_addr_t* ip, mac_addr_t* out_mac);

void arp_set_local_info(const mac_addr_t *mac, const ip_addr_t *ip);
void arp_input(struct arp_data_t* buffer, uint16_t len);
