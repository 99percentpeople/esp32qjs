
int main(int argc,char **argv) {
    assert(argc==4 || argc==5);int total=0;bool capture=!strcmp(argv[1],"capture");
    const esp32_mquickjs_wifi_recovery_kind_t kinds[]={ESP32_MQUICKJS_WIFI_RECOVERY_ACTION,
        ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX,ESP32_MQUICKJS_WIFI_RECOVERY_FTM};
    for(int kind=0;kind<(argc==5?1:3);++kind) {
    kind_case=argc==5?ESP32_MQUICKJS_WIFI_RECOVERY_TWT:kinds[kind];timeout_case=!strcmp(argv[1],"timeout");total=0;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef arg,result_ref;JS_PushGCRef(ctx,&arg);JSValue *result=JS_PushGCRef(ctx,&result_ref);
        if(capture){arg.val=JS_Eval(ctx,argv[2],strlen(argv[2]),"capture",JS_EVAL_RETVAL);assert(!JS_IsException(arg.val));}
        calls=0;fail_at=nth;inject=collect=true;
        bool ok;
        if(capture) {
            esp32_mquickjs_future_driver_state_t *state=NULL;
            ok=kind_case==ESP32_MQUICKJS_WIFI_RECOVERY_TWT?twt_recovery_capture(ctx,NULL,1,&arg,&state):
                kind_case==ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX?raw_tx_recovery_capture(ctx,NULL,1,&arg,&state):
                kind_case==ESP32_MQUICKJS_WIFI_RECOVERY_FTM?ftm_recovery_capture(ctx,NULL,1,&arg,&state):recovery_capture(ctx,NULL,1,&arg,&state);
            if(!nth){total=calls;assert(ok==(atoi(argv[3])!=0));}
            if(ok){assert(state && state->operation.kind==kind_case && !state->started && !state->recovery.execution.admitted && !state->recovery.access_point);heap_caps_free(state);}
            else assert(!state && JS_HasException(ctx));
        } else {
            esp32_mquickjs_future_driver_state_t state={.complete=true,.operation={9,51,kind_case}};
            if(kind_case==ESP32_MQUICKJS_WIFI_RECOVERY_TWT)state.operation.identity=0;
            state.recovery.execution.admitted=true;state.recovery.execution.stage="recovery-native-drain";
            if(!strcmp(argv[1],"finish"))*result=recovery_finish(ctx,&state);
            else if(!strcmp(argv[1],"timeout"))*result=recovery_on_timeout(ctx,&state,1000);
            else{state.error=77;*result=recovery_error(ctx,&state,false);}
            ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok==!strcmp(argv[1],"finish"));}
            if(ok){int32_t value;assert(!JS_ToInt32(ctx,&value,JS_GetPropertyStr(ctx,*result,"radioGeneration")) && value==10);
                if(kind_case==ESP32_MQUICKJS_WIFI_RECOVERY_TWT)assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"sequence")));}
        }
        inject=collect=false;
        if(!ok){assert(JS_HasException(ctx));*result=JS_GetException(ctx);}
        JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&arg);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    }
    return 0;
}
