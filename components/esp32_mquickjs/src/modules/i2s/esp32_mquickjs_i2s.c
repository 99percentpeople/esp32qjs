#include "esp32_mquickjs_i2s.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_I2S

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_i2s_channel_resources.h"
#include "esp32_mquickjs_i2s_timer_resources.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_peripheral_lease.h"
#include "utils/esp32_mquickjs_byte_source.h"

#include <math.h>
#include <stdbool.h>
#include <stdatomic.h>
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
#define I2S_MAX_WRITE_BYTES (64U * 1024U)
#define I2S_DMA_MAX_DESCRIPTOR_BYTES 4092U
#define I2S_DEFAULT_SAMPLE_RATE_HZ 16000U
#define I2S_DEFAULT_TIMEOUT_MS 1000U
#define I2S_DEFAULT_DMA_DESCRIPTORS 6U
#define I2S_DEFAULT_DMA_FRAMES 240U

typedef enum {
    ESP32_MQUICKJS_I2S_MODE_STANDARD,
    ESP32_MQUICKJS_I2S_MODE_PDM,
} esp32_mquickjs_i2s_mode_t;

typedef enum {
    ESP32_MQUICKJS_I2S_DIRECTION_RX = 1,
    ESP32_MQUICKJS_I2S_DIRECTION_TX = 2,
    ESP32_MQUICKJS_I2S_DIRECTION_DUPLEX = 3,
} esp32_mquickjs_i2s_direction_t;

typedef struct {
    int32_t port;
    uint32_t generation;
} esp32_mquickjs_i2s_ref_t;

typedef struct {
    bool allocated;
    bool running;
    bool rx_enabled;
    bool tx_enabled;
    bool rx_busy;
    bool tx_busy;
    bool rx_cancel_requested;
    bool tx_cancel_requested;
    bool release_pending;
    uint32_t future_reservations;
    uint8_t rx_lane_key;
    uint8_t tx_lane_key;
    int32_t port;
    uint32_t generation;
    esp32_mquickjs_i2s_mode_t mode;
    esp32_mquickjs_i2s_direction_t direction;
    uint32_t sample_rate_hz;
    uint8_t data_bits;
    uint8_t slot_bits;
    uint8_t channels;
    uint32_t dma_descriptor_count;
    uint32_t dma_frames_per_descriptor;
    size_t dma_buffer_bytes;
    size_t dma_total_buffer_bytes;
    uint32_t timeout_ms;
    _Atomic uint32_t overruns;
    _Atomic uint32_t send_queue_overflows;
    uint32_t sequence;
    i2s_chan_handle_t rx_handle;
    i2s_chan_handle_t tx_handle;
    esp_timer_handle_t rx_timeout_timer;
    esp_timer_handle_t tx_timeout_timer;
    esp32_mquickjs_runtime_t *rx_runtime;
    esp32_mquickjs_runtime_t *tx_runtime;
    esp32_mquickjs_future_token_t rx_token;
    esp32_mquickjs_future_token_t tx_token;
    esp32_mquickjs_memory_dma_reservation_t dma_reservation;
    esp32_mquickjs_peripheral_lease_t lease;
} esp32_mquickjs_i2s_slot_t;

static esp32_mquickjs_i2s_slot_t s_i2s_slots[I2S_LL_GET(INST_NUM)];
static uint32_t s_i2s_next_generation = 1;
/* Publishes each callback's busy/runtime/token wake target as one snapshot. */
static portMUX_TYPE s_i2s_callback_lock = portMUX_INITIALIZER_UNLOCKED;

static void i2s_rx_timeout(void *opaque);
static void i2s_tx_timeout(void *opaque);

typedef struct {
    const i2s_chan_config_t *config;
} i2s_channel_resource_context_t;

