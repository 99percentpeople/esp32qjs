#include "esp32_mquickjs_uart.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_UART

#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_future.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/uart_select.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#define UART_WRITE_FIFO_POLL_MS 1U
#define UART_FLUSH_POLL_US 10000ULL
#define UART_DRIVER_EVENT_QUEUE_LEN 20U
#define UART_WATCH_DEFAULT_MIN_BYTES 1U
#define UART_WATCH_DEFAULT_CAPACITY 8U
#define UART_WATCH_MAX_CAPACITY 64U
#define UART_WATCH_TASK_STACK_SIZE 3072U
#define UART_WATCH_TASK_PRIORITY (tskIDLE_PRIORITY + 2U)

typedef struct {
    int32_t port_id;
    uint32_t generation;
} esp32_mquickjs_uart_port_ref_t;

typedef enum {
    UART_WATCH_EVENT_READABLE,
    UART_WATCH_EVENT_ERROR,
} uart_watch_event_kind_t;

typedef enum {
    UART_WATCH_REASON_THRESHOLD,
    UART_WATCH_REASON_IDLE,
} uart_watch_reason_t;

typedef enum {
    UART_WATCH_ERROR_FIFO_OVERFLOW,
    UART_WATCH_ERROR_BUFFER_FULL,
    UART_WATCH_ERROR_BREAK,
    UART_WATCH_ERROR_PARITY,
    UART_WATCH_ERROR_FRAME,
} uart_watch_error_t;

typedef struct {
    uart_watch_event_kind_t kind;
    uint32_t port_generation;
    uint32_t watch_generation;
    uint32_t sequence;
    uint64_t timestamp_us;
    size_t available_bytes;
    uart_watch_reason_t reason;
    uart_watch_error_t error;
} uart_watch_event_t;

typedef struct {
    bool allocated;
    int32_t port_id;
    uint32_t generation;
    int32_t tx_pin;
    int32_t rx_pin;
    uint32_t baud;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    uint32_t rx_buffer_size;
    uint32_t tx_buffer_size;
    uint32_t timeout_ms;
    uint32_t future_reservations;
    bool release_pending;
    bool read_busy;
    bool write_busy;
    bool read_future_waiting;
    bool write_future_waiting;
    esp32_mquickjs_runtime_t *read_future_runtime;
    esp32_mquickjs_runtime_t *write_future_runtime;
    esp32_mquickjs_future_token_t read_future_token;
    esp32_mquickjs_future_token_t write_future_token;
    uint8_t rx_lane_key;
    uint8_t tx_lane_key;
    esp32_mquickjs_runtime_t *runtime;
    QueueHandle_t driver_events;
    SemaphoreHandle_t watch_lock;
    SemaphoreHandle_t watch_task_done;
    TaskHandle_t watch_task;
    _Atomic bool watch_task_stop;
    esp32_mquickjs_event_queue_t *watcher;
    uint32_t watch_generation;
    uint32_t watch_min_bytes;
    uint32_t watch_idle_ms;
    bool watch_include_errors;
    bool readable_queued;
    uint64_t last_rx_us;
    uint32_t event_sequence;
} esp32_mquickjs_uart_slot_t;

static esp32_mquickjs_uart_slot_t s_uart_slots[SOC_UART_NUM];
static uint32_t s_uart_next_generation = 1;

static bool uart_register_future_drivers(JSContext *ctx,
                                         esp32_mquickjs_runtime_t *runtime);
static void uart_watch_reset_state(esp32_mquickjs_uart_slot_t *slot);
static JSValue uart_future_call_and_wait(JSContext *ctx,
                                         JSValue receiver,
                                         const char *method_name,
                                         int argc,
                                         JSValue *argv);

static void IRAM_ATTR uart_notify_from_isr(
    uart_port_t uart_num,
    uart_select_notif_t notification,
    BaseType_t *task_woken)
{
    esp32_mquickjs_uart_slot_t *slot;
    esp32_mquickjs_runtime_t *runtime = NULL;
    esp32_mquickjs_future_token_t token = {0};
    bool waiting = false;

    if (uart_num < 0 || uart_num >= (uart_port_t)SOC_UART_NUM) {
        return;
    }
    slot = &s_uart_slots[uart_num];
    if (!slot->allocated) {
        return;
    }

    if (notification == UART_SELECT_WRITE_NOTIF) {
        waiting = slot->write_future_waiting;
        runtime = slot->write_future_runtime;
        token = slot->write_future_token;
    } else {
        waiting = slot->read_future_waiting;
        runtime = slot->read_future_runtime;
        token = slot->read_future_token;
    }
    if (waiting && runtime != NULL) {
        (void)esp32_mquickjs_future_wake_from_isr(
            runtime, token, (int *)task_woken);
    } else {
        esp32_mquickjs_notify_active_runtime_from_isr((int *)task_woken);
    }
}

static void uart_set_notifier(
    uart_port_t uart_num,
    uart_select_notif_callback_t callback)
{
    portENTER_CRITICAL(uart_get_selectlock());
    uart_set_select_notif_callback(uart_num, callback);
    portEXIT_CRITICAL(uart_get_selectlock());
}

static void uart_set_future_waiter(
    esp32_mquickjs_uart_slot_t *slot,
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    bool write_lane,
    bool waiting)
{
    if (slot == NULL) {
        return;
    }
    portENTER_CRITICAL(uart_get_selectlock());
    if (write_lane) {
        slot->write_future_runtime = waiting ? runtime : NULL;
        slot->write_future_token = token;
        slot->write_future_waiting = waiting;
    } else {
        slot->read_future_runtime = waiting ? runtime : NULL;
        slot->read_future_token = token;
        slot->read_future_waiting = waiting;
    }
    portEXIT_CRITICAL(uart_get_selectlock());
}

static bool js_value_to_i32(JSContext *ctx, JSValue value, int32_t *out_value)
{
    int raw_value = 0;

    if (JS_ToInt32(ctx, &raw_value, value) != 0) {
        return false;
    }
    *out_value = (int32_t)raw_value;
    return true;
}

static bool js_value_to_u32(JSContext *ctx, JSValue value, uint32_t *out_value)
{
    int raw_value = 0;

    if (JS_ToInt32(ctx, &raw_value, value) != 0 || raw_value < 0) {
        return false;
    }
    *out_value = (uint32_t)raw_value;
    return true;
}

static bool js_value_to_bool(JSContext *ctx, JSValue value, bool *out_value)
{
    int raw_value = 0;

    if (JS_IsBool(value)) {
        *out_value = value == JS_TRUE;
        return true;
    }
    if (JS_ToInt32(ctx, &raw_value, value) != 0) {
        return false;
    }
    *out_value = raw_value != 0;
    return true;
}

static bool js_value_to_uart_port(JSContext *ctx, JSValue value, int32_t *out_port)
{
    int32_t port = -1;

    if (!js_value_to_i32(ctx, value, &port) || port < 0 || port >= (int32_t)SOC_UART_NUM) {
        return false;
    }
    *out_port = port;
    return true;
}

static bool js_value_to_gpio_num(JSContext *ctx, JSValue value, int32_t *out_pin)
{
    int32_t pin = -1;

    if (!js_value_to_i32(ctx, value, &pin)) {
        return false;
    }
    if (pin == -1) {
        *out_pin = -1;
        return true;
    }
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin)) {
        return false;
    }

    *out_pin = pin;
    return true;
}

static bool js_value_to_uart_data_bits(JSContext *ctx, JSValue value, uart_word_length_t *out_data_bits)
{
    int32_t bits = 0;

    if (!js_value_to_i32(ctx, value, &bits)) {
        return false;
    }

    switch (bits) {
    case 5:
        *out_data_bits = UART_DATA_5_BITS;
        return true;
    case 6:
        *out_data_bits = UART_DATA_6_BITS;
        return true;
    case 7:
        *out_data_bits = UART_DATA_7_BITS;
        return true;
    case 8:
        *out_data_bits = UART_DATA_8_BITS;
        return true;
    default:
        return false;
    }
}

static int32_t uart_data_bits_to_number(uart_word_length_t data_bits)
{
    switch (data_bits) {
    case UART_DATA_5_BITS:
        return 5;
    case UART_DATA_6_BITS:
        return 6;
    case UART_DATA_7_BITS:
        return 7;
    case UART_DATA_8_BITS:
    default:
        return 8;
    }
}

static bool js_value_to_uart_parity(JSContext *ctx, JSValue value, uart_parity_t *out_parity)
{
    JSCStringBuf parity_buf;
    const char *parity;

    if (!JS_IsString(ctx, value)) {
        return false;
    }

    parity = JS_ToCString(ctx, value, &parity_buf);
    if (parity == NULL) {
        return false;
    }

    if (strcmp(parity, "none") == 0) {
        *out_parity = UART_PARITY_DISABLE;
        return true;
    }
    if (strcmp(parity, "even") == 0) {
        *out_parity = UART_PARITY_EVEN;
        return true;
    }
    if (strcmp(parity, "odd") == 0) {
        *out_parity = UART_PARITY_ODD;
        return true;
    }
    return false;
}

static const char *uart_parity_to_string(uart_parity_t parity)
{
    switch (parity) {
    case UART_PARITY_EVEN:
        return "even";
    case UART_PARITY_ODD:
        return "odd";
    case UART_PARITY_DISABLE:
    default:
        return "none";
    }
}

static bool js_value_to_uart_stop_bits(JSContext *ctx, JSValue value, uart_stop_bits_t *out_stop_bits)
{
    double stop_bits = 0.0;

    if (JS_ToNumber(ctx, &stop_bits, value) != 0) {
        return false;
    }

    if (stop_bits == 1.0) {
        *out_stop_bits = UART_STOP_BITS_1;
        return true;
    }
    if (stop_bits == 1.5) {
        *out_stop_bits = UART_STOP_BITS_1_5;
        return true;
    }
    if (stop_bits == 2.0) {
        *out_stop_bits = UART_STOP_BITS_2;
        return true;
    }
    return false;
}

