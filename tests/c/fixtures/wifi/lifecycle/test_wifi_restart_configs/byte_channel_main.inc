
int main(void) {
    for (unsigned second=0;second<=2;++second) {
        setup();wifi_radio_operation_lock();native_channel=native_home=6;
        native_secondary=native_home_secondary=(wifi_second_chan_t)second;
        wifi_radio_restart_configs_t snapshot={0};bool matches=false;
        assert(wifi_radio_restart_channel_read(&snapshot,false)==ESP_OK);
        assert(snapshot.primary==6 && snapshot.secondary==second);
        assert(wifi_radio_restart_channel_read(&snapshot,true)==ESP_OK);
        assert(wifi_radio_restart_recovery_home_read(&snapshot,true)==ESP_OK);
        assert(wifi_radio_restart_ap_channel_matches(&snapshot,&matches)==ESP_OK && matches);
        wifi_radio_capture_stop_snapshot_locked();
        assert(s_stop_snapshot.error==ESP_OK && s_stop_snapshot.unchanged);
        assert(s_stop_snapshot.secondary==second);
        native_home_secondary=(wifi_second_chan_t)((second+1)%3);
        assert(wifi_radio_restart_channel_read(&snapshot,false)==ESP_ERR_INVALID_STATE);
        assert(wifi_radio_restart_ap_channel_matches(&snapshot,&matches)==ESP_OK && !matches);
        native_secondary=native_home_secondary=(wifi_second_chan_t)99;
        assert(wifi_radio_restart_channel_read(&snapshot,false)==ESP_ERR_INVALID_RESPONSE);
        assert(wifi_radio_restart_recovery_home_read(&snapshot,false)==ESP_ERR_INVALID_RESPONSE);
        fail_at=calls+1;
        assert(wifi_radio_restart_channel_read(&snapshot,false)==77);
        wifi_radio_operation_unlock();
    }
    return 0;
}
