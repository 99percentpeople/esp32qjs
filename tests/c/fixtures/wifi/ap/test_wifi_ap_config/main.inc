
#undef JS_GetPropertyStr
static void check_readback(wifi_config_t *requested) {
    wifi_config_t actual=*requested;
    if(requested->ap.wpa3_compatible_mode)actual.ap.authmode=WIFI_AUTH_WPA2_PSK;
    assert(wifi_radio_ap_reopen_config_matches(requested,&actual));
    if(requested->ap.authmode!=WIFI_AUTH_OPEN && requested->ap.authmode!=WIFI_AUTH_WPA_PSK) {
        wifi_config_t changed=actual;changed.ap.pmf_cfg.capable=!requested->ap.pmf_cfg.capable;
        assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&changed));
        assert(!wifi_radio_ap_reopen_config_matches(requested,&changed));
        if(!requested->ap.pmf_cfg.capable || requested->ap.pmf_cfg.required) {
            changed=actual;changed.ap.pmf_cfg.required=!requested->ap.pmf_cfg.required;
            assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&changed));
        }
    }
#define CHECK_REOPEN_FIELD(field,value) do { wifi_config_t changed=actual;changed.ap.field=(value);assert(!wifi_radio_ap_reopen_config_matches(requested,&changed)); } while(0)
    CHECK_REOPEN_FIELD(channel,requested->ap.channel==1?6:1);
    CHECK_REOPEN_FIELD(ssid_hidden,!requested->ap.ssid_hidden);
    CHECK_REOPEN_FIELD(max_connection,requested->ap.max_connection==1?2:1);
    CHECK_REOPEN_FIELD(beacon_interval,requested->ap.beacon_interval+100);
    CHECK_REOPEN_FIELD(csa_count,requested->ap.csa_count==1?2:1);
    CHECK_REOPEN_FIELD(dtim_period,requested->ap.dtim_period==1?2:1);
    CHECK_REOPEN_FIELD(ftm_responder,!requested->ap.ftm_responder);
