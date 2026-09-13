
int main(int argc,char **argv) {
    queue_capacity=atoi(argv[1]);collect=atoi(argv[2]);int total=1;
    if(argc>3) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        parse_busy=true;esp32_mquickjs_future_driver_state_t *state=NULL;
        assert(!espnow_open_capture(ctx,NULL,0,NULL,&state) && !state && JS_HasException(ctx));
        assert(s_espnow_session.cleanup_scheduled && !radio_live && !queue_retains && !native_live);
        (void)JS_GetException(ctx);JS_FreeContext(ctx);free(heap);return 0;
    }
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        memset(&s_espnow_session,0,sizeof(s_espnow_session));queue_closed=false;
        calls=0;fail_at=nth;inject=true;esp32_mquickjs_future_driver_state_t *state=NULL;
        bool ok=espnow_open_capture(ctx,NULL,0,NULL,&state);inject=false;
        if(!nth)total=calls;
        if(nth) {assert(!ok && !state && JS_HasException(ctx));(void)JS_GetException(ctx);}
        else {assert(ok && state);espnow_open_release(state);for(unsigned i=0;i<16;i++)assert(state->pmk[i]==0);heap_caps_free(state);}
        assert(s_espnow_session.lifecycle==ESPNOW_LIFECYCLE_CLOSED);
        for(unsigned i=0;i<16;i++)assert(s_espnow_session.pmk[i]==0);
        assert(!queue_retains && !radio_live);JS_GC(ctx);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
    }
    printf("ESP-NOW capture queue=%u gc=%d boundaries=%d\n",queue_capacity,collect,total);
}
