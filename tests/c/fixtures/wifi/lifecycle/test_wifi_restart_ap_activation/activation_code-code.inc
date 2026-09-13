static int esp_wifi_set_mode(wifi_mode_t mode) {
        if(s_radio.started){assert(native_mode==WIFI_MODE_AP && mode==WIFI_MODE_APSTA && !csa_pending);
            station_activations++;if(station_channel_drift)native_channel=native_home=11;}
        native_mode=mode;return sdk_step(true);
    }