#undef CHECK_REOPEN_FIELD

    if(requested->ap.wpa3_compatible_mode)actual.ap.authmode=WIFI_AUTH_WPA2_PSK;
    assert(esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    actual.ap.sae_ext=!requested->ap.sae_ext;
    assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    actual.ap.sae_ext=requested->ap.sae_ext;
    actual.ap.wpa3_compatible_mode=!requested->ap.wpa3_compatible_mode;
    assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    actual.ap.wpa3_compatible_mode=requested->ap.wpa3_compatible_mode;
    actual.ap.bss_max_idle_cfg.period=requested->ap.bss_max_idle_cfg.period==10?11:10;
    assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    actual.ap.bss_max_idle_cfg.period=requested->ap.bss_max_idle_cfg.period;
    actual.ap.bss_max_idle_cfg.protected_keep_alive=!requested->ap.bss_max_idle_cfg.protected_keep_alive;
    assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    actual.ap.bss_max_idle_cfg.protected_keep_alive=requested->ap.bss_max_idle_cfg.protected_keep_alive;
    if(requested->ap.wpa3_compatible_mode) {
        actual.ap.authmode=WIFI_AUTH_WPA3_PSK;
        assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
        actual.ap.authmode=WIFI_AUTH_WPA2_PSK;
        actual.ap.pairwise_cipher=WIFI_CIPHER_TYPE_TKIP;
        assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    }
    esp32_mquickjs_wireless_secure_zero(&actual,sizeof(actual));
}
int main(int argc,char **argv) {
    assert(argc==4);bool expected=atoi(argv[2]);const char *kind=argv[3];int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef r,result_ref,view_ref;JSValue *root=JS_PushGCRef(ctx,&r);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"options",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        JSValue *view=JS_PushGCRef(ctx,&view_ref);
        if(!strcmp(kind,"binary-view") || !strcmp(kind,"closed-view")) {
            uint8_t *bytes=heap_caps_malloc(3,1);assert(bytes);bytes[0]=255;bytes[1]=0;bytes[2]=65;
            *view=esp32_mquickjs_new_owned_byte_view(ctx,bytes,3);assert(!JS_IsException(*view));
            assert(!JS_IsException(JS_SetPropertyStr(ctx,*root,"ssid",*view)));
            if(!strcmp(kind,"closed-view"))js_byte_view_close(ctx,view,0,NULL);
        }
        wifi_config_t config;memset(&config,0x55,sizeof(config));
        unsupported=0;calls=0;fail_at=nth;collect=1;inject=1;
        bool allow_disconnect=true;
        bool ok=esp32_mquickjs_wifi_parse_ap_config(ctx,*root,&config,&allow_disconnect);
        assert(allow_disconnect==(ok && !strcmp(kind,"allow-disconnect")));
        if(nth==0) {
            assert(ok==expected);
            if(!strcmp(kind,"unsupported"))assert(unsupported==1);
            if(ok) {
                check_readback(&config);
                if(!strcmp(kind,"ext"))assert(config.ap.sae_ext && config.ap.authmode==WIFI_AUTH_WPA3_PSK &&
                    config.ap.pairwise_cipher==WIFI_CIPHER_TYPE_GCMP256 && config.ap.pmf_cfg.required &&
                    config.ap.sae_pwe_h2e==WPA3_SAE_PWE_HASH_TO_ELEMENT);
                if(!strcmp(kind,"idle"))assert(config.ap.bss_max_idle_cfg.period==10 && config.ap.bss_max_idle_cfg.protected_keep_alive);
                if(!strcmp(kind,"pmf-disabled"))assert(!config.ap.pmf_cfg.capable && !config.ap.pmf_cfg.required);
                if(!strncmp(kind,"tu-",3))assert(config.ap.beacon_interval==(unsigned)atoi(kind+3));
                if(!strncmp(kind,"channel-",8))assert(config.ap.channel==(unsigned)atoi(kind+8));
                if(!strcmp(kind,"binary") || !strcmp(kind,"binary-view"))assert(config.ap.ssid_len==3 && config.ap.ssid[0]==255 && config.ap.ssid[1]==0 && config.ap.ssid[2]==65);
                if(!strcmp(kind,"binary32")) {assert(config.ap.ssid_len==32);for(size_t i=0;i<32;i++)assert(config.ap.ssid[i]==65);}
                if(!strcmp(kind,"nested"))assert(config.ap.max_connection==2 && config.ap.dtim_period==3 && !strcmp((char *)config.ap.password,"12345678"));
            }
        }
        if(!ok) {
            for(size_t i=0;i<sizeof(config);i++)assert(((uint8_t *)&config)[i]==0);
            assert(JS_HasException(ctx));JS_GetException(ctx);
        } else {
            /* Inject the native readback boundary's documented compatible base
             * values. This is not proof that the real driver performed it. */
            if(config.ap.wpa3_compatible_mode)config.ap.authmode=WIFI_AUTH_WPA2_PSK;
            JSValue *result=JS_PushGCRef(ctx,&result_ref);*result=wifi_ap_result(ctx,&config);
            if(JS_IsException(*result))JS_GetException(ctx);
            else {
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"password")));
                JSGCRef bytes_ref;JSValue *bytes=JS_PushGCRef(ctx,&bytes_ref);
                *bytes=JS_GetPropertyStr(ctx,*result,"ssidBytes");
                uint32_t length;assert(!JS_ToUint32(ctx,&length,JS_GetPropertyStr(ctx,*bytes,"length")) && length==config.ap.ssid_len);
                for(uint32_t i=0;i<length;i++) {uint32_t byte;assert(!JS_ToUint32(ctx,&byte,JS_GetPropertyUint32(ctx,*bytes,i)) && byte==config.ap.ssid[i]);}
                if(!strcmp(kind,"binary") || !strcmp(kind,"binary-view"))assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"ssid")));
                JS_PopGCRef(ctx,&bytes_ref);
                if(!strcmp(kind,"pmf-disabled")) {
                    *root=JS_GetPropertyStr(ctx,*result,"pmf");JSCStringBuf b;const char *s=JS_ToCString(ctx,*root,&b);
                    assert(s && !strcmp(s,"disabled"));
                }
                assert(JS_GetPropertyStr(ctx,*result,"saeExt")==JS_NewBool(config.ap.sae_ext));
                assert(JS_GetPropertyStr(ctx,*result,"wpa3CompatibleMode")==JS_NewBool(config.ap.wpa3_compatible_mode));
                assert(JS_GetPropertyStr(ctx,*result,"bssMaxIdleProtectedKeepAlive")==JS_NewBool(config.ap.bss_max_idle_cfg.protected_keep_alive));
                uint32_t period;
                assert(!JS_ToUint32(ctx,&period,JS_GetPropertyStr(ctx,*result,"bssMaxIdlePeriod")) && period==config.ap.bss_max_idle_cfg.period);
            }
            JS_PopGCRef(ctx,&result_ref);
        }
        if(nth==0)total=calls;
        esp32_mquickjs_wireless_secure_zero(&config,sizeof(config));
        inject=0;collect=0;
        if(!JS_IsUndefined(*view))assert(!JS_IsException(js_byte_view_close(ctx,view,0,NULL)));
        JS_PopGCRef(ctx,&view_ref);JS_PopGCRef(ctx,&r);assert(!root_count && !native_live);
        JS_FreeContext(ctx);free(heap);
    }
    return 0;
}
