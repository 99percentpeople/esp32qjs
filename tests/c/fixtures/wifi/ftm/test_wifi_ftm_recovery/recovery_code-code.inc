bool event_fence_posted,event_fence_seen;
        bool stop_submitted,ap_reopen_pending,ap_reopen_attempted,ap_reopen_quiesced,ap_reopen_restore_complete;
        wifi_mode_t event_live;unsigned ap_stop_phase;
        unsigned ap_transition_application,ap_transition_station,ap_transition_access_point;
        esp_err_t ap_stop_error,cleanup_error,channel_observation_error;
        uint64_t channel_observation_revision;uint8_t primary_channel;wifi_second_chan_t secondary_channel;