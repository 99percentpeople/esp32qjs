#include "repl_input.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_select.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp32_mquickjs.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#define REPL_PROMPT "js> "
#define REPL_CONT_PROMPT "... "
#define HISTORY_SIZE 16
/* Match the common esp-idf-monitor width so the device controls wrapping. */
#define REPL_DISPLAY_COLUMNS CONFIG_ESP32QJS_REPL_DISPLAY_COLUMNS
#define REPL_EXTERNAL_OUTPUT_SETTLE_US 50000

typedef struct {
    size_t rows;
    size_t cursor_row;
    size_t cursor_col;
} repl_layout_t;

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
static size_t s_rendered_cursor_col;
static bool s_prompt_visible;
static bool s_cursor_hidden;
static bool s_external_output_pending;
static int64_t s_last_external_output_us;

static void console_select_notif_callback(usj_select_notif_t notif, int *task_woken)
{
    if (notif == USJ_SELECT_READ_NOTIF) {
        esp32_mquickjs_notify_active_runtime_from_isr(task_woken);
    }
}

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

static void compute_repl_layout(const char *buf,
                                size_t len,
                                size_t cursor,
                                repl_layout_t *layout)
{
    size_t logical_line = 0;
    size_t physical_row = 0;
    size_t physical_col = prompt_len_for_line(0);
    bool cursor_set = false;

    if (cursor == 0) {
        layout->cursor_row = physical_row;
        layout->cursor_col = physical_col;
        cursor_set = true;
    }

    for (size_t i = 0; i < len; ++i) {
        if (buf[i] == '\n') {
            logical_line++;
            physical_row++;
            physical_col = prompt_len_for_line(logical_line);
            if (cursor == i + 1) {
                layout->cursor_row = physical_row;
                layout->cursor_col = physical_col;
                cursor_set = true;
            }
            continue;
        }

        if (physical_col >= REPL_DISPLAY_COLUMNS) {
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

static void emit_repl_buffer(const char *buf, size_t len)
{
    size_t logical_line = 0;
    size_t physical_col = prompt_len_for_line(0);

    fputs(prompt_for_line(0), stdout);
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] == '\n') {
            logical_line++;
            fputc('\n', stdout);
            fputs(prompt_for_line(logical_line), stdout);
            physical_col = prompt_len_for_line(logical_line);
            continue;
        }

        if (physical_col >= REPL_DISPLAY_COLUMNS) {
            fputc('\n', stdout);
            physical_col = 0;
        }

        fputc(buf[i], stdout);
        physical_col++;
    }
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
    repl_layout_t layout;

    hide_cursor();
    clear_rendered_block();
    compute_repl_layout(buf, len, cursor, &layout);
    emit_repl_buffer(buf, len);

    s_rendered_rows = layout.rows;
    s_prompt_visible = true;
    s_external_output_pending = false;
    s_rendered_cursor_line = layout.cursor_row;
    s_rendered_cursor_col = layout.cursor_col;
    if (layout.rows > 1) {
        printf("\r\x1b[%uA", (unsigned)(layout.rows - 1));
    } else {
        printf("\r");
    }
    if (layout.cursor_row > 0) {
        printf("\x1b[%uB", (unsigned)layout.cursor_row);
    }
    if (layout.cursor_col > 0) {
        printf("\x1b[%uC", (unsigned)layout.cursor_col);
    }
    show_cursor();
    fflush(stdout);
}

static void append_char_direct(char ch)
{
    fputc(ch, stdout);
    fflush(stdout);
}

