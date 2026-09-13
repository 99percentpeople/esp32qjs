
static void reset(void) {
    assert(!locks && !critical);memset(&s_radio,0,sizeof(s_radio));
    s_radio.driver_owned=s_radio.storage_configured=s_radio.started=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;s_radio.generation=7;
    native_mode=WIFI_MODE_STA;current_channel=6;home_channel=1;second=WIFI_SECOND_CHAN_NONE;
    country=(wifi_country_t){.cc="US",.schan=1,.nchan=11,.max_tx_power=20,.policy=WIFI_COUNTRY_POLICY_AUTO};
    reads=current_reads=home_reads=0;sdk_error=0;overlap=false;
}
