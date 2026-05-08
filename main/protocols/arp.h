#include "../utility/net_types.h"
#include "task.h"

#define ARP_ETHERTYPE 0x0806
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2
#define MAX_PENDING_PER_IP 8
#define MAX_PENDING_IPS 8

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

esp_err_t get_mac_addr(ip_addr_t ip, mac_addr_t *out_mac);

struct arp_request_t {
    uint8_t ip[4];
    TaskHandle_t task;
};

struct arp_pending_t {
    uint8_t ip[4];
    TaskHandle_t waiting_tasks[MAX_PENDING_PER_IP];
    uint8_t waiting_task_count;
};

void arp_init(void);

void arp_set_local_info(const mac_addr_t *mac, const ip_addr_t *ip);
void arp_input(struct arp_data_t* buffer, uint16_t len);
