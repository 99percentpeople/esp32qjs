#include "esp32_mquickjs_i2s.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_I2S

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_peripheral_lease.h"
#include "utils/esp32_mquickjs_byte_source.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_common.h"
#if SOC_I2S_SUPPORTS_PDM_RX
#include "driver/i2s_pdm.h"
#endif
#include "driver/i2s_std.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "hal/i2s_ll.h"
#include "soc/soc_caps.h"

#define I2S_MAX_READ_BYTES (64U * 1024U)
#define I2S_DMA_MAX_DESCRIPTOR_BYTES 4092U
#define I2S_DEFAULT_SAMPLE_RATE_HZ 16000U
#define I2S_DEFAULT_TIMEOUT_MS 1000U
#define I2S_DEFAULT_DMA_DESCRIPTORS 6U
#define I2S_DEFAULT_DMA_FRAMES 240U

typedef enum {
    ESP32_MQUICKJS_I2S_MODE_STANDARD,
    ESP32_MQUICKJS_I2S_MODE_PDM,
} esp32_mquickjs_i2s_mode_t;

typedef struct {
    int32_t port;
    uint32_t generation;
} esp32_mquickjs_i2s_ref_t;

typedef struct {
    bool allocated;
    bool running;
    bool busy;
    bool release_pending;
    int32_t port;
    uint32_t generation;
    esp32_mquickjs_i2s_mode_t mode;
    uint32_t sample_rate_hz;
    uint8_t data_bits;
    uint8_t slot_bits;
    uint8_t channels;
    uint32_t dma_descriptor_count;
    uint32_t dma_frames_per_descriptor;
    uint32_t timeout_ms;
    volatile uint32_t overruns;
    uint32_t sequence;
    i2s_chan_handle_t rx_handle;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    esp32_mquickjs_peripheral_lease_t lease;
} esp32_mquickjs_i2s_slot_t;

static esp32_mquickjs_i2s_slot_t s_i2s_slots[I2S_LL_GET(INST_NUM)];
static uint32_t s_i2s_next_generation = 1;

struct esp32_mquickjs_future_driver_state {
    JSContext *ctx;
    JSGCRef owner_ref;
    esp32_mquickjs_i2s_ref_t input_ref;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    esp_timer_handle_t timeout_timer;
    uint8_t *data;
    size_t requested_bytes;
    size_t received_bytes;
    uint32_t frame_count;
    uint32_t timeout_ms;
    uint64_t deadline_us;
    esp_err_t err;
    bool owner_retained;
    bool started;
    bool completed;
    bool cancelled;
    bool timed_out;
};

static uint32_t i2s_take_generation(void)
{
    uint32_t generation = s_i2s_next_generation++;

    if (generation == 0) {
        generation = s_i2s_next_generation++;
    }
    return generation;
}

static bool i2s_to_u32(JSContext *ctx, JSValue value, uint32_t *out)
{
    double number;
    uint32_t converted;

    if (!JS_IsNumber(ctx, value) ||
        JS_ToNumber(ctx, &number, value) != 0 || !isfinite(number) ||
        number < 0 || number > INT32_MAX) {
        return false;
    }
    converted = (uint32_t)number;
    if ((double)converted != number) {
        return false;
    }
    *out = converted;
    return true;
}

static bool i2s_to_gpio(JSContext *ctx, JSValue value, bool output, int *out)
{
    uint32_t raw;
    int pin;

    if (!i2s_to_u32(ctx, value, &raw) || raw >= GPIO_NUM_MAX) {
        return false;
    }
    pin = (int)raw;
    if (output ? !GPIO_IS_VALID_OUTPUT_GPIO(pin) : !GPIO_IS_VALID_GPIO(pin)) {
        return false;
    }
    *out = pin;
    return true;
}

static bool i2s_string_equals(JSContext *ctx, JSValue value, const char *expected)
{
    JSCStringBuf buffer;
    const char *text;

    if (!JS_IsString(ctx, value)) {
        return false;
    }
    text = JS_ToCString(ctx, value, &buffer);
    return text != NULL && strcmp(text, expected) == 0;
}

static esp32_mquickjs_i2s_slot_t *i2s_get_slot(
    const esp32_mquickjs_i2s_ref_t *ref)
{
    esp32_mquickjs_i2s_slot_t *slot;

    if (ref == NULL || ref->port < 0 ||
        ref->port >= I2S_LL_GET(INST_NUM)) {
        return NULL;
    }
    slot = &s_i2s_slots[ref->port];
    if (!slot->allocated || slot->generation != ref->generation) {
        return NULL;
    }
    return slot;
}

static void i2s_cleanup_slot(esp32_mquickjs_i2s_slot_t *slot)
{
    int32_t port;

    if (slot == NULL || !slot->allocated || slot->busy) {
        return;
    }
    port = slot->port;
    if (slot->running && slot->rx_handle != NULL) {
        (void)i2s_channel_disable(slot->rx_handle);
    }
    if (slot->rx_handle != NULL) {
        (void)i2s_del_channel(slot->rx_handle);
    }
    esp32_mquickjs_peripheral_lease_release(&slot->lease);
    memset(slot, 0, sizeof(*slot));
    slot->port = port;
}

