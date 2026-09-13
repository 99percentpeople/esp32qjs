
#define ESP_ERR_INVALID_RESPONSE -6
static unsigned mode_queries,starts,stops,shutdowns;
static int start_error,mode_error,verify_error;
static struct {bool captured;struct {bool restore_off;} *snapshot;} s_config_restart;
static struct {esp32_mquickjs_wifi_radio_lifecycle_t owner;wifi_mode_t mode;unsigned completed;struct {unsigned mask;} snapshot;} s_policy_restart;
typedef bool (*esp32_mquickjs_wifi_config_accept_fn)(const wifi_config_t *,const wifi_config_t *);
static int esp_wifi_get_mode(wifi_mode_t *out) {assert(locks && !critical);++mode_queries;*out=s_radio.effective_mode;return mode_error;}
static int wifi_radio_initialize(void) {assert(locks && s_radio.driver_owned && !s_radio.started);return ESP_OK;}
static int wifi_radio_configure_locked(wifi_mode_t mode,wifi_storage_t storage,wifi_config_t *sta,
    esp32_mquickjs_wifi_config_accept_fn sta_accept,wifi_config_t *ap,esp32_mquickjs_wifi_config_accept_fn ap_accept,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,esp32_mquickjs_wifi_radio_config_result_t *result) {
    (void)mode;(void)storage;(void)sta;(void)sta_accept;(void)ap;(void)ap_accept;(void)controls;(void)result;
    assert(!"parked Vendor IE start must not write configuration");return 77;
}
static int esp32_mquickjs_wifi_radio_validate_start_controls(bool start,const esp32_mquickjs_wifi_radio_start_controls_t *c) {(void)start;(void)c;return ESP_OK;}
static bool wifi_radio_restart_configs_ready_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {(void)t;(void)mode;return !s_config_restart.captured;}
static int wifi_radio_restart_configs_pre_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {(void)t;(void)mode;assert(!s_radio.started);return ESP_OK;}
static int wifi_radio_restart_configs_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode,esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks && !critical && wifi_radio_vendor_ie_start_matches(t) && wifi_radio_lease_valid(lease));
    assert(mode==s_radio.effective_mode);++starts;
    /* This is the physical START boundary. Every enabled IE must already be
     * backed by its new exact registry owner before any first frame can leave. */
    for(unsigned i=0;i<10;++i) if(native_length[i]) {
        wifi_interface_t iface=wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)(i/2));
        assert(wifi_radio_lease_valid(&s_vendor_ie.leases[iface]) && !s_vendor_ie.slots[i].pending);
        assert(native_ie[i][0]==221 && native_length[i]==6);
    }
    s_radio.started=s_radio.stop_required=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    return start_error; /* Also allow partial START on failure. */
}
static int wifi_radio_apply_start_controls(const esp32_mquickjs_wifi_radio_start_controls_t *c) {assert(c==NULL);return ESP_OK;}
static int wifi_radio_policy_restart_replay_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,bool post) {(void)t;(void)post;assert(0);return 77;}
static int wifi_radio_restart_configs_post_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return verify_error;}
static int wifi_radio_restart_configs_commit_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {(void)t;(void)mode;return ESP_OK;}
static void wifi_radio_restart_configs_discard_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;}
static int wifi_radio_stop_locked(void) {assert(locks && !critical);++stops;s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;return ESP_OK;}
static int wifi_radio_shutdown_locked(void) {
    assert(locks && !critical && !s_vendor_ie.start_owner.identity);++shutdowns;
    for(unsigned i=0;i<10;++i)assert(!s_vendor_ie.slots[i].length && !s_vendor_ie.slots[i].pending);
    assert(wifi_radio_stop_locked()==ESP_OK);s_radio.driver_owned=false;return ESP_OK;
}
