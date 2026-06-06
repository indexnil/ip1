#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "../utility/net_types.h"
#include "../utility/endian.h"
#include "../utility/checksum.h"
#include "tcp.h"
#include "ipv4.h"
#include <stdlib.h>
#include <string.h>

#define TAG "tcp"

const uint8_t FIN = 0b1;
const uint8_t SYN = 0b10;
const uint8_t ACK = 0b10000;

QueueHandle_t port_queue = NULL;
//QueueHandle_t send_queue = NULL;
QueueSetHandle_t tcp_task_queue_set = NULL;

uint8_t max_ports = 8;
struct tcp_port_t ports[8];

struct net_context_t self_net;

struct tcp_port_t* find_port(uint16_t self_port_number){
    for (uint8_t i = 0; i<max_ports; i++){
        if (ports[i].self_port_num == self_port_number){
            return &ports[i];
        }
    }

    return NULL;
}

// This takes ownership of params->data and eventually frees it
esp_err_t tcp_send(struct tcp_transmit_params_t* params){
    uint16_t datagram_length = sizeof(struct tcp_header_t) + params->data_len;
    uint8_t* datagram = malloc(datagram_length + sizeof(struct ipv4_psuedo_header_t));
    if (datagram == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // First apply the psuedo ipv4 header for the checksum
    uint8_t* psuedo_header_start = datagram;
    struct ipv4_psuedo_header_t* psuedo_header = (struct ipv4_psuedo_header_t*)psuedo_header_start;
    memcpy(psuedo_header->src_ipv4_addr, self_net.ip.bytes, 4);
    memcpy(psuedo_header->dst_ipv4_addr, params->dst_ipv4_addr.bytes, 4);
    psuedo_header->zero = 0;
    psuedo_header->protocol = TCP_PROTOCOL_NUMBER;
    psuedo_header->tcp_len = htons(datagram_length);

    datagram+=sizeof(struct ipv4_psuedo_header_t);

    // Cast to a struct header for easier writing
    struct tcp_header_t* header = (struct tcp_header_t*)datagram;
    header->src_port = htons(params->src_port);
    header->dst_port = htons(params->dst_port);
    header->seq_num = htonl(params->seq_num);
    header->ack_num = htonl(params->ack_num);
    header->dOffset = sizeof(struct tcp_header_t) / 4; // Should be an integer
    header->reserved = 0;
    header->control = params->control;
    header->window = htons(params->window);
    header->checksum = 0;
    header->urgent_ptr = 0;
    if (params->data != NULL && params->data_len > 0) {
        memcpy(datagram + sizeof(struct tcp_header_t), params->data, params->data_len);
        free(params->data);
        params->data = NULL;
    }

    header->checksum = htons(net_checksum(psuedo_header_start, datagram_length + sizeof(struct ipv4_psuedo_header_t)));

    // Shift the datagram over the pseudo-header so psuedo_header_start is the malloc'd
    // pointer we hand to ipv4 — free() requires the original allocation pointer.
    memmove(psuedo_header_start, datagram, datagram_length);

    struct ipv4_transmit_params_t ipv4_params = {
        .src_ipv4_addr = self_net.ip,
        .dst_ipv4_addr = params->dst_ipv4_addr,
        .protocol = TCP_PROTOCOL_NUMBER,
        .data = psuedo_header_start,
        .data_len = datagram_length,
    };

    return send_ipv4_packet(&ipv4_params);
}

void input_tcp(void* buffer, uint16_t length, ipv4_addr_t* src_ipv4_addr){
    struct tcp_header_t* header = (struct tcp_header_t*)buffer;

    uint16_t header_length = header->dOffset*4;
    if (header_length < sizeof(struct tcp_header_t)){
        ESP_LOGW(TAG, "Received datagram with an invalid header_length (doffset) of %d, expected at least %d", header_length, sizeof(struct tcp_header_t));
        return;
    }

    ESP_LOGI(TAG, "Incoming source port %u", ntohs(header->src_port));
    ESP_LOGI(TAG, "Incoming sequence number %u", ntohl(header->seq_num));

    uint16_t dst_port = ntohs(header->dst_port);

    struct tcp_port_t* port = find_port(dst_port);
    if (port == NULL){
        ESP_LOGW(TAG, "No port found with port number %u", dst_port);
        return;
    }

    // TCP state logic begins here
    switch (port->state){
        case CLOSED:
            ESP_LOGW(TAG, "Received input to a closed port");
            break;
        case LISTEN:
            if (header->flags.SYN != 1){
                ESP_LOGW(TAG, "Received segment at port listen state with no syn flag");
                break;
            }

            ESP_LOGI(TAG, "Syn received!");

            port->state = SYN_RECEIVED;
            port->other_ipv4_addr = *src_ipv4_addr;
            port->other_port_num = ntohs(header->src_port);
            port->ack_num = ntohl(header->seq_num) + 1; // + 1 because syns contain 1 byte of data
            port->seq_num = 0; // Ensure we start at seq 0 for easy debugging
            port->window = 1500;

            struct tcp_transmit_params_t response = {
                .dst_ipv4_addr = *src_ipv4_addr,
                .src_port = port->self_port_num,
                .dst_port = port->other_port_num,
                .ack_num = port->ack_num,
                .seq_num = port->seq_num,
                .control = SYN + ACK,
                .window = port->window,
                .data = NULL,
                .data_len = 0,
            };

            tcp_send(&response);

            // We sent one byte just now
            port->seq_num+=1;

            break;
        case SYN_RECEIVED:
            if (header->flags.ACK != 1){
                ESP_LOGW(TAG, "Received segment at port SYN_RECEIVED state with no ACK flag");
                break;
            }

            // The new seq num should be what we expect (the previous seq num + the previous data)
            if (ntohl(header->seq_num) != port->ack_num){
                ESP_LOGW(TAG, "Unexpected seq number in SYN_RECEIVED");
                break;
            }

            // They should ack all the data we sent (1 byte)
            if (ntohl(header->ack_num) != port->seq_num){
                ESP_LOGW(TAG, "Unexpected ack number in SYN_RECEIVED");
                break;
            }

            port->state = ESTABLISHED;

            ESP_LOGI(TAG, "Established connection!");

            break;
        case ESTABLISHED:
            if (header->flags.FIN){
                // Begin closing
                port->ack_num+=1; // Acknowledge the FIN flag
                struct tcp_transmit_params_t response = {
                    .dst_ipv4_addr = *src_ipv4_addr,
                    .src_port = port->self_port_num,
                    .dst_port = port->other_port_num,
                    .ack_num = port->ack_num,
                    .seq_num = port->seq_num,
                    .control = ACK + FIN,
                    .window = port->window,
                    .data = NULL, // Due to the FIN flag, its maybe not strictly needed to actually send data though.
                    .data_len = 0,
                };
                port->seq_num+=1; // Due to us sending a FIN flag
                
                tcp_send(&response);

                port->state = LAST_ACK; // Skip the CLOSE-WAIT state because we merged the ack + fin
                ESP_LOGI(TAG, "Port entered LAST_ACK state");
            }
            break;
        case LAST_ACK:
            if (header->flags.ACK == 0){
                ESP_LOGW(TAG, "No ACK flag received in LAST_ACK state");
                break;
            }

            // Ensure the sequence number is what it should be
            if (ntohl(header->seq_num) != port->ack_num){
                ESP_LOGW(TAG, "Unexpected sequence number received");
                break;
            }

            // Ensure they have acked our data
            if (ntohl(header->ack_num) != port->seq_num){
                ESP_LOGW(TAG, "Our data was not acknowledged");
                break;
            }

            port->state = CLOSED;
            ESP_LOGI(TAG, "Closed port!");

            break;
        default:
            break;
    }


    // Only if this is a data packet, we also want to defer this to another task instead
    //port->callback(buffer + header_length, length - header_length, src_ipv4_addr);
}

esp_err_t tcp_create_port(struct tcp_port_t* port){
    //if (port->callback == NULL){
    //    return ESP_ERR_INVALID_ARG;
    //}
    BaseType_t result = xQueueSend(port_queue, port, 0);
    if (result != pdTRUE){
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void tcp_set_self_net_context(const struct net_context_t *net){
    memcpy(&self_net, net, sizeof(struct net_context_t));
}

// Overwrites ports with self_port_num = 0, consider those to be unused
esp_err_t allocate_port(struct tcp_port_t* port){
    for (uint8_t i = 0; i<max_ports; i++){
        if (ports[i].self_port_num == 0){
            ports[i] = *port;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

void tcp_task(void* pvParameters){
    struct tcp_port_t port_buf;
    //struct tcp_transmit_params_t tx_buf;
    while (1) {
        QueueHandle_t queue = xQueueSelectFromSet(tcp_task_queue_set, portMAX_DELAY);
        if (queue == port_queue){
            BaseType_t result = xQueueReceive(port_queue, &port_buf, 0);
            if (result != pdTRUE){
                continue;
            }

            esp_err_t ares = allocate_port(&port_buf);
            if (ares != ESP_OK){
                ESP_LOGE(TAG, "Unable to allocate port");
                continue;
            }
            ESP_LOGI(TAG, "Allocated port %d", port_buf.self_port_num);
        } /*else if (queue == send_queue){
            BaseType_t result = xQueueReceive(send_queue, &tx_buf, 0);
            if (result != pdTRUE){
                continue;
            }

            esp_err_t sres = tcp_send_internal(&tx_buf);
            if (sres != ESP_OK){
                ESP_LOGW(TAG, "Unable to send datagram");
                continue;
            }
        }*/
    }
}

void init_tcp(){
    if (tcp_task_queue_set == NULL){
        tcp_task_queue_set = xQueueCreateSet(8);
    }

    if (port_queue == NULL){
        port_queue = xQueueCreate(4, sizeof(struct tcp_port_t));
        if (port_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create port_queue");
            return;
        }
        xQueueAddToSet(port_queue, tcp_task_queue_set);
    }
    /*
    if (send_queue == NULL){
        send_queue = xQueueCreate(8, sizeof(struct tcp_transmit_params_t));
        if (send_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create send_queue");
            return;
        }
        xQueueAddToSet(send_queue, tcp_task_queue_set);
    }
    */

    BaseType_t xReturned = xTaskCreate(tcp_task, "tcp task", 800, NULL, 1, NULL);
    if (xReturned  != pdPASS){
        // If not pass, then xReturned is garuanteed by the docs to be a errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY.
        ESP_LOGE(TAG, "Could not create tcp task. Not enough memory!");
        return;
    }
}