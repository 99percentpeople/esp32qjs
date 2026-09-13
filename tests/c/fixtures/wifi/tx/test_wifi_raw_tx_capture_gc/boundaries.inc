
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 1
#undef esp32_mquickjs_memory_wireless_alloc
static void *esp32_mquickjs_memory_wireless_alloc(const char *tag,size_t length,int policy,int role) {
    assert(role==ESP32_MQUICKJS_MEMORY_BUDGET_TX);
    assert(!strcmp(tag,"wifi.raw-tx") && length>=24 && length<=1500 && policy==1);
    return heap_caps_malloc(length,1);
}
static JSValue raw_tx_error(JSContext *ctx,const char *code,esp_err_t error,const char *stage,
                           esp32_mquickjs_wifi_raw_tx_validation_t validation) {
    (void)error;(void)stage;(void)validation;return JS_ThrowTypeError(ctx,"%s",code);
}
static const char *esp_get_idf_version(void) {return "recorded-fixture";}
/* Fixed native snapshots exercise the production status converter. Concurrency
 * of these boundaries is covered by separate broker/lane production fixtures. */
static const char s_retired_lock;
static struct {bool active;esp_err_t error;const char *stage;} s_retired={true,23,"radio-release-stop"};
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static bool terminated_snapshot;
void esp32_mquickjs_wifi_raw_tx_broker_status(esp32_mquickjs_wifi_raw_tx_broker_status_t *output) {
    *output=(esp32_mquickjs_wifi_raw_tx_broker_status_t){.operation_active=!terminated_snapshot,.quarantined=true,
        .native_terminated=terminated_snapshot,.token={.identity=45,.generation=7},.submit_error=12,.cleanup_error=34,
        .invalid_callbacks=3,.mismatched_callbacks=5,.orphan_callbacks=7,.duplicate_callbacks=9};
    if (!terminated_snapshot) {
        output->correlation_failure_reason="source-mismatch";
        output->correlation_failure_identity=46;
        output->correlation_failure_generation=7;
    }
}
void esp32_mquickjs_wifi_raw_tx_sessions_status(esp32_mquickjs_wifi_raw_tx_sessions_status_t *output) {
    *output=(esp32_mquickjs_wifi_raw_tx_sessions_status_t){.live=2,.closing=1,.faulted=1,
        .pending_results=3,.pending_flushes=2,.error_generation=12,.error=45,.stage="submit"};
}
void esp32_mquickjs_wifi_raw_tx_periodic_jobs_status(esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t *output) {
    *output=(esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t){.live=2,.cleanup_pending=1,.faulted=1,
        .error_generation=88,.error=12,.cleanup_error=34,.stage="timer-start",.cleanup_stage="timer-delete"};
}
void esp32_mquickjs_wifi_raw_tx_lane_status(esp32_mquickjs_wifi_raw_tx_lane_status_t *output) {
    *output=(esp32_mquickjs_wifi_raw_tx_lane_status_t){.active_identity=77,.waiting=8,.identity_exhausted=true};
}
