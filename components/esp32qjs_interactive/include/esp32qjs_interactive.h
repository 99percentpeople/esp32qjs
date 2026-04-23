#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t esp32qjs_interactive_poll_result_t;

#define ESP32QJS_INTERACTIVE_POLL_NONE   ((esp32qjs_interactive_poll_result_t)0U)
#define ESP32QJS_INTERACTIVE_POLL_OUTPUT ((esp32qjs_interactive_poll_result_t)(1U << 0))

typedef struct {
    const char *title;
    const char *subtitle;
    const char *hint;
} esp32qjs_interactive_banner_t;

typedef struct {
    void (*attach_current_task)(void *opaque);
    uint32_t (*output_generation)(void *opaque);
    void (*run_startup)(void *opaque);
    esp32qjs_interactive_poll_result_t (*poll)(void *opaque);
    void (*handle_line)(void *opaque, const char *line);
    bool (*wait_for_activity)(void *opaque, uint32_t timeout_ms);
    void (*notify_activity)(void *opaque);
    void (*notify_activity_from_isr)(void *opaque, int *task_woken);
    void *opaque;
} esp32qjs_interactive_host_t;

void esp32qjs_interactive_console_init(const esp32qjs_interactive_host_t *host);

void esp32qjs_interactive_install_log_bridge(const esp32qjs_interactive_host_t *host);

void esp32qjs_interactive_prepare_output(void *opaque);

size_t esp32qjs_interactive_task_stack_size(void);

void esp32qjs_interactive_run(const esp32qjs_interactive_host_t *host,
                              const esp32qjs_interactive_banner_t *banner);
