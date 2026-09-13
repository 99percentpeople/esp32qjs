
static unsigned submits, retires, next_native=1;
static int mode_error, channel_error, association_error, submit_error, retire_error;
static bool accept_native=true, early;
static esp32_mquickjs_wifi_twt_probe_result_snapshot_t native_result;
static esp32_mquickjs_wifi_twt_probe_timer_snapshot_t native_timer;
static esp32_mquickjs_wifi_twt_probe_wake_snapshot_t native_wake;
static int esp_wifi_get_mode(wifi_mode_t *out) {assert(locks && !critical);*out=s_radio.effective_mode;return mode_error;}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *out) {(void)out;assert(locks && !critical);return association_error;}
static int wifi_radio_get_channel_locked(uint8_t *out,wifi_second_chan_t *second,uint32_t *gen) {
    assert(locks && !critical);*out=6;*second=WIFI_SECOND_CHAN_NONE;*gen=s_radio.generation;return channel_error;
}
void esp32_mquickjs_wifi_twt_probe_result_snapshot(esp32_mquickjs_wifi_twt_probe_result_snapshot_t *out) {
    assert(!critical);*out=native_result;
}
void esp32_mquickjs_wifi_twt_probe_timer_snapshot(esp32_mquickjs_wifi_twt_probe_timer_snapshot_t *out) {
    assert(!critical);*out=native_timer;
}
void esp32_mquickjs_wifi_twt_probe_wake_snapshot(esp32_mquickjs_wifi_twt_probe_wake_snapshot_t *out) {
    assert(!critical);*out=native_wake;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_submit(uint32_t ms,uint32_t *id) {
    assert(locks && !critical && ms>=1 && ms<=60000);++submits;
    assert(s_radio.operation.identity && s_twt_probe.lease.acquired);
    assert(s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT]==1);
    if(early) {
        esp32_mquickjs_wifi_twt_probe_radio_state_t s;
        assert(esp32_mquickjs_wifi_radio_twt_probe_status(&s_twt_probe.state.token,&s));
        assert(s.dispatching && !s.native_identity);
    }
    if(accept_native) {
        *id=next_native++;
        native_result=(esp32_mquickjs_wifi_twt_probe_result_snapshot_t){.identity=*id,.owned=true,
            .event_seen=early,.event={.status=ITWT_PROBE_SUCCESS}};
    }
    return submit_error;
}
esp_err_t esp32_mquickjs_wifi_twt_probe_retire_poll(esp32_mquickjs_wifi_twt_probe_retire_t *s,
    const esp32_mquickjs_wifi_twt_token_t *t,uint32_t id) {
    assert(locks && !critical && s==&s_twt_probe.retirement && t->identity==s_twt_probe.state.token.identity);
    assert(id==native_result.identity && s_radio.operation.identity && s_twt_probe.lease.acquired);
    ++retires;s->stage="injected-native-suffix";
    if(!retire_error)native_result.owned=false;
    return retire_error;
}
