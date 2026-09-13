static int wifi_radio_begin_events(int phase,wifi_mode_t mode) {
        assert((phase==RADIO_EVENTS_START && (mode==WIFI_MODE_STA || mode==WIFI_MODE_AP) && !s_radio.started) ||
            (phase==RADIO_EVENTS_RESTART && mode==WIFI_MODE_STA && s_radio.started) ||
            (phase==RADIO_EVENTS_STA_START && mode==WIFI_MODE_STA && s_radio.started && native_mode==WIFI_MODE_AP));
        return sdk_step(false);
    }