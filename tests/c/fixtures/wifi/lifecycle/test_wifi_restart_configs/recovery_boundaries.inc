
static bool raw_recovery_admitted;
static bool wifi_radio_raw_tx_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return raw_recovery_admitted && token && token->identity==s_radio.lifecycle.identity && token->generation==s_radio.lifecycle.generation;
}
static uint32_t wifi_radio_raw_tx_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static bool recovery_admitted;
static uint32_t recovery_owner;
static uint32_t wifi_radio_raw_tx_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return wifi_radio_raw_tx_recovery_exact_locked(token) ? recovery_owner : 0;
}
static unsigned saved_phy_reads;
static bool change_home_after_phy;
static bool wifi_radio_action_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return recovery_admitted && token && token->identity==s_radio.lifecycle.identity && token->generation==s_radio.lifecycle.generation;
}
static uint32_t wifi_radio_action_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    return wifi_radio_action_recovery_exact_locked(token)?recovery_owner:0;
}
static int esp32_mquickjs_wifi_action_sdk_saved_phy(wifi_interface_t interface,esp32_mquickjs_wifi_saved_phy_t *output) {
    assert(locks && !critical && interface<=1);++saved_phy_reads;
    *output=(esp32_mquickjs_wifi_saved_phy_t){.protocols=native_protocols[interface],.bandwidths=native_bandwidths[interface]};
    if(change_home_after_phy)native_home=6;
    return sdk_step(false);
}