static JSValue uart_stop_bits_to_value(JSContext *ctx, uart_stop_bits_t stop_bits)
{
    switch (stop_bits) {
    case UART_STOP_BITS_1_5:
        return JS_NewFloat64(ctx, 1.5);
    case UART_STOP_BITS_2:
        return JS_NewInt32(ctx, 2);
    case UART_STOP_BITS_1:
    default:
        return JS_NewInt32(ctx, 1);
    }
}

static JSValue uart_throw_error(JSContext *ctx,
                                esp_err_t err,
                                const char *message)
{
    return JS_ThrowInternalError(ctx, "%s: %s", message, esp_err_to_name(err));
}

static void uart_init_slot(esp32_mquickjs_uart_slot_t *slot, int32_t port_id)
{
    memset(slot, 0, sizeof(*slot));
    slot->port_id = port_id;
    atomic_init(&slot->watch_task_stop, false);
    uart_watch_reset_state(slot);
}

static void uart_reset_slots(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)SOC_UART_NUM; ++i) {
        uart_init_slot(&s_uart_slots[i], i);
    }
    s_uart_next_generation = 1;
}

static uint32_t uart_take_generation(void)
{
    uint32_t generation = s_uart_next_generation++;

    if (generation == 0) {
        generation = s_uart_next_generation++;
    }
    return generation;
}

static esp32_mquickjs_uart_slot_t *uart_alloc_slot(int32_t port_id)
{
    esp32_mquickjs_uart_slot_t *slot;

    if (port_id < 0 || port_id >= (int32_t)SOC_UART_NUM) {
        return NULL;
    }

    slot = &s_uart_slots[port_id];
    if (slot->allocated || uart_is_driver_installed((uart_port_t)port_id)) {
        return NULL;
    }

    uart_init_slot(slot, port_id);
    slot->allocated = true;
    slot->generation = uart_take_generation();
    return slot;
}

static void uart_watch_reset_state(esp32_mquickjs_uart_slot_t *slot)
{
    if (slot == NULL) {
        return;
    }
    slot->watcher = NULL;
    slot->watch_min_bytes = UART_WATCH_DEFAULT_MIN_BYTES;
    slot->watch_idle_ms = 0;
    slot->watch_include_errors = true;
    slot->readable_queued = false;
    slot->last_rx_us = 0;
    slot->event_sequence = 0;
}

static bool uart_watch_lock(esp32_mquickjs_uart_slot_t *slot)
{
    return slot != NULL && slot->watch_lock != NULL &&
           xSemaphoreTake(slot->watch_lock, portMAX_DELAY) == pdTRUE;
}

static void uart_watch_unlock(esp32_mquickjs_uart_slot_t *slot)
{
    if (slot != NULL && slot->watch_lock != NULL) {
        xSemaphoreGive(slot->watch_lock);
    }
}

static void uart_watch_note_rx(esp32_mquickjs_uart_slot_t *slot)
{
    if (!uart_watch_lock(slot)) {
        return;
    }
    if (slot->watcher != NULL) {
        slot->last_rx_us = (uint64_t)esp_timer_get_time();
    }
    uart_watch_unlock(slot);
}

static bool uart_watch_publish_readable(esp32_mquickjs_uart_slot_t *slot,
                                        bool allow_idle)
{
    uart_watch_event_t event = {0};
    esp32_mquickjs_event_queue_t *watcher;
    uint64_t now_us;
    uint64_t idle_us;
    size_t available = 0;
    bool sent;

    if (!uart_watch_lock(slot)) {
        return false;
    }
    watcher = slot->watcher;
    if (watcher == NULL || slot->readable_queued ||
        esp32_mquickjs_event_queue_is_closed(watcher) ||
        uart_get_buffered_data_len((uart_port_t)slot->port_id,
                                   &available) != ESP_OK ||
        available == 0) {
        uart_watch_unlock(slot);
        return false;
    }

    now_us = (uint64_t)esp_timer_get_time();
    idle_us = (uint64_t)slot->watch_idle_ms * 1000ULL;
    if (available >= slot->watch_min_bytes) {
        event.reason = UART_WATCH_REASON_THRESHOLD;
    } else if (!allow_idle || idle_us == 0 || slot->last_rx_us == 0 ||
               now_us - slot->last_rx_us < idle_us) {
        uart_watch_unlock(slot);
        return false;
    } else {
        event.reason = UART_WATCH_REASON_IDLE;
    }

    event.kind = UART_WATCH_EVENT_READABLE;
    event.port_generation = slot->generation;
    event.watch_generation = slot->watch_generation;
    event.sequence = ++slot->event_sequence;
    event.timestamp_us = now_us;
    event.available_bytes = available;
    slot->readable_queued = true;
    sent = esp32_mquickjs_event_queue_send(watcher, &event);
    if (!sent && slot->watcher == watcher &&
        slot->watch_generation == event.watch_generation) {
        slot->readable_queued = false;
    }
    uart_watch_unlock(slot);
    return sent;
}

static bool uart_watch_error_from_driver(uart_event_type_t type,
                                         uart_watch_error_t *out_error)
{
    if (out_error == NULL) {
        return false;
    }
    switch (type) {
    case UART_FIFO_OVF:
        *out_error = UART_WATCH_ERROR_FIFO_OVERFLOW;
        return true;
    case UART_BUFFER_FULL:
        *out_error = UART_WATCH_ERROR_BUFFER_FULL;
        return true;
    case UART_BREAK:
        *out_error = UART_WATCH_ERROR_BREAK;
        return true;
    case UART_PARITY_ERR:
        *out_error = UART_WATCH_ERROR_PARITY;
        return true;
    case UART_FRAME_ERR:
        *out_error = UART_WATCH_ERROR_FRAME;
        return true;
    default:
        return false;
    }
}

static void uart_watch_publish_error(esp32_mquickjs_uart_slot_t *slot,
                                     uart_event_type_t type)
{
    uart_watch_event_t event = {0};
    esp32_mquickjs_event_queue_t *watcher;

    if (!uart_watch_error_from_driver(type, &event.error) ||
        !uart_watch_lock(slot)) {
        return;
    }
    watcher = slot->watcher;
    if (watcher != NULL && slot->watch_include_errors &&
        !esp32_mquickjs_event_queue_is_closed(watcher)) {
        event.kind = UART_WATCH_EVENT_ERROR;
        event.port_generation = slot->generation;
        event.watch_generation = slot->watch_generation;
        event.sequence = ++slot->event_sequence;
        event.timestamp_us = (uint64_t)esp_timer_get_time();
        (void)esp32_mquickjs_event_queue_send(watcher, &event);
    }
    uart_watch_unlock(slot);
}

static TickType_t uart_watch_next_wait(esp32_mquickjs_uart_slot_t *slot)
{
    uint64_t now_us;
    uint64_t deadline_us;
    TickType_t wait = portMAX_DELAY;

    if (!uart_watch_lock(slot)) {
        return pdMS_TO_TICKS(20);
    }
    if (slot->watcher != NULL && !slot->readable_queued &&
        slot->watch_idle_ms > 0 && slot->last_rx_us > 0) {
        now_us = (uint64_t)esp_timer_get_time();
        deadline_us = slot->last_rx_us +
                      (uint64_t)slot->watch_idle_ms * 1000ULL;
        if (deadline_us <= now_us) {
            wait = 0;
        } else {
            uint64_t remaining_ms =
                (deadline_us - now_us + 999ULL) / 1000ULL;

            wait = pdMS_TO_TICKS(remaining_ms > UINT32_MAX
                                     ? UINT32_MAX
                                     : (uint32_t)remaining_ms);
            if (wait == 0) {
                wait = 1;
            }
        }
    }
    uart_watch_unlock(slot);
    return wait;
}

static void uart_watch_task(void *opaque)
{
    esp32_mquickjs_uart_slot_t *slot = opaque;
    uart_event_t driver_event;

    while (slot != NULL && !atomic_load_explicit(
                               &slot->watch_task_stop,
                               memory_order_acquire)) {
        TickType_t wait = uart_watch_next_wait(slot);

        if (xQueueReceive(slot->driver_events, &driver_event, wait) == pdTRUE) {
            if (atomic_load_explicit(&slot->watch_task_stop,
                                     memory_order_acquire)) {
                break;
            }
            if (driver_event.type == UART_DATA) {
                uart_watch_note_rx(slot);
                (void)uart_watch_publish_readable(
                    slot, driver_event.timeout_flag);
            } else {
                uart_watch_publish_error(slot, driver_event.type);
            }
        } else if (!atomic_load_explicit(&slot->watch_task_stop,
                                         memory_order_acquire)) {
            (void)uart_watch_publish_readable(slot, true);
        }
    }
    if (slot != NULL && slot->watch_task_done != NULL) {
        xSemaphoreGive(slot->watch_task_done);
    }
    vTaskDelete(NULL);
}

static bool uart_start_watch_task(esp32_mquickjs_uart_slot_t *slot)
{
    if (slot == NULL || slot->driver_events == NULL) {
        return false;
    }
    if (slot->watch_task != NULL) {
        return true;
    }
    if (slot->watch_task_done == NULL) {
        slot->watch_task_done = xSemaphoreCreateBinary();
        if (slot->watch_task_done == NULL) {
            return false;
        }
    }
    atomic_store_explicit(&slot->watch_task_stop, false,
                          memory_order_release);
    return xTaskCreate(uart_watch_task,
                       "mqjs_uart_evt",
                       UART_WATCH_TASK_STACK_SIZE,
                       slot,
                       UART_WATCH_TASK_PRIORITY,
                       &slot->watch_task) == pdPASS;
}