static int i2s_ref_from_value(JSContext *ctx, JSValue value,
                              const char *api_name,
                              esp32_mquickjs_i2s_ref_t *out_ref,
                              esp32_mquickjs_i2s_slot_t **out_slot)
{
    esp32_mquickjs_i2s_ref_t *ref;
    esp32_mquickjs_i2s_slot_t *slot;

    if (JS_GetClassID(ctx, value) != JS_CLASS_I2S_INPUT ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "%s expects an I2SInput", api_name);
        return -1;
    }
    slot = i2s_get_slot(ref);
    if (slot == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the I2S input is closed",
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

static JSValue i2s_make_input(JSContext *ctx,
                              const esp32_mquickjs_i2s_slot_t *slot)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    esp32_mquickjs_i2s_ref_t *ref;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_I2S_INPUT);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    ref = heap_caps_malloc(sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    ref->port = slot->port;
    ref->generation = slot->generation;
    JS_SetOpaque(ctx, *object, ref);
    return JS_PopGCRef(ctx, &object_ref);
}

static bool IRAM_ATTR i2s_on_receive(i2s_chan_handle_t handle,
                                     i2s_event_data_t *event,
                                     void *user_ctx)
{
    esp32_mquickjs_i2s_slot_t *slot = user_ctx;
    int task_woken = pdFALSE;

    (void)handle;
    (void)event;
    if (slot != NULL && slot->busy && slot->runtime != NULL) {
        (void)esp32_mquickjs_future_wake_from_isr(slot->runtime, slot->token,
                                                 &task_woken);
    }
    return task_woken == pdTRUE;
}

static bool IRAM_ATTR i2s_on_overflow(i2s_chan_handle_t handle,
                                      i2s_event_data_t *event,
                                      void *user_ctx)
{
    esp32_mquickjs_i2s_slot_t *slot = user_ctx;
    int task_woken = pdFALSE;

    (void)handle;
    (void)event;
    if (slot != NULL) {
        slot->overruns++;
        if (slot->busy && slot->runtime != NULL) {
            (void)esp32_mquickjs_future_wake_from_isr(
                slot->runtime, slot->token, &task_woken);
        }
    }
    return task_woken == pdTRUE;
}

static JSValue i2s_status_object(JSContext *ctx,
                                 const esp32_mquickjs_i2s_slot_t *slot)
{
    JSGCRef result_ref;
    JSGCRef pcm_ref;
    JSGCRef dma_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *pcm = JS_PushGCRef(ctx, &pcm_ref);
    JSValue *dma = JS_PushGCRef(ctx, &dma_ref);

    *result = JS_NewObject(ctx);
    *pcm = JS_NewObject(ctx);
    *dma = JS_NewObject(ctx);
    if (JS_IsException(*result) || JS_IsException(*pcm) ||
        JS_IsException(*dma) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "port",
                                         JS_NewInt32(ctx, slot->port)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "running",
                                         JS_NewBool(slot->running)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "mode",
            JS_NewString(ctx, slot->mode == ESP32_MQUICKJS_I2S_MODE_PDM
                                  ? "pdm" : "standard")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "overruns",
                                         JS_NewUint32(ctx, slot->overruns)) ||
        !esp32_mquickjs_set_property_ref(ctx, pcm, "sampleRateHz",
                                         JS_NewUint32(ctx, slot->sample_rate_hz)) ||
        !esp32_mquickjs_set_property_ref(ctx, pcm, "dataBits",
                                         JS_NewInt32(ctx, slot->data_bits)) ||
        !esp32_mquickjs_set_property_ref(ctx, pcm, "slotBits",
                                         JS_NewInt32(ctx, slot->slot_bits)) ||
        !esp32_mquickjs_set_property_ref(ctx, pcm, "channels",
                                         JS_NewInt32(ctx, slot->channels)) ||
        !esp32_mquickjs_set_property_ref(ctx, pcm, "signed",
                                         JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, pcm, "endianness",
                                         JS_NewString(ctx, "little")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, dma, "descriptorCount",
            JS_NewUint32(ctx, slot->dma_descriptor_count)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, dma, "framesPerDescriptor",
            JS_NewUint32(ctx, slot->dma_frames_per_descriptor)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "pcm", *pcm) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "dma", *dma)) {
        JS_PopGCRef(ctx, &dma_ref);
        JS_PopGCRef(ctx, &pcm_ref);
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    *pcm = JS_UNDEFINED;
    *dma = JS_UNDEFINED;
    JS_PopGCRef(ctx, &dma_ref);
    JS_PopGCRef(ctx, &pcm_ref);
    return JS_PopGCRef(ctx, &result_ref);
}

static void i2s_timeout_timer(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state != NULL && !state->completed && state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
}