static int i2s_channel_resource_create(void *opaque, void **out_tx_channel,
                                       void **out_rx_channel)
{
    i2s_channel_resource_context_t *context = opaque;
    i2s_chan_handle_t tx_channel = NULL;
    i2s_chan_handle_t rx_channel = NULL;
    esp_err_t err;

    if (context == NULL || context->config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = i2s_new_channel(context->config,
                          out_tx_channel != NULL ? &tx_channel : NULL,
                          out_rx_channel != NULL ? &rx_channel : NULL);
    if (out_tx_channel != NULL) {
        *out_tx_channel = tx_channel;
    }
    if (out_rx_channel != NULL) {
        *out_rx_channel = rx_channel;
    }
    return err;
}

static int i2s_channel_resource_enable(void *channel, void *opaque)
{
    (void)opaque;
    return i2s_channel_enable((i2s_chan_handle_t)channel);
}

static int i2s_channel_resource_disable(void *channel, void *opaque)
{
    (void)opaque;
    return i2s_channel_disable((i2s_chan_handle_t)channel);
}

static int i2s_channel_resource_delete(void *channel, void *opaque)
{
    (void)opaque;
    return i2s_del_channel((i2s_chan_handle_t)channel);
}

static esp32_mquickjs_i2s_channel_resource_ops_t i2s_channel_resource_ops(
    i2s_channel_resource_context_t *context)
{
    return (esp32_mquickjs_i2s_channel_resource_ops_t){
        .create_channels = i2s_channel_resource_create,
        .enable_channel = i2s_channel_resource_enable,
        .disable_channel = i2s_channel_resource_disable,
        .delete_channel = i2s_channel_resource_delete,
        .opaque = context,
    };
}

static int i2s_create_rx_timeout_timer(void *opaque, void **out_timer)
{
    esp_timer_create_args_t timer_args = {
        .callback = i2s_rx_timeout,
        .arg = opaque,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mqjs_i2s_rx",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t timer = NULL;
    esp_err_t err = esp_timer_create(&timer_args, &timer);

    *out_timer = timer;
    return err;
}

static int i2s_create_tx_timeout_timer(void *opaque, void **out_timer)
{
    esp_timer_create_args_t timer_args = {
        .callback = i2s_tx_timeout,
        .arg = opaque,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mqjs_i2s_tx",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t timer = NULL;
    esp_err_t err = esp_timer_create(&timer_args, &timer);

    *out_timer = timer;
    return err;
}

static void i2s_delete_timeout_timer(void *timer, void *opaque)
{
    (void)opaque;
    (void)esp_timer_stop((esp_timer_handle_t)timer);
    (void)esp_timer_delete((esp_timer_handle_t)timer);
}

static esp32_mquickjs_i2s_timer_resource_ops_t i2s_timer_resource_ops(
    esp32_mquickjs_i2s_slot_t *slot)
{
    return (esp32_mquickjs_i2s_timer_resource_ops_t){
        .create_rx = i2s_create_rx_timeout_timer,
        .create_tx = i2s_create_tx_timeout_timer,
        .delete_timer = i2s_delete_timeout_timer,
        .opaque = slot,
    };
}

struct esp32_mquickjs_future_driver_state {
    JSContext *ctx;
    JSGCRef owner_ref;
    esp32_mquickjs_i2s_ref_t channel_ref;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    uint8_t *data;
    size_t requested_bytes;
    size_t transferred_bytes;
    uint32_t frame_count;
    uint32_t timeout_ms;
    uint64_t deadline_us;
    esp_err_t err;
    bool owner_retained;
    bool reservation_held;
    bool write_operation;
    bool started;
    bool completed;
    bool cancelled;
    bool timed_out;
};

static bool i2s_has_rx(const esp32_mquickjs_i2s_slot_t *slot)
{
    return slot != NULL &&
           (slot->direction & ESP32_MQUICKJS_I2S_DIRECTION_RX) != 0;
}

static bool i2s_has_tx(const esp32_mquickjs_i2s_slot_t *slot)
{
    return slot != NULL &&
           (slot->direction & ESP32_MQUICKJS_I2S_DIRECTION_TX) != 0;
}

static bool i2s_slot_busy(const esp32_mquickjs_i2s_slot_t *slot)
{
    return slot != NULL && (slot->rx_busy || slot->tx_busy);
}

static uint32_t i2s_take_generation(void)
{
    uint32_t generation = s_i2s_next_generation++;

    if (generation == 0) {
        generation = s_i2s_next_generation++;
    }
    return generation;
}

static void i2s_reset_slot(esp32_mquickjs_i2s_slot_t *slot, int32_t port)
{
    portENTER_CRITICAL(&s_i2s_callback_lock);
    memset(slot, 0, sizeof(*slot));
    atomic_init(&slot->overruns, 0);
    atomic_init(&slot->send_queue_overflows, 0);
    slot->port = port;
    portEXIT_CRITICAL(&s_i2s_callback_lock);
}

static void i2s_publish_rx_wake_target(
    esp32_mquickjs_i2s_slot_t *slot,
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token)
{
    portENTER_CRITICAL(&s_i2s_callback_lock);
    slot->rx_runtime = runtime;
    slot->rx_token = token;
    slot->rx_busy = true;
    portEXIT_CRITICAL(&s_i2s_callback_lock);
}

static void i2s_publish_tx_wake_target(
    esp32_mquickjs_i2s_slot_t *slot,
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token)
{
    portENTER_CRITICAL(&s_i2s_callback_lock);
    slot->tx_runtime = runtime;
    slot->tx_token = token;
    slot->tx_busy = true;
    portEXIT_CRITICAL(&s_i2s_callback_lock);
}

static void i2s_clear_rx_wake_target(esp32_mquickjs_i2s_slot_t *slot)
{
    portENTER_CRITICAL(&s_i2s_callback_lock);
    slot->rx_busy = false;
    slot->rx_runtime = NULL;
    slot->rx_token = (esp32_mquickjs_future_token_t){0};
    portEXIT_CRITICAL(&s_i2s_callback_lock);
}

static void i2s_clear_tx_wake_target(esp32_mquickjs_i2s_slot_t *slot)
{
    portENTER_CRITICAL(&s_i2s_callback_lock);
    slot->tx_busy = false;
    slot->tx_runtime = NULL;
    slot->tx_token = (esp32_mquickjs_future_token_t){0};
    portEXIT_CRITICAL(&s_i2s_callback_lock);
}

static void i2s_wake_rx(esp32_mquickjs_i2s_slot_t *slot)
{
    esp32_mquickjs_runtime_t *runtime = NULL;
    esp32_mquickjs_future_token_t token = {0};

    portENTER_CRITICAL(&s_i2s_callback_lock);
    if (slot != NULL && slot->rx_busy && slot->rx_runtime != NULL) {
        runtime = slot->rx_runtime;
        token = slot->rx_token;
    }
    portEXIT_CRITICAL(&s_i2s_callback_lock);
    if (runtime != NULL) {
        (void)esp32_mquickjs_future_wake(runtime, token);
    }
}

static void i2s_wake_tx(esp32_mquickjs_i2s_slot_t *slot)
{
    esp32_mquickjs_runtime_t *runtime = NULL;
    esp32_mquickjs_future_token_t token = {0};

    portENTER_CRITICAL(&s_i2s_callback_lock);
    if (slot != NULL && slot->tx_busy && slot->tx_runtime != NULL) {
        runtime = slot->tx_runtime;
        token = slot->tx_token;
    }
    portEXIT_CRITICAL(&s_i2s_callback_lock);
    if (runtime != NULL) {
        (void)esp32_mquickjs_future_wake(runtime, token);
    }
}

static bool IRAM_ATTR i2s_wake_rx_from_isr(
    esp32_mquickjs_i2s_slot_t *slot, int *task_woken)
{
    esp32_mquickjs_runtime_t *runtime = NULL;
    esp32_mquickjs_future_token_t token = {0};

    portENTER_CRITICAL_ISR(&s_i2s_callback_lock);
    if (slot != NULL && slot->rx_busy && slot->rx_runtime != NULL) {
        runtime = slot->rx_runtime;
        token = slot->rx_token;
    }
    portEXIT_CRITICAL_ISR(&s_i2s_callback_lock);
    return runtime != NULL &&
           esp32_mquickjs_future_wake_from_isr(runtime, token, task_woken);
}

static bool IRAM_ATTR i2s_wake_tx_from_isr(
    esp32_mquickjs_i2s_slot_t *slot, int *task_woken)
{
    esp32_mquickjs_runtime_t *runtime = NULL;
    esp32_mquickjs_future_token_t token = {0};

    portENTER_CRITICAL_ISR(&s_i2s_callback_lock);
    if (slot != NULL && slot->tx_busy && slot->tx_runtime != NULL) {
        runtime = slot->tx_runtime;
        token = slot->tx_token;
    }
    portEXIT_CRITICAL_ISR(&s_i2s_callback_lock);
    return runtime != NULL &&
           esp32_mquickjs_future_wake_from_isr(runtime, token, task_woken);
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

static void *i2s_operation_buffer_malloc(size_t size)
{
    return esp32_mquickjs_memory_payload_alloc(
        "i2s.operation", size, ESP32_MQUICKJS_MEMORY_EXTERNAL);
}

static void *i2s_operation_buffer_realloc(void *buffer, size_t size)
{
    return esp32_mquickjs_memory_payload_realloc(
        "i2s.operation", buffer, size,
        ESP32_MQUICKJS_MEMORY_EXTERNAL);
}

static JSValue i2s_throw_no_memory(JSContext *ctx, const char *operation)
{
    return JS_ThrowInternalError(ctx, "%s failed: I2S_NO_MEMORY",
                                 operation);
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

static esp32_mquickjs_i2s_channel_resources_t i2s_channel_resources_from_slot(
    const esp32_mquickjs_i2s_slot_t *slot)
{
    return (esp32_mquickjs_i2s_channel_resources_t){
        .rx_channel = slot->rx_handle,
        .tx_channel = slot->tx_handle,
        .rx_enabled = slot->rx_enabled,
        .tx_enabled = slot->tx_enabled,
    };
}

static void i2s_store_channel_resources(
    esp32_mquickjs_i2s_slot_t *slot,
    const esp32_mquickjs_i2s_channel_resources_t *resources)
{
    slot->rx_handle = (i2s_chan_handle_t)resources->rx_channel;
    slot->tx_handle = (i2s_chan_handle_t)resources->tx_channel;
    slot->rx_enabled = resources->rx_enabled;
    slot->tx_enabled = resources->tx_enabled;
}

static bool i2s_channels_fully_enabled(
    const esp32_mquickjs_i2s_slot_t *slot)
{
    bool has_channel = slot->rx_handle != NULL || slot->tx_handle != NULL;

    return has_channel &&
           (slot->rx_handle == NULL || slot->rx_enabled) &&
           (slot->tx_handle == NULL || slot->tx_enabled);
}

static esp_err_t i2s_cleanup_slot(esp32_mquickjs_i2s_slot_t *slot)
{
    i2s_channel_resource_context_t channel_context = {0};
    esp32_mquickjs_i2s_channel_resources_t channel_resources;
    esp32_mquickjs_i2s_channel_resource_ops_t channel_ops;
    esp32_mquickjs_i2s_timer_resources_t timer_resources;
    esp32_mquickjs_i2s_timer_resource_ops_t timer_ops;
    esp_err_t err;
    int32_t port;

    if (slot == NULL || !slot->allocated) {
        return ESP_OK;
    }
    if (i2s_slot_busy(slot) || slot->future_reservations > 0) {
        return ESP_ERR_INVALID_STATE;
    }
    port = slot->port;
    channel_resources = i2s_channel_resources_from_slot(slot);
    channel_ops = i2s_channel_resource_ops(&channel_context);
    err = (esp_err_t)esp32_mquickjs_i2s_channel_resources_stop(
        &channel_resources, &channel_ops);
    i2s_store_channel_resources(slot, &channel_resources);
    slot->running = i2s_channels_fully_enabled(slot);
    if (err != ESP_OK) {
        slot->release_pending = true;
        return err;
    }
    timer_resources = (esp32_mquickjs_i2s_timer_resources_t){
        .rx_timer = slot->rx_timeout_timer,
        .tx_timer = slot->tx_timeout_timer,
    };
    timer_ops = i2s_timer_resource_ops(slot);
    slot->rx_timeout_timer = NULL;
    slot->tx_timeout_timer = NULL;
    esp32_mquickjs_i2s_timer_resources_deinit(
        &timer_resources, &timer_ops);
    err = (esp_err_t)esp32_mquickjs_i2s_channel_resources_delete(
        &channel_resources, &channel_ops);
    i2s_store_channel_resources(slot, &channel_resources);
    if (err != ESP_OK) {
        slot->release_pending = true;
        return err;
    }
    if (slot->dma_reservation.state != ESP32_MQUICKJS_MEMORY_DMA_IDLE &&
        !esp32_mquickjs_memory_release_driver_pinned(
            &slot->dma_reservation)) {
        slot->release_pending = true;
        return ESP_ERR_INVALID_STATE;
    }
    esp32_mquickjs_peripheral_lease_release(&slot->lease);
    i2s_reset_slot(slot, port);
    return ESP_OK;
}

static void i2s_request_close(esp32_mquickjs_i2s_slot_t *slot)
{
    if (slot == NULL || !slot->allocated) {
        return;
    }
    slot->release_pending = true;
    if (slot->rx_busy) {
        slot->rx_cancel_requested = true;
        i2s_wake_rx(slot);
    }
    if (slot->tx_busy) {
        slot->tx_cancel_requested = true;
        i2s_wake_tx(slot);
    }
    if (!i2s_slot_busy(slot) && slot->future_reservations == 0) {
        (void)i2s_cleanup_slot(slot);
    }
}

static int i2s_ref_from_value(JSContext *ctx, JSValue value,
                              const char *api_name,
                              esp32_mquickjs_i2s_ref_t *out_ref,
                              esp32_mquickjs_i2s_slot_t **out_slot)
{
    esp32_mquickjs_i2s_ref_t *ref;
    esp32_mquickjs_i2s_slot_t *slot;

    if (JS_GetClassID(ctx, value) != JS_CLASS_I2S_CHANNEL ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "%s expects an I2SChannel", api_name);
        return -1;
    }
    slot = i2s_get_slot(ref);
    if (slot == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the I2S channel is closed",
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

static JSValue i2s_make_channel(JSContext *ctx,
                              const esp32_mquickjs_i2s_slot_t *slot)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    esp32_mquickjs_i2s_ref_t *ref;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_I2S_CHANNEL);
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
    (void)i2s_wake_rx_from_isr(slot, &task_woken);
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
        /* Counters do not publish any associated state. */
        atomic_fetch_add_explicit(&slot->overruns, 1, memory_order_relaxed);
        (void)i2s_wake_rx_from_isr(slot, &task_woken);
    }
    return task_woken == pdTRUE;
}

static bool IRAM_ATTR i2s_on_sent(i2s_chan_handle_t handle,
                                  i2s_event_data_t *event,
                                  void *user_ctx)
{
    esp32_mquickjs_i2s_slot_t *slot = user_ctx;
    int task_woken = pdFALSE;

    (void)handle;
    (void)event;
    (void)i2s_wake_tx_from_isr(slot, &task_woken);
    return task_woken == pdTRUE;
}

static bool IRAM_ATTR i2s_on_send_queue_overflow(i2s_chan_handle_t handle,
                                                 i2s_event_data_t *event,
                                                 void *user_ctx)
{
    esp32_mquickjs_i2s_slot_t *slot = user_ctx;
    int task_woken = pdFALSE;

    (void)handle;
    (void)event;
    if (slot != NULL) {
        /* Counters do not publish any associated state. */
        atomic_fetch_add_explicit(&slot->send_queue_overflows, 1,
                                  memory_order_relaxed);
        (void)i2s_wake_tx_from_isr(slot, &task_woken);
    }
    return task_woken == pdTRUE;
}

static void i2s_rx_timeout(void *opaque)
{
    esp32_mquickjs_i2s_slot_t *slot = opaque;

    i2s_wake_rx(slot);
}

static void i2s_tx_timeout(void *opaque)
{
    esp32_mquickjs_i2s_slot_t *slot = opaque;

    i2s_wake_tx(slot);
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
            ctx, result, "direction",
            JS_NewString(
                ctx,
                slot->direction == ESP32_MQUICKJS_I2S_DIRECTION_DUPLEX
                    ? "duplex"
                    : slot->direction == ESP32_MQUICKJS_I2S_DIRECTION_TX
                          ? "tx"
                          : "rx")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "mode",
            JS_NewString(ctx, slot->mode == ESP32_MQUICKJS_I2S_MODE_PDM
                                  ? "pdm" : "standard")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "overruns",
                                         JS_NewUint32(
                                             ctx, atomic_load_explicit(
                                                      &slot->overruns,
                                                      memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sendQueueOverflows",
            JS_NewUint32(
                ctx, atomic_load_explicit(&slot->send_queue_overflows,
                                          memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "readBusy",
                                         JS_NewBool(slot->rx_busy)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "writeBusy",
                                         JS_NewBool(slot->tx_busy)) ||
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
        !esp32_mquickjs_set_property_ref(ctx, dma, "storage",
                                         JS_NewString(ctx, "internal")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, dma, "bufferBytes",
            JS_NewUint32(ctx, (uint32_t)slot->dma_buffer_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, dma, "totalBufferBytes",
            JS_NewUint32(ctx, (uint32_t)slot->dma_total_buffer_bytes)) ||
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
                          "I2SChannel.read(frameCount, timeoutMs?) expects a positive frame count");
        return false;
    }
    if (i2s_ref_from_value(ctx, this_ref->val, "I2SChannel.read()", NULL,
                           &slot) != 0) {
        return false;
    }
    timeout_ms = slot->timeout_ms;
    if (argc == 2 && !JS_IsUndefined(argv[1].val) &&
        !i2s_to_u32(ctx, argv[1].val, &timeout_ms)) {
        JS_ThrowTypeError(ctx,
                          "I2SChannel.read(frameCount, timeoutMs?) expects a non-negative timeout");
        return false;
    }
    if (!i2s_has_rx(slot)) {
        JS_ThrowTypeError(ctx,
                          "I2SChannel.read() requires an rx or duplex channel");
        return false;
    }
    if (!slot->running) {
        JS_ThrowInternalError(ctx, "I2SChannel.read() requires start() first");
        return false;
    }
    bytes_per_frame = ((size_t)slot->slot_bits / 8U) * slot->channels;
    if (frame_count > I2S_MAX_READ_BYTES / bytes_per_frame) {
        JS_ThrowRangeError(ctx,
                           "I2SChannel.read() output exceeds the 64 KiB limit");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->data = i2s_operation_buffer_malloc(
        (size_t)frame_count * bytes_per_frame);
    if (state->data == NULL) {
        heap_caps_free(state);
        i2s_throw_no_memory(ctx, "I2SChannel.read()");
        return false;
    }
    state->ctx = ctx;
    state->channel_ref.port = slot->port;
    state->channel_ref.generation = slot->generation;
    state->requested_bytes = (size_t)frame_count * bytes_per_frame;
    state->frame_count = frame_count;
    state->timeout_ms = timeout_ms;
    slot->future_reservations++;
    state->reservation_held = true;
    owner = JS_AddGCRef(ctx, &state->owner_ref);
    *owner = this_ref->val;
    state->owner_retained = true;
    *out_state = state;
    return true;
}

static void i2s_read_step(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2s_slot_t *slot =
        state != NULL ? i2s_get_slot(&state->channel_ref) : NULL;
    size_t read_bytes = 0;

    if (state == NULL || state->completed || state->cancelled) {
        return;
    }
    if (slot != NULL && slot->rx_cancel_requested) {
        state->cancelled = true;
        state->completed = true;
        return;
    }
    if (slot == NULL || !slot->running) {
        state->err = ESP_ERR_INVALID_STATE;
        state->completed = true;
        return;
    }
    state->err = i2s_channel_read(
        slot->rx_handle, state->data + state->transferred_bytes,
        state->requested_bytes - state->transferred_bytes, &read_bytes, 0);
    /*
     * ESP-IDF reports ESP_ERR_TIMEOUT when a zero-wait read consumes the
     * currently available DMA bytes but cannot fill the complete request.
     * Those partial bytes are valid and must be retained for the next wake;
     * dropping them makes reads larger than one DMA descriptor eventually
     * time out while continuously draining the driver's queue.
     */
    if (state->err == ESP_OK || state->err == ESP_ERR_TIMEOUT) {
        state->transferred_bytes += read_bytes;
        if (state->transferred_bytes >= state->requested_bytes) {
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
        state->timed_out = state->transferred_bytes == 0;
        state->completed = true;
    }
}

static bool i2s_read_start(JSContext *ctx,
                           esp32_mquickjs_runtime_t *runtime,
                           esp32_mquickjs_future_token_t token,
                           esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2s_slot_t *slot =
        state != NULL ? i2s_get_slot(&state->channel_ref) : NULL;

    if (state == NULL || slot == NULL || !slot->running) {
        JS_ThrowReferenceError(ctx, "I2S channel closed before read started");
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    if (slot->release_pending) {
        state->cancelled = true;
        state->completed = true;
        (void)esp32_mquickjs_future_wake(runtime, token);
        return true;
    }
    if (slot->rx_busy) {
        JS_ThrowInternalError(ctx, "I2S channel already has a pending read");
        return false;
    }
    slot->rx_cancel_requested = false;
    i2s_publish_rx_wake_target(slot, runtime, token);
    state->started = true;
    if (state->timeout_ms > 0) {
        state->deadline_us = (uint64_t)esp_timer_get_time() +
                             (uint64_t)state->timeout_ms * 1000ULL;
        if (esp_timer_start_once(slot->rx_timeout_timer,
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
        state != NULL ? i2s_get_slot(&state->channel_ref) : NULL;
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
        return JS_ThrowInternalError(ctx, "I2SChannel.read() failed: %s",
                                     esp_err_to_name(state->err));
    }
    if (state->timed_out || state->transferred_bytes == 0) {
        return JS_NULL;
    }
    if (slot == NULL) {
        return JS_ThrowReferenceError(ctx, "I2S channel closed during read");
    }
    bytes_per_frame = ((size_t)slot->slot_bits / 8U) * slot->channels;
    frames = (uint32_t)(state->transferred_bytes / bytes_per_frame);
    state->transferred_bytes = (size_t)frames * bytes_per_frame;
    result = JS_PushGCRef(ctx, &result_ref);
    data = JS_PushGCRef(ctx, &data_ref);
    *result = JS_NewObject(ctx);
    *data = JS_IsException(*result)
                ? JS_EXCEPTION
                : esp32_mquickjs_new_owned_byte_view(
                      ctx, state->data, state->transferred_bytes);
    if (!JS_IsException(*data)) {
        state->data = NULL;
    }
    if (JS_IsException(*result) || JS_IsException(*data) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "data", *data) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "frames",
                                         JS_NewUint32(ctx, frames)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "byteLength",
            JS_NewInt64(ctx, (int64_t)state->transferred_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "timestampUs",
            JS_NewInt64(ctx, (int64_t)esp_timer_get_time())) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sequence", JS_NewUint32(ctx, slot->sequence++)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "overruns",
            JS_NewUint32(ctx, atomic_load_explicit(&slot->overruns,
                                                   memory_order_relaxed)))) {
        JS_PopGCRef(ctx, &data_ref);
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    *data = JS_UNDEFINED;
    JS_PopGCRef(ctx, &data_ref);
    return JS_PopGCRef(ctx, &result_ref);
}

static esp32_mquickjs_cancel_result_t i2s_read_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed || state->cancelled) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancelled = true;
    state->completed = true;
    if (state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
    return ESP32_MQUICKJS_CANCELLED;
}

static void i2s_read_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2s_slot_t *slot =
        state != NULL ? i2s_get_slot(&state->channel_ref) : NULL;

    if (state == NULL) {
        return;
    }
    if (slot != NULL && state->started) {
        if (slot->rx_timeout_timer != NULL) {
            (void)esp_timer_stop(slot->rx_timeout_timer);
        }
        i2s_clear_rx_wake_target(slot);
        slot->rx_cancel_requested = false;
    }
    if (state->owner_retained) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
    }
    esp32_mquickjs_memory_payload_free(state->data);
    if (slot != NULL && state->reservation_held) {
        if (slot->future_reservations > 0) {
            slot->future_reservations--;
        }
        if (slot->release_pending && slot->future_reservations == 0 &&
            !i2s_slot_busy(slot)) {
            i2s_cleanup_slot(slot);
        }
    }
    heap_caps_free(state);
}

static esp32_mquickjs_resource_key_t i2s_future_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2s_slot_t *slot = state != NULL
        ? i2s_get_slot(&state->channel_ref) : NULL;

    if (slot == NULL) {
        return NULL;
    }
    return state->write_operation
               ? (esp32_mquickjs_resource_key_t)&slot->tx_lane_key
               : (esp32_mquickjs_resource_key_t)&slot->rx_lane_key;
}

static const esp32_mquickjs_future_driver_t s_i2s_read_driver = {
    .capture = i2s_read_prepare,
    .start = i2s_read_start,
    .poll = i2s_read_poll,
    .finish = i2s_read_finish,
    .cancel = i2s_read_cancel,
    .destroy = i2s_read_destroy,
    .resource_key = i2s_future_resource_key,
};

static bool i2s_append_write_bytes(JSContext *ctx, uint8_t **buffer,
                                   size_t *length, size_t *capacity,
                                   const uint8_t *data, size_t data_length)
{
    size_t required;
    size_t next_capacity;
    uint8_t *next;

    if (data_length == 0) {
        return true;
    }
    if (data == NULL || *length > I2S_MAX_WRITE_BYTES - data_length) {
        JS_ThrowRangeError(ctx,
                           "I2SChannel.write() exceeds the 64 KiB limit");
        return false;
    }
    required = *length + data_length;
    if (required > *capacity) {
        next_capacity = *capacity == 0 ? 256U : *capacity;
        while (next_capacity < required) {
            if (next_capacity >= I2S_MAX_WRITE_BYTES / 2U) {
                next_capacity = I2S_MAX_WRITE_BYTES;
                break;
            }
            next_capacity *= 2U;
        }
        next = i2s_operation_buffer_realloc(*buffer, next_capacity);
        if (next == NULL) {
            i2s_throw_no_memory(ctx, "I2SChannel.write()");
            return false;
        }
        *buffer = next;
        *capacity = next_capacity;
    }
    memcpy(*buffer + *length, data, data_length);
    *length = required;
    return true;
}

static bool i2s_load_write_data(JSContext *ctx, JSValue value,
                                uint8_t **out_data, size_t *out_length)
{
    int class_id = JS_GetClassID(ctx, value);
    uint8_t *data = NULL;
    size_t length = 0;
    size_t capacity = 0;

    if (class_id == JS_CLASS_BYTE_SPAN_SOURCE ||
        class_id == JS_CLASS_BITMAP_SPAN_SOURCE) {
        esp32_mquickjs_byte_span_source_t source;
        JSValue error = JS_UNDEFINED;

        if (!esp32_mquickjs_open_byte_span_source(
                ctx, value, "I2SChannel.write(data)", &source, &error)) {
            return false;
        }
        while (true) {
            esp32_mquickjs_byte_span_t span;

            if (!esp32_mquickjs_byte_span_source_next(ctx, &source, &span)) {
                if (JS_HasException(ctx)) {
                    esp32_mquickjs_byte_span_source_close(ctx, &source);
                    esp32_mquickjs_memory_payload_free(data);
                    return false;
                }
                break;
            }
            if (!i2s_append_write_bytes(ctx, &data, &length, &capacity,
                                        span.data, span.length)) {
                esp32_mquickjs_byte_span_source_close(ctx, &source);
                esp32_mquickjs_memory_payload_free(data);
                return false;
            }
        }
        esp32_mquickjs_byte_span_source_close(ctx, &source);
    } else {
        esp32_mquickjs_byte_source_t source;
        uint8_t *owned = NULL;
        JSValue error = JS_UNDEFINED;

        if (!esp32_mquickjs_get_byte_source(
                ctx, value, "I2SChannel.write(data)", &source, &owned,
                &error)) {
            return false;
        }
        if (!i2s_append_write_bytes(ctx, &data, &length, &capacity,
                                    source.data, source.length)) {
            esp32_mquickjs_release_byte_source(owned);
            esp32_mquickjs_memory_payload_free(data);
            return false;
        }
        esp32_mquickjs_release_byte_source(owned);
    }
    *out_data = data;
    *out_length = length;
    return true;
}

static bool i2s_write_prepare(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_i2s_slot_t *slot;
    esp32_mquickjs_future_driver_state_t *state;
    uint32_t timeout_ms;
    size_t bytes_per_frame;
    JSValue *owner;

    if (out_state == NULL || argc < 1 || argc > 2) {
        JS_ThrowTypeError(
            ctx,
            "I2SChannel.write(data, timeoutMs?) expects a ByteSource or ByteSpanSource");
        return false;
    }
    if (i2s_ref_from_value(ctx, this_ref->val, "I2SChannel.write()", NULL,
                           &slot) != 0) {
        return false;
    }
    if (!i2s_has_tx(slot)) {
        JS_ThrowTypeError(ctx,
                          "I2SChannel.write() requires a tx or duplex channel");
        return false;
    }
    if (!slot->running) {
        JS_ThrowInternalError(ctx, "I2SChannel.write() requires start() first");
        return false;
    }
    timeout_ms = slot->timeout_ms;
    if (argc == 2 && !JS_IsUndefined(argv[1].val) &&
        !i2s_to_u32(ctx, argv[1].val, &timeout_ms)) {
        JS_ThrowTypeError(
            ctx,
            "I2SChannel.write(data, timeoutMs?) expects a non-negative timeout");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    if (!i2s_load_write_data(ctx, argv[0].val, &state->data,
                             &state->requested_bytes)) {
        heap_caps_free(state);
        return false;
    }
    bytes_per_frame = ((size_t)slot->slot_bits / 8U) * slot->channels;
    if (state->requested_bytes % bytes_per_frame != 0) {
        esp32_mquickjs_memory_payload_free(state->data);
        heap_caps_free(state);
        JS_ThrowRangeError(
            ctx, "I2SChannel.write() byte length must align to a PCM frame");
        return false;
    }
    state->ctx = ctx;
    state->channel_ref.port = slot->port;
    state->channel_ref.generation = slot->generation;
    state->frame_count =
        (uint32_t)(state->requested_bytes / bytes_per_frame);
    state->timeout_ms = timeout_ms;
    state->write_operation = true;
    slot->future_reservations++;
    state->reservation_held = true;
    owner = JS_AddGCRef(ctx, &state->owner_ref);
    *owner = this_ref->val;
    state->owner_retained = true;
    *out_state = state;
    return true;
}

static void i2s_write_step(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2s_slot_t *slot =
        state != NULL ? i2s_get_slot(&state->channel_ref) : NULL;
    size_t written_bytes = 0;

    if (state == NULL || state->completed || state->cancelled) {
        return;
    }
    if (slot != NULL && slot->tx_cancel_requested) {
        state->cancelled = true;
        state->completed = true;
        return;
    }
    if (slot == NULL || !slot->running) {
        state->err = ESP_ERR_INVALID_STATE;
        state->completed = true;
        return;
    }
    if (state->requested_bytes == 0) {
        state->err = ESP_OK;
        state->completed = true;
        return;
    }
    state->err = i2s_channel_write(
        slot->tx_handle, state->data + state->transferred_bytes,
        state->requested_bytes - state->transferred_bytes, &written_bytes, 0);
    if (state->err == ESP_OK || state->err == ESP_ERR_TIMEOUT) {
        state->transferred_bytes += written_bytes;
        if (state->transferred_bytes >= state->requested_bytes) {
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
        state->timed_out = true;
        state->completed = true;
    }
}

static bool i2s_write_start(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            esp32_mquickjs_future_token_t token,
                            esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2s_slot_t *slot =
        state != NULL ? i2s_get_slot(&state->channel_ref) : NULL;

    if (state == NULL || slot == NULL || !slot->running) {
        JS_ThrowReferenceError(ctx,
                               "I2S channel closed before write started");
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    if (slot->release_pending) {
        state->cancelled = true;
        state->completed = true;
        (void)esp32_mquickjs_future_wake(runtime, token);
        return true;
    }
    if (slot->tx_busy) {
        JS_ThrowInternalError(ctx,
                              "I2S channel already has a pending write");
        return false;
    }
    slot->tx_cancel_requested = false;
    i2s_publish_tx_wake_target(slot, runtime, token);
    state->started = true;
    if (state->timeout_ms > 0) {
        state->deadline_us = (uint64_t)esp_timer_get_time() +
                             (uint64_t)state->timeout_ms * 1000ULL;
        if (esp_timer_start_once(slot->tx_timeout_timer,
                                 (uint64_t)state->timeout_ms * 1000ULL) !=
            ESP_OK) {
            JS_ThrowInternalError(ctx, "failed to start I2S write timeout");
            return false;
        }
    }
    i2s_write_step(state);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static esp32_mquickjs_future_poll_t i2s_write_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    i2s_write_step(state);
    return state != NULL && state->completed
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue i2s_write_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);

    if (state == NULL || state->cancelled) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_ThrowInternalError(ctx, "I2S write cancelled");
    }
    if (state->timed_out) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_ThrowInternalError(
            ctx, "I2SChannel.write() timed out after %u bytes",
            (unsigned)state->transferred_bytes);
    }
    if (state->err != ESP_OK) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_ThrowInternalError(ctx, "I2SChannel.write() failed: %s",
                                     esp_err_to_name(state->err));
    }
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "frames", JS_NewUint32(ctx, state->frame_count)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "byteLength",
            JS_NewInt64(ctx, (int64_t)state->transferred_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "timestampUs",
            JS_NewInt64(ctx, (int64_t)esp_timer_get_time()))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static esp32_mquickjs_cancel_result_t i2s_write_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed || state->cancelled) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancelled = true;
    state->completed = true;
    if (state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
    return ESP32_MQUICKJS_CANCELLED;
}

static void i2s_write_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2s_slot_t *slot =
        state != NULL ? i2s_get_slot(&state->channel_ref) : NULL;

    if (state == NULL) {
        return;
    }
    if (slot != NULL && state->started) {
        if (slot->tx_timeout_timer != NULL) {
            (void)esp_timer_stop(slot->tx_timeout_timer);
        }
        i2s_clear_tx_wake_target(slot);
        slot->tx_cancel_requested = false;
    }
    if (state->owner_retained) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
    }
    esp32_mquickjs_memory_payload_free(state->data);
    if (slot != NULL && state->reservation_held) {
        if (slot->future_reservations > 0) {
            slot->future_reservations--;
        }
        if (slot->release_pending && slot->future_reservations == 0 &&
            !i2s_slot_busy(slot)) {
            i2s_cleanup_slot(slot);
        }
    }
    heap_caps_free(state);
}

static const esp32_mquickjs_future_driver_t s_i2s_write_driver = {
    .capture = i2s_write_prepare,
    .start = i2s_write_start,
    .poll = i2s_write_poll,
    .finish = i2s_write_finish,
    .cancel = i2s_write_cancel,
    .destroy = i2s_write_destroy,
    .resource_key = i2s_future_resource_key,
};

static bool i2s_register_future_driver(JSContext *ctx,
                                       esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef object_ref;
    JSGCRef method_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    bool result;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_I2S_CHANNEL);
    *method = JS_IsException(*object) ? JS_EXCEPTION
                                      : JS_GetPropertyStr(ctx, *object, "read");
    result = !JS_IsException(*method) &&
             esp32_mquickjs_future_register_driver(ctx, runtime, *method,
                                                    &s_i2s_read_driver);
    if (result) {
        *method = JS_GetPropertyStr(ctx, *object, "write");
        result = !JS_IsException(*method) &&
                 esp32_mquickjs_future_register_driver(
                     ctx, runtime, *method, &s_i2s_write_driver);
    }
    if (!result && !JS_IsException(*object)) {
        JS_ThrowInternalError(ctx, "failed to register I2S Future driver");
    }
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &object_ref);
    return result;
}