static void uart_stop_watch_task(esp32_mquickjs_uart_slot_t *slot)
{
    TaskHandle_t task;

    if (slot == NULL || slot->watch_task == NULL) {
        return;
    }
    if (uart_watch_lock(slot)) {
        atomic_store_explicit(&slot->watch_task_stop, true,
                              memory_order_release);
        task = slot->watch_task;
        uart_watch_unlock(slot);
    } else {
        task = slot->watch_task;
        atomic_store_explicit(&slot->watch_task_stop, true,
                              memory_order_release);
    }
    (void)xTaskAbortDelay(task);
    if (slot->watch_task_done != NULL) {
        (void)xSemaphoreTake(slot->watch_task_done, portMAX_DELAY);
    }
    slot->watch_task = NULL;
}

static void uart_watch_close_queue(void *opaque)
{
    esp32_mquickjs_uart_slot_t *slot = opaque;

    if (!uart_watch_lock(slot)) {
        return;
    }
    slot->watcher = NULL;
    slot->readable_queued = false;
    slot->last_rx_us = 0;
    uart_watch_unlock(slot);
}

static void uart_close_watcher(esp32_mquickjs_uart_slot_t *slot)
{
    esp32_mquickjs_event_queue_t *watcher = NULL;

    if (!uart_watch_lock(slot)) {
        return;
    }
    watcher = slot->watcher;
    slot->watcher = NULL;
    slot->readable_queued = false;
    slot->last_rx_us = 0;
    uart_watch_unlock(slot);
    if (watcher != NULL) {
        (void)esp32_mquickjs_event_queue_close(watcher);
    }
}

static void uart_cleanup_slot(esp32_mquickjs_uart_slot_t *slot)
{
    int32_t port_id;
    SemaphoreHandle_t watch_lock;
    SemaphoreHandle_t watch_task_done;

    if (slot == NULL || !slot->allocated) {
        return;
    }

    port_id = slot->port_id;
    uart_stop_watch_task(slot);
    uart_close_watcher(slot);
    if (uart_is_driver_installed((uart_port_t)port_id)) {
        uart_set_notifier((uart_port_t)port_id, NULL);
        uart_driver_delete((uart_port_t)port_id);
    }
    watch_lock = slot->watch_lock;
    watch_task_done = slot->watch_task_done;
    uart_init_slot(slot, port_id);
    if (watch_task_done != NULL) {
        vSemaphoreDelete(watch_task_done);
    }
    if (watch_lock != NULL) {
        vSemaphoreDelete(watch_lock);
    }
}

static esp32_mquickjs_uart_slot_t *uart_get_slot(const esp32_mquickjs_uart_port_ref_t *ref)
{
    esp32_mquickjs_uart_slot_t *slot;

    if (ref == NULL || ref->port_id < 0 || ref->port_id >= (int32_t)SOC_UART_NUM) {
        return NULL;
    }

    slot = &s_uart_slots[ref->port_id];
    if (!slot->allocated || slot->generation != ref->generation) {
        return NULL;
    }
    return slot;
}

