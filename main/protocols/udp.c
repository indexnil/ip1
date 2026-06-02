#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "../utility/net_types.h"
#include "../utility/endian.h"
#include "udp.h"
#include "ipv4.h"
#include <stdlib.h>
#include <string.h>

#define UDP_PROTOCOL_NUMBER 17
#define TAG "udp"

QueueHandle_t port_queue = NULL;
QueueHandle_t send_queue = NULL;
QueueSetHandle_t udp_task_queue_set = NULL;

uint8_t max_ports = 8;
struct udp_port_t ports[8];

struct net_context_t self_net;

struct udp_port_t* find_port(uint8_t port_number){
    for (uint8_t i = 0; i<max_ports; i++){
        if (ports[i].port_number == port_number){
            return &ports[i];
        }
    }

    return NULL;
}

void udp_input(void* buffer, ipv4_addr_t* src_addr){
    struct udp_header_t* header = (struct udp_header_t*)buffer;

    uint16_t length = ntohs(header->length);
    if (length < sizeof(struct udp_header_t)){
        ESP_LOGW(TAG, "Received datagram with an invalid length of %d, expected at least %d", length, sizeof(struct udp_header_t));
        return;
    }

    uint16_t dst_port = ntohs(header->dst_port);

    struct udp_port_t* port = find_port(dst_port);
    if (port == NULL){
        ESP_LOGW(TAG, "No port found with port number %d", dst_port);
        return;
    }

    port->callback(buffer + sizeof(struct udp_header_t), length - sizeof(struct udp_header_t), src_addr);
}

esp_err_t udp_create_port(struct udp_port_t* port){
    if (port->callback == NULL){
        return ESP_ERR_INVALID_ARG;
    }
    BaseType_t result = xQueueSend(port_queue, port, 0);
    if (result != pdTRUE){
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void udp_set_self_net_context(const struct net_context_t *net){
    memcpy(&self_net, net, sizeof(struct net_context_t));
}

// This takes ownership of params->data and eventually frees it in another task
esp_err_t udp_send(struct udp_transmit_params_t* params){
    BaseType_t result = xQueueSend(send_queue, params, 0);
    if (result != pdTRUE){
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t udp_send_internal(struct udp_transmit_params_t* params){
    uint16_t datagram_length = sizeof(struct udp_header_t) + params->data_len;
    uint8_t* datagram = malloc(datagram_length);
    if (datagram == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Cast to a struct header for easier writing
    struct udp_header_t* header = (struct udp_header_t*)datagram;
    header->src_port = htons(params->src_port);
    header->dst_port = htons(params->dst_port);
    header->length = htons(datagram_length);
    header->checksum = 0; // Skip the checksum

    if (params->data != NULL && params->data_len > 0) {
        memcpy(datagram + sizeof(struct udp_header_t), params->data, params->data_len);
        free(params->data);
        params->data = NULL;
    }

    struct ipv4_transmit_params_t ipv4_params = {
        .src_ipv4_addr = self_net.ip,
        .dst_ipv4_addr = params->dst_ipv4_addr,
        .protocol = UDP_PROTOCOL_NUMBER,
        .data = datagram,
        .data_len = datagram_length,
    };

    return send_ipv4_packet(&ipv4_params);
}

esp_err_t allocate_port(struct udp_port_t* port){
    for (uint8_t i = 0; i<max_ports; i++){
        if (ports[i].port_number == 0){
            ports[i] = *port;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

void udp_task(void* pvParameters){
    struct udp_port_t port_buf;
    struct udp_transmit_params_t tx_buf;
    while (1) {
        QueueHandle_t queue = xQueueSelectFromSet(udp_task_queue_set, portMAX_DELAY);
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
        } else if (queue == send_queue){
            BaseType_t result = xQueueReceive(send_queue, &tx_buf, 0);
            if (result != pdTRUE){
                continue;
            }

            esp_err_t sres = udp_send_internal(&tx_buf);
            if (sres != ESP_OK){
                ESP_LOGW(TAG, "Unable to send datagram");
                continue;
            }
        }
    }
}

void udp_init(){
    if (udp_task_queue_set == NULL){
        udp_task_queue_set = xQueueCreateSet(8);
    }

    if (port_queue == NULL){
        port_queue = xQueueCreate(4, sizeof(struct udp_port_t));
        if (port_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create port_queue");
            return;
        }
        xQueueAddToSet(port_queue, udp_task_queue_set);
    }
    if (send_queue == NULL){
        send_queue = xQueueCreate(8, sizeof(struct udp_transmit_params_t));
        if (send_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create send_queue");
            return;
        }
        xQueueAddToSet(send_queue, udp_task_queue_set);
    }

    BaseType_t xReturned = xTaskCreate(udp_task, "udp task", 800, NULL, 1, NULL);
    if (xReturned  != pdPASS){
        // If not pass, then xReturned is garuanteed by the docs to be a errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY.
        ESP_LOGE(TAG, "Could not create udp task. Not enough memory!");
        return;
    }
}