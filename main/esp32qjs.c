#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "esp32_mquickjs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

static const char *TAG = "esp32qjs";

#define REPL_PROMPT "js> "
#define REPL_LINE_SIZE 512
#define JS_HEAP_SIZE (160 * 1024)

#define HISTORY_SIZE 16

static char history[HISTORY_SIZE][REPL_LINE_SIZE];
static size_t history_len = 0;

static void print_banner(void)
{
    printf("\n");
    printf("mquickjs REPL on ESP32-S3\n");
    printf("Type JavaScript and press Enter.\n");
    printf("Special commands: .help .gc .mem\n");
    printf("Examples: 1 + 2, print('hello'), Date.now(), Math.sin(0.5)\n");
}

static void print_prompt(void)
{
    printf(REPL_PROMPT);
    fflush(stdout);
}

static void handle_meta_command(JSContext *ctx, const char *line)
{
    if (strcmp(line, ".help") == 0)
    {
        print_banner();
        return;
    }
    if (strcmp(line, ".gc") == 0)
    {
        JS_GC(ctx);
        printf("GC complete\n");
        return;
    }
    if (strcmp(line, ".mem") == 0)
    {
        printf("free_heap=%u\n", (unsigned)esp_get_free_heap_size());
        return;
    }
    printf("Unknown command: %s\n", line);
}

static void setup_console_io(void)
{
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    usb_serial_jtag_vfs_use_driver();
}

static void redraw_repl_line(const char *buf, size_t len, size_t cursor)
{
    printf("\r" REPL_PROMPT);
    if (len > 0)
    {
        fwrite(buf, 1, len, stdout);
    }
    printf("\x1b[K"); // clear to end of line

    if (len > cursor)
    {
        printf("\x1b[%uD", (unsigned)(len - cursor));
    }
    fflush(stdout);
}

static void history_push(const char *line)
{
    if (line[0] == '\0')
    {
        return;
    }

    if (history_len > 0 && strcmp(history[history_len - 1], line) == 0)
    {
        return;
    }

    if (history_len < HISTORY_SIZE)
    {
        strncpy(history[history_len], line, REPL_LINE_SIZE - 1);
        history[history_len][REPL_LINE_SIZE - 1] = '\0';
        history_len++;
        return;
    }

    for (size_t i = 1; i < HISTORY_SIZE; ++i)
    {
        memcpy(history[i - 1], history[i], REPL_LINE_SIZE);
    }
    strncpy(history[HISTORY_SIZE - 1], line, REPL_LINE_SIZE - 1);
    history[HISTORY_SIZE - 1][REPL_LINE_SIZE - 1] = '\0';
}

static void delete_char_before_cursor(char *buf, size_t *len, size_t *cursor)
{
    if (*cursor == 0)
    {
        return;
    }

    memmove(&buf[*cursor - 1], &buf[*cursor], *len - *cursor + 1);
    (*cursor)--;
    (*len)--;
}

static void delete_char_at_cursor(char *buf, size_t *len, size_t cursor)
{
    if (cursor >= *len)
    {
        return;
    }

    memmove(&buf[cursor], &buf[cursor + 1], *len - cursor);
    (*len)--;
}

static void insert_char_at_cursor(char *buf, size_t buf_size, size_t *len, size_t *cursor, char ch)
{
    if (*len >= buf_size - 1)
    {
        return;
    }

    if (*cursor < *len)
    {
        memmove(&buf[*cursor + 1], &buf[*cursor], *len - *cursor + 1);
    }
    else
    {
        buf[*len + 1] = '\0';
    }

    buf[*cursor] = ch;
    (*cursor)++;
    (*len)++;
    buf[*len] = '\0';
}

static void delete_word_before_cursor(char *buf, size_t *len, size_t *cursor)
{
    if (*cursor == 0)
    {
        return;
    }

    size_t start = *cursor;

    while (start > 0 && isspace((unsigned char)buf[start - 1]))
    {
        start--;
    }
    while (start > 0 && !isspace((unsigned char)buf[start - 1]))
    {
        start--;
    }

    memmove(&buf[start], &buf[*cursor], *len - *cursor + 1);
    *len -= (*cursor - start);
    *cursor = start;
}
static void editor_set_line(
    char *edit_buf,
    size_t edit_buf_size,
    size_t *len,
    size_t *cursor,
    const char *src
) {
    size_t n = strlen(src);
    if (n >= edit_buf_size) {
        n = edit_buf_size - 1;
    }
    memcpy(edit_buf, src, n);
    edit_buf[n] = '\0';
    *len = n;
    *cursor = n;
    redraw_repl_line(edit_buf, *len, *cursor);
}

