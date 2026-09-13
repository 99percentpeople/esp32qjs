
int main(void) {
    u8 first[600],second[600];memset(first,0x41,sizeof(first));memset(second,0x42,sizeof(second));
    int (*setters[])(const unsigned char *,int)={esp_eap_client_set_identity,esp_eap_client_set_username,
        esp_eap_client_set_password,esp_eap_client_set_new_password};
    void (*clearers[])(void)={esp_eap_client_clear_identity,esp_eap_client_clear_username,
        esp_eap_client_clear_password,esp_eap_client_clear_new_password};
    u8 **values[]={&g_wpa_anonymous_identity,&g_wpa_username,&g_wpa_password,&g_wpa_new_password};
    int *lengths[]={&g_wpa_anonymous_identity_len,&g_wpa_username_len,&g_wpa_password_len,&g_wpa_new_password_len};
    for(unsigned i=0;i<4;i++) {
        assert(setters[i](first,24)==ESP_OK && active==1);
        u8 *previous=*values[i];unsigned before=timers;
        fail_next=true;assert(setters[i](second,16)==ESP_ERR_NO_MEM);
        if(PATCHED)assert(*values[i]==previous && *lengths[i]==24 && !memcmp(previous,first,24) && timers==before);
        else assert(!*values[i] && *lengths[i]==24 && active==0);
        assert(setters[i](second,16)==ESP_OK);
#if PATCHED
        assert(setters[i](*values[i]+1,8)==ESP_OK && *lengths[i]==8 && !memcmp(*values[i],second,8));
        assert(setters[i](NULL,1)==ESP_ERR_INVALID_ARG && *lengths[i]==8);
#endif
        clearers[i]();assert(!*values[i] && !*lengths[i] && !active);
        assert(setters[i](first,24)==ESP_OK);eap_globals_reset();assert(!active);
    }
    /* Full-sized PAC replacement used to leak. Failed replacement must keep it. */
    assert(esp_eap_client_set_pac_file(first,600)==ESP_OK);
    u8 *old_pac=g_wpa_pac_file;
    assert(esp_eap_client_set_pac_file(second,512)==ESP_OK);
    assert(active==(PATCHED?1U:2U) && g_wpa_pac_file_len==512);
    if(!PATCHED)os_free(old_pac); /* Release the proven unreferenced original leak. */
    old_pac=g_wpa_pac_file;fail_next=true;
    assert(esp_eap_client_set_pac_file(first,600)==ESP_ERR_NO_MEM);
    if(PATCHED)assert(g_wpa_pac_file==old_pac && g_wpa_pac_file_len==512);
    else {assert(!g_wpa_pac_file && active==1);os_free(old_pac);}
    eap_globals_reset();assert(!active);
    /* Existing short-input semantics create a 512-byte empty PAC. Wipe all 512
     * even if native code later fills it while logical length remains zero. */
    assert(esp_eap_client_set_pac_file(first,1)==ESP_OK && !g_wpa_pac_file_len);
    memset(g_wpa_pac_file,0x77,512);esp_eap_client_clear_certificate_and_key();assert(!active);
    esp_eap_fast_config fast={.fast_provisioning=2,.fast_max_pac_list_len=99,.fast_pac_format_binary=true};
    assert(esp_eap_client_set_fast_params(fast)==ESP_OK);
    char *old_phase=g_wpa_phase1_options;fail_next=true;
    assert(esp_eap_client_set_fast_params(fast)==ESP_ERR_NO_MEM);
    if(PATCHED)assert(g_wpa_phase1_options==old_phase);else assert(!g_wpa_phase1_options);
    eap_globals_reset();assert(!active);
#ifndef CONFIG_TLS_INTERNAL_CLIENT
    assert(esp_eap_client_set_domain_name("radius.example")==ESP_OK);
    char *old_domain=g_wpa_domain_match;fail_next=true;
    assert(esp_eap_client_set_domain_name("other.example")==ESP_ERR_NO_MEM);
    if(PATCHED)assert(g_wpa_domain_match==old_domain);else assert(!g_wpa_domain_match);
#if PATCHED
    assert(esp_eap_client_set_domain_name(g_wpa_domain_match+1)==ESP_OK && !strcmp(g_wpa_domain_match,"adius.example"));
#endif
    assert(esp_eap_client_set_domain_name(NULL)==ESP_OK);assert(!active);
#else
    assert(esp_eap_client_set_domain_name("radius.example")==ESP_ERR_NOT_SUPPORTED);
#endif
    /* Complete and partial-init SM copies are wiped by actual SDK deinit. */
    for(unsigned count=0;count<=4;count++) {
        struct eap_sm sm={0};u8 **p[]={&sm.config.anonymous_identity,&sm.config.identity,&sm.config.password,&sm.config.new_password};
        size_t *n[]={&sm.config.anonymous_identity_len,&sm.config.identity_len,&sm.config.password_len,&sm.config.new_password_len};
        for(unsigned i=0;i<4;i++){*n[i]=24;if(i<count){*p[i]=os_zalloc(24);memset(*p[i],0xa5,24);}}
        eap_peer_config_deinit(&sm);assert(!active && !config_methods);
    }
    /* Borrowed cert/key pointers must only be cleared, never freed by SDK. */
    g_wpa_private_key=first;g_wpa_private_key_len=600;g_wpa_client_cert=second;g_wpa_client_cert_len=600;
    g_wpa_private_key_passwd=first;g_wpa_private_key_passwd_len=600;g_wpa_ca_cert=second;g_wpa_ca_cert_len=600;
    eap_globals_reset();assert(!g_wpa_private_key && first[0]==0x41 && second[0]==0x42 && !active);
    if(PATCHED)assert(!dirty_frees);else assert(dirty_frees>0);
    return 0;
}
