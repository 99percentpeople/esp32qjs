
static esp32_mquickjs_future_driver_state_t *capture(JSContext *ctx) {
    esp32_mquickjs_future_driver_state_t *s=NULL;assert(broadcast_capture(ctx,NULL,0,NULL,&s));return s;
}
static void discovery_lifecycle(JSContext *ctx) {
    reset_twt();esp32_mquickjs_future_driver_state_t *s=capture(ctx);
    worker_full=true;assert(broadcast_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING && !worker);
    assert(broadcast_cancel(s)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(broadcast_poll(s)==ESP32_MQUICKJS_FUTURE_READY && !reads);broadcast_destroy(s);
    worker_full=false;s=capture(ctx);broadcast_schedule(s);assert(worker);
    assert(JS_IsException(broadcast_timeout(ctx,s,6000)));(void)JS_GetException(ctx);
    assert(broadcast_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING && native_live==1);
    work();assert(broadcast_poll(s)==ESP32_MQUICKJS_FUTURE_READY && !reads);broadcast_destroy(s);
    s=capture(ctx);broadcast_schedule(s);work();assert(reads==1 && !s->error);
    assert(broadcast_poll(s)==ESP32_MQUICKJS_FUTURE_READY);
    assert(s->generation==s_radio.generation && !s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT]);
    broadcast_destroy(s);
    s_radio.lifecycle.identity=1;s=capture(ctx);broadcast_schedule(s);work();
    assert(s->error==ESP_ERR_INVALID_STATE && reads==1);broadcast_destroy(s);s_radio.lifecycle.identity=0;
    read_error=79;s=capture(ctx);broadcast_schedule(s);work();
    assert(s->error==79 && reads==2 && !s->generation);broadcast_destroy(s);
    assert(!locks && !critical && !worker && !native_live);
}
int main(int argc,char **argv) {
    assert(argc==4);int total=1,expected=atoi(argv[3]);bool input=!strcmp(argv[1],"capture");
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        if(!strcmp(argv[1],"lifecycle")){discovery_lifecycle(ctx);JS_FreeContext(ctx);free(heap);assert(!root_count);return 0;}
        reset_twt();JSGCRef ref,outref;JSValue *value=JS_PushGCRef(ctx,&ref),*out=JS_PushGCRef(ctx,&outref);
        *value=JS_Eval(ctx,argv[2],strlen(argv[2]),"broadcast-discovery",JS_EVAL_RETVAL);assert(!JS_IsException(*value));
        esp32_mquickjs_future_driver_state_t *s=NULL,complete={.generation=9};
        complete.snapshot.count=32;complete.snapshot.joined_bitmap=UINT32_C(0x80000001);
        for(unsigned i=0;i<32;++i)complete.snapshot.schedules[i]=(esp_wifi_btwt_info_t){.btwt_id_in_use=true,
            .btwt_info_id=i,.btwt_trigger=1,.btwt_wake_duration=255,.btwt_wake_interval_mantissa=65535,
            .btwt_wake_interval_exponent=31,.btwt_info_persistence=255};
        bool error_mode=!strcmp(argv[1],"error");if(error_mode)complete.error=71;
        calls=0;fail_at=nth;inject=collect=true;
        bool ok=input?broadcast_capture(ctx,NULL,1,&ref,&s):!JS_IsException(*out=broadcast_finish(ctx,&complete));
        inject=collect=false;
        if(!nth){total=calls;assert(ok==(error_mode?false:expected));}else assert(!ok);
        if(!ok){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
        if(!input && ok) {
            *value=JS_GetPropertyStr(ctx,*out,"schedules");*value=JS_GetPropertyUint32(ctx,*value,31);
            *value=JS_GetPropertyStr(ctx,*value,"wakeIntervalUs");double interval;
            assert(!JS_ToNumber(ctx,&interval,*value) && interval==140735340871680.0);
        }
        if(s)broadcast_destroy(s);
        JS_PopGCRef(ctx,&outref);JS_PopGCRef(ctx,&ref);assert(!native_live && !root_count);
        JS_FreeContext(ctx);free(heap);
    }
    return 0;
}
