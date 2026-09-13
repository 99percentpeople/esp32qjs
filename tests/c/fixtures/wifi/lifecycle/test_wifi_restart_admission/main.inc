
static void reject(int expected) {
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};
    esp32_mquickjs_wifi_radio_restart_selection_t selected={.mode=99,.cold=true,.restore_off=true};
    unsigned next=s_radio.next_lifecycle_identity;
    esp32_mquickjs_wifi_radio_lifecycle_t previous=s_radio.lifecycle;
    assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&selected)==expected);
    assert(!token.identity && !token.generation && !selected.mode && !selected.cold && !selected.restore_off);
    assert(!locked && !critical && next==s_radio.next_lifecycle_identity);
    assert(!memcmp(&previous,&s_radio.lifecycle,sizeof(previous)));
}
static void late_owner(void) {s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=123;}
static void late_write(void) {s_stop_snapshot.unchanged=false;}
static void late_generation(void) {++s_radio.generation;}
static void late_mode(void) {s_radio.effective_mode=WIFI_MODE_AP;s_stop_snapshot.mode=WIFI_MODE_AP;}
static void late_ap_policy(void) {s_policies.records[ESP32_MQUICKJS_WIFI_POLICY_SLOT_AP_11B].configured=true;}
static void admit_without_history(void) {
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};
    esp32_mquickjs_wifi_radio_restart_selection_t selected={0};
    assert(!esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&selected));
    assert(selected.mode==s_radio.effective_mode && !selected.cold && !selected.restore_off);
    assert(token.generation==s_radio.generation && token.identity==31 && !locked && !critical);
    assert(!s_radio.started && !s_radio.stop_required);
}
static void cold_setup(void) {
    setup();s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;
    s_radio.driver_owned=s_radio.storage_configured=false;s_radio.effective_mode=WIFI_MODE_NULL;
}
int main(void) {
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};
    esp32_mquickjs_wifi_radio_restart_selection_t selected={0};
    cold_setup();
    assert(!esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&selected));
    assert(selected.mode==WIFI_MODE_STA && selected.cold && !selected.restore_off);
    assert(token.identity==31 && token.generation==7);
    for(unsigned failure=0;failure<10;++failure) {
        cold_setup();
        if(failure==0)s_radio.restart_required=true;
        if(failure==1)s_radio.fault_stage="init";
        if(failure==2)s_radio.cleanup_stage="channel-drain";
        if(failure==3)s_radio.driver_owned=true;
        if(failure==4)s_radio.storage_configured=true;
        if(failure==5)s_radio.effective_mode=WIFI_MODE_STA;
        if(failure==6)s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_FAULTED;
        if(failure==7)s_radio.leases[0].identity=17;
        if(failure==8)s_radio.event_live=1;
        if(failure==9)on_lock=late_owner;
        reject(ESP_ERR_INVALID_STATE);
    }
    for(int mode=0;mode<=3;++mode) {
        setup();s_radio.effective_mode=mode;s_stop_snapshot.mode=mode;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if(mode&WIFI_MODE_AP){reject(ESP_ERR_NOT_SUPPORTED);continue;}
#endif
        token=(esp32_mquickjs_wifi_radio_lifecycle_t){0};
        selected=(esp32_mquickjs_wifi_radio_restart_selection_t){0};
        assert(!esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&selected));
        assert(selected.mode==(mode?mode:WIFI_MODE_STA) && !selected.cold && selected.restore_off==(mode==0));
        assert(token.generation==7 && token.identity==31 && lock_calls==1);
        assert(s_radio.next_lifecycle_identity==32 && !s_radio.started && s_radio.effective_mode==mode);
        reject(ESP_ERR_INVALID_STATE);
    }
    for(unsigned policy=0;policy<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?3U:1U);++policy) {
        setup();s_radio.effective_mode=WIFI_MODE_NULL;
        if(policy==0)late_ap_policy();
        if(policy==1)s_ftm_offset.configured=true;
        if(policy==2)on_lock=late_ap_policy;
        reject(ESP_ERR_INVALID_STATE); /* No consent, no claim or native mutation. */
        token=(esp32_mquickjs_wifi_radio_lifecycle_t){0};
        selected=(esp32_mquickjs_wifi_radio_restart_selection_t){.allow_ap_restart=true};
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        assert(!esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&selected));
        assert(selected.mode==WIFI_MODE_APSTA && selected.restore_off && !selected.cold && selected.allow_ap_restart);
