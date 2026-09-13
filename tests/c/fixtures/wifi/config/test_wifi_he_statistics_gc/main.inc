
int main(void){
    unsigned total=1;
    for(unsigned nth=0;nth<=total;++nth){
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef result_ref,item_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*item=JS_PushGCRef(ctx,&item_ref);
        calls=0;fail_at=nth;inject=true;collect=true;
        *result=js_wifi_driver_get_statistics_config(ctx,NULL,0,NULL);
        inject=false;collect=false;if(!nth)total=calls;
        if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
        else{
            *item=JS_GetPropertyStr(ctx,*result,"rx");assert(JS_GetPropertyStr(ctx,*item,"ordinary")==JS_TRUE);
            assert(JS_GetPropertyStr(ctx,*item,"multiUser")==JS_FALSE);
            *item=JS_GetPropertyStr(ctx,*result,"tx");assert(JS_GetPropertyStr(ctx,*item,"voice")==JS_TRUE);
            assert(JS_GetPropertyStr(ctx,*item,"video")==JS_FALSE);
            assert(JS_GetPropertyStr(ctx,*item,"background")==JS_TRUE);
        }
        const char *text="({ordinary:true,multiUser:false})";
        *result=JS_Eval(ctx,text,strlen(text),"statistics.js",JS_EVAL_RETVAL);assert(!JS_IsException(*result));
        uint8_t selection;calls=0;fail_at=nth;inject=true;collect=true;
        bool captured=driver_rx_statistics_capture(ctx,*result,&selection);
        inject=false;collect=false;
        if(!nth){assert(captured);if(calls>total)total=calls;}
        if(!captured){assert(JS_HasException(ctx));JS_GetException(ctx);}else assert(selection==1);
        const char *invalid[]={"({ordinary:1,multiUser:false})","({ordinary:true})",
            "({ordinary:true,multiUser:false,extra:true})","null"};
        for(unsigned i=0;i<4;++i){*result=JS_Eval(ctx,invalid[i],strlen(invalid[i]),"invalid.js",JS_EVAL_RETVAL);
            assert(!JS_IsException(*result));unsigned before=native_reads;
            assert(!driver_rx_statistics_capture(ctx,*result,&selection) && native_reads==before);JS_GetException(ctx);}
        JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&result_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