static JSValue uart_make_write_stats(JSContext *ctx,
                                     uint32_t chunks,
                                     size_t bytes,
                                     uint64_t total_us)
{
    JSGCRef stats_ref;
    JSValue *stats;

    stats = JS_PushGCRef(ctx, &stats_ref);
    *stats = JS_NewObject(ctx);
    if (JS_IsException(*stats)) {
        JS_PopGCRef(ctx, &stats_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, stats, "chunks", JS_NewUint32(ctx, chunks)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "bytes", JS_NewInt64(ctx, (int64_t)bytes)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "totalUs", JS_NewInt64(ctx, (int64_t)total_us))) {
        JS_PopGCRef(ctx, &stats_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &stats_ref);
}

static JSValue uart_make_status_object(JSContext *ctx, const esp32_mquickjs_uart_slot_t *slot)
{
    JSGCRef status_ref;
    JSGCRef stop_bits_ref;
    JSValue *status_obj;
    JSValue *stop_bits_value;

    status_obj = JS_PushGCRef(ctx, &status_ref);
    stop_bits_value = JS_PushGCRef(ctx, &stop_bits_ref);
    *status_obj = JS_NewObject(ctx);
    *stop_bits_value = JS_UNDEFINED;
    if (JS_IsException(*status_obj)) {
        JS_PopGCRef(ctx, &stop_bits_ref);
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    *stop_bits_value = uart_stop_bits_to_value(ctx, slot != NULL ? slot->stop_bits : UART_STOP_BITS_1);
    if (JS_IsException(*stop_bits_value) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "opened",
                                     JS_NewBool(slot != NULL && slot->allocated)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "port",
                                     JS_NewInt32(ctx, slot != NULL ? slot->port_id : ESP32_MQUICKJS_UART_DEFAULT_PORT)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "tx",
                                     JS_NewInt32(ctx, slot != NULL ? slot->tx_pin : ESP32_MQUICKJS_UART_DEFAULT_TX_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "rx",
                                     JS_NewInt32(ctx, slot != NULL ? slot->rx_pin : ESP32_MQUICKJS_UART_DEFAULT_RX_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "baud",
                                     JS_NewUint32(ctx, slot != NULL ? slot->baud : ESP32_MQUICKJS_UART_DEFAULT_BAUD)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "dataBits",
                                     JS_NewInt32(ctx, slot != NULL ? uart_data_bits_to_number(slot->data_bits) : 8)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "parity",
                                     JS_NewString(ctx, slot != NULL ? uart_parity_to_string(slot->parity) : "none")) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "stopBits", *stop_bits_value) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "rxBufferSize",
                                     JS_NewUint32(ctx, slot != NULL ? slot->rx_buffer_size : ESP32_MQUICKJS_UART_DEFAULT_RX_BUFFER_SIZE)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "txBufferSize",
                                     JS_NewUint32(ctx, slot != NULL ? slot->tx_buffer_size : ESP32_MQUICKJS_UART_DEFAULT_TX_BUFFER_SIZE)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "timeoutMs",
                                     JS_NewUint32(ctx, slot != NULL ? slot->timeout_ms : ESP32_MQUICKJS_UART_DEFAULT_TIMEOUT_MS))) {
        JS_PopGCRef(ctx, &stop_bits_ref);
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    JS_PopGCRef(ctx, &stop_bits_ref);
    return JS_PopGCRef(ctx, &status_ref);
}

static JSValue uart_make_port_object(JSContext *ctx, const esp32_mquickjs_uart_slot_t *slot)
{
    JSGCRef port_ref;
    JSValue *port_obj;
    esp32_mquickjs_uart_port_ref_t *port_data;

    port_obj = JS_PushGCRef(ctx, &port_ref);
    *port_obj = JS_NewObjectClassUser(ctx, JS_CLASS_UART_PORT);
    if (JS_IsException(*port_obj) || slot == NULL) {
        JS_PopGCRef(ctx, &port_ref);
        return JS_EXCEPTION;
    }

    port_data = heap_caps_malloc(sizeof(*port_data), MALLOC_CAP_8BIT);
    if (port_data == NULL) {
        JS_PopGCRef(ctx, &port_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    port_data->port_id = slot->port_id;
    port_data->generation = slot->generation;
    JS_SetOpaque(ctx, *port_obj, port_data);

    return JS_PopGCRef(ctx, &port_ref);
}

static int uart_port_ref_from_object(JSContext *ctx,
                                     JSValue port_value,
                                     const char *api_name,
                                     esp32_mquickjs_uart_port_ref_t *out_ref)
{
    const esp32_mquickjs_uart_port_ref_t *port_ref;

    if (out_ref == NULL || JS_GetClassID(ctx, port_value) != JS_CLASS_UART_PORT) {
        JS_ThrowTypeError(ctx, "%s expects a valid UARTPort instance", api_name);
        return -1;
    }
    port_ref = JS_GetOpaque(ctx, port_value);
    if (port_ref == NULL) {
        JS_ThrowTypeError(ctx, "%s expects a valid UARTPort instance", api_name);
        return -1;
    }
    *out_ref = *port_ref;
    return 0;
}

static int uart_get_bound_slot(JSContext *ctx,
                               JSValue port_value,
                               const char *api_name,
                               esp32_mquickjs_uart_port_ref_t *out_ref,
                               esp32_mquickjs_uart_slot_t **out_slot)
{
    esp32_mquickjs_uart_slot_t *slot;

    if (uart_port_ref_from_object(ctx, port_value, api_name, out_ref) != 0) {
        return -1;
    }
    slot = uart_get_slot(out_ref);
    if (slot == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the UART port is closed", api_name);
        return -1;
    }
    if (out_slot != NULL) {
        *out_slot = slot;
    }
    return 0;
}

static const char *uart_watch_error_name(uart_watch_error_t error)
{
    switch (error) {
    case UART_WATCH_ERROR_FIFO_OVERFLOW:
        return "fifoOverflow";
    case UART_WATCH_ERROR_BUFFER_FULL:
        return "bufferFull";
    case UART_WATCH_ERROR_BREAK:
        return "break";
    case UART_WATCH_ERROR_PARITY:
        return "parity";
    case UART_WATCH_ERROR_FRAME:
        return "frame";
    default:
        return "frame";
    }
}

static JSValue uart_watch_event_to_js(JSContext *ctx,
                                      const void *data,
                                      void *opaque)
{
    const uart_watch_event_t *event = data;
    esp32_mquickjs_uart_slot_t *slot = opaque;
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    bool rearm = false;

    if (event == NULL) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_ThrowInternalError(ctx, "UART watcher received an invalid event");
    }
    if (uart_watch_lock(slot)) {
        if (slot->allocated &&
            slot->generation == event->port_generation &&
            slot->watch_generation == event->watch_generation) {
            if (event->kind == UART_WATCH_EVENT_READABLE) {
                slot->readable_queued = false;
            }
            rearm = slot->watcher != NULL;
        }
        uart_watch_unlock(slot);
    }

    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "type",
            JS_NewString(ctx,
                         event->kind == UART_WATCH_EVENT_READABLE
                             ? "readable"
                             : "error")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sequence", JS_NewUint32(ctx, event->sequence)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "timestampUs",
            JS_NewInt64(ctx, (int64_t)event->timestamp_us))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    if (event->kind == UART_WATCH_EVENT_READABLE) {
        if (!esp32_mquickjs_set_property_ref(
                ctx, result, "availableBytes",
                JS_NewInt64(ctx, (int64_t)event->available_bytes)) ||
            !esp32_mquickjs_set_property_ref(
                ctx, result, "reason",
                JS_NewString(ctx,
                             event->reason == UART_WATCH_REASON_IDLE
                                 ? "idle"
                                 : "threshold"))) {
            JS_PopGCRef(ctx, &result_ref);
            return JS_EXCEPTION;
        }
    } else if (!esp32_mquickjs_set_property_ref(
                   ctx, result, "code",
                   JS_NewString(ctx, uart_watch_error_name(event->error)))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }

    if (rearm) {
        (void)uart_watch_publish_readable(slot, true);
    }
    return JS_PopGCRef(ctx, &result_ref);
}

JSValue js_uart_port_watch(JSContext *ctx, JSValue *this_val,
                           int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_slot_t *slot = NULL;
    esp32_mquickjs_runtime_t *runtime;
    uint32_t min_bytes = UART_WATCH_DEFAULT_MIN_BYTES;
    uint32_t idle_ms = 0;
    uint32_t capacity = UART_WATCH_DEFAULT_CAPACITY;
    bool include_errors = true;
    JSGCRef queue_ref;
    JSValue *queue_object;
    esp32_mquickjs_event_queue_t *watcher;
    bool task_already_started;
    size_t available = 0;

    if (uart_get_bound_slot(ctx, *this_val, "UARTPort.watch()",
                            &port_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (argc > 1 ||
        (argc == 1 && !JS_IsUndefined(argv[0]) &&
         JS_GetClassID(ctx, argv[0]) < 0)) {
        return JS_ThrowTypeError(
            ctx,
            "UARTPort.watch(options?) expects { minBytes?, idleMs?, capacity?, includeErrors? }");
    }
    if (slot->rx_pin < 0) {
        return JS_ThrowInternalError(
            ctx, "UARTPort.watch() requires an RX GPIO");
    }
    if (argc == 1 && !JS_IsUndefined(argv[0])) {
        JSGCRef property_ref;
        JSValue *property = JS_PushGCRef(ctx, &property_ref);

        *property = JS_GetPropertyStr(ctx, argv[0], "minBytes");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             (!js_value_to_u32(ctx, *property, &min_bytes) ||
              min_bytes == 0))) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(
                ctx, "UARTPort.watch({ minBytes }) expects a positive integer");
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "idleMs");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             !js_value_to_u32(ctx, *property, &idle_ms))) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(
                ctx, "UARTPort.watch({ idleMs }) expects a non-negative integer");
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "capacity");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             (!js_value_to_u32(ctx, *property, &capacity) ||
              capacity == 0 || capacity > UART_WATCH_MAX_CAPACITY))) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowRangeError(
                ctx, "UARTPort.watch({ capacity }) expects an integer in 1..%u",
                UART_WATCH_MAX_CAPACITY);
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "includeErrors");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) &&
             !js_value_to_bool(ctx, *property, &include_errors))) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(
                ctx, "UARTPort.watch({ includeErrors }) expects a boolean-like value");
        }
        JS_PopGCRef(ctx, &property_ref);
    }

    runtime = slot->runtime;
    if (runtime == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "UARTPort.watch() requires an active runtime");
    }
    if (!uart_watch_lock(slot)) {
        return JS_ThrowInternalError(ctx,
                                     "UARTPort.watch() failed to lock watcher state");
    }
    if (slot->watcher != NULL) {
        uart_watch_unlock(slot);
        return JS_ThrowInternalError(
            ctx, "UARTPort.watch() allows only one watcher per port");
    }
    task_already_started = slot->watch_task != NULL;
    uart_watch_unlock(slot);

    if (!task_already_started) {
        uart_event_t ignored;

        while (xQueueReceive(slot->driver_events, &ignored, 0) == pdTRUE) {
        }
    }

    queue_object = JS_PushGCRef(ctx, &queue_ref);
    *queue_object = esp32_mquickjs_event_queue_new(
        ctx,
        runtime,
        sizeof(uart_watch_event_t),
        capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
        uart_watch_event_to_js,
        NULL,
        uart_watch_close_queue,
        slot);
    if (JS_IsException(*queue_object)) {
        JS_PopGCRef(ctx, &queue_ref);
        return JS_EXCEPTION;
    }
    watcher = esp32_mquickjs_event_queue_from_value(ctx, *queue_object);

    if (!uart_watch_lock(slot)) {
        (void)esp32_mquickjs_event_queue_close(watcher);
        JS_PopGCRef(ctx, &queue_ref);
        return JS_ThrowInternalError(ctx,
                                     "UARTPort.watch() failed to lock watcher state");
    }
    slot->watcher = watcher;
    slot->watch_generation++;
    if (slot->watch_generation == 0) {
        slot->watch_generation++;
    }
    slot->watch_min_bytes = min_bytes;
    slot->watch_idle_ms = idle_ms;
    slot->watch_include_errors = include_errors;
    slot->readable_queued = false;
    slot->event_sequence = 0;
    if (uart_get_buffered_data_len((uart_port_t)slot->port_id,
                                   &available) == ESP_OK &&
        available > 0) {
        slot->last_rx_us = (uint64_t)esp_timer_get_time();
    } else {
        slot->last_rx_us = 0;
    }
    uart_watch_unlock(slot);

    if (!uart_start_watch_task(slot)) {
        (void)esp32_mquickjs_event_queue_close(watcher);
        JS_PopGCRef(ctx, &queue_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    if (task_already_started) {
        (void)xTaskAbortDelay(slot->watch_task);
    }
    (void)uart_watch_publish_readable(slot, false);
    return JS_PopGCRef(ctx, &queue_ref);
}

typedef enum {
    UART_WRITE_OK = 0,
    UART_WRITE_CLOSED,
    UART_WRITE_TIMEOUT,
    UART_WRITE_INTERRUPTED,
    UART_WRITE_DRIVER_ERROR,
} esp32_mquickjs_uart_write_result_t;

typedef struct {
    uint64_t deadline_us;
    uint32_t timeout_ms;
    size_t bytes_written;
    bool attempted;
} esp32_mquickjs_uart_write_scope_t;

static void uart_write_scope_begin(
    const esp32_mquickjs_uart_slot_t *slot,
    esp32_mquickjs_uart_write_scope_t *scope)
{
    uint32_t timeout_ms = slot != NULL ? slot->timeout_ms : 0;

    memset(scope, 0, sizeof(*scope));
    scope->timeout_ms = timeout_ms;
    scope->deadline_us = (uint64_t)esp_timer_get_time() +
                         ((uint64_t)timeout_ms * 1000ULL);
}

static JSValue uart_annotate_write_exception(
    JSContext *ctx,
    const esp32_mquickjs_uart_write_scope_t *scope)
{
    JSGCRef error_ref;
    JSValue *error;

    if (!JS_HasException(ctx)) {
        return JS_EXCEPTION;
    }
    error = JS_PushGCRef(ctx, &error_ref);
    *error = JS_GetException(ctx);
    if (JS_GetClassID(ctx, *error) >= 0 &&
        JS_IsException(JS_SetPropertyStr(
            ctx, *error, "bytesWritten",
            JS_NewInt64(ctx, (int64_t)(scope != NULL
                                           ? scope->bytes_written
                                           : 0))))) {
        JS_PopGCRef(ctx, &error_ref);
        return JS_EXCEPTION;
    }
    return JS_Throw(ctx, JS_PopGCRef(ctx, &error_ref));
}

static JSValue uart_write_error(
    JSContext *ctx,
    esp32_mquickjs_uart_write_result_t result,
    const char *api_name,
    const esp32_mquickjs_uart_write_scope_t *scope)
{
    JSValue thrown;

    switch (result) {
    case UART_WRITE_CLOSED:
        thrown = JS_ThrowReferenceError(
            ctx, "%s was interrupted because the UART port was closed",
            api_name);
        break;
    case UART_WRITE_TIMEOUT:
        thrown = JS_ThrowInternalError(
            ctx, "%s timed out after %u ms", api_name,
            (unsigned)(scope != NULL ? scope->timeout_ms : 0));
        break;
    case UART_WRITE_INTERRUPTED:
        thrown = JS_ThrowInternalError(
            ctx, "%s was interrupted by a runtime stop request", api_name);
        break;
    case UART_WRITE_DRIVER_ERROR:
        thrown = JS_ThrowInternalError(ctx, "%s failed", api_name);
        break;
    case UART_WRITE_OK:
    default:
        thrown = JS_ThrowInternalError(ctx, "%s failed", api_name);
        break;
    }
    if (!JS_IsException(thrown) || !JS_HasException(ctx)) {
        return thrown;
    }
    return uart_annotate_write_exception(ctx, scope);
}

static JSValue uart_open(JSContext *ctx, int argc, JSValue *argv)
{
    int32_t port_id = ESP32_MQUICKJS_UART_DEFAULT_PORT;
    int32_t tx_pin = ESP32_MQUICKJS_UART_DEFAULT_TX_PIN;
    int32_t rx_pin = ESP32_MQUICKJS_UART_DEFAULT_RX_PIN;
    uint32_t baud = ESP32_MQUICKJS_UART_DEFAULT_BAUD;
    uart_word_length_t data_bits = UART_DATA_8_BITS;
    uart_parity_t parity = UART_PARITY_DISABLE;
    uart_stop_bits_t stop_bits = UART_STOP_BITS_1;
    uint32_t rx_buffer_size = ESP32_MQUICKJS_UART_DEFAULT_RX_BUFFER_SIZE;
    uint32_t tx_buffer_size = ESP32_MQUICKJS_UART_DEFAULT_TX_BUFFER_SIZE;
    uint32_t timeout_ms = ESP32_MQUICKJS_UART_DEFAULT_TIMEOUT_MS;
    uart_config_t uart_config = {0};
    esp32_mquickjs_uart_slot_t *slot;
    JSValue result;
    esp_err_t err;

    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        JSGCRef property_ref;
        JSValue *property;

        if (JS_GetClassID(ctx, argv[0]) < 0) {
            return JS_ThrowTypeError(ctx,
                                     "uart.open(options?) expects an object with port/tx/rx/baud/dataBits/parity/stopBits");
        }

        property = JS_PushGCRef(ctx, &property_ref);

        *property = JS_GetPropertyStr(ctx, argv[0], "port");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_uart_port(ctx, *property, &port_id)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ port }) expects a valid UART port number");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "tx");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &tx_pin)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ tx }) expects a valid GPIO or -1");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "rx");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &rx_pin)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ rx }) expects a valid GPIO or -1");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "baud");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) &&
            (!js_value_to_u32(ctx, *property, &baud) || baud == 0U)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ baud }) expects a positive integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "dataBits");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_uart_data_bits(ctx, *property, &data_bits)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ dataBits }) expects 5, 6, 7, or 8");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "parity");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_uart_parity(ctx, *property, &parity)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ parity }) expects \"none\", \"even\", or \"odd\"");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "stopBits");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_uart_stop_bits(ctx, *property, &stop_bits)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ stopBits }) expects 1, 1.5, or 2");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "rxBufferSize");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_u32(ctx, *property, &rx_buffer_size)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ rxBufferSize }) expects a non-negative integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "txBufferSize");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_u32(ctx, *property, &tx_buffer_size)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ txBufferSize }) expects a non-negative integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "timeoutMs");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_u32(ctx, *property, &timeout_ms)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ timeoutMs }) expects a non-negative integer");
        }

        JS_PopGCRef(ctx, &property_ref);
    }

    if (tx_pin < 0 && rx_pin < 0) {
        return JS_ThrowTypeError(ctx, "uart.open() requires a TX or RX GPIO");
    }
    if (tx_pin >= 0 && !GPIO_IS_VALID_OUTPUT_GPIO(tx_pin)) {
        return JS_ThrowTypeError(ctx, "uart.open() requires TX to be an output-capable GPIO or -1");
    }
    if (rx_pin >= 0 && !GPIO_IS_VALID_GPIO(rx_pin)) {
        return JS_ThrowTypeError(ctx, "uart.open() requires RX to be a valid GPIO or -1");
    }
    if (rx_buffer_size <= UART_HW_FIFO_LEN((uart_port_t)port_id)) {
        return JS_ThrowTypeError(ctx, "uart.open({ rxBufferSize }) must be greater than the UART hardware FIFO length");
    }
    if (tx_buffer_size != 0U && tx_buffer_size <= UART_HW_FIFO_LEN((uart_port_t)port_id)) {
        return JS_ThrowTypeError(ctx, "uart.open({ txBufferSize }) must be zero or greater than the UART hardware FIFO length");
    }
