
#undef JS_GetPropertyStr
#undef JS_GetPropertyUint32
int main(int argc,char **argv) {
    assert(argc==6);int interface=atoi(argv[2]),expected=atoi(argv[3]);const char *mode=argv[5];
    size_t expected_length=strlen(argv[4])/2;uint8_t bytes[33]={0};
    for(size_t i=0;i<expected_length;i++){unsigned x;assert(sscanf(argv[4]+i*2,"%2x",&x)==1);bytes[i]=x;}
    int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef options_ref,view_ref;JSValue *options=JS_PushGCRef(ctx,&options_ref);
        *options=JS_Eval(ctx,argv[1],strlen(argv[1]),"config",JS_EVAL_RETVAL);assert(!JS_IsException(*options));
        JSValue *view=JS_PushGCRef(ctx,&view_ref);
        if(strstr(mode,"view")) {
            size_t length=!strcmp(mode,"long-view")?33:expected_length;
            uint8_t *data=heap_caps_malloc(length?length:1,1);assert(data);memcpy(data,bytes,length);
            *view=esp32_mquickjs_new_owned_byte_view(ctx,data,length);assert(!JS_IsException(*view));
            assert(!JS_IsException(JS_SetPropertyStr(ctx,*options,"ssid",*view)));
            if(!strcmp(mode,"closed-view"))js_byte_view_close(ctx,view,0,NULL);
        }
        wifi_config_t config;memset(&config,0xa5,sizeof(config));
        calls=0;fail_at=nth;policy_calls=0;policy_failure=!strcmp(mode,"policy-fail");
        validator_error=!strcmp(mode,"validate-fail")?ESP_ERR_NOT_SUPPORTED:0;
        force_required=!strcmp(mode,"required");collect=1;inject=1;
        bool ok=!strcmp(mode,"driver") ? esp32_mquickjs_wifi_parse_driver_config_for_operation(ctx,*options,interface,&config,"wifi.driver.setInterfaceConfig")
            : esp32_mquickjs_wifi_parse_driver_config(ctx,*options,interface,&config);
        if(nth==0) {assert(ok==expected);total=calls;}
        if(ok) {
            uint8_t *ssid=interface==WIFI_IF_AP?config.ap.ssid:config.sta.ssid;
            assert(!memcmp(ssid,bytes,expected_length));
            for(size_t i=expected_length;i<32;i++)assert(ssid[i]==0);
            if(interface==WIFI_IF_AP) {
                assert(config.ap.ssid_len==expected_length);
                if(!strcmp(mode,"tu"))assert(config.ap.beacon_interval==60000 && config.ap.channel==36);
            }
        } else {
            assert(JS_HasException(ctx));JS_GetException(ctx);
            for(size_t i=0;i<sizeof(config);i++)assert(((uint8_t *)&config)[i]==0);
        }
        esp32_mquickjs_wireless_secure_zero(&config,sizeof(config));
        inject=0;collect=0;
        if(!JS_IsUndefined(*view))js_byte_view_close(ctx,view,0,NULL);
        JS_PopGCRef(ctx,&view_ref);JS_PopGCRef(ctx,&options_ref);assert(!root_count);
        JS_FreeContext(ctx);assert(!native_live);free(heap);
    }
    return 0;
}
