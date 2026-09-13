
#undef JS_GetPropertyStr
int main(int argc,char **argv) {
    assert(argc==4);int expected=atoi(argv[2]),adjustment=atoi(argv[3]),total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"options",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        wifi_sta_config_t sta={0};sta.scan_method=WIFI_ALL_CHANNEL_SCAN;
        calls=0;unsupported=0;fail_at=nth;collect=1;inject=1;
        bool ok=wifi_parse_station_extensions(ctx,*root,&sta,"wifi.connect");
        if(nth==0) {
            assert(ok==expected);
            if(ok)assert(sta.threshold.rssi_5g_adjustment==adjustment);
            if(adjustment==-1)assert(unsupported==1);
            total=calls;
        }
        if(!ok){assert(JS_HasException(ctx));JS_GetException(ctx);}
        inject=0;collect=0;JS_PopGCRef(ctx,&r);assert(!root_count && !native_live);
        JS_FreeContext(ctx);free(heap);
    }
    return 0;
}
