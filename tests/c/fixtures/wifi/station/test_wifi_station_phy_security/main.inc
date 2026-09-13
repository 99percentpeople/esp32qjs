
#undef JS_GetPropertyStr
int main(int argc,char **argv) {
    assert(argc==5);bool expected=atoi(argv[2]);const char *kind=argv[3];int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef args[2];JSValue *ssid=JS_PushGCRef(ctx,&args[0]);
        bool view=!strcmp(argv[4],"view") || !strcmp(argv[4],"closed-view");
        if(view) {
            uint8_t *bytes=heap_caps_malloc(2,1);assert(bytes);bytes[0]=255;bytes[1]=65;
            *ssid=esp32_mquickjs_new_owned_byte_view(ctx,bytes,2);
            if(!strcmp(argv[4],"closed-view"))js_byte_view_close(ctx,ssid,0,NULL);
        } else *ssid=JS_Eval(ctx,argv[4],strlen(argv[4]),"ssid",JS_EVAL_RETVAL);
        assert(!JS_IsException(*ssid));
        JSValue *options=JS_PushGCRef(ctx,&args[1]);
        *options=JS_Eval(ctx,argv[1],strlen(argv[1]),"options",JS_EVAL_RETVAL);assert(!JS_IsException(*options));
        wifi_config_t config;memset(&config,0xa5,sizeof(config));uint32_t timeout=0;
        unsupported=0;unsupported_option=NULL;calls=0;fail_at=nth;collect=1;inject=1;
        bool configuring=!strncmp(kind,"configure-",10);
        tracked_input=options;input_driver_reads=input_password_reads=0;
        bool ok=configuring
            ? esp32_mquickjs_wifi_parse_station_config_for_operation(ctx,2,args,&config,&timeout,"wifi.configure")
            : esp32_mquickjs_wifi_parse_connect_config(ctx,!strcmp(kind,"no-options")?1:2,args,&config,&timeout);
        tracked_input=NULL;
        if(nth==0) {
            assert(ok==expected);total=calls;
            if(ok && !strcmp(kind,"only")) {
                assert(config.sta.sae_pk_mode==WPA3_SAE_PK_MODE_ONLY);
                assert(config.sta.threshold.authmode==WIFI_AUTH_WPA3_PSK && config.sta.pmf_cfg.required);
                assert(config.sta.sae_pwe_h2e==WPA3_SAE_PWE_BOTH || config.sta.sae_pwe_h2e==WPA3_SAE_PWE_HASH_TO_ELEMENT);
            }
            if(ok && !strcmp(kind,"he-defaults"))
                assert(config.sta.he_dcm_set && config.sta.he_dcm_max_constellation_tx==3 && config.sta.he_dcm_max_constellation_rx==3);
            if(ok && !strcmp(kind,"he-values"))
                assert(config.sta.he_dcm_set && config.sta.he_dcm_max_constellation_tx==0 && config.sta.he_dcm_max_constellation_rx==2 && config.sta.he_mcs9_enabled);
            if(ok && !strcmp(kind,"vht"))
                assert(config.sta.vht_su_beamformee_disabled && config.sta.vht_mu_beamformee_disabled && config.sta.vht_mcs8_enabled);
            if(ok && !strcmp(kind,"disabled"))assert(config.sta.sae_pk_mode==WPA3_SAE_PK_MODE_DISABLED);
            if(ok && !strcmp(kind,"security-flags"))assert(config.sta.transition_disable && config.sta.disable_wpa3_compatible_mode);
            if(ok && (!strcmp(kind,"configure-pmf") || !strcmp(kind,"connect-pmf")))assert(!config.sta.pmf_cfg.capable && !config.sta.pmf_cfg.required && config.sta.disable_wpa3_compatible_mode);
            if(ok && !strcmp(kind,"nested")) {
                assert(!strcmp((char *)config.sta.ssid,"capture-test"));
                assert(!strcmp((char *)config.sta.password,"nested-secret"));
                assert(config.sta.listen_interval==77 && config.sta.threshold.rssi==-70 && timeout==4321);
                assert(input_driver_reads==1 && input_password_reads==1);
            }
            if(ok && (!strcmp(kind,"binary") || !strcmp(kind,"no-options"))) {
                assert(config.sta.ssid[0]==255 && config.sta.ssid[1]==65);
                for(size_t i=2;i<32;i++)assert(!config.sta.ssid[i]);
            }
            if(ok && !strcmp(kind,"binary32"))for(size_t i=0;i<32;i++)assert(config.sta.ssid[i]==65);
            if(!strncmp(kind,"unsupported:",12))assert(unsupported==1 && !strcmp(unsupported_option,kind+12));
        }
        if(!ok) {
            assert(JS_HasException(ctx));JS_GetException(ctx);
            for(size_t i=0;i<sizeof(config);i++)assert(((uint8_t *)&config)[i]==0);
        }
        esp32_mquickjs_wireless_secure_zero(&config,sizeof(config));
        inject=0;collect=0;
        if(view)assert(!JS_IsException(js_byte_view_close(ctx,ssid,0,NULL)));
        JS_PopGCRef(ctx,&args[1]);JS_PopGCRef(ctx,&args[0]);
        assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
    }
    return 0;
}