static void editor_clear_line(char *edit_buf, size_t *len, size_t *cursor) {
    edit_buf[0] = '\0';
    *len = 0;
    *cursor = 0;
}

static void history_restore(
    char *edit_buf,
    size_t edit_buf_size,
    size_t *len,
    size_t *cursor,
    int *history_index,
    char *draft,
    bool up
) {
    if (history_len == 0) {
        return;
    }

    if (up) {
        if (*history_index < 0) {
            strncpy(draft, edit_buf, REPL_LINE_SIZE - 1);
            draft[REPL_LINE_SIZE - 1] = '\0';
            *history_index = (int)history_len - 1;
        } else if (*history_index > 0) {
            (*history_index)--;
        }

        editor_set_line(edit_buf, edit_buf_size, len, cursor, history[*history_index]);
        return;
    }

    if (*history_index < 0) {
        return;
    }

    if (*history_index < (int)history_len - 1) {
        (*history_index)++;
        editor_set_line(edit_buf, edit_buf_size, len, cursor, history[*history_index]);
    } else {
        *history_index = -1;
        editor_set_line(edit_buf, edit_buf_size, len, cursor, draft);
    }
}

static bool read_repl_line(char *out_buf, size_t out_buf_size) {
    static char edit_buf[REPL_LINE_SIZE];
    static char draft[REPL_LINE_SIZE];

    static size_t len = 0;
    static size_t cursor = 0;
    static bool swallow_lf = false;

    // 0: normal, 1: ESC, 2: ESC[, 3: ESCO
    static int esc_state = 0;
    static int csi_num = 0;

    static int history_index = -1;  // -1 表示当前编辑态

    int ch = fgetc(stdin);
    if (ch == EOF) {
        vTaskDelay(pdMS_TO_TICKS(10));
        return false;
    }

    if (swallow_lf && ch == '\n') {
        swallow_lf = false;
        return false;
    }
    swallow_lf = false;

    // ESC start
    if (esc_state == 0 && ch == 0x1b) {
        esc_state = 1;
        return false;
    }

    if (esc_state == 1) {
        if (ch == '[') {
            esc_state = 2;
            csi_num = 0;
            return false;
        }
        if (ch == 'O') {
            esc_state = 3;
            return false;
        }
        esc_state = 0;
        return false;
    }

    if (esc_state == 2) {
        if (ch >= '0' && ch <= '9') {
            csi_num = csi_num * 10 + (ch - '0');
            return false;
        }

        if (ch == '~') {
            switch (csi_num) {
                case 1:
                case 7:
                    cursor = 0;
                    redraw_repl_line(edit_buf, len, cursor);
                    break;
                case 3:   // Delete
                    delete_char_at_cursor(edit_buf, &len, cursor);
                    redraw_repl_line(edit_buf, len, cursor);
                    break;
                case 4:
                case 8:
                    cursor = len;
                    redraw_repl_line(edit_buf, len, cursor);
                    break;
                default:
                    break;
            }
            esc_state = 0;
            csi_num = 0;
            return false;
        }

        switch (ch) {
            case 'A':   // Up
                history_restore(edit_buf, sizeof(edit_buf), &len, &cursor, &history_index, draft, true);
                break;

            case 'B':   // Down
                history_restore(edit_buf, sizeof(edit_buf), &len, &cursor, &history_index, draft, false);
                break;

            case 'C':   // Right
                if (cursor < len) {
                    cursor++;
                    redraw_repl_line(edit_buf, len, cursor);
                }
                break;

            case 'D':   // Left
                if (cursor > 0) {
                    cursor--;
                    redraw_repl_line(edit_buf, len, cursor);
                }
                break;

            case 'H':   // Home
                cursor = 0;
                redraw_repl_line(edit_buf, len, cursor);
                break;

            case 'F':   // End
                cursor = len;
                redraw_repl_line(edit_buf, len, cursor);
                break;

            default:
                break;
        }

        esc_state = 0;
        csi_num = 0;
        return false;
    }

    if (esc_state == 3) {
        switch (ch) {
            case 'A':
                history_restore(edit_buf, sizeof(edit_buf), &len, &cursor, &history_index, draft, true);
                break;

            case 'B':
                history_restore(edit_buf, sizeof(edit_buf), &len, &cursor, &history_index, draft, false);
                break;

            case 'C':
                if (cursor < len) {
                    cursor++;
                    redraw_repl_line(edit_buf, len, cursor);
                }
                break;

            case 'D':
                if (cursor > 0) {
                    cursor--;
                    redraw_repl_line(edit_buf, len, cursor);
                }
                break;

            case 'H':
                cursor = 0;
                redraw_repl_line(edit_buf, len, cursor);
                break;

            case 'F':
                cursor = len;
                redraw_repl_line(edit_buf, len, cursor);
                break;

            default:
                break;
        }

        esc_state = 0;
        return false;
    }

    if (ch == 0x01) {  // Ctrl+A
        cursor = 0;
        redraw_repl_line(edit_buf, len, cursor);
        return false;
    }

    if (ch == 0x05) {  // Ctrl+E
        cursor = len;
        redraw_repl_line(edit_buf, len, cursor);
        return false;
    }

    if (ch == 0x15) {  // Ctrl+U
        if (cursor > 0) {
            memmove(edit_buf, &edit_buf[cursor], len - cursor + 1);
            len -= cursor;
            cursor = 0;
            redraw_repl_line(edit_buf, len, cursor);
        }
        history_index = -1;
        return false;
    }

    if (ch == 0x0b) {  // Ctrl+K
        if (cursor < len) {
            edit_buf[cursor] = '\0';
            len = cursor;
            redraw_repl_line(edit_buf, len, cursor);
        }
        history_index = -1;
        return false;
    }

    if (ch == 0x17) {  // Ctrl+W
        delete_word_before_cursor(edit_buf, &len, &cursor);
        redraw_repl_line(edit_buf, len, cursor);
        history_index = -1;
        return false;
    }

    if (ch == 0x0c) {  // Ctrl+L
        printf("\x1b[2J\x1b[H");
        redraw_repl_line(edit_buf, len, cursor);
        return false;
    }

    if (ch == '\r' || ch == '\n') {
        if (ch == '\r') {
            swallow_lf = true;
        }

        edit_buf[len] = '\0';

        // 把当前编辑结果复制到输出缓冲区，交给 app_main()
        size_t n = len;
        if (n >= out_buf_size) {
            n = out_buf_size - 1;
        }
        memcpy(out_buf, edit_buf, n);
        out_buf[n] = '\0';

        printf("\n");
        fflush(stdout);

        // 重置编辑器状态，开始真正的新空行
        editor_clear_line(edit_buf, &len, &cursor);
        draft[0] = '\0';
        history_index = -1;
        esc_state = 0;
        csi_num = 0;

        return true;
    }

    if (ch == '\b' || ch == 127) {
        if (cursor > 0) {
            delete_char_before_cursor(edit_buf, &len, &cursor);
            redraw_repl_line(edit_buf, len, cursor);
        }
        history_index = -1;
        return false;
    }

    if (!isprint((unsigned char)ch)) {
        return false;
    }

    insert_char_at_cursor(edit_buf, sizeof(edit_buf), &len, &cursor, (char)ch);
    redraw_repl_line(edit_buf, len, cursor);
    history_index = -1;
    return false;
}

