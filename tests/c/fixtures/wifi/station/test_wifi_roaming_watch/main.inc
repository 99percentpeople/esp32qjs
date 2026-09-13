
int main(int argc,char **argv) {
    assert(argc==4);bool expected=atoi(argv[2]),scoped=!strcmp(argv[3],"scoped");
    int total=0;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef arg_ref;JSValue *arg=JS_PushGCRef(ctx,&arg_ref);
        *arg=JS_Eval(ctx,argv[1],strlen(argv[1]),"watch-options",JS_EVAL_RETVAL);assert(!JS_IsException(*arg));
        wifi_watch_source_t parsed,unchanged;memset(&parsed,0xa5,sizeof(parsed));unchanged=parsed;
        uint32_t capacity=999;uint64_t allowed=wifi_roaming_watch_mask();
        calls=0;fail_at=nth;inject=collect=true;
        bool ok=wifi_watch_options(ctx,1,arg,scoped?"wifi.roaming.watch":"wifi.watch",scoped,allowed,&parsed,&capacity);
        if(!nth){total=calls;assert(ok==expected);}
        inject=collect=false;
        if(!ok) {
            assert(JS_HasException(ctx));(void)JS_GetException(ctx);
            assert(!memcmp(&parsed,&unchanged,sizeof(parsed)) && capacity==999);
        } else {
            assert(capacity>=1 && capacity<=64);
            if(scoped)assert(!parsed.all && !parsed.raw && parsed.mask && !(parsed.mask&~allowed));
            /* Capture and poll every SDK descriptor through the shared broker. */
            source.all=parsed.all;source.raw=parsed.raw;source.mask=parsed.mask;
            for(size_t i=0;i<sizeof(s_descriptors)/sizeof(s_descriptors[0]);i++) {
                int id=s_descriptors[i].id;sends=wakes=subscriber_sends=0;queued_ready=false;
                esp32_mquickjs_wifi_watch_capture(id,NULL,91);
                bool selected=parsed.all || (parsed.mask&(UINT64_C(1)<<id));
                queued_ready=sends!=0;
                wifi_watch_poll(ctx,source.runtime,NULL);
                assert(subscriber_sends==(int)selected && !queued_ready && !locked);
                if(selected)assert(subscriber_events[0].raw_requested==parsed.raw);
            }
        }
        JS_PopGCRef(ctx,&arg_ref);JS_FreeContext(ctx);free(heap);assert(!root_count && !native_live);
    }
    return 0;
}
