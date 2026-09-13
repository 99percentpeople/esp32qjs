
int main(int argc,char **argv) {
    assert(argc==2);bool arity=!strcmp(argv[1],"arity"),failed=!strcmp(argv[1],"sdk-error"),status=!strcmp(argv[1],"status");
    bool valid=!strcmp(argv[1],"-32768") || !strcmp(argv[1],"32767") || !strcmp(argv[1],"0") || failed;
    int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef arg_ref,out_ref,field_ref;JSValue *arg=JS_PushGCRef(ctx,&arg_ref),*out=JS_PushGCRef(ctx,&out_ref),*field=JS_PushGCRef(ctx,&field_ref);
        const char *expr=arity || failed || status?"0":argv[1];
        *arg=JS_Eval(ctx,expr,strlen(expr),"offset",JS_EVAL_RETVAL);assert(!JS_IsException(*arg));
        policy_reset(true);s_radio.effective_mode=WIFI_MODE_APSTA;
        memset(&s_ftm_offset,0,sizeof(s_ftm_offset));offset_calls=mode_calls=0;mode_error=0;mode_mismatch=false;offset_error=failed?77:0;
        calls=0;fail_at=nth;collect=inject=true;
        if(status)*out=js_wifi_ftm_responder_offset_status(ctx,NULL,0,NULL);
        else *out=js_wifi_ftm_set_responder_offset(ctx,NULL,arity?0:1,arg);
        inject=collect=false;
        if(!nth){total=calls;assert(JS_IsException(*out)==!(status || (valid && !failed)));}
        assert(offset_calls==(valid?1U:0U));
        if(valid)assert(s_ftm_offset.known==!failed && s_ftm_offset.uncertain==failed);
        if(JS_IsException(*out)) {
            assert(JS_HasException(ctx));*out=JS_GetException(ctx);
            if(!nth && failed) {
                *out=JS_GetPropertyStr(ctx,*out,"details");*field=JS_GetPropertyStr(ctx,*out,"mutationAttempted");assert(*field==JS_TRUE);
                *out=JS_GetPropertyStr(ctx,*out,"offset");*field=JS_GetPropertyStr(ctx,*out,"uncertain");assert(*field==JS_TRUE);
            }
        } else {
            *field=JS_GetPropertyStr(ctx,*out,"valueCm");
            if(status)assert(JS_IsNull(*field));
            else {int32_t value;assert(!JS_ToInt32(ctx,&value,*field) && value==atoi(expr));}
        }
        if(valid && !failed) { /* Conversion failure must not undo SDK acceptance. */
            *out=js_wifi_ftm_responder_offset_status(ctx,NULL,0,NULL);assert(!JS_IsException(*out));
            *field=JS_GetPropertyStr(ctx,*out,"known");assert(*field==JS_TRUE && offset_calls==1);
        }
        JS_PopGCRef(ctx,&field_ref);JS_PopGCRef(ctx,&out_ref);JS_PopGCRef(ctx,&arg_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
