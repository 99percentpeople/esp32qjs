
int main(int argc,char **argv){
 assert(argc==4);int total=1,expected=atoi(argv[3]);bool options_mode=!strcmp(argv[1],"options"),credential_mode=!strncmp(argv[1],"credentials",11);
 bool empty_custom=!strcmp(argv[1],"credentials-empty-custom"),airkiss=!strcmp(argv[1],"credentials-airkiss");
 for(int nth=0;nth<=total;nth++){
    void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    JSGCRef input_ref,result_ref,bytes_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref),*bytes=JS_PushGCRef(ctx,&bytes_ref);
    esp32_mquickjs_wifi_smartconfig_session_t *s=NULL;sc_options_t options;
    assert(!esp32_mquickjs_wifi_smartconfig_open_runtime());
    if(options_mode){*input=JS_Eval(ctx,argv[2],strlen(argv[2]),"sc-options",JS_EVAL_RETVAL);assert(!JS_IsException(*input));}
    else {
        esp32_mquickjs_wifi_smartconfig_decoder_options_t cfg={.type=SC_TYPE_ESPTOUCH,.channel_timeout_s=15};
        assert(!esp32_mquickjs_wifi_smartconfig_session_create(&cfg,false,1000,&s));
        s->status.credentials_ready=true;s->status.native.decoder.token.identity=UINT64_MAX;
        memset(s->credentials.network.ssid,0xff,32);memset(s->credentials.network.password,'P',64);
        s->credentials.network.type=SC_TYPE_ESPTOUCH_V2;s->credentials.network.bssid_set=true;memset(s->credentials.network.bssid,2,6);
        s->credentials.custom_length=64;for(unsigned i=0;i<64;i++)s->credentials.custom_data[i]=(uint8_t)i;
        s->credentials.custom_data[63]=0;
        if(empty_custom)s->credentials.custom_length=0;
        if(airkiss){s->credentials.network.type=SC_TYPE_AIRKISS;s->credentials.custom_length=0;}
        if(!strcmp(argv[1],"closed"))esp32_mquickjs_wifi_smartconfig_session_close(s,false);
        if(!strcmp(argv[1],"consumed"))assert(!esp32_mquickjs_wifi_smartconfig_session_credentials(s,NULL,true));
    }
    calls=0;fail_at=nth;inject=collect=true;bool ok;
    if(options_mode){ok=sc_options(ctx,&input_ref,&options);if(!nth){total=calls;assert(ok==expected);}}
    else if(!strcmp(argv[1],"status")){*result=sc_status_to_js(ctx,&s->status);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok);}}
    else if(!strcmp(argv[1],"observation")){sc_watch_event_t event={.sequence=9,.status=s->status};*result=sc_watch_to_js(ctx,&event,NULL);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok);}}
    else if(!strcmp(argv[1],"error")){*result=sc_error(ctx,"WiFiSmartConfigSession.receive",s,ESP_ERR_TIMEOUT,true);ok=!JS_IsException(*result);assert(!ok);if(!nth)total=calls;}
    else {*result=sc_credentials_to_js(ctx,s);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok==credential_mode);}}
    inject=collect=false;
    if(!ok){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
    if(options_mode){
        if(ok){assert(options.timeout_ms && options.decoder.channel_timeout_s>=15);if(options.decoder.key_length)assert(options.decoder.key==options.key);}
        else {const uint8_t *p=(const uint8_t *)&options;for(size_t i=0;i<sizeof(options);i++)assert(!p[i]);}
        esp32_mquickjs_wireless_secure_zero(&options,sizeof(options));
    }
    if(credential_mode){
        if(!ok){assert(!s->status.credentials_consumed && s->credentials.network.password[0]=='P' && s->credentials.custom_length==((empty_custom || airkiss)?0:64) && s->credentials.custom_data[62]==62);*result=sc_credentials_to_js(ctx,s);assert(!JS_IsException(*result));}
        assert(s->status.credentials_consumed && !s->credentials.network.password[0]);
        *bytes=JS_GetPropertyStr(ctx,*result,"ssidBytes");double v;assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,31)) && v==255);
        *bytes=JS_GetPropertyStr(ctx,*result,"passwordBytes");assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,63)) && v=='P');
        *bytes=JS_GetPropertyStr(ctx,*result,"customDataBytes");
        if(airkiss)assert(JS_IsNull(*bytes));
        else if(empty_custom)assert(JS_IsUndefined(JS_GetPropertyUint32(ctx,*bytes,0)));
        else {
            assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,0)) && v==0);
            assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,62)) && v==62);
            assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,63)) && v==0);
            assert(JS_IsUndefined(JS_GetPropertyUint32(ctx,*bytes,64)));
        }
        assert(!s->credentials.custom_length);
        for(unsigned i=0;i<64;i++)assert(!s->credentials.custom_data[i]);
        assert(JS_IsException(sc_credentials_to_js(ctx,s)));assert(JS_HasException(ctx));(void)JS_GetException(ctx);
    }
    if(s && !strcmp(argv[1],"status") && ok){
        assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"passwordBytes")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"ssidBytes")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"customDataBytes")));
        *bytes=JS_GetPropertyStr(ctx,*result,"decoderIdentity");double v;assert(!JS_ToNumber(ctx,&v,JS_GetPropertyStr(ctx,*bytes,"high")) && v==4294967295.0);
    }
    if(s && !strcmp(argv[1],"observation") && ok){
        double v;assert(!JS_ToNumber(ctx,&v,JS_GetPropertyStr(ctx,*result,"sequence")) && v==9);
        *bytes=JS_GetPropertyStr(ctx,*result,"status");
        assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"customDataBytes")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"passwordBytes")));
        assert(!s->status.credentials_consumed && s->credentials.custom_length==64);
    }
    if(s){esp32_mquickjs_wifi_smartconfig_session_close(s,false);esp32_mquickjs_wifi_smartconfig_session_release(s);}
    assert(!s_sc_handles && !native_live && !session_locked);
    JS_PopGCRef(ctx,&bytes_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
    JS_FreeContext(ctx);free(heap);assert(!root_count);
 }
 return 0;
}
