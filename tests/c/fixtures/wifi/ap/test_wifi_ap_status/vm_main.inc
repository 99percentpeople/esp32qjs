
int main(int argc,char **argv) {
    assert(argc==2);scenario=atoi(argv[1]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        s_ap_netif=scenario?(void *)1:NULL;s_ap_lease.acquired=scenario!=0;
        s_ap_cleanup_pending=scenario==3;s_ap_stage="netif-detach";queries=0;
        JSGCRef ref;JSValue *result=JS_PushGCRef(ctx,&ref);
        calls=0;fail_at=nth;collect=true;inject=true;
        *result=esp32_mquickjs_wifi_ap_status(ctx);
        if(!nth){total=calls;assert(!JS_IsException(*result));}
        inject=false;collect=false;
        if(JS_IsException(*result))JS_GetException(ctx);
        else if(!scenario)assert(JS_IsNull(*result) && !queries);
        else {
            assert(queries==1 && JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"password")));
            JSValue bytes=JS_GetPropertyStr(ctx,*result,"ssidBytes");
            if(scenario==1 || scenario==4) {
                uint32_t n;assert(!JS_ToUint32(ctx,&n,JS_GetPropertyUint32(ctx,bytes,2)) && n==(scenario==4?0xc3:255));
                assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"ssid"))==(scenario==1));
                assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*result,"channel")) && n==6);
                assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*result,"clientCount")) && n==2);
                JSCStringBuf buffer;assert(!strcmp(JS_ToCString(ctx,JS_GetPropertyStr(ctx,*result,"mac"),&buffer),"02:00:00:00:00:00"));
                assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"queryError")));
            } else {
                assert(JS_IsNull(bytes) && JS_IsNull(JS_GetPropertyStr(ctx,*result,"ssid")));
                assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"clientCount")));
                int32_t err;assert(!JS_ToInt32(ctx,&err,JS_GetPropertyStr(ctx,*result,"queryError")) && err==-27);
            }
        }
        assert(!strcmp(s_ap_stage,"netif-detach"));
        JS_PopGCRef(ctx,&ref);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);test_ctx=NULL;
    }
}
