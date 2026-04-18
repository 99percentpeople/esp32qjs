#include "repl_input.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define REPL_PROMPT "js> "
#define REPL_CONT_PROMPT "... "
#define HISTORY_SIZE 16

typedef enum {
    ASYNC_PROMPT_NONE = 0,
    ASYNC_PROMPT_RESTORE_BLOCK,
    ASYNC_PROMPT_RESTORE_SINGLE_LINE,
} async_prompt_mode_t;

static char s_history[HISTORY_SIZE][ESP32QJS_REPL_LINE_SIZE];
static size_t s_history_len;

static char s_edit_buf[ESP32QJS_REPL_LINE_SIZE];
static char s_draft_buf[ESP32QJS_REPL_LINE_SIZE];
static size_t s_edit_len;
static size_t s_cursor;
static bool s_swallow_lf;
static int s_escape_state;
static int s_csi_num;
static int s_history_index = -1;
static size_t s_rendered_rows = 1;
static size_t s_rendered_cursor_line;
static bool s_prompt_visible;
static bool s_cursor_hidden;
static async_prompt_mode_t s_async_prompt_mode;

static int read_console_char(void)
{
    uint8_t ch = 0;

    if (usb_serial_jtag_read_bytes(&ch, 1, 0) == 1) {
        return ch;
    }
    return EOF;
}

static const char *prompt_for_line(size_t line)
{
    return line == 0 ? REPL_PROMPT : REPL_CONT_PROMPT;
}

static size_t prompt_len_for_line(size_t line)
{
    return line == 0 ? sizeof(REPL_PROMPT) - 1 : sizeof(REPL_CONT_PROMPT) - 1;
}

static size_t count_render_rows(const char *buf, size_t len)
{
    size_t rows = 1;

    for (size_t i = 0; i < len; ++i) {
        if (buf[i] == '\n') {
            rows++;
        }
    }
    return rows;
}

static void cursor_line_col(const char *buf, size_t cursor, size_t *line, size_t *col)
{
    size_t current_line = 0;
    size_t current_col = 0;

    for (size_t i = 0; i < cursor; ++i) {
        if (buf[i] == '\n') {
            current_line++;
            current_col = 0;
        } else {
            current_col++;
        }
    }

    *line = current_line;
    *col = current_col;
}

static void move_to_render_top(void)
{
    printf("\r");
    for (size_t i = 0; i < s_rendered_cursor_line; ++i) {
        printf("\x1b[1A\r");
    }
}

static void clear_rendered_block(void)
{
    if (!s_prompt_visible) {
        return;
    }

    move_to_render_top();
    for (size_t i = 0; i < s_rendered_rows; ++i) {
        printf("\x1b[2K");
        if (i + 1 < s_rendered_rows) {
            printf("\x1b[1B\r");
        }
    }
    for (size_t i = 1; i < s_rendered_rows; ++i) {
        printf("\x1b[1A\r");
    }
    printf("\r");
}

static void hide_cursor(void)
{
    if (!s_cursor_hidden) {
        printf("\x1b[?25l");
        s_cursor_hidden = true;
    }
}

static void show_cursor(void)
{
    if (s_cursor_hidden) {
        printf("\x1b[?25h");
        s_cursor_hidden = false;
    }
}

static bool needs_multiline_continuation(const char *buf, size_t len)
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

static void redraw_repl_line(const char *buf, size_t len, size_t cursor)
{
    size_t rows = count_render_rows(buf, len);
    size_t cursor_line;
    size_t cursor_col;

    hide_cursor();
    clear_rendered_block();
    fputs(prompt_for_line(0), stdout);
    for (size_t i = 0, line = 0; i < len; ++i) {
        if (buf[i] == '\n') {
            line++;
            printf("\n%s", prompt_for_line(line));
            continue;
        }
        fputc(buf[i], stdout);
    }

    s_rendered_rows = rows;
    s_prompt_visible = true;
    cursor_line_col(buf, cursor, &cursor_line, &cursor_col);
    s_rendered_cursor_line = cursor_line;
    if (rows > 1) {
        printf("\r\x1b[%uA", (unsigned)(rows - 1));
    } else {
        printf("\r");
    }
    if (cursor_line > 0) {
        printf("\x1b[%uB", (unsigned)cursor_line);
    }
    if (prompt_len_for_line(cursor_line) + cursor_col > 0) {
        printf("\x1b[%uC", (unsigned)(prompt_len_for_line(cursor_line) + cursor_col));
    }
    show_cursor();
    fflush(stdout);
}

