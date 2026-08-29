#include "esp32_mquickjs_rmt.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_RMT

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_rmt_channel_resources.h"
#include "esp32_mquickjs_rmt_symbol_buffer_resources.h"

#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_common.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "soc/soc_caps.h"

#define RMT_MAX_CHANNELS 8U
#define RMT_MAX_SYMBOLS 4096U
#define RMT_MAX_DURATION_TICKS 32767U
#define RMT_DEFAULT_TIMEOUT_MS 1000U

typedef enum {
    ESP32_MQUICKJS_RMT_RX,
    ESP32_MQUICKJS_RMT_TX,
} esp32_mquickjs_rmt_direction_t;

typedef enum {
    ESP32_MQUICKJS_RMT_OP_RECEIVE,
    ESP32_MQUICKJS_RMT_OP_TRANSMIT,
} esp32_mquickjs_rmt_operation_t;

typedef struct {
    rmt_symbol_word_t *symbols;
    size_t capacity;
    size_t length;
    uint16_t leases;
    bool close_pending;
} esp32_mquickjs_rmt_symbol_buffer_t;

typedef struct {
    uint8_t slot;
    uint32_t generation;
} esp32_mquickjs_rmt_channel_ref_t;

typedef struct esp32_mquickjs_future_driver_state
    esp32_mquickjs_rmt_future_state_t;

typedef struct {
    bool allocated;
    bool running;
    bool busy;
    bool release_pending;
    uint32_t future_reservations;
    uint8_t index;
    uint32_t generation;
    esp32_mquickjs_rmt_direction_t direction;
    int pin;
    uint32_t resolution_hz;
    uint32_t memory_symbols;
    bool dma;
    bool invert;
    rmt_channel_handle_t handle;
    rmt_encoder_handle_t encoder;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    esp32_mquickjs_rmt_future_state_t *active;
} esp32_mquickjs_rmt_channel_slot_t;

struct esp32_mquickjs_future_driver_state {
    JSContext *ctx;
    JSGCRef channel_owner_ref;
    JSGCRef symbols_owner_ref;
    esp32_mquickjs_rmt_channel_ref_t channel_ref;
    esp32_mquickjs_rmt_symbol_buffer_t *buffer;
    esp32_mquickjs_rmt_operation_t operation;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    uint32_t timeout_ms;
    uint32_t min_pulse_ns;
    uint32_t idle_threshold_ns;
    uint64_t timestamp_us;
    int loop_count;
    bool end_level;
    size_t received_symbols;
    _Atomic bool completed;
    bool buffer_leased;
    bool channel_reserved;
    bool started;
    bool cancelled;
    bool timed_out;
    bool truncated;
    esp_err_t err;
};

static esp32_mquickjs_rmt_channel_slot_t s_rmt_channels[RMT_MAX_CHANNELS];
static uint32_t s_rmt_next_generation = 1;
static portMUX_TYPE s_rmt_callback_lock = portMUX_INITIALIZER_UNLOCKED;

static void rmt_request_close(esp32_mquickjs_rmt_channel_slot_t *slot);
static bool rmt_on_transmit_done(
    rmt_channel_handle_t channel, const rmt_tx_done_event_data_t *event,
    void *user_ctx);
static bool rmt_on_receive_done(
    rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event,
    void *user_ctx);

static void *rmt_symbol_buffer_allocate(size_t size, void *opaque)
{
    (void)opaque;
    return heap_caps_calloc(1, size, MALLOC_CAP_8BIT);
}

