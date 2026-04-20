#include "runtime.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#include "sdkconfig.h"

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
#include "repl.h"
#include "repl_input.h"
#endif

#define STARTUP_SCRIPT_PATH ESP32_MQUICKJS_LITTLEFS_BASE_PATH "/index.js"

static void run_optional_script(JSContext *ctx,
                                esp32_mquickjs_runtime_t *runtime,
                                const char *absolute_path,
                                const char *relative_path,
                                const char *source_name)
{
    struct stat st;
    char command[96];
    JSValue result;

    if (stat(absolute_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return;
    }

    snprintf(command, sizeof(command), "load('%s')", relative_path);
    result = esp32_mquickjs_eval(ctx, runtime, command, source_name, 0);
    if (JS_IsException(result)) {
        esp32_mquickjs_print_exception(ctx);
    }
}

static void run_startup_script(JSContext *ctx,
                               esp32_mquickjs_runtime_t *runtime)
{
#ifdef CONFIG_ESP32QJS_AUTORUN_INDEX_JS
    run_optional_script(ctx, runtime, STARTUP_SCRIPT_PATH, "index.js", "<startup>");
#else
    (void)ctx;
    (void)runtime;
#endif
}

void esp32qjs_runtime_run(JSContext *ctx,
                          esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_poll_result_t poll_result;
    uint32_t wait_ms;
#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    uint32_t startup_output_generation;
    bool prompt_deferred;
#endif

    esp32_mquickjs_attach_current_task(runtime);

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    esp32qjs_repl_print_banner();
    startup_output_generation = runtime->output_generation;
#endif
    run_startup_script(ctx, runtime);

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    /* If startup JS already wrote to the console, defer the first prompt until
     * later JS output or real user input. Once prompt bytes hit the serial log,
     * later ANSI clears cannot remove them from monitor captures. */
    prompt_deferred = runtime->output_generation != startup_output_generation;
    if (!prompt_deferred) {
        esp32qjs_repl_print_prompt();
    }
#endif

    while (true) {
        poll_result = esp32_mquickjs_poll(ctx, runtime);

#ifdef CONFIG_ESP32QJS_ENABLE_REPL
        if ((poll_result & ESP32_MQUICKJS_POLL_OUTPUT) != 0) {
            esp32qjs_repl_redraw_line();
            prompt_deferred = false;
            continue;
        }

        if (esp32qjs_repl_process_input(ctx, runtime)) {
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

        if (poll_result == ESP32_MQUICKJS_POLL_NONE) {
            esp32_mquickjs_wait_for_activity(runtime, wait_ms);
        }
    }
}