static void redraw_single_line_prompt(const char *buf, size_t len, size_t cursor)
{
    hide_cursor();
    fputs(REPL_PROMPT, stdout);
    if (len > 0) {
        fwrite(buf, 1, len, stdout);
    }

    s_rendered_rows = 1;
    s_rendered_cursor_line = 0;
    s_prompt_visible = true;
    printf("\r");
    if (prompt_len_for_line(0) + cursor > 0) {
        printf("\x1b[%uC", (unsigned)(prompt_len_for_line(0) + cursor));
    }
    show_cursor();
    fflush(stdout);
}

static void append_char_direct(char ch)
{
    fputc(ch, stdout);
    fflush(stdout);
}

static void delete_char_before_cursor(char *buf, size_t *len, size_t *cursor);

static bool delete_char_direct(void)
{
    if (!s_prompt_visible || s_cursor == 0 || s_cursor != s_edit_len) {
        return false;
    }
    if (s_edit_len == 0 || s_edit_buf[s_edit_len - 1] == '\n') {
        return false;
    }

    delete_char_before_cursor(s_edit_buf, &s_edit_len, &s_cursor);
    printf("\b\x1b[K");
    fflush(stdout);
    return true;
}

static void delete_char_before_cursor(char *buf, size_t *len, size_t *cursor)
{
    if (*cursor == 0) {
        return;
    }

    memmove(&buf[*cursor - 1], &buf[*cursor], *len - *cursor + 1);
    (*cursor)--;
    (*len)--;
}

static void delete_char_at_cursor(char *buf, size_t *len, size_t cursor)
{
    if (cursor >= *len) {
        return;
    }

    memmove(&buf[cursor], &buf[cursor + 1], *len - cursor);
    (*len)--;
}

static void insert_char_at_cursor(char *buf, size_t buf_size, size_t *len, size_t *cursor, char ch)
{
    if (*len >= buf_size - 1) {
        return;
    }

    if (*cursor < *len) {
        memmove(&buf[*cursor + 1], &buf[*cursor], *len - *cursor + 1);
    } else {
        buf[*len + 1] = '\0';
    }

    buf[*cursor] = ch;
    (*cursor)++;
    (*len)++;
    buf[*len] = '\0';
}

static void delete_word_before_cursor(char *buf, size_t *len, size_t *cursor)
{
    size_t start;

    if (*cursor == 0) {
        return;
    }

    start = *cursor;
    while (start > 0 && isspace((unsigned char)buf[start - 1])) {
        start--;
    }
    while (start > 0 && !isspace((unsigned char)buf[start - 1])) {
        start--;
    }

    memmove(&buf[start], &buf[*cursor], *len - *cursor + 1);
    *len -= (*cursor - start);
    *cursor = start;
}

static void editor_set_line(const char *src)
{
    size_t copy_len = strlen(src);

    if (copy_len >= sizeof(s_edit_buf)) {
        copy_len = sizeof(s_edit_buf) - 1;
    }

    memcpy(s_edit_buf, src, copy_len);
    s_edit_buf[copy_len] = '\0';
    s_edit_len = copy_len;
    s_cursor = copy_len;
    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
}

static void editor_clear_line(void)
{
    s_edit_buf[0] = '\0';
    s_edit_len = 0;
    s_cursor = 0;
    s_rendered_cursor_line = 0;
}

static void history_restore(bool up)
{
    if (s_history_len == 0) {
        return;
    }

    if (up) {
        if (s_history_index < 0) {
            strncpy(s_draft_buf, s_edit_buf, sizeof(s_draft_buf) - 1);
            s_draft_buf[sizeof(s_draft_buf) - 1] = '\0';
            s_history_index = (int)s_history_len - 1;
        } else if (s_history_index > 0) {
            s_history_index--;
        }

        editor_set_line(s_history[s_history_index]);
        return;
    }

    if (s_history_index < 0) {
        return;
    }

    if (s_history_index < (int)s_history_len - 1) {
        s_history_index++;
        editor_set_line(s_history[s_history_index]);
    } else {
        s_history_index = -1;
        editor_set_line(s_draft_buf);
    }
}

