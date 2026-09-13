
static espnow_session_t session;
static const char *stage;
static void baseline(void) {
    reset();memset(&s_interval,0,sizeof(s_interval));memset(&session,0,sizeof(session));
    interval_calls=window_calls=0;interval_error=window_error=0;
    assert(esp32_mquickjs_wifi_apply_interval(300,&result)==ESP_OK && native_interval==300);
    session.now_initialized=true;
    session.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){7,21,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,true};
    s_radio.leases[2]=(wifi_radio_live_lease_t){21,session.radio_lease.client};
}
int main(void) {
    baseline();
    assert(esp32_mquickjs_wifi_apply_interval(600,&result)==ESP_ERR_INVALID_STATE && interval_calls==1);
    esp32_mquickjs_wifi_radio_lease_t forged=session.radio_lease;forged.generation++;
    assert(esp32_mquickjs_wifi_radio_interval_configure(&forged,100,&session.interval_token)==ESP_ERR_INVALID_STATE);
    s_radio.wake_locks=1;
    assert(espnow_apply_power_save(&session,true,20,100,&stage)==ESP_ERR_INVALID_STATE && !session.power_save_fault && !session.interval_token.identity);
    s_radio.wake_locks=0;
    assert(espnow_apply_power_save(&session,true,20,100,&stage)==ESP_OK && native_interval==100 && native_window==20);
    s_radio.operation.identity=2;
    assert(espnow_apply_power_save(&session,true,40,200,&stage)==ESP_ERR_INVALID_STATE && !session.power_save_fault && native_interval==100);
    s_radio.operation.identity=0;
    esp32_mquickjs_wifi_interval_token_t old=session.interval_token;
    assert(espnow_apply_power_save(&session,true,40,200,&stage)==ESP_OK && s_interval.previous==300);
    window_error=77;
    assert(espnow_apply_power_save(&session,true,50,400,&stage)==77 && session.power_save_fault && session.interval_token.identity);
    unsigned before=interval_calls;window_error=0;
    assert(espnow_apply_power_save(&session,true,60,500,&stage)==ESP_ERR_INVALID_STATE && interval_calls==before);
    interval_error=88;
    assert(espnow_restore_power_save(&session,&stage)==88 && session.close_power_phase==1 && session.interval_token.identity && s_interval.uncertain);
    before=window_calls;interval_error=0;
    assert(espnow_restore_power_save(&session,&stage)==ESP_OK && window_calls==before && native_interval==300 && !session.interval_token.identity);
    session.power_save_fault=false;
    assert(espnow_apply_power_save(&session,true,10,100,&stage)==ESP_OK);
    before=interval_calls;
    assert(esp32_mquickjs_wifi_radio_interval_release(&session.radio_lease,&old)==ESP_ERR_INVALID_STATE && interval_calls==before);
    session.now_initialized=false;before=window_calls;
    assert(espnow_restore_power_save(&session,&stage)==ESP_OK && window_calls==before && native_interval==300);
    baseline();interval_error=66;
    assert(espnow_apply_power_save(&session,true,10,100,&stage)==66 && !window_calls && session.interval_token.identity && session.power_save_fault);
    interval_error=0;
    assert(espnow_restore_power_save(&session,&stage)==ESP_OK && !window_calls && native_interval==300);
    baseline();before=interval_calls;
    assert(espnow_apply_power_save(&session,false,0,0,&stage)==ESP_OK && interval_calls==before && native_interval==300 && native_window==UINT16_MAX);
    s_radio.leases[2].identity=0;interval_error=99;
    assert(esp32_mquickjs_wifi_apply_interval(800,&result)==99 && result.mutation_attempted && s_interval.uncertain);
    interval_error=0;
    assert(esp32_mquickjs_wifi_apply_interval(900,&result)==ESP_OK && s_interval.known && !s_interval.uncertain);
    assert(!locks && !critical && !helper_locks);
    return 0;
}
