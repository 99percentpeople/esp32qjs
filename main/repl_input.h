#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32QJS_REPL_LINE_SIZE 1024

void esp32qjs_console_init(void);
void esp32qjs_repl_print_prompt(void);
void esp32qjs_repl_redraw_line(void);
void esp32qjs_repl_prepare_output(void *opaque);
void esp32qjs_repl_history_push(const char *line);
bool esp32qjs_repl_read_line(char *out_buf, size_t out_buf_size);
