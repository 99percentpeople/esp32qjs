
int main(void) {
    setup();
    stored.ap.authmode=WIFI_AUTH_WPA2_PSK;memcpy(stored.ap.password,"12345678",9);
    stored.ap.max_connection=1;stored.ap.beacon_interval=100;stored.ap.dtim_period=1;
    stored.ap.csa_count=3;stored.ap.pairwise_cipher=WIFI_CIPHER_TYPE_CCMP;
    stored.ap.pmf_cfg.capable=true;stored.ap.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    /* Explicit dormant SAE options remain invalid input; the SDK nonetheless
     * returns this default for a normal WPA2 AP. Replay must preserve it. */
    assert(esp32_mquickjs_wifi_radio_validate_ap_config(&stored)==ESP_ERR_INVALID_ARG);
    assert(begin()==ESP_OK && !memcmp(&snapshot,&stored,sizeof(stored)));
    assert(start()==ESP_OK && !memcmp(&snapshot,&stored,sizeof(stored)));
    assert(close_rate()==ESP_OK);
    for (int i=0;i<4;i++) {
        wifi_config_t invalid=stored;
        if(i==0)invalid.ap.password[0]=0;
        if(i==1){invalid.ap.pmf_cfg.required=true;invalid.ap.pmf_cfg.capable=false;}
        if(i==2)invalid.ap.authmode=WIFI_AUTH_WPA3_PSK;
        if(i==3)invalid.ap.sae_pwe_h2e=(wifi_sae_pwe_method_t)255;
        wifi_config_t before=invalid;
        assert(wifi_radio_validate_saved_ap_config(&invalid)!=ESP_OK);
        assert(!memcmp(&before,&invalid,sizeof(invalid)));
    }
    return 0;
}