static void *rmt_symbol_buffer_allocate_symbols(size_t size, void *opaque)
{
    (void)opaque;
    return heap_caps_calloc(
        1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
}

static void rmt_symbol_buffer_free(void *value, void *opaque)
{
    (void)opaque;
    heap_caps_free(value);
}

static const esp32_mquickjs_rmt_symbol_buffer_resource_ops_t
    s_rmt_symbol_buffer_resource_ops = {
        .allocate_buffer = rmt_symbol_buffer_allocate,
        .allocate_symbols = rmt_symbol_buffer_allocate_symbols,
        .release = rmt_symbol_buffer_free,
    };

typedef struct {
    esp32_mquickjs_rmt_channel_slot_t *slot;
    esp32_mquickjs_rmt_direction_t direction;
    rmt_tx_channel_config_t tx_config;
    rmt_rx_channel_config_t rx_config;
} rmt_channel_resource_context_t;

static int rmt_channel_resource_create_channel(void *opaque,
                                               void **out_channel)
{
    rmt_channel_resource_context_t *context = opaque;
    rmt_channel_handle_t channel = NULL;
    esp_err_t err;

    if (context == NULL || context->slot == NULL || out_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (context->direction == ESP32_MQUICKJS_RMT_TX) {
        err = rmt_new_tx_channel(&context->tx_config, &channel);
    } else {
        err = rmt_new_rx_channel(&context->rx_config, &channel);
    }
    *out_channel = channel;
    return err;
}

static int rmt_channel_resource_create_encoder(void *opaque,
                                               void **out_encoder)
{
    rmt_copy_encoder_config_t config = {};
    rmt_encoder_handle_t encoder = NULL;
    esp_err_t err;

    if (opaque == NULL || out_encoder == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = rmt_new_copy_encoder(&config, &encoder);
    *out_encoder = encoder;
    return err;
}

static int rmt_channel_resource_register_callbacks(void *channel,
                                                   void *opaque)
{
    rmt_channel_resource_context_t *context = opaque;

    if (channel == NULL || context == NULL || context->slot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (context->direction == ESP32_MQUICKJS_RMT_TX) {
        rmt_tx_event_callbacks_t callbacks = {
            .on_trans_done = rmt_on_transmit_done,
        };

        return rmt_tx_register_event_callbacks(
            (rmt_channel_handle_t)channel, &callbacks, context->slot);
    }
    {
        rmt_rx_event_callbacks_t callbacks = {
            .on_recv_done = rmt_on_receive_done,
        };

        return rmt_rx_register_event_callbacks(
            (rmt_channel_handle_t)channel, &callbacks, context->slot);
    }
}

static int rmt_channel_resource_disable(void *channel, void *opaque)
{
    (void)opaque;
    return rmt_disable((rmt_channel_handle_t)channel);
}

static int rmt_channel_resource_delete_encoder(void *encoder, void *opaque)
{
    (void)opaque;
    return rmt_del_encoder((rmt_encoder_handle_t)encoder);
}

static int rmt_channel_resource_delete_channel(void *channel, void *opaque)
{
    (void)opaque;
    return rmt_del_channel((rmt_channel_handle_t)channel);
}

static esp32_mquickjs_rmt_channel_resource_ops_t rmt_channel_resource_ops(
    rmt_channel_resource_context_t *context)
{
    return (esp32_mquickjs_rmt_channel_resource_ops_t){
        .create_channel = rmt_channel_resource_create_channel,
        .create_encoder = rmt_channel_resource_create_encoder,
        .register_callbacks = rmt_channel_resource_register_callbacks,
        .disable_channel = rmt_channel_resource_disable,
        .delete_encoder = rmt_channel_resource_delete_encoder,
        .delete_channel = rmt_channel_resource_delete_channel,
        .opaque = context,
    };
}

static uint32_t rmt_take_generation(void)
{
    uint32_t generation = s_rmt_next_generation++;

    if (generation == 0) {
        generation = s_rmt_next_generation++;
    }
    return generation;
}

static bool rmt_to_u32(JSContext *ctx, JSValue value, uint32_t *out)
{
    double number;
    uint32_t converted;

    if (!JS_IsNumber(ctx, value) ||
        JS_ToNumber(ctx, &number, value) != 0 || !isfinite(number) ||
        number < 0 || number > UINT32_MAX) {
        return false;
    }
    converted = (uint32_t)number;
    if ((double)converted != number) {
        return false;
    }
    *out = converted;
    return true;
}

static bool rmt_to_bool(JSContext *ctx, JSValue value, bool *out)
{
    (void)ctx;
    if (!JS_IsBool(value)) {
        return false;
    }
    *out = value == JS_TRUE;
    return true;
}

static bool rmt_to_level(JSContext *ctx, JSValue value, bool *out)
{
    uint32_t level;

    if (JS_IsBool(value)) {
        *out = value == JS_TRUE;
        return true;
    }
    if (!rmt_to_u32(ctx, value, &level) || level > 1) {
        return false;
    }
    *out = level != 0;
    return true;
}

static bool rmt_string_equals(JSContext *ctx, JSValue value,
                              const char *expected)
{
    JSCStringBuf buffer;
    const char *text;

    if (!JS_IsString(ctx, value)) {
        return false;
    }
    text = JS_ToCString(ctx, value, &buffer);
    return text != NULL && strcmp(text, expected) == 0;
}

static bool rmt_is_object(JSContext *ctx, JSValue value)
{
    return JS_GetClassID(ctx, value) >= 0 && !JS_IsArray(ctx, value);
}

static esp32_mquickjs_rmt_symbol_buffer_t *rmt_symbol_buffer_from_value(
    JSContext *ctx, JSValue value, const char *api_name)
{
    esp32_mquickjs_rmt_symbol_buffer_t *buffer;

    if (JS_GetClassID(ctx, value) != JS_CLASS_RMT_SYMBOL_BUFFER) {
        JS_ThrowTypeError(ctx, "%s expects an RMTSymbolBuffer", api_name);
        return NULL;
    }
    buffer = JS_GetOpaque(ctx, value);
    if (buffer == NULL || buffer->symbols == NULL) {
        JS_ThrowReferenceError(
            ctx, "%s failed because the RMTSymbolBuffer is closed", api_name);
        return NULL;
    }
    return buffer;
}

static void rmt_symbol_buffer_release(
    esp32_mquickjs_rmt_symbol_buffer_t *buffer)
{
    esp32_mquickjs_rmt_symbol_buffer_resources_t resources;

    if (buffer == NULL || buffer->symbols == NULL || buffer->leases != 0) {
        return;
    }
    resources = (esp32_mquickjs_rmt_symbol_buffer_resources_t){
        .buffer = buffer,
        .symbols = buffer->symbols,
    };
    buffer->symbols = NULL;
    esp32_mquickjs_rmt_symbol_buffer_resources_deinit(
        &resources, &s_rmt_symbol_buffer_resource_ops);
}

static esp32_mquickjs_rmt_channel_slot_t *rmt_channel_get_slot(
    const esp32_mquickjs_rmt_channel_ref_t *ref)
{
    esp32_mquickjs_rmt_channel_slot_t *slot;

    if (ref == NULL || ref->slot >= RMT_MAX_CHANNELS) {
        return NULL;
    }
    slot = &s_rmt_channels[ref->slot];
    if (!slot->allocated || slot->generation != ref->generation) {
        return NULL;
    }
    return slot;
}

static int rmt_channel_from_value(
    JSContext *ctx, JSValue value, const char *api_name,
    esp32_mquickjs_rmt_channel_ref_t *out_ref,
    esp32_mquickjs_rmt_channel_slot_t **out_slot)
{
    esp32_mquickjs_rmt_channel_ref_t *ref;
    esp32_mquickjs_rmt_channel_slot_t *slot;

    if (JS_GetClassID(ctx, value) != JS_CLASS_RMT_CHANNEL ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "%s expects an RMTChannel", api_name);
        return -1;
    }
    slot = rmt_channel_get_slot(ref);
    if (slot == NULL) {
        JS_ThrowReferenceError(ctx,
                               "%s failed because the RMT channel is closed",
                               api_name);
        return -1;
    }
    if (out_ref != NULL) {
        *out_ref = *ref;
    }
    if (out_slot != NULL) {
        *out_slot = slot;
    }
    return 0;
}

static esp_err_t rmt_channel_cleanup(esp32_mquickjs_rmt_channel_slot_t *slot)
{
    rmt_channel_resource_context_t context;
    esp32_mquickjs_rmt_channel_resources_t resources;
    esp32_mquickjs_rmt_channel_resource_ops_t ops;
    uint8_t index;
    esp_err_t err;

    if (slot == NULL || !slot->allocated) {
        return ESP_OK;
    }
    if (slot->busy || slot->future_reservations > 0) {
        return ESP_ERR_INVALID_STATE;
    }
    index = slot->index;
    context = (rmt_channel_resource_context_t){
        .slot = slot,
        .direction = slot->direction,
    };
    resources = (esp32_mquickjs_rmt_channel_resources_t){
        .channel = slot->handle,
        .encoder = slot->encoder,
        .enabled = slot->running,
    };
    ops = rmt_channel_resource_ops(&context);
    err = esp32_mquickjs_rmt_channel_resources_deinit(&resources, &ops);
    slot->handle = (rmt_channel_handle_t)resources.channel;
    slot->encoder = (rmt_encoder_handle_t)resources.encoder;
    slot->running = resources.enabled;
    if (err != ESP_OK) {
        slot->release_pending = true;
        return err;
    }
    memset(slot, 0, sizeof(*slot));
    slot->index = index;
    return ESP_OK;
}

static esp_err_t rmt_abort_active(esp32_mquickjs_rmt_channel_slot_t *slot)
{
    esp_err_t err;

    if (slot == NULL || !slot->running || slot->handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    err = rmt_disable(slot->handle);
    if (err != ESP_OK) {
        return err;
    }
    slot->running = false;
    if (!slot->release_pending) {
        err = rmt_enable(slot->handle);
        if (err == ESP_OK) {
            slot->running = true;
        }
    }
    return ESP_OK;
}

static esp32_mquickjs_rmt_future_state_t *rmt_active_operation(
    esp32_mquickjs_rmt_channel_slot_t *slot)
{
    esp32_mquickjs_rmt_future_state_t *state = NULL;

    portENTER_CRITICAL(&s_rmt_callback_lock);
    if (slot != NULL) {
        state = slot->active;
    }
    portEXIT_CRITICAL(&s_rmt_callback_lock);
    return state;
}

static bool rmt_complete_cancelled(
    esp32_mquickjs_rmt_channel_slot_t *slot,
    esp32_mquickjs_rmt_future_state_t *state,
    bool timed_out)
{
    esp32_mquickjs_runtime_t *runtime = NULL;
    esp32_mquickjs_future_token_t token = {0};
    bool completed = false;

    portENTER_CRITICAL(&s_rmt_callback_lock);
    if (slot != NULL && state != NULL && slot->active == state &&
        !atomic_load_explicit(&state->completed, memory_order_relaxed)) {
        state->cancelled = !timed_out;
        state->timed_out = timed_out;
        atomic_store_explicit(&state->completed, true, memory_order_release);
        runtime = slot->runtime;
        token = slot->token;
        completed = true;
    }
    portEXIT_CRITICAL(&s_rmt_callback_lock);
    if (runtime != NULL) {
        (void)esp32_mquickjs_future_wake(runtime, token);
    }
    return completed;
}

static void rmt_request_close(esp32_mquickjs_rmt_channel_slot_t *slot)
{
    esp32_mquickjs_rmt_future_state_t *state;

    if (slot == NULL || !slot->allocated) {
        return;
    }
    slot->release_pending = true;
    state = rmt_active_operation(slot);
    if (state != NULL && rmt_abort_active(slot) == ESP_OK) {
        (void)rmt_complete_cancelled(slot, state, false);
    }
    if (!slot->busy && slot->future_reservations == 0) {
        rmt_channel_cleanup(slot);
    }
}

static JSValue rmt_make_channel(JSContext *ctx,
                                const esp32_mquickjs_rmt_channel_slot_t *slot)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    esp32_mquickjs_rmt_channel_ref_t *ref;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_RMT_CHANNEL);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    ref = heap_caps_malloc(sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    ref->slot = slot->index;
    ref->generation = slot->generation;
    JS_SetOpaque(ctx, *object, ref);
    return JS_PopGCRef(ctx, &object_ref);
}

static JSValue rmt_symbol_object(JSContext *ctx,
                                 const rmt_symbol_word_t *symbol)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);

    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "duration0Ticks",
            JS_NewUint32(ctx, symbol->duration0)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "level0",
                                         JS_NewBool(symbol->level0)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "duration1Ticks",
            JS_NewUint32(ctx, symbol->duration1)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "level1",
                                         JS_NewBool(symbol->level1))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static bool rmt_parse_symbol(JSContext *ctx, int argc, JSValue *argv,
                             rmt_symbol_word_t *out)
{
    uint32_t duration0;
    uint32_t duration1;
    bool level0;
    bool level1;

    if (argc != 4 || !rmt_to_u32(ctx, argv[0], &duration0) ||
        !rmt_to_level(ctx, argv[1], &level0) ||
        !rmt_to_u32(ctx, argv[2], &duration1) ||
        !rmt_to_level(ctx, argv[3], &level1)) {
        JS_ThrowTypeError(
            ctx,
            "RMT symbol expects duration0Ticks, level0, duration1Ticks, level1");
        return false;
    }
    if (duration0 > RMT_MAX_DURATION_TICKS ||
        duration1 > RMT_MAX_DURATION_TICKS) {
        JS_ThrowRangeError(ctx,
                           "RMT symbol durations must fit the 15-bit tick field");
        return false;
    }
    *out = (rmt_symbol_word_t){
        .duration0 = duration0,
        .level0 = level0,
        .duration1 = duration1,
        .level1 = level1,
    };
    return true;
}

static bool IRAM_ATTR rmt_on_transmit_done(
    rmt_channel_handle_t channel, const rmt_tx_done_event_data_t *event,
    void *user_ctx)
{
    esp32_mquickjs_rmt_channel_slot_t *slot = user_ctx;
    esp32_mquickjs_rmt_future_state_t *state = NULL;
    esp32_mquickjs_runtime_t *runtime = NULL;
    esp32_mquickjs_future_token_t token = {0};
    int task_woken = pdFALSE;

    (void)channel;
    (void)event;
    portENTER_CRITICAL_ISR(&s_rmt_callback_lock);
    if (slot != NULL && (state = slot->active) != NULL &&
        !atomic_load_explicit(&state->completed, memory_order_relaxed)) {
        state->err = ESP_OK;
        atomic_store_explicit(&state->completed, true, memory_order_release);
        runtime = slot->runtime;
        token = slot->token;
    }
    portEXIT_CRITICAL_ISR(&s_rmt_callback_lock);
    if (runtime != NULL) {
        (void)esp32_mquickjs_future_wake_from_isr(
            runtime, token, &task_woken);
    }
    return task_woken == pdTRUE;
}

static bool IRAM_ATTR rmt_on_receive_done(
    rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event,
    void *user_ctx)
{
    esp32_mquickjs_rmt_channel_slot_t *slot = user_ctx;
    esp32_mquickjs_rmt_future_state_t *state = NULL;
    esp32_mquickjs_runtime_t *runtime = NULL;
    esp32_mquickjs_future_token_t token = {0};
    uint64_t timestamp_us = (uint64_t)esp_timer_get_time();
    int task_woken = pdFALSE;

    (void)channel;
    portENTER_CRITICAL_ISR(&s_rmt_callback_lock);
    if (slot != NULL && event != NULL &&
        (state = slot->active) != NULL &&
        !atomic_load_explicit(&state->completed, memory_order_relaxed)) {
        state->received_symbols = event->num_symbols;
        state->truncated = event->num_symbols >= state->buffer->capacity;
        state->timestamp_us = timestamp_us;
        state->err = ESP_OK;
        atomic_store_explicit(&state->completed, true, memory_order_release);
        runtime = slot->runtime;
        token = slot->token;
    }
    portEXIT_CRITICAL_ISR(&s_rmt_callback_lock);
    if (runtime != NULL) {
        (void)esp32_mquickjs_future_wake_from_isr(
            runtime, token, &task_woken);
    }
    return task_woken == pdTRUE;
}

static bool rmt_parse_timeout_option(JSContext *ctx, JSValue options,
                                     uint32_t *timeout_ms)
{
    JSValue property = JS_GetPropertyStr(ctx, options, "timeoutMs");

    if (JS_IsException(property)) {
        return false;
    }
    if (!JS_IsUndefined(property) &&
        !esp32_mquickjs_value_to_bounded_u32(
            ctx, property, 0, UINT32_MAX, timeout_ms)) {
        JS_ThrowTypeError(ctx, "RMT timeoutMs must be a non-negative integer");
        return false;
    }
    return true;
}

static bool rmt_prepare_common(
    JSContext *ctx, JSGCRef *this_ref, JSValue symbols_value,
    esp32_mquickjs_rmt_operation_t operation,
    esp32_mquickjs_rmt_future_state_t **out_state)
{
    esp32_mquickjs_rmt_channel_slot_t *slot;
    esp32_mquickjs_rmt_symbol_buffer_t *buffer;
    esp32_mquickjs_rmt_future_state_t *state;
    JSValue *owner;

    if (rmt_channel_from_value(ctx, this_ref->val,
                               operation == ESP32_MQUICKJS_RMT_OP_TRANSMIT
                                   ? "RMTChannel.transmit()"
                                   : "RMTChannel.receive()",
                               NULL, &slot) != 0) {
        return false;
    }
    if ((operation == ESP32_MQUICKJS_RMT_OP_TRANSMIT &&
         slot->direction != ESP32_MQUICKJS_RMT_TX) ||
        (operation == ESP32_MQUICKJS_RMT_OP_RECEIVE &&
         slot->direction != ESP32_MQUICKJS_RMT_RX)) {
        JS_ThrowTypeError(ctx, "RMT operation does not match channel direction");
        return false;
    }
    if (!slot->running) {
        JS_ThrowInternalError(ctx, "RMT operation requires start() first");
        return false;
    }
    buffer = rmt_symbol_buffer_from_value(
        ctx, symbols_value,
        operation == ESP32_MQUICKJS_RMT_OP_TRANSMIT
            ? "RMTChannel.transmit(symbols)"
            : "RMTChannel.receive(symbols)");
    if (buffer == NULL) {
        return false;
    }
    if (operation == ESP32_MQUICKJS_RMT_OP_TRANSMIT && buffer->length == 0) {
        JS_ThrowRangeError(ctx,
                           "RMTChannel.transmit() requires a non-empty symbol buffer");
        return false;
    }
    if (buffer->leases != 0) {
        JS_ThrowInternalError(ctx,
                              "RMTSymbolBuffer already has a pending operation");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_INTERNAL);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->channel_ref.slot = slot->index;
    state->channel_ref.generation = slot->generation;
    state->buffer = buffer;
    state->operation = operation;
    state->timeout_ms = RMT_DEFAULT_TIMEOUT_MS;
    state->err = ESP_OK;
    atomic_init(&state->completed, false);
    slot->future_reservations++;
    state->channel_reserved = true;
    buffer->leases++;
    state->buffer_leased = true;
    owner = JS_AddGCRef(ctx, &state->channel_owner_ref);
    *owner = this_ref->val;
    owner = JS_AddGCRef(ctx, &state->symbols_owner_ref);
    *owner = symbols_value;
    *out_state = state;
    return true;
}

static bool rmt_transmit_prepare(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_rmt_future_state_t *state;
    JSValue options = JS_UNDEFINED;

    if (out_state == NULL || argc < 1 || argc > 2 ||
        (argc == 2 && !JS_IsUndefined(argv[1].val) &&
         !rmt_is_object(ctx, argv[1].val))) {
        JS_ThrowTypeError(
            ctx,
            "RMTChannel.transmit(symbols, options?) expects an RMTSymbolBuffer and options object");
        return false;
    }
    if (!rmt_prepare_common(ctx, this_ref, argv[0].val,
                            ESP32_MQUICKJS_RMT_OP_TRANSMIT, &state)) {
        return false;
    }
    if (argc == 2 && !JS_IsUndefined(argv[1].val)) {
        JSGCRef property_ref;
        JSValue *property = JS_PushGCRef(ctx, &property_ref);
        uint32_t loop_count;

        options = argv[1].val;
        *property = JS_GetPropertyStr(ctx, options, "loopCount");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             (!rmt_to_u32(ctx, *property, &loop_count) ||
              loop_count > INT32_MAX))) {
            JS_ThrowTypeError(ctx,
                              "RMT loopCount must be a finite non-negative integer");
            JS_PopGCRef(ctx, &property_ref);
            goto fail;
        }
        if (!JS_IsUndefined(*property)) {
            state->loop_count = (int)loop_count;
        }
        *property = JS_GetPropertyStr(ctx, options, "endLevel");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             !rmt_to_level(ctx, *property, &state->end_level))) {
            JS_ThrowTypeError(ctx, "RMT endLevel must be boolean, zero, or one");
            JS_PopGCRef(ctx, &property_ref);
            goto fail;
        }
        JS_PopGCRef(ctx, &property_ref);
        if (!rmt_parse_timeout_option(ctx, options, &state->timeout_ms)) {
            goto fail;
        }
    }
    *out_state = state;
    return true;

