
int main(int argc,char **argv) {
    assert(argc==4);int total=1;bool valid=atoi(argv[2]);
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"roaming",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        submits=0;sdk_error=!strcmp(argv[3],"sdk-error");
        bool caps=!strcmp(argv[3],"capabilities"),rrm=!strcmp(argv[3],"rrm"),btm=!strcmp(argv[3],"btm");
        calls=0;fail_at=nth;collect=inject=true;
        if(caps)*output=js_wifi_roaming_capabilities(ctx,NULL,0,NULL);
        else if(rrm)*output=js_wifi_roaming_is_rrm_supported(ctx,NULL,0,NULL);
        else if(btm)*output=js_wifi_roaming_is_btm_supported(ctx,NULL,0,NULL);
        else *output=js_wifi_roaming_send_btm_query(ctx,NULL,1,input);
        inject=collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!valid);}
        else assert(JS_IsException(*output));
        if(!caps && !rrm && !btm && !sdk_error)assert(submits==(unsigned)(!nth && valid));
        assert(submits<=1);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && sdk_error){*output=JS_GetPropertyStr(ctx,*output,"details");*input=JS_GetPropertyStr(ctx,*output,"sdkCode");int32_t value;assert(!JS_ToInt32(ctx,&value,*input) && value==-55);}
        } else if(rrm || btm)assert(*output==JS_TRUE);
        else if(!caps)assert(JS_GetPropertyStr(ctx,*output,"accepted")==JS_TRUE);
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);free(heap);
        assert(!root_count && !native_live);
    }
}
