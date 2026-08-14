#include "esp32_mquickjs_runtime_logs.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_log_write.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define ESP32_MQUICKJS_LOG_RAW_LINE_MAX_BYTES 1024U
#define ESP32_MQUICKJS_LOG_READ_MAX_ENTRIES 16U
#define ESP32_MQUICKJS_LOG_READ_MAX_BYTES 2048U

typedef struct {
    esp32_mquickjs_log_ring_t ring;
    portMUX_TYPE lock;
    char boot_id[17];
    char console_line[ESP32_MQUICKJS_LOG_RAW_LINE_MAX_BYTES];
    size_t console_line_length;
    esp32_mquickjs_log_source_t console_source;
    bool console_open;
    bool active;
} esp32_mquickjs_runtime_logs_state_t;

static esp32_mquickjs_runtime_logs_state_t s_runtime_logs;
static esp32_mquickjs_runtime_t *s_runtime_logs_owner;
static vprintf_like_t s_previous_vprintf;

static const char *runtime_log_source_name(esp32_mquickjs_log_source_t source)
{
    switch (source) {
    case ESP32_MQUICKJS_LOG_SOURCE_EXCEPTION:
        return "exception";
    case ESP32_MQUICKJS_LOG_SOURCE_RUNTIME:
        return "runtime";
    case ESP32_MQUICKJS_LOG_SOURCE_ESP_IDF:
        return "esp-idf";
    case ESP32_MQUICKJS_LOG_SOURCE_JAVASCRIPT:
    default:
        return "javascript";
    }
}

static size_t utf8_sequence_length(const unsigned char *data, size_t remaining)
{
    unsigned char first;
    size_t length;
    size_t i;

    if (remaining == 0U) {
        return 0U;
    }
    first = data[0];
    if (first < 0x80U) {
        return 1U;
    }
    if (first >= 0xc2U && first <= 0xdfU) {
        length = 2U;
    } else if (first >= 0xe0U && first <= 0xefU) {
        length = 3U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        length = 4U;
    } else {
        return 0U;
    }
    if (remaining < length) {
        return 0U;
    }
    for (i = 1U; i < length; ++i) {
        if ((data[i] & 0xc0U) != 0x80U) {
            return 0U;
        }
    }
    if ((first == 0xe0U && data[1] < 0xa0U) ||
        (first == 0xedU && data[1] >= 0xa0U) ||
        (first == 0xf0U && data[1] < 0x90U) ||
        (first == 0xf4U && data[1] >= 0x90U)) {
        return 0U;
    }
    return length;
}

static size_t normalize_log_line(const char *input,
                                 size_t input_length,
                                 char output[ESP32_MQUICKJS_LOG_TEXT_MAX_BYTES + 1U])
{
    const unsigned char *bytes = (const unsigned char *)input;
    size_t input_index = 0U;
    size_t output_length = 0U;

    while (input_index < input_length &&
           output_length < ESP32_MQUICKJS_LOG_TEXT_MAX_BYTES) {
        unsigned char byte = bytes[input_index];
        size_t sequence_length;

        if (byte == 0x1bU && input_index + 1U < input_length &&
            bytes[input_index + 1U] == '[') {
            input_index += 2U;
            while (input_index < input_length) {
                byte = bytes[input_index++];
                if (byte >= 0x40U && byte <= 0x7eU) {
                    break;
                }
            }
            continue;
        }
        if (byte == '\t') {
            output[output_length++] = ' ';
            input_index++;
            continue;
        }
        if (byte < 0x20U || byte == 0x7fU) {
            input_index++;
            continue;
        }
        sequence_length = utf8_sequence_length(bytes + input_index,
                                               input_length - input_index);
        if (sequence_length == 0U) {
            output[output_length++] = '?';
            input_index++;
            continue;
        }
        if (output_length + sequence_length > ESP32_MQUICKJS_LOG_TEXT_MAX_BYTES) {
            break;
        }
        memcpy(output + output_length, bytes + input_index, sequence_length);
        output_length += sequence_length;
        input_index += sequence_length;
    }
    while (output_length > 0U && output[output_length - 1U] == ' ') {
        output_length--;
    }
    output[output_length] = '\0';
    return output_length;
}

static void append_line_locked(esp32_mquickjs_runtime_logs_state_t *state,
                               esp32_mquickjs_log_source_t source,
                               const char *line,
                               size_t line_length)
{
    char normalized[ESP32_MQUICKJS_LOG_TEXT_MAX_BYTES + 1U];
    size_t normalized_length = normalize_log_line(line, line_length, normalized);

    if (normalized_length == 0U) {
        return;
    }
    (void)esp32_mquickjs_log_ring_append(&state->ring,
                                         (uint32_t)(esp_timer_get_time() / 1000),
                                         source,
                                         normalized,
                                         normalized_length);
}