void app_main(void)
{
    static esp32_mquickjs_runtime_t js_runtime;
    static char line[REPL_LINE_SIZE];

    void *js_heap = NULL;
    JSContext *ctx = NULL;

    setup_console_io();

    js_heap = heap_caps_malloc(JS_HEAP_SIZE, MALLOC_CAP_8BIT);
    if (js_heap == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for JS heap", (unsigned)JS_HEAP_SIZE);
        return;
    }

    ctx = esp32_mquickjs_create(
        js_heap,
        JS_HEAP_SIZE,
        &js_runtime,
        ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS);
    if (ctx == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize mquickjs");
        heap_caps_free(js_heap);
        return;
    }

    ESP_LOGI(TAG, "mquickjs runtime ready");
    ESP_LOGI(TAG, "js_heap=%u bytes, free_heap=%u bytes",
             (unsigned)JS_HEAP_SIZE,
             (unsigned)esp_get_free_heap_size());

    print_banner();
    print_prompt();

    while (true)
    {
        if (!read_repl_line(line, sizeof(line)))
        {
            continue;
        }

        size_t len = strlen(line);
        if (len == 0)
        {
            print_prompt();
            continue;
        }

        history_push(line);

        if (line[0] == '.')
        {
            handle_meta_command(ctx, line);
            print_prompt();
            continue;
        }

        JSValue result = esp32_mquickjs_eval(
            ctx,
            &js_runtime,
            line,
            "<repl>",
            JS_EVAL_REPL | JS_EVAL_RETVAL);

        if (JS_IsException(result))
        {
            esp32_mquickjs_print_exception(ctx);
        }
        else if (!JS_IsUndefined(result))
        {
            JS_PrintValueF(ctx, result, JS_DUMP_LONG);
            printf("\n");
        }

        print_prompt();
    }
}