fail:
    if (state->buffer_leased && state->buffer->leases > 0) {
        state->buffer->leases--;
    }
    {
        esp32_mquickjs_rmt_channel_slot_t *slot =
            rmt_channel_get_slot(&state->channel_ref);

        if (state->channel_reserved && slot != NULL &&
            slot->future_reservations > 0) {
            slot->future_reservations--;
        }
    }
    JS_DeleteGCRef(ctx, &state->symbols_owner_ref);
    JS_DeleteGCRef(ctx, &state->channel_owner_ref);
    heap_caps_free(state);
    return false;
}

static bool rmt_receive_prepare(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_rmt_future_state_t *state;
    JSGCRef property_ref;
    JSValue *property;

    if (out_state == NULL || argc != 2 ||
        !rmt_is_object(ctx, argv[1].val)) {
        JS_ThrowTypeError(
            ctx,
            "RMTChannel.receive(symbols, options) expects an RMTSymbolBuffer and options object");
        return false;
    }
    if (!rmt_prepare_common(ctx, this_ref, argv[0].val,
                            ESP32_MQUICKJS_RMT_OP_RECEIVE, &state)) {
        return false;
    }
    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, argv[1].val, "minPulseNs");
    if (JS_IsException(*property) ||
        (!JS_IsUndefined(*property) &&
         !rmt_to_u32(ctx, *property, &state->min_pulse_ns))) {
        JS_ThrowTypeError(ctx, "RMT minPulseNs must be a non-negative integer");
        goto fail;
    }
    *property = JS_GetPropertyStr(ctx, argv[1].val, "idleThresholdNs");
    if (JS_IsException(*property) ||
        !rmt_to_u32(ctx, *property, &state->idle_threshold_ns) ||
        state->idle_threshold_ns == 0) {
        JS_ThrowTypeError(ctx,
                          "RMT idleThresholdNs must be a positive integer");
        goto fail;
    }
    JS_PopGCRef(ctx, &property_ref);
    if (!rmt_parse_timeout_option(ctx, argv[1].val, &state->timeout_ms)) {
        goto release;
    }
    state->buffer->length = 0;
    *out_state = state;
    return true;

