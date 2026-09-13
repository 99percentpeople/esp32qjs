
int main(int argc,char **argv) {
    assert(argc==5);bool mode=atoi(argv[1]),expected=atoi(argv[3]),sdk_fail=atoi(argv[4]);int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[2],strlen(argv[2]),"band",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        band_reset();native_band_mode=WIFI_BAND_MODE_AUTO;if(sdk_fail)native_fail_at=4;
        calls=0;fail_at=nth;collect=true;inject=true;
        *output=mode ? js_wifi_driver_set_band_mode(ctx,NULL,1,input) : js_wifi_driver_set_band(ctx,NULL,1,input);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(!expected && !sdk_fail)assert(!writes);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && sdk_fail)assert(s_radio.configuration.error==77 && s_radio.configuration.mutation_attempted && s_radio.fault_error==77);
        } else assert(JS_IsString(ctx,*output));
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
