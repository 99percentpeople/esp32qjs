static bool esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(unsigned generation) {
        assert(locks && !critical && deinit_calls && !s_radio.driver_owned && generation==raw_native.token.generation);
        raw_native.operation_active=false;raw_native.native_terminated=true;raw_native.generation=0;return true;
    }