fail:
    JS_PopGCRef(ctx, &property_ref);
release:
    if (state->buffer_leased && state->buffer->leases > 0) {
        state->buffer->leases--;
    }
    {
        esp32_mquickjs_rmt_channel_slot_t *slot =
            rmt_channel_get_slot(&state->channel_ref);

        if (state->channel_reserved && slot != NULL &&
            slot->future_reservations > 0) {
            slot->future_reservations--;
        }
    }
    JS_DeleteGCRef(ctx, &state->symbols_owner_ref);
    JS_DeleteGCRef(ctx, &state->channel_owner_ref);
    heap_caps_free(state);
    return false;
}

static bool rmt_operation_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *driver_state)
{
    esp32_mquickjs_rmt_future_state_t *state = driver_state;
    esp32_mquickjs_rmt_channel_slot_t *slot =
        state != NULL ? rmt_channel_get_slot(&state->channel_ref) : NULL;

    if (state == NULL || slot == NULL) {
        JS_ThrowReferenceError(ctx,
                               "RMT channel closed before operation started");
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    if (slot->release_pending) {
        state->cancelled = true;
        atomic_store_explicit(&state->completed, true, memory_order_release);
        (void)esp32_mquickjs_future_wake(runtime, token);
        return true;
    }
    if (!slot->running) {
        JS_ThrowReferenceError(ctx,
                               "RMT channel stopped before operation started");
        return false;
    }
    if (slot->busy) {
        JS_ThrowInternalError(ctx,
                              "RMT channel already has a pending operation");
        return false;
    }
    slot->busy = true;
    portENTER_CRITICAL(&s_rmt_callback_lock);
    slot->runtime = runtime;
    slot->token = token;
    slot->active = state;
    portEXIT_CRITICAL(&s_rmt_callback_lock);
    state->started = true;
    if (state->operation == ESP32_MQUICKJS_RMT_OP_TRANSMIT) {
        rmt_transmit_config_t config = {
            .loop_count = state->loop_count,
            .flags = {
                .eot_level = state->end_level,
                .queue_nonblocking = true,
            },
        };

        state->err = rmt_transmit(
            slot->handle, slot->encoder, state->buffer->symbols,
            state->buffer->length * sizeof(rmt_symbol_word_t), &config);
    } else {
        rmt_receive_config_t config = {
            .signal_range_min_ns = state->min_pulse_ns,
            .signal_range_max_ns = state->idle_threshold_ns,
        };

        state->err = rmt_receive(
            slot->handle, state->buffer->symbols,
            state->buffer->capacity * sizeof(rmt_symbol_word_t), &config);
    }
    if (state->err != ESP_OK) {
        JS_ThrowInternalError(ctx, "RMT operation failed to start: %s",
                              esp_err_to_name(state->err));
        return false;
    }
    if (state->timeout_ms == 0) {
        if (rmt_abort_active(slot) == ESP_OK) {
            (void)rmt_complete_cancelled(slot, state, true);
        }
    }
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static esp32_mquickjs_future_poll_t rmt_operation_poll(
    esp32_mquickjs_future_driver_state_t *driver_state)
{
    esp32_mquickjs_rmt_future_state_t *state = driver_state;

    return state != NULL && atomic_load_explicit(
                                &state->completed, memory_order_acquire)
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue rmt_operation_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *driver_state)
{
    esp32_mquickjs_rmt_future_state_t *state = driver_state;
    JSGCRef result_ref;
    JSValue *result;

    if (state == NULL || state->cancelled) {
        return JS_ThrowInternalError(ctx, "RMT operation cancelled");
    }
    if (state->timed_out) {
        if (state->operation == ESP32_MQUICKJS_RMT_OP_RECEIVE) {
            return JS_NULL;
        }
        return JS_ThrowInternalError(ctx, "RMT transmission timed out");
    }
    if (state->err != ESP_OK) {
        return JS_ThrowInternalError(ctx, "RMT operation failed: %s",
                                     esp_err_to_name(state->err));
    }
    result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (state->operation == ESP32_MQUICKJS_RMT_OP_RECEIVE) {
        size_t length = state->received_symbols;

        if (length > state->buffer->capacity) {
            length = state->buffer->capacity;
            state->truncated = true;
        }
        state->buffer->length = length;
        if (JS_IsException(*result) ||
            !esp32_mquickjs_set_property_ref(
                ctx, result, "length", JS_NewInt64(ctx, (int64_t)length)) ||
            !esp32_mquickjs_set_property_ref(
                ctx, result, "truncated", JS_NewBool(state->truncated)) ||
            !esp32_mquickjs_set_property_ref(
                ctx, result, "timestampUs",
                JS_NewInt64(ctx, (int64_t)state->timestamp_us))) {
            JS_PopGCRef(ctx, &result_ref);
            return JS_EXCEPTION;
        }
    } else if (JS_IsException(*result) ||
               !esp32_mquickjs_set_property_ref(
                   ctx, result, "symbols",
                   JS_NewInt64(ctx, (int64_t)state->buffer->length)) ||
               !esp32_mquickjs_set_property_ref(
                   ctx, result, "loopCount",
                   JS_NewInt32(ctx, state->loop_count))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static esp32_mquickjs_cancel_result_t rmt_operation_cancel(
    esp32_mquickjs_future_driver_state_t *driver_state)
{
    esp32_mquickjs_rmt_future_state_t *state = driver_state;
    esp32_mquickjs_rmt_channel_slot_t *slot =
        state != NULL ? rmt_channel_get_slot(&state->channel_ref) : NULL;

    if (state == NULL || atomic_load_explicit(
                             &state->completed, memory_order_acquire)) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    if (slot != NULL && state->started) {
        if (rmt_abort_active(slot) != ESP_OK ||
            !rmt_complete_cancelled(slot, state, false)) {
            return ESP32_MQUICKJS_CANCEL_REJECTED;
        }
    } else {
        state->cancelled = true;
        atomic_store_explicit(&state->completed, true, memory_order_release);
        if (state->runtime != NULL) {
            (void)esp32_mquickjs_future_wake(state->runtime, state->token);
        }
    }
    return ESP32_MQUICKJS_CANCELLED;
}

static void rmt_operation_destroy(
    esp32_mquickjs_future_driver_state_t *driver_state)
{
    esp32_mquickjs_rmt_future_state_t *state = driver_state;
    esp32_mquickjs_rmt_channel_slot_t *slot;
    esp32_mquickjs_rmt_symbol_buffer_t *buffer;

    if (state == NULL) {
        return;
    }
    slot = rmt_channel_get_slot(&state->channel_ref);
    buffer = state->buffer;
    if (slot != NULL && state->started) {
        portENTER_CRITICAL(&s_rmt_callback_lock);
        if (slot->active == state) {
            slot->runtime = NULL;
            slot->token = (esp32_mquickjs_future_token_t){0};
            slot->active = NULL;
        }
        portEXIT_CRITICAL(&s_rmt_callback_lock);
        slot->busy = false;
    }
    if (state->buffer_leased && buffer != NULL && buffer->leases > 0) {
        buffer->leases--;
        if (buffer->close_pending && buffer->leases == 0) {
            rmt_symbol_buffer_release(buffer);
        }
    }
    if (slot != NULL && state->channel_reserved) {
        if (slot->future_reservations > 0) {
            slot->future_reservations--;
        }
        if (slot->release_pending && slot->future_reservations == 0 &&
            !slot->busy) {
            rmt_channel_cleanup(slot);
        }
    }
    JS_DeleteGCRef(state->ctx, &state->symbols_owner_ref);
    JS_DeleteGCRef(state->ctx, &state->channel_owner_ref);
    heap_caps_free(state);
}

static uint32_t rmt_operation_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *driver_state)
{
    const esp32_mquickjs_rmt_future_state_t *state = driver_state;

    return state != NULL ? state->timeout_ms : 0;
}

static esp32_mquickjs_resource_key_t rmt_operation_resource_key(
    const esp32_mquickjs_future_driver_state_t *driver_state)
{
    const esp32_mquickjs_rmt_future_state_t *state = driver_state;
    esp32_mquickjs_rmt_channel_slot_t *slot = state != NULL
        ? rmt_channel_get_slot(&state->channel_ref) : NULL;

    return slot != NULL ? (esp32_mquickjs_resource_key_t)slot : NULL;
}

static const esp32_mquickjs_future_driver_t s_rmt_transmit_driver = {
    .capture = rmt_transmit_prepare,
    .start = rmt_operation_start,
    .poll = rmt_operation_poll,
    .finish = rmt_operation_finish,
    .cancel = rmt_operation_cancel,
    .destroy = rmt_operation_destroy,
    .timeout_ms = rmt_operation_timeout_ms,
    .resource_key = rmt_operation_resource_key,
};

static const esp32_mquickjs_future_driver_t s_rmt_receive_driver = {
    .capture = rmt_receive_prepare,
    .start = rmt_operation_start,
    .poll = rmt_operation_poll,
    .finish = rmt_operation_finish,
    .cancel = rmt_operation_cancel,
    .destroy = rmt_operation_destroy,
    .timeout_ms = rmt_operation_timeout_ms,
    .resource_key = rmt_operation_resource_key,
};

static bool rmt_register_future_drivers(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef object_ref;
    JSGCRef method_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    bool result;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_RMT_CHANNEL);
    *method = JS_IsException(*object)
                  ? JS_EXCEPTION
                  : JS_GetPropertyStr(ctx, *object, "transmit");
    result = !JS_IsException(*method) &&
             esp32_mquickjs_future_register_driver(
                 ctx, runtime, *method, &s_rmt_transmit_driver);
    if (result) {
        *method = JS_GetPropertyStr(ctx, *object, "receive");
        result = !JS_IsException(*method) &&
                 esp32_mquickjs_future_register_driver(
                     ctx, runtime, *method, &s_rmt_receive_driver);
    }
    if (!result && !JS_IsException(*object)) {
        JS_ThrowInternalError(ctx, "failed to register RMT Future drivers");
    }
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &object_ref);
    return result;
}

bool esp32_mquickjs_init_rmt_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime)
{
    uint8_t i;

    for (i = 0; i < RMT_MAX_CHANNELS; ++i) {
        esp_err_t err = rmt_channel_cleanup(&s_rmt_channels[i]);

        if (err != ESP_OK) {
            JS_ThrowInternalError(
                ctx, "failed to reset RMT channel slot %u: %s",
                (unsigned)i, esp_err_to_name(err));
            return false;
        }
        memset(&s_rmt_channels[i], 0, sizeof(s_rmt_channels[i]));
        s_rmt_channels[i].index = i;
    }
    return rmt_register_future_drivers(ctx, runtime);
}

