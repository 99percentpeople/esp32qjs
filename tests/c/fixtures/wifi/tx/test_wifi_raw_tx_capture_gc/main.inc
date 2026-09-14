
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
            if(!strcmp(argv[2],"failed")) {
                state.result.completion.raw_status=WIFI_SEND_FAIL;
                state.result.completion.status=ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_FAILED;
            } else if(!strcmp(argv[2],"unknown")) {
                state.result.completion.raw_status=99;
                state.result.completion.status=ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_UNKNOWN;
            } else if(!strncmp(argv[2],"descriptor-",11)) {
                int code=atoi(argv[2]+11);
                state.result.completion.mac_status_available=true;
                state.result.completion.mac_status_byte=(uint8_t)code;
                state.result.completion.raw_status=code==1 ? WIFI_SEND_SUCCESS : WIFI_SEND_FAIL;
                state.result.completion.status=code==1 ? ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS : ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_FAILED;
                if(!strcmp(argv[2],"descriptor-sdk-unknown")) {
                    state.result.completion.raw_status=99;
                    state.result.completion.status=ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_UNKNOWN;
                }
            } else if(!strcmp(argv[2],"incomplete")) state.result.driver_completed=false;
            terminated_snapshot=!strcmp(argv[1],"terminated-status");
            *result=!strcmp(argv[1],"finish") ? raw_tx_finish(ctx,&state) :
                (!strcmp(argv[1],"status") || terminated_snapshot) ? esp32_mquickjs_wifi_raw_tx_status(ctx) : js_wifi_raw_tx_capabilities(ctx,NULL,0,NULL);
            ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok == (strcmp(argv[2],"incomplete") != 0));}
            if(ok && (!strcmp(argv[1],"status") || terminated_snapshot)) {
                int32_t number;
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"laneIdentity")) && number==77);
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"operationIdentity")) && number==45);
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"laneWaiters")) && number==8);
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"invalidCallbacks")) && number==3);
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"mismatchedCallbacks")) && number==5);
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"orphanCallbacks")) && number==7);
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"duplicateCallbacks")) && number==9);
                if (terminated_snapshot) {
                    assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"correlationFailureReason")));
                    assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"correlationFailureIdentity")));
                    assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"correlationFailureGeneration")));
                } else {
                    /* Origin may differ from the first retained operation. */
                    assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"correlationFailureIdentity")) && number==46);
                    assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"correlationFailureGeneration")) && number==7);
                    args[1].val=JS_GetPropertyStr(ctx,*result,"correlationFailureReason");
                    JSCStringBuf buffer;size_t length;
                    const char *reason=JS_ToCStringLen(ctx,&length,args[1].val,&buffer);
                    assert(reason && length==15 && !memcmp(reason,"source-mismatch",15));
                }
                assert(JS_GetPropertyStr(ctx,*result,"laneIdentityExhausted")==JS_TRUE);
                assert(JS_GetPropertyStr(ctx,*result,"nativeTerminated")==JS_NewBool(terminated_snapshot));
                if(terminated_snapshot) {
                    assert(JS_GetPropertyStr(ctx,*result,"operationActive")==JS_FALSE);
                    assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"terminatedRadioGeneration")) && number==7);
                } else assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"terminatedRadioGeneration")));
            }
        }
        inject=false;collect=false;
        if(ok && !strcmp(argv[1],"finish")) {
            assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"tx",*result)));
            const char *check = "tx.driverStatus === undefined && tx.rawStatus === undefined && tx.rawStatusName === undefined && tx.driverAccepted === undefined && tx.driverCompleted === undefined && tx.macStatusByte === undefined && tx.macStatusName === undefined";
            *root=JS_Eval(ctx,check,strlen(check),"completion-shape",JS_EVAL_RETVAL);assert(*root==JS_TRUE);
            bool descriptor=!strncmp(argv[2],"descriptor-",11);
            const char *domain=descriptor ? "tx.completion.native.domain === 'esp_wifi_tx_descriptor_status'" :
                "tx.completion.native.domain === 'wifi_tx_status_t'";
            *root=JS_Eval(ctx,domain,strlen(domain),"completion-domain",JS_EVAL_RETVAL);assert(*root==JS_TRUE);
            const char *outcome = !strcmp(argv[2],"unknown") ?
                "tx.completion.status === 'unknown' && tx.completion.native.code === 99 && tx.completion.native.name === null" :
                !strcmp(argv[2],"failed") ? "tx.completion.status === 'failed' && tx.completion.native.code === 1 && tx.completion.native.name === 'WIFI_SEND_FAIL'" :
                "tx.completion.status === 'success' && tx.completion.native.code === 0 && tx.completion.native.name === 'WIFI_SEND_SUCCESS'";
            char descriptor_check[256];
            if(descriptor) {
                int code=atoi(argv[2]+11);
                const char *name=code==1 ? "'success'" : code==2 || code==3 ? "'frame-exchange'" :
                    code==4 ? "'discarded'" : "null";
                const char *status=!strcmp(argv[2],"descriptor-sdk-unknown") ? "unknown" : code==1 ? "success" : "failed";
                snprintf(descriptor_check,sizeof(descriptor_check),
                    "tx.completion.status === '%s' && tx.completion.native.code === %d && tx.completion.native.name === %s",status,code,name);
                outcome=descriptor_check;
            }
            *root=JS_Eval(ctx,outcome,strlen(outcome),"completion-value",JS_EVAL_RETVAL);assert(*root==JS_TRUE);
        }
        if(ok && !strcmp(argv[1],"capabilities")) {
            assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"caps",*result)));