static bool move_cursor_direct(size_t old_cursor, size_t new_cursor)
{
    repl_layout_t old_layout;
    repl_layout_t new_layout;

    if (!s_prompt_visible) {
        return false;
    }

    compute_repl_layout(s_edit_buf, s_edit_len, old_cursor, &old_layout);
    if (old_layout.rows != s_rendered_rows ||
        old_layout.cursor_row != s_rendered_cursor_line ||
        old_layout.cursor_col != s_rendered_cursor_col) {
        return false;
    }

    compute_repl_layout(s_edit_buf, s_edit_len, new_cursor, &new_layout);
    hide_cursor();
    move_to_render_top();
    if (new_layout.cursor_row > 0) {
        printf("\x1b[%uB", (unsigned)new_layout.cursor_row);
    }
    if (new_layout.cursor_col > 0) {
        printf("\x1b[%uC", (unsigned)new_layout.cursor_col);
    }
    show_cursor();
    fflush(stdout);

    s_rendered_rows = new_layout.rows;
    s_rendered_cursor_line = new_layout.cursor_row;
    s_rendered_cursor_col = new_layout.cursor_col;
    return true;
}

static void delete_char_before_cursor(char *buf, size_t *len, size_t *cursor);

static bool delete_char_direct(void)
{
    if (!s_prompt_visible || s_cursor == 0 || s_cursor != s_edit_len) {
        return false;
    }
    if (s_rendered_rows != 1 || s_rendered_cursor_line != 0) {
        return false;
    }
    if (s_edit_len == 0 || s_edit_buf[s_edit_len - 1] == '\n') {
        return false;
    }

    delete_char_before_cursor(s_edit_buf, &s_edit_len, &s_cursor);
    printf("\b\x1b[K");
    s_rendered_cursor_col = prompt_len_for_line(0) + s_cursor;
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
    s_rendered_cursor_col = prompt_len_for_line(0);
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
    usb_serial_jtag_set_select_notif_callback(console_select_notif_callback);
}

void esp32qjs_repl_print_prompt(void)
{
    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
}

void esp32qjs_repl_redraw_line(void)
{
    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
}

void esp32qjs_repl_prepare_output(void *opaque)
{
    (void)opaque;

    if (!s_prompt_visible) {
        return;
    }

    hide_cursor();
    if (s_rendered_rows == 1) {
        printf("\r\x1b[2K");
    } else {
        clear_rendered_block();
    }
    fflush(stdout);
    s_prompt_visible = false;
}

void esp32qjs_repl_note_external_output(void)
{
    s_external_output_pending = true;
    s_last_external_output_us = esp_timer_get_time();
}

bool esp32qjs_repl_external_output_pending(void)
{
    return s_external_output_pending;
}

