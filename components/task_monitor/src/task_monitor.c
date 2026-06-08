#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <stdio.h>

#include "task_monitor.h"

#include "protocols/tcp.h"

#define TASK_MONITOR_PORT 10741

//static const char TAG[] = "task_monitor";

char* watched_names;

uint16_t other_port_num;
ipv4_addr_t other_ipv4_addr;
/*
typedef struct {
    uint32_t interval_ms;
    const char *const *names;
} monitor_cfg_t;
*/

static bool name_in_filter(const char *name, const char *const *names)
{
    for (; *names != NULL; names++) {
        if (strcmp(name, *names) == 0) return true;
    }
    return false;
}

char *task_monitor_build_report(const char *const *names)
{
    UBaseType_t max_tasks = uxTaskGetNumberOfTasks() + 4;

    TaskStatus_t *buf = malloc(max_tasks * sizeof(TaskStatus_t));
    if (!buf) return NULL;

    uint32_t total_runtime;
    UBaseType_t filled = uxTaskGetSystemState(buf, max_tasks, &total_runtime);

    /* header (32) + per-task line (56) + null terminator */
    size_t out_size = 32 + filled * 56 + 1;
    char *out = malloc(out_size);
    if (!out) {
        free(buf);
        return NULL;
    }

    size_t pos = 0;
    pos += snprintf(out + pos, out_size - pos, "--- task stack watermarks ---\n");

    for (UBaseType_t i = 0; i < filled; i++) {
        if (names && !name_in_filter(buf[i].pcTaskName, names)) continue;
#if configGENERATE_RUN_TIME_STATS
        uint32_t cpu_pct = total_runtime > 0
            ? (uint32_t)((uint64_t)buf[i].ulRunTimeCounter * 100 / total_runtime)
            : 0;
        pos += snprintf(out + pos, out_size - pos, "  %-16s  hwm: %5u bytes  cpu: %3u%%\n",
                        buf[i].pcTaskName,
                        (unsigned)buf[i].usStackHighWaterMark,
                        cpu_pct);
#else
        pos += snprintf(out + pos, out_size - pos, "  %-16s  hwm: %5u bytes\n",
                        buf[i].pcTaskName,
                        (unsigned)buf[i].usStackHighWaterMark);
#endif
    }

    free(buf);
    return out;
}
/*
static void monitor_task(void *arg)
{
    monitor_cfg_t *cfg = (monitor_cfg_t *)arg;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(cfg->interval_ms));

        char *report = task_monitor_build_report(cfg->names);
        if (!report) {
            ESP_LOGE(TAG, "malloc failed");
            continue;
        }

        ESP_LOGI(TAG, "%s", report);
        free(report);
    }
}
*/

void on_task_monitor_tcp_connect(uint16_t new_other_port_num, ipv4_addr_t* new_other_ipv4_addr){
    other_port_num = new_other_port_num;
    other_ipv4_addr = *new_other_ipv4_addr;
}

void on_task_monitor_tcp_receive(void* data, uint16_t data_len){
    char* report = task_monitor_build_report(watched_names);
    tcp_send(TASK_MONITOR_PORT, other_port_num, &other_ipv4_addr, report, strlen(report), 0);
}

void start_task_monitor_tcp(){
    tcp_listen_port(TASK_MONITOR_PORT, on_task_monitor_tcp_receive, on_task_monitor_tcp_connect, start_task_monitor_tcp);
}

void task_monitor_init(char* names)
{
    watched_names = names;

    //monitor_cfg_t *cfg = malloc(sizeof(monitor_cfg_t));
    //cfg->interval_ms = interval_ms;
    //cfg->names = names;
    //xTaskCreate(monitor_task, "task_monitor", 2048, cfg, 1, NULL);

    start_task_monitor_tcp();
}