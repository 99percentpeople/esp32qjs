
static void *registry_storage;
static void future_runtime_resource_release(void *p,void *opaque) {assert(p==registry_storage && !opaque);registry_storage=NULL;}
static bool esp32_mquickjs_wifi_smartconfig_poll_observations(bool teardown) {assert(teardown);return false;}
static bool esp32_mquickjs_wifi_wps_poll_observations(bool teardown) {assert(teardown);return false;}
static bool esp32_mquickjs_wifi_wps_prepare_runtime_destroy(void) {return true;}
static bool hold_future,hold_worker,hold_raw,hold_enterprise,hold_smartconfig;
static unsigned enterprise_prepares;
static unsigned smartconfig_prepares;
static bool esp32_mquickjs_wifi_smartconfig_prepare_runtime_destroy(void) {++smartconfig_prepares;return !hold_smartconfig;}
static bool esp32_mquickjs_wifi_eap_prepare_runtime_destroy(void) {++enterprise_prepares;return !hold_enterprise;}
static unsigned vm_frees,root_deletes,recovery_disposals,action_prepares,raw_prepares,ftm_prepares;
static esp32_mquickjs_runtime_t *s_active_runtime;
static void JS_DeleteGCRef(JSContext *ctx,JSGCRef *ref) {(void)ctx;(void)ref;++root_deletes;}
static int esp_timer_stop(void *timer) {(void)timer;return 0;}
static int esp_timer_delete(void *timer) {(void)timer;return 0;}
/* Allocation is outside capture here. The actual caller-owned recovery snapshot
 * is disposed by production code; native operation storage remains independent. */
static void dispose_recovery(esp32_mquickjs_future_driver_state_t *state) {
    assert(state->cancelled);esp32_mquickjs_wifi_recovery_dispose(&state->recovery);++recovery_disposals;
}
static const esp32_mquickjs_future_driver_t recovery_driver={recovery_cancel,recovery_poll,dispose_recovery};
/* An unrelated native driver can remain pending while cleanup must advance. */
static esp32_mquickjs_cancel_result_t pending_cancel(esp32_mquickjs_future_driver_state_t *state) {
    (void)state;return ESP32_MQUICKJS_CANCEL_REQUESTED;
}
static esp32_mquickjs_future_poll_t pending_poll(esp32_mquickjs_future_driver_state_t *state) {
    (void)state;return hold_future?ESP32_MQUICKJS_FUTURE_PENDING:ESP32_MQUICKJS_FUTURE_READY;
}
static const esp32_mquickjs_future_driver_t pending_driver={pending_cancel,pending_poll,NULL};
static bool esp32_mquickjs_prepare_wifi_monitor_runtime_destroy(esp32_mquickjs_runtime_t *runtime) {(void)runtime;return true;}
static void consume_original(esp32_mquickjs_wifi_recovery_kind_t kind) {
    /* Radio boundary reports post-deinit pending retirement, worker completes
     * only when explicitly scheduled. Never infer termination from STOP alone. */
    if(original.kind==kind && !hold_worker && recovery_shutdown_calls)
        recovery_owner_pending=false;
}
static bool esp32_mquickjs_prepare_wifi_action_runtime_destroy(void) {
    ++action_prepares;consume_original(ESP32_MQUICKJS_WIFI_RECOVERY_ACTION);
    return original.kind!=ESP32_MQUICKJS_WIFI_RECOVERY_ACTION || !recovery_owner_pending;
}
static bool esp32_mquickjs_prepare_wifi_raw_tx_runtime_destroy(void) {
    ++raw_prepares;consume_original(ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX);
    return !hold_raw && (original.kind!=ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX || !recovery_owner_pending);
}
static bool esp32_mquickjs_wifi_ftm_prepare_runtime_destroy(void) {
    ++ftm_prepares;consume_original(ESP32_MQUICKJS_WIFI_RECOVERY_FTM);
    return original.kind!=ESP32_MQUICKJS_WIFI_RECOVERY_FTM || !recovery_owner_pending;
}
static int esp32_mquickjs_poll_reapers(esp32_mquickjs_runtime_t *r) {(void)r;return 0;}
static int esp32_mquickjs_reapers_pending(esp32_mquickjs_runtime_t *r) {(void)r;return 0;}
static void esp32_mquickjs_clear_idle_jobs(JSContext *c,esp32_mquickjs_runtime_t *r) {(void)c;(void)r;}
static void esp32_mquickjs_deinit_time_runtime(void) {}
static void esp32_mquickjs_deinit_stream_runtime(void) {}
static void esp32_mquickjs_deinit_wifi_runtime(JSContext *ctx) {
    (void)ctx;assert(!s_wifi_configuration_cleanup && !recovery_owner_pending);
}
static void esp32_mquickjs_deinit_timer_state(JSContext *c,esp32_mquickjs_runtime_t *r) {(void)c;(void)r;}
static void JS_FreeContext(JSContext *ctx) {(void)ctx;++vm_frees;}
void esp32_mquickjs_memory_release_generation(void) {}
static void esp32_mquickjs_deinit_event_queue_runtime(esp32_mquickjs_runtime_t *r) {(void)r;}
static void teardown_payload_free(void *p) {assert(!p);}
#undef esp32_mquickjs_memory_payload_free
#define esp32_mquickjs_memory_payload_free teardown_payload_free
static void esp32_mquickjs_deinit_future_runtime(esp32_mquickjs_runtime_t *r) {r->future_state=NULL;}
static void esp32_mquickjs_deinit_async_state(esp32_mquickjs_runtime_t *r) {(void)r;}
