
static void native_bounds(void){
    esp32_mquickjs_wifi_itwt_options_t o={.config={.setup_cmd=TWT_REQUEST,.min_wake_dura=1,
        .wake_invl_mant=10256,.timeout_time_ms=100},.timeout_ms=1};
    assert(esp32_mquickjs_wifi_itwt_options_valid(&o));
    assert(esp32_mquickjs_wifi_itwt_duration_us(&o.config)==256 && esp32_mquickjs_wifi_itwt_interval_us(&o.config)==10256);
    o.config.wake_invl_mant--;assert(!esp32_mquickjs_wifi_itwt_options_valid(&o));o.config.wake_invl_mant++;
    o.config.reserved=1;assert(!esp32_mquickjs_wifi_itwt_options_valid(&o));o.config.reserved=0;
    o.config.twt_id=32768;assert(!esp32_mquickjs_wifi_itwt_options_valid(&o));o.config.twt_id=0;
    o.config.wake_invl_expn=31;o.config.wake_invl_mant=16;
    assert(esp32_mquickjs_wifi_itwt_interval_us(&o.config)==(UINT64_C(1)<<35));
    assert(esp32_mquickjs_wifi_itwt_options_valid(&o));
    o.config.wake_invl_mant=65535;
    assert(esp32_mquickjs_wifi_itwt_interval_us(&o.config)==(UINT64_C(65535)<<31));
    assert(!esp32_mquickjs_wifi_itwt_options_valid(&o));
    assert(!esp32_mquickjs_wifi_itwt_options_valid(NULL) && !esp32_mquickjs_wifi_btwt_options_valid(NULL));
}
int main(int argc,char **argv){
    assert(argc==4);native_bounds();bool individual=argv[1][0]=='i',expected=atoi(argv[3]);int total=1;
    for(int nth=0;nth<=total;nth++){
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref);
        char expression[4096];snprintf(expression,sizeof(expression),"(%s)",argv[2]);
        *input=JS_Eval(ctx,expression,strlen(expression),"twt-options",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        esp32_mquickjs_wifi_itwt_options_t i;esp32_mquickjs_wifi_btwt_options_t b;
        memset(&i,0xa5,sizeof(i));memset(&b,0xa5,sizeof(b));
        calls=0;fail_at=nth;inject=collect=true;
        bool ok=individual?esp32_mquickjs_wifi_capture_itwt(ctx,*input,&i):esp32_mquickjs_wifi_capture_btwt(ctx,*input,&b);
        inject=collect=false;
        if(!nth){total=calls;assert(ok==expected);}else assert(!ok);
        if(ok){
            assert(individual?esp32_mquickjs_wifi_itwt_options_valid(&i):esp32_mquickjs_wifi_btwt_options_valid(&b));
            if(individual && !strcmp(argv[2],"{}"))assert(!i.connection_id_set && i.config.flow_type==0 && i.config.trigger==1 && i.timeout_ms==6000);
        }else{
            assert(JS_HasException(ctx));JSValue exception=JS_GetException(ctx);
            if(!nth && strstr(argv[2],"throw 12345") && !expected)assert(exception==JS_NewInt32(ctx,12345));
            const unsigned char *p=individual?(const unsigned char *)&i:(const unsigned char *)&b;
            for(size_t n=0;n<(individual?sizeof(i):sizeof(b));n++)assert(!p[n]);
        }
        JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);free(heap);
        assert(!root_count && !native_live);
    }
    return 0;
}
