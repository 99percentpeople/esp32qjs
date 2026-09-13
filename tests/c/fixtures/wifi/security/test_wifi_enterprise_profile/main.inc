
static void empty_counts(void) {
    esp32_mquickjs_wifi_eap_counts_t counts;
    esp32_mquickjs_wifi_eap_profile_counts(&counts);
    assert(!counts.profiles && !counts.reserved_bytes && !critical);
    for(unsigned i=0;i<4;i++)assert(!live[i].p);
}
int main(void) {
    uint8_t user[]={0x55,0,0xff},password[]={1,2,0,4};
    esp32_mquickjs_wifi_eap_input_t input={.policy={.methods=ESP_EAP_TYPE_PEAP,
        .ttls_phase2=ESP_EAP_TTLS_PHASE2_MSCHAPV2}};
    input.fields[ESP32_MQUICKJS_WIFI_EAP_USERNAME]=(esp32_mquickjs_wifi_eap_span_t){user,sizeof(user)};
    input.fields[ESP32_MQUICKJS_WIFI_EAP_PASSWORD]=(esp32_mquickjs_wifi_eap_span_t){password,sizeof(password)};
    esp32_mquickjs_wifi_eap_profile_t *a=NULL,*b=NULL,*c=NULL;
    assert(esp32_mquickjs_wifi_eap_profile_create(&input,&a)==ESP_OK && a);
    esp32_mquickjs_wifi_eap_input_t view;
    assert(esp32_mquickjs_wifi_eap_profile_view(a,&view));
    assert(!memcmp(view.fields[ESP32_MQUICKJS_WIFI_EAP_USERNAME].data,user,sizeof(user)));
    assert(!view.fields[ESP32_MQUICKJS_WIFI_EAP_USERNAME].data[sizeof(user)]);
    memset(password,99,sizeof(password));
    assert(view.fields[ESP32_MQUICKJS_WIFI_EAP_PASSWORD].data[0]==1);
    assert(esp32_mquickjs_wifi_eap_profile_retain(a));
    esp32_mquickjs_wifi_eap_profile_release(a);assert(a->refs==1);
    a->refs=UINT32_MAX;assert(!esp32_mquickjs_wifi_eap_profile_retain(a));a->refs=1;
    assert(esp32_mquickjs_wifi_eap_profile_create(&input,&b)==ESP_OK);
    unsigned before=allocations;
    assert(esp32_mquickjs_wifi_eap_profile_create(&input,&c)==ESP_ERR_NO_MEM && !c && allocations==before);
    assert(s_eap_profile_counts.profiles==2 && s_eap_profile_counts.reserved_bytes==a->bytes+b->bytes);
    esp32_mquickjs_wifi_eap_profile_release(b);esp32_mquickjs_wifi_eap_profile_release(a);empty_counts();
    allocation_fail=true;assert(esp32_mquickjs_wifi_eap_profile_create(&input,&a)==ESP_ERR_NO_MEM && !a);
    allocation_fail=false;empty_counts();
    for(unsigned field=0;field<ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT;field++) {
        esp32_mquickjs_wifi_eap_input_t bad=input;
        /* Overflow rejected before reading these deliberately inaccessible bytes. */
        bad.fields[field]=(esp32_mquickjs_wifi_eap_span_t){(void *)1,SIZE_MAX};
        before=allocations;assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)!=ESP_OK && !a && before==allocations);
        bad=input;bad.fields[field]=(esp32_mquickjs_wifi_eap_span_t){NULL,1};
        assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)!=ESP_OK && !a);empty_counts();
    }
    esp32_mquickjs_wifi_eap_input_t bad=input;bad.policy.methods=0;
    assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==ESP_ERR_INVALID_ARG);
    bad=input;bad.policy.methods=0x80;assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==ESP_ERR_INVALID_ARG);
    uint8_t max_identity[128];memset(max_identity,0x41,sizeof(max_identity));
    bad=input;bad.fields[ESP32_MQUICKJS_WIFI_EAP_USERNAME]=(esp32_mquickjs_wifi_eap_span_t){max_identity,128};
    assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==ESP_OK);esp32_mquickjs_wifi_eap_profile_release(a);
    bad=input;bad.policy.methods=ESP_EAP_TYPE_TLS;assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==ESP_ERR_INVALID_ARG);
    bad=input;bad.policy.ttls_phase2=255;assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==ESP_ERR_INVALID_ARG);
    bad=input;bad.policy.suiteb=true;
    assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==(CONFIG_ESP_WIFI_SUITE_B_192?ESP_OK:ESP_ERR_NOT_SUPPORTED));
    esp32_mquickjs_wifi_eap_profile_release(a);
    bad=input;bad.policy.methods=ESP_EAP_TYPE_FAST;
    bad.fields[ESP32_MQUICKJS_WIFI_EAP_PAC]=(esp32_mquickjs_wifi_eap_span_t){user,sizeof(user)};
    assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==(CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT?ESP_ERR_NOT_SUPPORTED:ESP_ERR_INVALID_ARG));
    bad.fields[ESP32_MQUICKJS_WIFI_EAP_PAC]=(esp32_mquickjs_wifi_eap_span_t){0};
    bad.policy.fast_provisioning=1;
    assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==(CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT?ESP_ERR_NOT_SUPPORTED:ESP_OK));
    esp32_mquickjs_wifi_eap_profile_release(a);
    bad.policy.fast_provisioning=0;
    assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==(CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT?ESP_ERR_NOT_SUPPORTED:ESP_ERR_INVALID_ARG));
    uint8_t pac[512];memset(pac,0xa5,sizeof(pac));
    bad.fields[ESP32_MQUICKJS_WIFI_EAP_PAC]=(esp32_mquickjs_wifi_eap_span_t){pac,sizeof(pac)};
    assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==(CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT?ESP_ERR_NOT_SUPPORTED:ESP_OK));
    esp32_mquickjs_wifi_eap_profile_release(a);
    bad=input;bad.fields[ESP32_MQUICKJS_WIFI_EAP_DOMAIN]=(esp32_mquickjs_wifi_eap_span_t){user,sizeof(user)};
    assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==(CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT?ESP_ERR_INVALID_ARG:ESP_ERR_NOT_SUPPORTED));
    /* Full allowed native field sizes, cross-field budget and certificate/key pairing. */
    uint8_t *blob=malloc(32768);assert(blob);memset(blob,0xa5,32768);
    bad=input;bad.policy.methods=ESP_EAP_TYPE_TLS;
    bad.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT]=(esp32_mquickjs_wifi_eap_span_t){blob,32768};
    bad.fields[ESP32_MQUICKJS_WIFI_EAP_CLIENT_CERT]=(esp32_mquickjs_wifi_eap_span_t){blob,16384};
    bad.fields[ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY]=(esp32_mquickjs_wifi_eap_span_t){blob,4096};
    assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==ESP_OK);
    assert(a->bytes<=ESP32_MQUICKJS_WIFI_ENTERPRISE_MAX_PROFILE_BYTES);
    esp32_mquickjs_wifi_eap_profile_release(a);
