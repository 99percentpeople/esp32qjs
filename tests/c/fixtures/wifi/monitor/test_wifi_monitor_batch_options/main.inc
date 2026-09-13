
#undef JS_GetPropertyStr
#undef JS_GetPropertyUint32
int main(int argc,char **argv) {
    assert(argc==4);uint32_t pool=(uint32_t)atoi(argv[2]);bool expected=atoi(argv[3])!=0;int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef r;JSValue *root=push_root(ctx,&r);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"batch options",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        esp32_mquickjs_wifi_monitor_batch_options_t result,before;
        memset(&result,0xa5,sizeof(result));memcpy(&before,&result,sizeof(before));
        calls=0;fail_at=nth;inject=collect=true;
        bool ok=esp32_mquickjs_wifi_monitor_capture_batch_options(ctx,*root,pool,&result);
        inject=collect=false;
        if(nth==0) {total=calls;assert(ok==expected);} else assert(!ok);
        if(!ok) {
            assert(JS_HasException(ctx));(void)JS_GetException(ctx);
            assert(!memcmp(&before,&result,sizeof(result)));
        } else {
            assert(result.maximum_frames>=1 && result.maximum_frames<=pool);
            assert(result.minimum_frames>=1 && result.minimum_frames<=result.maximum_frames);
            if(!strcmp(argv[1],"undefined") || !strcmp(argv[1],"({})")) {
                assert(result.maximum_frames==(pool<32?pool:32));
                assert(result.minimum_frames==1 && !result.timeout_set && !result.maximum_latency_ms);
            }
            if(strstr(argv[1],"timeoutMs:0"))assert(result.timeout_set && result.timeout_ms==0);
            if(strstr(argv[1],"timeoutMs:2147483647"))assert(result.timeout_set && result.timeout_ms==INT32_MAX);
        }
        assert(root_count==1 && !native_live);pop_root(ctx,&r);assert(!root_count);
        JS_FreeContext(ctx);free(heap);
    }
}
