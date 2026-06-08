#pragma once

#include <stdint.h>

/* Build a watermark report string for all tasks (or a filtered subset).
   names: NULL-terminated filter array, or NULL for all tasks.
   Returns a heap-allocated string the caller must free(). Returns NULL on
   allocation failure. */
char *task_monitor_build_report(const char *const *names);

/* Start a periodic task that logs stack high watermarks.
   interval_ms: how often to print.
   names: NULL-terminated array of task name strings to filter by,
          or NULL to report every running task. */
void task_monitor_init(char *names);