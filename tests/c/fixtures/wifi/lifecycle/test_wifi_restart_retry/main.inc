
static void reject(int error) {
    esp32_mquickjs_wifi_radio_restart_selection_t selection={.mode=99,.cold=true,.restore_off=true};
    esp32_mquickjs_wifi_radio_lifecycle_t original=token;
    assert(esp32_mquickjs_wifi_radio_admit_restart_retry(&token,&selection)==error);
    assert(!memcmp(&original,&token,sizeof(token)) && !selection.mode && !selection.cold && !selection.restore_off);
    assert(!locked && !critical && !strcmp(s_radio.configuration.stage,"old-config"));
    assert(!strcmp(s_radio.fault_stage,"start-events") && s_radio.fault_error==77);
}
static void late_owner(void) {s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=71;}
static void late_checkpoint(void) {s_config_restart.owner.identity++;}
static void accept(bool consent) {
    esp32_mquickjs_wifi_radio_restart_selection_t selection={.allow_ap_restart=consent};
    unsigned identity=s_radio.next_lease_identity;
    assert(!esp32_mquickjs_wifi_radio_admit_restart_retry(&token,&selection));
    assert(selection.mode==s_config_restart.mode && selection.cold==(s_config_restart.snapshot==NULL));
    assert(selection.restore_off==(s_config_restart.snapshot && frozen.restore_off));
    assert(selection.allow_ap_restart==consent && token.identity==41 && token.generation==7);
    assert(s_radio.lifecycle.identity==41 && s_radio.next_lease_identity==identity && s_config_restart.captured);
    assert(!s_radio.configuration.stage && !s_radio.configuration.error);
    assert(!strcmp(s_radio.fault_stage,"start-events") && s_radio.fault_error==77);
    assert(!strcmp(s_radio.cleanup_stage,"stop-events") && s_radio.cleanup_error==88 && !locked && !critical);
}
int main(void) {
    setup();accept(false);
    setup();s_config_restart.snapshot=NULL;accept(false); /* Retained complete cold defaults. */
    setup();s_radio.driver_owned=s_radio.storage_configured=s_radio.started=s_radio.stop_required=false;
    accept(false); /* Known pre-init event/handler failure after original deinit. */
    setup();s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.started=s_radio.stop_required=false;accept(false);
    setup();s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;accept(false);
#define REJECT_WITH(change) do {setup();change;reject(ESP_ERR_INVALID_STATE);} while(0)
    REJECT_WITH(s_config_restart.captured=false);REJECT_WITH(frozen.capture_error=77);
    REJECT_WITH(s_config_restart.source_generation=0);REJECT_WITH(s_config_restart.owner.identity++);
    REJECT_WITH(s_config_restart.owner.generation++);REJECT_WITH(s_policy_restart.owner.identity++);
    REJECT_WITH(s_policy_restart.owner.generation++);REJECT_WITH(s_policy_restart.mode=WIFI_MODE_AP);
    REJECT_WITH(s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_NULL);
    REJECT_WITH(token.generation++);REJECT_WITH(s_radio.lifecycle.identity++);
    REJECT_WITH(s_radio.restart_required=true);REJECT_WITH(s_radio.operation.identity=7);
    REJECT_WITH(s_radio.wake_locks=1);REJECT_WITH(s_radio.promiscuous_claimed=true);
    REJECT_WITH(s_tx_rate_lease.identity=7);REJECT_WITH(s_tx_rate_lease.restore_pending=true);
    REJECT_WITH(s_interval.owner.identity=7);REJECT_WITH(s_interval.restore_pending=true);
    REJECT_WITH(s_vendor_ie.start_owner.identity=7);
    REJECT_WITH(s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_INITIALIZING);
    REJECT_WITH(s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTING);
    REJECT_WITH(s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPING);
    REJECT_WITH(s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED);
    REJECT_WITH(s_radio.driver_owned=false);REJECT_WITH(on_lock=late_owner);REJECT_WITH(on_lock=late_checkpoint);
    for(unsigned slot=0;slot<WIFI_RADIO_MAX_LEASES;++slot)REJECT_WITH(s_radio.leases[slot].identity=9);
    for(unsigned i=0;i<6;++i)REJECT_WITH(native_pending[i]=true);
    for(unsigned i=0;i<(CONFIG_SOC_WIFI_HE_SUPPORT?4U:3U);++i)REJECT_WITH(recovery[i]=true);
    setup();s_radio.next_lease_identity=0;reject(ESP_ERR_NO_MEM);
    setup();s_radio.next_lease_identity=UINT32_MAX;reject(ESP_ERR_NO_MEM);
    setup();s_radio.next_lease_identity=UINT32_MAX-1;accept(false);
    setup();frozen.restore_off=true;s_radio.next_lease_identity=UINT32_MAX;accept(false);
    setup();s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_AP;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    s_radio.next_lease_identity=UINT32_MAX;accept(false);
    setup();s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_APSTA;
    s_radio.next_lease_identity=UINT32_MAX-1;reject(ESP_ERR_NO_MEM);
    setup();s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_APSTA;
    s_radio.next_lease_identity=UINT32_MAX-2;accept(false);
    setup();s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_APSTA;frozen.restore_off=true;
    reject(ESP_ERR_INVALID_STATE);accept(true);
#else
    reject(ESP_ERR_NOT_SUPPORTED);
#endif
    setup();esp32_mquickjs_wifi_radio_restart_selection_t selection={.mode=99};
    assert(esp32_mquickjs_wifi_radio_admit_restart_retry(NULL,&selection)==ESP_ERR_INVALID_ARG && selection.mode==99);
    assert(esp32_mquickjs_wifi_radio_admit_restart_retry(&token,NULL)==ESP_ERR_INVALID_ARG);
    token.identity=0;assert(esp32_mquickjs_wifi_radio_admit_restart_retry(&token,&selection)==ESP_ERR_INVALID_ARG);
    assert(!lock_calls && selection.mode==99);
    return 0;
}
