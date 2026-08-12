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

typedef struct {
    bool (*prepare)(JSContext *ctx,
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
    bool (*cancel)(esp32_mquickjs_future_driver_state_t *state);
    void (*destroy)(esp32_mquickjs_future_driver_state_t *state);
    uint32_t (*timeout_ms)(const esp32_mquickjs_future_driver_state_t *state);
} esp32_mquickjs_future_driver_t;

typedef void (*esp32_mquickjs_future_worker_fn_t)(void *opaque);

bool esp32_mquickjs_init_future_runtime(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_prepare_future_runtime_destroy(JSContext *ctx,
                                                   esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_future_runtime(esp32_mquickjs_runtime_t *runtime);

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
JSValue js_future_call(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_all(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_race(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_sleep(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_timeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_wait(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_future_cancel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
