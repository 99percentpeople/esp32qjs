#include "esp32_mquickjs_runtime_logs.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_log_write.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define ESP32_MQUICKJS_LOG_CAPTURE_MAX_BYTES 1024U
#define ESP32_MQUICKJS_LOG_READ_MAX_ENTRIES 16U
#define ESP32_MQUICKJS_LOG_READ_MAX_BYTES 2048U

typedef struct {
    esp32_mquickjs_log_ring_t ring;
    portMUX_TYPE lock;
    char boot_id[17];
    char console_chunk[ESP32_MQUICKJS_LOG_TEXT_MAX_BYTES];
    size_t console_chunk_length;
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

static size_t raw_chunk_length(const char *input, size_t input_length)
{
    const unsigned char *bytes = (const unsigned char *)input;
    size_t limit = input_length > ESP32_MQUICKJS_LOG_TEXT_MAX_BYTES
                       ? ESP32_MQUICKJS_LOG_TEXT_MAX_BYTES
                       : input_length;
    size_t index = 0U;

    while (index < limit) {
        size_t sequence_length;

        sequence_length = utf8_sequence_length(bytes + index,
                                               input_length - index);
        if (sequence_length == 0U) {
            sequence_length = 1U;
        }
        if (index + sequence_length > limit) {
            break;
        }
        index += sequence_length;
    }
    return index == 0U ? limit : index;
}

static void append_raw_locked(esp32_mquickjs_runtime_logs_state_t *state,
                              esp32_mquickjs_log_source_t source,
                              const char *text,
                              size_t text_length)
{
    size_t offset = 0U;

    while (offset < text_length) {
        size_t chunk_length = raw_chunk_length(text + offset,
                                               text_length - offset);

        if (chunk_length == 0U) {
            break;
        }
        (void)esp32_mquickjs_log_ring_append(
            &state->ring,
            (uint32_t)(esp_timer_get_time() / 1000),
            source,
            text + offset,
            chunk_length);
        offset += chunk_length;
    }
}

static void flush_console_chunk_locked(esp32_mquickjs_runtime_logs_state_t *state)
{
    if (state->console_chunk_length == 0U) {
        return;
    }
    append_raw_locked(state,
                      state->console_source,
                      state->console_chunk,
                      state->console_chunk_length);
    state->console_chunk_length = 0U;
}

static void buffer_console_output_locked(esp32_mquickjs_runtime_logs_state_t *state,
                                         const char *text,
                                         size_t text_length)
{
    const unsigned char *bytes = (const unsigned char *)text;
    size_t offset = 0U;

    while (offset < text_length) {
        size_t sequence_length = utf8_sequence_length(bytes + offset,
                                                      text_length - offset);

        if (sequence_length == 0U) {
            sequence_length = 1U;
        }
        if (state->console_chunk_length + sequence_length >
            sizeof(state->console_chunk)) {
            flush_console_chunk_locked(state);
        }
        memcpy(state->console_chunk + state->console_chunk_length,
               text + offset,
               sequence_length);
        state->console_chunk_length += sequence_length;
        offset += sequence_length;
        if (state->console_chunk_length == sizeof(state->console_chunk)) {
            flush_console_chunk_locked(state);
        }
    }
}

static int runtime_logs_vprintf(const char *format, va_list arguments)
{
    char buffer[ESP32_MQUICKJS_LOG_CAPTURE_MAX_BYTES];
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
    append_raw_locked(state,
                      ESP32_MQUICKJS_LOG_SOURCE_ESP_IDF,
                      buffer,
                      captured_length);
    portEXIT_CRITICAL(&state->lock);
    return formatted_length;
}

bool esp32_mquickjs_init_runtime_logs(esp32_mquickjs_runtime_t *runtime)
{
    if (runtime == NULL || s_runtime_logs_owner != NULL) {
        return false;
    }
    memset(&s_runtime_logs, 0, sizeof(s_runtime_logs));
    s_runtime_logs.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    esp32_mquickjs_log_ring_init(&s_runtime_logs.ring);
    snprintf(s_runtime_logs.boot_id,
             sizeof(s_runtime_logs.boot_id),
             "%s",
             runtime->boot_id);
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
    if (state->console_open) {
        flush_console_chunk_locked(state);
    }
    state->console_open = true;
    state->console_chunk_length = 0U;
    state->console_source = source;
    portEXIT_CRITICAL(&state->lock);
}

void esp32_mquickjs_runtime_logs_console_write(esp32_mquickjs_runtime_t *runtime,
                                               const void *data,
                                               size_t data_length)
{
    esp32_mquickjs_runtime_logs_state_t *state;

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
    buffer_console_output_locked(state, data, data_length);
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
    if (state->console_open) {
        buffer_console_output_locked(state, "\n", 1U);
        flush_console_chunk_locked(state);
    }
    state->console_open = false;
    state->console_chunk_length = 0U;
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
