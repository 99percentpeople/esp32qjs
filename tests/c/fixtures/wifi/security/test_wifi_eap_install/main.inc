
static void reset_observations(void){steps=fail_at=0;activation_attempted=false;clear_fail=dirty=false;}
static esp32_mquickjs_wifi_eap_profile_t *make_profile(void){
    static const uint8_t user[]={1,0,3},password[]={4,0,6};
    static const uint8_t ca[]="-----BEGIN CERTIFICATE-----\nCA\n-----END CERTIFICATE-----";
    static const uint8_t cert[]="-----BEGIN CERTIFICATE-----\nCLIENT\n-----END CERTIFICATE-----";
    static const uint8_t key[]={0x30,0x02,0x01,0x00};
    esp32_mquickjs_wifi_eap_input_t in={.policy={.methods=ESP_EAP_TYPE_TLS|ESP_EAP_TYPE_PEAP,
        .ttls_phase2=ESP_EAP_TTLS_PHASE2_CHAP,.suiteb=true}};
#define FIELD(i,p,n) in.fields[i]=(esp32_mquickjs_wifi_eap_span_t){p,n}
    FIELD(ESP32_MQUICKJS_WIFI_EAP_ANONYMOUS_IDENTITY,user,sizeof(user));
    FIELD(ESP32_MQUICKJS_WIFI_EAP_USERNAME,user,sizeof(user));
    FIELD(ESP32_MQUICKJS_WIFI_EAP_PASSWORD,password,sizeof(password));
    FIELD(ESP32_MQUICKJS_WIFI_EAP_NEW_PASSWORD,password,sizeof(password));
    FIELD(ESP32_MQUICKJS_WIFI_EAP_CA_CERT,ca,sizeof(ca)-1);
    FIELD(ESP32_MQUICKJS_WIFI_EAP_CLIENT_CERT,cert,sizeof(cert)); /* Existing PEM NUL. */
    FIELD(ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY,key,sizeof(key)); /* DER exact bytes. */
    FIELD(ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY_PASSWORD,(const uint8_t *)"pass",4);
#if CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT
    FIELD(ESP32_MQUICKJS_WIFI_EAP_DOMAIN,(const uint8_t *)"example.test",12);
#endif
#undef FIELD
    esp32_mquickjs_wifi_eap_profile_t *p=NULL;
    assert(esp32_mquickjs_wifi_eap_profile_create(&in,&p)==0);return p;
}
int main(void){
    esp32_mquickjs_wifi_eap_profile_t *p=make_profile();
    esp32_mquickjs_wifi_eap_install_result_t out;
    dispatch_fail=true;assert(esp32_mquickjs_wifi_eap_install(p,&out)==72 && !out.entered && !out.identity && p->refs==1 && !steps);dispatch_fail=false;
    on_worker=true;assert(esp32_mquickjs_wifi_eap_install(p,&out)==ESP_ERR_INVALID_STATE && !out.entered);on_worker=false;
    foreign=true;assert(esp32_mquickjs_wifi_eap_install(p,&out)==ESP_ERR_INVALID_STATE && !clears && p->refs==1);foreign=false;
    p->refs=UINT32_MAX;assert(esp32_mquickjs_wifi_eap_install(p,&out)==ESP_ERR_NO_MEM && !clears);p->refs=1;
    assert(esp32_mquickjs_wifi_eap_install(p,&out)==0 && out.enabled && out.retained && out.identity && p->refs==2);
    unsigned total_steps=steps;uint64_t first=out.identity;
    assert(!time_disabled && !okc && dirty && seen_suiteb && seen_phase2==ESP_EAP_TTLS_PHASE2_CHAP);
    assert(ca_length==(int)p->input.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT].length+1);
    assert(cert_length==(int)p->input.fields[ESP32_MQUICKJS_WIFI_EAP_CLIENT_CERT].length && key_length==4);
    assert(borrowed_ca==p->input.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT].data);
    unsigned before=steps;
    assert(esp32_mquickjs_wifi_eap_install(p,&out)==ESP_ERR_INVALID_STATE && steps==before && p->refs==2);
    assert(esp32_mquickjs_wifi_eap_install_status(first+1,&out)==ESP_ERR_INVALID_STATE && !out.retained);
    assert(esp32_mquickjs_wifi_eap_install_status(first,&out)==0 && out.identity==first && out.enabled);
    /* Restart admission observes the actual enabled binding and exact profile,
     * without setters, reference transfer or native identity consumption. */
    unsigned clear_before=clears;uint64_t next_before=s_eap_install_next_identity;
    esp32_mquickjs_wifi_eap_profile_t *different=make_profile();
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,different,&out)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_eap_profile_release(different);
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first+1,p,&out)==ESP_ERR_INVALID_STATE);
    observed_control_error=91;
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,p,&out)==ESP_ERR_INVALID_STATE);
    observed_control_error=0;observed_cleanup_error=92;
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,p,&out)==ESP_ERR_INVALID_STATE);
    observed_cleanup_error=0;resources &= ~ESP32_MQUICKJS_WIFI_EAP_SDK_ENABLED;
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,p,&out)==ESP_ERR_INVALID_STATE);
    resources |= ESP32_MQUICKJS_WIFI_EAP_SDK_ENABLED;
    s_eap_install_next_identity=UINT64_MAX;
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,p,&out)==ESP_ERR_INVALID_STATE);
    s_eap_install_next_identity=next_before;
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,p,&out)==ESP_OK && out.enabled && out.retained);
    assert(steps==before && clears==clear_before && p->refs==2 && s_eap_install_next_identity==next_before);
    assert(esp32_mquickjs_wifi_eap_install_clear(first,&out)==0 && !out.retained && !out.identity && p->refs==1 && !borrowed_ca);
    before=clears;assert(esp32_mquickjs_wifi_eap_install_clear(first,&out)==ESP_ERR_INVALID_STATE && clears==before);
    /* Every setter/enable failure runs production rollback; native reference
     * survives until the injected SDK says it has retired all borrowers. */
    for(unsigned n=1;n<=total_steps;n++){
        reset_observations();fail_at=n;
        assert(esp32_mquickjs_wifi_eap_install(p,&out)==71 && !out.retained && !out.identity && !resources && p->refs==1);
        assert(out.cleanup_error==0 && !borrowed_ca);
    }
    reset_observations();fail_at=total_steps;clear_fail=true;
    assert(esp32_mquickjs_wifi_eap_install(p,&out)==71 && out.retained && out.identity && out.cleanup_error==73 && p->refs==2);
    uint64_t failed=out.identity;assert(failed>first && borrowed_ca);
    before=clears;assert(esp32_mquickjs_wifi_eap_install_clear(first,&out)==ESP_ERR_INVALID_STATE && clears==before && p->refs==2);
    dispatch_fail=true;assert(esp32_mquickjs_wifi_eap_install_clear(failed,&out)==72 && p->refs==2);dispatch_fail=false;
    clear_fail=false;fail_at=0;assert(esp32_mquickjs_wifi_eap_install_clear(failed,&out)==0 && p->refs==1);
    /* Last caller can go away while native cleanup is still pending. */
    reset_observations();assert(esp32_mquickjs_wifi_eap_install(p,&out)==0);uint64_t last=out.identity;
    esp32_mquickjs_wifi_eap_profile_release(p);assert(s_eap_profile_counts.profiles==1);
    clear_fail=true;assert(esp32_mquickjs_wifi_eap_install_clear(last,&out)==73 && out.retained && borrowed_ca);
    clear_fail=false;assert(esp32_mquickjs_wifi_eap_install_clear(last,&out)==0 && !s_eap_profile_counts.profiles && !s_eap_profile_counts.reserved_bytes);
    p=make_profile();reset_observations();
    /* Trust rejection precedes all SDK mutations; retained bytes remain caller-owned. */
    esp32_mquickjs_wifi_eap_input_t untrusted=p->input;untrusted.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT]=(esp32_mquickjs_wifi_eap_span_t){0};
    esp32_mquickjs_wifi_eap_profile_t *q=NULL;assert(esp32_mquickjs_wifi_eap_profile_create(&untrusted,&q)==0);
    before=clears;assert(esp32_mquickjs_wifi_eap_install(q,&out)==ESP_ERR_INVALID_ARG && !strcmp(out.stage,"server-trust") && clears==before && q->refs==1);
    esp32_mquickjs_wifi_eap_profile_release(q);
    on_wifi=true;before=dispatches;assert(esp32_mquickjs_wifi_eap_install(p,&out)==0 && dispatches==before);last=out.identity;
    assert(esp32_mquickjs_wifi_eap_install_clear(last,&out)==0 && dispatches==before);on_wifi=false;
#if !CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT
    esp32_mquickjs_wifi_eap_input_t fast={.policy={.methods=ESP_EAP_TYPE_FAST,.fast_provisioning=1}};
    assert(esp32_mquickjs_wifi_eap_profile_create(&fast,&q)==0);
    assert(esp32_mquickjs_wifi_eap_install(q,&out)==0 && seen_fast==1 && seen_pac==1);last=out.identity;
    assert(esp32_mquickjs_wifi_eap_install_clear(last,&out)==0);esp32_mquickjs_wifi_eap_profile_release(q);
#endif
    s_eap_install_next_identity=UINT64_MAX;before=clears;
    assert(esp32_mquickjs_wifi_eap_install(p,&out)==ESP_ERR_INVALID_STATE && !strcmp(out.stage,"identity") && clears==before && p->refs==1);
    esp32_mquickjs_wifi_eap_profile_release(p);
    assert(!s_eap_profile_counts.profiles && !s_eap_profile_counts.reserved_bytes && !s_eap_installed_profile);
    return 0;
}
