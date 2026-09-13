
int main(void){
    reset_vendor();s_radio.effective_mode=WIFI_MODE_APSTA;s_radio.event_live=WIFI_MODE_APSTA;
    esp32_mquickjs_wifi_radio_lease_t owners[3]={0};
    wifi_radio_operation_lock();
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_APSTA,&owners[0]));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&owners[1]));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&owners[2]));
    wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_eap_install_result_t result;
    esp32_mquickjs_wifi_eap_profile_t *profile=(void *)1;
    assert(!esp32_mquickjs_wifi_radio_eap_install(&owners[0],&owners[1],&owners[2],profile,&result));
    uint64_t original=result.identity;unsigned before=installs;
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};wifi_mode_t mode=WIFI_MODE_NULL;
#define BEGIN(allow) esp32_mquickjs_wifi_radio_eap_begin_restart(original,&owners[0],&owners[1],&owners[2],profile,allow,&token,&mode)
    assert(BEGIN(false)==ESP_ERR_INVALID_STATE && !token.identity && !source_reads);
    link_result=0;assert(BEGIN(true)==ESP_ERR_INVALID_STATE&&!source_reads);link_result=ESP_ERR_WIFI_NOT_CONNECT;
    s_radio.event_live=0;assert(BEGIN(true)==ESP_ERR_INVALID_STATE);s_radio.event_live=WIFI_MODE_APSTA;
    uint32_t saved=s_radio.next_lease_identity;s_radio.next_lease_identity=UINT32_MAX;
    assert(BEGIN(true)==ESP_ERR_NO_MEM);s_radio.next_lease_identity=saved;
    source_rejected=true;assert(BEGIN(true)==ESP_ERR_INVALID_STATE&&!token.identity);source_rejected=false;
    assert(!BEGIN(true)&&token.identity&&mode==WIFI_MODE_APSTA&&installs==before);
    assert(!esp32_mquickjs_wifi_radio_eap_clear_lifecycle(original,&token,&result));
    wifi_radio_operation_lock();
    for(unsigned i=0;i<3;++i)wifi_radio_release_locked(&owners[i]);
    /* Physical rebuild boundary changes driver generation, not the reservation. */
    ++s_radio.generation;
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_APSTA,&owners[0]));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&owners[1]));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&owners[2]));
    install_error=93;retain_failure=true;
    assert(wifi_radio_eap_restart_install_locked(profile,owners)==93);
    assert(s_eap_radio.identity>original&&!s_eap_radio.ready&&s_eap_radio.generation==s_radio.generation);
    for(unsigned i=0;i<3;++i){assert(!s_eap_radio.owners[i]);wifi_radio_release_locked(&owners[i]);assert(!owners[i].acquired);}
    wifi_radio_operation_unlock();
    assert(!strcmp(s_radio.configuration.stage,"restart-enterprise-install")&&s_radio.configuration.error==93);
    uint64_t failed=s_eap_radio.identity;esp32_mquickjs_wifi_radio_lifecycle_t wrong=token;++wrong.generation;
    before=clears;
    assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(failed,&wrong,&result)==ESP_ERR_INVALID_STATE&&clears==before);
    assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(original,&token,&result)==ESP_ERR_INVALID_STATE&&clears==before);
    clear_error=94;assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(failed,&token,&result)==94&&s_eap_radio.identity==failed);
    clear_error=0;assert(!esp32_mquickjs_wifi_radio_eap_clear_lifecycle(failed,&token,&result)&&!s_eap_radio.identity);
    before=clears;assert(!esp32_mquickjs_wifi_radio_eap_clear_lifecycle(failed,&token,&result)&&clears==before);
    assert(!locks&&!critical&&s_radio.lifecycle.identity==token.identity);
}
