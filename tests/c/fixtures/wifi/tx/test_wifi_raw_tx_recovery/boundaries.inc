
static int esp32_mquickjs_wifi_raw_tx_broker_quiesce(unsigned generation,const void *pointer) {
    const esp32_mquickjs_wifi_raw_tx_token_t *token=pointer;
    assert(locks && !critical && !s_radio.stop_required && token->identity==raw_native.token.identity);
    assert(generation==raw_native.generation);
    if(!raw_unregistered){++raw_quiesces;if(raw_unregister_error)return raw_unregister_error;raw_unregistered=true;}
    return ESP_OK;
}
static bool esp32_mquickjs_wifi_raw_tx_broker_retire(esp32_mquickjs_wifi_raw_tx_token_t *token) {
    assert(locks && !critical && token->identity==raw_native.token.identity);
    /* The Radio must not call this boundary for ordinary completion while
     * recovery owns the lifecycle; only physical proof can retire it now. */
    assert(raw_native.native_terminated);memset(&raw_native,0,sizeof(raw_native));memset(token,0,sizeof(*token));return true;
}

static bool esp32_mquickjs_wifi_raw_tx_broker_result(const esp32_mquickjs_wifi_raw_tx_token_t *token,
    esp32_mquickjs_wifi_raw_tx_broker_status_t *out) {
    *out=raw_native;return token->identity==raw_native.token.identity;
}
static uint32_t esp32_mquickjs_wifi_raw_tx_broker_owner_identity(uint32_t lease) {
    return raw_native.token.radio_lease_identity==lease ? raw_native.token.identity : 0;
}