static bool i2s_read_prepare(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_i2s_slot_t *slot;
    uint32_t frame_count;
    uint32_t timeout_ms;
    size_t bytes_per_frame;
    JSValue *owner;

    if (out_state == NULL || argc < 1 || argc > 2 ||
        !i2s_to_u32(ctx, argv[0].val, &frame_count) || frame_count == 0) {
        JS_ThrowTypeError(ctx,
                          "I2SInput.read(frameCount, timeoutMs?) expects a positive frame count");
        return false;
    }
    if (i2s_ref_from_value(ctx, this_ref->val, "I2SInput.read()", NULL,
                           &slot) != 0) {
        return false;
    }
    timeout_ms = slot->timeout_ms;
    if (argc == 2 && !JS_IsUndefined(argv[1].val) &&
        !i2s_to_u32(ctx, argv[1].val, &timeout_ms)) {
        JS_ThrowTypeError(ctx,
                          "I2SInput.read(frameCount, timeoutMs?) expects a non-negative timeout");
        return false;
    }
    if (!slot->running) {
        JS_ThrowInternalError(ctx, "I2SInput.read() requires start() first");
        return false;
    }
    if (slot->busy) {
        JS_ThrowInternalError(ctx, "I2S input already has a pending read");
        return false;
    }
    bytes_per_frame = ((size_t)slot->slot_bits / 8U) * slot->channels;
    if (frame_count > I2S_MAX_READ_BYTES / bytes_per_frame) {
        JS_ThrowRangeError(ctx,
                           "I2SInput.read() output exceeds the 64 KiB limit");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->data = heap_caps_malloc((size_t)frame_count * bytes_per_frame,
                                   MALLOC_CAP_8BIT);
    if (state->data == NULL) {
        heap_caps_free(state);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->input_ref.port = slot->port;
    state->input_ref.generation = slot->generation;
    state->requested_bytes = (size_t)frame_count * bytes_per_frame;
    state->frame_count = frame_count;
    state->timeout_ms = timeout_ms;
    owner = JS_AddGCRef(ctx, &state->owner_ref);
    *owner = this_ref->val;
    state->owner_retained = true;
    *out_state = state;
    return true;
}

static void i2s_read_step(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2s_slot_t *slot =
        state != NULL ? i2s_get_slot(&state->input_ref) : NULL;
    size_t read_bytes = 0;

    if (state == NULL || state->completed || state->cancelled) {
        return;
    }
    if (slot == NULL || !slot->running) {
        state->err = ESP_ERR_INVALID_STATE;
        state->completed = true;
        return;
    }
    state->err = i2s_channel_read(
        slot->rx_handle, state->data + state->received_bytes,
        state->requested_bytes - state->received_bytes, &read_bytes, 0);
    /*
     * ESP-IDF reports ESP_ERR_TIMEOUT when a zero-wait read consumes the
     * currently available DMA bytes but cannot fill the complete request.
     * Those partial bytes are valid and must be retained for the next wake;
     * dropping them makes reads larger than one DMA descriptor eventually
     * time out while continuously draining the driver's queue.
     */
    if (state->err == ESP_OK || state->err == ESP_ERR_TIMEOUT) {
        state->received_bytes += read_bytes;
        if (state->received_bytes >= state->requested_bytes) {
            state->err = ESP_OK;
            state->completed = true;
            return;
        }
        if (state->err == ESP_ERR_TIMEOUT) {
            state->err = ESP_OK;
        }
    } else {
        state->completed = true;
        return;
    }
    if (state->timeout_ms == 0 ||
        (state->deadline_us > 0 &&
         (uint64_t)esp_timer_get_time() >= state->deadline_us)) {
        state->timed_out = state->received_bytes == 0;
        state->completed = true;
    }
}

static bool i2s_read_start(JSContext *ctx,
                           esp32_mquickjs_runtime_t *runtime,
                           esp32_mquickjs_future_token_t token,
                           esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2s_slot_t *slot =
        state != NULL ? i2s_get_slot(&state->input_ref) : NULL;

    if (state == NULL || slot == NULL || !slot->running) {
        JS_ThrowReferenceError(ctx, "I2S input closed before read started");
        return false;
    }
    if (slot->busy) {
        JS_ThrowInternalError(ctx, "I2S input already has a pending read");
        return false;
    }
    slot->busy = true;
    slot->runtime = runtime;
    slot->token = token;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (state->timeout_ms > 0) {
        esp_timer_create_args_t args = {
            .callback = i2s_timeout_timer,
            .arg = state,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "mqjs_i2s",
            .skip_unhandled_events = true,
        };

        state->deadline_us = (uint64_t)esp_timer_get_time() +
                             (uint64_t)state->timeout_ms * 1000ULL;
        if (esp_timer_create(&args, &state->timeout_timer) != ESP_OK ||
            esp_timer_start_once(state->timeout_timer,
                                 (uint64_t)state->timeout_ms * 1000ULL) !=
                ESP_OK) {
            JS_ThrowInternalError(ctx, "failed to start I2S read timeout");
            return false;
        }
    }
    i2s_read_step(state);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static esp32_mquickjs_future_poll_t i2s_read_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    i2s_read_step(state);
    return state != NULL && state->completed
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue i2s_read_finish(JSContext *ctx,
                               esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2s_slot_t *slot =
        state != NULL ? i2s_get_slot(&state->input_ref) : NULL;
    JSGCRef result_ref;
    JSGCRef data_ref;
    JSValue *result;
    JSValue *data;
    size_t bytes_per_frame;
    uint32_t frames;

    if (state == NULL || state->cancelled) {
        return JS_ThrowInternalError(ctx, "I2S read cancelled");
    }
    if (state->err != ESP_OK) {
        return JS_ThrowInternalError(ctx, "I2SInput.read() failed: %s",
                                     esp_err_to_name(state->err));
    }
    if (state->timed_out || state->received_bytes == 0) {
        return JS_NULL;
    }
    if (slot == NULL) {
        return JS_ThrowReferenceError(ctx, "I2S input closed during read");
    }
    bytes_per_frame = ((size_t)slot->slot_bits / 8U) * slot->channels;
    frames = (uint32_t)(state->received_bytes / bytes_per_frame);
    state->received_bytes = (size_t)frames * bytes_per_frame;
    result = JS_PushGCRef(ctx, &result_ref);
    data = JS_PushGCRef(ctx, &data_ref);
    *result = JS_NewObject(ctx);
    *data = JS_IsException(*result)
                ? JS_EXCEPTION
                : esp32_mquickjs_new_owned_byte_view(
                      ctx, state->data, state->received_bytes);
    if (!JS_IsException(*data)) {
        state->data = NULL;
    }
    if (JS_IsException(*result) || JS_IsException(*data) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "data", *data) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "frames",
                                         JS_NewUint32(ctx, frames)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "byteLength",
            JS_NewInt64(ctx, (int64_t)state->received_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "timestampUs",
            JS_NewInt64(ctx, (int64_t)esp_timer_get_time())) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sequence", JS_NewUint32(ctx, slot->sequence++)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "overruns", JS_NewUint32(ctx, slot->overruns))) {
        JS_PopGCRef(ctx, &data_ref);
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    *data = JS_UNDEFINED;
    JS_PopGCRef(ctx, &data_ref);
    return JS_PopGCRef(ctx, &result_ref);
}

static bool i2s_read_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed || state->cancelled) {
        return false;
    }
    state->cancelled = true;
    state->completed = true;
    if (state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
    return true;
}

static void i2s_read_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2s_slot_t *slot =
        state != NULL ? i2s_get_slot(&state->input_ref) : NULL;

    if (state == NULL) {
        return;
    }
    if (state->timeout_timer != NULL) {
        (void)esp_timer_stop(state->timeout_timer);
        (void)esp_timer_delete(state->timeout_timer);
    }
    if (slot != NULL && state->started) {
        slot->busy = false;
        slot->runtime = NULL;
        if (slot->release_pending) {
            i2s_cleanup_slot(slot);
        }
    }
    if (state->owner_retained) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
    }
    heap_caps_free(state->data);
    heap_caps_free(state);
}

