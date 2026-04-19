#include "repl.h"

#include <stdio.h>
#include <sys/stat.h>

#include "repl_input.h"

#define REPL_STARTUP_SCRIPT_PATH ESP32_MQUICKJS_LITTLEFS_BASE_PATH "/index.js"

static void run_startup_script(JSContext *ctx,
                               esp32_mquickjs_runtime_t *runtime)
{
#ifdef CONFIG_ESP32QJS_AUTORUN_INDEX_JS
    struct stat st;
    JSValue result;

    if (stat(REPL_STARTUP_SCRIPT_PATH, &st) != 0 || !S_ISREG(st.st_mode)) {
        return;
    }

    result = esp32_mquickjs_eval(ctx,
                                 runtime,
                                 "load('index.js')",
                                 "<startup>",
                                 0);
    if (JS_IsException(result)) {
        esp32_mquickjs_print_exception(ctx);
    }
#else
    (void)ctx;
    (void)runtime;
#endif
}

static void print_banner(void)
{
    printf("\n");
    printf("mquickjs REPL on ESP32-S3\n");
    printf("Type JavaScript and press Enter.\n");
    printf("Run help() for usage.\n");
}

void esp32qjs_repl_run(JSContext *ctx,
                       esp32_mquickjs_runtime_t *runtime)
{
    char line[ESP32QJS_REPL_LINE_SIZE];

    print_banner();
    run_startup_script(ctx, runtime);
    esp32qjs_repl_print_prompt();

    while (true) {
        if (esp32_mquickjs_poll(ctx, runtime)) {
            esp32qjs_repl_redraw_line();
        }

        if (!esp32qjs_repl_read_line(line, sizeof(line))) {
            continue;
        }

        if (line[0] == '\0') {
            esp32qjs_repl_print_prompt();
            continue;
        }

        esp32qjs_repl_history_push(line);

        JSValue result = esp32_mquickjs_eval(ctx,
                                             runtime,
                                             line,
                                             "<repl>",
                                             JS_EVAL_REPL | JS_EVAL_RETVAL);

        if (JS_IsException(result)) {
            esp32_mquickjs_print_exception(ctx);
        } else if (!JS_IsUndefined(result)) {
            JS_PrintValueF(ctx, result, JS_DUMP_LONG);
            printf("\n");
        }

        esp32qjs_repl_print_prompt();
    }
}
