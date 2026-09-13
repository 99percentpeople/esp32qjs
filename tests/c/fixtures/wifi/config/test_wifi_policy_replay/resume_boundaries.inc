
/* Vendor IE handoff is covered by test_wifi_vendor_ie_prestart. */
static struct {esp32_mquickjs_wifi_radio_lifecycle_t start_owner;wifi_mode_t start_mode;wifi_storage_t start_storage;} s_vendor_ie;
static bool wifi_radio_vendor_ie_start_matches(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return false;}
static unsigned wifi_radio_vendor_ie_start_count(void) {return 0;}
static int wifi_radio_vendor_ie_start_attach(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return ESP_OK;}
static void wifi_radio_vendor_ie_start_park(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;}
static void wifi_radio_vendor_ie_start_commit(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;}

typedef struct {bool restore_off;} resume_config_snapshot_t;
static struct {bool captured;resume_config_snapshot_t *snapshot;} s_config_restart;
static bool wifi_radio_restart_configs_ready_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t mode) {(void)token;(void)mode;return true;}
static bool config_committed;static unsigned config_discards;
static int config_verify_error;
static int config_power_error;
static int config_pre_start_error;
/* The production checkpoint implementation is covered by restart_configs.
 * This boundary injects its outcome into the actual resume ordering. */
static int wifi_radio_restart_configs_pre_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t mode) {
    (void)token;(void)mode;assert(!s_radio.started && !observed_owner->acquired);return config_pre_start_error;
}
static int wifi_radio_restart_configs_post_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {(void)token;assert(!observed_owner->acquired);return config_power_error;}
static int wifi_radio_restart_configs_commit_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t mode) {(void)token;(void)mode;config_committed=config_verify_error==0;return config_verify_error;}
static void wifi_radio_restart_configs_discard_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {(void)token;++config_discards;}
#define ESP_ERR_NO_MEM 17
typedef struct {bool tx_power_set;int8_t tx_power_quarter_dbm;} esp32_mquickjs_wifi_radio_start_controls_t;
static int esp32_mquickjs_wifi_radio_validate_start_controls(bool start,const esp32_mquickjs_wifi_radio_start_controls_t *c) {(void)start;assert(c==NULL);return ESP_OK;}
static int wifi_radio_apply_start_controls(const esp32_mquickjs_wifi_radio_start_controls_t *c) {assert(c==NULL);return ESP_OK;}
static int esp_wifi_get_mode(wifi_mode_t *mode) {assert(locks==1 && !critical);*mode=s_radio.effective_mode;return ESP_OK;}
static unsigned acquired,released;
static int wifi_radio_acquire_locked(esp32_mquickjs_wifi_radio_client_t client,wifi_mode_t mode,esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks==1 && mode==WIFI_MODE_STA);++acquired;
    *lease=(esp32_mquickjs_wifi_radio_lease_t){s_radio.generation,s_radio.next_lease_identity++,client,true};
    s_radio.leases[0]=(wifi_radio_live_lease_t){lease->identity,client};return ESP_OK;
}
static int wifi_radio_ensure_started_locked(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks==1 && lease->acquired);s_radio.started=s_radio.stop_required=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;return ESP_OK;
}
static int wifi_radio_restart_configs_start_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t mode,esp32_mquickjs_wifi_radio_lease_t *lease) {
    (void)token;assert(mode==WIFI_MODE_STA && !observed_owner->acquired);return wifi_radio_ensure_started_locked(lease);
}
static void wifi_radio_release_locked(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks==1);if(!lease->acquired)return;++released;s_radio.leases[0].identity=0;memset(lease,0,sizeof(*lease));
}
