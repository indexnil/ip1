#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"

#include "protocols/tcp.h"
#include "debug.h"

static const char TAG[] = "tcp test";

void tcp_test_init(void)
{
    ESP_LOGI(TAG, "Starting");

    struct tcp_port_t port = {
        .self_port_num = 10742,
        .state = LISTEN,
    };

    if (tcp_create_port(&port) != ESP_OK){
        ESP_LOGE(TAG, "Unable to create tcp port");
    };
}