
/* Native START/lease delivery boundary. Replay and configuration acceptance
 * below are the actual production helpers, with SDK calls fault-injected. */
static unsigned temporary_releases;
static int wifi_radio_resume_lifecycle_locked(esp32_mquickjs_wifi_radio_lifecycle_t *t,
    wifi_mode_t mode,bool start,esp32_mquickjs_wifi_radio_lease_t *application,
    esp32_mquickjs_wifi_radio_lease_t *station,esp32_mquickjs_wifi_radio_lease_t *ap,
    const void *controls,bool keep_lifecycle,void *enterprise_profile) {
    assert(locks==1 && !critical && start && application && !station && !ap && !controls && keep_lifecycle && !enterprise_profile);
    if(!wifi_radio_restart_configs_ready_locked(t,mode) || !s_config_restart.snapshot ||
        !s_config_restart.snapshot->restore_off || s_radio.started || s_radio.stop_required ||
        s_radio.fault_stage || s_radio.cleanup_stage)return ESP_ERR_INVALID_STATE;
    int error=wifi_radio_restart_configs_pre_start_locked(t,mode);
    if(error)return error;
    error=esp_wifi_start();s_radio.stop_required=true;s_radio.started=error==ESP_OK;
    if(error)return wifi_radio_record_fault("start",error);
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    error=wifi_radio_restart_configs_post_start_locked(t);
    if(!error)error=wifi_radio_restart_configs_commit_locked(t,mode);
    if(error)return error;
    assert(native_storage==WIFI_STORAGE_RAM && s_radio.storage==WIFI_STORAGE_RAM && allocation);
    application->identity=71;application->generation=s_radio.generation;application->acquired=true;
    return ESP_OK;
}
static void wifi_radio_release_locked(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks==1 && !critical);
    if(lease->acquired){assert(allocation && s_radio.lifecycle.identity==41);++temporary_releases;}
    memset(lease,0,sizeof(*lease));
}
static int wifi_radio_check_stopped_lifecycle_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,bool cleanup) {
    assert(!cleanup && locks==1 && !critical);
    return wifi_radio_restart_configs_owner(t) && !s_radio.started && !s_radio.stop_required &&
        s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_STOPPED ? ESP_OK : ESP_ERR_INVALID_STATE;
}