#else
        assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&selected)==ESP_ERR_NOT_SUPPORTED);
        assert(!token.identity && !selected.mode && selected.allow_ap_restart);
#endif
    }
    for(unsigned slot=0;slot<WIFI_RADIO_MAX_LEASES;++slot) {
        for(unsigned client=0;client<ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT;++client) {
            setup();s_radio.leases[slot].identity=71;s_radio.leases[slot].client=client;
            reject(ESP_ERR_INVALID_STATE);assert(s_radio.leases[slot].identity==71);
        }
    }
#define REJECT_WITH(change) do {setup();change;reject(ESP_ERR_INVALID_STATE);} while(0)
    REJECT_WITH(s_radio.wake_locks=1);REJECT_WITH(s_radio.operation.identity=5);
    REJECT_WITH(s_radio.promiscuous_claimed=true);REJECT_WITH(s_radio.fault_stage="original-fault");
    REJECT_WITH(s_radio.cleanup_stage="stop-events");REJECT_WITH(s_radio.restart_required=true);
    REJECT_WITH(s_radio.started=true);REJECT_WITH(s_radio.stop_required=true);
    REJECT_WITH(s_radio.driver_owned=false);REJECT_WITH(s_radio.storage_configured=false);
    REJECT_WITH(s_radio.storage=(wifi_storage_t)99);REJECT_WITH(s_radio.driver_state=99);
    REJECT_WITH(s_radio.stop_submitted=true);REJECT_WITH(s_radio.event_phase=1);REJECT_WITH(s_radio.event_live=1);
    REJECT_WITH(s_radio.effective_mode=(wifi_mode_t)99);
    REJECT_WITH(s_tx_rate_lease.identity=3);REJECT_WITH(s_tx_rate_lease.restore_pending=true);
    REJECT_WITH(s_interval.owner.identity=4);REJECT_WITH(s_interval.restore_pending=true);
    REJECT_WITH(s_config_restart.snapshot=(void *)1);REJECT_WITH(s_policy_restart.owner.identity=8);
    REJECT_WITH(s_vendor_ie.start_owner.identity=9);
    for(unsigned i=0;i<6;++i){setup();native_pending[i]=true;reject(ESP_ERR_INVALID_STATE);}
    REJECT_WITH(on_lock=late_owner);
#define ADMIT_WITH(change) do {setup();change;admit_without_history();} while(0)
    ADMIT_WITH(s_radio.generation++);ADMIT_WITH(s_radio.event_identity++);
    ADMIT_WITH(s_stop_snapshot.stop_identity=0);ADMIT_WITH(s_stop_snapshot.error=77);
    ADMIT_WITH(s_stop_snapshot.step=ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_NONE);
    ADMIT_WITH(s_stop_snapshot.unchanged=false);ADMIT_WITH(s_stop_snapshot.mode=WIFI_MODE_AP);
    ADMIT_WITH(memset(&s_stop_snapshot,0,sizeof(s_stop_snapshot)));
    ADMIT_WITH(on_lock=late_write);ADMIT_WITH(on_lock=late_generation);
    setup();on_lock=late_mode;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    admit_without_history(); /* Mode resolves inside the same lock as admission. */
#else
    reject(ESP_ERR_NOT_SUPPORTED);
#endif
    setup();s_radio.next_lifecycle_identity=UINT32_MAX;
    token=(esp32_mquickjs_wifi_radio_lifecycle_t){0};selected=(esp32_mquickjs_wifi_radio_restart_selection_t){0};
    assert(!esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&selected) && token.identity==UINT32_MAX);
    assert(!s_radio.next_lifecycle_identity);
    memset(&s_radio.lifecycle,0,sizeof(s_radio.lifecycle));reject(ESP_ERR_NO_MEM);
    setup();token=(esp32_mquickjs_wifi_radio_lifecycle_t){.generation=7};selected.mode=99;
    assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&selected)==ESP_ERR_INVALID_ARG);
    assert(!lock_calls && token.generation==7 && selected.mode==99);
    token.identity=4;
    assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&selected)==ESP_ERR_INVALID_ARG && token.identity==4);
    assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(NULL,&selected)==ESP_ERR_INVALID_ARG);
    token=(esp32_mquickjs_wifi_radio_lifecycle_t){0};
    assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,NULL)==ESP_ERR_INVALID_ARG);
    assert(!lock_calls && !locked && !critical);
    return 0;
}
