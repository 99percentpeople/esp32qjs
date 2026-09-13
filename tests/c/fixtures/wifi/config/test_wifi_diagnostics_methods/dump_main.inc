
int main(int argc,char **argv) {
    assert(argc==2);int total=0;bool invalid=!strcmp(argv[1],"invalid");
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        reset();if(!strcmp(argv[1],"sdk-error"))sdk_error=91;
        if(!strcmp(argv[1],"admission"))s_radio.lifecycle.identity=3;
        JSGCRef rr,ar;JSValue *r=JS_PushGCRef(ctx,&rr),*a=JS_PushGCRef(ctx,&ar);*a=JS_UNDEFINED;
        if(invalid) {
            const char *expressions[]={"true","null","'1'","({valueOf:function(){throw Error('coercion');}})","NaN","Infinity","0.5","-2","32","4294967296"};
            for(unsigned i=0;i<sizeof(expressions)/sizeof(*expressions);i++) {
                *a=JS_Eval(ctx,expressions[i],strlen(expressions[i]),"mask.js",JS_EVAL_RETVAL);assert(!JS_IsException(*a));
                *r=js_wifi_diagnostics_dump_driver_stats(ctx,NULL,1,a);assert(JS_IsException(*r)&&!sdk_calls&&!locked_calls);JS_GetException(ctx);
            }
            *r=js_wifi_diagnostics_dump_driver_stats(ctx,NULL,2,NULL);assert(JS_IsException(*r)&&!locked_calls);JS_GetException(ctx);
        } else {
            inject=true;collect=true;calls=0;fail_at=nth;
            *r=js_wifi_diagnostics_dump_driver_stats(ctx,NULL,0,NULL);
            inject=false;collect=false;if(!nth)total=calls;
            assert(sdk_calls==(!s_radio.lifecycle.identity));
            if(!nth)assert(JS_IsException(*r)==(sdk_error!=0||s_radio.lifecycle.identity!=0));
            if(JS_IsException(*r)) {
                assert(JS_HasException(ctx));*r=JS_GetException(ctx);
                if(!nth) {
                    *a=JS_GetPropertyStr(ctx,*r,"details");uint32_t code;
                    assert(!JS_ToUint32(ctx,&code,JS_GetPropertyStr(ctx,*a,"espCode"))&&code==(sdk_error?sdk_error:ESP_ERR_INVALID_STATE));
                    JSCStringBuf b;const char *stage=JS_ToCString(ctx,JS_GetPropertyStr(ctx,*a,"stage"),&b);
                    assert(stage&&!strcmp(stage,sdk_error?"driver-dump":"admission"));
                }
            }
            else assert(*r==JS_TRUE && seen_mask==UINT32_MAX && !sdk_error && !s_radio.lifecycle.identity);
            assert(!locks);
            if(!sdk_error&&!s_radio.lifecycle.identity) {
                const double masks[]={0,1,31,-1,4294967295.0};
                for(unsigned i=0;i<sizeof(masks)/sizeof(*masks);i++) {
                    *a=JS_NewFloat64(ctx,masks[i]);*r=js_wifi_diagnostics_dump_driver_stats(ctx,NULL,1,a);
                    assert(*r==JS_TRUE && seen_mask==(masks[i]<0?UINT32_MAX:(uint32_t)masks[i]));
                }
            }
        }
        JS_PopGCRef(ctx,&ar);JS_PopGCRef(ctx,&rr);assert(!root_count&&!native_live);
        JS_FreeContext(ctx);free(heap);
    }
}