#if !CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT
    bad.policy.methods|=ESP_EAP_TYPE_FAST;
    bad.fields[ESP32_MQUICKJS_WIFI_EAP_PAC]=(esp32_mquickjs_wifi_eap_span_t){blob,16384};
    assert(esp32_mquickjs_wifi_eap_profile_create(&bad,&a)==ESP_ERR_INVALID_SIZE && !a);
#endif
    free(blob);empty_counts();assert(frees+1==allocations); /* One injected allocation failure. */
    /* Partial capture lives in the same budget and cannot be published. */
    size_t lengths[ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT]={0};
    lengths[ESP32_MQUICKJS_WIFI_EAP_USERNAME]=3;
    assert(esp32_mquickjs_wifi_eap_profile_begin(&input.policy,lengths,&a)==ESP_OK);
    assert(!esp32_mquickjs_wifi_eap_profile_view(a,&view) && !esp32_mquickjs_wifi_eap_profile_retain(a));
    uint8_t *field=esp32_mquickjs_wifi_eap_profile_field(a,ESP32_MQUICKJS_WIFI_EAP_USERNAME);
    assert(field);memcpy(field,"abc",3);
    assert(esp32_mquickjs_wifi_eap_profile_seal(a)==ESP_OK && esp32_mquickjs_wifi_eap_profile_view(a,&view));
    assert(!memcmp(view.fields[ESP32_MQUICKJS_WIFI_EAP_USERNAME].data,"abc",3));
    assert(!esp32_mquickjs_wifi_eap_profile_field(a,ESP32_MQUICKJS_WIFI_EAP_USERNAME));
    esp32_mquickjs_wifi_eap_profile_release(a);empty_counts();
    assert(esp32_mquickjs_wifi_eap_profile_begin(&input.policy,lengths,&a)==ESP_OK);
    field=esp32_mquickjs_wifi_eap_profile_field(a,ESP32_MQUICKJS_WIFI_EAP_USERNAME);memcpy(field,"key",3);
    esp32_mquickjs_wifi_eap_profile_release(a);empty_counts(); /* Inspected wipe before free. */
    return 0;
}
