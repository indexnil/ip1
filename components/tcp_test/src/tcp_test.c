#include <string.h>
#include <stdint.h>

#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"

#include "protocols/tcp.h"
#include "debug.h"

#define RESPONSE "Hello world!"

static const char TAG[] = "tcp test";

static ipv4_addr_t dst_ipv4_addr = {.bytes = {192, 168, 1, 214}};


void on_receive(void* data, uint16_t data_length){
    char* data_string = malloc(data_length + 1);
    memcpy(data_string, data, data_length);
    data_string[data_length] = 0;
    ESP_LOGI(TAG, "Received data: %s", data_string);
}

void on_connect(uint16_t other_port_num, ipv4_addr_t* other_ipv4_addr){
    ESP_LOGI(TAG, "Connected to port %d", other_port_num);

    tcp_send(10741, other_port_num, other_ipv4_addr, RESPONSE, sizeof(RESPONSE), 0);
}

void on_disconnect(){
    ESP_LOGI(TAG, "Disconnected");
    tcp_listen_port(10741, on_receive, on_connect, on_disconnect);
}

void tcp_test_init(void)
{
    ESP_LOGI(TAG, "Starting");
    /*
    if (tcp_listen_port(10742, on_receive, on_connect) != ESP_OK){
        ESP_LOGE(TAG, "Unable to listen at tcp port");
        return;
    }
    */

    //tcp_connect_port(10742, 10741, &dst_ipv4_addr, on_receive, on_connect);

    tcp_listen_port(10741, on_receive, on_connect, on_disconnect);
}