bool esp32_mquickjs_init_i2s_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime)
{
    int i;

    for (i = 0; i < I2S_LL_GET(INST_NUM); ++i) {
        esp_err_t err = i2s_cleanup_slot(&s_i2s_slots[i]);

        if (err != ESP_OK) {
            JS_ThrowInternalError(
                ctx, "failed to reset I2S slot %d: %s", i,
                esp_err_to_name(err));
            return false;
        }
        i2s_reset_slot(&s_i2s_slots[i], i);
    }
    return i2s_register_future_driver(ctx, runtime);
}

void esp32_mquickjs_deinit_i2s_runtime(void)
{
    int i;

    for (i = 0; i < I2S_LL_GET(INST_NUM); ++i) {
        if (s_i2s_slots[i].allocated) {
            i2s_request_close(&s_i2s_slots[i]);
        }
    }
}

JSValue js_i2s_channel_constructor(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "I2SChannel cannot be constructed directly");
}

void js_i2s_channel_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_i2s_ref_t *ref = opaque;
    esp32_mquickjs_i2s_slot_t *slot = i2s_get_slot(ref);

    (void)ctx;
    if (slot != NULL) {
        i2s_request_close(slot);
    }
    heap_caps_free(ref);
}

JSValue js_i2s_channel_start(JSContext *ctx, JSValue *this_val,
                           int argc, JSValue *argv)
{
    i2s_channel_resource_context_t channel_context = {0};
    esp32_mquickjs_i2s_channel_resources_t channel_resources;
    esp32_mquickjs_i2s_channel_resource_ops_t channel_ops;
    esp32_mquickjs_i2s_slot_t *slot;
    esp_err_t err;

    (void)argc;
    (void)argv;
    if (i2s_ref_from_value(ctx, *this_val, "I2SChannel.start()", NULL,
                           &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (slot->running) {
        return JS_TRUE;
    }
    channel_resources = i2s_channel_resources_from_slot(slot);
    channel_ops = i2s_channel_resource_ops(&channel_context);
    err = (esp_err_t)esp32_mquickjs_i2s_channel_resources_start(
        &channel_resources, &channel_ops);
    i2s_store_channel_resources(slot, &channel_resources);
    slot->running = i2s_channels_fully_enabled(slot);
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx, "I2SChannel.start() failed: %s",
                                     esp_err_to_name(err));
    }
    return JS_TRUE;
}

