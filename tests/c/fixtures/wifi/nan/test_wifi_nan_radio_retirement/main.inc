
int main(void) {
    s_radio.generation=7;s_radio.driver_owned=true;s_radio.started=true;s_radio.stop_required=true;
    s_radio.effective_mode=4;s_radio.event_live=4;
    s_radio.operation=(esp32_mquickjs_wifi_radio_operation_t){7,3,5,ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN};
    s_nan_radio=calloc(1,sizeof(*s_nan_radio));assert(s_nan_radio);
    s_nan_radio->lease=(esp32_mquickjs_wifi_radio_lease_t){7,3,0,true};
    s_nan_radio->status.operation=s_radio.operation;
    s_nan_radio->status.start_attempted=s_nan_radio->status.start_accepted=true;
    s_nan_radio->netif=&netif;s_nan_radio->observer=9;
    s_nan_radio->prepare_attempted=s_nan_radio->saved=s_nan_radio->storage_attempted=true;
    s_nan_radio->tx_owned=true;
    s_nan_radio->saved_mode=1;s_nan_radio->saved_storage=0;
    esp32_mquickjs_wifi_radio_operation_t token=s_radio.operation,stale=token;
    esp32_mquickjs_wifi_nan_radio_status_t status={0};
    --stale.identity;
    assert(esp32_mquickjs_wifi_radio_nan_close(&stale,&status)==ESP_ERR_INVALID_STATE&&!stops&&!frees);
    stale=token;--stale.generation;
    assert(esp32_mquickjs_wifi_radio_nan_close(&stale,&status)==ESP_ERR_INVALID_STATE&&!stops);
    service_callback_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&!stops&&token.identity&&tx_seals==1);
    service_callback_error=0;
    wait_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&token.identity&&stops==1&&!resets);
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&stops==1&&waits==2);
    wait_error=0;tx_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&stops==1&&!resets&&tx_closes==1);
    assert(s_nan_radio->tx_owned&&token.identity&&!releases);
    tx_error=0;detach_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&stops==1&&resets==1&&detaches==1&&!frees);
    detach_error=0;observer_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_ERR_TIMEOUT&&resets==1&&detaches==2&&unobserves==1&&!mode_writes);
    observer_error=0;mode_error=-71;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==-71&&token.identity&&mode_writes==1&&!releases);
    /* A failed setter that nevertheless reached the desired mode is read back
     * on retry; it is not submitted twice. Original failure was returned above. */
    mode_error=0;
    stale=token;
    assert(esp32_mquickjs_wifi_radio_nan_close(&token,&status)==ESP_OK&&!token.identity&&!s_nan_radio);
    assert(stops==1&&resets==1&&detaches==2&&unobserves==2&&mode_writes==1&&storage_writes==1&&releases==1&&frees==1);
    assert(status.stopped&&status.native_reset&&status.netif_retired&&status.observer_retired&&status.mode_restored&&status.storage_restored);
    assert(!s_radio.cleanup_stage&&s_radio.effective_mode==1&&!s_radio.started&&!s_radio.stop_required);
    assert(esp32_mquickjs_wifi_radio_nan_close(&stale,&status)==ESP_ERR_INVALID_STATE&&frees==1);
}
