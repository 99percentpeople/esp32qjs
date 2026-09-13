
int main(void) {
    bool accepted;
#define WRITE(q,iface,v) esp32_mquickjs_wifi_apply_policy(ESP32_MQUICKJS_WIFI_POLICY_##q,iface,v,&accepted,&result)
    policy_reset(false);assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_OK && accepted && native_dynamic && calls==1);
    assert(s_policies.revision==1 && s_policies.records[0].known && s_policies.records[0].value);
    assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_OK && calls==2); /* No getter, no guessed deduplication. */
    assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,false)==ESP_OK && !accepted && !native_dynamic);
    policy_reset(true);assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_WIFI_NOT_STARTED && !calls);
    policy_reset(false);assert(WRITE(11B_RATE,WIFI_IF_STA,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(true);assert(WRITE(11B_RATE,WIFI_IF_STA,true)==ESP_OK && native_11b[0] && !native_11b[1]);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(WRITE(11B_RATE,WIFI_IF_AP,true)==ESP_OK && native_11b[1]);
#else
    assert(WRITE(11B_RATE,WIFI_IF_AP,true)==ESP_ERR_NOT_SUPPORTED);
#endif
    policy_reset(true);s_radio.leases[3]=(wifi_radio_live_lease_t){23,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI};
    assert(WRITE(11B_RATE,WIFI_IF_STA,true)==ESP_ERR_INVALID_STATE && !calls);
    for(unsigned q=0;q<3;++q) {
        policy_reset(q==1);fail_at=1;
        int err=esp32_mquickjs_wifi_apply_policy(q,q==1 ? WIFI_IF_STA : WIFI_IF_MAX,true,&accepted,&result);
#if !CONFIG_ESP_COEX_POWER_MANAGEMENT
        if(q==2){assert(err==ESP_ERR_NOT_SUPPORTED && !calls && !result.mutation_attempted);continue;}
#endif
        assert(s_policies.revision==1 && s_policies.records[q==2 ? 3 : q].uncertain && !s_policies.records[q==2 ? 3 : q].known);
        assert(err==77 && !accepted && writes==1 && s_radio.fault_error==77 && !result.rollback_attempted);
        assert(!locks && !critical && !helper_locks);
        fail_at=0;unsigned before=calls;
        assert(esp32_mquickjs_wifi_apply_policy(q,q==1 ? WIFI_IF_STA : WIFI_IF_MAX,true,&accepted,&result)==ESP_ERR_INVALID_STATE && calls==before);
    }
#if CONFIG_ESP_COEX_POWER_MANAGEMENT
    policy_reset(false);assert(WRITE(COEX_POWER,WIFI_IF_MAX,true)==ESP_OK && native_coex);
    policy_reset(true);assert(WRITE(COEX_POWER,WIFI_IF_MAX,true)==ESP_OK && native_coex);
#endif
    policy_reset(false);s_wifi_state.radio_lease.generation++;assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(false);s_radio.leases[3]=(wifi_radio_live_lease_t){23,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW};
    assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(false);s_radio.wake_locks=1;assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(false);s_wifi_state.scan_draining=true;assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(true);s_radio.stop_required=true;assert(WRITE(11B_RATE,WIFI_IF_STA,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(true);s_radio.effective_mode=WIFI_MODE_NULL;assert(WRITE(11B_RATE,WIFI_IF_STA,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(false);s_policies.revision=UINT32_MAX;
    assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_INVALID_STATE && !calls && !result.mutation_attempted && !s_radio.fault_stage);
    assert(!strcmp(result.stage,"policy-record-admission"));
    assert(!locks && !critical && !helper_locks);return 0;
}