JSValue js_i2s_channel_stop(JSContext *ctx, JSValue *this_val,
                          int argc, JSValue *argv)
{
    i2s_channel_resource_context_t channel_context = {0};
    esp32_mquickjs_i2s_channel_resources_t channel_resources;
    esp32_mquickjs_i2s_channel_resource_ops_t channel_ops;
    esp32_mquickjs_i2s_slot_t *slot;
    esp_err_t err;

    (void)argc;
    (void)argv;
    if (i2s_ref_from_value(ctx, *this_val, "I2SChannel.stop()", NULL,
                           &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (!slot->running && !slot->rx_enabled && !slot->tx_enabled) {
        return JS_TRUE;
    }
    if (i2s_slot_busy(slot) || slot->future_reservations > 0) {
        return JS_ThrowInternalError(ctx,
                                     "I2SChannel.stop() refused while I/O is pending");
    }
    channel_resources = i2s_channel_resources_from_slot(slot);
    channel_ops = i2s_channel_resource_ops(&channel_context);
    err = (esp_err_t)esp32_mquickjs_i2s_channel_resources_stop(
        &channel_resources, &channel_ops);
    i2s_store_channel_resources(slot, &channel_resources);
    slot->running = i2s_channels_fully_enabled(slot);
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx, "I2SChannel.stop() failed: %s",
                                     esp_err_to_name(err));
    }
    return JS_TRUE;
}

