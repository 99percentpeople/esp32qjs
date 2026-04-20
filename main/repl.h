#pragma once

#include "esp32_mquickjs.h"

void esp32qjs_repl_print_banner(void);

bool esp32qjs_repl_process_input(JSContext *ctx,
                                 esp32_mquickjs_runtime_t *runtime);
