#include "repl_line.h"

#include <stdbool.h>

static size_t prompt_len_for_line(size_t line,
                                  size_t first_prompt_len,
                                  size_t continuation_prompt_len)
{
    return line == 0 ? first_prompt_len : continuation_prompt_len;
}

bool esp32qjs_repl_needs_multiline_continuation(const char *buf, size_t len)
{
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool in_template = false;
    bool in_line_comment = false;
    bool in_block_comment = false;
    bool escaped = false;

    for (size_t i = 0; i < len; ++i) {
        char ch = buf[i];
        char next = (i + 1 < len) ? buf[i + 1] : '\0';

        if (in_line_comment) {
            if (ch == '\n') {
                in_line_comment = false;
            }
            continue;
        }

        if (in_block_comment) {
            if (ch == '*' && next == '/') {
                in_block_comment = false;
                i++;
            }
            continue;
        }

        if (in_single_quote) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '\'') {
                in_single_quote = false;
            }
            continue;
        }

        if (in_double_quote) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_double_quote = false;
            }
            continue;
        }

        if (in_template) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '`') {
                in_template = false;
            }
            continue;
        }

        if (ch == '/' && next == '/') {
            in_line_comment = true;
            i++;
            continue;
        }

        if (ch == '/' && next == '*') {
            in_block_comment = true;
            i++;
            continue;
        }

        if (ch == '\'') {
            in_single_quote = true;
            continue;
        }

        if (ch == '"') {
            in_double_quote = true;
            continue;
        }

        if (ch == '`') {
            in_template = true;
            continue;
        }

        switch (ch) {
            case '(':
                paren_depth++;
                break;
            case ')':
                if (paren_depth > 0) {
                    paren_depth--;
                }
                break;
            case '[':
                bracket_depth++;
                break;
            case ']':
                if (bracket_depth > 0) {
                    bracket_depth--;
                }
                break;
            case '{':
                brace_depth++;
                break;
            case '}':
                if (brace_depth > 0) {
                    brace_depth--;
                }
                break;
            default:
                break;
        }
    }

    return paren_depth > 0 || bracket_depth > 0 || brace_depth > 0 ||
           in_single_quote || in_double_quote || in_template || in_block_comment;
}

void esp32qjs_repl_compute_layout(const char *buf,
                                  size_t len,
                                  size_t cursor,
                                  size_t display_columns,
                                  size_t first_prompt_len,
                                  size_t continuation_prompt_len,
                                  esp32qjs_repl_layout_t *layout)
{
    size_t logical_line = 0;
    size_t physical_row = 0;
    size_t physical_col = prompt_len_for_line(0, first_prompt_len, continuation_prompt_len);
    bool cursor_set = false;

    if (layout == NULL) {
        return;
    }

    if (display_columns == 0) {
        display_columns = 1;
    }

    if (cursor == 0) {
        layout->cursor_row = physical_row;
        layout->cursor_col = physical_col;
        cursor_set = true;
    }

    for (size_t i = 0; i < len; ++i) {
        if (buf[i] == '\n') {
            logical_line++;
            physical_row++;
            physical_col = prompt_len_for_line(logical_line, first_prompt_len, continuation_prompt_len);
            if (cursor == i + 1) {
                layout->cursor_row = physical_row;
                layout->cursor_col = physical_col;
                cursor_set = true;
            }
            continue;
        }

        if (physical_col >= display_columns) {
            physical_row++;
            physical_col = 0;
        }

        if (!cursor_set && cursor == i) {
            layout->cursor_row = physical_row;
            layout->cursor_col = physical_col;
            cursor_set = true;
        }

        physical_col++;

        if (cursor == i + 1) {
            layout->cursor_row = physical_row;
            layout->cursor_col = physical_col;
            cursor_set = true;
        }
    }

    if (!cursor_set) {
        layout->cursor_row = physical_row;
        layout->cursor_col = physical_col;
    }

    layout->rows = physical_row + 1;
}