static void append_text_locked(esp32_mquickjs_runtime_logs_state_t *state,
                               esp32_mquickjs_log_source_t source,
                               const char *text,
                               size_t text_length)
{
    size_t line_start = 0U;
    size_t i;

    for (i = 0U; i <= text_length; ++i) {
        if (i != text_length && text[i] != '\n') {
            continue;
        }
        if (i > line_start && text[i - 1U] == '\r') {
            append_line_locked(state, source, text + line_start,
                               i - line_start - 1U);
        } else {
            append_line_locked(state, source, text + line_start,
                               i - line_start);
        }
        line_start = i + 1U;
    }
}

static int runtime_logs_vprintf(const char *format, va_list arguments)
{
    char buffer[ESP32_MQUICKJS_LOG_RAW_LINE_MAX_BYTES];
    va_list copy;
    int formatted_length;
    size_t captured_length;
    esp32_mquickjs_runtime_logs_state_t *state = &s_runtime_logs;

    va_copy(copy, arguments);
    formatted_length = vsnprintf(buffer, sizeof(buffer), format, copy);
    va_end(copy);
    if (formatted_length <= 0 || !state->active) {
        return formatted_length;
    }
    captured_length = (size_t)formatted_length;
    if (captured_length >= sizeof(buffer)) {
        captured_length = sizeof(buffer) - 1U;
    }
    portENTER_CRITICAL(&state->lock);
    append_text_locked(state,
                       ESP32_MQUICKJS_LOG_SOURCE_ESP_IDF,
                       buffer,
                       captured_length);
    portEXIT_CRITICAL(&state->lock);
    return formatted_length;
}

bool esp32_mquickjs_init_runtime_logs(esp32_mquickjs_runtime_t *runtime)
{
    uint8_t boot_id_bytes[8];
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (runtime == NULL || s_runtime_logs_owner != NULL) {
        return false;
    }
    memset(&s_runtime_logs, 0, sizeof(s_runtime_logs));
    s_runtime_logs.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    esp32_mquickjs_log_ring_init(&s_runtime_logs.ring);
    esp_fill_random(boot_id_bytes, sizeof(boot_id_bytes));
    for (i = 0U; i < sizeof(boot_id_bytes); ++i) {
        s_runtime_logs.boot_id[i * 2U] = hex[boot_id_bytes[i] >> 4U];
        s_runtime_logs.boot_id[i * 2U + 1U] = hex[boot_id_bytes[i] & 0x0fU];
    }
    s_runtime_logs.boot_id[16] = '\0';
    s_runtime_logs.active = true;
    s_runtime_logs_owner = runtime;
    runtime->runtime_log_state = &s_runtime_logs;
    s_previous_vprintf = esp_log_set_vprintf(runtime_logs_vprintf);
    return true;
}

void esp32_mquickjs_deinit_runtime_logs(esp32_mquickjs_runtime_t *runtime)
{
    if (runtime == NULL || s_runtime_logs_owner != runtime) {
        return;
    }
    (void)esp_log_set_vprintf(s_previous_vprintf);
    portENTER_CRITICAL(&s_runtime_logs.lock);
    s_runtime_logs.active = false;
    s_runtime_logs.console_open = false;
    portEXIT_CRITICAL(&s_runtime_logs.lock);
    runtime->runtime_log_state = NULL;
    s_runtime_logs_owner = NULL;
    s_previous_vprintf = NULL;
}

void esp32_mquickjs_runtime_logs_console_begin(esp32_mquickjs_runtime_t *runtime,
                                               esp32_mquickjs_log_source_t source)
{
    esp32_mquickjs_runtime_logs_state_t *state;

    if (runtime == NULL || runtime->runtime_log_state == NULL) {
        return;
    }
    state = runtime->runtime_log_state;
    portENTER_CRITICAL(&state->lock);
    state->console_open = true;
    state->console_line_length = 0U;
    state->console_source = source;
    portEXIT_CRITICAL(&state->lock);
}

void esp32_mquickjs_runtime_logs_console_write(esp32_mquickjs_runtime_t *runtime,
                                               const void *data,
                                               size_t data_length)
{
    esp32_mquickjs_runtime_logs_state_t *state;
    const char *bytes = data;
    size_t i;

    if (runtime == NULL || runtime->runtime_log_state == NULL ||
        data == NULL || data_length == 0U) {
        return;
    }
    state = runtime->runtime_log_state;
    portENTER_CRITICAL(&state->lock);
    if (!state->console_open) {
        state->console_open = true;
        state->console_source = ESP32_MQUICKJS_LOG_SOURCE_RUNTIME;
    }
    for (i = 0U; i < data_length; ++i) {
        if (bytes[i] == '\n') {
            append_line_locked(state,
                               state->console_source,
                               state->console_line,
                               state->console_line_length);
            state->console_line_length = 0U;
            continue;
        }
        if (state->console_line_length < sizeof(state->console_line)) {
            state->console_line[state->console_line_length++] = bytes[i];
        }
    }
    portEXIT_CRITICAL(&state->lock);
}

