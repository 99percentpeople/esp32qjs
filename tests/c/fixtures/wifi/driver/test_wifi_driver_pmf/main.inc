
#define DISABLE(iface) esp32_mquickjs_wifi_radio_disable_pmf(iface,&result)
int main(void) {
    pmf_reset();assert(DISABLE(WIFI_IF_MAX)==ESP_ERR_INVALID_ARG && !calls);
    assert(esp32_mquickjs_wifi_radio_disable_pmf(WIFI_IF_STA,NULL)==ESP_ERR_INVALID_ARG && !calls);
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(DISABLE(WIFI_IF_AP)==ESP_ERR_NOT_SUPPORTED && !calls);
#endif
    for(unsigned iface=0;iface<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 2U : 1U);++iface) {
        for(unsigned failure=0;failure<6;++failure) {
            pmf_reset();wifi_config_t other=configs[1-iface];
            if(failure<=3)fail_at=failure;
            corrupt_pmf=failure==4;corrupt_other=failure==5;
            assert(DISABLE(iface)==(!failure ? ESP_OK : failure<=3 ? 77 : ESP_ERR_INVALID_RESPONSE));
            assert(!memcmp(&other,&configs[1-iface],sizeof(other)) && !s_radio.started && s_radio.generation==7);
            if(failure==1)assert(!writes && !result.mutation_attempted && !s_radio.fault_stage);
            else {
                assert(writes==1 && result.mutation_attempted && !result.rollback_attempted);
                if(failure) {
                    assert(s_radio.fault_stage && s_radio.fault_error==result.error);
                    unsigned before=calls;fail_at=0;assert(DISABLE(iface)==ESP_ERR_INVALID_STATE && calls==before);
                } else {
                    unsigned before=calls;assert(DISABLE(iface)==ESP_OK && calls==before+1 && writes==1 && !result.mutation_attempted);
                }
            }
        }
        pmf_reset();s_radio.storage=WIFI_STORAGE_FLASH;fail_at=2;
        assert(DISABLE(iface)==77 && result.persistent_mutation_possible && !result.rollback_attempted);
        for(int auth=-1;auth<=WIFI_AUTH_MAX;++auth) {
            pmf_reset();if(iface==0)configs[0].sta.threshold.authmode=auth;else configs[1].ap.authmode=auth;
            bool allowed=auth==WIFI_AUTH_OPEN || auth==WIFI_AUTH_WEP || auth==WIFI_AUTH_WPA_PSK ||
                auth==WIFI_AUTH_WPA2_PSK || auth==WIFI_AUTH_WPA_WPA2_PSK || auth==WIFI_AUTH_ENTERPRISE || auth==WIFI_AUTH_WPA_ENTERPRISE;
            assert(DISABLE(iface)==(allowed ? ESP_OK : ESP_ERR_NOT_SUPPORTED));
            if(!allowed)assert(calls==1 && !writes && !result.mutation_attempted && !strcmp(result.stage,"pmf-security"));
        }
        pmf_reset();if(iface==0)configs[0].sta.disable_wpa3_compatible_mode=false;else configs[1].ap.wpa3_compatible_mode=true;
        assert(DISABLE(iface)==ESP_ERR_NOT_SUPPORTED && !writes);
        /* A recorded disabled predecessor is re-applied only when the SDK
         * enabled PMF while restoring config. Never reinterpret new input. */
        pmf_reset();wifi_config_t prior=configs[iface];
        wifi_pmf_config_t *pmf=iface==WIFI_IF_STA ? &prior.sta.pmf_cfg : &prior.ap.pmf_cfg;
        pmf->capable=pmf->required=false;
        wifi_radio_operation_lock();
        assert(wifi_radio_restore_disabled_pmf(iface,&prior)==ESP_OK && writes==1);
        unsigned before=calls;assert(wifi_radio_restore_disabled_pmf(iface,&prior)==ESP_OK && writes==1 && calls==before+1);
        wifi_radio_operation_unlock();
    }
    for(unsigned bad=0;bad<15;++bad) {
        pmf_reset();switch(bad) {
            case 0:s_radio.driver_owned=false;break;
            case 1:s_radio.storage_configured=false;break;
            case 2:s_radio.storage=99;break;
            case 3:s_radio.started=true;break;
            case 4:s_radio.stop_required=true;break;
            case 5:s_radio.restart_required=true;break;
            case 6:s_radio.lifecycle.identity=1;break;
            case 7:s_radio.operation.identity=1;break;
            case 8:s_radio.wake_locks=1;break;
            case 9:s_radio.promiscuous_claimed=true;break;
            case 10:s_tx_rate_lease.identity=1;break;
            case 11:s_tx_rate_lease.restore_pending=true;break;
            case 12:s_radio.fault_stage="other";break;
            case 13:s_radio.cleanup_stage="other";break;
            case 14:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTING;break;
        }
        assert(DISABLE(WIFI_IF_STA)==(bad ? ESP_ERR_INVALID_STATE : ESP_ERR_WIFI_NOT_INIT) && !calls);
    }
    for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i) {
        pmf_reset();s_radio.leases[i].identity=i+1;assert(DISABLE(WIFI_IF_STA)==ESP_ERR_INVALID_STATE && !calls);
    }
    assert(!locks && !critical && !helper_locks);return 0;
}