uint32_t esp32qjs_repl_external_output_wait_ms(void)
{
    int64_t elapsed_us;

    if (!s_external_output_pending) {
        return UINT32_MAX;
    }

    elapsed_us = esp_timer_get_time() - s_last_external_output_us;
    if (elapsed_us >= REPL_EXTERNAL_OUTPUT_SETTLE_US) {
        return 0;
    }

    return (uint32_t)((REPL_EXTERNAL_OUTPUT_SETTLE_US - elapsed_us + 999) / 1000);
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
    for (;;) {
        int ch = read_console_char();

        if (ch == EOF) {
            return false;
        }

        if (s_swallow_lf && ch == '\n') {
            s_swallow_lf = false;
            continue;
        }
        s_swallow_lf = false;

        if (s_escape_state == 0 && ch == 0x1b) {
            s_escape_state = 1;
            continue;
        }

        if (s_escape_state == 1) {
            if (ch == '[') {
                s_escape_state = 2;
                s_csi_num = 0;
                continue;
            }
            if (ch == 'O') {
                s_escape_state = 3;
                continue;
            }
            s_escape_state = 0;
            continue;
        }

        if (s_escape_state == 2) {
            if (ch >= '0' && ch <= '9') {
                s_csi_num = s_csi_num * 10 + (ch - '0');
                continue;
            }

            if (ch == '~') {
                switch (s_csi_num) {
                    case 1:
                    case 7:
                    {
                        size_t old_cursor = s_cursor;
                        s_cursor = 0;
                        if (!move_cursor_direct(old_cursor, s_cursor)) {
                            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                        }
                        break;
                    }
                    case 3:
                        delete_char_at_cursor(s_edit_buf, &s_edit_len, s_cursor);
                        redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                        break;
                    case 4:
                    case 8:
                    {
                        size_t old_cursor = s_cursor;
                        s_cursor = s_edit_len;
                        if (!move_cursor_direct(old_cursor, s_cursor)) {
                            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                        }
                        break;
                    }
                    default:
                        break;
                }
                s_escape_state = 0;
                s_csi_num = 0;
                continue;
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
                        size_t old_cursor = s_cursor;
                        s_cursor++;
                        if (!move_cursor_direct(old_cursor, s_cursor)) {
                            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                        }
                    }
                    break;
                case 'D':
                    if (s_cursor > 0) {
                        size_t old_cursor = s_cursor;
                        s_cursor--;
                        if (!move_cursor_direct(old_cursor, s_cursor)) {
                            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                        }
                    }
                    break;
                case 'H':
                {
                    size_t old_cursor = s_cursor;
                    s_cursor = 0;
                    if (!move_cursor_direct(old_cursor, s_cursor)) {
                        redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                    }
                    break;
                }
                case 'F':
                {
                    size_t old_cursor = s_cursor;
                    s_cursor = s_edit_len;
                    if (!move_cursor_direct(old_cursor, s_cursor)) {
                        redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                    }
                    break;
                }
                default:
                    break;
            }

            s_escape_state = 0;
            s_csi_num = 0;
            continue;
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
                        size_t old_cursor = s_cursor;
                        s_cursor++;
                        if (!move_cursor_direct(old_cursor, s_cursor)) {
                            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                        }
                    }
                    break;
                case 'D':
                    if (s_cursor > 0) {
                        size_t old_cursor = s_cursor;
                        s_cursor--;
                        if (!move_cursor_direct(old_cursor, s_cursor)) {
                            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                        }
                    }
                    break;
                case 'H':
                {
                    size_t old_cursor = s_cursor;
                    s_cursor = 0;
                    if (!move_cursor_direct(old_cursor, s_cursor)) {
                        redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                    }
                    break;
                }
                case 'F':
                {
                    size_t old_cursor = s_cursor;
                    s_cursor = s_edit_len;
                    if (!move_cursor_direct(old_cursor, s_cursor)) {
                        redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
                    }
                    break;
                }
                default:
                    break;
            }

            s_escape_state = 0;
            continue;
        }

        if (ch == 0x01) {
            size_t old_cursor = s_cursor;
            s_cursor = 0;
            if (!move_cursor_direct(old_cursor, s_cursor)) {
                redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
            }
            continue;
        }

        if (ch == 0x05) {
            size_t old_cursor = s_cursor;
            s_cursor = s_edit_len;
            if (!move_cursor_direct(old_cursor, s_cursor)) {
                redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
            }
            continue;
        }

        if (ch == 0x15) {
            if (s_cursor > 0) {
                memmove(s_edit_buf, &s_edit_buf[s_cursor], s_edit_len - s_cursor + 1);
                s_edit_len -= s_cursor;
                s_cursor = 0;
                redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
            }
            s_history_index = -1;
            continue;
        }

        if (ch == 0x0b) {
            if (s_cursor < s_edit_len) {
                s_edit_buf[s_cursor] = '\0';
                s_edit_len = s_cursor;
                redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
            }
            s_history_index = -1;
            continue;
        }

        if (ch == 0x17) {
            delete_word_before_cursor(s_edit_buf, &s_edit_len, &s_cursor);
            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
            s_history_index = -1;
            continue;
        }

        if (ch == 0x0c) {
            printf("\x1b[2J\x1b[H");
            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
            continue;
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
                continue;
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
            continue;
        }

        if (!isprint((unsigned char)ch)) {
            continue;
        }

        insert_char_at_cursor(s_edit_buf, sizeof(s_edit_buf), &s_edit_len, &s_cursor, (char)ch);
        if (s_cursor == s_edit_len) {
            repl_layout_t layout;

            compute_repl_layout(s_edit_buf, s_edit_len, s_cursor, &layout);
            if (layout.rows == s_rendered_rows && layout.cursor_row == s_rendered_cursor_line) {
                append_char_direct((char)ch);
                s_rendered_cursor_col = layout.cursor_col;
            } else {
                redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
            }
        } else {
            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
        }
        s_history_index = -1;
    }
}
