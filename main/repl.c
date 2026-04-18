#include "repl.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "repl_input.h"

static void print_banner(void)
{
    printf("\n");
    printf("mquickjs REPL on ESP32-S3\n");
    printf("Type JavaScript and press Enter.\n");
    printf("Special commands: .help .gc .mem\n");
    printf("Examples: 1 + 2, print('hello'), esp32.info(), esp32.led(true)\n");
    printf("Timers: setTimeout(function() { print('tick'); }, 1000), setInterval(function() { esp32.led(true); }, 500)\n");
    printf("GPIO: esp32.pinMode(21, esp32.OUTPUT), esp32.digitalWrite(21, 1)\n");
}

static void handle_meta_command(JSContext *ctx, const char *line)
{
    if (strcmp(line, ".help") == 0) {
        print_banner();
        return;
    }
    if (strcmp(line, ".gc") == 0) {
        JS_GC(ctx);
        printf("GC complete\n");
        return;
    }
    if (strcmp(line, ".mem") == 0) {
        printf("free_heap=%u\n", (unsigned)esp_get_free_heap_size());
        return;
    }
    printf("Unknown command: %s\n", line);
}

void esp32qjs_repl_run(JSContext *ctx,
                       esp32_mquickjs_runtime_t *runtime)
{
    char line[ESP32QJS_REPL_LINE_SIZE];

    print_banner();
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

        if (line[0] == '.') {
            handle_meta_command(ctx, line);
            esp32qjs_repl_print_prompt();
            continue;
        }

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
