
int main(void){
    reset_vendor();s_radio.effective_mode=WIFI_MODE_STA;
    esp32_mquickjs_wifi_radio_lease_t app={0},sta={0},ap={0},other={0};
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&app)==0);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&sta)==0);
    wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_eap_install_result_t result;
    esp32_mquickjs_wifi_eap_profile_t *profile=(void *)1; /* Native storage boundary, never dereferenced here. */
#define INSTALL() esp32_mquickjs_wifi_radio_eap_install(&app,&sta,&ap,profile,&result)
    link_result=0;assert(INSTALL()==ESP_ERR_INVALID_STATE && !installs);
    link_result=88;assert(INSTALL()==88 && !installs);link_result=ESP_ERR_WIFI_NOT_CONNECT;
    s_radio.operation.identity=1;assert(INSTALL()==ESP_ERR_INVALID_STATE && !installs);s_radio.operation.identity=0;
    ++sta.identity;assert(INSTALL()==ESP_ERR_INVALID_STATE && !installs);--sta.identity;
    install_error=71;assert(INSTALL()==71 && !s_eap_radio.identity);
    retain_failure=true;assert(INSTALL()==71 && s_eap_radio.identity && !s_eap_radio.ready);
    uint64_t failed=s_eap_radio.identity;
    wifi_radio_operation_lock();
    wifi_radio_release_locked(&sta);wifi_radio_release_locked(&app);assert(sta.acquired && app.acquired);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&other)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle={0};
    assert(wifi_radio_begin_lifecycle_locked(&app,&sta,&ap,&lifecycle)==ESP_ERR_INVALID_STATE && !lifecycle.identity);
    assert(wifi_radio_connection_owner_locked(&app,&sta,&ap,WIFI_IF_STA,false)==ESP_ERR_INVALID_STATE);
    wifi_radio_operation_unlock();
    unsigned before=clears;assert(esp32_mquickjs_wifi_radio_eap_clear(failed+1,&result)==ESP_ERR_INVALID_STATE && clears==before);
    clear_error=72;assert(esp32_mquickjs_wifi_radio_eap_clear(failed,&result)==72 && result.retained && result.identity==failed && !result.entered);
    status_fail=true;assert(esp32_mquickjs_wifi_radio_eap_status(failed,&result)==74 && result.retained && result.identity==failed);status_fail=false;
    clear_error=0;assert(esp32_mquickjs_wifi_radio_eap_clear(failed,&result)==0 && !s_eap_radio.identity);
    before=clears;assert(esp32_mquickjs_wifi_radio_eap_clear(failed,&result)==ESP_ERR_INVALID_STATE && clears==before);
    /* APSTA helper identities are pinned together. Normal connection ownership
     * remains admissible because EAP uses no scan/connect operation slot. */
    s_radio.effective_mode=WIFI_MODE_APSTA;
    wifi_radio_operation_lock();assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&ap)==0);wifi_radio_operation_unlock();
    install_error=0;retain_failure=false;assert(INSTALL()==0 && s_eap_radio.ready && !s_radio.operation.identity);
    uint64_t active=s_eap_radio.identity;assert(active>failed);
    wifi_radio_operation_lock();assert(wifi_radio_connection_owner_locked(&app,&sta,&ap,WIFI_IF_STA,false)==0);
    wifi_radio_release_locked(&ap);assert(ap.acquired);wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_eap_clear(failed,&result)==ESP_ERR_INVALID_STATE && s_eap_radio.identity==active);
    /* Exact stop admission transfers pins without an SDK call. */
    before=clears;
    assert(esp32_mquickjs_wifi_radio_eap_begin_stop(active+1,&app,&sta,&ap,&lifecycle)==ESP_ERR_INVALID_STATE);
    s_radio.wake_locks=1;
    assert(esp32_mquickjs_wifi_radio_eap_begin_stop(active,&app,&sta,&ap,&lifecycle)==ESP_ERR_INVALID_STATE);
    s_radio.wake_locks=0;
    esp32_mquickjs_wifi_radio_lease_t wrong=ap;++wrong.identity;
    assert(esp32_mquickjs_wifi_radio_eap_begin_stop(active,&app,&sta,&wrong,&lifecycle)==ESP_ERR_INVALID_STATE);
    assert(!lifecycle.identity && clears==before);
    assert(esp32_mquickjs_wifi_radio_eap_begin_stop(active,&app,&sta,&ap,&lifecycle)==ESP_OK && lifecycle.identity);
    esp32_mquickjs_wifi_radio_lifecycle_t old=lifecycle;++old.identity;
    assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(active,&old,&result)==ESP_ERR_INVALID_STATE && clears==before);
    assert(esp32_mquickjs_wifi_radio_eap_clear(active,&result)==ESP_ERR_INVALID_STATE && clears==before);
    clear_error=81;assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(active,&lifecycle,&result)==81);
    assert(s_eap_radio.identity==active && s_radio.lifecycle.identity==lifecycle.identity && ap.acquired);
    clear_error=0;assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(active,&lifecycle,&result)==ESP_OK);
    before=clears;assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(active,&lifecycle,&result)==ESP_OK && clears==before);
    assert(esp32_mquickjs_wifi_radio_eap_clear_lifecycle(active,&old,&result)==ESP_ERR_INVALID_STATE && clears==before);
    /* Simulate the subsequent driver/helper suffix releasing its lifecycle;
     * those production bodies are composed by the enterprise-stop fixture. */
    s_radio.lifecycle=(esp32_mquickjs_wifi_radio_lifecycle_t){0};lifecycle=s_radio.lifecycle;
    assert(INSTALL()==0);active=s_eap_radio.identity;
    s_wifi_eap_stop.control.identity=1;config_busy=true;
    before=disconnects;assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && disconnects==before);
    stop_cleanup_ready=true;
    s_wifi_eap_stop.control.identity=0;config_busy=false;
    /* Runtime keeps helpers and credentials until disconnect AND reuse fences
     * are drained; later SDK clear failure is retried on the original identity. */
    config_busy=true;before=disconnects;assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && disconnects==before);config_busy=false;
    disconnect_pending=true;before=clears;assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && clears==before);
    disconnect_pending=false;helpers_idle=false;assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && clears==before);
    helpers_idle=true;clear_error=73;assert(!esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && s_eap_radio.identity==active);
    clear_error=0;assert(esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && !s_eap_radio.identity && !binding);
    before=disconnects;assert(esp32_mquickjs_wifi_eap_prepare_runtime_destroy() && disconnects==before);
    wifi_radio_operation_lock();wifi_radio_release_locked(&ap);wifi_radio_release_locked(&sta);wifi_radio_release_locked(&app);wifi_radio_operation_unlock();
    assert(!ap.acquired && !sta.acquired && !app.acquired && !locks && !critical && drains && releases);
    return 0;
}