void esp32_mquickjs_runtime_logs_console_end(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_runtime_logs_state_t *state;

    if (runtime == NULL || runtime->runtime_log_state == NULL) {
        return;
    }
    state = runtime->runtime_log_state;
    portENTER_CRITICAL(&state->lock);
    if (state->console_open && state->console_line_length > 0U) {
        append_line_locked(state,
                           state->console_source,
                           state->console_line,
                           state->console_line_length);
    }
    state->console_open = false;
    state->console_line_length = 0U;
    portEXIT_CRITICAL(&state->lock);
}

static bool read_number_argument(JSContext *ctx,
                                 JSValue value,
                                 double minimum,
                                 double maximum,
                                 double *output)
{
    double converted;

    if (JS_ToNumber(ctx, &converted, value) != 0 || !isfinite(converted) ||
        floor(converted) != converted || converted < minimum || converted > maximum) {
        return false;
    }
    *output = converted;
    return true;
}

JSValue js_runtime_logs_read(JSContext *ctx,
                             JSValue *this_val,
                             int argc,
                             JSValue *argv)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    esp32_mquickjs_runtime_logs_state_t *state;
    esp32_mquickjs_log_entry_t *snapshot = NULL;
    size_t entry_count;
    uint32_t dropped;
    double after_value;
    double limit_value;
    double max_bytes_value;
    JSGCRef result_ref;
    JSGCRef entries_ref;
    JSValue *result;
    JSValue *entries;
    size_t i;

    (void)this_val;
    if (runtime == NULL || runtime->runtime_log_state == NULL) {
        return JS_ThrowInternalError(ctx, "runtimeLogs is not active");
    }
    if (argc != 3 ||
        !read_number_argument(ctx, argv[0], 0.0, 4294967295.0, &after_value) ||
        !read_number_argument(ctx, argv[1], 1.0,
                              (double)ESP32_MQUICKJS_LOG_READ_MAX_ENTRIES,
                              &limit_value) ||
        !read_number_argument(ctx, argv[2], 1.0,
                              (double)ESP32_MQUICKJS_LOG_READ_MAX_BYTES,
                              &max_bytes_value)) {
        return JS_ThrowTypeError(ctx,
            "runtimeLogs.read(afterSequence, limit, maxBytes) expects 0..4294967295, 1..16, 1..2048");
    }
    snapshot = heap_caps_calloc((size_t)limit_value,
                                sizeof(*snapshot),
                                MALLOC_CAP_8BIT);
    if (snapshot == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    state = runtime->runtime_log_state;
    portENTER_CRITICAL(&state->lock);
    entry_count = esp32_mquickjs_log_ring_read(&state->ring,
                                               (uint32_t)after_value,
                                               (size_t)limit_value,
                                               (size_t)max_bytes_value,
                                               snapshot,
                                               (size_t)limit_value);
    dropped = state->ring.dropped;
    portEXIT_CRITICAL(&state->lock);

    result = JS_PushGCRef(ctx, &result_ref);
    entries = JS_PushGCRef(ctx, &entries_ref);
    *result = JS_NewObject(ctx);
    *entries = JS_NewArray(ctx, 0);
    if (JS_IsException(*result) || JS_IsException(*entries)) {
        goto fail;
    }
    for (i = 0U; i < entry_count; ++i) {
        JSGCRef entry_ref;
        JSValue *entry = JS_PushGCRef(ctx, &entry_ref);

        *entry = JS_NewObject(ctx);
        if (JS_IsException(*entry) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "sequence",
                                             JS_NewUint32(ctx, snapshot[i].sequence)) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "uptimeMs",
                                             JS_NewUint32(ctx, snapshot[i].uptime_ms)) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "source",
                                             JS_NewString(ctx, runtime_log_source_name(snapshot[i].source))) ||
            !esp32_mquickjs_set_property_ref(ctx, entry, "text",
                                             JS_NewStringLen(ctx,
                                                             snapshot[i].text,
                                                             snapshot[i].text_length)) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *entries, (uint32_t)i, *entry))) {
            JS_PopGCRef(ctx, &entry_ref);
            goto fail;
        }
        JS_PopGCRef(ctx, &entry_ref);
    }
    if (!esp32_mquickjs_set_property_ref(ctx, result, "bootId",
                                         JS_NewString(ctx, state->boot_id)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "entries", *entries) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "dropped",
                                         JS_NewUint32(ctx, dropped))) {
        goto fail;
    }
    heap_caps_free(snapshot);
    JS_PopGCRef(ctx, &entries_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
    heap_caps_free(snapshot);
    JS_PopGCRef(ctx, &entries_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}
