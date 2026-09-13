
int main(int argc,char **argv) {
    assert(argc==4);int total=1;bool expected=atoi(argv[3]);
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef root_ref;JSValue *root=JS_PushGCRef(ctx,&root_ref);
        *root=JS_Eval(ctx,argv[2],strlen(argv[2]),"phy",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        esp32_mquickjs_wifi_phy_readback_t captured,zero={0};memset(&captured,0x55,sizeof(captured));
        calls=0;fail_at=nth;collect=true;inject=true;
        bool ok=driver_phy_capture(ctx,*root,(esp32_mquickjs_wifi_phy_query_t)atoi(argv[1]),&captured);
        inject=false;collect=false;
        if(!nth){total=calls;assert(ok==expected);}
        if(!ok) {
            assert(!memcmp(&captured,&zero,sizeof(captured)) && JS_HasException(ctx));
            *root=JS_GetException(ctx);
            if(!nth && strstr(argv[2],"sentinel")) {
                *root=JS_GetPropertyStr(ctx,*root,"message");JSCStringBuf buffer;
                const char *text=JS_ToCString(ctx,*root,&buffer);assert(text && !strcmp(text,"sentinel"));
            }
        } else assert(captured.bands && !(captured.bands & ~3U));
        JS_PopGCRef(ctx,&root_ref);JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
