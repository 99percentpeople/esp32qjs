
static wifi_tx_rate_config_t predecessor;
static void raw_setup(bool borrowed) {
    setup();recovery_admitted=false;raw_recovery_admitted=true;recovery_owner=73;
    s_radio.leases[0].identity=73;s_radio.operation.identity=0;
    predecessor=native_rates[0];
    if(borrowed) {
        s_tx_rate_lease=(esp32_mquickjs_wifi_tx_rate_lease_t){.identity=73,.generation=7,
            .write_identity=s_tx_rates.records[0].write_identity,.interface=WIFI_IF_STA,.previous=predecessor};
        native_rates[0].rate=WIFI_PHY_RATE_24M;s_tx_rates.records[0].config=native_rates[0];
    }
}
int main(void) {
    for(unsigned borrowed=0;borrowed<2;++borrowed) {
        raw_setup(borrowed);wifi_radio_operation_lock();
        esp32_mquickjs_wifi_tx_rate_state_t before=s_tx_rates;
        esp32_mquickjs_wifi_tx_rate_lease_t lease_before=s_tx_rate_lease;
        assert(!wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA));
        assert(s_config_restart.captured && !writes && !native_stops && !native_starts);
        assert(!memcmp(&s_tx_rates,&before,sizeof(before)) && !memcmp(&s_tx_rate_lease,&lease_before,sizeof(lease_before)));
        assert(!memcmp(&s_config_restart.snapshot->rates.configs[0],&predecessor,sizeof(predecessor)));
        assert(native_rates[0].rate==(borrowed?WIFI_PHY_RATE_24M:predecessor.rate));
        if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)
            assert(!memcmp(&s_config_restart.snapshot->rates.configs[1],&native_rates[1],sizeof(predecessor)));
        unsigned before_calls=calls;wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
        assert(!wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA) && calls==before_calls);
        assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
        /* Inject successful physical deinit and original owner retirement, whose
         * production producer is exercised in test_wifi_raw_tx_recovery. */
        memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));memset(s_radio.leases,0,sizeof(s_radio.leases));
        raw_recovery_admitted=false;new_driver();
        assert(!wifi_radio_restart_configs_replay_locked(&token));
        assert(!memcmp(&native_rates[0],&predecessor,sizeof(predecessor)));
        unsigned applied=rate_writes[0];
        assert(!wifi_radio_restart_configs_replay_locked(&token) && rate_writes[0]==applied);
        assert(!wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA));
        assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)) && !nvs_writes);
        wipe();wifi_radio_operation_unlock();
    }
    for(unsigned bad=0;bad<9;++bad) {
        raw_setup(true);wifi_radio_operation_lock();
        switch(bad) {
        case 0:s_tx_rate_lease.identity++;break;
        case 1:s_tx_rate_lease.generation++;break;
        case 2:s_tx_rate_lease.write_identity++;break;
        case 3:s_tx_rate_lease.interface=(wifi_interface_t)99;break;
        case 4:s_tx_rate_lease.previous.rate=(wifi_phy_rate_t)99999;break;
        case 5:s_tx_rate_lease.restore_pending=true;break;
        case 6:s_tx_rates.records[0].uncertain=true;break;
        case 7:s_tx_rates.records[0].known=false;break;
        case 8:s_tx_rates.next_identity=0;break;
        }
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)!=ESP_OK);
        assert(!s_config_restart.captured && !allocation && allocations==frees && !writes);
        assert(s_tx_rate_lease.identity && native_rates[0].rate==WIFI_PHY_RATE_24M);
        wifi_radio_operation_unlock();
    }
    raw_setup(true);wifi_radio_operation_lock();allocation_fail=true;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_NO_MEM);
    assert(!calls && !writes && s_tx_rate_lease.identity);wifi_radio_operation_unlock();
    raw_setup(true);raw_recovery_admitted=false;wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(!calls && !writes && !allocation);wifi_radio_operation_unlock();
    return 0;
}
