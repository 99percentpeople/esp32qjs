
int main(void) {
    esp_wps_config_t config={.wps_type=WPS_TYPE_PIN};
    esp32_mquickjs_wifi_wps_worker_t *w=NULL;
    memcpy(config.pin,"12345671",9);
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_ERR_INVALID_ARG && !allocations);
    memcpy(config.pin,"12345670",9);
    memset(config.factory_info.manufacturer,'x',sizeof(config.factory_info.manufacturer));
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_ERR_INVALID_ARG && !allocations);
    config.factory_info.manufacturer[0]=0;
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_OK);
    assert(esp32_mquickjs_wifi_wps_worker_start(w)==ESP_ERR_INVALID_STATE && !start_calls);
    assert(esp32_mquickjs_wifi_wps_worker_prepare(w)==ESP_OK && !start_calls);
    assert(esp32_mquickjs_wifi_wps_worker_prepare(w)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_wps_worker_start(w)==ESP_OK && start_calls==1);
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_ERR_INVALID_STATE && allocations==1);
    for(size_t i=0;i<sizeof(w->config);i++)assert(!((uint8_t *)&w->config)[i]);
    uint8_t pin[8];
    assert(esp32_mquickjs_wifi_wps_worker_pin(w,pin,false)==ESP_OK && !memcmp(pin,"12345670",8));
    assert(!commit_calls);
    for(size_t i=0;i<sizeof(w->result);i++)assert(!((uint8_t *)&w->result)[i]);
    assert(esp32_mquickjs_wifi_wps_worker_pin(w,NULL,true)==ESP_OK && commit_calls==1);
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==ESP_ERR_INVALID_STATE);
    native.terminal_seen=true;native.credentials_available=true;
    create_error=301;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==301 && !retire_calls);
    create_error=0;start_error=302;
    unsigned completed_stops=stop_calls;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==302 && stop_calls==completed_stops);
    start_error=0;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==ESP_ERR_NOT_FINISHED && !retire_calls);
    fire_timer();stop_error=303;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==303 && !retire_calls);
    stop_error=0;delete_error=304;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==304 && !retire_calls);
    unsigned stopped=stops;
    delete_error=0;
    assert(esp32_mquickjs_wifi_wps_worker_finish_capture(w)==ESP_OK && retire_calls==1 && stops==stopped);
    esp32_mquickjs_wifi_wps_credentials_t credentials;
    assert(esp32_mquickjs_wifi_wps_worker_credentials(w,&credentials,false)==ESP_OK);
    assert(credentials.count==1 && !memcmp(credentials.entries[0].ssid,"wps",3));
    assert(native.credentials_available);
    assert(esp32_mquickjs_wifi_wps_worker_credentials(w,NULL,true)==ESP_OK && !native.credentials_available);
    release_error=ESP_ERR_NOT_FINISHED;
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_ERR_NOT_FINISHED && !w->capture_retired);
    release_error=0;
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_ERR_NOT_FINISHED && timer_exists);
    fire_timer();
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_OK && w->retired && !w->identity);
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_OK);
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_OK && !w && !allocations);
    reset_boundaries();

    /* Definite pre-handoff OOM permits cleanup of an owner with no native ID. */
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_OK);ipc_mode=1;
    assert(esp32_mquickjs_wifi_wps_worker_prepare(w)==ESP_ERR_NO_MEM && !w->handoff_unknown);
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_OK && !creates);
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_OK);
    reset_boundaries();

    /* An unconfirmed copy must retain its output and surface the raw IPC
     * error even when start itself succeeded. */
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_OK);
    assert(esp32_mquickjs_wifi_wps_worker_prepare(w)==ESP_OK);
    assert(esp32_mquickjs_wifi_wps_worker_start(w)==ESP_OK);
    ipc_mode=2;
    assert(esp32_mquickjs_wifi_wps_worker_pin(w,pin,false)==909 && w->handoff_unknown);
    esp32_mquickjs_wifi_wps_worker_status_t copy_status;
    assert(esp32_mquickjs_wifi_wps_worker_status(w,&copy_status)==ESP_OK && copy_status.error==909);
    assert(pending.fn(pending.arg)==ESP_OK && !memcmp(w->result.pin,"12345670",8));
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wireless_secure_zero(w,sizeof(*w));heap_caps_free(w);w=NULL;
    reset_boundaries();

    /* Unknown handoff retains argument/result storage, including after a late
     * receipt. It does not create an automatic retry or recovery policy. */
    assert(esp32_mquickjs_wifi_wps_worker_create(&config,&w)==ESP_OK);ipc_mode=2;
    assert(esp32_mquickjs_wifi_wps_worker_prepare(w)==909 && w->handoff_unknown);
    esp32_mquickjs_wifi_wps_worker_status_t status;
    w->native.identity=UINT64_MAX;
    assert(esp32_mquickjs_wifi_wps_worker_status(w,&status)==ESP_OK && !status.native.identity && status.handoff_unknown);
    assert(esp32_mquickjs_wifi_wps_worker_close(w)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_ERR_INVALID_STATE && allocations==1);
    assert(pending.fn(pending.arg)==ESP_OK);
    assert(esp32_mquickjs_wifi_wps_worker_status(w,&status)==ESP_OK && !status.native.identity && status.handoff_unknown);
    assert(esp32_mquickjs_wifi_wps_worker_release(&w)==ESP_ERR_INVALID_STATE);
    /* Fixture disposal after the simulated task returned, not production
     * close or proof of device/runtime recovery from an unknown handoff. */
    esp32_mquickjs_wireless_secure_zero(w,sizeof(*w));heap_caps_free(w);
    return 0;
}
