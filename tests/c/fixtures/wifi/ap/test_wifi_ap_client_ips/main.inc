
int main(int argc,char **argv) {
    assert(argc==6);int expected=atoi(argv[2]),args=atoi(argv[3]);scenario=atoi(argv[4]);
    bool include=atoi(argv[5]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef options_ref,result_ref;
        JSValue *options=JS_PushGCRef(ctx,&options_ref),*result=JS_PushGCRef(ctx,&result_ref);
        *options=JS_Eval(ctx,argv[1],strlen(argv[1]),"options",JS_EVAL_RETVAL);assert(!JS_IsException(*options));
        radio_calls=dhcp_calls=error_code=0;s_ap_stage="retained";s_ap_cleanup_pending=scenario==5;
        calls=0;fail_at=nth;collect=true;inject=true;
        *result=js_wifi_ap_clients(ctx,NULL,args,options);
        if(!nth) {total=calls;assert(!JS_IsException(*result)==expected);}
        inject=false;collect=false;
        if(JS_IsException(*result))JS_GetException(ctx);
        else {
            uint32_t length;assert(!JS_ToUint32(ctx,&length,JS_GetPropertyStr(ctx,*result,"length")));
            assert(length==(scenario==2?0:2));
            for(uint32_t i=0;i<length;i++) {
                JSValue entry=JS_GetPropertyUint32(ctx,*result,i),ip=JS_GetPropertyStr(ctx,entry,"ip");
                if(!include)assert(JS_IsUndefined(ip));
                else if(i==1)assert(JS_IsNull(ip));
                else {JSCStringBuf b;assert(!strcmp(JS_ToCString(ctx,ip,&b),"192.168.4.2"));}
                JSValue address=JS_GetPropertyStr(ctx,entry,"address");JSCStringBuf b;
                assert(!strcmp(JS_ToCString(ctx,address,&b),i?"02:00:00:00:00:02":"02:00:00:00:00:01"));
            }
        }
        if(!nth) {
            if(args>1 || scenario==5 || !expected && scenario==0)assert(!radio_calls && !dhcp_calls);
            if(!include || scenario==2 || !CONFIG_LWIP_DHCPS)assert(!dhcp_calls);
            if(scenario==3)assert(error_code==-8 && !strcmp(s_ap_stage,"clients-ip"));
            if(scenario==5)assert(!strcmp(s_ap_stage,"retained"));
            if(include && !CONFIG_LWIP_DHCPS)assert(error_code==ESP_ERR_NOT_SUPPORTED);
        }
        JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&options_ref);
        assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
    }
}
