unsigned event_phase;wifi_mode_t event_live;
            bool stop_submitted,ap_reopen_pending,ap_reopen_attempted,ap_reopen_quiesced,ap_reopen_restore_complete;unsigned ap_stop_phase;
            unsigned ap_transition_application,ap_transition_station,ap_transition_access_point;
            esp_err_t ap_stop_error,cleanup_error;bool driver_owned,