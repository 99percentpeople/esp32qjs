
typedef void JSContext;
typedef void esp32_mquickjs_runtime_t;
typedef void esp32_mquickjs_future_driver_state_t;
typedef int esp32_mquickjs_future_token_t;
static bool START_locked(JSContext *ctx,esp32_mquickjs_runtime_t *r,
    esp32_mquickjs_future_token_t t,esp32_mquickjs_future_driver_state_t *s) {
    (void)ctx;(void)r;(void)t;(void)s;
    return ble_gap_terminate(7,3)==0;
}