JSValue js_i2s_channel_read(JSContext *ctx, JSValue *this_val,
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

JSValue js_i2s_channel_write(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *method = JS_GetPropertyStr(ctx, *this_val, "write");
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

JSValue js_i2s_channel_status(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv)
{
    esp32_mquickjs_i2s_slot_t *slot;

    (void)argc;
    (void)argv;
    if (i2s_ref_from_value(ctx, *this_val, "I2SChannel.status()", NULL,
                           &slot) != 0) {
        return JS_EXCEPTION;
    }
    return i2s_status_object(ctx, slot);
}

JSValue js_i2s_channel_close(JSContext *ctx, JSValue *this_val,
                           int argc, JSValue *argv)
{
    esp32_mquickjs_i2s_ref_t *ref;
    esp32_mquickjs_i2s_slot_t *slot;
    esp_err_t err;

    (void)argc;
    (void)argv;
    if (JS_GetClassID(ctx, *this_val) != JS_CLASS_I2S_CHANNEL) {
        return JS_ThrowTypeError(ctx, "I2SChannel.close() expects an I2SChannel");
    }
    ref = JS_GetOpaque(ctx, *this_val);
    if (ref == NULL) {
        return JS_TRUE;
    }
    slot = i2s_get_slot(ref);
    if (slot != NULL) {
        if (!i2s_slot_busy(slot) && slot->future_reservations == 0) {
            slot->release_pending = true;
            err = i2s_cleanup_slot(slot);
            if (err != ESP_OK) {
                return JS_ThrowInternalError(
                    ctx, "I2SChannel.close() failed: %s",
                    esp_err_to_name(err));
            }
        } else {
            i2s_request_close(slot);
        }
    }
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
        !esp32_mquickjs_set_property_ref(ctx, limits, "maxWriteBytes",
                                         JS_NewUint32(ctx, I2S_MAX_WRITE_BYTES)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ports", *ports) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "standard", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "standardRx", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "standardTx", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "standardDuplex",
                                         JS_TRUE) ||
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

static bool i2s_commit_dma_accounting(esp32_mquickjs_i2s_slot_t *slot)
{
    i2s_chan_info_t info;
    size_t buffer_bytes = 0;
    size_t total_buffer_bytes = 0;

    if (slot == NULL) {
        return false;
    }
    if (slot->rx_handle != NULL) {
        if (i2s_channel_get_info(slot->rx_handle, &info) != ESP_OK ||
            info.total_dma_buf_size == 0 ||
            info.total_dma_buf_size % slot->dma_descriptor_count != 0 ||
            total_buffer_bytes > SIZE_MAX - info.total_dma_buf_size) {
            return false;
        }
        buffer_bytes =
            info.total_dma_buf_size / slot->dma_descriptor_count;
        total_buffer_bytes += info.total_dma_buf_size;
    }
    if (slot->tx_handle != NULL) {
        if (i2s_channel_get_info(slot->tx_handle, &info) != ESP_OK ||
            info.total_dma_buf_size == 0 ||
            info.total_dma_buf_size % slot->dma_descriptor_count != 0 ||
            (buffer_bytes != 0 &&
             buffer_bytes !=
                 info.total_dma_buf_size / slot->dma_descriptor_count) ||
            total_buffer_bytes > SIZE_MAX - info.total_dma_buf_size) {
            return false;
        }
        buffer_bytes =
            info.total_dma_buf_size / slot->dma_descriptor_count;
        total_buffer_bytes += info.total_dma_buf_size;
    }
    if (!esp32_mquickjs_memory_commit_driver_pinned(
            &slot->dma_reservation, total_buffer_bytes)) {
        return false;
    }
    slot->dma_buffer_bytes = buffer_bytes;
    slot->dma_total_buffer_bytes = total_buffer_bytes;
    return true;
}

JSValue js_i2s_open(JSContext *ctx, JSValue *this_val, int argc,
                    JSValue *argv)
{
    esp32_mquickjs_i2s_channel_resources_t channel_resources = {0};
    esp32_mquickjs_i2s_channel_resource_ops_t channel_ops;
    i2s_channel_resource_context_t channel_context;
    esp32_mquickjs_i2s_timer_resources_t timer_resources = {0};
    esp32_mquickjs_i2s_timer_resource_ops_t timer_ops;
    esp32_mquickjs_i2s_mode_t mode = ESP32_MQUICKJS_I2S_MODE_STANDARD;
    esp32_mquickjs_i2s_direction_t direction =
        ESP32_MQUICKJS_I2S_DIRECTION_RX;
    uint32_t sample_rate = I2S_DEFAULT_SAMPLE_RATE_HZ;
    uint32_t data_bits = 16;
    uint32_t slot_bits = 16;
    uint32_t dma_descriptors = I2S_DEFAULT_DMA_DESCRIPTORS;
    uint32_t dma_frames = I2S_DEFAULT_DMA_FRAMES;
    size_t dma_buffer_bytes;
    size_t dma_largest_block_bytes;
    size_t dma_request;
    uint32_t timeout_ms = I2S_DEFAULT_TIMEOUT_MS;
    i2s_slot_mode_t slot_mode = I2S_SLOT_MODE_MONO;
    i2s_std_slot_mask_t slot_mask = I2S_STD_SLOT_LEFT;
    const char *format = "philips";
    int requested_port = I2S_NUM_AUTO;
    int bclk = -1;
    int ws = -1;
    int din = -1;
    int dout = -1;
    int mclk = I2S_GPIO_UNUSED;
    int pdm_clk = esp32_mquickjs_profile_int_or("ESP32QJS_I2S_PDM_CLK", -2);
    int pdm_din = esp32_mquickjs_profile_int_or("ESP32QJS_I2S_PDM_DIN", -2);
    esp32_mquickjs_i2s_slot_t *slot = NULL;
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_event_callbacks_t callbacks = {
        .on_recv = i2s_on_receive,
        .on_recv_q_ovf = i2s_on_overflow,
        .on_sent = i2s_on_sent,
        .on_send_q_ovf = i2s_on_send_queue_overflow,
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
        if (JS_IsException(*property)) {
            goto parse_fail;
        }
        if (i2s_string_equals(ctx, *property, "rx")) {
            direction = ESP32_MQUICKJS_I2S_DIRECTION_RX;
        } else if (i2s_string_equals(ctx, *property, "tx")) {
            direction = ESP32_MQUICKJS_I2S_DIRECTION_TX;
        } else if (i2s_string_equals(ctx, *property, "duplex")) {
            direction = ESP32_MQUICKJS_I2S_DIRECTION_DUPLEX;
        } else {
            JS_ThrowTypeError(
                ctx,
                "i2s.open({ direction }) expects \"rx\", \"tx\", or \"duplex\"");
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
            if ((direction & ESP32_MQUICKJS_I2S_DIRECTION_RX) != 0) {
                READ_STD_PIN("din", &din, false);
            }
            if ((direction & ESP32_MQUICKJS_I2S_DIRECTION_TX) != 0) {
                READ_STD_PIN("dout", &dout, true);
            }
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
        if (direction != ESP32_MQUICKJS_I2S_DIRECTION_RX) {
            return JS_ThrowTypeError(
                ctx, "i2s.open(pdm) only accepts direction \"rx\"");
        }
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
        if (bclk == ws || (din >= 0 && (bclk == din || ws == din)) ||
            (dout >= 0 &&
             (bclk == dout || ws == dout || dout == din)) ||
            (mclk != I2S_GPIO_UNUSED &&
             (mclk == bclk || mclk == ws || mclk == din ||
              mclk == dout))) {
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
    dma_buffer_bytes = (size_t)dma_frames *
#if CONFIG_IDF_TARGET_ESP32
                       (((size_t)data_bits + 15U) / 16U) * 2U *
#else
                       (((size_t)data_bits + 7U) / 8U) *
#endif
                       (slot_mode == I2S_SLOT_MODE_MONO ? 1U : 2U);
    dma_largest_block_bytes = dma_buffer_bytes + 32U;
    if (dma_largest_block_bytes < 32U) {
        dma_largest_block_bytes = 32U;
    }
    if (dma_largest_block_bytes <
        (size_t)dma_descriptors * sizeof(void *)) {
        dma_largest_block_bytes =
            (size_t)dma_descriptors * sizeof(void *);
    }
    dma_request =
        (direction == ESP32_MQUICKJS_I2S_DIRECTION_DUPLEX ? 2U : 1U) *
        (size_t)dma_descriptors * (dma_buffer_bytes + 32U);
    for (i = 0; i < I2S_LL_GET(INST_NUM); ++i) {
        if (s_i2s_slots[i].allocated &&
            s_i2s_slots[i].release_pending &&
            !i2s_slot_busy(&s_i2s_slots[i]) &&
            s_i2s_slots[i].future_reservations == 0) {
            (void)i2s_cleanup_slot(&s_i2s_slots[i]);
        }
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
    slot->direction = direction;
    slot->sample_rate_hz = sample_rate;
    slot->data_bits = (uint8_t)data_bits;
    slot->slot_bits = (uint8_t)slot_bits;
    slot->channels = slot_mode == I2S_SLOT_MODE_MONO ? 1 : 2;
    slot->dma_descriptor_count = dma_descriptors;
    slot->dma_frames_per_descriptor = dma_frames;
    slot->dma_buffer_bytes = dma_buffer_bytes;
    slot->timeout_ms = timeout_ms;
    if (!esp32_mquickjs_memory_reserve_internal_dma(
            &slot->dma_reservation, "i2s.driver", dma_request,
            dma_largest_block_bytes)) {
        i2s_cleanup_slot(slot);
        return i2s_throw_no_memory(ctx, "i2s.open()");
    }
    channel_config.id = slot->port;
    channel_config.dma_desc_num = dma_descriptors;
    channel_config.dma_frame_num = dma_frames;
    channel_config.auto_clear_after_cb =
        (direction & ESP32_MQUICKJS_I2S_DIRECTION_TX) != 0;
    channel_context = (i2s_channel_resource_context_t){
        .config = &channel_config,
    };
    channel_ops = i2s_channel_resource_ops(&channel_context);
    err = (esp_err_t)esp32_mquickjs_i2s_channel_resources_init(
        &channel_resources,
        (direction & ESP32_MQUICKJS_I2S_DIRECTION_RX) != 0,
        (direction & ESP32_MQUICKJS_I2S_DIRECTION_TX) != 0,
        &channel_ops);
    i2s_store_channel_resources(slot, &channel_resources);
    if (err != ESP_OK) {
        esp_err_t cleanup_err = i2s_cleanup_slot(slot);

        if (cleanup_err != ESP_OK) {
            err = cleanup_err;
        }
        if (err == ESP_ERR_NO_MEM) {
            return i2s_throw_no_memory(ctx, "i2s.open()");
        }
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
                .dout = dout >= 0 ? (gpio_num_t)dout : I2S_GPIO_UNUSED,
                .din = din >= 0 ? (gpio_num_t)din : I2S_GPIO_UNUSED,
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
        err = slot->tx_handle != NULL
                  ? i2s_channel_init_std_mode(slot->tx_handle, &config)
                  : ESP_OK;
        if (err == ESP_OK && slot->rx_handle != NULL) {
            err = i2s_channel_init_std_mode(slot->rx_handle, &config);
        }
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
    if (err == ESP_OK && !i2s_commit_dma_accounting(slot)) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK && slot->rx_handle != NULL) {
        err = i2s_channel_register_event_callback(slot->rx_handle,
                                                  &callbacks, slot);
    }
    if (err == ESP_OK && slot->tx_handle != NULL) {
        err = i2s_channel_register_event_callback(slot->tx_handle,
                                                  &callbacks, slot);
    }
    if (err == ESP_OK && timeout_ms > 0) {
        timer_ops = i2s_timer_resource_ops(slot);
        err = (esp_err_t)esp32_mquickjs_i2s_timer_resources_init(
            &timer_resources, &timer_ops, slot->rx_handle != NULL,
            slot->tx_handle != NULL);
        if (err == ESP_OK) {
            slot->rx_timeout_timer = timer_resources.rx_timer;
            slot->tx_timeout_timer = timer_resources.tx_timer;
        }
    }
    if (err != ESP_OK) {
        esp_err_t cleanup_err = i2s_cleanup_slot(slot);

        if (cleanup_err != ESP_OK) {
            err = cleanup_err;
        }
        if (err == ESP_ERR_NO_MEM) {
            return i2s_throw_no_memory(ctx, "i2s.open()");
        }
        return JS_ThrowInternalError(ctx, "i2s.open() failed to initialize channel: %s",
                                     esp_err_to_name(err));
    }
    result = i2s_make_channel(ctx, slot);
    if (JS_IsException(result)) {
        slot->release_pending = true;
        (void)i2s_cleanup_slot(slot);
    }
    return result;
}

#endif
