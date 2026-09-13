
static bool accepted;
static int change(bool value){return esp32_mquickjs_wifi_apply_policy(
    ESP32_MQUICKJS_WIFI_POLICY_BSS_COLOR,WIFI_IF_MAX,value,&accepted,&result);}
int main(void){
    policy_reset(false);
#if !CONFIG_SOC_WIFI_HE_SUPPORT
    assert(change(true)==ESP_ERR_NOT_SUPPORTED && !calls && !result.mutation_attempted);
#else
    assert(change(true)==ESP_OK && accepted && native_bss && bitmap_resets==1);
    assert(change(true)==ESP_OK && bitmap_resets==2); /* No implicit deduplication. */
    unsigned slot=ESP32_MQUICKJS_WIFI_POLICY_SLOT_BSS_COLOR;
    assert(s_policies.records[slot].known && s_policies.records[slot].value);
    fail_at=calls+1;assert(change(false)==77 && !accepted && native_bss);
    assert(s_policies.records[slot].uncertain && !s_policies.records[slot].known && s_radio.fault_stage);
    unsigned before=calls;assert(change(false)==ESP_ERR_INVALID_STATE && calls==before);
    policy_reset(true);assert(change(true)==ESP_ERR_WIFI_NOT_STARTED && !calls);
    policy_reset(false);s_radio.effective_mode=WIFI_MODE_AP;
    assert(change(true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(false);s_wifi_state.radio_lease.identity++;
    assert(change(true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(false);s_radio.leases[3]=(wifi_radio_live_lease_t){80,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI};
    assert(change(true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(false);s_wifi_state.status.connected=true;assert(change(false)==ESP_OK && !accepted);
    esp32_mquickjs_wifi_policy_snapshot_t frozen;
    wifi_radio_operation_lock();assert(esp32_mquickjs_wifi_policy_capture(&s_policies,7,&frozen)==ESP_OK);
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);s_radio.generation=8;s_radio.started=false;
    uint8_t completed=0;esp32_mquickjs_wifi_policy_write_t write;before=calls;
    assert(esp32_mquickjs_wifi_policy_replay(&s_policies,8,&frozen,false,&completed,wifi_radio_policy_writer,NULL,&write)==ESP_OK);
    assert(calls==before && !completed);
    s_radio.started=true;native_bss=true;
    assert(esp32_mquickjs_wifi_policy_replay(&s_policies,8,&frozen,true,&completed,wifi_radio_policy_writer,NULL,&write)==ESP_OK);
    assert(!native_bss && completed==frozen.mask && s_policies.records[slot].generation==8);
    before=calls;
    assert(esp32_mquickjs_wifi_policy_replay(&s_policies,8,&frozen,true,&completed,wifi_radio_policy_writer,NULL,&write)==ESP_OK && calls==before);
    wifi_radio_operation_unlock();
#endif
    return 0;
}