void esp32_mquickjs_deinit_rmt_runtime(void)
{
    uint8_t i;

    for (i = 0; i < RMT_MAX_CHANNELS; ++i) {
        if (s_rmt_channels[i].allocated) {
            rmt_request_close(&s_rmt_channels[i]);
        }
    }
}

JSValue js_rmt_symbol_buffer_constructor(JSContext *ctx, JSValue *this_val,
                                         int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(
        ctx, "RMTSymbolBuffer cannot be constructed directly");
}

void js_rmt_symbol_buffer_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_rmt_symbol_buffer_t *buffer = opaque;

    (void)ctx;
    if (buffer != NULL) {
        buffer->close_pending = true;
        if (buffer->leases == 0) {
            rmt_symbol_buffer_release(buffer);
        }
    }
}

JSValue js_rmt_symbol_buffer_get_capacity(JSContext *ctx, JSValue *this_val,
                                          int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_symbol_buffer_t *buffer;

    (void)argc;
    (void)argv;
    buffer = rmt_symbol_buffer_from_value(ctx, *this_val,
                                          "RMTSymbolBuffer.capacity");
    return buffer == NULL ? JS_EXCEPTION
                          : JS_NewInt64(ctx, (int64_t)buffer->capacity);
}

JSValue js_rmt_symbol_buffer_get_length(JSContext *ctx, JSValue *this_val,
                                        int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_symbol_buffer_t *buffer;

    (void)argc;
    (void)argv;
    buffer = rmt_symbol_buffer_from_value(ctx, *this_val,
                                          "RMTSymbolBuffer.length");
    return buffer == NULL ? JS_EXCEPTION
                          : JS_NewInt64(ctx, (int64_t)buffer->length);
}

