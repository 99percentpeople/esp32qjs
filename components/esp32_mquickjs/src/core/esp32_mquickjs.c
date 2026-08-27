#include "esp32_mquickjs_core.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32_mquickjs_sys.h"
#include "esp32_mquickjs_adc.h"
#include "esp32_mquickjs_dac.h"
#include "esp32_mquickjs_fs.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_gpio.h"
#include "esp32_mquickjs_http.h"
#include "esp32_mquickjs_http_server.h"
#include "esp32_mquickjs_i2c.h"
#include "esp32_mquickjs_i2s.h"
#include "esp32_mquickjs_rmt.h"
#include "esp32_mquickjs_camera.h"
#include "esp32_mquickjs_bitmap.h"
#include "esp32_mquickjs_ledc.h"
#include "esp32_mquickjs_log_ring.h"
#include "esp32_mquickjs_nvs.h"
#include "esp32_mquickjs_net.h"
#include "esp32_mquickjs_peripheral_lease.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_RPC
#include "esp32_mquickjs_rpc.h"
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
#include "esp32_mquickjs_runtime_logs.h"
#endif
#include "esp32_mquickjs_spi.h"
#include "esp32_mquickjs_stream.h"
#include "esp32_mquickjs_time.h"
#include "esp32_mquickjs_socket.h"
#include "esp32_mquickjs_uart.h"
#include "esp32_mquickjs_usb_serial.h"
#include "esp32_mquickjs_websocket.h"
#include "esp32_mquickjs_wifi.h"
#include "js_stdlib.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static esp32_mquickjs_runtime_t *s_active_runtime;

static void esp32_mquickjs_ensure_boot_id(esp32_mquickjs_runtime_t *runtime)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t bytes[ESP32_MQUICKJS_BOOT_ID_LENGTH / 2U];
    size_t i;

    if (runtime == NULL || runtime->boot_id[0] != '\0') {
        return;
    }
    esp_fill_random(bytes, sizeof(bytes));
    for (i = 0; i < sizeof(bytes); ++i) {
        runtime->boot_id[i * 2U] = hex[bytes[i] >> 4U];
        runtime->boot_id[(i * 2U) + 1U] = hex[bytes[i] & 0x0fU];
    }
    runtime->boot_id[ESP32_MQUICKJS_BOOT_ID_LENGTH] = '\0';
}

typedef struct esp32_mquickjs_timer_slot esp32_mquickjs_timer_slot_t;

typedef struct {
    uint8_t timer_id;
    uint32_t generation;
} esp32_mquickjs_timer_event_t;

typedef struct {
    QueueHandle_t queue;
    esp32_mquickjs_timer_slot_t *slots;
} esp32_mquickjs_timer_state_t;

typedef struct {
    esp32_mquickjs_async_poller_t poller;
    void *opaque;
} esp32_mquickjs_async_poller_entry_t;

#define ESP32_MQUICKJS_IDLE_JOB_CAPACITY 8
#define ESP32_MQUICKJS_NATIVE_GC_DEBT_BYTES 2048U

typedef struct {
    JSGCRef callback;
    bool allocated;
} esp32_mquickjs_idle_job_t;

typedef struct {
    void *task_handle;
    size_t poller_count;
    esp32_mquickjs_async_poller_entry_t *pollers;
    esp32_mquickjs_idle_job_t idle_jobs[ESP32_MQUICKJS_IDLE_JOB_CAPACITY];
    uint8_t idle_head;
    uint8_t idle_tail;
    uint8_t idle_count;
    uint32_t execution_depth;
    size_t js_owned_native_bytes;
    size_t native_gc_debt_bytes;
    bool native_gc_pending;
} esp32_mquickjs_async_state_t;

static esp32_mquickjs_timer_state_t *esp32_mquickjs_timer_state(
    esp32_mquickjs_runtime_t *runtime);

struct esp32_mquickjs_timer_slot {
    esp32_mquickjs_runtime_t *runtime;
    esp_timer_handle_t handle;
    JSGCRef callback;
    uint32_t generation;
    uint8_t timer_id;
    bool allocated;
    bool repeating;
    bool pending;
};

#define ESP32_MQUICKJS_MAX_ASYNC_POLLERS 8
#define ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS 6U
#define ESP32_MQUICKJS_TIMER_HANDLE_ID_MASK ((1U << ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS) - 1U)
#define ESP32_MQUICKJS_TIMER_HANDLE_MAX \
    (((uint64_t)UINT32_MAX << ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS) | \
     ESP32_MQUICKJS_TIMER_HANDLE_ID_MASK)

static void note_console_output(void)
{
    if (s_active_runtime != NULL) {
        s_active_runtime->output_generation++;
    }
}

static esp32_mquickjs_async_state_t *esp32_mquickjs_async_state(esp32_mquickjs_runtime_t *runtime)
{
    if (runtime == NULL) {
        return NULL;
    }
    return runtime->async_state;
}

static void esp32_mquickjs_execution_enter(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state != NULL) {
        state->execution_depth++;
    }
}

static void esp32_mquickjs_execution_leave(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state != NULL && state->execution_depth > 0) {
        state->execution_depth--;
    }
}

static bool esp32_mquickjs_execution_active(
    esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    return state != NULL && state->execution_depth != 0;
}

void esp32_mquickjs_native_gc_alloc(esp32_mquickjs_runtime_t *runtime,
                                    size_t size)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state == NULL || size == 0) {
        return;
    }
    if (SIZE_MAX - state->js_owned_native_bytes < size) {
        state->js_owned_native_bytes = SIZE_MAX;
    } else {
        state->js_owned_native_bytes += size;
    }
    if (SIZE_MAX - state->native_gc_debt_bytes < size) {
        state->native_gc_debt_bytes = SIZE_MAX;
    } else {
        state->native_gc_debt_bytes += size;
    }
}

void esp32_mquickjs_native_gc_free(esp32_mquickjs_runtime_t *runtime,
                                   size_t size)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state == NULL || size == 0) {
        return;
    }
    state->js_owned_native_bytes =
        size < state->js_owned_native_bytes
            ? state->js_owned_native_bytes - size
            : 0;
}

void esp32_mquickjs_native_gc_reclaimable(
    esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state != NULL) {
        state->native_gc_pending = true;
    }
}

static bool native_gc_due(esp32_mquickjs_runtime_t *runtime,
                          esp32_mquickjs_async_state_t *state)
{
    esp32_mquickjs_memory_status_t memory_status;

    if (state == NULL || !state->native_gc_pending) {
        return false;
    }
    if (!esp32_mquickjs_execution_active(runtime) ||
        state->native_gc_debt_bytes >= ESP32_MQUICKJS_NATIVE_GC_DEBT_BYTES) {
        return true;
    }
    esp32_mquickjs_memory_get_status(&memory_status);
    return memory_status.pressure != ESP32_MQUICKJS_MEMORY_PRESSURE_NORMAL;
}

static void run_pending_gc(JSContext *ctx,
                           esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);
    bool requested;
    bool native_due;

    if (ctx == NULL) {
        return;
    }
    requested = esp32_mquickjs_byte_source_take_gc_request();
    native_due = native_gc_due(runtime, state);
    if (!requested && !native_due) {
        return;
    }
    if (state != NULL) {
        state->native_gc_pending = false;
        state->native_gc_debt_bytes = 0;
    }
    JS_GC(ctx);
}