#if defined(ESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT) && ESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT
            const char *check = "caps.supports.extendedManagement === true && Array.isArray(caps.frameTypes) && caps.frameTypes.map(function(f){return f.name;}).sort().join(',') === "
                "'action,action-no-ack,association-request,association-response,atim,authentication,beacon,data,deauthentication,disassociation,probe-request,probe-response,reassociation-request,reassociation-response,timing-advertisement'";
#else
            const char *check = "Array.isArray(caps.frameTypes) && caps.frameTypes.map(function(f){return f.name;}).sort().join(',') === "
                "'action,beacon,data,probe-request,probe-response'";
#endif
            *root=JS_Eval(ctx,check,strlen(check),"frame-types",JS_EVAL_RETVAL);
            assert(*root==JS_TRUE);
            *root=JS_Eval(ctx,"caps.frameTypes[0].name='changed'; caps.frameTypes[0].type=3; caps.frameTypes.length=0;",
                strlen("caps.frameTypes[0].name='changed'; caps.frameTypes[0].type=3; caps.frameTypes.length=0;"),"snapshot",0);
            assert(!JS_IsException(*root));
            *root=js_wifi_raw_tx_capabilities(ctx,NULL,0,NULL);
            assert(!JS_IsException(*root));
            assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"caps",*root)));
            *root=JS_Eval(ctx,check,strlen(check),"fresh-snapshot",JS_EVAL_RETVAL);
            assert(*root==JS_TRUE);
            const char *names[]={"beacon","probe-request","probe-response","action","data",
#if defined(ESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT) && ESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT
                "association-request","association-response","reassociation-request","reassociation-response","timing-advertisement","atim","disassociation","authentication","deauthentication","action-no-ack",
#endif
            };
            const uint8_t frame_control[]={0x80,0x40,0x50,0xd0,0x08,
#if defined(ESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT) && ESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT
                0x00,0x10,0x20,0x30,0x60,0x90,0xa0,0xb0,0xc0,0xe0,
#endif
            };
            for(unsigned i=0;i<sizeof(frame_control);i++) {
                uint8_t bytes[24]={0};bytes[0]=frame_control[i];
                esp32_mquickjs_wifi_raw_tx_validation_policy_t policy={0};
                esp32_mquickjs_wifi_raw_tx_validated_frame_t validated={0};
                assert(esp32_mquickjs_wifi_raw_tx_validate(bytes,sizeof(bytes),&policy,&validated)==ESP32_MQUICKJS_WIFI_RAW_TX_VALID);
                esp32_mquickjs_wifi_raw_tx_broker_status_t native={0};
                native.frame_type=validated.frame_type; native.driver_accepted=native.driver_completed=true;
                *root=esp32_mquickjs_wifi_raw_tx_result_to_js(ctx,&native,ESP32_MQUICKJS_WIFI_RAW_TX_STATION,1);
                assert(!JS_IsException(*root));
                args[1].val=JS_GetPropertyStr(ctx,*root,"frameType");
                int32_t code;
                assert(!JS_ToInt32(ctx,&code,JS_GetPropertyStr(ctx,args[1].val,"type")) && code==((frame_control[i]>>2)&3));
                assert(!JS_ToInt32(ctx,&code,JS_GetPropertyStr(ctx,args[1].val,"subtype")) && code==(frame_control[i]>>4));
                args[1].val=JS_GetPropertyStr(ctx,args[1].val,"name");
                JSCStringBuf buffer;size_t length;
                const char *name=JS_ToCStringLen(ctx,&length,args[1].val,&buffer);
                assert(name && length==strlen(names[i]) && !memcmp(name,names[i],length));
            }
        }
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