JSValue js_rmt_symbol_buffer_push(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_symbol_buffer_t *buffer =
        rmt_symbol_buffer_from_value(ctx, *this_val,
                                     "RMTSymbolBuffer.push()");
    rmt_symbol_word_t symbol;

    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (buffer->leases != 0) {
        return JS_ThrowInternalError(
            ctx, "RMTSymbolBuffer.push() refused while an operation is pending");
    }
    if (!rmt_parse_symbol(ctx, argc, argv, &symbol)) {
        return JS_EXCEPTION;
    }
    if (buffer->length >= buffer->capacity) {
        return JS_ThrowRangeError(ctx, "RMTSymbolBuffer capacity is exhausted");
    }
    buffer->symbols[buffer->length++] = symbol;
    return JS_NewInt64(ctx, (int64_t)buffer->length);
}

JSValue js_rmt_symbol_buffer_get(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_symbol_buffer_t *buffer =
        rmt_symbol_buffer_from_value(ctx, *this_val,
                                     "RMTSymbolBuffer.get()");
    uint32_t index;

    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc != 1 || !rmt_to_u32(ctx, argv[0], &index) ||
        index >= buffer->length) {
        return JS_ThrowRangeError(
            ctx, "RMTSymbolBuffer.get(index) expects an existing index");
    }
    return rmt_symbol_object(ctx, &buffer->symbols[index]);
}

JSValue js_rmt_symbol_buffer_set(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_symbol_buffer_t *buffer =
        rmt_symbol_buffer_from_value(ctx, *this_val,
                                     "RMTSymbolBuffer.set()");
    rmt_symbol_word_t symbol;
    uint32_t index;

    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (buffer->leases != 0) {
        return JS_ThrowInternalError(
            ctx, "RMTSymbolBuffer.set() refused while an operation is pending");
    }
    if (argc != 5 || !rmt_to_u32(ctx, argv[0], &index) ||
        index >= buffer->length) {
        return JS_ThrowRangeError(
            ctx,
            "RMTSymbolBuffer.set(index, ...) expects an existing index");
    }
    if (!rmt_parse_symbol(ctx, argc - 1, argv + 1, &symbol)) {
        return JS_EXCEPTION;
    }
    buffer->symbols[index] = symbol;
    return JS_TRUE;
}

