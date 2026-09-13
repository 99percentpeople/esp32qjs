
static esp32_mquickjs_wifi_twt_probe_wake_snapshot_t wake_state;
void esp32_mquickjs_wifi_twt_probe_wake_snapshot(esp32_mquickjs_wifi_twt_probe_wake_snapshot_t *out) {
    *out=wake_state;
}
