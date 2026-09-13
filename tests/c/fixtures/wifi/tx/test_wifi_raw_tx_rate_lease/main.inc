
#define radio(name) esp32_mquickjs_wifi_radio_##name
static wifi_tx_rate_config_t requested={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_9M};
static uint8_t channel;
static int open_rate(esp32_mquickjs_wifi_radio_lease_t *lease) {
    return radio(raw_tx_acquire)(ESP32_MQUICKJS_WIFI_RAW_TX_STATION,6,&requested,lease,&channel);
}
static void baseline(void) {
    s_tx_rates.records[0]=(esp32_mquickjs_wifi_tx_rate_record_t){.known=true,.generation=7,.write_identity=1,
        .config={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M}};
}
int main(void) {
    (void)esp32_mquickjs_wifi_radio_5ghz_channel_bit;
    esp32_mquickjs_wifi_radio_lease_t lease={0},other={0};
    assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !lease.acquired && !writes && !starts);
    baseline();s_radio.started=true;assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !writes);s_radio.started=false;
    s_radio.wake_locks=1;assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !writes);s_radio.wake_locks=0;
    s_radio.leases[0].identity=99;assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !writes);s_radio.leases[0].identity=0;
    assert(radio(raw_tx_acquire)(ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT,6,&requested,&lease,&channel)==ESP_ERR_NOT_SUPPORTED && !writes);
    uint32_t next=s_tx_rates.next_identity;s_tx_rates.next_identity=UINT32_MAX;
    assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !writes);s_tx_rates.next_identity=next;
    /* Production transaction admission runs before allocating a Radio owner. */
    for(unsigned bad=0;bad<4;++bad) {
        baseline();
        switch(bad) {
        case 0:s_tx_rates.records[0].uncertain=true;break;
        case 1:s_tx_rates.records[0].write_identity=0;break;
        case 2:s_tx_rates.records[0].config.rate=(wifi_phy_rate_t)9999;break;
        case 3:s_tx_rate_lease.restore_pending=true;break;
        }
        uint32_t identity_before=s_radio.next_lease_identity;
        assert(open_rate(&lease)==ESP_ERR_INVALID_STATE && !lease.acquired && !writes && !starts);
        assert(s_radio.next_lease_identity==identity_before);
        s_tx_rate_lease.restore_pending=false;
    }
    baseline();
    assert(open_rate(&lease)==ESP_OK && lease.acquired && native_rate.rate==WIFI_PHY_RATE_9M);
    assert(s_tx_rate_lease.identity==lease.identity && starts==1 && writes==1);
    esp32_mquickjs_wifi_radio_lease_t stale=lease;stale.generation++;
    assert(radio(release_and_stop_idle)(&stale)==ESP_ERR_INVALID_STATE && stale.acquired && !stops);
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&other)==ESP_ERR_INVALID_STATE);
    assert(wifi_radio_stop_locked()==ESP_ERR_INVALID_STATE && !stops);
    wifi_radio_release_locked(&lease);assert(lease.acquired);
    wifi_radio_operation_unlock();
    /* A pending native packet excludes stop/restore, preserving owner/grant. */
    s_radio.leases[0].raw_tx_identity=88;
    assert(radio(release_and_stop_idle)(&lease)==ESP_ERR_INVALID_STATE && !stops && lease.acquired);
    s_radio.leases[0].raw_tx_identity=0;
    wait_error=51;assert(radio(release_and_stop_idle)(&lease)==51 && stops==1 && writes==1 && lease.acquired);
    wait_error=0;next_write[0]=52;next_write[1]=0;
    assert(radio(release_and_stop_idle)(&lease)==52 && stops==1 && writes==3 && lease.acquired);
    assert(native_rate.rate==WIFI_PHY_RATE_9M && s_tx_rate_lease.restore_pending && s_tx_rate_lease.restore_error==52);
    esp32_mquickjs_wifi_tx_rate_record_t record;esp32_mquickjs_wifi_tx_rate_lease_t temporary;uint32_t generation;
    assert(radio(tx_rate_status)(WIFI_IF_STA,&record,&generation,&temporary)==ESP_OK);
    assert(temporary.identity==lease.identity && temporary.previous.rate==WIFI_PHY_RATE_6M && record.known);
    assert(radio(release_and_stop_idle)(&lease)==ESP_OK && !lease.acquired && !s_tx_rate_lease.identity && writes==4 && stops==1);
    assert(native_rate.rate==WIFI_PHY_RATE_6M && !s_radio.fault_stage && !s_radio.cleanup_stage);
    assert(radio(release_and_stop_idle)(&lease)==ESP_OK && writes==4 && stops==1);
    /* Failed initial write + failed rollback retains the exact predecessor. */
    next_write[0]=61;next_write[1]=62;
    assert(open_rate(&lease)==61 && lease.acquired && s_tx_rate_lease.restore_pending && starts==1);
    assert(radio(release_and_stop_idle)(&lease)==ESP_OK && !lease.acquired && native_rate.rate==WIFI_PHY_RATE_6M && stops==1);
    /* A successful borrow followed by failed start restores, preserving its
     * unrelated lifecycle diagnostic. The rate adapter does not reset driver. */
    start_error=71;assert(open_rate(&lease)==71 && lease.acquired);
    assert(radio(release_and_stop_idle)(&lease)==ESP_OK && !lease.acquired && !s_tx_rate_lease.identity);
    assert(native_rate.rate==WIFI_PHY_RATE_6M && s_radio.fault_error==71 && !strcmp(s_radio.fault_stage,"start"));
    assert(!locks && !critical);
    return 0;
}
