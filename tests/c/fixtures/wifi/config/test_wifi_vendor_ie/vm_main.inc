
int main(int argc,char **argv) {
    assert(argc==4);bool expected=atoi(argv[2]);int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"vendor-ie",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        reset_vendor();
        bool query=!strcmp(argv[3],"status"),clear=!strcmp(argv[3],"clear"),arity=!strcmp(argv[3],"arity");
        bool failed=!strcmp(argv[3],"sdk-error");
        if(query || clear)assert(put(1,0)==ESP_OK);
        if(failed)fail_call=1;
        calls=0;fail_at=nth;collect=true;inject=true;
        if(query)*output=js_wifi_vendor_ie_status(ctx,NULL,0,NULL);
        else if(clear)*output=js_wifi_vendor_ie_clear(ctx,NULL,0,NULL);
        else *output=js_wifi_vendor_ie_set(ctx,NULL,arity ? 0 : 1,input);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(arity || (!expected && !failed))assert(!native_calls && !owners());
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && failed) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*input=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*input) && n==77 && owners()==1);
            }
        } else if(!clear) {
            *input=JS_GetPropertyStr(ctx,*output,"owners");int32_t n;
            assert(!JS_ToInt32(ctx,&n,*input) && n==1);
        }
        /* OOM after SDK dispatch does not undo the write or silently resubmit. */
        if(!clear && native_calls)assert(native_calls==1 && owners()==1);
        fail_call=0;assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners());
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical);free(heap);
    }
    return 0;
}
