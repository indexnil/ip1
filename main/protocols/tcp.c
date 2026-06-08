#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
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

// Milliseconds
#define TIMEOUT_CHECK_TIME 50
#define TIME_WAIT_DURATION 1000
#define RETRANSMIT_TIMEOUT 100

struct tcp_input_event_t {
    uint8_t* buffer;
    uint16_t length;
    ipv4_addr_t src_ipv4_addr;
};

struct tcp_port_cmd_t {
    enum {CMD_CREATE_AND_LISTEN, CMD_CREATE_AND_CONNECT, CMD_CLOSE} type;
    uint16_t self_port_num;
    uint16_t other_port_num;
    ipv4_addr_t other_ipv4_addr; 
    void (*on_receive)(void* data, uint16_t data_len);
    void (*on_connect)(uint16_t other_port_num, ipv4_addr_t* other_ipv4_addr);
    void (*on_disconnect)();
};

const uint8_t FIN = 0b1;
const uint8_t SYN = 0b10;
const uint8_t PSH = 0b1000;
const uint8_t ACK = 0b10000;
const ipv4_addr_t IPV4_ALL_ZEROES = {.bytes = {0, 0, 0, 0}};

QueueHandle_t send_queue = NULL;
QueueHandle_t input_queue = NULL;
QueueHandle_t port_cmd_queue = NULL;
QueueSetHandle_t tcp_task_queue_set = NULL;

uint8_t max_ports = 8;
struct tcp_port_t ports[8];

struct net_context_t self_net;

struct tcp_port_t* find_port(uint16_t self_port_number, uint16_t other_port_number, ipv4_addr_t* other_ipv4_address){
    for (uint8_t i = 0; i<max_ports; i++){
        if (ports[i].self_port_num != self_port_number || ports[i].other_port_num != other_port_number || memcmp(ports[i].other_ipv4_addr.bytes, other_ipv4_address->bytes, 4) != 0){
            continue;
        }
        return &ports[i];
    }
    return NULL;
}

struct tcp_port_t* find_listening_port(uint16_t self_port_number){
    for (uint8_t i = 0; i<max_ports; i++){
        if (ports[i].self_port_num != self_port_number){
            continue;
        }
        if (ports[i].state == LISTEN && memcmp(ports[i].other_ipv4_addr.bytes, IPV4_ALL_ZEROES.bytes, 4) == 0){
            return &ports[i];
        }        
    }
    return NULL;
}

struct tcp_unacked_t* find_empty_unacked(struct tcp_port_t* port){
    for (uint8_t i = 0; i<MAX_UNACKED; i++){
        if (port->unacked[i].in_use == false){
            return &port->unacked[i];
        }
    }

    return NULL;
}

uint8_t get_unacked_count(struct tcp_port_t* port){
    uint8_t count = 0;
    for (uint8_t i = 0; i<MAX_UNACKED; i++){
        if (port->unacked[i].in_use == true){
            count++;
        }
    }
    return count;
}