static const esp32_mquickjs_future_driver_t s_i2s_read_driver = {
    .prepare = i2s_read_prepare,
    .start = i2s_read_start,
    .poll = i2s_read_poll,
    .finish = i2s_read_finish,
    .cancel = i2s_read_cancel,
    .destroy = i2s_read_destroy,
};

static bool i2s_register_future_driver(JSContext *ctx,
                                       esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef object_ref;
    JSGCRef read_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *read = JS_PushGCRef(ctx, &read_ref);
    bool result;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_I2S_INPUT);
    *read = JS_IsException(*object) ? JS_EXCEPTION
                                    : JS_GetPropertyStr(ctx, *object, "read");
    result = !JS_IsException(*read) &&
             esp32_mquickjs_future_register_driver(ctx, runtime, *read,
                                                    &s_i2s_read_driver);
    if (!result && !JS_IsException(*object)) {
        JS_ThrowInternalError(ctx, "failed to register I2S Future driver");
    }
    JS_PopGCRef(ctx, &read_ref);
    JS_PopGCRef(ctx, &object_ref);
    return result;
}

bool esp32_mquickjs_init_i2s_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime)
{
    int i;

    for (i = 0; i < I2S_LL_GET(INST_NUM); ++i) {
        if (s_i2s_slots[i].allocated && !s_i2s_slots[i].busy) {
            i2s_cleanup_slot(&s_i2s_slots[i]);
        }
        memset(&s_i2s_slots[i], 0, sizeof(s_i2s_slots[i]));
        s_i2s_slots[i].port = i;
    }
    return i2s_register_future_driver(ctx, runtime);
}

void esp32_mquickjs_deinit_i2s_runtime(void)
{
    int i;

    for (i = 0; i < I2S_LL_GET(INST_NUM); ++i) {
        if (s_i2s_slots[i].allocated) {
            s_i2s_slots[i].release_pending = true;
            if (!s_i2s_slots[i].busy) {
                i2s_cleanup_slot(&s_i2s_slots[i]);
            }
        }
    }
}

JSValue js_i2s_input_constructor(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "I2SInput cannot be constructed directly");
}

void js_i2s_input_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_i2s_ref_t *ref = opaque;
    esp32_mquickjs_i2s_slot_t *slot = i2s_get_slot(ref);

    (void)ctx;
    if (slot != NULL) {
        if (slot->busy) {
            slot->release_pending = true;
        } else {
            i2s_cleanup_slot(slot);
        }
    }
    heap_caps_free(ref);
}

JSValue js_i2s_input_start(JSContext *ctx, JSValue *this_val,
                           int argc, JSValue *argv)
{
    esp32_mquickjs_i2s_slot_t *slot;
    esp_err_t err;

    (void)argc;
    (void)argv;
    if (i2s_ref_from_value(ctx, *this_val, "I2SInput.start()", NULL,
                           &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (slot->running) {
        return JS_TRUE;
    }
    err = i2s_channel_enable(slot->rx_handle);
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx, "I2SInput.start() failed: %s",
                                     esp_err_to_name(err));
    }
    slot->running = true;
    return JS_TRUE;
}

JSValue js_i2s_input_stop(JSContext *ctx, JSValue *this_val,
                          int argc, JSValue *argv)
{
    esp32_mquickjs_i2s_slot_t *slot;
    esp_err_t err;

    (void)argc;
    (void)argv;
    if (i2s_ref_from_value(ctx, *this_val, "I2SInput.stop()", NULL,
                           &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (!slot->running) {
        return JS_TRUE;
    }
    if (slot->busy) {
        return JS_ThrowInternalError(ctx,
                                     "I2SInput.stop() refused while a read is pending");
    }
    err = i2s_channel_disable(slot->rx_handle);
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx, "I2SInput.stop() failed: %s",
                                     esp_err_to_name(err));
    }
    slot->running = false;
    return JS_TRUE;
}

