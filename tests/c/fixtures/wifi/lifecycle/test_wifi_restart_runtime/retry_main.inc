
int main(void) {
    esp32_mquickjs_wifi_configuration_execution_t result;
    for(unsigned off=0;off<2;++off) {
        for(unsigned ap=0;ap<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?2U:1U);++ap) {
            wifi_mode_t work=ap?WIFI_MODE_APSTA:WIFI_MODE_STA;
            stopped_setup(off?WIFI_MODE_NULL:work);history_missing=true;
            off_source=off;off_needs_ap=off&&ap;phase_failure=off?12:9;
            assert(esp32_mquickjs_wifi_restart_stopped_interfaces(ap,&result)==-(100+phase_failure));
            assert(s_wifi_lifecycle.identity==41 && checkpoint && !temporary && running);
            retry_allowed=true;phase_failure=0;
            unsigned captured=phase_calls[1],begun=begin_calls,rebuilt=phase_calls[2];
            assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(ap,&result));
            assert(result.admitted && result.stop_attempted && result.configuration_attempted && result.resume_attempted);
            assert(retry_admissions==1 && begin_calls==begun && phase_calls[1]==captured && phase_calls[2]==rebuilt+1);
            assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity && !temporary);
            assert(running==!off && sta_storage==!off && (s_ap_netif!=NULL)==(!off&&ap));
            /* A STOP failure cannot jump to helper retirement or another init. */
            stopped_setup(off?WIFI_MODE_NULL:work);history_missing=true;
            off_source=off;off_needs_ap=off&&ap;phase_failure=off?12:9;
            assert(esp32_mquickjs_wifi_restart_stopped_interfaces(ap,&result)!=ESP_OK);
            retry_allowed=true;phase_failure=0;fail_at=1;
            captured=phase_calls[1];rebuilt=phase_calls[2];unsigned retired=ap_calls+sta_calls;
            assert(esp32_mquickjs_wifi_restart_stopped_interfaces(ap,&result)==-1);
            assert(result.admitted && s_wifi_lifecycle.identity==41 && checkpoint && !temporary);
            assert(phase_calls[1]==captured && phase_calls[2]==rebuilt && ap_calls+sta_calls==retired);
            fail_at=0;
            assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(ap,&result));
            assert(phase_calls[1]==captured && phase_calls[2]==rebuilt+1 && !s_wifi_lifecycle.identity);
        }
    }
    stopped_setup(WIFI_MODE_NULL);history_missing=true;phase_failure=9;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-109);
    assert(phase_calls[1]==1 && !phase_calls[10]);
    retry_allowed=true;phase_failure=0;
    assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result));
    assert(phase_calls[1]==1 && phase_calls[2]==2 && !phase_calls[10] && running && !s_wifi_lifecycle.identity);
    /* Rejection by the real Radio admission (separately exercised) does not
     * consume central cleanup. Allocation failure also preserves its source. */
    stopped_setup(WIFI_MODE_STA);history_missing=true;phase_failure=9;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-109);
    retry_allowed=false;unsigned stopped=stop_calls;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE);
    assert(!result.admitted && s_wifi_lifecycle.identity==41 && checkpoint && stop_calls==stopped);
    phase_failure=0;assert(!wifi_finish_configuration_cleanup());
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    stopped_setup(WIFI_MODE_APSTA);history_missing=true;phase_failure=9;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-109);
    retry_allowed=alloc_fail=true;phase_failure=0;stopped=stop_calls;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_NO_MEM);
    assert(result.admitted && s_wifi_lifecycle.identity==41 && checkpoint && stop_calls==stopped && !temporary);
    alloc_fail=false;assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result));
#endif
    return 0;
}
