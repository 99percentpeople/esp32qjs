
int main(void) {
    for(int mode=0;mode<4;mode++) {
        int total=1;
        for(int nth=0;nth<=total;nth++) {
            void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);
            assert(ctx);test_ctx=ctx;driver_restart_required=(mode&1)!=0;s_ap_detach_error=(mode&2)?-3:0;
            JSGCRef root_ref;JSValue *result=JS_PushGCRef(ctx,&root_ref);
            calls=0;fail_at=nth;collect=1;inject=1;
            *result=wifi_ap_error(ctx,"wifi.startAP",-7);
            if(nth==0){total=calls;assert(!JS_IsException(*result));}
            if(JS_IsException(*result))JS_GetException(ctx);
            else {
                JSValue required=JS_GetPropertyStr(ctx,*result,"restartRequired");
                assert(JS_IsBool(required));assert(required==JS_NewBool(mode!=0));
            }
            inject=0;collect=0;JS_PopGCRef(ctx,&root_ref);assert(!root_count);
            JS_FreeContext(ctx);free(heap);
        }
    }
    return 0;
}
