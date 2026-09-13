
static void reset(void) {
    assert(!locks);memset(&s_radio,0,sizeof(s_radio));
    s_radio.driver_owned=s_radio.storage_configured=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    sdk_calls=locked_calls=0;sdk_error=0;seen_mask=0;
}
