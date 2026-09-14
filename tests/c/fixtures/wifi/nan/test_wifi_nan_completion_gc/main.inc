int main(int argc,char **argv) {
    assert(argc==3);int raw=atoi(argv[1]);bool usd=atoi(argv[2]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef ref;JSValue *result=JS_PushGCRef(ctx,&ref);
        esp32_mquickjs_wifi_nan_message_status_t s={.identity=7,.submitted=true,.tx={
            .tx_done=raw>=0,.tx_succeeded=raw==0,.has_native_status=usd,.native_status=raw,.buffer_retired=false}};
        calls=0;fail_at=nth;inject=true;collect=true;
        *result=nan_message_result_to_js(ctx,&s);bool ok=!JS_IsException(*result);
        if(!nth){total=calls;assert(ok);}
        inject=false;collect=false;
        if(ok) {
            assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"tx",*result)));
            const char *check="tx.txCompleted === undefined && tx.txSucceeded === undefined && tx.bufferRetired === false && tx.identity === 7";
            assert(JS_Eval(ctx,check,strlen(check),"shape",JS_EVAL_RETVAL)==JS_TRUE);
            check=raw<0?"tx.completion === null":raw==0?"tx.completion.status === 'success'":raw==1?"tx.completion.status === 'failed'":"tx.completion.status === 'unknown'";
            assert(JS_Eval(ctx,check,strlen(check),"status",JS_EVAL_RETVAL)==JS_TRUE);
            if(raw>=0) {
                check=!usd?"tx.completion.native === null":raw==99?"tx.completion.native.code === 99 && tx.completion.native.name === null":
                    raw==1?"tx.completion.native.code === 1 && tx.completion.native.name === 'WIFI_ACTION_TX_FAILED'":"tx.completion.native.code === 0 && tx.completion.native.name === 'WIFI_ACTION_TX_DONE'";
                assert(JS_Eval(ctx,check,strlen(check),"native",JS_EVAL_RETVAL)==JS_TRUE);
            }
        } else {assert(JS_HasException(ctx));*result=JS_GetException(ctx);}
        s.done=true;
        *result=nan_message_finish(ctx,&s);
        assert(JS_IsException(*result)==(raw<0));
        if(raw<0) *result=JS_GetException(ctx);
        /* A completion cannot hide submission, cancellation or timeout errors. */
        s.error=259;*result=nan_message_finish(ctx,&s);assert(JS_IsException(*result));*result=JS_GetException(ctx);
        s.error=0;s.timed_out=true;*result=nan_message_finish(ctx,&s);assert(JS_IsException(*result));*result=JS_GetException(ctx);
        s.timed_out=false;s.cancelled=true;*result=nan_message_finish(ctx,&s);assert(JS_IsException(*result));*result=JS_GetException(ctx);
        *result=esp32_mquickjs_native_code_to_js(ctx,"esp_err_t",-999,"UNKNOWN ERROR");
        assert(!JS_IsException(*result));
        assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"name")));
        JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);assert(!root_count&&!native_live);free(heap);
    }
    return 0;
}