#if defined(CONFIG_ESP_CONSOLE_UART_NUM) && CONFIG_ESP_CONSOLE_UART_NUM >= 0
    if (port_id == CONFIG_ESP_CONSOLE_UART_NUM) {
        return JS_ThrowInternalError(ctx, "uart.open() refused to take the console UART port");
    }
#endif

    slot = uart_alloc_slot(port_id);
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "uart.open() failed: selected UART port is unavailable or already in use");
    }
    slot->watch_lock = xSemaphoreCreateMutex();
    if (slot->watch_lock == NULL) {
        uart_cleanup_slot(slot);
        return JS_ThrowOutOfMemory(ctx);
    }
    slot->runtime = esp32_mquickjs_get_active_runtime();
    if (slot->runtime == NULL) {
        uart_cleanup_slot(slot);
        return JS_ThrowInternalError(ctx,
                                     "uart.open() requires an active runtime");
    }

    uart_config.baud_rate = (int)baud;
    uart_config.data_bits = data_bits;
    uart_config.parity = parity;
    uart_config.stop_bits = stop_bits;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 0;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    err = uart_param_config((uart_port_t)port_id, &uart_config);
    if (err != ESP_OK) {
        uart_cleanup_slot(slot);
        return uart_throw_error(ctx, err, "uart.open() failed to configure parameters");
    }

    err = uart_set_pin((uart_port_t)port_id,
                       tx_pin,
                       rx_pin,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        uart_cleanup_slot(slot);
        return uart_throw_error(ctx, err, "uart.open() failed to configure pins");
    }

    err = uart_driver_install((uart_port_t)port_id,
                              (int)rx_buffer_size,
                              (int)tx_buffer_size,
                              (int)UART_DRIVER_EVENT_QUEUE_LEN,
                              &slot->driver_events,
                              0);
    if (err != ESP_OK) {
        uart_cleanup_slot(slot);
        return uart_throw_error(ctx, err, "uart.open() failed to install driver");
    }

    slot->tx_pin = tx_pin;
    slot->rx_pin = rx_pin;
    slot->baud = baud;
    slot->data_bits = data_bits;
    slot->parity = parity;
    slot->stop_bits = stop_bits;
    slot->rx_buffer_size = rx_buffer_size;
    slot->tx_buffer_size = tx_buffer_size;
    slot->timeout_ms = timeout_ms;
    uart_set_notifier((uart_port_t)port_id, uart_notify_from_isr);

    result = uart_make_port_object(ctx, slot);
    if (JS_IsException(result)) {
        uart_cleanup_slot(slot);
    }
    return result;
}

void esp32_mquickjs_deinit_uart_runtime(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)SOC_UART_NUM; ++i) {
        if (s_uart_slots[i].allocated) {
            uart_cleanup_slot(&s_uart_slots[i]);
        }
    }
    uart_reset_slots();
}

bool esp32_mquickjs_init_uart_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_deinit_uart_runtime();
    return uart_register_future_drivers(ctx, runtime);
}

JSValue js_uart_port_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "UARTPort cannot be constructed directly");
}

void js_uart_port_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_uart_port_ref_t *port_ref = opaque;
    esp32_mquickjs_uart_slot_t *slot;

    (void)ctx;

    if (port_ref == NULL) {
        return;
    }

    slot = uart_get_slot(port_ref);
    if (slot != NULL) {
        if (slot->future_reservations > 0 || slot->read_busy ||
            slot->write_busy) {
            slot->release_pending = true;
        } else {
            uart_cleanup_slot(slot);
        }
    }
    heap_caps_free(port_ref);
}

JSValue js_uart_port_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_port_ref_t *port_ref_ptr;
    esp32_mquickjs_uart_slot_t *slot;

    (void)argc;
    (void)argv;

    if (uart_port_ref_from_object(ctx, *this_val, "UARTPort.close()", &port_ref) != 0) {
        return JS_EXCEPTION;
    }
    slot = uart_get_slot(&port_ref);
    if (slot != NULL) {
        if (slot->future_reservations > 0 || slot->read_busy ||
            slot->write_busy) {
            return JS_ThrowInternalError(ctx,
                                         "UARTPort.close() refused while an operation is pending");
        }
        uart_cleanup_slot(slot);
    }
    port_ref_ptr = JS_GetOpaque(ctx, *this_val);
    if (port_ref_ptr != NULL) {
        port_ref_ptr->port_id = -1;
        port_ref_ptr->generation = 0;
    }
    return JS_TRUE;
}

JSValue js_uart_port_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_slot_t *slot = NULL;

    (void)argc;
    (void)argv;

    if (uart_get_bound_slot(ctx, *this_val, "UARTPort.status()", &port_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    return uart_make_status_object(ctx, slot);
}

JSValue js_uart_port_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return uart_future_call_and_wait(ctx, *this_val, "write", argc, argv);
}

JSValue js_uart_port_write_chunks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return uart_future_call_and_wait(ctx, *this_val,
                                     "writeChunks", argc, argv);
}

JSValue js_uart_port_write_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return uart_future_call_and_wait(ctx, *this_val,
                                     "writeSource", argc, argv);
}

typedef enum {
    UART_FUTURE_READ,
    UART_FUTURE_FLUSH,
    UART_FUTURE_WRITE,
    UART_FUTURE_WRITE_CHUNKS,
    UART_FUTURE_WRITE_SOURCE,
} uart_future_kind_t;

struct esp32_mquickjs_future_driver_state {
    uart_future_kind_t kind;
    JSContext *ctx;
    JSGCRef owner_ref;
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    esp_timer_handle_t poll_timer;
    uint64_t deadline_us;
    uint32_t timeout_ms;
    uint32_t length;
    uint8_t *data;
    const uint8_t *write_data;
    uint8_t *write_owned;
    size_t write_length;
    int read_length;
    esp_err_t err;
    esp32_mquickjs_uart_write_result_t write_result;
    esp32_mquickjs_uart_write_scope_t write_scope;
    uint64_t write_started_us;
    size_t write_offset;
    uint32_t write_chunks;
    uint32_t chunk_count;
    uint32_t chunk_index;
    esp32_mquickjs_byte_source_chunk_t chunk;
    esp32_mquickjs_byte_span_source_t span_source;
    esp32_mquickjs_byte_span_t span;
    JSGCRef value_ref;
    bool value_retained;
    bool byte_view_leased;
    bool chunk_active;
    bool chunk_byte_view_leased;
    bool span_source_opened;
    bool source_failed;
    bool owner_retained;
    bool reservation_held;
    bool started;
    _Atomic bool completed;
    bool cancelled;
};

