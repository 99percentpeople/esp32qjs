
static esp32_mquickjs_wifi_radio_lease_t helper[3];
static esp_wps_config_t options={.wps_type=WPS_TYPE_PIN};
static void setup(void) {
    assert(!s_wps_radio && !allocations && !worker_live);reset_vendor();memset(helper,0,sizeof(helper));
    s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_operation_identity=1;s_radio.storage=WIFI_STORAGE_FLASH;
    wifi_radio_operation_lock();
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&helper[0]));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&helper[1]));
    wifi_radio_operation_unlock();
    storage=WIFI_STORAGE_FLASH;channel=6;secondary=WIFI_SECOND_CHAN_NONE;
    memset(&original,0,sizeof(original));memcpy(original.sta.ssid,"original",9);memcpy(original.sta.password,"secret",7);
    actual=original;prepare_error=start_error=close_error=storage_error=config_error=channel_error=fence_error=0;
    peer_error=ESP_ERR_WIFI_NOT_CONNECT;corrupt_config=fail_binding=false;
    prepares=starts=config_writes=storage_writes=channel_writes=releases=0;
    s_wps_fence_posted=s_wps_fence_seen=0;memset(&posted,0,sizeof(posted));
}
static void pinned(void) {
    wifi_radio_operation_lock();for(unsigned i=0;i<3;i++)if(helper[i].acquired) {
        wifi_radio_release_locked(&helper[i]);assert(wifi_radio_lease_valid(&helper[i]));
    }wifi_radio_operation_unlock();
}
static void deliver(void) {
    assert(posted.identity);wifi_radio_operation_lock();wifi_radio_wps_observe_fence(&posted);wifi_radio_operation_unlock();
}
static void close_fenced(esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(esp32_mquickjs_wifi_radio_wps_close(token,status)==ESP_ERR_NOT_FINISHED && token->identity);
    deliver();assert(!esp32_mquickjs_wifi_radio_wps_close(token,status) && !token->identity && !s_wps_radio);
}
int main(void) {
    setup();esp32_mquickjs_wifi_radio_operation_t token={0};esp32_mquickjs_wifi_wps_radio_status_t status;
    peer_error=0;assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==ESP_ERR_INVALID_STATE && !prepares && !token.identity);
    peer_error=ESP_ERR_WIFI_NOT_CONNECT;fail_binding=true;
    assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==ESP_ERR_NO_MEM && !allocations && !worker_live);fail_binding=false;
    unsigned released_before_start=releases;
    assert(!esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status) && starts==1 && storage==WIFI_STORAGE_RAM);
    pinned();esp32_mquickjs_wifi_radio_operation_t stale=token;stale.generation++;
    assert(esp32_mquickjs_wifi_radio_wps_close(&stale,&status)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_radio_end_operation(&token);assert(token.identity && s_radio.operation.identity);
    esp32_mquickjs_wifi_radio_lease_t foreign={0};wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&foreign)==ESP_ERR_INVALID_STATE);wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_wps_credentials_t credentials;
    assert(esp32_mquickjs_wifi_radio_wps_credentials(&token,&credentials,false)==ESP_ERR_INVALID_STATE);
    close_error=600;assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==600 && !config_writes);pinned();
    close_error=0;config_error=601;assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==601 && !channel_writes && storage==WIFI_STORAGE_RAM);pinned();
    config_error=0;corrupt_config=true;
    assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==ESP_ERR_INVALID_RESPONSE && !status.config_restored);
    corrupt_config=false;channel_error=602;
    assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==602 && status.config_restored && storage==WIFI_STORAGE_RAM);
    unsigned writes=config_writes;channel_error=0;storage_error=603;
    assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==603 && status.channel_restored && !s_radio.storage_configured);
    storage_error=0;fence_error=604;
    assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==604 && status.storage_restored && releases==released_before_start);
    assert(config_writes==writes && storage==WIFI_STORAGE_FLASH && !memcmp(&actual,&original,sizeof(actual)));pinned();
    fence_error=0;assert(esp32_mquickjs_wifi_radio_wps_close(&token,&status)==ESP_ERR_NOT_FINISHED);
    wifi_radio_operation_lock();wifi_radio_wps_observe_fence(&stale);wifi_radio_operation_unlock();assert(!s_wps_fence_seen);
    deliver();assert(!esp32_mquickjs_wifi_radio_wps_close(&token,&status) && !allocations && !worker_live && !token.identity);

    setup();prepare_error=701;
    assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==701 && token.identity && !storage_writes && !starts);
    close_fenced(&token,&status);assert(!config_writes && !channel_writes && !storage_writes);
    setup();storage_error=702;
    assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==702 && token.identity && !starts && !s_radio.storage_configured);
    storage_error=0;close_fenced(&token,&status);assert(!config_writes && !channel_writes && storage==WIFI_STORAGE_FLASH);

    setup();assert(!esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status));
    assert(esp32_mquickjs_wifi_radio_wps_finish_capture(&token,&status)==ESP_ERR_NOT_FINISHED);deliver();
    assert(!esp32_mquickjs_wifi_radio_wps_finish_capture(&token,&status));
    assert(!esp32_mquickjs_wifi_radio_wps_credentials(&token,&credentials,false) && credentials.count==1);
    assert(!esp32_mquickjs_wifi_radio_wps_credentials(&token,NULL,true));
    assert(!esp32_mquickjs_wifi_radio_wps_prepare_close(&token,&status) && token.identity && s_wps_radio);
    pinned();
    assert(!esp32_mquickjs_wifi_radio_wps_close(&token,&status) && !allocations);

    setup();wifi_radio_operation_lock();
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&helper[2]));
    wifi_radio_operation_unlock();s_radio.effective_mode=WIFI_MODE_APSTA;
    assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==ESP_ERR_INVALID_STATE && !prepares);
    assert(!esp32_mquickjs_wifi_radio_wps_begin(helper,true,&options,&token,&status));pinned();close_fenced(&token,&status);
    setup();s_radio.next_operation_identity=0;
    assert(esp32_mquickjs_wifi_radio_wps_begin(helper,false,&options,&token,&status)==ESP_ERR_NO_MEM && !prepares && !token.identity);
    return 0;
}
