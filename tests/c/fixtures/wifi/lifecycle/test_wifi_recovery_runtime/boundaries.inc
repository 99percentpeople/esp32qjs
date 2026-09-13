
static wifi_mode_t recovery_mode;
static unsigned recovery_handoffs;
static esp32_mquickjs_wifi_recovery_request_t original={9,51};
static int esp32_mquickjs_wifi_radio_begin_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const esp32_mquickjs_wifi_radio_lease_t *ap,const esp32_mquickjs_wifi_recovery_request_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t *mode) {
    assert(app==&s_wifi_application && sta==&s_wifi_state.radio_lease);
    assert(ap==(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?&s_ap_lease:NULL));
    assert(!token->identity && !token->generation);++begin_calls;*mode=WIFI_MODE_NULL;
    if(foreign_owner || operation->identity!=original.identity || operation->generation!=original.generation || operation->kind!=original.kind)
        return ESP_ERR_INVALID_STATE;
    native_token=(esp32_mquickjs_wifi_radio_lifecycle_t){9,41};*token=native_token;
    *mode=recovery_mode;recovery_admitted=true;return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_checkpoint_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token,wifi_mode_t mode) {
    assert(recovery_admitted && running);target_mode=mode;
    int err=phase(1,token);if(!err)checkpoint=true;return err;
}
static int esp32_mquickjs_wifi_radio_finish_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(exact(token) && recovery_admitted && !recovery_owner_pending && !running);
    assert(!sta_storage && !s_ap_netif);++recovery_handoffs;recovery_admitted=false;return ESP_OK;
}
