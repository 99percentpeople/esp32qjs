
int main(int argc,char **argv) {
    assert(argc==5);int expected=atoi(argv[2]),mode=atoi(argv[3]),storage=atoi(argv[4]),total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"start",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        esp32_mquickjs_wifi_radio_configuration_selection_t s;memset(&s,0x55,sizeof(s));
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok=esp32_mquickjs_wifi_capture_start(ctx,*root,&s);
        if(!nth){total=calls;assert(ok==expected);}
        if(ok) {
            assert(s.start_only && s.start && s.start_set && !s.allow_disconnect && !s.station_set && !s.access_point_set);
            assert(s.mode_set==(mode!=0) && (!mode || s.mode==mode));
            assert(s.storage_set==(storage>=0) && (storage<0 || s.storage==storage));
        } else {
            for(size_t i=0;i<sizeof(s);i++)assert(!((unsigned char *)&s)[i]);
            assert(JS_HasException(ctx));JSValue error=JS_GetException(ctx);
            if(!nth && strstr(argv[1],"throw 12345"))assert(error==JS_NewInt32(ctx,12345));
        }
        inject=false;collect=false;JS_PopGCRef(ctx,&ref);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
    }
}