static JSValue esp32_mquickjs_finish_execution(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    JSValue result)
{
    JSGCRef result_ref;
    JSValue *rooted_result = JS_PushGCRef(ctx, &result_ref);

    *rooted_result = result;
    esp32_mquickjs_execution_leave(runtime);
    run_pending_gc(ctx, runtime);
    return JS_PopGCRef(ctx, &result_ref);
}

static void esp32_mquickjs_clear_idle_jobs(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state == NULL) {
        return;
    }
    while (state->idle_count > 0) {
        esp32_mquickjs_idle_job_t *job = &state->idle_jobs[state->idle_head];

        if (job->allocated && ctx != NULL) {
            JS_DeleteGCRef(ctx, &job->callback);
        }
        job->allocated = false;
        state->idle_head = (uint8_t)(
            (state->idle_head + 1U) % ESP32_MQUICKJS_IDLE_JOB_CAPACITY);
        state->idle_count--;
    }
    state->idle_head = 0;
    state->idle_tail = 0;
}

static bool esp32_mquickjs_poll_idle_job(JSContext *ctx,
                                         esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);
    esp32_mquickjs_idle_job_t *job;
    JSGCRef callback_ref;
    JSValue *callback;
    JSValue result;

    if (ctx == NULL || state == NULL || state->execution_depth != 0 ||
        state->idle_count == 0) {
        return false;
    }

    job = &state->idle_jobs[state->idle_head];
    if (!job->allocated) {
        state->idle_head = (uint8_t)(
            (state->idle_head + 1U) % ESP32_MQUICKJS_IDLE_JOB_CAPACITY);
        state->idle_count--;
        return true;
    }

    callback = JS_PushGCRef(ctx, &callback_ref);
    *callback = job->callback.val;
    JS_DeleteGCRef(ctx, &job->callback);
    job->allocated = false;
    state->idle_head = (uint8_t)(
        (state->idle_head + 1U) % ESP32_MQUICKJS_IDLE_JOB_CAPACITY);
    state->idle_count--;

    result = esp32_mquickjs_call(ctx, runtime, *callback, JS_NULL, 0, NULL);
    if (JS_IsException(result)) {
        esp32_mquickjs_print_exception(ctx);
    }
    JS_PopGCRef(ctx, &callback_ref);
    return true;
}

static bool esp32_mquickjs_init_async_state(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state;

    if (runtime == NULL) {
        return false;
    }
    if (runtime->async_state != NULL) {
        return true;
    }

    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        return false;
    }

    state->pollers = heap_caps_calloc(ESP32_MQUICKJS_MAX_ASYNC_POLLERS,
                                      sizeof(*state->pollers),
                                      MALLOC_CAP_8BIT);
    if (state->pollers == NULL) {
        heap_caps_free(state);
        return false;
    }

    runtime->async_state = state;
    return true;
}

bool esp32_mquickjs_register_async_poller(esp32_mquickjs_runtime_t *runtime,
                                          esp32_mquickjs_async_poller_t poller,
                                          void *opaque)
{
    esp32_mquickjs_async_state_t *state;
    size_t i;

    if (runtime == NULL || poller == NULL) {
        return false;
    }
    if (!esp32_mquickjs_init_async_state(runtime)) {
        return false;
    }

    state = esp32_mquickjs_async_state(runtime);
    if (state == NULL || state->pollers == NULL) {
        return false;
    }

    for (i = 0; i < state->poller_count; ++i) {
        if (state->pollers[i].poller == poller && state->pollers[i].opaque == opaque) {
            return true;
        }
    }
    if (state->poller_count >= ESP32_MQUICKJS_MAX_ASYNC_POLLERS) {
        return false;
    }

    state->pollers[state->poller_count].poller = poller;
    state->pollers[state->poller_count].opaque = opaque;
    state->poller_count++;
    return true;
}

void esp32_mquickjs_attach_current_task(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state;

    if (runtime == NULL || !esp32_mquickjs_init_async_state(runtime)) {
        return;
    }

    state = esp32_mquickjs_async_state(runtime);
    if (state != NULL) {
        state->task_handle = xTaskGetCurrentTaskHandle();
    }
}

void esp32_mquickjs_detach_current_task(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state != NULL) {
        state->task_handle = NULL;
    }
}

void esp32_mquickjs_notify_activity(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state != NULL && state->task_handle != NULL) {
        xTaskNotifyGive(state->task_handle);
    }
}

void esp32_mquickjs_notify_active_runtime_from_isr(int *task_woken)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(s_active_runtime);

    if (state != NULL && state->task_handle != NULL) {
        vTaskNotifyGiveFromISR(state->task_handle, (BaseType_t *)task_woken);
    }
}

void esp32_mquickjs_set_cooperate_hook(esp32_mquickjs_runtime_t *runtime,
                                       esp32_mquickjs_cooperate_fn cooperate,
                                       void *opaque)
{
    if (runtime == NULL) {
        return;
    }
    runtime->cooperate = cooperate;
    runtime->cooperate_opaque = opaque;
}

bool esp32_mquickjs_cooperate(esp32_mquickjs_runtime_t *runtime)
{
    if (runtime == NULL) {
        return true;
    }
    if (runtime->scoped_deadline_us > 0 &&
        (uint64_t)esp_timer_get_time() >= runtime->scoped_deadline_us) {
        return false;
    }
    return runtime->cooperate == NULL || runtime->cooperate(runtime->cooperate_opaque);
}

void esp32_mquickjs_native_wait_begin(esp32_mquickjs_runtime_t *runtime,
                                      esp32_mquickjs_native_wait_t *wait)
{
    if (wait == NULL) {
        return;
    }
    wait->saved_deadline_us = runtime != NULL ? runtime->deadline_us : 0;
    wait->started_us = (uint64_t)esp_timer_get_time();
    wait->active = runtime != NULL;
    if (runtime != NULL) {
        runtime->deadline_us = 0;
        if (runtime->native_wait_depth < UINT16_MAX) {
            runtime->native_wait_depth++;
        }
    }
}

void esp32_mquickjs_native_wait_end(esp32_mquickjs_runtime_t *runtime,
                                    esp32_mquickjs_native_wait_t *wait)
{
    uint64_t elapsed_us;

    if (runtime == NULL || wait == NULL || !wait->active) {
        return;
    }
    wait->active = false;
    if (runtime->native_wait_depth > 0) {
        runtime->native_wait_depth--;
    }
    elapsed_us = (uint64_t)esp_timer_get_time() - wait->started_us;
    if (wait->saved_deadline_us == 0) {
        runtime->deadline_us = 0;
    } else if (wait->saved_deadline_us > UINT64_MAX - elapsed_us) {
        runtime->deadline_us = UINT64_MAX;
    } else {
        runtime->deadline_us = wait->saved_deadline_us + elapsed_us;
    }
    wait->saved_deadline_us = 0;
    wait->started_us = 0;
}

static uint32_t esp32_mquickjs_bound_wait_slice(
    const esp32_mquickjs_runtime_t *runtime,
    uint32_t slice_ms)
{
    uint64_t now_us;
    uint64_t remaining_us;
    uint32_t remaining_ms;

    if (runtime == NULL || runtime->scoped_deadline_us == 0) {
        return slice_ms;
    }
    now_us = (uint64_t)esp_timer_get_time();
    if (now_us >= runtime->scoped_deadline_us) {
        return 0;
    }
    remaining_us = runtime->scoped_deadline_us - now_us;
    remaining_ms = (uint32_t)((remaining_us + 999ULL) / 1000ULL);
    return remaining_ms < slice_ms ? remaining_ms : slice_ms;
}