// This takes ownership of params->data and eventually frees it
// Make sure to ONLY call this outside of tcp_send if params->data.len = 0
// Otherwise call tcp_send
// If data_len == 0, then also provide a sequence number
static esp_err_t tcp_enqueue(struct tcp_transmit_params_t* params, TickType_t timeout){
    BaseType_t result = xQueueSend(send_queue, params, timeout);
    if (result != pdTRUE){
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

// Clears everything related to it and allows it to be re-allocated
void tcp_reset_port(struct tcp_port_t* port){
    ESP_LOGI(TAG, "Resetting port");

    port->state = CLOSED;
    port->self_port_num = 0; // Allow a new port to be overwritten on it
    for (uint8_t i = 0; i<MAX_UNACKED; i++){
        if (port->unacked[i].in_use == 1 && port->unacked[i].data != NULL){
            free(port->unacked[i].data);
            port->unacked[i].data = NULL;
        }
    }
    // Release all tasks waiting before deleting the semaphore
    uint8_t waiting_tasks = MAX_UNACKED-uxSemaphoreGetCount(port->unacked_slots);
    for (uint8_t i = 0; i<waiting_tasks; i++){
        xSemaphoreGive(port->unacked_slots);
    }
    vSemaphoreDelete(port->unacked_slots);

}

// Always runs in the tcp_task
// Takes ownership of the data
esp_err_t tcp_send_internal(struct tcp_transmit_params_t* params){
    uint16_t datagram_length = sizeof(struct tcp_header_t) + params->data_len;
    uint8_t* datagram = malloc(datagram_length + sizeof(struct ipv4_psuedo_header_t));
    if (datagram == NULL) {
        free(params->data);
        ESP_LOGW(TAG, "tcp_send_internal is unable to send packet, unable to allocate data for it");
        return ESP_ERR_NO_MEM;
    }

    struct tcp_port_t* port = find_port(params->src_port, params->dst_port, &params->dst_ipv4_addr);
    if (port == NULL){
        free(params->data);
        ESP_LOGW(TAG, "tcp_send_internal is unable to send packet, port not found");
        return ESP_FAIL;
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
    header->ack_num = htonl(port->ack_num);
    header->dOffset = sizeof(struct tcp_header_t) / 4; // Should be an integer
    header->reserved = 0;
    header->control = params->control;
    header->window = htons(port->self_window);
    header->checksum = 0;
    header->urgent_ptr = 0;

    if (params->data != NULL && params->data_len > 0) {
        memcpy(datagram + sizeof(struct tcp_header_t), params->data, params->data_len);
    }

    uint16_t seq_increment = params->data_len + ((params->flags.SYN) || (params->flags.FIN));
    if (seq_increment > 0 && !params->isRetransmission){
        // If we increment the sequence number, we also need it to be ACKed
        struct tcp_unacked_t* unacked = find_empty_unacked(port);
        if (unacked == NULL){
            free(params->data);
            ESP_LOGW(TAG, "Unable to send packet, unacked buffer is full");
            return ESP_ERR_NO_MEM;
        }
        unacked->in_use = true;
        unacked->seq_num = port->seq_num;
        unacked->control = params->control;
        unacked->data = params->data;
        unacked->data_len = params->data_len;
        unacked->sent_at = xTaskGetTickCount();

        header->seq_num = htonl(port->seq_num);
        port->seq_num += seq_increment;
    } else {
        // Basically only happens when sending acks
        header->seq_num = htonl(params->seq_num);
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

// Same as tcp_send except more parameters exposed, risky for apps use.
esp_err_t tcp_enqueue_reliable(struct tcp_transmit_params_t* params, TickType_t timeout) {
    struct tcp_port_t* port = find_port(params->src_port, params->dst_port, &params->dst_ipv4_addr);
    if (port == NULL){
        free(params->data);
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(port->unacked_slots, timeout) != pdTRUE) {
        free(params->data);
        return ESP_ERR_TIMEOUT;
    }
    if (port->state == CLOSED){
        // The port could have closed whilst we were waiting
        free(params->data);
        return ESP_FAIL;
    }
    if (xQueueSend(send_queue, params, 0) != pdTRUE) {
        xSemaphoreGive(port->unacked_slots);
        free(params->data);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// This takes ownership of data and eventually frees it. The intended path for apps
esp_err_t tcp_send(uint16_t src_port_num, uint16_t dst_port_num, ipv4_addr_t* dst_ipv4_addr, void* data, uint16_t len, TickType_t timeout) {
    struct tcp_port_t* port = find_port(src_port_num, dst_port_num, dst_ipv4_addr);
    if (port == NULL){
        free(data);
        return ESP_FAIL;
    }

    struct tcp_transmit_params_t params = {
        .dst_ipv4_addr = port->other_ipv4_addr,
        .dst_port = port->other_port_num,
        .src_port = port->self_port_num, 
        .control = ACK + PSH,
        .data = data,
        .data_len = len,
    };

    return tcp_enqueue_reliable(&params, timeout);
}

esp_err_t input_tcp(void* buffer, uint16_t length, ipv4_addr_t* src_ipv4_addr){
    // Copy it because it will be freed once we return and we still need the buffer on the queue
    void* copied_buffer = malloc(length);
    if (copied_buffer == NULL){
        return ESP_ERR_NO_MEM;
    }
    memcpy(copied_buffer, buffer, length);
    struct tcp_input_event_t input_event = {
        .buffer = copied_buffer,
        .length = length,
        .src_ipv4_addr = *src_ipv4_addr,
    };
    // The queue handler now takes owneship of the buffer
    if (xQueueSend(input_queue, &input_event, 0) != pdTRUE) {
        free(copied_buffer);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void input_tcp_internal(void* buffer, uint16_t length, ipv4_addr_t* src_ipv4_addr){
    struct tcp_header_t* header = (struct tcp_header_t*)buffer;

    uint16_t header_length = header->dOffset*4;
    if (header_length < sizeof(struct tcp_header_t)){
        ESP_LOGW(TAG, "Received datagram with an invalid header_length (doffset) of %d, expected at least %d", header_length, sizeof(struct tcp_header_t));
        return;
    }

    //ESP_LOGI(TAG, "Incoming source port %u", ntohs(header->src_port));
    //ESP_LOGI(TAG, "Incoming sequence number %u", ntohl(header->seq_num));

    uint16_t dst_port = ntohs(header->dst_port);

    struct tcp_port_t* port = find_port(dst_port, ntohs(header->src_port), src_ipv4_addr);
    if (port == NULL){
        if (header->flags.SYN){
            port = find_listening_port(dst_port);
        }

        if (port == NULL){
            ESP_LOGW(TAG, "No port found with port number %u", dst_port);
            return;
        }
    }

    // TCP state logic begins here
    if (port->state == CLOSED){
        ESP_LOGW(TAG, "Received input to closed port %d", dst_port);
        return;
    }

    uint32_t received_seq_num = ntohl(header->seq_num);
    uint32_t received_ack_num = ntohl(header->ack_num);

    if (header->flags.RST){
        // Reset the port completely
        ESP_LOGW(TAG, "Remote host forcibly closed a connection");
        tcp_reset_port(port);
        return;
    }

    if (port->state != LISTEN && port->state != SYN_SENT) {
        // Only accept sequential segments
        if (received_seq_num != port->ack_num){
            // If not, ignore it
            ESP_LOGW(TAG, "Unexpected sequence number received");
            return;
        }
    }

    if (header->flags.ACK){
        if (received_ack_num <= port->seq_num){
            for (uint8_t i = 0; i < MAX_UNACKED; i++){
                struct tcp_unacked_t* unacked = &port->unacked[i];
                if (unacked->in_use == false){
                    continue;
                }

                if (unacked->seq_num <= received_ack_num){
                    // Packet acknowledged!
                    unacked->in_use = false;
                    xSemaphoreGive(port->unacked_slots);
                    free(unacked->data);
                    unacked->data = NULL;

                }
            }
        }
    } else if (header->flags.SYN == 0){
        // Every packet after the handshake should have its ACK flag set
        ESP_LOGW(TAG, "Malformed packet received, no ACK flag");
        return;
    }

    // Handle acknowledging the data except for in the states where we have already sent an ack
    uint16_t sequence_increment = (length - header_length) + ((header->flags.SYN) || (header->flags.FIN));
    if (sequence_increment > 0 && port->state != LAST_ACK && port->state != LISTEN){
        port->ack_num = received_seq_num + sequence_increment;
        struct tcp_transmit_params_t response = {
            .dst_ipv4_addr = *src_ipv4_addr,
            .src_port = port->self_port_num,
            .dst_port = port->other_port_num,
            .seq_num = port->seq_num,
            .control = ACK,
            .data = NULL,
            .data_len = 0,
        };
        tcp_enqueue(&response, portMAX_DELAY);
    }

    port->other_window = ntohs(header->window);

    switch (port->state){
        case LISTEN:
            if (header->flags.SYN != 1){
                ESP_LOGW(TAG, "Received segment at port listen state with no syn flag");
                break;
            }

            //ESP_LOGI(TAG, "Syn received!");

            port->state = SYN_RECEIVED;
            port->other_ipv4_addr = *src_ipv4_addr;
            port->other_port_num = ntohs(header->src_port);
            port->ack_num = ntohl(header->seq_num) + 1; // + 1 because syns contain 1 byte of data

            struct tcp_transmit_params_t response = {
                .dst_ipv4_addr = *src_ipv4_addr,
                .src_port = port->self_port_num,
                .dst_port = port->other_port_num,
                .seq_num = port->seq_num,
                .control = SYN + ACK,
                .data = NULL,
                .data_len = 0,
            };

            tcp_enqueue_reliable(&response, portMAX_DELAY);

            break;
        case SYN_SENT:
            if (header->flags.SYN != 1){
                ESP_LOGW(TAG, "Received segment at port SYN_SENT state with no syn flag");
                break;
            }
            if (header->flags.ACK != 1){
                ESP_LOGW(TAG, "Received segment at port SYN_SENT state with no ack flag");
                break;
            }

            port->state = ESTABLISHED;

            ESP_LOGI(TAG, "Established connection!");

            if (port->on_connect != NULL) {
                port->on_connect(port->other_port_num, &port->other_ipv4_addr);
            }

            break;
        case SYN_RECEIVED:
            if (header->flags.ACK != 1){
                ESP_LOGW(TAG, "Received segment at port SYN_RECEIVED state with no ACK flag");
                break;
            }

            // They should ack all the data we sent (1 byte)
            if (ntohl(header->ack_num) != port->seq_num){
                ESP_LOGW(TAG, "Unexpected ack number in SYN_RECEIVED");
                break;
            }

            port->state = ESTABLISHED;

            ESP_LOGI(TAG, "Established connection!");

            if (port->on_connect != NULL) {
                port->on_connect(port->other_port_num, &port->other_ipv4_addr);
            }

            break;
        case ESTABLISHED: { // We dont accept data after initiating a FIN, havent implemented that yet
            // Handle closing connection
            if (header->flags.FIN){
                struct tcp_transmit_params_t response = {
                    .dst_ipv4_addr = *src_ipv4_addr,
                    .src_port = port->self_port_num,
                    .dst_port = port->other_port_num,
                    .seq_num = port->seq_num,
                    .control = FIN + ACK,
                    .data = NULL,
                    .data_len = 0,
                };

                tcp_enqueue_reliable(&response, portMAX_DELAY);

                port->state = LAST_ACK;
                //ESP_LOGI(TAG, "Port entered LAST_ACK state");
            }
            

            uint16_t data_length = (length - header_length);
            if (data_length > 0){
                // Deliver data to application, can not yield currently
                if (port->on_receive != NULL) {
                    port->on_receive(buffer + header_length, data_length);
                }
            }

            break;
        }
        case LAST_ACK:
            if (header->flags.ACK == 0){
                ESP_LOGW(TAG, "LAST_ACK - No ACK flag received in LAST_ACK state");
                break;
            }

            // Ensure they have acked our data
            if (ntohl(header->ack_num) != port->seq_num){
                ESP_LOGW(TAG, "LAST_ACK - Our data was not acknowledged");
                break;
            }

            tcp_reset_port(port);
            ESP_LOGI(TAG, "Closed port!");

            port->on_disconnect();

            break;
        case FIN_WAIT_1:
            if (header->flags.FIN == 1){
                // In case the sender merges these two packets.
                // We dont check for ACK in case the packet was unordered
                port->state = TIME_WAIT;
                port->entered_time_wait_at = xTaskGetTickCount();
                port->on_disconnect();
                break;
            }
            if (header->flags.ACK == 0){
                // We might have missed some data because we dont accept data after initating FIN
                // But this is "intentional"
                break;
            }
            port->state = FIN_WAIT_2;
            break;
        case FIN_WAIT_2:
            if (header->flags.FIN == 0){
                break;
            }

            port->state = TIME_WAIT;
            port->entered_time_wait_at = xTaskGetTickCount();
            port->on_disconnect();
            break;
        default:
            break;
    }
}

// Overwrites ports with self_port_num = 0, consider those to be unused
struct tcp_port_t* allocate_port(struct tcp_port_t* port){
    for (uint8_t i = 0; i<max_ports; i++){
        if (ports[i].self_port_num == 0){
            ports[i] = *port;
            return &ports[i];
        }
    }
    return NULL;
}

struct tcp_port_t* tcp_create_port_internal(uint16_t src_port_num, 
    void (*on_receive)(void* data, uint16_t data_len), 
    void (*on_connect)(uint16_t other_port_num, ipv4_addr_t* other_ipv4_addr),
    void (*on_disconnect)()
){
    struct tcp_port_t port = {
        .other_ipv4_addr = IPV4_ALL_ZEROES,
        .other_port_num = 0,
        .self_port_num = src_port_num,
        .state = CLOSED,
        .on_receive = on_receive,
        .on_connect = on_connect,
        .on_disconnect = on_disconnect,
        .seq_num = 0,
        .self_window = 1500,
    };

    port.unacked_slots = xSemaphoreCreateCounting(MAX_UNACKED, MAX_UNACKED);
    if (port.unacked_slots == NULL){
        ESP_LOGE(TAG, "Failed to create unacked_slots for port");
        return NULL;
    }

    return allocate_port(&port);
}


esp_err_t tcp_connect_port(uint16_t src_port_num, uint16_t dst_port_num, ipv4_addr_t* dst_ipv4_addr, 
    void (*on_receive)(void* data, uint16_t data_len), 
    void (*on_connect)(uint16_t other_port_num, ipv4_addr_t* other_ipv4_addr),
    void (*on_disconnect)()
){
    struct tcp_port_cmd_t port_cmd = {
        .type = CMD_CREATE_AND_CONNECT,
        .self_port_num = src_port_num,
        .other_port_num = dst_port_num,
        .other_ipv4_addr = *dst_ipv4_addr,
        .on_receive = on_receive,
        .on_connect = on_connect,
        .on_disconnect = on_disconnect,
    };
    if (xQueueSend(port_cmd_queue, &port_cmd, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t tcp_connect_port_internal(uint16_t src_port_num, uint16_t dst_port_num, ipv4_addr_t* dst_ipv4_addr, 
    void (*on_receive)(void* data, uint16_t data_len), 
    void (*on_connect)(uint16_t other_port_num, ipv4_addr_t* other_ipv4_addr),
    void (*on_disconnect)()
){
    struct tcp_port_t* port = tcp_create_port_internal(src_port_num, on_receive, on_connect, on_disconnect);
    if (port == NULL){
        return ESP_FAIL;
    }

    port->other_ipv4_addr = *dst_ipv4_addr;
    port->other_port_num = dst_port_num;

    struct tcp_transmit_params_t syn_request = {
        .dst_ipv4_addr = *dst_ipv4_addr,
        .src_port = port->self_port_num,
        .dst_port = port->other_port_num,
        .seq_num = port->seq_num,
        .control = SYN,
        .data = NULL,
        .data_len = 0,
    };

    port->state = SYN_SENT;

    return tcp_enqueue_reliable(&syn_request, portMAX_DELAY); // Might cause problems that we can wait forever in the tcp task
}

esp_err_t tcp_listen_port(uint16_t src_port_num, 
    void (*on_receive)(void* data, uint16_t data_len), 
    void (*on_connect)(uint16_t other_port_num, ipv4_addr_t* other_ipv4_addr),
    void (*on_disconnect)()
){
    struct tcp_port_cmd_t port_cmd = {
        .type = CMD_CREATE_AND_LISTEN,
        .self_port_num = src_port_num,
        .other_port_num = 0,
        .other_ipv4_addr = IPV4_ALL_ZEROES,
        .on_receive = on_receive,
        .on_connect = on_connect,
        .on_disconnect = on_disconnect,
    };
    if (xQueueSend(port_cmd_queue, &port_cmd, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t tcp_listen_port_internal(uint16_t src_port_num, 
    void (*on_receive)(void* data, uint16_t data_len), 
    void (*on_connect)(uint16_t other_port_num, ipv4_addr_t* other_ipv4_addr),
    void (*on_disconnect)()
){
    struct tcp_port_t* port = tcp_create_port_internal(src_port_num, on_receive, on_connect, on_disconnect);
    if (port == NULL){
        return ESP_FAIL;
    }

    port->state = LISTEN;
    // Zero out the ipv4 address so the port accepts connections from everywhere
    port->other_ipv4_addr = IPV4_ALL_ZEROES;

    return ESP_OK;
}

esp_err_t tcp_close_port(uint16_t src_port_num, uint16_t dst_port_num, ipv4_addr_t* dst_ipv4_addr){
    struct tcp_port_cmd_t port_cmd = {
        .type = CMD_CLOSE,
        .self_port_num = src_port_num,
        .other_port_num = dst_port_num,
        .other_ipv4_addr = *dst_ipv4_addr,
    };
    if (xQueueSend(port_cmd_queue, &port_cmd, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t tcp_close_port_internal(uint16_t src_port_num, uint16_t dst_port_num, ipv4_addr_t* dst_ipv4_addr){
    struct tcp_port_t* port = find_port(src_port_num, dst_port_num, dst_ipv4_addr);
    if (port == NULL){
        return ESP_ERR_INVALID_ARG;
    }

    struct tcp_transmit_params_t fin_request = {
        .dst_ipv4_addr = *dst_ipv4_addr,
        .src_port = port->self_port_num,
        .dst_port = port->other_port_num,
        .seq_num = port->seq_num,
        .control = FIN,
        .data = NULL,
        .data_len = 0,
    };

    port->state = FIN_WAIT_1;

    return tcp_enqueue_reliable(&fin_request, portMAX_DELAY);
}

void tcp_set_self_net_context(const struct net_context_t *net){
    memcpy(&self_net, net, sizeof(struct net_context_t));
}

void tcp_task(void* pvParameters){
    struct tcp_transmit_params_t tx_buf;
    struct tcp_input_event_t rx_buf;
    struct tcp_port_cmd_t cmd_buf;
    while (1) {
        QueueHandle_t queue = xQueueSelectFromSet(tcp_task_queue_set, TIMEOUT_CHECK_TIME);
        if (queue == send_queue){
            BaseType_t result = xQueueReceive(send_queue, &tx_buf, 0);
            if (result != pdTRUE){
                continue;
            }

            esp_err_t sres = tcp_send_internal(&tx_buf);
            if (sres != ESP_OK){
                ESP_LOGW(TAG, "Unable to send datagram");
                continue;
            }
        } else if (queue == input_queue){
            BaseType_t result = xQueueReceive(input_queue, &rx_buf, 0);
            if (result != pdTRUE){
                continue;
            }

            input_tcp_internal(rx_buf.buffer, rx_buf.length, &rx_buf.src_ipv4_addr);

            // The buffer was malloced right before being placed on the queue, so we need to free it
            free(rx_buf.buffer);
        } else if (queue == port_cmd_queue) {
            BaseType_t result = xQueueReceive(port_cmd_queue, &cmd_buf, 0);
            if (result != pdTRUE){
                continue;
            }

            switch (cmd_buf.type){
                case CMD_CREATE_AND_CONNECT: 
                    tcp_connect_port_internal(cmd_buf.self_port_num, cmd_buf.other_port_num, &cmd_buf.other_ipv4_addr, cmd_buf.on_receive, cmd_buf.on_connect, cmd_buf.on_disconnect);
                    break;
                case CMD_CREATE_AND_LISTEN:
                    tcp_listen_port_internal(cmd_buf.self_port_num, cmd_buf.on_receive, cmd_buf.on_connect, cmd_buf.on_disconnect);
                    break;
                case CMD_CLOSE:
                    tcp_close_port_internal(cmd_buf.self_port_num, cmd_buf.other_port_num, &cmd_buf.other_ipv4_addr);
                    break;
                default:
                    ESP_LOGW(TAG, "Invalid tcp port command type received");
                    break;
            }
        } else if (queue == NULL){
            // Scan for timeouts

            TickType_t now = xTaskGetTickCount();
            for (uint8_t p = 0; p < max_ports; p++) {
                struct tcp_port_t* port = &ports[p];
                if (port->self_port_num == 0) continue;

                if (port->state == TIME_WAIT){
                    if (now - port->entered_time_wait_at > TIME_WAIT_DURATION){
                        tcp_reset_port(port);
                        continue;
                    }
                }

                for (uint8_t i = 0; i < MAX_UNACKED; i++) {
                    struct tcp_unacked_t* u = &port->unacked[i];
                    if (u->in_use == false) continue;
                    if ((now - u->sent_at) < RETRANSMIT_TIMEOUT) continue;

                    // Timeout found
                    u->sent_at = now; 

                    struct tcp_transmit_params_t params = {
                        .dst_ipv4_addr = port->other_ipv4_addr,
                        .dst_port = port->other_port_num,
                        .src_port = port->self_port_num,
                        .seq_num = u->seq_num,
                        .control = u->control,
                        .isRetransmission = true,
                        .data = u->data,
                        .data_len = u->data_len,
                    };

                    esp_err_t res = tcp_send_internal(&params);
                    if (res != ESP_OK) {
                        // tcp_send_internal already freed u->data on failure
                        u->data = NULL;
                        u->in_use = false;
                        xSemaphoreGive(port->unacked_slots);
                    }                
                }

            }
        }

        
    }
}

esp_err_t init_tcp(){
    if (tcp_task_queue_set == NULL){
        tcp_task_queue_set = xQueueCreateSet(20);
    }
    
    if (send_queue == NULL){
        send_queue = xQueueCreate(8, sizeof(struct tcp_transmit_params_t));
        if (send_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create send_queue");
            return ESP_ERR_NO_MEM;
        }
        xQueueAddToSet(send_queue, tcp_task_queue_set);
    }

    if (input_queue == NULL){
        input_queue = xQueueCreate(8, sizeof(struct tcp_input_event_t));
        if (input_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create input_queue");
            return ESP_ERR_NO_MEM;
        }
        xQueueAddToSet(input_queue, tcp_task_queue_set);
    }
    if (port_cmd_queue == NULL){
        port_cmd_queue = xQueueCreate(4, sizeof(struct tcp_port_cmd_t));
        if (port_cmd_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create port_cmd_queue");
            return ESP_ERR_NO_MEM;
        }
        xQueueAddToSet(port_cmd_queue, tcp_task_queue_set);
    }


    BaseType_t xReturned = xTaskCreate(tcp_task, "tcp task", 3000, NULL, 1, NULL);
    if (xReturned  != pdPASS){
        // If not pass, then xReturned is garuanteed by the docs to be a errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY.
        ESP_LOGE(TAG, "Could not create tcp task. Not enough memory!");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}