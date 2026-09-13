
int main(void){
    unsigned total=1;
    for(unsigned nth=0;nth<=total;++nth){
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef ref;JSValue *result=JS_PushGCRef(ctx,&ref);
        wifi_twt_config_t config={true,false};
        calls=0;fail_at=nth;inject=true;collect=true;*result=twt_policy_value(ctx,&config);
        inject=false;collect=false;if(!nth)total=calls;
        if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
        else{assert(JS_GetPropertyStr(ctx,*result,"postWakeupEvents")==JS_TRUE);
            assert(JS_GetPropertyStr(ctx,*result,"keepAlive")==JS_FALSE);}
        const char *text="({postWakeupEvents:false,keepAlive:true})";
        *result=JS_Eval(ctx,text,strlen(text),"twt-policy.js",JS_EVAL_RETVAL);assert(!JS_IsException(*result));
        calls=0;fail_at=nth;inject=true;collect=true;bool captured=twt_policy_capture(ctx,*result,&config);
        inject=false;collect=false;
        if(!nth){assert(captured);if(calls>total)total=calls;}
        if(!captured){assert(JS_HasException(ctx));JS_GetException(ctx);}
        else assert(!config.post_wakeup_event && config.twt_enable_keep_alive);
        const char *invalid[]={"({postWakeupEvents:1,keepAlive:false})","({postWakeupEvents:true})",
            "({postWakeupEvents:true,keepAlive:false,extra:1})","[]","null"};
        for(unsigned i=0;i<5;++i){*result=JS_Eval(ctx,invalid[i],strlen(invalid[i]),"invalid.js",JS_EVAL_RETVAL);
            assert(!JS_IsException(*result));assert(!twt_policy_capture(ctx,*result,&config));JS_GetException(ctx);}
        JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