bool esp32_mquickjs_cooperative_delay(esp32_mquickjs_runtime_t *runtime,
                                      uint32_t delay_ms)
{
    esp32_mquickjs_native_wait_t wait;
    uint32_t remaining_ms = delay_ms;
    bool completed = true;

    esp32_mquickjs_native_wait_begin(runtime, &wait);
    do {
        uint32_t slice_ms;
        TickType_t wait_ticks;

        if (!esp32_mquickjs_cooperate(runtime)) {
            completed = false;
            break;
        }
        (void)esp32_mquickjs_future_cooperate(runtime);
        if (remaining_ms == 0) {
            break;
        }
        slice_ms = remaining_ms > ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS
                       ? ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS
                       : remaining_ms;
        slice_ms = esp32_mquickjs_bound_wait_slice(runtime, slice_ms);
        if (slice_ms == 0) {
            completed = false;
            break;
        }
        wait_ticks = pdMS_TO_TICKS(slice_ms);
        if (wait_ticks == 0) {
            wait_ticks = 1;
        }
        vTaskDelay(wait_ticks);
        remaining_ms -= slice_ms;
    } while (remaining_ms > 0);

    if (completed && !esp32_mquickjs_cooperate(runtime)) {
        completed = false;
    }
    esp32_mquickjs_native_wait_end(runtime, &wait);
    return completed;
}

bool esp32_mquickjs_wait_for_activity(esp32_mquickjs_runtime_t *runtime,
                                      uint32_t timeout_ms)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);
    uint32_t remaining_ms;

    timeout_ms = esp32_mquickjs_future_next_wait_ms(runtime, timeout_ms);
    remaining_ms = timeout_ms;

    for (;;) {
        uint32_t slice_ms;
        TickType_t wait_ticks;
        bool notified;

        if (!esp32_mquickjs_cooperate(runtime)) {
            return false;
        }
        if (timeout_ms == UINT32_MAX) {
            slice_ms = ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS;
        } else {
            slice_ms = remaining_ms > ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS
                           ? ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS
                           : remaining_ms;
        }
        slice_ms = esp32_mquickjs_bound_wait_slice(runtime, slice_ms);
        if (slice_ms == 0) {
            return false;
        }
        wait_ticks = pdMS_TO_TICKS(slice_ms);
        if (slice_ms > 0 && wait_ticks == 0) {
            wait_ticks = 1;
        }

        if (state == NULL || state->task_handle == NULL) {
            if (wait_ticks > 0) {
                vTaskDelay(wait_ticks);
            }
            notified = false;
        } else {
            notified = ulTaskNotifyTake(pdTRUE, wait_ticks) > 0;
        }
        if (!esp32_mquickjs_cooperate(runtime)) {
            return false;
        }
        if (notified) {
            return true;
        }
        if (timeout_ms == UINT32_MAX) {
            continue;
        }
        if (remaining_ms <= slice_ms) {
            return false;
        }
        remaining_ms -= slice_ms;
    }
}

static bool esp32_mquickjs_poll_registered(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);
    bool handled = false;
    size_t i;

    if (state == NULL || state->pollers == NULL) {
        return false;
    }

    for (i = 0; i < state->poller_count; ++i) {
        if (state->pollers[i].poller != NULL &&
            state->pollers[i].poller(ctx, runtime, state->pollers[i].opaque)) {
            handled = true;
        }
    }
    return handled;
}

static void prepare_console_output(void)
{
    if (s_active_runtime != NULL && s_active_runtime->prepare_output != NULL) {
        s_active_runtime->prepare_output(s_active_runtime->prepare_output_opaque);
    }
}

static void console_output_begin(esp32_mquickjs_runtime_t *runtime,
                                 esp32_mquickjs_log_source_t source)
{
    prepare_console_output();
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
    esp32_mquickjs_runtime_logs_console_begin(runtime, source);
#else
    (void)runtime;
    (void)source;
#endif
}

static void console_output_write(esp32_mquickjs_runtime_t *runtime,
                                 const void *buf,
                                 size_t buf_len)
{
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
    esp32_mquickjs_runtime_logs_console_write(runtime, buf, buf_len);
#else
    (void)runtime;
    fwrite(buf, 1, buf_len, stdout);
#endif
}

static void console_output_end(esp32_mquickjs_runtime_t *runtime)
{
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
    esp32_mquickjs_runtime_logs_console_end(runtime);
#else
    (void)runtime;
    fputc('\n', stdout);
    fflush(stdout);
#endif
    note_console_output();
}

static void js_log_write(void *opaque, const void *buf, size_t buf_len)
{
    console_output_write(opaque, buf, buf_len);
}

static int js_interrupt_handler(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_runtime_t *runtime = opaque;

    (void)ctx;
    if (runtime == NULL) {
        return 0;
    }
    if (!esp32_mquickjs_cooperate(runtime)) {
        return 1;
    }
    return runtime->deadline_us != 0 && esp_timer_get_time() > runtime->deadline_us;
}

JSValue esp32_mquickjs_call(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            JSValue function,
                            JSValue this_value,
                            int argc,
                            JSValue *argv)
{
    uint64_t previous_deadline = runtime != NULL ? runtime->deadline_us : 0;
    JSGCRef function_ref;
    JSGCRef this_ref;
    JSGCRef *argument_refs = NULL;
    JSValue *rooted_function;
    JSValue *rooted_this;
    JSValue result = JS_EXCEPTION;
    int i;

    if (ctx == NULL || argc < 0 || (argc > 0 && argv == NULL)) {
        return JS_EXCEPTION;
    }
    if (argc > 0) {
        argument_refs = heap_caps_calloc((size_t)argc,
                                         sizeof(*argument_refs),
                                         MALLOC_CAP_8BIT);
        if (argument_refs == NULL) {
            return JS_ThrowOutOfMemory(ctx);
        }
    }

    rooted_function = JS_PushGCRef(ctx, &function_ref);
    rooted_this = JS_PushGCRef(ctx, &this_ref);
    *rooted_function = function;
    *rooted_this = this_value;
    for (i = 0; i < argc; ++i) {
        JSValue *rooted_argument = JS_PushGCRef(ctx, &argument_refs[i]);

        *rooted_argument = argv[i];
    }

    if (JS_StackCheck(ctx, (uint32_t)(argc + 2))) {
        goto done;
    }
    if (runtime != NULL && runtime->eval_timeout_ms > 0) {
        uint64_t call_deadline = esp_timer_get_time() +
                                 ((uint64_t)runtime->eval_timeout_ms * 1000ULL);

        if (previous_deadline == 0 || call_deadline < previous_deadline) {
            runtime->deadline_us = call_deadline;
        }
    }

    for (i = argc - 1; i >= 0; --i) {
        JS_PushArg(ctx, argument_refs[i].val);
    }
    JS_PushArg(ctx, *rooted_function);
    JS_PushArg(ctx, *rooted_this);
    esp32_mquickjs_execution_enter(runtime);
    result = JS_Call(ctx, argc);
    result = esp32_mquickjs_finish_execution(ctx, runtime, result);

    if (runtime != NULL) {
        runtime->deadline_us = previous_deadline;
    }

done:
    for (i = argc - 1; i >= 0; --i) {
        JS_PopGCRef(ctx, &argument_refs[i]);
    }
    JS_PopGCRef(ctx, &this_ref);
    JS_PopGCRef(ctx, &function_ref);
    heap_caps_free(argument_refs);
    return result;
}

