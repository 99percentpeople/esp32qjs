
int main(int argc,char **argv) {
    assert(argc==5);int total=1,expected=atoi(argv[3]);
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef root_ref,result_ref,message_ref;
        JSValue *root=JS_PushGCRef(ctx,&root_ref),*result=JS_PushGCRef(ctx,&result_ref);
        JSValue *message=JS_PushGCRef(ctx,&message_ref);
        if(!strcmp(argv[1],"capture")) {
            *root=JS_Eval(ctx,argv[2],strlen(argv[2]),"capture",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        }
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok;
        if(!strcmp(argv[1],"capture")) {
            wifi_roc_req_t request={0};uint32_t timeout=0;
            ok=roc_options(ctx,&root_ref,&request,&timeout);
            if(!nth){total=calls;assert(ok==expected);}
            if(ok) {
                assert(request.rx_cb==esp32_mquickjs_wifi_action_receive && !request.done_cb && !request.op_id);
                assert(request.type==WIFI_ROC_REQ && request.wait_time_ms>=1 && request.wait_time_ms<=60000);
                assert(timeout>=1 && timeout<=60000);
            } else assert(JS_HasException(ctx));
        } else {
            esp32_mquickjs_wifi_roc_status_t status={.request={.channel=6,.wait_time_ms=100},
                .started=true,.submitted=true,.close_requested=true,.retired=true,
                .native={.identity=45,.generation=7,.operation_id=255,.submitted=true,.terminal=true,.terminal_status=WIFI_ROC_DONE}};
            *result=roc_status_to_js(ctx,&status);ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok);}
            if(ok) {
                assert(JS_GetPropertyStr(ctx,*result,"cleanupPending")==JS_FALSE);
                assert(JS_GetPropertyStr(ctx,*result,"driverAccepted")==JS_TRUE);
                int32_t n;assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,*result,"operationId")) && n==255);
                assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,*result,"sequence")) && n==45);
            }
        }
        inject=false;collect=false;
        if(!ok) {
            assert(JS_HasException(ctx));*result=JS_GetException(ctx);
            if(!nth && argv[4][0]) {
                *message=JS_GetPropertyStr(ctx,*result,"message");
                JSCStringBuf buffer;size_t length;
                const char *text=JS_ToCStringLen(ctx,&length,*message,&buffer);
                assert(text && length==strlen(argv[4]) && !memcmp(text,argv[4],length));
            }
        }
        JS_PopGCRef(ctx,&message_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&root_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