static bool uart_future_is_completed(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && atomic_load_explicit(
                                &state->completed,
                                memory_order_acquire);
}

static void uart_future_mark_completed(
    esp32_mquickjs_future_driver_state_t *state)
{
    atomic_store_explicit(&state->completed, true, memory_order_release);
}

static esp32_mquickjs_future_driver_state_t *uart_future_allocate(
    JSContext *ctx,
    JSValue this_value,
    uart_future_kind_t kind)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_uart_slot_t *slot = NULL;
    JSValue *owner;

    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    if (uart_get_bound_slot(ctx, this_value, "UART Future operation",
                            &state->port_ref, &slot) != 0) {
        heap_caps_free(state);
        return NULL;
    }
    state->kind = kind;
    state->ctx = ctx;
    state->timeout_ms = slot->timeout_ms;
    atomic_init(&state->completed, false);
    slot->future_reservations++;
    state->reservation_held = true;
    owner = JS_AddGCRef(ctx, &state->owner_ref);
    *owner = this_value;
    state->owner_retained = true;
    return state;
}

static void uart_future_release(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_uart_slot_t *slot;

    if (state == NULL) {
        return;
    }
    if (state->poll_timer != NULL) {
        (void)esp_timer_stop(state->poll_timer);
        (void)esp_timer_delete(state->poll_timer);
    }
    if (state->owner_retained) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
    }
    if (state->span_source_opened) {
        esp32_mquickjs_byte_span_source_close(state->ctx,
                                               &state->span_source);
    }
    if (state->chunk_byte_view_leased && state->chunk.rooted) {
        esp32_mquickjs_byte_view_release_read(state->ctx,
                                              state->chunk.value_ref.val);
    }
    if (state->chunk_active || state->chunk.rooted ||
        state->chunk.owned != NULL) {
        esp32_mquickjs_release_byte_source_chunk(state->ctx, &state->chunk);
    }
    if (state->byte_view_leased && state->value_retained) {
        esp32_mquickjs_byte_view_release_read(state->ctx,
                                              state->value_ref.val);
    }
    if (state->value_retained) {
        JS_DeleteGCRef(state->ctx, &state->value_ref);
    }
    heap_caps_free(state->data);
    heap_caps_free(state->write_owned);
    slot = state->reservation_held ? uart_get_slot(&state->port_ref) : NULL;
    if (slot != NULL) {
        if (slot->future_reservations > 0) {
            slot->future_reservations--;
        }
        if (slot->release_pending && slot->future_reservations == 0 &&
            !slot->read_busy && !slot->write_busy) {
            uart_cleanup_slot(slot);
        }
    }
    heap_caps_free(state);
}

static const char *uart_future_write_api(uart_future_kind_t kind)
{
    switch (kind) {
    case UART_FUTURE_WRITE:
        return "UARTPort.write()";
    case UART_FUTURE_WRITE_CHUNKS:
        return "UARTPort.writeChunks()";
    case UART_FUTURE_WRITE_SOURCE:
        return "UARTPort.writeSource()";
    default:
        return "UART operation";
    }
}

static bool uart_write_future_prepare_common(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    uart_future_kind_t kind,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_uart_slot_t *slot;
    JSValue error = JS_UNDEFINED;

    if (out_state == NULL || argc != 1) {
        JS_ThrowTypeError(ctx, "%s expects one data argument",
                          uart_future_write_api(kind));
        return false;
    }
    state = uart_future_allocate(ctx, this_ref->val, kind);
    if (state == NULL) {
        return false;
    }
    slot = uart_get_slot(&state->port_ref);
    if (slot == NULL) {
        uart_future_release(state);
        JS_ThrowReferenceError(ctx, "UART port closed during Future capture");
        return false;
    }
    state->write_result = UART_WRITE_OK;
    esp32_mquickjs_byte_span_clear(&state->span);

    if (kind == UART_FUTURE_WRITE) {
        esp32_mquickjs_byte_source_t source;

        if (JS_GetClassID(ctx, argv[0].val) == JS_CLASS_BYTE_VIEW) {
            JSValue *value = JS_AddGCRef(ctx, &state->value_ref);

            *value = argv[0].val;
            state->value_retained = true;
            if (!esp32_mquickjs_byte_view_acquire_read(
                    ctx, state->value_ref.val, "UARTPort.write(data)",
                    &state->write_data, &state->write_length)) {
                uart_future_release(state);
                return false;
            }
            state->byte_view_leased = true;
        } else if (!esp32_mquickjs_get_byte_source(
                       ctx, argv[0].val, "UARTPort.write(data)",
                       &source, &state->write_owned, &error)) {
            uart_future_release(state);
            return false;
        } else {
            state->write_data = source.data;
            state->write_length = source.length;
        }
        if (state->write_length > INT_MAX) {
            uart_future_release(state);
            JS_ThrowRangeError(
                ctx,
                "UARTPort.write() byte length exceeds supported UART write size");
            return false;
        }
    } else if (kind == UART_FUTURE_WRITE_CHUNKS) {
        JSValue *value = JS_AddGCRef(ctx, &state->value_ref);

        *value = argv[0].val;
        state->value_retained = true;
        if (!esp32_mquickjs_get_byte_source_array_length(
                ctx, state->value_ref.val,
                "UARTPort.writeChunks(chunks)", &state->chunk_count,
                &error)) {
            uart_future_release(state);
            return false;
        }
    } else {
        if (!esp32_mquickjs_open_byte_span_source(
                ctx, argv[0].val, "UARTPort.writeSource(source)",
                &state->span_source, &error)) {
            uart_future_release(state);
            return false;
        }
        state->span_source_opened = true;
    }
    *out_state = state;
    return true;
}

#define UART_WRITE_FUTURE_PREPARE(name, kind_value)                         \
    static bool name(                                                       \
        JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,         \
        esp32_mquickjs_future_driver_state_t **out_state)                   \
    {                                                                       \
        return uart_write_future_prepare_common(                            \
            ctx, this_ref, argc, argv, kind_value, out_state);              \
    }

UART_WRITE_FUTURE_PREPARE(uart_write_future_prepare, UART_FUTURE_WRITE)
UART_WRITE_FUTURE_PREPARE(uart_write_chunks_future_prepare,
                          UART_FUTURE_WRITE_CHUNKS)
UART_WRITE_FUTURE_PREPARE(uart_write_source_future_prepare,
                          UART_FUTURE_WRITE_SOURCE)

static bool uart_read_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    if (out_state == NULL || argc < 1 || argc > 2) {
        JS_ThrowTypeError(ctx,
                          "UARTPort.read(length, timeoutMs?) expects a byte length");
        return false;
    }
    state = uart_future_allocate(ctx, this_ref->val, UART_FUTURE_READ);
    if (state == NULL) {
        return false;
    }
    if (!js_value_to_u32(ctx, argv[0].val, &state->length) ||
        (argc >= 2 && !JS_IsUndefined(argv[1].val) &&
         !js_value_to_u32(ctx, argv[1].val, &state->timeout_ms))) {
        uart_future_release(state);
        JS_ThrowTypeError(ctx,
                          "UARTPort.read(length, timeoutMs?) expects non-negative integers");
        return false;
    }
    if (state->length > 0) {
        state->data = esp32_mquickjs_memory_payload_alloc(
            state->length, ESP32_MQUICKJS_MEMORY_EXTERNAL);
        if (state->data == NULL) {
            uart_future_release(state);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
    }
    *out_state = state;
    return true;
}

static bool uart_flush_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    if (out_state == NULL || argc > 1) {
        JS_ThrowTypeError(ctx, "UARTPort.flush(timeoutMs?) expected");
        return false;
    }
    state = uart_future_allocate(ctx, this_ref->val, UART_FUTURE_FLUSH);
    if (state == NULL) {
        return false;
    }
    if (argc == 1 && !JS_IsUndefined(argv[0].val) &&
        !js_value_to_u32(ctx, argv[0].val, &state->timeout_ms)) {
        uart_future_release(state);
        JS_ThrowTypeError(ctx,
                          "UARTPort.flush(timeoutMs?) expects timeoutMs to be non-negative");
        return false;
    }
    *out_state = state;
    return true;
}

static bool uart_future_is_write(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL &&
           (state->kind == UART_FUTURE_WRITE ||
            state->kind == UART_FUTURE_WRITE_CHUNKS ||
            state->kind == UART_FUTURE_WRITE_SOURCE);
}

static void uart_future_release_chunk(
    esp32_mquickjs_future_driver_state_t *state,
    bool completed)
{
    if (state == NULL || !state->chunk_active) {
        return;
    }
    if (state->chunk_byte_view_leased && state->chunk.rooted) {
        esp32_mquickjs_byte_view_release_read(state->ctx,
                                              state->chunk.value_ref.val);
    }
    state->chunk_byte_view_leased = false;
    esp32_mquickjs_release_byte_source_chunk(state->ctx, &state->chunk);
    state->chunk_active = false;
    state->write_offset = 0;
    if (completed) {
        state->write_chunks++;
    }
}

