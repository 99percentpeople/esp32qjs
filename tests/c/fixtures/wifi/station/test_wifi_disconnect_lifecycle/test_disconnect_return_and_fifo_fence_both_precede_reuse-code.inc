
        wifi_process_driver_event(&done);
        const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
        wifi_process_driver_event(&fence);
        assert(s_wifi_state.connect_draining && s_wifi_state.disconnect_active);
        assert(!(event_bits & WIFI_LINK_DRAINED_BIT));
    }
    return disconnect_error;