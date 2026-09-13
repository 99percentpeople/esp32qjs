
int main(int argc,char **argv) {
    assert(argc==5);int total=1,expected=atoi(argv[3]);
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef view_ref,arg,result_ref,message_ref;
        JSValue *view=JS_PushGCRef(ctx,&view_ref),*result=JS_PushGCRef(ctx,&result_ref);
        JS_PushGCRef(ctx,&arg);JSValue *message=JS_PushGCRef(ctx,&message_ref);
        uint8_t *payload=heap_caps_malloc(1,1);payload[0]=4;
        *view=esp32_mquickjs_new_owned_byte_view(ctx,payload,1);assert(!JS_IsException(*view));
        assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"view",*view)));
        if(!strcmp(argv[1],"capture")) {
            arg.val=JS_Eval(ctx,argv[2],strlen(argv[2]),"capture",JS_EVAL_RETVAL);assert(!JS_IsException(arg.val));
        }
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok;
        if(!strcmp(argv[1],"capture")) {
            esp32_mquickjs_future_driver_state_t *state=NULL;
            ok=action_capture(ctx,NULL,1,&arg,&state);
            if(!nth){total=calls;assert(ok==expected);}
            if(ok) {
                assert(state && !state->token.identity && !state->submitted);
                assert(state->request_bytes==sizeof(*state->request)+state->request->data_len);
                assert(state->request->data[0]==4 && state->request->channel==6);
                assert(state->request->rx_cb==esp32_mquickjs_wifi_action_receive);
                assert(state->request->type==WIFI_OFFCHAN_TX_REQ && !state->request->op_id);
                esp32_mquickjs_memory_payload_free(state->request);heap_caps_free(state);
            } else assert(!state && JS_HasException(ctx));
        } else {
            wifi_action_tx_req_t request={.ifx=WIFI_IF_STA,.channel=6,.data_len=1};
            esp32_mquickjs_future_driver_state_t state={.request=&request,.token={.generation=7,.identity=45},
                .result={.tx_status=WIFI_ACTION_TX_DONE,.terminal_status=WIFI_ACTION_TX_DURATION_COMPLETED,.operation_id=255}};
            *result=!strcmp(argv[1],"finish") ? action_finish(ctx,&state) : !strcmp(argv[1],"status") ?
                esp32_mquickjs_wifi_action_status(ctx) : js_wifi_action_capabilities(ctx,NULL,0,NULL);
            ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok);}
            if(ok && !strcmp(argv[1],"status")) {
                assert(JS_GetPropertyStr(ctx,*result,"cleanupPending")==JS_TRUE);
                assert(JS_GetPropertyStr(ctx,*result,"identityExhausted")==JS_TRUE);
                int32_t n;assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,*result,"operationId")) && n==255);
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
        assert(!JS_IsException(js_byte_view_close(ctx,view,0,NULL)));
        JS_PopGCRef(ctx,&message_ref);JS_PopGCRef(ctx,&arg);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&view_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
