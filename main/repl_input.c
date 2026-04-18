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
#define HISTORY_SIZE 16

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

static int read_console_char(void)
{
    uint8_t ch = 0;

    if (usb_serial_jtag_read_bytes(&ch, 1, 0) == 1) {
        return ch;
    }
    return EOF;
}

static void redraw_repl_line(const char *buf, size_t len, size_t cursor)
{
    printf("\r" REPL_PROMPT);
    if (len > 0) {
        fwrite(buf, 1, len, stdout);
    }
    printf("\x1b[K");
    if (len > cursor) {
        printf("\x1b[%uD", (unsigned)(len - cursor));
    }
    fflush(stdout);
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
    printf(REPL_PROMPT);
    fflush(stdout);
}

void esp32qjs_repl_redraw_line(void)
{
    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
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

        if (ch == '\r') {
            s_swallow_lf = true;
        }

        s_edit_buf[s_edit_len] = '\0';
        if (copy_len >= out_buf_size) {
            copy_len = out_buf_size - 1;
        }

        memcpy(out_buf, s_edit_buf, copy_len);
        out_buf[copy_len] = '\0';

        printf("\n");
        fflush(stdout);

        editor_clear_line();
        s_draft_buf[0] = '\0';
        s_history_index = -1;
        s_escape_state = 0;
        s_csi_num = 0;
        return true;
    }

    if (ch == '\b' || ch == 127) {
        if (s_cursor > 0) {
            delete_char_before_cursor(s_edit_buf, &s_edit_len, &s_cursor);
            redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
        }
        s_history_index = -1;
        return false;
    }

    if (!isprint((unsigned char)ch)) {
        return false;
    }

    insert_char_at_cursor(s_edit_buf, sizeof(s_edit_buf), &s_edit_len, &s_cursor, (char)ch);
    redraw_repl_line(s_edit_buf, s_edit_len, s_cursor);
    s_history_index = -1;
    return false;
}
