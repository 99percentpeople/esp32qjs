
static void reset_vendor(void) {
    assert(!locks && !critical);memset(&s_radio,0,sizeof(s_radio));memset(&s_vendor_ie,0,sizeof(s_vendor_ie));
    memset(&s_interval,0,sizeof(s_interval));memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));
    memset(native_length,0,sizeof(native_length));memset(removal_calls,0,sizeof(removal_calls));
    native_calls=fail_call=invalidations=0;on_lock=NULL;
    s_radio.generation=7;s_radio.next_lease_identity=s_radio.next_lifecycle_identity=1;
    s_radio.started=s_radio.driver_owned=s_radio.storage_configured=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_radio.effective_mode=CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? WIFI_MODE_APSTA : WIFI_MODE_STA;
}
static const uint8_t element[]={221,4,1,2,3,4};
static int put(unsigned frame,unsigned index) {
    return esp32_mquickjs_wifi_radio_vendor_ie_set(wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)frame),
        (wifi_vendor_ie_type_t)frame,index,true,element,sizeof(element));
}
static unsigned owners(void) {
    esp32_mquickjs_wifi_vendor_ie_status_t status;esp32_mquickjs_wifi_radio_vendor_ie_status(&status);return status.owners;
}
static int lifecycle(void) {
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};wifi_radio_operation_lock();
    int err=wifi_radio_begin_lifecycle_locked(NULL,NULL,NULL,&token);wifi_radio_operation_unlock();return err;
}
static void late_lifecycle(void) {s_radio.lifecycle.identity=999;}