void esp32qjs_console_init(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    usb_serial_jtag_vfs_use_driver();
}

void esp32qjs_repl_print_prompt(void)
{
    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
}

void esp32qjs_repl_redraw_line(void)
{
    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
}

void esp32qjs_repl_prepare_async_output(void *opaque)
{
    (void)opaque;

    if (!s_prompt_visible) {
        return;
    }

    hide_cursor();
    clear_rendered_block();
    s_prompt_visible = false;
    s_async_prompt_mode = ASYNC_PROMPT_NONE;
}

void esp32qjs_repl_begin_async_output(void *opaque, uint32_t lines)
{
    (void)opaque;
    (void)lines;

    if (!s_prompt_visible) {
        s_async_prompt_mode = ASYNC_PROMPT_NONE;
        return;
    }

    hide_cursor();
    if (s_rendered_rows == 1) {
        printf("\r\x1b[2K");
        fflush(stdout);
        s_prompt_visible = false;
        s_async_prompt_mode = ASYNC_PROMPT_RESTORE_SINGLE_LINE;
        return;
    }

    clear_rendered_block();
    s_prompt_visible = false;
    s_async_prompt_mode = ASYNC_PROMPT_RESTORE_BLOCK;
}

void esp32qjs_repl_end_async_output(void *opaque)
{
    (void)opaque;

    switch (s_async_prompt_mode) {
        case ASYNC_PROMPT_RESTORE_SINGLE_LINE:
            redraw_single_line_prompt(s_edit_buf, s_edit_len, s_cursor);
            break;
        case ASYNC_PROMPT_RESTORE_BLOCK:
            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
            break;
        case ASYNC_PROMPT_NONE:
        default:
            show_cursor();
            break;
    }

    s_async_prompt_mode = ASYNC_PROMPT_NONE;
}

void esp32qjs_repl_history_push(const char *line)
{
    if (line[0] == '\0') {
        return;
    }
    if (s_history_len > 0 && strcmp(s_history[s_history_len - 1], line) == 0) {
        return;
    }

    if (s_history_len < HISTORY_SIZE) {
        strncpy(s_history[s_history_len], line, ESP32QJS_REPL_LINE_SIZE - 1);
        s_history[s_history_len][ESP32QJS_REPL_LINE_SIZE - 1] = '\0';
        s_history_len++;
        return;
    }

    for (size_t i = 1; i < HISTORY_SIZE; ++i) {
        memcpy(s_history[i - 1], s_history[i], ESP32QJS_REPL_LINE_SIZE);
    }
    strncpy(s_history[HISTORY_SIZE - 1], line, ESP32QJS_REPL_LINE_SIZE - 1);
    s_history[HISTORY_SIZE - 1][ESP32QJS_REPL_LINE_SIZE - 1] = '\0';
}

