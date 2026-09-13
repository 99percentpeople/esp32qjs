
int main(void){
    policy_reset(true);s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_lease_identity=1;
    esp32_mquickjs_wifi_radio_lifecycle_t token={.generation=7,.identity=41};
    s_radio.lifecycle.generation=7;s_radio.lifecycle.identity=41;
    esp32_mquickjs_wifi_radio_lease_t app={0},sta={0};observed_owner=&app;
    struct esp32_mquickjs_wifi_eap_profile *profile=(void *)1;
    wifi_radio_operation_lock();
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&app,&sta,NULL,NULL,true,profile)==ESP_ERR_INVALID_ARG);
    assert(!enterprise_installs&&!acquired&&!config_discards);
    config_verify_error=91;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&app,&sta,NULL,NULL,false,profile)==91);
    assert(!enterprise_installs&&!config_discards&&!app.acquired&&!sta.acquired&&token.identity);
    config_verify_error=0;s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    enterprise_error=92;
    assert(wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&app,&sta,NULL,NULL,false,profile)==92);
    assert(enterprise_installs==1&&!config_discards&&!app.acquired&&!sta.acquired&&token.identity);
    assert(acquired==released); /* Staged owners stay private on EAP failure. */
    enterprise_error=0;s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(!wifi_radio_resume_lifecycle_locked(&token,WIFI_MODE_STA,true,&app,&sta,NULL,NULL,false,profile));
    assert(enterprise_installs==2&&config_discards==1&&app.acquired&&sta.acquired&&!token.identity&&!s_radio.lifecycle.identity);
    wifi_radio_release_locked(&sta);wifi_radio_release_locked(&app);wifi_radio_operation_unlock();
    assert(acquired==released&&!locks&&!critical);
}
