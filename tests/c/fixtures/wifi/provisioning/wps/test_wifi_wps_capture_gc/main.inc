
int main(int argc,char **argv){
 assert(argc==4);int total=1,expected=atoi(argv[3]);bool ap_options=!strcmp(argv[1],"ap-options");bool options_mode=ap_options || !strcmp(argv[1],"options");
 bool pin_mode=!strcmp(argv[1],"pin"),credential_mode=!strcmp(argv[1],"credentials");
 for(int nth=0;nth<=total;nth++){
    void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    JSGCRef input_ref,result_ref,bytes_ref,entry_ref;
    JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref),*bytes=JS_PushGCRef(ctx,&bytes_ref),*entry=JS_PushGCRef(ctx,&entry_ref);
    esp32_mquickjs_wifi_wps_session_t *s=NULL;wps_options_t options;
    assert(!esp32_mquickjs_wifi_wps_open_runtime());
    if(options_mode){*input=JS_Eval(ctx,argv[2],strlen(argv[2]),"wps-options",JS_EVAL_RETVAL);assert(!JS_IsException(*input));}
    else {
        esp_wps_config_t cfg={.wps_type=WPS_TYPE_PIN};
        assert(!esp32_mquickjs_wifi_wps_session_create(&cfg,false,1000,&s));
        s->status.credentials_ready=s->status.pin_ready=true;s->status.native.worker.native.identity=UINT64_MAX;
        memcpy(s->pin,"12345670",8);s->credentials.count=3;
        for(unsigned i=0;i<3;i++){
            esp32_mquickjs_wifi_wps_credential_t *c=&s->credentials.entries[i];
            memset(c->ssid,0xff,32);memset(c->password,'P'+i,64);c->ssid[1]=0;c->password[63]=0;
            c->ssid_length=32;c->password_length=64;c->auth_type=0x20;c->encryption_type=8;c->key_index=i;
            memset(c->mac,2+i,6);
        }
        if(!strcmp(argv[1],"bad-count"))s->credentials.count=4;
        if(!strcmp(argv[1],"bad-length"))s->credentials.entries[2].password_length=65;
        if(!strcmp(argv[1],"closed"))esp32_mquickjs_wifi_wps_session_close(s,false);
        if(!strcmp(argv[1],"consumed"))assert(!esp32_mquickjs_wifi_wps_session_credentials(s,NULL,true));
    }
    calls=0;fail_at=nth;inject=collect=true;bool ok;
    if(options_mode){ok=ap_options?wps_options_for_role(ctx,&input_ref,&options,true):wps_options(ctx,&input_ref,&options);if(!nth){total=calls;assert(ok==expected);}}
    else if(!strcmp(argv[1],"status")){*result=wps_status_to_js(ctx,&s->status);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok);}}
    else if(!strcmp(argv[1],"observation")){wps_watch_event_t event={.sequence=9,.status=s->status};*result=wps_watch_to_js(ctx,&event,NULL);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok);}}
    else if(!strcmp(argv[1],"error")){*result=wps_error(ctx,"WiFiWpsSession.receive",s,ESP_ERR_TIMEOUT,true);ok=!JS_IsException(*result);assert(!ok);if(!nth)total=calls;}
    else {*result=pin_mode?wps_pin_to_js(ctx,s):wps_credentials_to_js(ctx,s);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok==(pin_mode || credential_mode));}}
    inject=collect=false;
    if(!ok){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
    if(options_mode){
        if(ok)assert(esp32_mquickjs_wifi_wps_config_valid(&options.config) && options.timeout_ms);
        else {const uint8_t *p=(const uint8_t *)&options;for(size_t i=0;i<sizeof(options);i++)assert(!p[i]);}
        esp32_mquickjs_wireless_secure_zero(&options,sizeof(options));
    }
    if(pin_mode){
        if(!ok){assert(!s->status.pin_consumed && !memcmp(s->pin,"12345670",8));*result=wps_pin_to_js(ctx,s);assert(!JS_IsException(*result));}
        assert(s->status.pin_consumed && !s->status.pin_ready);for(unsigned i=0;i<8;i++)assert(!s->pin[i]);
        *bytes=JS_GetPropertyStr(ctx,*result,"pin");JSCStringBuf buf;size_t n;const char *pin=JS_ToCStringLen(ctx,&n,*bytes,&buf);assert(pin && n==8 && !memcmp(pin,"12345670",8));
    }
    if(credential_mode){
        if(!ok){assert(!s->status.credentials_consumed && s->credentials.entries[2].password[0]=='R');*result=wps_credentials_to_js(ctx,s);assert(!JS_IsException(*result));}
        assert(s->status.credentials_consumed);for(size_t i=0;i<sizeof(s->credentials);i++)assert(!((uint8_t*)&s->credentials)[i]);
        *bytes=JS_GetPropertyStr(ctx,*result,"credentials");assert(JS_IsUndefined(JS_GetPropertyUint32(ctx,*bytes,3)));
        *entry=JS_GetPropertyUint32(ctx,*bytes,2);*bytes=JS_GetPropertyStr(ctx,*entry,"ssidBytes");double v;
        assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,1)) && v==0);
        assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,31)) && v==255);
        *bytes=JS_GetPropertyStr(ctx,*entry,"passwordBytes");assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,63)) && v==0);
        assert(JS_IsUndefined(JS_GetPropertyUint32(ctx,*bytes,64)));
        assert(JS_IsException(wps_credentials_to_js(ctx,s)));(void)JS_GetException(ctx);
    }
    if(s && (!strcmp(argv[1],"bad-count") || !strcmp(argv[1],"bad-length")))assert(!s->status.credentials_consumed);
    if(s && !strcmp(argv[1],"status") && ok){
        assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"pin")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"credentials")));
        *bytes=JS_GetPropertyStr(ctx,*result,"nativeIdentity");double v;assert(!JS_ToNumber(ctx,&v,JS_GetPropertyStr(ctx,*bytes,"high")) && v==4294967295.0);
    }
    if(s && !strcmp(argv[1],"observation") && ok){
        *bytes=JS_GetPropertyStr(ctx,*result,"status");assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"pin")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"credentials")));
        assert(!s->status.credentials_consumed && !s->status.pin_consumed);
    }
    if(s){esp32_mquickjs_wifi_wps_session_close(s,false);esp32_mquickjs_wifi_wps_session_release(s);}
    assert(!s_wps_handles && !native_live && !session_locked);
    JS_PopGCRef(ctx,&entry_ref);JS_PopGCRef(ctx,&bytes_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
    JS_FreeContext(ctx);free(heap);assert(!root_count);
 }
 return 0;
}