static bool uart_future_next_write_data(
    esp32_mquickjs_future_driver_state_t *state,
    const uint8_t **out_data,
    size_t *out_length)
{
    JSValue error = JS_UNDEFINED;

    if (state == NULL || out_data == NULL || out_length == NULL) {
        return false;
    }
    if (state->kind == UART_FUTURE_WRITE) {
        if (state->write_offset >= state->write_length) {
            uart_future_mark_completed(state);
            return false;
        }
        *out_data = state->write_data;
        *out_length = state->write_length;
        return true;
    }
    if (state->kind == UART_FUTURE_WRITE_CHUNKS) {
        for (;;) {
            if (state->chunk_active &&
                state->write_offset < state->chunk.source.length) {
                *out_data = state->chunk.source.data;
                *out_length = state->chunk.source.length;
                return true;
            }
            uart_future_release_chunk(state, state->chunk_active);
            if (state->chunk_index >= state->chunk_count) {
                uart_future_mark_completed(state);
                return false;
            }
            if (!esp32_mquickjs_get_byte_source_chunk(
                    state->ctx, state->value_ref.val, state->chunk_index++,
                    "UARTPort.writeChunks(chunks)", &state->chunk,
                    &error)) {
                state->source_failed = true;
                uart_future_mark_completed(state);
                return false;
            }
            state->chunk_active = true;
            if (JS_GetClassID(state->ctx, state->chunk.value_ref.val) ==
                JS_CLASS_BYTE_VIEW) {
                if (!esp32_mquickjs_byte_view_acquire_read(
                        state->ctx, state->chunk.value_ref.val,
                        "UARTPort.writeChunks(chunks)",
                        &state->chunk.source.data,
                        &state->chunk.source.length)) {
                    state->source_failed = true;
                    uart_future_mark_completed(state);
                    return false;
                }
                state->chunk_byte_view_leased = true;
            }
            if (state->chunk.source.length > INT_MAX) {
                JS_ThrowRangeError(
                    state->ctx,
                    "UARTPort.writeChunks(chunks) byte length exceeds supported UART write size");
                state->source_failed = true;
                uart_future_mark_completed(state);
                return false;
            }
            if (state->chunk.source.length == 0) {
                uart_future_release_chunk(state, false);
            }
        }
    }
    for (;;) {
        if (state->write_offset < state->span.length) {
            *out_data = state->span.data;
            *out_length = state->span.length;
            return true;
        }
        if (state->span.length > 0) {
            state->write_chunks++;
        }
        esp32_mquickjs_byte_span_clear(&state->span);
        state->write_offset = 0;
        if (!esp32_mquickjs_byte_span_source_next(
                state->ctx, &state->span_source, &state->span)) {
            state->source_failed = JS_HasException(state->ctx);
            uart_future_mark_completed(state);
            return false;
        }
        if (state->span.length > INT_MAX) {
            JS_ThrowRangeError(
                state->ctx,
                "UARTPort.writeSource() span length exceeds supported UART write size");
            state->source_failed = true;
            uart_future_mark_completed(state);
            return false;
        }
        if (state->span.length > 0 && state->span.data == NULL) {
            JS_ThrowInternalError(
                state->ctx,
                "UARTPort.writeSource() received a non-empty span with null data");
            state->source_failed = true;
            uart_future_mark_completed(state);
            return false;
        }
    }
}

static void uart_write_future_step(
    esp32_mquickjs_future_driver_state_t *state,
    esp32_mquickjs_uart_slot_t *slot)
{
    size_t budget = 1024U;

    while (budget > 0 && !uart_future_is_completed(state)) {
        const uint8_t *data = NULL;
        size_t length = 0;
        size_t remaining;
        size_t chunk;
        int written = 0;
        uint64_t now_us;

        if (!uart_future_next_write_data(state, &data, &length)) {
            return;
        }
        if (data == NULL) {
            JS_ThrowInternalError(
                state->ctx, "%s received non-empty byte data with null data",
                uart_future_write_api(state->kind));
            state->source_failed = true;
            uart_future_mark_completed(state);
            return;
        }
        now_us = (uint64_t)esp_timer_get_time();
        if ((state->write_scope.attempted || state->timeout_ms > 0) &&
            now_us >= state->write_scope.deadline_us) {
            state->write_result = UART_WRITE_TIMEOUT;
            uart_future_mark_completed(state);
            return;
        }
        remaining = length - state->write_offset;
        chunk = remaining < budget ? remaining : budget;
        if (slot->tx_buffer_size > 0) {
            size_t free_size = 0;

            if (uart_get_tx_buffer_free_size((uart_port_t)slot->port_id,
                                             &free_size) != ESP_OK) {
                state->write_result = UART_WRITE_DRIVER_ERROR;
                uart_future_mark_completed(state);
                return;
            }
            if (chunk > free_size) {
                chunk = free_size;
            }
            if (chunk > 0) {
                written = uart_write_bytes((uart_port_t)slot->port_id,
                                           data + state->write_offset,
                                           chunk);
            }
        } else {
            written = uart_tx_chars((uart_port_t)slot->port_id,
                                    (const char *)(data + state->write_offset),
                                    (uint32_t)chunk);
        }
        state->write_scope.attempted = true;
        if (written < 0) {
            state->write_result = UART_WRITE_DRIVER_ERROR;
            uart_future_mark_completed(state);
            return;
        }
        if (written == 0) {
            if ((uint64_t)esp_timer_get_time() >=
                state->write_scope.deadline_us) {
                state->write_result = UART_WRITE_TIMEOUT;
                uart_future_mark_completed(state);
            }
            return;
        }
        state->write_offset += (size_t)written;
        state->write_scope.bytes_written += (size_t)written;
        budget -= (size_t)written;
    }
    if (!uart_future_is_completed(state)) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
}

static void uart_future_step(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_uart_slot_t *slot;

    if (state == NULL || uart_future_is_completed(state) || state->cancelled) {
        return;
    }
    slot = uart_get_slot(&state->port_ref);
    if (slot == NULL) {
        state->err = ESP_ERR_INVALID_STATE;
        uart_future_mark_completed(state);
        return;
    }
    if (uart_future_is_write(state)) {
        uart_write_future_step(state, slot);
    } else if (state->kind == UART_FUTURE_READ) {
        size_t available = 0;

        state->err = uart_get_buffered_data_len((uart_port_t)slot->port_id,
                                                &available);
        if (state->err != ESP_OK) {
            uart_future_mark_completed(state);
        } else if (state->length == 0 || available > 0) {
            size_t wanted = available < state->length ? available : state->length;

            state->read_length = wanted == 0 ? 0 : uart_read_bytes(
                (uart_port_t)slot->port_id,
                state->data,
                wanted,
                0);
            if (state->read_length < 0) {
                state->err = ESP_FAIL;
            }
            uart_future_mark_completed(state);
        }
    } else {
        state->err = uart_wait_tx_done((uart_port_t)slot->port_id, 0);
        if (state->err == ESP_OK) {
            uart_future_mark_completed(state);
        } else if (state->err == ESP_ERR_TIMEOUT) {
            state->err = ESP_OK;
        } else {
            uart_future_mark_completed(state);
        }
    }
    if (!uart_future_is_completed(state) && (state->timeout_ms == 0 ||
        (state->deadline_us > 0 &&
         (uint64_t)esp_timer_get_time() >= state->deadline_us))) {
        if (state->kind == UART_FUTURE_READ) {
            state->read_length = 0;
            state->err = ESP_OK;
        } else if (uart_future_is_write(state)) {
            state->write_result = UART_WRITE_TIMEOUT;
        } else {
            state->err = ESP_ERR_TIMEOUT;
        }
        uart_future_mark_completed(state);
    }
}

static void uart_future_timer(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state != NULL && !uart_future_is_completed(state)) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
}

static bool uart_future_start(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              esp32_mquickjs_future_token_t token,
                              esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_uart_slot_t *slot = state != NULL
        ? uart_get_slot(&state->port_ref) : NULL;
    bool write_lane = state != NULL && state->kind != UART_FUTURE_READ;
    esp_timer_create_args_t timer_args = {
        .callback = uart_future_timer,
        .arg = state,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mqjs_uart",
        .skip_unhandled_events = true,
    };

    if (state == NULL || slot == NULL) {
        JS_ThrowReferenceError(ctx, "UART port closed before operation start");
        return false;
    }
    if (write_lane) {
        if (slot->write_busy) {
            JS_ThrowInternalError(
                ctx, "UART TX lane started more than one operation");
            return false;
        }
        slot->write_busy = true;
        if (uart_future_is_write(state)) {
            uart_write_scope_begin(slot, &state->write_scope);
            state->write_started_us = (uint64_t)esp_timer_get_time();
        }
    } else {
        if (slot->read_busy) {
            JS_ThrowInternalError(
                ctx, "UART RX lane started more than one operation");
            return false;
        }
        slot->read_busy = true;
    }
    uart_set_future_waiter(slot, runtime, token, write_lane, true);
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (state->timeout_ms > 0) {
        state->deadline_us = (uint64_t)esp_timer_get_time() +
                             (uint64_t)state->timeout_ms * 1000ULL;
    }
    uart_future_step(state);
    if (!uart_future_is_completed(state)) {
        uint64_t timeout_us = (uint64_t)state->timeout_ms * 1000ULL;
        esp_err_t timer_err = esp_timer_create(&timer_args,
                                               &state->poll_timer);

        if (timer_err == ESP_OK) {
            if (state->kind == UART_FUTURE_READ) {
                timer_err = esp_timer_start_once(state->poll_timer,
                                                 timeout_us);
            } else {
                /* ESP-IDF exposes ring-buffer write readiness through the
                 * select callback, but not final hardware TX-done. Keep the
                 * flush probe bounded and let its timer only wake the Future. */
                uint64_t maximum_period_us = uart_future_is_write(state)
                                                 ? UART_WRITE_FIFO_POLL_MS * 1000ULL
                                                 : UART_FLUSH_POLL_US;
                uint64_t period_us = timeout_us < maximum_period_us
                                         ? timeout_us
                                         : maximum_period_us;

                timer_err = esp_timer_start_periodic(state->poll_timer,
                                                     period_us);
            }
        }
        if (timer_err != ESP_OK) {
            JS_ThrowInternalError(ctx,
                                  "failed to start UART readiness timer");
            return false;
        }
    }
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static esp32_mquickjs_future_poll_t uart_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    uart_future_step(state);
    return uart_future_is_completed(state)
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue uart_future_finish(JSContext *ctx,
                                  esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->cancelled) {
        return JS_ThrowInternalError(ctx, "UART operation cancelled");
    }
    if (uart_future_is_write(state)) {
        if (state->source_failed || JS_HasException(ctx)) {
            return state->write_scope.bytes_written > 0
                       ? uart_annotate_write_exception(
                             ctx, &state->write_scope)
                       : JS_EXCEPTION;
        }
        if (state->write_result != UART_WRITE_OK) {
            return uart_write_error(ctx, state->write_result,
                                    uart_future_write_api(state->kind),
                                    &state->write_scope);
        }
        if (state->kind == UART_FUTURE_WRITE) {
            return JS_NewInt64(
                ctx, (int64_t)state->write_scope.bytes_written);
        }
        return uart_make_write_stats(
            ctx, state->write_chunks,
            state->write_scope.bytes_written,
            (uint64_t)esp_timer_get_time() - state->write_started_us);
    }
    if (state->err != ESP_OK) {
        return uart_throw_error(ctx, state->err,
                                state->kind == UART_FUTURE_READ
                                    ? "UARTPort.read() failed"
                                    : "UARTPort.flush() failed");
    }
    if (state->kind == UART_FUTURE_READ) {
        if (state->length > 0 && state->read_length == 0) {
            return JS_NULL;
        }
        JSValue result = esp32_mquickjs_new_owned_byte_view(
            ctx, state->data, (size_t)state->read_length);

        if (!JS_IsException(result)) {
            state->data = NULL;
        }
        return result;
    }
    return JS_TRUE;
}

