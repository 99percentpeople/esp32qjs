
static int reinstall_error;
static unsigned reinstalls;
static uint64_t next_eap_binding=200;
static int esp32_mquickjs_wifi_radio_eap_begin_restart(uint64_t binding,
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const esp32_mquickjs_wifi_radio_lease_t *ap,esp32_mquickjs_wifi_eap_profile_t *profile,bool allow,
    esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t *mode){
    assert(config_busy&&binding==eap_binding&&profile==(void *)1);
    if(!running || !sta_storage || s_wifi_state.status.connected || (ap->acquired&&!allow))return ESP_ERR_INVALID_STATE;
    int error=esp32_mquickjs_wifi_radio_begin_lifecycle(app,sta,ap,token);
    if(!error)*mode=ap->acquired?WIFI_MODE_APSTA:WIFI_MODE_STA;
    return error;
}
static int esp32_mquickjs_wifi_radio_eap_resume_restart(esp32_mquickjs_wifi_radio_lifecycle_t *t,
    wifi_mode_t mode,esp32_mquickjs_wifi_radio_lease_t *app,esp32_mquickjs_wifi_radio_lease_t *sta,
    esp32_mquickjs_wifi_radio_lease_t *ap,esp32_mquickjs_wifi_eap_profile_t *profile){
    assert(config_busy&&!config_closing&&profile==(void *)1&&!eap_binding&&checkpoint&&replayed&&exact(t));
    ++reinstalls;
    if(reinstall_error){
        assert(!app->acquired&&!sta->acquired&&(!ap||!ap->acquired));
        running=true;eap_binding=++next_eap_binding;return reinstall_error;
    }
    int error=esp32_mquickjs_wifi_radio_resume_lifecycle(t,mode,true,app,sta,ap,NULL);
    if(!error)eap_binding=++next_eap_binding;
    return error;
}
