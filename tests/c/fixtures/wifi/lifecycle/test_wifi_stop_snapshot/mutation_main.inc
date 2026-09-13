
/* The SDK can fail after a setter is attempted; its result must never preserve
 * the old STOP observation merely because this setter missed the inventory. */
static int (esp_wifi_enable_bsscolor_collision_detection)(wifi_interface_t interface,bool enabled) {
    assert(interface==WIFI_IF_STA && enabled);return sdk_step(true);
}
static unsigned argument_calls;
static wifi_mode_t next_mode(void) {++argument_calls;return WIFI_MODE_STA;}
static void stopped_candidate(void) {
    reset_observation();observe();
    assert(!read_snapshot().unchanged); /* Native STOP and fence have not completed. */
    s_radio.started=false;s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(read_snapshot().unchanged);
}
int main(void) {
    stopped_candidate();
    unsigned before=calls;wifi_ps_type_t ps;
    wifi_radio_operation_lock();assert(esp_wifi_get_ps(&ps)==ESP_OK);wifi_radio_operation_unlock();
    assert(read_snapshot().unchanged && calls==before+1);
    s_radio.stop_required=true;assert(!read_snapshot().unchanged);s_radio.stop_required=false;
    s_radio.generation++;assert(!read_snapshot().unchanged);s_radio.generation--;
    s_radio.event_identity++;assert(!read_snapshot().unchanged);s_radio.event_identity--;
    s_radio.storage_configured=false;assert(!read_snapshot().unchanged);s_radio.storage_configured=true;
    s_radio.cleanup_stage="stop-events";assert(!read_snapshot().unchanged);s_radio.cleanup_stage=NULL;
    assert(read_snapshot().unchanged);
    fail_at=calls+1;argument_calls=0;
    wifi_radio_operation_lock();assert(esp_wifi_set_mode(next_mode())==77);wifi_radio_operation_unlock();
    assert(argument_calls==1 && !read_snapshot().unchanged);
    esp32_mquickjs_wifi_radio_stop_snapshot_t historical=read_snapshot();
    assert(historical.error==ESP_OK && historical.tx_power==52 && historical.stop_identity==51);
    fail_at=0;
    wifi_radio_operation_lock();assert(esp_wifi_set_mode(WIFI_MODE_STA)==ESP_OK);wifi_radio_operation_unlock();
    assert(!read_snapshot().unchanged); /* A successful rollback does not renew proof. */
    stopped_candidate();fail_at=calls+1;
    wifi_radio_operation_lock();assert(esp_wifi_enable_bsscolor_collision_detection(WIFI_IF_STA,true)==77);wifi_radio_operation_unlock();
    assert(!read_snapshot().unchanged);fail_at=0;
    stopped_candidate();
    wifi_radio_operation_lock();assert(esp_wifi_set_storage(WIFI_STORAGE_RAM)==ESP_OK);wifi_radio_operation_unlock();
    assert(read_snapshot().unchanged); /* Reviewed policy-only writer preserves RF history. */
    wifi_radio_operation_lock();assert(esp_wifi_set_rssi_threshold(-75)==ESP_OK);wifi_radio_operation_unlock();
    assert(read_snapshot().unchanged); /* Observer request is not a saved RF setting. */
    stopped_candidate();
    wifi_radio_operation_lock();wifi_radio_invalidate_stop_snapshot_locked();wifi_radio_operation_unlock();
    assert(!read_snapshot().unchanged);
    assert(!locks && !critical && !helper_locks && !allocation);
    return 0;
}
