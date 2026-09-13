
int main(void) {
    struct wps_credential cred={0};
    memcpy(cred.ssid,"new",3);cred.ssid_len=3;
    memcpy(cred.key,"new-pass",8);cred.key_len=8;
    reset();
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_OK);
    assert(!memcmp(installed.sta.ssid,"new",3) && !memcmp(installed.sta.password,"new-pass",8));
    for(unsigned i=3;i<32;i++)assert(!installed.sta.ssid[i]);
    for(unsigned i=8;i<64;i++)assert(!installed.sta.password[i]);
    assert(installed.sta.untouched==saved.sta.untouched && installed.sta.threshold.authmode==7);
    assert(!installed.sta.bssid_set && !installed.sta.sae_pwe_h2e);
    assert(!allocated && !dirty_frees);
    reset();fail_alloc=true;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_NO_MEM && !calls[1] && !calls[2] && !calls[3]);
    reset();get_error=201;
    assert(esp32qjs_wps_apply_credential(&cred)==201 && !calls[2] && !calls[3]);
    reset();disconnect_error=202;
    assert(esp32qjs_wps_apply_credential(&cred)==202 && !calls[3]);
    reset();set_error=203;
    assert(esp32qjs_wps_apply_credential(&cred)==203 && calls[3]==1);
    assert(!allocated && !dirty_frees);
    reset();cred.ssid_len=32;cred.key_len=64;
    memset(cred.ssid,0x61,32);memset(cred.key,0x62,64);
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_OK);
    assert(!memcmp(installed.sta.ssid,cred.ssid,32) && !memcmp(installed.sta.password,cred.key,64));
    reset();cred.key_len=0;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_OK);
    for(unsigned i=0;i<64;i++)assert(!installed.sta.password[i]);
    reset();cred.ssid_len=33;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_INVALID_ARG && !calls[0]);
    cred.ssid_len=SIZE_MAX;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_INVALID_ARG && !calls[0]);
    cred.ssid_len=0;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_INVALID_ARG && !calls[0]);
    cred.ssid_len=3;cred.key_len=65;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_INVALID_ARG && !calls[0]);
    assert(save_credentials_cb(NULL,&cred)==ESP_FAIL && !native.ap_cred_cnt);
    cred.key_len=SIZE_MAX;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_INVALID_ARG && !calls[0]);
    assert(esp32qjs_wps_apply_credential(NULL)==ESP_ERR_INVALID_ARG && !calls[0]);
    cred.key_len=8;cred.auth_type=WPS_AUTH_WPAPSK;cred.encr_type=WPS_ENCR_AES;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_OK);
#ifdef CONFIG_WPS_STRICT
    assert(installed.sta.threshold.authmode==7);
#else
    assert(installed.sta.threshold.authmode==WIFI_AUTH_WPA_PSK);
#endif
    cred.cred_attr=cred.key;cred.cred_attr_len=8;
    for(unsigned i=0;i<MAX_CRED_COUNT;i++) {
        assert(save_credentials_cb(NULL,&cred)==ESP_OK);
        assert(native.ap_cred_cnt==i+1 && !native.creds[i].cred_attr && !native.creds[i].cred_attr_len);
        assert(!memcmp(native.creds[i].ssid,cred.ssid,32) && !memcmp(native.creds[i].key,cred.key,64));
    }
    assert(save_credentials_cb(NULL,&cred)==ESP_FAIL && native.ap_cred_cnt==MAX_CRED_COUNT);
    assert(save_credentials_cb(NULL,NULL)==ESP_FAIL);
    missing_owner=true;assert(save_credentials_cb(NULL,&cred)==ESP_FAIL);
    assert(!allocated && !dirty_frees);
    return 0;
}