bool esp32_mquickjs_set_property(JSContext *ctx,
                                 JSValue target_obj,
                                 const char *name,
                                 JSValue value)
{
    return !JS_IsException(JS_SetPropertyStr(ctx, target_obj, name, value));
}

bool esp32_mquickjs_set_property_ref(JSContext *ctx,
                                     JSValue *target_obj,
                                     const char *name,
                                     JSValue value)
{
    return target_obj != NULL &&
           !JS_IsException(JS_SetPropertyStr(ctx, *target_obj, name, value));
}

esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void)
{
    return s_active_runtime;
}

void esp32_mquickjs_set_system_hooks(esp32_mquickjs_runtime_t *runtime,
                                     esp32_mquickjs_host_status_fn status,
                                     esp32_mquickjs_system_control_fn control,
                                     void *opaque)
{
    if (runtime == NULL) {
        return;
    }
    runtime->host_status = status;
    runtime->system_control = control;
    runtime->system_opaque = opaque;
}

void esp32_mquickjs_set_safe_mode_hook(
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_safe_mode_control_fn control)
{
    if (runtime != NULL) {
        runtime->safe_mode_control = control;
    }
}

bool esp32_mquickjs_set_safe_mode(esp32_mquickjs_runtime_t *runtime,
                                  bool enabled)
{
    return runtime != NULL && runtime->safe_mode_control != NULL &&
           runtime->safe_mode_control(runtime->system_opaque, enabled);
}

