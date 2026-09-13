
int main(int argc,char **argv) {
    assert(argc==5);int total=1,expected=atoi(argv[3]);
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef view_ref,args[2],root_ref,result_ref;
        JSValue *view=JS_PushGCRef(ctx,&view_ref),*root=JS_PushGCRef(ctx,&root_ref);
        JSValue *result=JS_PushGCRef(ctx,&result_ref);
        JS_PushGCRef(ctx,&args[0]);JS_PushGCRef(ctx,&args[1]);
        uint8_t *payload=heap_caps_malloc(24,1);memset(payload,0,24);payload[0]=0x80;
        *view=esp32_mquickjs_new_owned_byte_view(ctx,payload,24);assert(!JS_IsException(*view));
        assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"view",*view)));
        if(!strcmp(argv[1],"capture")) {
            *root=JS_Eval(ctx,argv[2],strlen(argv[2]),"capture",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
            args[0].val=JS_GetPropertyStr(ctx,*root,"frame");args[1].val=JS_GetPropertyStr(ctx,*root,"options");
            assert(!JS_IsException(args[0].val) && !JS_IsException(args[1].val));
        }
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok;
        if(!strcmp(argv[1],"capture")) {
            esp32_mquickjs_future_driver_state_t *state=NULL;
            ok=raw_tx_capture(ctx,NULL,2,args,&state);
            if(!nth){total=calls;assert(ok==expected);}
            if(ok) {
                assert(state && state->length==24 && state->bytes[0]==0x80 && !state->lease.acquired);
                assert(state->validation==ESP32_MQUICKJS_WIFI_RAW_TX_VALID);
                esp32_mquickjs_memory_payload_free(state->bytes);heap_caps_free(state);
            } else assert(!state && JS_HasException(ctx));
        } else {
            esp32_mquickjs_future_driver_state_t state={0};
            state.result.frame_type=ESP32_MQUICKJS_WIFI_RAW_TX_BEACON;
            state.result.token.identity=7;state.result.token.generation=9;state.result.byte_length=24;
            state.result.driver_accepted=state.result.driver_completed=true;
            state.result.completion.status=ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS;
            terminated_snapshot=!strcmp(argv[1],"terminated-status");
            *result=!strcmp(argv[1],"finish") ? raw_tx_finish(ctx,&state) :
                (!strcmp(argv[1],"status") || terminated_snapshot) ? esp32_mquickjs_wifi_raw_tx_status(ctx) : js_wifi_raw_tx_capabilities(ctx,NULL,0,NULL);
            ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok);}
            if(ok && !strcmp(argv[1],"finish")) {
                JSValue flag=JS_GetPropertyStr(ctx,*result,"driverCompleted");assert(flag==JS_TRUE);
            }
            if(ok && (!strcmp(argv[1],"status") || terminated_snapshot)) {
                int32_t number;
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"laneIdentity")) && number==77);
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"operationIdentity")) && number==45);
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"laneWaiters")) && number==8);
                assert(JS_GetPropertyStr(ctx,*result,"laneIdentityExhausted")==JS_TRUE);
                assert(JS_GetPropertyStr(ctx,*result,"nativeTerminated")==JS_NewBool(terminated_snapshot));
                if(terminated_snapshot) {
                    assert(JS_GetPropertyStr(ctx,*result,"operationActive")==JS_FALSE);
                    assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"terminatedRadioGeneration")) && number==7);
                } else assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"terminatedRadioGeneration")));
            }
        }
        inject=false;collect=false;
        if(!ok) {
            assert(JS_HasException(ctx));*result=JS_GetException(ctx);
            if(!nth && argv[4][0]) {
                args[1].val=JS_GetPropertyStr(ctx,*result,"message");
                JSCStringBuf buffer;size_t length;
                const char *text=JS_ToCStringLen(ctx,&length,args[1].val,&buffer);
                assert(text && length==strlen(argv[4]) && !memcmp(text,argv[4],length));
            }
        }
        assert(!JS_IsException(js_byte_view_close(ctx,view,0,NULL)));
        JS_PopGCRef(ctx,&args[1]);JS_PopGCRef(ctx,&args[0]);JS_PopGCRef(ctx,&result_ref);
        JS_PopGCRef(ctx,&root_ref);JS_PopGCRef(ctx,&view_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
