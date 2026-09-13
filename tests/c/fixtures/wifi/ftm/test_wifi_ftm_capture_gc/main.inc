
int main(int argc,char **argv) {
    assert(argc==4);int total=1,expected=atoi(argv[3]);
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef root_ref,result_ref,array_ref,item_ref,word_ref;
        JSValue *root=JS_PushGCRef(ctx,&root_ref),*result=JS_PushGCRef(ctx,&result_ref);
        JSValue *array=JS_PushGCRef(ctx,&array_ref),*item=JS_PushGCRef(ctx,&item_ref),*word=JS_PushGCRef(ctx,&word_ref);
        esp32_mquickjs_wifi_ftm_session_t *session=NULL;
        esp32_mquickjs_wifi_ftm_status_t status={0};
        if(!strcmp(argv[1],"capture")) {
            *root=JS_Eval(ctx,argv[2],strlen(argv[2]),"ftm-capture",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        } else {
            wifi_ftm_initiator_cfg_t config={.resp_mac={2,3,4,5,6,7},.channel=6};
            assert(!esp32_mquickjs_wifi_ftm_create(&config,2,&session));
            session->status.started=session->status.submitted=session->status.retired=session->status.report_ready=true;
            session->status.native=(esp32_mquickjs_wifi_ftm_state_t){.token={7,45},.submitted=true,.terminal=true,
                .report_consumed=true,.copied_entries=2,.sdk_fenced=true,.event_fenced=true,
                .report={.peer_mac={2,3,4,5,6,7},.status=FTM_STATUS_SUCCESS,.rtt_raw=UINT32_MAX,.rtt_est=20,.dist_est=30,.ftm_report_num_entries=3}};
            session->entries[0]=(wifi_ftm_report_entry_t){.dlog_token=255,.rssi=-127,.rtt=UINT32_MAX,.ppm=-32768,
                .t1=UINT64_MAX,.t2=UINT64_C(0x123456789abcdef0),.t3=UINT64_C(0x100000000),.t4=0};
            session->entries[1]=session->entries[0];assert(esp32_mquickjs_wifi_ftm_status(session,&status));
        }
        calls=0;fail_at=nth;inject=true;collect=true;bool ok;
        if(!strcmp(argv[1],"capture")) {
            ftm_options_t options={0};ok=ftm_options(ctx,&root_ref,&options);
            if(!nth){total=calls;assert(ok==expected);}
            if(ok)assert(esp32_mquickjs_wifi_ftm_config_valid(&options.config) && options.report_capacity<=64);
            else assert(JS_HasException(ctx));
        } else if(!strcmp(argv[1],"report")) {
            *result=ftm_report_to_js(ctx,session,&status);ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok);}
        } else if(!strcmp(argv[1],"status")) {
            *result=ftm_status_to_js(ctx,&status);ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok);}
        } else if(!strcmp(argv[1],"receive-timeout")) {
            esp32_mquickjs_future_driver_state_t state={.operation=FTM_RECEIVE,.receive_timed_out=true};
            *result=ftm_finish(ctx,&state);ok=*result==JS_NULL;assert(ok);if(!nth)total=calls;
        } else {
            esp32_mquickjs_future_driver_state_t state={.session=session,.operation=FTM_END};
            *result=ftm_error(ctx,&state,true);ok=!JS_IsException(*result);assert(!ok && JS_HasException(ctx));
            if(!nth)total=calls;
        }
        inject=collect=false;
        if(JS_HasException(ctx))(void)JS_GetException(ctx);
        if(session && !strcmp(argv[1],"report")) {
            assert(session->status.report_ready && session->entries[0].t1==UINT64_MAX);
            if(!ok){*result=ftm_report_to_js(ctx,session,&status);assert(!JS_IsException(*result));}
            *array=JS_GetPropertyStr(ctx,*result,"entries");*item=JS_GetPropertyUint32(ctx,*array,0);
            *word=JS_GetPropertyStr(ctx,*item,"t2Ps");
            double low,high;assert(!JS_ToNumber(ctx,&low,JS_GetPropertyStr(ctx,*word,"low")));
            assert(!JS_ToNumber(ctx,&high,JS_GetPropertyStr(ctx,*word,"high")));
            assert(low==0x9abcdef0U && high==0x12345678U);
            assert(JS_GetPropertyStr(ctx,*result,"truncated")==JS_TRUE);
        }
        if(session){esp32_mquickjs_wifi_ftm_close(session);esp32_mquickjs_wifi_ftm_release(session);}
        assert(!s_ftm_handles && !s_ftm_reserved_entries && !native_live && !session_locked);
        JS_PopGCRef(ctx,&word_ref);JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&array_ref);
        JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&root_ref);
        JS_FreeContext(ctx);free(heap);assert(!root_count);
    }
    return 0;
}
