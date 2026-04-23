#pragma once

#include "esp32qjs_interactive/esp32qjs_interactive.h"

void esp32qjs_repl_print_banner(const esp32qjs_interactive_banner_t *banner);

bool esp32qjs_repl_process_input(const esp32qjs_interactive_host_t *host);