JSValue js_i2s_input_read(JSContext *ctx, JSValue *this_val,
                          int argc, JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *method = JS_GetPropertyStr(ctx, *this_val, "read");
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

JSValue js_i2s_input_status(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv)
{
    esp32_mquickjs_i2s_slot_t *slot;

    (void)argc;
    (void)argv;
    if (i2s_ref_from_value(ctx, *this_val, "I2SInput.status()", NULL,
                           &slot) != 0) {
        return JS_EXCEPTION;
    }
    return i2s_status_object(ctx, slot);
}

JSValue js_i2s_input_close(JSContext *ctx, JSValue *this_val,
                           int argc, JSValue *argv)
{
    esp32_mquickjs_i2s_ref_t *ref;
    esp32_mquickjs_i2s_slot_t *slot;

    (void)argc;
    (void)argv;
    if (JS_GetClassID(ctx, *this_val) != JS_CLASS_I2S_INPUT) {
        return JS_ThrowTypeError(ctx, "I2SInput.close() expects an I2SInput");
    }
    ref = JS_GetOpaque(ctx, *this_val);
    if (ref == NULL) {
        return JS_TRUE;
    }
    slot = i2s_get_slot(ref);
    if (slot == NULL) {
        JS_SetOpaque(ctx, *this_val, NULL);
        heap_caps_free(ref);
        return JS_TRUE;
    }
    if (slot->busy) {
        return JS_ThrowInternalError(ctx,
                                     "I2SInput.close() refused while a read is pending");
    }
    i2s_cleanup_slot(slot);
    JS_SetOpaque(ctx, *this_val, NULL);
    heap_caps_free(ref);
    return JS_TRUE;
}

JSValue js_i2s_capabilities(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv)
{
    JSGCRef result_ref;
    JSGCRef ports_ref;
    JSGCRef bits_ref;
    JSGCRef limits_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *ports = JS_PushGCRef(ctx, &ports_ref);
    JSValue *bits = JS_PushGCRef(ctx, &bits_ref);
    JSValue *limits = JS_PushGCRef(ctx, &limits_ref);
    int i;

    (void)this_val;
    (void)argc;
    (void)argv;
    *result = JS_NewObject(ctx);
    *ports = JS_NewArray(ctx, 0);
    *bits = JS_NewArray(ctx, 0);
    *limits = JS_NewObject(ctx);
    if (JS_IsException(*result) || JS_IsException(*ports) ||
        JS_IsException(*bits) || JS_IsException(*limits)) {
        goto fail;
    }
    for (i = 0; i < I2S_LL_GET(INST_NUM); ++i) {
        if (JS_IsException(JS_SetPropertyUint32(ctx, *ports, (uint32_t)i,
                                                JS_NewInt32(ctx, i)))) {
            goto fail;
        }
    }
    if (JS_IsException(JS_SetPropertyUint32(ctx, *bits, 0,
                                            JS_NewInt32(ctx, 8))) ||
        JS_IsException(JS_SetPropertyUint32(ctx, *bits, 1,
                                            JS_NewInt32(ctx, 16))) ||
        JS_IsException(JS_SetPropertyUint32(ctx, *bits, 2,
                                            JS_NewInt32(ctx, 24))) ||
        JS_IsException(JS_SetPropertyUint32(ctx, *bits, 3,
                                            JS_NewInt32(ctx, 32))) ||
        !esp32_mquickjs_set_property_ref(ctx, limits, "maxDescriptorBytes",
                                         JS_NewUint32(ctx, I2S_DMA_MAX_DESCRIPTOR_BYTES)) ||
        !esp32_mquickjs_set_property_ref(ctx, limits, "maxReadBytes",
                                         JS_NewUint32(ctx, I2S_MAX_READ_BYTES)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ports", *ports) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "standard", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "pdm",
#if SOC_I2S_SUPPORTS_PDM_RX && SOC_I2S_SUPPORTS_PDM2PCM
            JS_TRUE) ||
#else
            JS_FALSE) ||
#endif
        !esp32_mquickjs_set_property_ref(ctx, result, "dataBits", *bits) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "limits", *limits)) {
        goto fail;
    }
    *ports = JS_UNDEFINED;
    *bits = JS_UNDEFINED;
    *limits = JS_UNDEFINED;
    JS_PopGCRef(ctx, &limits_ref);
    JS_PopGCRef(ctx, &bits_ref);
    JS_PopGCRef(ctx, &ports_ref);
    return JS_PopGCRef(ctx, &result_ref);