JSValue js_rmt_symbol_buffer_clear(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_symbol_buffer_t *buffer =
        rmt_symbol_buffer_from_value(ctx, *this_val,
                                     "RMTSymbolBuffer.clear()");

    (void)argc;
    (void)argv;
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (buffer->leases != 0) {
        return JS_ThrowInternalError(
            ctx, "RMTSymbolBuffer.clear() refused while an operation is pending");
    }
    buffer->length = 0;
    return JS_TRUE;
}

JSValue js_rmt_symbol_buffer_close(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_symbol_buffer_t *buffer;

    (void)argc;
    (void)argv;
    if (JS_GetClassID(ctx, *this_val) != JS_CLASS_RMT_SYMBOL_BUFFER) {
        return JS_ThrowTypeError(
            ctx, "RMTSymbolBuffer.close() expects an RMTSymbolBuffer");
    }
    buffer = JS_GetOpaque(ctx, *this_val);
    if (buffer == NULL) {
        return JS_TRUE;
    }
    JS_SetOpaque(ctx, *this_val, NULL);
    buffer->close_pending = true;
    if (buffer->leases == 0) {
        rmt_symbol_buffer_release(buffer);
    }
    return JS_TRUE;
}

JSValue js_rmt_channel_constructor(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "RMTChannel cannot be constructed directly");
}

void js_rmt_channel_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_rmt_channel_ref_t *ref = opaque;
    esp32_mquickjs_rmt_channel_slot_t *slot = rmt_channel_get_slot(ref);

    (void)ctx;
    if (slot != NULL) {
        rmt_request_close(slot);
    }
    heap_caps_free(ref);
}

JSValue js_rmt_channel_start(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_channel_slot_t *slot;
    esp_err_t err;

    (void)argc;
    (void)argv;
    if (rmt_channel_from_value(ctx, *this_val, "RMTChannel.start()", NULL,
                               &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (slot->running) {
        return JS_TRUE;
    }
    err = rmt_enable(slot->handle);
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx, "RMTChannel.start() failed: %s",
                                     esp_err_to_name(err));
    }
    slot->running = true;
    return JS_TRUE;
}

JSValue js_rmt_channel_stop(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_channel_slot_t *slot;
    esp_err_t err;

    (void)argc;
    (void)argv;
    if (rmt_channel_from_value(ctx, *this_val, "RMTChannel.stop()", NULL,
                               &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (!slot->running) {
        return JS_TRUE;
    }
    if (slot->busy || slot->future_reservations > 0) {
        return JS_ThrowInternalError(
            ctx, "RMTChannel.stop() refused while an operation is pending");
    }
    err = rmt_disable(slot->handle);
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx, "RMTChannel.stop() failed: %s",
                                     esp_err_to_name(err));
    }
    slot->running = false;
    return JS_TRUE;
}

static JSValue rmt_call_and_wait(JSContext *ctx, JSValue *this_val,
                                 const char *method_name, int argc,
                                 JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *method = JS_GetPropertyStr(ctx, *this_val, method_name);
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

JSValue js_rmt_channel_transmit(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv)
{
    return rmt_call_and_wait(ctx, this_val, "transmit", argc, argv);
}

JSValue js_rmt_channel_receive(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv)
{
    return rmt_call_and_wait(ctx, this_val, "receive", argc, argv);
}

JSValue js_rmt_channel_status(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_channel_slot_t *slot;
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);

    (void)argc;
    (void)argv;
    if (rmt_channel_from_value(ctx, *this_val, "RMTChannel.status()", NULL,
                               &slot) != 0) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "direction",
                                         JS_NewString(
                                             ctx,
                                             slot->direction ==
                                                     ESP32_MQUICKJS_RMT_TX
                                                 ? "tx"
                                                 : "rx")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "pin",
                                         JS_NewInt32(ctx, slot->pin)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "resolutionHz",
            JS_NewUint32(ctx, slot->resolution_hz)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "memorySymbols",
            JS_NewUint32(ctx, slot->memory_symbols)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "dma",
                                         JS_NewBool(slot->dma)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "invert",
                                         JS_NewBool(slot->invert)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "running",
                                         JS_NewBool(slot->running)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "busy",
                                         JS_NewBool(slot->busy))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

JSValue js_rmt_channel_close(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_channel_ref_t *ref;
    esp32_mquickjs_rmt_channel_slot_t *slot;
    esp_err_t err;

    (void)argc;
    (void)argv;
    if (JS_GetClassID(ctx, *this_val) != JS_CLASS_RMT_CHANNEL) {
        return JS_ThrowTypeError(ctx,
                                 "RMTChannel.close() expects an RMTChannel");
    }
    ref = JS_GetOpaque(ctx, *this_val);
    if (ref == NULL) {
        return JS_TRUE;
    }
    slot = rmt_channel_get_slot(ref);
    if (slot != NULL) {
        if (slot->busy || slot->future_reservations > 0) {
            rmt_request_close(slot);
        } else {
            slot->release_pending = true;
            err = rmt_channel_cleanup(slot);
            if (err != ESP_OK) {
                return JS_ThrowInternalError(
                    ctx, "RMTChannel.close() failed: %s",
                    esp_err_to_name(err));
            }
        }
    }
    JS_SetOpaque(ctx, *this_val, NULL);
    heap_caps_free(ref);
    return JS_TRUE;
}

JSValue js_rmt_capabilities(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "rx", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "tx", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "dma",
#if SOC_RMT_SUPPORT_DMA
            JS_TRUE) ||
#else
            JS_FALSE) ||
#endif
        !esp32_mquickjs_set_property_ref(
            ctx, result, "minMemorySymbols",
            JS_NewUint32(ctx, SOC_RMT_MEM_WORDS_PER_CHANNEL)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "maxSymbols",
            JS_NewUint32(ctx, RMT_MAX_SYMBOLS)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "maxDurationTicks",
            JS_NewUint32(ctx, RMT_MAX_DURATION_TICKS)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "finiteLoops",
                                         JS_TRUE)) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

