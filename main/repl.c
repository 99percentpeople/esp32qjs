#include "repl.h"

#include <stdio.h>

#include "sdkconfig.h"
#include "repl_input.h"

void esp32qjs_repl_print_banner(void)
{
    printf("\n");
    printf("mquickjs REPL on %s\n", CONFIG_ESP32_MQUICKJS_BOARD_NAME);
    printf("Type JavaScript and press Enter.\n");
    printf("Run help() for usage.\n");
}

bool esp32qjs_repl_process_input(JSContext *ctx,
                                 esp32_mquickjs_runtime_t *runtime)
{
    char line[ESP32QJS_REPL_LINE_SIZE];
    JSValue result;

    if (!esp32qjs_repl_read_line(line, sizeof(line))) {
        return false;
    }

    if (line[0] == '\0') {
        esp32qjs_repl_print_prompt();
        return true;
    }

    esp32qjs_repl_history_push(line);

    result = esp32_mquickjs_eval(ctx,
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
    return true;
}
