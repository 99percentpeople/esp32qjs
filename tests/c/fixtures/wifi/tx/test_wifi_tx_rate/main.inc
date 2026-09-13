
#define api(name) esp32_mquickjs_wifi_tx_rate_##name
#define radio(name) esp32_mquickjs_wifi_radio_##name
static wifi_tx_rate_config_t requested={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M};
static esp32_mquickjs_wifi_tx_rate_write_t write_result;
static esp32_mquickjs_wifi_tx_rate_record_t record;
static uint32_t generation;
static int apply(int first,int rollback) {
    writes=0;replies[0]=first;replies[1]=rollback;
    return radio(configure_tx_rate)(WIFI_IF_STA,&requested,&write_result,&record,&generation,NULL);
}
int main(void) {
    assert(radio(tx_rate_status)(WIFI_IF_STA,&record,&generation,NULL)==ESP_OK);
    assert(!record.known && !record.uncertain && !record.write_identity && generation==7 && !writes);
    s_radio.leases[2].identity=99;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);
    s_radio.leases[2].identity=0;
    s_radio.started=true;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.started=false;
    s_radio.stop_required=true;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.stop_required=false;
    s_radio.operation.identity=1;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.operation.identity=0;
    s_radio.lifecycle.identity=1;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.lifecycle.identity=0;
    s_radio.wake_locks=1;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.wake_locks=0;
    s_radio.promiscuous_claimed=true;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.promiscuous_claimed=false;
    assert(apply(0,0)==ESP_OK && writes==1 && record.known && record.config.rate==WIFI_PHY_RATE_6M);
    assert(write_result.accepted && record.write_identity==1 && record.generation==7);
    requested.rate=WIFI_PHY_RATE_9M;
    assert(apply(71,0)==71 && writes==2 && write_result.restored && write_result.rollback_attempted);
    assert(record.known && !record.uncertain && record.config.rate==WIFI_PHY_RATE_6M && record.error==71);
    assert(observed[0].rate==WIFI_PHY_RATE_9M && observed[1].rate==WIFI_PHY_RATE_6M && !s_radio.fault_stage);
    assert(apply(72,73)==72 && writes==2 && record.uncertain && !record.known && record.rollback_error==73);
    assert(s_radio.fault_stage==s_tx_rate_fault && s_radio.cleanup_stage==s_tx_rate_fault);
    assert(radio(tx_rate_status)(WIFI_IF_STA,&record,&generation,NULL)==ESP_OK && record.uncertain);
    assert(apply(0,0)==ESP_OK && writes==1 && record.known && !s_radio.fault_stage && !s_radio.cleanup_stage);
    /* A different lifecycle fault cannot be repaired through rate configuration. */
    s_radio.fault_stage="deinit";assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.fault_stage=NULL;
    s_radio.generation=8;assert(apply(74,0)==74 && writes==1 && !write_result.rollback_attempted && record.uncertain);
    /* Other-interface uncertainty is not cleared by a successful STA repair. */
    s_tx_rates.records[1].uncertain=true;
    assert(apply(0,0)==ESP_OK && record.known && s_radio.fault_stage==s_tx_rate_fault);
    writes=0;replies[0]=0;
    assert(radio(configure_tx_rate)(WIFI_IF_AP,&requested,&write_result,&record,&generation,NULL)==ESP_OK);
    assert(!s_radio.fault_stage && !api(uncertain)(&s_tx_rates));
    uint32_t identity=s_tx_rates.next_identity;
    api(invalidate)(&s_tx_rates);assert(s_tx_rates.next_identity==identity && !s_tx_rates.records[0].known);
    assert(apply(75,0)==75 && writes==1 && !write_result.rollback_attempted);
    assert(apply(0,0)==ESP_OK);
    s_tx_rates.next_identity=UINT32_MAX;assert(apply(0,0)==ESP_OK && record.write_identity==UINT32_MAX);
    assert(!s_tx_rates.next_identity && apply(0,0)==ESP_ERR_INVALID_STATE && !writes && !write_result.attempted);
    wifi_tx_rate_config_t invalid=requested;invalid.phymode=WIFI_PHY_MODE_HT20;
    assert(!api(valid)(&invalid));invalid.rate=WIFI_PHY_RATE_MCS7_SGI;assert(api(valid)(&invalid));
    assert(!strcmp(api(name)(WIFI_PHY_RATE_MCS7_SGI),"mcs7-short"));
    invalid.dcm=true;assert(!api(valid)(&invalid));
#if CONFIG_SOC_WIFI_HE_SUPPORT
    invalid.phymode=WIFI_PHY_MODE_HE20;invalid.rate=WIFI_PHY_RATE_MCS9_SGI;assert(api(valid)(&invalid));
#else
    invalid.phymode=WIFI_PHY_MODE_HE20;assert(!api(valid)(&invalid));
#endif
    assert(!api(name)(-1) && !api(name)(999));
    assert(!lock_depth && !critical_depth);
    return 0;
}