static esp32_mquickjs_cancel_result_t uart_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || uart_future_is_completed(state) || state->cancelled) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancelled = true;
    uart_future_mark_completed(state);
    if (state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
    return ESP32_MQUICKJS_CANCELLED;
}

static void uart_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_uart_slot_t *slot = state != NULL
        ? uart_get_slot(&state->port_ref) : NULL;
    bool write_lane = state != NULL && state->kind != UART_FUTURE_READ;

    if (slot != NULL && state->started) {
        if (write_lane) {
            slot->write_busy = false;
        } else {
            slot->read_busy = false;
        }
        uart_set_future_waiter(slot, NULL, state->token, write_lane, false);
    }
    uart_future_release(state);
}

static esp32_mquickjs_resource_key_t uart_future_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_uart_slot_t *slot = state != NULL
        ? uart_get_slot(&state->port_ref) : NULL;

    if (slot == NULL) {
        return NULL;
    }
    return state->kind == UART_FUTURE_READ
               ? (esp32_mquickjs_resource_key_t)&slot->rx_lane_key
               : (esp32_mquickjs_resource_key_t)&slot->tx_lane_key;
}

static const esp32_mquickjs_future_driver_t s_uart_read_driver = {
    .capture = uart_read_future_prepare,
    .start = uart_future_start,
    .poll = uart_future_poll,
    .finish = uart_future_finish,
    .cancel = uart_future_cancel,
    .destroy = uart_future_destroy,
    .resource_key = uart_future_resource_key,
};

static const esp32_mquickjs_future_driver_t s_uart_flush_driver = {
    .capture = uart_flush_future_prepare,
    .start = uart_future_start,
    .poll = uart_future_poll,
    .finish = uart_future_finish,
    .cancel = uart_future_cancel,
    .destroy = uart_future_destroy,
    .resource_key = uart_future_resource_key,
};

#define UART_WRITE_FUTURE_DRIVER(name, prepare_fn)              \
    static const esp32_mquickjs_future_driver_t name = {        \
        .capture = prepare_fn,                                  \
        .start = uart_future_start,                              \
        .poll = uart_future_poll,                                \
        .finish = uart_future_finish,                            \
        .cancel = uart_future_cancel,                            \
        .destroy = uart_future_destroy,                          \
        .resource_key = uart_future_resource_key,                \
    }

UART_WRITE_FUTURE_DRIVER(s_uart_write_driver, uart_write_future_prepare);
UART_WRITE_FUTURE_DRIVER(s_uart_write_chunks_driver,
                         uart_write_chunks_future_prepare);
UART_WRITE_FUTURE_DRIVER(s_uart_write_source_driver,
                         uart_write_source_future_prepare);

static JSValue uart_future_call_and_wait(JSContext *ctx,
                                         JSValue receiver,
                                         const char *method_name,
                                         int argc,
                                         JSValue *argv)
{
    JSGCRef receiver_ref;
    JSGCRef method_ref;
    JSValue *rooted_receiver = JS_PushGCRef(ctx, &receiver_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *rooted_receiver = receiver;
    *method = JS_GetPropertyStr(ctx, *rooted_receiver, method_name);
    result = JS_IsException(*method)
        ? JS_EXCEPTION
        : esp32_mquickjs_future_call_and_wait(ctx,
                                              esp32_mquickjs_get_active_runtime(),
                                              *method,
                                              *rooted_receiver,
                                              argc,
                                              argv);
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &receiver_ref);
    return result;
}

static bool uart_register_future_drivers(JSContext *ctx,
                                         esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef object_ref;
    JSGCRef write_ref;
    JSGCRef write_chunks_ref;
    JSGCRef write_source_ref;
    JSGCRef read_ref;
    JSGCRef flush_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *write_fn = JS_PushGCRef(ctx, &write_ref);
    JSValue *write_chunks_fn = JS_PushGCRef(ctx, &write_chunks_ref);
    JSValue *write_source_fn = JS_PushGCRef(ctx, &write_source_ref);
    JSValue *read_fn = JS_PushGCRef(ctx, &read_ref);
    JSValue *flush_fn = JS_PushGCRef(ctx, &flush_ref);
    bool result;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_UART_PORT);
    *write_fn = JS_IsException(*object)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *object, "write");
    *write_chunks_fn = JS_IsException(*object)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *object, "writeChunks");
    *write_source_fn = JS_IsException(*object)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *object, "writeSource");
    *read_fn = JS_IsException(*object)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *object, "read");
    *flush_fn = JS_IsException(*object)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *object, "flush");
    result = !JS_IsException(*write_fn) &&
             !JS_IsException(*write_chunks_fn) &&
             !JS_IsException(*write_source_fn) &&
             !JS_IsException(*read_fn) && !JS_IsException(*flush_fn) &&
             esp32_mquickjs_future_register_driver(
                 ctx, runtime, *write_fn, &s_uart_write_driver) &&
             esp32_mquickjs_future_register_driver(
                 ctx, runtime, *write_chunks_fn,
                 &s_uart_write_chunks_driver) &&
             esp32_mquickjs_future_register_driver(
                 ctx, runtime, *write_source_fn,
                 &s_uart_write_source_driver) &&
             esp32_mquickjs_future_register_driver(ctx, runtime,
                                                   *read_fn, &s_uart_read_driver) &&
             esp32_mquickjs_future_register_driver(ctx, runtime,
                                                   *flush_fn, &s_uart_flush_driver);
    if (!result && !JS_IsException(*object)) {
        JS_ThrowInternalError(ctx, "failed to register UART Future drivers");
    }
    JS_PopGCRef(ctx, &flush_ref);
    JS_PopGCRef(ctx, &read_ref);
    JS_PopGCRef(ctx, &write_source_ref);
    JS_PopGCRef(ctx, &write_chunks_ref);
    JS_PopGCRef(ctx, &write_ref);
    JS_PopGCRef(ctx, &object_ref);
    return result;
}

JSValue js_uart_port_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return uart_future_call_and_wait(ctx, *this_val, "read", argc, argv);
}

JSValue js_uart_port_available(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_slot_t *slot = NULL;
    size_t available = 0;
    esp_err_t err;

    (void)argc;
    (void)argv;

    if (uart_get_bound_slot(ctx, *this_val, "UARTPort.available()", &port_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    err = uart_get_buffered_data_len((uart_port_t)slot->port_id, &available);
    if (err != ESP_OK) {
        return uart_throw_error(ctx, err, "UARTPort.available() failed");
    }
    return JS_NewInt64(ctx, (int64_t)available);
}

JSValue js_uart_port_flush(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return uart_future_call_and_wait(ctx, *this_val, "flush", argc, argv);
}

JSValue js_uart_port_clear_rx(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_slot_t *slot = NULL;
    esp_err_t err;

    (void)argc;
    (void)argv;

    if (uart_get_bound_slot(ctx, *this_val, "UARTPort.clearRx()", &port_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    err = uart_flush_input((uart_port_t)slot->port_id);
    if (err != ESP_OK) {
        return uart_throw_error(ctx, err, "UARTPort.clearRx() failed");
    }
    return JS_TRUE;
}

JSValue js_uart_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return uart_open(ctx, argc, argv);
}

JSValue js_uart_get_default_port(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_UART_DEFAULT_PORT);
}

JSValue js_uart_get_default_tx(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_UART_DEFAULT_TX_PIN);
}

JSValue js_uart_get_default_rx(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_UART_DEFAULT_RX_PIN);
}

JSValue js_uart_get_default_baud(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_UART_DEFAULT_BAUD);
}

JSValue js_uart_get_default_rx_buffer_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_UART_DEFAULT_RX_BUFFER_SIZE);
}

JSValue js_uart_get_default_tx_buffer_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_UART_DEFAULT_TX_BUFFER_SIZE);
}

JSValue js_uart_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_UART_DEFAULT_TIMEOUT_MS);
}

#endif