JSValue js_rmt_create_symbols(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    uint32_t capacity;
    esp32_mquickjs_rmt_symbol_buffer_t *buffer;
    esp32_mquickjs_rmt_symbol_buffer_resources_t resources = {0};
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    (void)this_val;
    if (argc != 1 || !rmt_to_u32(ctx, argv[0], &capacity) || capacity == 0 ||
        capacity > RMT_MAX_SYMBOLS) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowRangeError(
            ctx,
            "rmt.createSymbols(capacity) expects 1 through 4096 symbols");
    }
    if (!esp32_mquickjs_rmt_symbol_buffer_resources_init(
            &resources, &s_rmt_symbol_buffer_resource_ops, sizeof(*buffer),
            capacity, sizeof(*buffer->symbols))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    buffer = resources.buffer;
    buffer->symbols = resources.symbols;
    buffer->capacity = capacity;
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_RMT_SYMBOL_BUFFER);
    if (JS_IsException(*object)) {
        rmt_symbol_buffer_release(buffer);
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(ctx, *object, buffer);
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_rmt_open(JSContext *ctx, JSValue *this_val,
                    int argc, JSValue *argv)
{
    esp32_mquickjs_rmt_direction_t direction;
    uint32_t resolution_hz;
    uint32_t memory_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
    uint32_t raw_pin;
    bool dma = false;
    bool invert = false;
    int pin;
    esp32_mquickjs_rmt_channel_slot_t *slot = NULL;
    rmt_channel_resource_context_t resource_context = {0};
    esp32_mquickjs_rmt_channel_resources_t resources = {0};
    esp32_mquickjs_rmt_channel_resource_ops_t resource_ops;
    esp_err_t err;
    uint8_t i;
    JSValue result;

    (void)this_val;
    if (argc != 1 || !rmt_is_object(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx,
                                 "rmt.open(options) expects an options object");
    }
    {
        JSGCRef property_ref;
        JSValue *property = JS_PushGCRef(ctx, &property_ref);

        *property = JS_GetPropertyStr(ctx, argv[0], "direction");
        if (JS_IsException(*property)) {
            goto parse_fail;
        }
        if (rmt_string_equals(ctx, *property, "rx")) {
            direction = ESP32_MQUICKJS_RMT_RX;
        } else if (rmt_string_equals(ctx, *property, "tx")) {
            direction = ESP32_MQUICKJS_RMT_TX;
        } else {
            JS_ThrowTypeError(ctx,
                              "rmt.open({ direction }) expects \"rx\" or \"tx\"");
            goto parse_fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "pin");
        if (JS_IsException(*property) ||
            !rmt_to_u32(ctx, *property, &raw_pin) || raw_pin >= GPIO_NUM_MAX) {
            JS_ThrowTypeError(ctx,
                              "rmt.open({ pin }) expects a valid GPIO number");
            goto parse_fail;
        }
        pin = (int)raw_pin;
        if ((direction == ESP32_MQUICKJS_RMT_TX &&
             !GPIO_IS_VALID_OUTPUT_GPIO(pin)) ||
            (direction == ESP32_MQUICKJS_RMT_RX &&
             !GPIO_IS_VALID_GPIO(pin))) {
            JS_ThrowTypeError(ctx,
                              "rmt.open({ pin }) is invalid for the direction");
            goto parse_fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "resolutionHz");
        if (JS_IsException(*property) ||
            !rmt_to_u32(ctx, *property, &resolution_hz) ||
            resolution_hz == 0) {
            JS_ThrowTypeError(
                ctx, "rmt.open({ resolutionHz }) expects a positive integer");
            goto parse_fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "memorySymbols");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             !rmt_to_u32(ctx, *property, &memory_symbols))) {
            JS_ThrowTypeError(
                ctx, "rmt.open({ memorySymbols }) expects an integer");
            goto parse_fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "dma");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             !rmt_to_bool(ctx, *property, &dma))) {
            JS_ThrowTypeError(ctx, "rmt.open({ dma }) expects a boolean");
            goto parse_fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "invert");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             !rmt_to_bool(ctx, *property, &invert))) {
            JS_ThrowTypeError(ctx, "rmt.open({ invert }) expects a boolean");
            goto parse_fail;
        }
        JS_PopGCRef(ctx, &property_ref);
        goto parsed;

parse_fail:
        JS_PopGCRef(ctx, &property_ref);
        return JS_EXCEPTION;
    }

parsed:
    if (memory_symbols < SOC_RMT_MEM_WORDS_PER_CHANNEL ||
        memory_symbols > RMT_MAX_SYMBOLS || (memory_symbols & 1U) != 0) {
        return JS_ThrowRangeError(
            ctx,
            "RMT memorySymbols must be even and within the target memory limits");
    }
#if !SOC_RMT_SUPPORT_DMA
    if (dma) {
        return JS_ThrowInternalError(
            ctx, "RMT DMA is unavailable on this target");
    }
#endif
    for (i = 0; i < RMT_MAX_CHANNELS; ++i) {
        if (s_rmt_channels[i].allocated &&
            s_rmt_channels[i].release_pending &&
            !s_rmt_channels[i].busy &&
            s_rmt_channels[i].future_reservations == 0) {
            (void)rmt_channel_cleanup(&s_rmt_channels[i]);
        }
        if (!s_rmt_channels[i].allocated) {
            slot = &s_rmt_channels[i];
            break;
        }
    }
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "rmt.open() found no free channel slots");
    }
    slot->direction = direction;
    slot->pin = pin;
    slot->resolution_hz = resolution_hz;
    slot->memory_symbols = memory_symbols;
    slot->dma = dma;
    slot->invert = invert;
    resource_context.slot = slot;
    resource_context.direction = direction;
    if (direction == ESP32_MQUICKJS_RMT_TX) {
        resource_context.tx_config = (rmt_tx_channel_config_t){
            .gpio_num = (gpio_num_t)pin,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = resolution_hz,
            .mem_block_symbols = memory_symbols,
            .trans_queue_depth = 1,
            .flags = {
                .invert_out = invert,
                .with_dma = dma,
            },
        };
    } else {
        resource_context.rx_config = (rmt_rx_channel_config_t){
            .gpio_num = (gpio_num_t)pin,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = resolution_hz,
            .mem_block_symbols = memory_symbols,
            .flags = {
                .invert_in = invert,
                .with_dma = dma,
            },
        };
    }
    resource_ops = rmt_channel_resource_ops(&resource_context);
    err = (esp_err_t)esp32_mquickjs_rmt_channel_resources_init(
        &resources, direction == ESP32_MQUICKJS_RMT_TX, &resource_ops);
    slot->handle = (rmt_channel_handle_t)resources.channel;
    slot->encoder = (rmt_encoder_handle_t)resources.encoder;
    slot->running = resources.enabled;
    if (err != ESP_OK) {
        uint8_t index = slot->index;

        if (slot->handle != NULL || slot->encoder != NULL || slot->running) {
            slot->allocated = true;
            slot->release_pending = true;
        } else {
            memset(slot, 0, sizeof(*slot));
            slot->index = index;
        }
        return JS_ThrowInternalError(ctx, "rmt.open() failed: %s",
                                     esp_err_to_name(err));
    }
    slot->allocated = true;
    slot->generation = rmt_take_generation();
    result = rmt_make_channel(ctx, slot);
    if (JS_IsException(result)) {
        slot->release_pending = true;
        (void)rmt_channel_cleanup(slot);
    }
    return result;
}

#endif
