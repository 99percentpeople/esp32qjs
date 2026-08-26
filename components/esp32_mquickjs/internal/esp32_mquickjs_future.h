#pragma once

#include "esp32_mquickjs_types.h"

typedef struct {
    uint8_t slot;
    uint32_t generation;
} esp32_mquickjs_future_token_t;

typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;

typedef enum {
    ESP32_MQUICKJS_FUTURE_PENDING,
    ESP32_MQUICKJS_FUTURE_READY,
} esp32_mquickjs_future_poll_t;

typedef enum {
    /* The operation cannot be cancelled and remains pending. */
    ESP32_MQUICKJS_CANCEL_REJECTED,
    /* The operation is already stopped and may settle immediately. */
    ESP32_MQUICKJS_CANCELLED,
    /* Cancellation was accepted; poll() confirms when teardown is complete. */
    ESP32_MQUICKJS_CANCEL_REQUESTED,
} esp32_mquickjs_cancel_result_t;

/* NULL means that the operation does not require resource serialization.
   Equal non-NULL keys share one bounded FIFO lane within a runtime. */
typedef const void *esp32_mquickjs_resource_key_t;

typedef struct {
    /* Capture immutable arguments and native leases during Future.call().
       This callback must not start I/O or block on hardware. On failure it
       must release any partial state and leave out_state set to NULL. */
    bool (*capture)(JSContext *ctx,
                    JSGCRef *this_ref,
                    int argc,
                    JSGCRef *argv,
                    esp32_mquickjs_future_driver_state_t **out_state);
    bool (*start)(JSContext *ctx,
                  esp32_mquickjs_runtime_t *runtime,
                  esp32_mquickjs_future_token_t token,
                  esp32_mquickjs_future_driver_state_t *state);
    esp32_mquickjs_future_poll_t (*poll)(esp32_mquickjs_future_driver_state_t *state);
    JSValue (*finish)(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state);
    esp32_mquickjs_cancel_result_t (*cancel)(
        esp32_mquickjs_future_driver_state_t *state);
    void (*destroy)(esp32_mquickjs_future_driver_state_t *state);
    uint32_t (*timeout_ms)(const esp32_mquickjs_future_driver_state_t *state);
    esp32_mquickjs_resource_key_t (*resource_key)(
        const esp32_mquickjs_future_driver_state_t *state);
} esp32_mquickjs_future_driver_t;

/* A generic worker callback publishes its result and returns. The generic
   worker pool is solely responsible for waking the Future once afterward. */
typedef void (*esp32_mquickjs_future_worker_fn_t)(void *opaque);

typedef struct {
    uint32_t queued;
    uint32_t pending;
    uint32_t capacity;
    uint32_t user_capacity;
    uint32_t internal_reserve;
} esp32_mquickjs_future_status_t;

bool esp32_mquickjs_init_future_runtime(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_prepare_future_runtime_destroy(JSContext *ctx,
                                                   esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_future_runtime(esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_get_future_status(esp32_mquickjs_runtime_t *runtime,
                                      esp32_mquickjs_future_status_t *status);

bool esp32_mquickjs_future_register_driver(JSContext *ctx,
                                           esp32_mquickjs_runtime_t *runtime,
                                           JSValue function,
                                           const esp32_mquickjs_future_driver_t *driver);
bool esp32_mquickjs_future_wake(esp32_mquickjs_runtime_t *runtime,
                               esp32_mquickjs_future_token_t token);
bool esp32_mquickjs_future_wake_from_isr(esp32_mquickjs_runtime_t *runtime,
                                        esp32_mquickjs_future_token_t token,
                                        int *task_woken);
bool esp32_mquickjs_future_submit_worker(
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_worker_fn_t function,
    void *opaque);
bool esp32_mquickjs_future_poll(JSContext *ctx,
                               esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_future_cooperate(esp32_mquickjs_runtime_t *runtime);
uint32_t esp32_mquickjs_future_next_wait_ms(esp32_mquickjs_runtime_t *runtime,
                                           uint32_t requested_ms);
JSValue esp32_mquickjs_future_call_and_wait(JSContext *ctx,
                                            esp32_mquickjs_runtime_t *runtime,
                                            JSValue function,
                                            JSValue this_value,
                                            int argc,
                                            JSValue *argv);

JSValue js_future_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_future_finalizer(JSContext *ctx, void *opaque);
void js_future_gc_trace(JSContext *ctx, void *opaque,
                        JSCGCVisitor visit, void *visitor_opaque);
JSValue js_future_call(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_all(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_race(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_sleep(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_timeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_map(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_flat_map(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_wait(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_cancel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