bool esp32_mquickjs_get_host_status(esp32_mquickjs_runtime_t *runtime,
                                    esp32_mquickjs_host_status_t *status)
{
    if (runtime == NULL || status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    if (runtime->host_status != NULL) {
        return runtime->host_status(runtime->system_opaque, status);
    }
    status->state = ESP32_MQUICKJS_RUNTIME_RUNNING;
    status->generation = 1U;
    status->generation_started_us = runtime->context_started_us;
    status->littlefs_mounted = runtime->littlefs_mounted;
    snprintf(status->fs_root,
             sizeof(status->fs_root),
             "%s",
             ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    return true;
}

bool esp32_mquickjs_request_system_control(
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_control_action_t action,
    const char *reason,
    uint32_t delay_ms,
    esp32_mquickjs_control_receipt_t *receipt,
    esp32_mquickjs_control_result_t *result)
{
    esp32_mquickjs_control_result_t control_result;

    if (result != NULL) {
        *result = ESP32_MQUICKJS_CONTROL_UNAVAILABLE;
    }
    if (runtime == NULL || reason == NULL || receipt == NULL ||
        runtime->system_control == NULL) {
        return false;
    }
    control_result = runtime->system_control(runtime->system_opaque,
                                             action,
                                             reason,
                                             delay_ms,
                                             receipt);
    if (result != NULL) {
        *result = control_result;
    }
    return control_result == ESP32_MQUICKJS_CONTROL_ACCEPTED;
}

bool esp32_mquickjs_get_resource_status(
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_resource_status_t *status)
{
    esp32_mquickjs_timer_state_t *timer_state;
    esp32_mquickjs_async_state_t *async_state;
    esp32_mquickjs_future_status_t future_status;
    esp32_mquickjs_event_queue_status_t event_status;
    int i;

    if (runtime == NULL || status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    status->timers_capacity = ESP32_MQUICKJS_MAX_TIMERS;
    status->async_pollers_capacity = ESP32_MQUICKJS_MAX_ASYNC_POLLERS;
    timer_state = esp32_mquickjs_timer_state(runtime);
    if (timer_state != NULL && timer_state->slots != NULL) {
        for (i = 0; i < ESP32_MQUICKJS_MAX_TIMERS; ++i) {
            if (timer_state->slots[i].allocated) {
                status->timers_active++;
            }
        }
    }
    async_state = esp32_mquickjs_async_state(runtime);
    if (async_state != NULL) {
        status->async_pollers_registered = (uint32_t)async_state->poller_count;
    }
    if (!esp32_mquickjs_get_future_status(runtime, &future_status) ||
        !esp32_mquickjs_get_event_queue_status(runtime, &event_status)) {
        return false;
    }
    status->futures_queued = future_status.queued;
    status->futures_pending = future_status.pending;
    status->futures_capacity = future_status.capacity;
    status->futures_user_capacity = future_status.user_capacity;
    status->futures_internal_reserve = future_status.internal_reserve;
    status->event_queues_open = event_status.open;
    status->event_queues_dropped = event_status.dropped;
    return true;
}

static esp32_mquickjs_timer_state_t *esp32_mquickjs_timer_state(esp32_mquickjs_runtime_t *runtime)
{
    if (runtime == NULL) {
        return NULL;
    }
    return runtime->timer_state;
}

static bool esp32_mquickjs_init_timer_state(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_timer_state_t *state;
    esp32_mquickjs_timer_slot_t *slots;
    int i;

    if (runtime == NULL) {
        return false;
    }
    if (runtime->timer_state != NULL) {
        return true;
    }

    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    slots = heap_caps_calloc(ESP32_MQUICKJS_MAX_TIMERS, sizeof(*slots), MALLOC_CAP_8BIT);
    if (state == NULL || slots == NULL) {
        heap_caps_free(state);
        heap_caps_free(slots);
        return false;
    }

    state->queue = xQueueCreate(ESP32_MQUICKJS_TIMER_QUEUE_LEN, sizeof(esp32_mquickjs_timer_event_t));
    if (state->queue == NULL) {
        heap_caps_free(slots);
        heap_caps_free(state);
        return false;
    }

    state->slots = slots;
    for (i = 0; i < ESP32_MQUICKJS_MAX_TIMERS; ++i) {
        slots[i].runtime = runtime;
        slots[i].timer_id = (uint8_t)i;
    }

    runtime->timer_state = state;
    return true;
}

static void esp32_mquickjs_timer_cb(void *arg)
{
    esp32_mquickjs_timer_slot_t *slot = arg;
    esp32_mquickjs_timer_state_t *state;
    esp32_mquickjs_timer_event_t event;

    if (slot == NULL || !slot->allocated || slot->pending) {
        return;
    }

    state = esp32_mquickjs_timer_state(slot->runtime);
    if (state == NULL || state->queue == NULL) {
        return;
    }

    event.timer_id = slot->timer_id;
    event.generation = slot->generation;
    if (xQueueSend(state->queue, &event, 0) == pdTRUE) {
        slot->pending = true;
        esp32_mquickjs_notify_activity(slot->runtime);
    }
}

static JSValue js_value_to_delay_ms(JSContext *ctx,
                                     int argc,
                                     JSValue *argv,
                                     int arg_index,
                                     bool repeating)
{
    int delay_ms = 0;

    if (argc > arg_index && JS_ToInt32(ctx, &delay_ms, argv[arg_index]) != 0) {
        return JS_EXCEPTION;
    }
    if (delay_ms < 0) {
        return JS_ThrowRangeError(ctx, "timer delay must be non-negative");
    }
    if (repeating && delay_ms < CONFIG_ESP32_MQUICKJS_MIN_INTERVAL_MS) {
        delay_ms = CONFIG_ESP32_MQUICKJS_MIN_INTERVAL_MS;
    } else if (delay_ms == 0) {
        delay_ms = 1;
    }
    return JS_NewInt32(ctx, delay_ms);
}

static uint64_t esp32_mquickjs_timer_handle(const esp32_mquickjs_timer_slot_t *slot)
{
    return ((uint64_t)slot->generation << ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS) |
           (uint64_t)slot->timer_id;
}

static bool esp32_mquickjs_decode_timer_handle(JSContext *ctx,
                                                JSValue value,
                                                uint8_t *out_timer_id,
                                                uint32_t *out_generation)
{
    double raw_handle;
    uint64_t handle;

    if (JS_ToNumber(ctx, &raw_handle, value) != 0 ||
        !(raw_handle > 0) ||
        raw_handle > (double)ESP32_MQUICKJS_TIMER_HANDLE_MAX) {
        return false;
    }
    handle = (uint64_t)raw_handle;
    if ((double)handle != raw_handle) {
        return false;
    }
    *out_timer_id = (uint8_t)(handle & ESP32_MQUICKJS_TIMER_HANDLE_ID_MASK);
    *out_generation = (uint32_t)(handle >> ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS);
    return *out_generation != 0 &&
           *out_timer_id < ESP32_MQUICKJS_MAX_TIMERS &&
           handle == (((uint64_t)*out_generation << ESP32_MQUICKJS_TIMER_HANDLE_ID_BITS) |
                      (uint64_t)*out_timer_id);
}

static void esp32_mquickjs_cancel_timer(JSContext *ctx, esp32_mquickjs_timer_slot_t *slot)
{
    if (slot == NULL || !slot->allocated) {
        return;
    }

    if (slot->handle != NULL) {
        esp_timer_stop(slot->handle);
        esp_timer_delete(slot->handle);
        slot->handle = NULL;
    }

    JS_DeleteGCRef(ctx, &slot->callback);
    slot->allocated = false;
    slot->repeating = false;
    slot->pending = false;
}

static void esp32_mquickjs_deinit_timer_state(JSContext *ctx,
                                               esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_timer_state_t *state = esp32_mquickjs_timer_state(runtime);
    int i;

    if (state == NULL) {
        return;
    }
    if (state->slots != NULL) {
        for (i = 0; i < ESP32_MQUICKJS_MAX_TIMERS; ++i) {
            esp32_mquickjs_timer_slot_t *slot = &state->slots[i];

            if (slot->allocated && ctx != NULL) {
                esp32_mquickjs_cancel_timer(ctx, slot);
            } else if (slot->handle != NULL) {
                esp_timer_stop(slot->handle);
                esp_timer_delete(slot->handle);
                slot->handle = NULL;
            }
        }
    }
    if (state->queue != NULL) {
        vQueueDelete(state->queue);
    }
    heap_caps_free(state->slots);
    heap_caps_free(state);
    runtime->timer_state = NULL;
}

static void esp32_mquickjs_deinit_async_state(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(runtime);

    if (state == NULL) {
        return;
    }
    heap_caps_free(state->pollers);
    heap_caps_free(state);
    runtime->async_state = NULL;
}

static JSValue esp32_mquickjs_create_timer(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime,
                                           JSValue *callback,
                                           JSValue delay_value,
                                           bool repeating)
{
    esp32_mquickjs_timer_state_t *state = esp32_mquickjs_timer_state(runtime);
    esp32_mquickjs_timer_slot_t *slot = NULL;
    esp_timer_create_args_t timer_args = {0};
    JSValue delay_js;
    JSValue *pfunc;
    int delay_ms = 0;
    int i;

    if (!JS_IsFunction(ctx, *callback)) {
        return JS_ThrowTypeError(ctx, "timer callback must be a function");
    }
    if (state == NULL || state->slots == NULL) {
        return JS_ThrowInternalError(ctx, "timer state is not initialized");
    }

    delay_js = js_value_to_delay_ms(ctx, 1, &delay_value, 0, repeating);
    if (JS_IsException(delay_js)) {
        return delay_js;
    }
    if (JS_ToInt32(ctx, &delay_ms, delay_js) != 0) {
        return JS_EXCEPTION;
    }

    for (i = 0; i < ESP32_MQUICKJS_MAX_TIMERS; ++i) {
        if (!state->slots[i].allocated) {
            slot = &state->slots[i];
            break;
        }
    }
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "too many timers");
    }

    slot->generation++;
    if (slot->generation == 0) {
        slot->generation++;
    }
    slot->pending = false;
    slot->repeating = repeating;
    slot->allocated = true;
    pfunc = JS_AddGCRef(ctx, &slot->callback);
    *pfunc = *callback;

    timer_args.callback = esp32_mquickjs_timer_cb;
    timer_args.arg = slot;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = repeating ? "mqjs_interval" : "mqjs_timeout";
    timer_args.skip_unhandled_events = true;

    if (esp_timer_create(&timer_args, &slot->handle) != ESP_OK) {
        JS_DeleteGCRef(ctx, &slot->callback);
        slot->allocated = false;
        slot->repeating = false;
        return JS_ThrowInternalError(ctx, "esp_timer_create() failed");
    }

    if ((repeating ? esp_timer_start_periodic(slot->handle, (uint64_t)delay_ms * 1000ULL)
                   : esp_timer_start_once(slot->handle, (uint64_t)delay_ms * 1000ULL)) != ESP_OK) {
        esp_timer_delete(slot->handle);
        slot->handle = NULL;
        JS_DeleteGCRef(ctx, &slot->callback);
        slot->allocated = false;
        slot->repeating = false;
        return JS_ThrowInternalError(ctx, "failed to start timer");
    }

    return JS_NewInt64(ctx, (int64_t)esp32_mquickjs_timer_handle(slot));
}

JSContext *esp32_mquickjs_create(void *mem_start,
                                 size_t mem_size,
                                 esp32_mquickjs_runtime_t *runtime,
                                 uint32_t eval_timeout_ms)
{
    JSContext *ctx;
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
    bool initialized_runtime_logs = false;
#endif

    if (runtime == NULL || s_active_runtime != NULL) {
        return NULL;
    }

    esp32_mquickjs_peripheral_leases_reset();

    runtime->deadline_us = 0;
    runtime->eval_timeout_ms = eval_timeout_ms;
    runtime->async_generation = 0;
    runtime->output_generation = 0;
    runtime->littlefs_mounted = false;
    runtime->scoped_deadline_us = 0;
    runtime->native_wait_depth = 0;
    runtime->load_root_depth = 0;
    snprintf(runtime->startup_fs_root,
             sizeof(runtime->startup_fs_root),
             "%s",
             ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
    runtime->load_root[0] = '\0';
    runtime->repl_enabled = false;
    runtime->auto_run_startup_script = false;
    runtime->format_littlefs_on_mount_fail = false;
#ifdef CONFIG_ESP32QJS_ENABLE_REPL
    runtime->repl_enabled = true;
#endif
#ifdef CONFIG_ESP32QJS_AUTORUN_INDEX_JS
    runtime->auto_run_startup_script = true;
#endif
#ifdef CONFIG_ESP32QJS_LITTLEFS_FORMAT_ON_MOUNT_FAIL
    runtime->format_littlefs_on_mount_fail = true;
#endif
    runtime->prepare_output = NULL;
    runtime->prepare_output_opaque = NULL;
    runtime->cooperate = NULL;
    runtime->cooperate_opaque = NULL;
    runtime->timer_state = NULL;
    runtime->async_state = NULL;
    runtime->future_state = NULL;
    runtime->event_queue_state = NULL;
    runtime->fs_state = NULL;
    runtime->startup_bytecode = NULL;
    runtime->context_started_us = (uint64_t)esp_timer_get_time();
    esp32_mquickjs_ensure_boot_id(runtime);
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
    if (runtime->runtime_log_state == NULL) {
        if (!esp32_mquickjs_init_runtime_logs(runtime)) {
            return NULL;
        }
        initialized_runtime_logs = true;
    }
#else
    runtime->runtime_log_state = NULL;
#endif
    if (!esp32_mquickjs_init_async_state(runtime)) {
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
        if (initialized_runtime_logs) {
            esp32_mquickjs_deinit_runtime_logs(runtime);
        }
#endif
        return NULL;
    }
    if (!esp32_mquickjs_init_timer_state(runtime)) {
        esp32_mquickjs_deinit_async_state(runtime);
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
        if (initialized_runtime_logs) {
            esp32_mquickjs_deinit_runtime_logs(runtime);
        }
#endif
        return NULL;
    }

    ctx = JS_NewContext(mem_start, mem_size, &js_stdlib);
    if (ctx == NULL) {
        esp32_mquickjs_deinit_timer_state(NULL, runtime);
        esp32_mquickjs_deinit_async_state(runtime);
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
        if (initialized_runtime_logs) {
            esp32_mquickjs_deinit_runtime_logs(runtime);
        }
#endif
        return NULL;
    }

    JS_SetContextOpaque(ctx, runtime);
    JS_SetLogFunc(ctx, js_log_write);
    JS_SetInterruptHandler(ctx, js_interrupt_handler);
    JS_SetRandomSeed(ctx, (uint64_t)esp_timer_get_time());
    s_active_runtime = runtime;
    if (!esp32_mquickjs_init_future_runtime(ctx, runtime)) {
        s_active_runtime = NULL;
        JS_FreeContext(ctx);
        esp32_mquickjs_deinit_timer_state(NULL, runtime);
        esp32_mquickjs_deinit_async_state(runtime);
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
        if (initialized_runtime_logs) {
            esp32_mquickjs_deinit_runtime_logs(runtime);
        }
#endif
        return NULL;
    }
    if (!esp32_mquickjs_init_event_queue_runtime(ctx, runtime)) {
        esp32_mquickjs_deinit_future_runtime(runtime);
        s_active_runtime = NULL;
        JS_FreeContext(ctx);
        esp32_mquickjs_deinit_timer_state(NULL, runtime);
        esp32_mquickjs_deinit_async_state(runtime);
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
        if (initialized_runtime_logs) {
            esp32_mquickjs_deinit_runtime_logs(runtime);
        }
#endif
        return NULL;
    }
    return ctx;
}

static bool esp32_mquickjs_destroy_internal(JSContext *ctx,
                                            esp32_mquickjs_runtime_t *runtime,
                                            bool preserve_persistent_state)
{
    if (runtime == NULL) {
        return false;
    }
    if (!esp32_mquickjs_prepare_future_runtime_destroy(ctx, runtime)) {
        return false;
    }
    esp32_mquickjs_clear_idle_jobs(ctx, runtime);

#if CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET
    esp32_mquickjs_deinit_socket_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET
    esp32_mquickjs_deinit_websocket_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    if (!esp32_mquickjs_deinit_http_runtime(ctx)) {
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL
    esp32_mquickjs_deinit_usb_serial_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_RPC
    esp32_mquickjs_deinit_rpc_runtime();
#endif
    if (s_active_runtime == runtime) {
        s_active_runtime = NULL;
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    esp32_mquickjs_deinit_http_server_runtime(ctx);
#endif
    esp32_mquickjs_deinit_time_runtime();
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    esp32_mquickjs_deinit_wifi_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_NET
    esp32_mquickjs_deinit_net_runtime(runtime);
#endif
    esp32_mquickjs_deinit_stream_runtime();
#if CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP
    esp32_mquickjs_deinit_bitmap_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
    esp32_mquickjs_deinit_camera_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_I2S
    esp32_mquickjs_deinit_i2s_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_RMT
    esp32_mquickjs_deinit_rmt_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_GPIO
    esp32_mquickjs_deinit_gpio_runtime(ctx);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_ADC
    esp32_mquickjs_deinit_adc_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_DAC
    esp32_mquickjs_deinit_dac_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_LEDC
    esp32_mquickjs_deinit_ledc_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C
    esp32_mquickjs_deinit_i2c_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_SPI
    esp32_mquickjs_deinit_spi_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_UART
    esp32_mquickjs_deinit_uart_runtime();
#endif

    esp32_mquickjs_deinit_timer_state(ctx, runtime);
    if (ctx != NULL) {
        JS_FreeContext(ctx);
        esp32_mquickjs_memory_release_generation();
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
    esp32_mquickjs_deinit_fs_runtime(runtime);
#endif
    esp32_mquickjs_deinit_event_queue_runtime(runtime);
    heap_caps_free(runtime->startup_bytecode);
    runtime->startup_bytecode = NULL;
    esp32_mquickjs_deinit_future_runtime(runtime);
    esp32_mquickjs_deinit_async_state(runtime);
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
    if (!preserve_persistent_state) {
        esp32_mquickjs_deinit_runtime_logs(runtime);
    }
#endif
    runtime->deadline_us = 0;
    runtime->scoped_deadline_us = 0;
    runtime->native_wait_depth = 0;
    runtime->littlefs_mounted = false;
    runtime->load_root_depth = 0;
    runtime->startup_fs_root[0] = '\0';
    runtime->load_root[0] = '\0';
    runtime->repl_enabled = false;
    runtime->auto_run_startup_script = false;
    runtime->format_littlefs_on_mount_fail = false;
    runtime->prepare_output = NULL;
    runtime->prepare_output_opaque = NULL;
    runtime->cooperate = NULL;
    runtime->cooperate_opaque = NULL;
    runtime->context_started_us = 0;
    return true;
}

bool esp32_mquickjs_destroy(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime)
{
    bool destroyed = esp32_mquickjs_destroy_internal(ctx, runtime, false);

    if (destroyed && runtime != NULL) {
        runtime->host_status = NULL;
        runtime->system_control = NULL;
        runtime->safe_mode_control = NULL;
        runtime->system_opaque = NULL;
    }
    return destroyed;
}

bool esp32_mquickjs_destroy_generation(JSContext *ctx,
                                       esp32_mquickjs_runtime_t *runtime)
{
    return esp32_mquickjs_destroy_internal(ctx, runtime, true);
}

void esp32_mquickjs_release_persistent_state(esp32_mquickjs_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
    esp32_mquickjs_deinit_runtime_logs(runtime);
#endif
    runtime->host_status = NULL;
    runtime->system_control = NULL;
    runtime->safe_mode_control = NULL;
    runtime->system_opaque = NULL;
}

void esp32_mquickjs_set_eval_timeout(esp32_mquickjs_runtime_t *runtime,
                                     uint32_t eval_timeout_ms)
{
    if (runtime == NULL) {
        return;
    }
    runtime->eval_timeout_ms = eval_timeout_ms;
}

JSValue esp32_mquickjs_eval(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            const char *source,
                            const char *filename,
                            int eval_flags)
{
    uint64_t previous_deadline = runtime != NULL ? runtime->deadline_us : 0;
    JSValue result;

    if (runtime != NULL && runtime->eval_timeout_ms > 0) {
        uint64_t eval_deadline = esp_timer_get_time() +
                                 ((uint64_t)runtime->eval_timeout_ms * 1000ULL);

        if (previous_deadline == 0 || eval_deadline < previous_deadline) {
            runtime->deadline_us = eval_deadline;
        }
    }

    esp32_mquickjs_execution_enter(runtime);
    result = JS_Eval(ctx, source, strlen(source), filename, eval_flags);
    result = esp32_mquickjs_finish_execution(ctx, runtime, result);

    if (runtime != NULL) {
        runtime->deadline_us = previous_deadline;
    }
    return result;
}

JSValue esp32_mquickjs_run(JSContext *ctx,
                           esp32_mquickjs_runtime_t *runtime,
                           JSValue compiled_code)
{
    uint64_t previous_deadline = runtime != NULL ? runtime->deadline_us : 0;
    JSValue result;

    if (runtime != NULL && runtime->eval_timeout_ms > 0) {
        uint64_t eval_deadline = esp_timer_get_time() +
                                 ((uint64_t)runtime->eval_timeout_ms * 1000ULL);

        if (previous_deadline == 0 || eval_deadline < previous_deadline) {
            runtime->deadline_us = eval_deadline;
        }
    }

    esp32_mquickjs_execution_enter(runtime);
    result = JS_Run(ctx, compiled_code);
    result = esp32_mquickjs_finish_execution(ctx, runtime, result);

    if (runtime != NULL) {
        runtime->deadline_us = previous_deadline;
    }
    return result;
}

void esp32_mquickjs_print_exception(JSContext *ctx)
{
    JSValue exception = JS_GetException(ctx);
    esp32_mquickjs_runtime_t *runtime = JS_GetContextOpaque(ctx);

    console_output_begin(runtime, ESP32_MQUICKJS_LOG_SOURCE_EXCEPTION);
    JS_PrintValueF(ctx, exception, JS_DUMP_LONG);
    console_output_end(runtime);
}

JSValue js_help(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;
    console_output_begin(esp32_mquickjs_get_active_runtime(),
                         ESP32_MQUICKJS_LOG_SOURCE_RUNTIME);
    console_output_write(esp32_mquickjs_get_active_runtime(),
                         "See docs/api.md, docs/c-api.md, or docs/js-api.md for the API reference.",
                         strlen("See docs/api.md, docs/c-api.md, or docs/js-api.md for the API reference."));
    console_output_end(esp32_mquickjs_get_active_runtime());
    return JS_UNDEFINED;
}

bool esp32_mquickjs_install_globals(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime)
{
    if (ctx == NULL || runtime == NULL) {
        return false;
    }

    esp32_mquickjs_memory_init();
    if (!esp32_mquickjs_init_secure_random(ctx)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
    if (!esp32_mquickjs_init_stream_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_NVS
    if (!esp32_mquickjs_init_nvs_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
    if (!esp32_mquickjs_init_fs_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP
    if (!esp32_mquickjs_init_bitmap_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_LEDC
    esp32_mquickjs_init_ledc_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_ADC
    esp32_mquickjs_init_adc_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_DAC
    esp32_mquickjs_init_dac_runtime();
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C
    if (!esp32_mquickjs_init_i2c_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_SPI
    if (!esp32_mquickjs_init_spi_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_UART
    if (!esp32_mquickjs_init_uart_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_I2S
    if (!esp32_mquickjs_init_i2s_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_RMT
    if (!esp32_mquickjs_init_rmt_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
    if (!esp32_mquickjs_init_camera_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL
    if (!esp32_mquickjs_init_usb_serial_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_NET
    if (!esp32_mquickjs_init_net_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
    if (!esp32_mquickjs_init_time_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (!esp32_mquickjs_init_wifi_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET
    if (!esp32_mquickjs_init_socket_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET
    if (!esp32_mquickjs_init_websocket_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    if (!esp32_mquickjs_init_http_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    if (!esp32_mquickjs_init_http_server_runtime(ctx, runtime)) {
        esp32_mquickjs_print_exception(ctx);
        return false;
    }
#endif
    return true;
}

esp32_mquickjs_poll_result_t esp32_mquickjs_poll(JSContext *ctx,
                                                 esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_timer_state_t *state = esp32_mquickjs_timer_state(runtime);
    esp32_mquickjs_timer_event_t event;
    UBaseType_t timer_budget;
    bool core_async_handled = false;
    bool async_handled = false;
    uint32_t output_generation;
    esp32_mquickjs_poll_result_t result = ESP32_MQUICKJS_POLL_NONE;

    if (ctx == NULL || runtime == NULL) {
        return ESP32_MQUICKJS_POLL_NONE;
    }
    /*
     * Consume requests left by an earlier JavaScript turn before advancing
     * the Future that caused this poll. A long-running application can stay
     * inside its initial JS_Eval() forever while cooperatively waiting, so
     * execution_depth == 0 is not a prerequisite for a scheduler safe point.
     * Running here also leaves requests raised during this poll pending until
     * the next turn, after the settling Future has left the JavaScript stack.
     */
    run_pending_gc(ctx, runtime);
    output_generation = runtime->output_generation;
    if (esp32_mquickjs_future_poll(ctx, runtime)) {
        core_async_handled = true;
    }
    if (state == NULL || state->queue == NULL || state->slots == NULL) {
        async_handled = esp32_mquickjs_poll_registered(ctx, runtime);
        core_async_handled = esp32_mquickjs_poll_idle_job(ctx, runtime) ||
                             core_async_handled;
        if (core_async_handled || async_handled) {
            runtime->async_generation++;
            result |= ESP32_MQUICKJS_POLL_ASYNC;
        }
        if (runtime->output_generation != output_generation) {
            result |= ESP32_MQUICKJS_POLL_OUTPUT;
        }
        if (!esp32_mquickjs_execution_active(runtime)) {
            esp32_mquickjs_memory_maintain();
        }
        return result;
    }

    /*
     * Only consume timer events that were ready when this scheduler turn
     * started. A periodic callback can take longer than its own interval and
     * enqueue itself again as soon as the callback returns. Draining until
     * the queue becomes empty would then starve registered pollers and idle
     * jobs indefinitely.
     */
    timer_budget = uxQueueMessagesWaiting(state->queue);
    while (timer_budget > 0 &&
           xQueueReceive(state->queue, &event, 0) == pdTRUE) {
        esp32_mquickjs_timer_slot_t *slot;
        JSGCRef callback_ref;
        JSValue *callback;
        JSValue ret;

        timer_budget--;
        if (event.timer_id >= ESP32_MQUICKJS_MAX_TIMERS) {
            continue;
        }

        slot = &state->slots[event.timer_id];
        if (!slot->allocated || slot->generation != event.generation) {
            continue;
        }

        core_async_handled = true;

        if (JS_StackCheck(ctx, 2)) {
            console_output_begin(runtime, ESP32_MQUICKJS_LOG_SOURCE_RUNTIME);
            console_output_write(runtime,
                                 "Timer callback skipped: JS stack overflow",
                                 strlen("Timer callback skipped: JS stack overflow"));
            console_output_end(runtime);
            esp32_mquickjs_cancel_timer(ctx, slot);
            continue;
        }

        callback = JS_PushGCRef(ctx, &callback_ref);
        *callback = slot->callback.val;
        if (!slot->repeating) {
            esp32_mquickjs_cancel_timer(ctx, slot);
        }

        ret = esp32_mquickjs_call(ctx, runtime, *callback, JS_NULL, 0, NULL);
        if (slot->allocated && slot->generation == event.generation && slot->repeating) {
            /*
             * Keep a repeating timer pending for the entire callback. Native
             * Future waits can poll the scheduler recursively; clearing this
             * flag before the call lets the same periodic timer enqueue and
             * enter itself again until the C stack overflows.
             */
            slot->pending = false;
        }
        if (JS_IsException(ret)) {
            esp32_mquickjs_print_exception(ctx);
            if (slot->allocated && slot->generation == event.generation && slot->repeating) {
                esp32_mquickjs_cancel_timer(ctx, slot);
            }
        }
        JS_PopGCRef(ctx, &callback_ref);
    }

    async_handled = esp32_mquickjs_poll_registered(ctx, runtime);
    core_async_handled = esp32_mquickjs_poll_idle_job(ctx, runtime) ||
                         core_async_handled;
    if (core_async_handled || async_handled) {
        runtime->async_generation++;
        result |= ESP32_MQUICKJS_POLL_ASYNC;
    }
    if (runtime->output_generation != output_generation) {
        result |= ESP32_MQUICKJS_POLL_OUTPUT;
    }
    if (!esp32_mquickjs_execution_active(runtime)) {
        esp32_mquickjs_memory_maintain();
    }
    return result;
}

JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int i;
    esp32_mquickjs_runtime_t *runtime = JS_GetContextOpaque(ctx);

    (void)this_val;
    console_output_begin(runtime, ESP32_MQUICKJS_LOG_SOURCE_JAVASCRIPT);
    for (i = 0; i < argc; i++) {
        if (i != 0) {
            console_output_write(runtime, " ", 1U);
        }

        if (JS_IsString(ctx, argv[i])) {
            JSCStringBuf buf;
            size_t len = 0;
            const char *str = JS_ToCStringLen(ctx, &len, argv[i], &buf);

            console_output_write(runtime, str, len);
        } else {
            JS_PrintValueF(ctx, argv[i], JS_DUMP_LONG);
        }
    }

    console_output_end(runtime);
    return JS_UNDEFINED;
}

JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_async_state_t *state =
        esp32_mquickjs_async_state(s_active_runtime);

    (void)this_val;
    (void)argc;
    (void)argv;
    (void)esp32_mquickjs_byte_source_take_gc_request();
    if (state != NULL) {
        state->native_gc_pending = false;
        state->native_gc_debt_bytes = 0;
    }
    JS_GC(ctx);
    return JS_UNDEFINED;
}

JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
#if !CONFIG_ESP32_MQUICKJS_FEATURE_FS
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowInternalError(ctx, "load() requires the fs feature");
#else
    JSCStringBuf command_buf;
    const char *command;

    (void)this_val;
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "load(path) expects a script path");
    }

    command = JS_ToCString(ctx, argv[0], &command_buf);

    return esp32_mquickjs_load_from_active_fs(ctx, s_active_runtime, command);
#endif
}

JSValue js_sleep(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int delay_ms;

    (void)this_val;
    if (argc < 1 || JS_ToInt32(ctx, &delay_ms, argv[0]) != 0 || delay_ms < 0) {
        return JS_ThrowTypeError(ctx, "sleep(ms) expects a non-negative integer");
    }
    if (!esp32_mquickjs_cooperative_delay(s_active_runtime, (uint32_t)delay_ms)) {
        return JS_ThrowInternalError(ctx, "sleep(ms) was interrupted by a runtime stop request");
    }
    return JS_NewInt32(ctx, delay_ms);
}

JSValue js_runtime_defer_idle(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    esp32_mquickjs_async_state_t *state = esp32_mquickjs_async_state(s_active_runtime);
    esp32_mquickjs_idle_job_t *job;
    JSValue *callback;

    (void)this_val;
    if (argc != 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "sys._deferIdle(callback) expects one function");
    }
    if (state == NULL) {
        return JS_ThrowInternalError(ctx, "runtime idle queue is unavailable");
    }
    if (state->idle_count >= ESP32_MQUICKJS_IDLE_JOB_CAPACITY) {
        return JS_ThrowInternalError(ctx, "runtime idle queue is full");
    }

    job = &state->idle_jobs[state->idle_tail];
    if (job->allocated) {
        return JS_ThrowInternalError(ctx, "runtime idle queue is inconsistent");
    }
    callback = JS_AddGCRef(ctx, &job->callback);
    *callback = argv[0];
    job->allocated = true;
    state->idle_tail = (uint8_t)(
        (state->idle_tail + 1U) % ESP32_MQUICKJS_IDLE_JOB_CAPACITY);
    state->idle_count++;
    esp32_mquickjs_notify_activity(s_active_runtime);
    return JS_UNDEFINED;
}

JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "setTimeout(fn, ms) expects a function");
    }
    return esp32_mquickjs_create_timer(ctx,
                                       s_active_runtime,
                                       &argv[0],
                                       argc >= 2 ? argv[1] : JS_NewInt32(ctx, 0),
                                       false);
}

JSValue js_setInterval(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "setInterval(fn, ms) expects a function");
    }
    return esp32_mquickjs_create_timer(ctx,
                                       s_active_runtime,
                                       &argv[0],
                                       argc >= 2 ? argv[1] : JS_NewInt32(ctx, 0),
                                       true);
}

JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_timer_state_t *state = esp32_mquickjs_timer_state(s_active_runtime);
    uint8_t timer_id;
    uint32_t generation;

    (void)this_val;
    if (argc < 1 || !esp32_mquickjs_decode_timer_handle(ctx, argv[0], &timer_id, &generation)) {
        return JS_ThrowTypeError(ctx, "clearTimeout(handle) expects a timer handle");
    }
    if (state != NULL && state->slots != NULL) {
        esp32_mquickjs_timer_slot_t *slot = &state->slots[timer_id];

        if (slot->allocated && slot->generation == generation) {
            esp32_mquickjs_cancel_timer(ctx, slot);
        }
    }
    return JS_UNDEFINED;
}

JSValue js_date_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    double value;

    (void)this_val;
    argc &= ~FRAME_CF_CTOR;
    if (argc == 0) {
        struct timeval now = {0};

        gettimeofday(&now, NULL);
        value = (double)now.tv_sec * 1000.0 + (double)now.tv_usec / 1000.0;
    } else if (argc == 1 && JS_IsNumber(ctx, argv[0])) {
        if (JS_ToNumber(ctx, &value, argv[0]) != 0) {
            return JS_EXCEPTION;
        }
    } else {
        return JS_ThrowTypeError(ctx, "unsupported Date() parameter");
    }
    return JS_NewDate(ctx, value);
}

JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    struct timeval now = {0};

    (void)this_val;
    (void)argc;
    (void)argv;
    gettimeofday(&now, NULL);
    return JS_NewInt64(ctx,
                       (int64_t)now.tv_sec * 1000LL +
                           (int64_t)now.tv_usec / 1000LL);
}

JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt64(ctx, esp_timer_get_time() / 1000);
}
