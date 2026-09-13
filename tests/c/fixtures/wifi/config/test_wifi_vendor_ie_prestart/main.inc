
static esp32_mquickjs_wifi_radio_lifecycle_t token;
static esp32_mquickjs_wifi_radio_lease_t app,sta,ap;
static void setup(wifi_mode_t mode) {
    reset_vendor();s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.effective_mode=mode;s_radio.storage=WIFI_STORAGE_FLASH;
    memset(&token,0,sizeof(token));memset(&app,0,sizeof(app));memset(&sta,0,sizeof(sta));memset(&ap,0,sizeof(ap));
    memset(&s_policy_restart,0,sizeof(s_policy_restart));s_config_restart.captured=false;
    starts=stops=shutdowns=mode_queries=0;start_error=mode_error=verify_error=0;
    if(mode & WIFI_MODE_STA)assert(put(1,0)==ESP_OK);
    if(mode & WIFI_MODE_AP)assert(put(0,1)==ESP_OK);
}
static int begin(wifi_mode_t mode,wifi_storage_t storage) {
    esp32_mquickjs_wifi_radio_configuration_selection_t selection={.start_only=true,.mode_set=true,.mode=mode,.storage_set=true,.storage=storage};
    bool running=true;
    int err=esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,NULL,&selection,&token,&running);
    assert(!running);return err;
}
static int resume(wifi_mode_t mode) {
    return esp32_mquickjs_wifi_radio_resume_lifecycle(&token,mode,true,
        mode & WIFI_MODE_STA ? &app : NULL,mode & WIFI_MODE_STA ? &sta : NULL,mode & WIFI_MODE_AP ? &ap : NULL,NULL);
}
static void cleanup(void) {
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle(&token)==ESP_OK);
    assert(!s_vendor_ie.start_owner.identity);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_OK && !token.identity);
}
int main(void) {
    const wifi_mode_t modes[]={WIFI_MODE_STA,WIFI_MODE_AP,WIFI_MODE_APSTA};
    for(unsigned m=0;m<3;++m) {
        wifi_mode_t mode=modes[m];if(!CONFIG_ESP_WIFI_SOFTAP_SUPPORT && (mode & WIFI_MODE_AP))continue;
        setup(mode);unsigned copies=native_calls;
        assert(begin(mode,WIFI_STORAGE_FLASH)==ESP_OK && token.identity);
        esp32_mquickjs_wifi_vendor_ie_status_t status;esp32_mquickjs_wifi_radio_vendor_ie_status(&status);
        assert(status.start_pending && !status.owners && native_calls==copies);
        assert(esp32_mquickjs_wifi_radio_initialize_lifecycle(&token)==ESP_OK);
        esp32_mquickjs_wifi_radio_config_result_t result;
        assert(esp32_mquickjs_wifi_radio_configure_lifecycle(&token,mode,WIFI_STORAGE_FLASH,NULL,NULL,NULL,NULL,NULL,&result)==ESP_OK);
        assert(!result.mutation_attempted && native_calls==copies && !starts);
        assert(resume(mode)==ESP_OK && starts==1 && !token.identity && !s_vendor_ie.start_owner.identity);
        assert(owners()==((mode & WIFI_MODE_STA ? 1U : 0U)+(mode & WIFI_MODE_AP ? 1U : 0U)) && native_calls==copies);
        assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK);
        wifi_radio_operation_lock();wifi_radio_release_locked(&ap);wifi_radio_release_locked(&sta);wifi_radio_release_locked(&app);wifi_radio_operation_unlock();
    }
    setup(WIFI_MODE_STA);unsigned copies=native_calls;
    assert(begin(WIFI_MODE_STA,WIFI_STORAGE_RAM)==ESP_ERR_INVALID_STATE && !token.identity && owners()==1 && native_calls==copies);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(begin(WIFI_MODE_APSTA,WIFI_STORAGE_FLASH)==ESP_ERR_INVALID_STATE && !token.identity && owners()==1);
#endif
    s_radio.next_lease_identity=UINT32_MAX;assert(begin(WIFI_MODE_STA,WIFI_STORAGE_FLASH)==ESP_ERR_NO_MEM && owners()==1 && !token.identity);
    setup(WIFI_MODE_STA);s_vendor_ie.slots[2].pending=true;
    assert(begin(WIFI_MODE_STA,WIFI_STORAGE_FLASH)==ESP_ERR_INVALID_STATE && owners()==1 && !token.identity);
    setup(WIFI_MODE_STA);s_radio.leases[11].identity=99;
    assert(begin(WIFI_MODE_STA,WIFI_STORAGE_FLASH)==ESP_ERR_INVALID_STATE && owners()==1 && !token.identity);
    setup(WIFI_MODE_STA);assert(begin(WIFI_MODE_STA,WIFI_STORAGE_FLASH)==ESP_OK);
    copies=native_calls;esp32_mquickjs_wifi_radio_lifecycle_t old=token;++old.identity;
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle(&old)==ESP_ERR_INVALID_STATE && native_calls==copies);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_ERR_INVALID_STATE && native_calls==copies);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_ERR_INVALID_STATE && !shutdowns && token.identity);
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,false)==ESP_ERR_INVALID_STATE && !stops && token.identity);
    fail_call=native_calls+1;assert(esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle(&token)==77);
    assert(s_vendor_ie.start_owner.identity && s_vendor_ie.slots[2].pending);
    fail_call=0;cleanup();assert(shutdowns==1);
    for(unsigned failure=0;failure<3;++failure) {
        setup(WIFI_MODE_STA);assert(begin(WIFI_MODE_STA,WIFI_STORAGE_FLASH)==ESP_OK);
        if(failure==0)mode_error=71;
        if(failure==1)start_error=72;
        if(failure==2)verify_error=73;
        assert(resume(WIFI_MODE_STA)==(int)(71+failure));
        assert(token.identity && s_vendor_ie.start_owner.identity && !app.acquired && !sta.acquired && !owners());
        assert(starts==(failure==0 ? 0U : 1U));mode_error=start_error=verify_error=0;cleanup();
    }
    assert(!locks && !critical);return 0;
}
