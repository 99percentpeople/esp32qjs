static int esp_wifi_set_channel(uint8_t primary,wifi_second_chan_t secondary) {
        assert(s_radio.started && native_mode==WIFI_MODE_AP && native_storage==WIFI_STORAGE_RAM);
        ++channel_writes;csa_primary=primary;csa_secondary=secondary;csa_pending=true;return sdk_step(true);
    }