fail:
    JS_PopGCRef(ctx, &limits_ref);
    JS_PopGCRef(ctx, &bits_ref);
    JS_PopGCRef(ctx, &ports_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static bool i2s_read_u32_property(JSContext *ctx, JSValue object,
                                  const char *name, uint32_t *value)
{
    JSValue property = JS_GetPropertyStr(ctx, object, name);

    return !JS_IsException(property) &&
           (JS_IsUndefined(property) || i2s_to_u32(ctx, property, value));
}

JSValue js_i2s_open(JSContext *ctx, JSValue *this_val, int argc,
                    JSValue *argv)
{
    esp32_mquickjs_i2s_mode_t mode = ESP32_MQUICKJS_I2S_MODE_STANDARD;
    uint32_t sample_rate = I2S_DEFAULT_SAMPLE_RATE_HZ;
    uint32_t data_bits = 16;
    uint32_t slot_bits = 16;
    uint32_t dma_descriptors = I2S_DEFAULT_DMA_DESCRIPTORS;
    uint32_t dma_frames = I2S_DEFAULT_DMA_FRAMES;
    uint32_t timeout_ms = I2S_DEFAULT_TIMEOUT_MS;
    i2s_slot_mode_t slot_mode = I2S_SLOT_MODE_MONO;
    i2s_std_slot_mask_t slot_mask = I2S_STD_SLOT_LEFT;
    const char *format = "philips";
    int requested_port = I2S_NUM_AUTO;
    int bclk = -1;
    int ws = -1;
    int din = -1;
    int mclk = I2S_GPIO_UNUSED;
    int pdm_clk = esp32_mquickjs_profile_int_or("ESP32QJS_I2S_PDM_CLK", -2);
    int pdm_din = esp32_mquickjs_profile_int_or("ESP32QJS_I2S_PDM_DIN", -2);
    esp32_mquickjs_i2s_slot_t *slot = NULL;
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_event_callbacks_t callbacks = {
        .on_recv = i2s_on_receive,
        .on_recv_q_ovf = i2s_on_overflow,
    };
    esp_err_t err;
    JSValue result;
    int i;

    (void)this_val;
    if (argc != 1 || JS_GetClassID(ctx, argv[0]) < 0 ||
        JS_IsArray(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "i2s.open(options) expects an options object");
    }
    {
        JSGCRef property_ref;
        JSGCRef pins_ref;
        JSGCRef dma_ref;
        JSValue *property = JS_PushGCRef(ctx, &property_ref);
        JSValue *pins = JS_PushGCRef(ctx, &pins_ref);
        JSValue *dma = JS_PushGCRef(ctx, &dma_ref);
        JSCStringBuf text_buf;
        const char *text;

        *property = JS_GetPropertyStr(ctx, argv[0], "direction");
        if (JS_IsException(*property) || !i2s_string_equals(ctx, *property, "rx")) {
            JS_ThrowTypeError(ctx, "i2s.open({ direction }) only accepts \"rx\"");
            goto parse_fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "mode");
        if (JS_IsException(*property) || !JS_IsString(ctx, *property)) {
            JS_ThrowTypeError(ctx, "i2s.open({ mode }) expects \"standard\" or \"pdm\"");
            goto parse_fail;
        }
        text = JS_ToCString(ctx, *property, &text_buf);
        if (text == NULL) {
            goto parse_fail;
        }
        if (strcmp(text, "pdm") == 0) {
            mode = ESP32_MQUICKJS_I2S_MODE_PDM;
        } else if (strcmp(text, "standard") != 0) {
            JS_ThrowTypeError(ctx, "i2s.open({ mode }) expects \"standard\" or \"pdm\"");
            goto parse_fail;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "port");
        if (JS_IsException(*property)) {
            goto parse_fail;
        }
        if (!JS_IsUndefined(*property) && !i2s_string_equals(ctx, *property, "auto")) {
            uint32_t parsed_port;

            if (!i2s_to_u32(ctx, *property, &parsed_port) ||
                parsed_port >= I2S_LL_GET(INST_NUM)) {
                JS_ThrowTypeError(ctx, "i2s.open({ port }) expects \"auto\" or a valid port");
                goto parse_fail;
            }
            requested_port = (int)parsed_port;
        }
        if (!i2s_read_u32_property(ctx, argv[0], "sampleRateHz", &sample_rate) ||
            !i2s_read_u32_property(ctx, argv[0], "timeoutMs", &timeout_ms) ||
            sample_rate < 8000 || sample_rate > 192000) {
            JS_ThrowRangeError(ctx, "i2s.open() received an invalid sample rate or timeout");
            goto parse_fail;
        }
        *pins = JS_GetPropertyStr(ctx, argv[0], "pins");
        *dma = JS_GetPropertyStr(ctx, argv[0], "dma");
        if (JS_IsException(*pins) || JS_IsException(*dma) ||
            ((!JS_IsUndefined(*pins) && !JS_IsNull(*pins)) &&
             (JS_GetClassID(ctx, *pins) < 0 || JS_IsArray(ctx, *pins))) ||
            (mode == ESP32_MQUICKJS_I2S_MODE_STANDARD &&
             (JS_IsUndefined(*pins) || JS_IsNull(*pins)))) {
            JS_ThrowTypeError(
                ctx,
                "i2s.open() expects standard pins or a PDM hardware profile");
            goto parse_fail;
        }
        if (!JS_IsUndefined(*dma) &&
            (JS_GetClassID(ctx, *dma) < 0 || JS_IsArray(ctx, *dma))) {
            JS_ThrowTypeError(ctx, "i2s.open({ dma }) expects an object");
            goto parse_fail;
        }
        if (!JS_IsUndefined(*dma) &&
            (!i2s_read_u32_property(ctx, *dma, "descriptorCount", &dma_descriptors) ||
             !i2s_read_u32_property(ctx, *dma, "framesPerDescriptor", &dma_frames))) {
            JS_ThrowTypeError(ctx, "i2s.open({ dma }) expects integer fields");
            goto parse_fail;
        }
        if (mode == ESP32_MQUICKJS_I2S_MODE_PDM) {
            static const char *const standard_only[] = {
                "dataBits", "slotBits", "slotMode", "slotMask", "format",
            };
            size_t option_index;

            for (option_index = 0;
                 option_index < sizeof(standard_only) /
                                    sizeof(standard_only[0]);
                 ++option_index) {
                *property = JS_GetPropertyStr(ctx, argv[0],
                                              standard_only[option_index]);
                if (JS_IsException(*property)) {
                    goto parse_fail;
                }
                if (!JS_IsUndefined(*property)) {
                    JS_ThrowTypeError(
                        ctx, "i2s.open(pdm) does not accept standard-I2S option %s",
                        standard_only[option_index]);
                    goto parse_fail;
                }
            }
            if (!JS_IsUndefined(*pins) && !JS_IsNull(*pins)) {
                *property = JS_GetPropertyStr(ctx, *pins, "clk");
                if (JS_IsException(*property) ||
                    (!JS_IsUndefined(*property) &&
                     !i2s_to_gpio(ctx, *property, true, &pdm_clk))) {
                    JS_ThrowTypeError(ctx, "i2s.open({ pins.clk }) expects an output GPIO");
                    goto parse_fail;
                }
                *property = JS_GetPropertyStr(ctx, *pins, "din");
                if (JS_IsException(*property) ||
                    (!JS_IsUndefined(*property) &&
                     !i2s_to_gpio(ctx, *property, false, &pdm_din))) {
                    JS_ThrowTypeError(ctx, "i2s.open({ pins.din }) expects an input GPIO");
                    goto parse_fail;
                }
            }
        } else {
            if (!i2s_read_u32_property(ctx, argv[0], "dataBits", &data_bits) ||
                !i2s_read_u32_property(ctx, argv[0], "slotBits", &slot_bits)) {
                JS_ThrowTypeError(ctx, "i2s.open() expects integer dataBits/slotBits");
                goto parse_fail;
            }
            *property = JS_GetPropertyStr(ctx, argv[0], "slotMode");
            if (JS_IsException(*property)) {
                goto parse_fail;
            }
            if (!JS_IsUndefined(*property)) {
                if (i2s_string_equals(ctx, *property, "mono")) {
                    slot_mode = I2S_SLOT_MODE_MONO;
                } else if (i2s_string_equals(ctx, *property, "stereo")) {
                    slot_mode = I2S_SLOT_MODE_STEREO;
                } else {
                    JS_ThrowTypeError(ctx, "i2s.open({ slotMode }) expects \"mono\" or \"stereo\"");
                    goto parse_fail;
                }
            }
            *property = JS_GetPropertyStr(ctx, argv[0], "slotMask");
            if (JS_IsException(*property)) {
                goto parse_fail;
            }
            if (!JS_IsUndefined(*property)) {
                if (i2s_string_equals(ctx, *property, "left")) {
                    slot_mask = I2S_STD_SLOT_LEFT;
                } else if (i2s_string_equals(ctx, *property, "right")) {
                    slot_mask = I2S_STD_SLOT_RIGHT;
                } else if (i2s_string_equals(ctx, *property, "both")) {
                    slot_mask = I2S_STD_SLOT_BOTH;
                } else {
                    JS_ThrowTypeError(ctx, "i2s.open({ slotMask }) expects left/right/both");
                    goto parse_fail;
                }
            }
            *property = JS_GetPropertyStr(ctx, argv[0], "format");
            if (JS_IsException(*property)) {
                goto parse_fail;
            }
            if (!JS_IsUndefined(*property)) {
                text = JS_ToCString(ctx, *property, &text_buf);
                if (text == NULL ||
                    (strcmp(text, "philips") != 0 && strcmp(text, "msb") != 0 &&
                     strcmp(text, "pcmShort") != 0 && strcmp(text, "pcmLong") != 0)) {
                    JS_ThrowTypeError(ctx, "i2s.open({ format }) received an unsupported format");
                    goto parse_fail;
                }
                format = strcmp(text, "msb") == 0 ? "msb" :
                         strcmp(text, "pcmShort") == 0 ? "pcmShort" :
                         strcmp(text, "pcmLong") == 0 ? "pcmLong" : "philips";
            }
#define READ_STD_PIN(name, target, output) do { \
    *property = JS_GetPropertyStr(ctx, *pins, name); \
    if (JS_IsException(*property) || !i2s_to_gpio(ctx, *property, output, target)) { \
        JS_ThrowTypeError(ctx, "i2s.open({ pins.%s }) expects a valid GPIO", name); \
        goto parse_fail; \
    } \
} while (0)
            READ_STD_PIN("bclk", &bclk, true);
            READ_STD_PIN("ws", &ws, true);
            READ_STD_PIN("din", &din, false);
#undef READ_STD_PIN
            *property = JS_GetPropertyStr(ctx, *pins, "mclk");
            if (JS_IsException(*property) ||
                (!JS_IsUndefined(*property) &&
                 !i2s_to_gpio(ctx, *property, true, &mclk))) {
                JS_ThrowTypeError(ctx, "i2s.open({ pins.mclk }) expects an output GPIO");
                goto parse_fail;
            }
        }
        JS_PopGCRef(ctx, &dma_ref);
        JS_PopGCRef(ctx, &pins_ref);
        JS_PopGCRef(ctx, &property_ref);
        goto parsed;

parse_fail:
        JS_PopGCRef(ctx, &dma_ref);
        JS_PopGCRef(ctx, &pins_ref);
        JS_PopGCRef(ctx, &property_ref);
        return JS_EXCEPTION;
    }

parsed:
    if (mode == ESP32_MQUICKJS_I2S_MODE_PDM) {
        if (pdm_clk >= 0 && pdm_clk == pdm_din) {
            return JS_ThrowRangeError(
                ctx, "i2s.open(pdm) requires distinct clk and din GPIOs");
        }
#if !(SOC_I2S_SUPPORTS_PDM_RX && SOC_I2S_SUPPORTS_PDM2PCM)
        return JS_ThrowInternalError(ctx,
                                     "PDM-to-PCM receive is unavailable on this target");
#else
        data_bits = 16;
        slot_bits = 16;
        slot_mode = I2S_SLOT_MODE_MONO;
        if (pdm_clk < 0 || pdm_din < 0) {
            return JS_ThrowTypeError(ctx,
                                     "i2s.open(pdm) requires pins.clk and pins.din or a hardware profile");
        }
        if (requested_port != I2S_NUM_AUTO && requested_port != I2S_NUM_0) {
            return JS_ThrowRangeError(ctx, "PDM RX is available only on I2S port 0");
        }
#endif
    } else {
        if (!((data_bits == 8 || data_bits == 16 || data_bits == 24 ||
               data_bits == 32) &&
              (slot_bits == 8 || slot_bits == 16 || slot_bits == 24 ||
               slot_bits == 32) && data_bits <= slot_bits)) {
            return JS_ThrowRangeError(
                ctx,
                "standard I2S dataBits/slotBits must be 8, 16, 24, or 32 with dataBits <= slotBits");
        }
        if (bclk == ws || bclk == din || ws == din ||
            (mclk != I2S_GPIO_UNUSED &&
             (mclk == bclk || mclk == ws || mclk == din))) {
            return JS_ThrowRangeError(
                ctx, "i2s.open(standard) requires distinct GPIOs");
        }
    }
    if (dma_descriptors < 2 || dma_descriptors > 16 || dma_frames == 0 ||
        dma_frames > I2S_DMA_MAX_DESCRIPTOR_BYTES /
                         (((size_t)slot_bits / 8U) *
                          (slot_mode == I2S_SLOT_MODE_MONO ? 1U : 2U))) {
        return JS_ThrowRangeError(ctx,
                                  "i2s.open({ dma }) exceeds the 4092-byte descriptor limit");
    }
    if (mode == ESP32_MQUICKJS_I2S_MODE_PDM) {
        requested_port = I2S_NUM_0;
    }
    if (requested_port == I2S_NUM_AUTO) {
        for (i = 0; i < I2S_LL_GET(INST_NUM); ++i) {
            if (!s_i2s_slots[i].allocated &&
                esp32_mquickjs_peripheral_lease_acquire(
                    ESP32_MQUICKJS_PERIPHERAL_I2S_PORT, i,
                    ESP32_MQUICKJS_PERIPHERAL_OWNER_I2S,
                    &s_i2s_slots[i].lease)) {
                slot = &s_i2s_slots[i];
                break;
            }
        }
    } else if (!s_i2s_slots[requested_port].allocated &&
               esp32_mquickjs_peripheral_lease_acquire(
                   ESP32_MQUICKJS_PERIPHERAL_I2S_PORT, requested_port,
                   ESP32_MQUICKJS_PERIPHERAL_OWNER_I2S,
                   &s_i2s_slots[requested_port].lease)) {
        slot = &s_i2s_slots[requested_port];
    }
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "i2s.open() found no available I2S port");
    }
    slot->allocated = true;
    slot->generation = i2s_take_generation();
    slot->mode = mode;
    slot->sample_rate_hz = sample_rate;
    slot->data_bits = (uint8_t)data_bits;
    slot->slot_bits = (uint8_t)slot_bits;
    slot->channels = slot_mode == I2S_SLOT_MODE_MONO ? 1 : 2;
    slot->dma_descriptor_count = dma_descriptors;
    slot->dma_frames_per_descriptor = dma_frames;
    slot->timeout_ms = timeout_ms;
    channel_config.id = slot->port;
    channel_config.dma_desc_num = dma_descriptors;
    channel_config.dma_frame_num = dma_frames;
    err = i2s_new_channel(&channel_config, NULL, &slot->rx_handle);
    if (err != ESP_OK) {
        i2s_cleanup_slot(slot);
        return JS_ThrowInternalError(ctx, "i2s.open() failed to allocate channel: %s",
                                     esp_err_to_name(err));
    }
    if (mode == ESP32_MQUICKJS_I2S_MODE_STANDARD) {
        i2s_std_config_t config = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                (i2s_data_bit_width_t)data_bits, slot_mode),
            .gpio_cfg = {
                .mclk = (gpio_num_t)mclk,
                .bclk = (gpio_num_t)bclk,
                .ws = (gpio_num_t)ws,
                .dout = I2S_GPIO_UNUSED,
                .din = (gpio_num_t)din,
            },
        };

        if (strcmp(format, "msb") == 0) {
            config.slot_cfg = (i2s_std_slot_config_t)
                I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                    (i2s_data_bit_width_t)data_bits, slot_mode);
        } else if (strcmp(format, "pcmShort") == 0 ||
                   strcmp(format, "pcmLong") == 0) {
            config.slot_cfg = (i2s_std_slot_config_t)
                I2S_STD_PCM_SLOT_DEFAULT_CONFIG(
                    (i2s_data_bit_width_t)data_bits, slot_mode);
            if (strcmp(format, "pcmLong") == 0) {
                config.slot_cfg.ws_width = slot_bits;
            }
        }
        config.slot_cfg.slot_bit_width = (i2s_slot_bit_width_t)slot_bits;
        config.slot_cfg.slot_mask = slot_mask;
        err = i2s_channel_init_std_mode(slot->rx_handle, &config);
    } else {
#if SOC_I2S_SUPPORTS_PDM_RX && SOC_I2S_SUPPORTS_PDM2PCM
        i2s_pdm_rx_config_t config = {
            .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .clk = (gpio_num_t)pdm_clk,
                .din = (gpio_num_t)pdm_din,
            },
        };
        err = i2s_channel_init_pdm_rx_mode(slot->rx_handle, &config);
#else
        err = ESP_ERR_NOT_SUPPORTED;
#endif
    }
    if (err == ESP_OK) {
        err = i2s_channel_register_event_callback(slot->rx_handle,
                                                  &callbacks, slot);
    }
    if (err != ESP_OK) {
        i2s_cleanup_slot(slot);
        return JS_ThrowInternalError(ctx, "i2s.open() failed to initialize RX: %s",
                                     esp_err_to_name(err));
    }
    result = i2s_make_input(ctx, slot);
    if (JS_IsException(result)) {
        i2s_cleanup_slot(slot);
    }
    return result;
}

#endif
