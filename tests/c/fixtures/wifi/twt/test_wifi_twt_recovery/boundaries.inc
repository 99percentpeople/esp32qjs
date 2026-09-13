
#define RADIO_EVENTS_IDLE 0
#define WIFI_RADIO_EAP_PENDING eap_pending
static bool eap_pending;
static struct {void *snapshot;} s_config_restart;
static struct {esp32_mquickjs_wifi_radio_lifecycle_t owner;} s_policy_restart;
static unsigned cancelled,prepared,captured,stop_calls,shutdown_calls;
static int cancel_error,prepare_error,capture_error,physical_error;
static esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_cancel(uint32_t id) {
    assert(locks && !critical && id==s_twt_probe.state.native_identity);++cancelled;return cancel_error;
}
static int wifi_radio_policy_restart_prepare_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {
    assert(locks && !critical && t->identity==s_radio.lifecycle.identity && mode==s_radio.effective_mode);
    ++prepared;return prepare_error;
}
static int wifi_radio_restart_configs_capture_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {
    assert(locks && !critical && t->identity==s_radio.lifecycle.identity && mode==s_radio.effective_mode);
    ++captured;return capture_error;
}
static int wifi_radio_restart_configs_record(const char *stage,int error,bool mutated) {
    assert(locks && !critical && stage && !mutated);return error;
}
static int wifi_radio_stop_lease_locked(const esp32_mquickjs_wifi_radio_lease_t *owner,bool twt) {
    assert(locks && !critical && !owner && twt);++stop_calls;return physical_error;
}
static int wifi_radio_shutdown_lease_locked(const esp32_mquickjs_wifi_radio_lease_t *owner,bool twt) {
    assert(locks && !critical && !owner && twt);++shutdown_calls;return physical_error;
}
