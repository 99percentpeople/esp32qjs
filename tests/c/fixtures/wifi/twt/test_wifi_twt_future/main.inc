
static esp32_mquickjs_future_driver_state_t *capture(JSContext *ctx) {
    esp32_mquickjs_future_driver_state_t *s=NULL;assert(twt_capture(ctx,NULL,0,NULL,&s));return s;
}
static void probe_lifecycle(JSContext *ctx) {
    reset_twt();esp32_mquickjs_future_driver_state_t *s=capture(ctx);
    twt_schedule(s);assert(worker && s->submitted && !submits);
    assert(twt_cancel(s)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(twt_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING && !atomic_load(&s->worker_done));
    JSValue error=twt_on_timeout(ctx,s,6000);assert(JS_IsException(error));(void)JS_GetException(ctx);
    run_worker();assert(!submits && twt_poll(s)==ESP32_MQUICKJS_FUTURE_READY);
    twt_destroy(s);assert(!native_live && !twt_retired_pending() && !worker);

    s=capture(ctx);early=true;twt_schedule(s);run_worker();
    assert(twt_poll(s)==ESP32_MQUICKJS_FUTURE_READY && !s->error && s->result.native.event_seen);
    native_result.event.status=ITWT_PROBE_TIMEOUT;native_result.event.reason=18;
    native_result.observation_error=88;
    assert(twt_poll(s)==ESP32_MQUICKJS_FUTURE_READY && !s->error);
    JSGCRef ref;JSValue *value=JS_PushGCRef(ctx,&ref);*value=twt_finish(ctx,s);assert(!JS_IsException(*value));
    *value=JS_GetPropertyStr(ctx,*value,"status");
    JSCStringBuf buf;assert(!strcmp(JS_ToCString(ctx,*value,&buf),"timeout"));
    JS_PopGCRef(ctx,&ref);
    uint64_t owner=s->token.identity;
    worker_full=true;twt_destroy(s);
    assert(!native_live && twt_retired_pending() && s_twt_retired.token.identity==owner && !worker);
    assert(!esp32_mquickjs_prepare_wifi_twt_runtime_destroy());
    esp32_mquickjs_future_driver_state_t *next=capture(ctx);twt_schedule(next);assert(!next->submitted);
    worker_full=false;clock_us+=100000;retire_error=76;
    assert(esp32_mquickjs_wifi_twt_service() && worker);run_worker();
    assert(twt_retired_pending() && s_twt_retired.error==76 && s_twt_retired.token.identity==owner);
    twt_schedule(next);assert(!next->submitted && !worker);
    retire_error=0;clock_us+=100000;
    assert(!esp32_mquickjs_prepare_wifi_twt_runtime_destroy() && worker);run_worker();
    assert(esp32_mquickjs_prepare_wifi_twt_runtime_destroy() && !s_radio.operation.identity);
    twt_schedule(next);assert(next->submitted && worker);
    assert(twt_cancel(next)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(twt_poll(next)==ESP32_MQUICKJS_FUTURE_PENDING);run_worker();
    assert(twt_poll(next)==ESP32_MQUICKJS_FUTURE_READY);twt_destroy(next);
    assert(!native_live && !worker && !retired_locked && !locks && !critical);
    /* Recovery asks Radio to close while the original public Future lives.
     * Late destruction must not overwrite a successor's native cleanup slot. */
    s=capture(ctx);twt_schedule(s);run_worker();
    assert(esp32_mquickjs_wifi_radio_twt_probe_request_close(&s->token));
    assert(twt_poll(s)==ESP32_MQUICKJS_FUTURE_READY && s->error==ESP_ERR_INVALID_STATE);
    clock_us+=100000;assert(esp32_mquickjs_wifi_twt_service());run_worker();
    assert(!twt_retired_pending() && !s_radio.operation.identity);
    next=capture(ctx);twt_schedule(next);run_worker();
    assert(esp32_mquickjs_wifi_radio_twt_probe_request_close(&next->token));
    worker_full=true;(void)esp32_mquickjs_wifi_twt_service();
    uint64_t successor=s_twt_retired.token.identity;
    twt_destroy(s);assert(s_twt_retired.token.identity==successor && successor==next->token.identity);
    worker_full=false;clock_us+=100000;assert(esp32_mquickjs_wifi_twt_service());run_worker();
    twt_destroy(next);assert(!native_live && !twt_retired_pending());
}
int main(int argc,char **argv) {
    assert(argc==4);int total=1,expected=atoi(argv[3]);
    bool input=!strcmp(argv[1],"capture");
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        if(!strcmp(argv[1],"lifecycle")) {probe_lifecycle(ctx);JS_FreeContext(ctx);free(heap);assert(!root_count);return 0;}
        reset_twt();JSGCRef ref,outref;JSValue *value=JS_PushGCRef(ctx,&ref),*out=JS_PushGCRef(ctx,&outref);
        *value=JS_Eval(ctx,argv[2],strlen(argv[2]),"twt-public",JS_EVAL_RETVAL);assert(!JS_IsException(*value));
        esp32_mquickjs_future_driver_state_t *s=NULL;
        esp32_mquickjs_future_driver_state_t complete={.token={7,9},.result={.native_identity=31,
            .native={.event_seen=true,.event={.status=ITWT_PROBE_SUCCESS,.reason=0}}}};
        if(!strcmp(argv[1],"error")) {complete.error=81;complete.stage="submit";}
        calls=0;fail_at=nth;inject=collect=true;bool ok;
        if(input)ok=twt_capture(ctx,NULL,1,&ref,&s);
        else {
            if(!strcmp(argv[1],"status"))*out=js_wifi_twt_status(ctx,NULL,0,NULL);
            else if(!strcmp(argv[1],"capabilities"))*out=js_wifi_twt_capabilities(ctx,NULL,0,NULL);
            else *out=twt_finish(ctx,&complete);
            ok=!JS_IsException(*out);
        }
        inject=collect=false;
        bool error_mode=!strcmp(argv[1],"error");
        if(!nth){total=calls;assert(ok==(error_mode?false:expected));}else assert(!ok);
        if(!ok) {
            assert(JS_HasException(ctx));JSValue exception=JS_GetException(ctx);
            if(!nth && strstr(argv[2],"throw 12345"))assert(exception==JS_NewInt32(ctx,12345));
        }
        if(s){assert(s->response_ms>=1 && s->response_ms<=60000 && !s->token.identity);twt_destroy(s);}
        assert(!native_live && !submits && !twt_retired_pending());
        JS_PopGCRef(ctx,&outref);JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);free(heap);assert(!root_count);
    }
    return 0;
}
