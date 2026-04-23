#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t rows;
    size_t cursor_row;
    size_t cursor_col;
} esp32qjs_repl_layout_t;

bool esp32qjs_repl_needs_multiline_continuation(const char *buf, size_t len);

void esp32qjs_repl_compute_layout(const char *buf,
                                  size_t len,
                                  size_t cursor,
                                  size_t display_columns,
                                  size_t first_prompt_len,
                                  size_t continuation_prompt_len,
                                  esp32qjs_repl_layout_t *layout);
