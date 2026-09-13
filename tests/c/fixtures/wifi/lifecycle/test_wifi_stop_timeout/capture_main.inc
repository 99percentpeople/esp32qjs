
int main(int argc,char **argv) {
    assert(argc==3);unsigned expected=atoi(argv[2]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"stop",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        uint32_t timeout=0xdeadbeef;
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok=esp32_mquickjs_wifi_capture_stop(ctx,*root,&timeout);
        if(!nth){total=calls;assert(ok==(expected!=0));}
        if(ok)assert(timeout==expected && !JS_HasException(ctx));
        else { assert(timeout==0 && JS_HasException(ctx));JS_GetException(ctx); }
        inject=false;collect=false;JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);free(heap);test_ctx=NULL;
    }
}
