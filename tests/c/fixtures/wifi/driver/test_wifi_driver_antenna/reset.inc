
static void reset(void) {
    assert(!locks);memset(&s_radio,0,sizeof(s_radio));
    s_radio.driver_owned=s_radio.storage_configured=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_radio.lease_identity=19;s_radio.wake_locks=3;s_radio.promiscuous_claimed=true;
    memset(&stored_ant,0,sizeof(stored_ant));memset(&stored_gpio,0,sizeof(stored_gpio));
    stored_ant.rx_ant_mode=stored_ant.tx_ant_mode=ESP_PHY_ANT_MODE_AUTO;
    stored_ant.rx_ant_default=ESP_PHY_ANT_ANT1;stored_ant.enabled_ant0=15;stored_ant.enabled_ant1=14;
    for(unsigned i=0;i<4;++i){stored_gpio.gpio_cfg[i].gpio_select=i%2;stored_gpio.gpio_cfg[i].gpio_num=127-i;}
    reads=ant_reads=gpio_reads=0;sdk_error=0;
}
