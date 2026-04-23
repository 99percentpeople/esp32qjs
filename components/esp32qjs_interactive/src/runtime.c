#include "esp32qjs_interactive/esp32qjs_interactive.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log_write.h"
#include "sdkconfig.h"
#include "repl.h"
#include "repl_input.h"

static const esp32qjs_interactive_host_t *s_active_host;

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
static vprintf_like_t s_log_vprintf;

static int esp32qjs_interactive_log_vprintf(const char *fmt, va_list args)
{
    int written;

    esp32qjs_interactive_prepare_output(NULL);

    if (s_log_vprintf != NULL) {
        written = s_log_vprintf(fmt, args);
    } else {
        written = vprintf(fmt, args);
    }

    if (s_active_host != NULL) {
        esp32qjs_repl_note_external_output();
        if (s_active_host->notify_activity != NULL) {
            s_active_host->notify_activity(s_active_host->opaque);
        }
    }
    return written;
}
#endif

void esp32qjs_interactive_console_init(const esp32qjs_interactive_host_t *host)
{
#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    s_active_host = host;
    if (host != NULL) {
        esp32qjs_console_init(host->notify_activity_from_isr, host->opaque);
    } else {
        esp32qjs_console_init(NULL, NULL);
    }
#else
    (void)host;
#endif
}

size_t esp32qjs_interactive_task_stack_size(void)
{
    return (size_t)CONFIG_ESP32QJS_REPL_TASK_STACK_SIZE;
}

void esp32qjs_interactive_install_log_bridge(const esp32qjs_interactive_host_t *host)
{
#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    s_active_host = host;
    s_log_vprintf = esp_log_set_vprintf(esp32qjs_interactive_log_vprintf);
#else
    (void)host;
#endif
}

void esp32qjs_interactive_prepare_output(void *opaque)
{
#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    esp32qjs_repl_prepare_output(opaque);
#else
    (void)opaque;
#endif
}

static uint32_t host_output_generation(const esp32qjs_interactive_host_t *host)
{
    if (host == NULL || host->output_generation == NULL) {
        return 0;
    }
    return host->output_generation(host->opaque);
}

void esp32qjs_interactive_run(const esp32qjs_interactive_host_t *host,
                              const esp32qjs_interactive_banner_t *banner)
{
    esp32qjs_interactive_poll_result_t poll_result;
    uint32_t wait_ms;
#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    uint32_t startup_output_generation;
    bool prompt_deferred;
#endif

    s_active_host = host;

    if (host != NULL && host->attach_current_task != NULL) {
        host->attach_current_task(host->opaque);
    }

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    esp32qjs_repl_print_banner(banner);
    startup_output_generation = host_output_generation(host);
#endif
    if (host != NULL && host->run_startup != NULL) {
        host->run_startup(host->opaque);
    }

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    /* If startup JS already wrote to the console, defer the first prompt until
     * later JS output or real user input. Once prompt bytes hit the serial log,
     * later ANSI clears cannot remove them from monitor captures. */
    prompt_deferred = host_output_generation(host) != startup_output_generation;
    if (!prompt_deferred) {
        esp32qjs_repl_print_prompt();
    }
#endif

    while (true) {
        if (host != NULL && host->poll != NULL) {
            poll_result = host->poll(host->opaque);
        } else {
            poll_result = ESP32QJS_INTERACTIVE_POLL_NONE;
        }

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
        if ((poll_result & ESP32QJS_INTERACTIVE_POLL_OUTPUT) != 0) {
            esp32qjs_repl_redraw_line();
            prompt_deferred = false;
            continue;
        }

        if (esp32qjs_repl_process_input(host)) {
            prompt_deferred = false;
            continue;
        }

        if (!prompt_deferred && esp32qjs_repl_external_output_pending()) {
            wait_ms = esp32qjs_repl_external_output_wait_ms();
            if (wait_ms == 0) {
                if (esp32qjs_repl_should_restore_prompt()) {
                    esp32qjs_repl_redraw_line();
                    continue;
                }
                wait_ms = UINT32_MAX;
            }
        } else {
            wait_ms = UINT32_MAX;
        }
#else
        wait_ms = UINT32_MAX;
#endif

        if (poll_result == ESP32QJS_INTERACTIVE_POLL_NONE &&
            host != NULL &&
            host->wait_for_activity != NULL) {
            host->wait_for_activity(host->opaque, wait_ms);
        }
    }
}
