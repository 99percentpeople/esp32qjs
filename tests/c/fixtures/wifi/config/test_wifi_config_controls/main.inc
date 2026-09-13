
#undef calloc
#undef free
static wifi_config_t requested[2],before[2];
static esp32_mquickjs_wifi_radio_config_controls_t controls;
static bool accept_config(const wifi_config_t *a,const wifi_config_t *b) { return !memcmp(a,b,sizeof(*a)); }
static void reset(void) {
    assert(!depth && !critical && !alloc_live);
    memset(&s_radio,0,sizeof(s_radio));memset(&controls,0,sizeof(controls));memset(&result,0,sizeof(result));
    s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.lifecycle=(esp32_mquickjs_wifi_radio_lifecycle_t){.identity=7,.generation=3};
    native_mode=s_radio.effective_mode=WIFI_MODE_STA;native_storage=s_radio.storage=WIFI_STORAGE_RAM;
    native_country=(wifi_country_t){.cc="US",.schan=1,.nchan=11,.max_tx_power=20,.policy=WIFI_COUNTRY_POLICY_MANUAL};
    native_ps=WIFI_PS_NONE;native_band=WIFI_BAND_MODE_2G_ONLY;
    memset(native_config,0,sizeof(native_config));native_config[0].sta.ssid[0]='s';native_config[0].sta.password[0]='x';
    native_config[0].sta.pmf_cfg.capable=true;
    native_config[1].ap.ssid[0]='a';native_config[1].ap.ssid_len=1;native_config[1].ap.channel=1;
    memcpy(before,native_config,sizeof(before));memcpy(requested,before,sizeof(requested));
    requested[0].sta.ssid[0]='n';requested[1].ap.ssid[0]='b';
    for(int i=0;i<2;i++) {
        native_protocols[i]=(wifi_protocols_t){.ghz_2g=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N};
        native_bandwidths[i]=(wifi_bandwidths_t){.ghz_2g=WIFI_BW20};
        controls.protocol_bands[i]=controls.bandwidth_bands[i]=1;
        controls.protocols[i]=native_protocols[i];controls.bandwidths[i].ghz_2g=WIFI_BW40;
    }
    controls.power_save_set=true;controls.power_save=WIFI_PS_MIN_MODEM;
    calls=writes=fail_at=rollback_fail=0;allocation_failure=corrupt_ps=corrupted=reenable_pmf=false;rollback_failed_stage=NULL;
    pmf_error=pmf_calls=0;
}
static int configure(void) {
    return esp32_mquickjs_wifi_radio_configure_lifecycle(&s_radio.lifecycle,WIFI_MODE_APSTA,WIFI_STORAGE_RAM,
        &requested[0],accept_config,&requested[1],accept_config,&controls,&result);
}
static void restored(void) {
    assert(native_mode==WIFI_MODE_STA && native_storage==WIFI_STORAGE_RAM && native_ps==WIFI_PS_NONE);
    assert(!memcmp(native_config,before,sizeof(before)));
    for(int i=0;i<2;i++)assert(native_bandwidths[i].ghz_2g==WIFI_BW20);
    assert(!depth && !critical && !alloc_live && s_radio.lifecycle.identity==7);
}
int main(void) {
    reset();assert(configure()==0 && !strcmp(result.stage,"complete"));int total=calls;
    assert(native_mode==WIFI_MODE_APSTA && native_ps==WIFI_PS_MIN_MODEM && !alloc_live);
    /* Every native call on the success path can fail, including setters that
     * mutate before returning an error. Caller configs stay uncommitted. */
    for(int nth=1;nth<=total;nth++) {
        reset();fail_at=nth;wifi_config_t input[2];memcpy(input,requested,sizeof(input));
        assert(configure()==-77 && result.error==-77 && !memcmp(input,requested,sizeof(input)));
        assert(!depth && !critical && !alloc_live && s_radio.lifecycle.identity==7);
        if(result.mutation_attempted) { assert(result.rollback_complete);restored(); }
        else assert(writes==0 && !result.rollback_attempted);
    }
    /* Enumerate the recovery suffix separately; original failure survives. */
    reset();fail_at=total;assert(configure()==-77);int recovery_calls=calls-total;
    for(int nth=1;nth<=recovery_calls;nth++) {
        reset();fail_at=total;rollback_fail=nth;
        assert(configure()==-77 && result.rollback_error==-88 && !result.rollback_complete);
        assert(result.rollback_stage==rollback_failed_stage && s_radio.fault_stage && s_radio.cleanup_stage);
        assert(!alloc_live && !depth && s_radio.lifecycle.identity==7);
        fail_at=0;int count=calls;assert(configure()==ESP_ERR_INVALID_STATE && calls==count);
    }
    reset();allocation_failure=true;
    /* Regulatory read precedes allocation; no SDK mutation is permitted. */
    assert(configure()==ESP_ERR_NO_MEM && calls==1 && !writes && !alloc_live);
    assert(!strcmp(result.stage,"config-allocate"));restored();
    reset();s_radio.leases[1].identity=9;assert(configure()==ESP_ERR_INVALID_STATE && !calls);
    reset();esp32_mquickjs_wifi_radio_lifecycle_t stale=s_radio.lifecycle;stale.generation++;
    assert(esp32_mquickjs_wifi_radio_configure_lifecycle(&stale,WIFI_MODE_STA,WIFI_STORAGE_RAM,
        NULL,NULL,NULL,NULL,NULL,&result)==ESP_ERR_INVALID_STATE && !calls);
    reset();controls.protocols[0].ghz_2g=WIFI_PROTOCOL_11N;
    assert(configure()==ESP_ERR_INVALID_ARG && !calls);
    reset();controls.bandwidths[0].ghz_2g=(wifi_bandwidth_t)999;
    assert(configure()==ESP_ERR_INVALID_ARG && !calls);
    reset();controls.protocol_bands[0]=2;controls.protocols[0].ghz_5g=WIFI_PROTOCOL_11A;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    assert(configure()==ESP_ERR_INVALID_STATE && !writes);
#else
    assert(configure()==ESP_ERR_NOT_SUPPORTED && !calls);
#endif
    reset();controls.protocols[0].ghz_2g|=WIFI_PROTOCOL_11AX;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    assert(configure()==ESP_ERR_INVALID_ARG && !calls); /* explicit AX + HT40 */
    controls.bandwidth_bands[0]=0;native_bandwidths[0].ghz_2g=WIFI_BW40;
    assert(configure()==ESP_ERR_INVALID_ARG && !writes); /* inherited HT40 */
#else
    assert(configure()==ESP_ERR_NOT_SUPPORTED && !calls);
#endif
    reset();corrupt_ps=true;assert(configure()==ESP_ERR_INVALID_RESPONSE && result.rollback_complete);restored();
    /* Country persistence is recorded even with RAM storage, including a
     * failed setter. RAM restoration does not establish the old NVS image. */
    reset();controls.country_set=controls.country_by_code=true;
    controls.country=(wifi_country_t){.cc="TW",.policy=WIFI_COUNTRY_POLICY_MANUAL};
    assert(configure()==ESP_OK && result.persistent_mutation_possible);int country_total=calls;
    for(int nth=1;nth<=country_total;nth++) {
        reset();controls.country_set=controls.country_by_code=true;
        controls.country=(wifi_country_t){.cc="TW",.policy=WIFI_COUNTRY_POLICY_MANUAL};fail_at=nth;
        assert(configure()==-77 && !alloc_live && !depth);
        if(result.persistent_mutation_possible)assert(s_radio.fault_stage);
        if(result.rollback_complete) { restored();assert(native_country.cc[0]=='U' && native_country.nchan==11); }
    }
    reset();controls.country_set=true;controls.country=native_country;
    assert(configure()==ESP_ERR_INVALID_ARG && !writes); /* max power is read-only */
    controls.country.max_tx_power=0;controls.country.nchan=13;requested[1].ap.channel=13;
    assert(configure()==0 && native_country.nchan==13); /* validate AP using NEW country */
    reset();reenable_pmf=true;corrupt_ps=true;
    native_config[0].sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
    native_config[0].sta.disable_wpa3_compatible_mode=true;
    native_config[0].sta.pmf_cfg.capable=false;
    before[0]=native_config[0];requested[0]=before[0];requested[0].sta.ssid[0]='n';
    assert(configure()==ESP_ERR_INVALID_RESPONSE && result.rollback_complete);
    restored(); /* Dedicated disable restores the PMF state observed before the failed transaction. */
    for(int failure=0;failure<2;++failure) {
        reset();reenable_pmf=true;pmf_error=failure ? -79 : 0;
        native_config[1].ap.authmode=WIFI_AUTH_WPA2_PSK;native_config[1].ap.pmf_cfg.capable=true;
        before[1]=native_config[1];requested[1]=before[1];requested[1].ap.pmf_cfg.capable=false;
        requested[1].ap.ssid[0]='b';
        int err=esp32_mquickjs_wifi_radio_configure_lifecycle(&s_radio.lifecycle,WIFI_MODE_APSTA,WIFI_STORAGE_RAM,
            NULL,NULL,&requested[1],accept_config,NULL,&result);
        assert(err==(failure ? -79 : ESP_OK) && pmf_calls==1 && !s_radio.started);
        assert(!memcmp(&native_config[0],&before[0],sizeof(before[0])));
        if(failure) { assert(result.rollback_complete && !strcmp(result.stage,"ap-pmf-config"));restored(); }
        else assert(!native_config[1].ap.pmf_cfg.capable && !native_config[1].ap.pmf_cfg.required);
    }
    reset();requested[1].ap.authmode=WIFI_AUTH_WPA3_PSK;
    assert(configure()==ESP_ERR_INVALID_ARG && !calls && !alloc_live);
    reset();requested[1].ap.pmf_cfg.required=true;
    assert(configure()==ESP_ERR_INVALID_ARG && !calls && !alloc_live);
    for(int failure=0;failure<2;++failure) {
        reset();reenable_pmf=true;pmf_error=failure ? -79 : 0;
        native_config[0].sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
        native_config[0].sta.disable_wpa3_compatible_mode=true;
        before[0]=native_config[0];requested[0]=before[0];requested[0].sta.pmf_cfg.capable=false;
        requested[0].sta.ssid[0]='n';
        int err=esp32_mquickjs_wifi_radio_configure_lifecycle(&s_radio.lifecycle,WIFI_MODE_STA,WIFI_STORAGE_RAM,
            &requested[0],accept_config,NULL,NULL,NULL,&result);
        assert(err==(failure ? -79 : ESP_OK) && pmf_calls==1 && !s_radio.started);
        assert(!memcmp(&native_config[1],&before[1],sizeof(before[1])));
        if(failure) { assert(result.rollback_complete && !strcmp(result.stage,"station-pmf-config"));restored(); }
        else assert(!native_config[0].sta.pmf_cfg.capable && !native_config[0].sta.pmf_cfg.required);
    }
    reset();requested[0].sta.pmf_cfg.capable=false;
    assert(configure()==ESP_ERR_INVALID_ARG && !calls && !alloc_live); /* Compatible policy not disabled. */
    return 0;
}