bool esp32qjs_repl_read_line(char *out_buf, size_t out_buf_size)
{
    int ch = read_console_char();

    if (ch == EOF) {
        vTaskDelay(pdMS_TO_TICKS(10));
        return false;
    }

    if (s_swallow_lf && ch == '\n') {
        s_swallow_lf = false;
        return false;
    }
    s_swallow_lf = false;

    if (s_escape_state == 0 && ch == 0x1b) {
        s_escape_state = 1;
        return false;
    }

    if (s_escape_state == 1) {
        if (ch == '[') {
            s_escape_state = 2;
            s_csi_num = 0;
            return false;
        }
        if (ch == 'O') {
            s_escape_state = 3;
            return false;
        }
        s_escape_state = 0;
        return false;
    }

    if (s_escape_state == 2) {
        if (ch >= '0' && ch <= '9') {
            s_csi_num = s_csi_num * 10 + (ch - '0');
            return false;
        }

        if (ch == '~') {
            switch (s_csi_num) {
                case 1:
                case 7:
                    s_cursor = 0;
                    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                    break;
                case 3:
                    delete_char_at_cursor(s_edit_buf, &s_edit_len, s_cursor);
                    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                    break;
                case 4:
                case 8:
                    s_cursor = s_edit_len;
                    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                    break;
                default:
                    break;
            }
            s_escape_state = 0;
            s_csi_num = 0;
            return false;
        }

        switch (ch) {
            case 'A':
                history_restore(true);
                break;
            case 'B':
                history_restore(false);
                break;
            case 'C':
                if (s_cursor < s_edit_len) {
                    s_cursor++;
                    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                }
                break;
            case 'D':
                if (s_cursor > 0) {
                    s_cursor--;
                    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                }
                break;
            case 'H':
                s_cursor = 0;
                redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                break;
            case 'F':
                s_cursor = s_edit_len;
                redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                break;
            default:
                break;
        }

        s_escape_state = 0;
        s_csi_num = 0;
        return false;
    }

    if (s_escape_state == 3) {
        switch (ch) {
            case 'A':
                history_restore(true);
                break;
            case 'B':
                history_restore(false);
                break;
            case 'C':
                if (s_cursor < s_edit_len) {
                    s_cursor++;
                    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                }
                break;
            case 'D':
                if (s_cursor > 0) {
                    s_cursor--;
                    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                }
                break;
            case 'H':
                s_cursor = 0;
                redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                break;
            case 'F':
                s_cursor = s_edit_len;
                redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                break;
            default:
                break;
        }

        s_escape_state = 0;
        return false;
    }

    if (ch == 0x01) {
        s_cursor = 0;
        redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
        return false;
    }

    if (ch == 0x05) {
        s_cursor = s_edit_len;
        redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
        return false;
    }

    if (ch == 0x15) {
        if (s_cursor > 0) {
            memmove(s_edit_buf, &s_edit_buf[s_cursor], s_edit_len - s_cursor + 1);
            s_edit_len -= s_cursor;
            s_cursor = 0;
            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
        }
        s_history_index = -1;
        return false;
    }

    if (ch == 0x0b) {
        if (s_cursor < s_edit_len) {
            s_edit_buf[s_cursor] = '\0';
            s_edit_len = s_cursor;
            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
        }
        s_history_index = -1;
        return false;
    }

    if (ch == 0x17) {
        delete_word_before_cursor(s_edit_buf, &s_edit_len, &s_cursor);
        redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
        s_history_index = -1;
        return false;
    }

    if (ch == 0x0c) {
        printf("\x1b[2J\x1b[H");
        redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
        return false;
    }

    if (ch == '\r' || ch == '\n') {
        size_t copy_len = s_edit_len;
        bool continue_multiline = needs_multiline_continuation(s_edit_buf, s_cursor);

        if (ch == '\r') {
            s_swallow_lf = true;
        }

        if (continue_multiline) {
            insert_char_at_cursor(s_edit_buf, sizeof(s_edit_buf), &s_edit_len, &s_cursor, '\n');
            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
            s_history_index = -1;
            return false;
        }

        s_edit_buf[s_edit_len] = '\0';
        if (copy_len >= out_buf_size) {
            copy_len = out_buf_size - 1;
        }

        memcpy(out_buf, s_edit_buf, copy_len);
        out_buf[copy_len] = '\0';

        redraw_repl_line(s_edit_buf, s_edit_len, s_edit_len);
        printf("\n");
        fflush(stdout);

        editor_clear_line();
        s_draft_buf[0] = '\0';
        s_history_index = -1;
        s_escape_state = 0;
        s_csi_num = 0;
        s_rendered_rows = 1;
        s_prompt_visible = false;
        return true;
    }

    if (ch == '\b' || ch == 127) {
        if (s_cursor > 0) {
            if (!delete_char_direct()) {
                delete_char_before_cursor(s_edit_buf, &s_edit_len, &s_cursor);
                redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
            }
        }
        s_history_index = -1;
        return false;
    }

    if (!isprint((unsigned char)ch)) {
        return false;
    }

    insert_char_at_cursor(s_edit_buf, sizeof(s_edit_buf), &s_edit_len, &s_cursor, (char)ch);
    if (s_cursor == s_edit_len) {
        append_char_direct((char)ch);
    } else {
        redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
    }
    s_history_index = -1;
    return false;
}
