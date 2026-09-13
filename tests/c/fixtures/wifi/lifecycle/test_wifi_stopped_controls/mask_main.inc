
int main(void) {
    uint32_t actual=99;
    for(unsigned requested=0;requested<2;++requested) {
        stopped();native_event_mask=1-requested;
        esp32_mquickjs_wifi_radio_stop_snapshot_t before=s_stop_snapshot;
        assert(esp32_mquickjs_wifi_radio_event_mask(true,requested,&actual,&result)==ESP_OK);
        assert(actual==requested && calls==3 && writes==1 && unchanged());
        assert(!memcmp(&before,&s_stop_snapshot,sizeof(before)));
        reconstruct(WIFI_STORAGE_FLASH); /* Capture/replay the new mask, not the STOP-time value. */
    }
    for(unsigned failure=1;failure<=3;++failure) {
        stopped();fail_at=failure;
        esp32_mquickjs_wifi_radio_stop_snapshot_t before=s_stop_snapshot;
        assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==77);
        assert(unchanged() && !memcmp(&before,&s_stop_snapshot,sizeof(before)) && native_event_mask==0);
        assert(result.rollback_attempted==(failure!=1));
        assert(result.rollback_complete==(failure!=1));
        fail_at=0;reconstruct(WIFI_STORAGE_FLASH);
    }
    for(unsigned failure=1;failure<=2;++failure) {
        stopped();fail_at=2;rollback_fail=failure;
        assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==77);
        assert(result.rollback_attempted && !result.rollback_complete && result.rollback_error==88);
        assert(s_stop_snapshot.unchanged && !unchanged());
        unsigned attempts=calls;reject();assert(calls==attempts); /* Preserved RF history cannot clear a fault. */
    }
    stopped();native_event_mask=2;fail_at=2;
    assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==77);
    assert(!result.rollback_attempted && writes==1 && !unchanged());reject();
    stopped();assert(esp32_mquickjs_wifi_radio_event_mask(true,2,&actual,&result)==ESP_ERR_INVALID_ARG);
    assert(!calls && unchanged()); /* Required completion-event bits remain protected. */
    stopped();assert(esp32_mquickjs_wifi_radio_event_mask(true,0,&actual,&result)==ESP_OK);
    assert(calls==1 && !writes && unchanged());
    stopped();memset(&s_stop_snapshot,0,sizeof(s_stop_snapshot));
    assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==ESP_OK && !unchanged());admit_without_history();
    stopped();wifi_radio_operation_lock();assert(esp_wifi_set_mode(WIFI_MODE_STA)==ESP_OK);wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==ESP_OK && !unchanged());admit_without_history();
    assert(!allocation && !locks && !critical && !helper_locks);return 0;
}
