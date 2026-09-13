
static uint32_t value(JSContext *ctx,JSValue object,const char *key) {
    uint32_t n;assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,object,key)));return n;
}
int main(void) {
    for(int scenario=0;scenario<13;scenario++) {
        int total=1;
        for(int nth=0;nth<=total;nth++) {
            union {wifi_event_sta_itwt_setup_t individual;wifi_event_sta_btwt_setup_t broadcast;
                wifi_event_sta_itwt_teardown_t down;wifi_event_sta_btwt_teardown_t bdown;
                wifi_event_sta_itwt_probe_t probe;wifi_event_sta_itwt_suspend_t suspend;
                wifi_event_sta_twt_wakeup_t wake;} native;
            memset(&native,0xa5,sizeof(native));int id=WIFI_EVENT_ITWT_SETUP;const void *input=&native;
            switch(scenario) {
            case 0:
                native.individual.status=1;native.individual.target_wake_time=UINT64_MAX;
                native.individual.config=(wifi_itwt_setup_config_t){.setup_cmd=TWT_ACCEPT,.trigger=1,.flow_type=1,
                    .flow_id=7,.wake_invl_expn=31,.wake_duration_unit=1,.min_wake_dura=255,
                    .wake_invl_mant=65535,.twt_id=32767,.timeout_time_ms=65535};break;
            case 1:native.individual.status=0;native.individual.reason=93;break;
            case 2:id=WIFI_EVENT_BTWT_SETUP;
                native.broadcast=(wifi_event_sta_btwt_setup_t){.status=BTWT_SETUP_SUCCESS,.setup_cmd=TWT_ACCEPT,
                    .btwt_id=31,.min_wake_dura=255,.wake_invl_expn=31,.wake_invl_mant=65535,.trigger=true,
                    .flow_type=1,.target_wake_time=UINT64_C(0x0123456789abcdef)};break;
            case 3:case 4:id=WIFI_EVENT_BTWT_SETUP;
                native.broadcast.status=scenario==3?BTWT_SETUP_TXFAIL:BTWT_SETUP_TIMEOUT;native.broadcast.reason=77;break;
            case 5:id=WIFI_EVENT_ITWT_TEARDOWN;native.down.status=ITWT_TEARDOWN_SUCCESS;native.down.flow_id=8;break;
            case 6:id=WIFI_EVENT_BTWT_TEARDOWN;native.bdown.status=BTWT_TEARDOWN_SUCCESS;native.bdown.btwt_id=32;break;
            case 7:case 8:id=WIFI_EVENT_ITWT_PROBE;native.probe.status=scenario==7?ITWT_PROBE_FAIL:ITWT_PROBE_TIMEOUT;native.probe.reason=65;break;
            case 9:id=WIFI_EVENT_ITWT_SUSPEND;native.suspend.status=-123;native.suspend.flow_id_bitmap=0x81;
                native.suspend.actual_suspend_time_ms[0]=UINT32_MAX;native.suspend.actual_suspend_time_ms[7]=0x80000000U;break;
            case 10:id=WIFI_EVENT_TWT_WAKEUP;native.wake.twt_type=TWT_TYPE_BROADCAST;native.wake.flow_id=31;break;
            case 11:input=NULL;break;
            case 12:id=WIFI_EVENT_ITWT_PROBE;native.probe.status=ITWT_PROBE_SUCCESS;native.probe.reason=65;break;
            }
            sends=wakes=0;calls=0;inject=1;collect=0;fail_at=0;
            esp32_mquickjs_wifi_watch_capture(id,input,5);assert(sends==1 && wakes==1 && !locked && !calls);
            if(scenario==1 || scenario==3 || scenario==4)assert(!queued_event.twt_setup.target_wake_time && !queued_event.twt_setup.mantissa);
            if(scenario==9)for(size_t i=1;i<7;i++)assert(!queued_event.twt_suspend.duration_ms[i]);
            memset(&native,0,sizeof(native));
            void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
            test_ctx=ctx;JSGCRef r,data_ref,config_ref,time_ref;
            JSValue *result=JS_PushGCRef(ctx,&r);calls=0;fail_at=nth;collect=1;inject=1;
            *result=wifi_watch_to_js(ctx,&queued_event,NULL);
            if(nth==0){assert(!JS_IsException(*result));total=calls;}else assert(JS_IsException(*result));
            collect=0;inject=0;
            if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
            else {
                JSValue *data=JS_PushGCRef(ctx,&data_ref);*data=JS_GetPropertyStr(ctx,*result,"data");
                if(scenario==11)assert(JS_IsNull(*data));
                else {
                    assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"dataUnavailableReason")));
                    if(scenario<5) {
                        JSValue *config=JS_PushGCRef(ctx,&config_ref);*config=JS_GetPropertyStr(ctx,*data,"configuration");
                        JSValue *time=JS_PushGCRef(ctx,&time_ref);*time=JS_GetPropertyStr(ctx,*data,"targetWakeTime");
                        if(scenario==0 || scenario==2) {
                            assert(value(ctx,*data,"statusId")==1 && JS_IsNull(JS_GetPropertyStr(ctx,*data,"reason")));
                            assert(value(ctx,*config,"wakeIntervalMantissa")==65535 && value(ctx,*config,"wakeIntervalExponent")==31);
                            assert(value(ctx,*config,"minimumWakeDuration")==255 && JS_GetPropertyStr(ctx,*config,"trigger")==JS_TRUE);
                            assert(value(ctx,*time,"low")== (scenario==0?UINT32_MAX:0x89abcdefU));
                            assert(value(ctx,*time,"high")== (scenario==0?UINT32_MAX:0x01234567U));
                            if(!scenario)assert(value(ctx,*config,"flowId")==7 && value(ctx,*config,"wakeDurationUnitId")==1 && value(ctx,*config,"twtId")==32767 && value(ctx,*config,"timeoutMs")==65535);
                            else assert(value(ctx,*config,"broadcastId")==31 && JS_IsUndefined(JS_GetPropertyStr(ctx,*config,"twtId")));
                        } else {
                            assert(JS_IsNull(*config) && JS_IsNull(*time));
                            if(scenario==4)assert(JS_IsNull(JS_GetPropertyStr(ctx,*data,"reason")));
                            else assert(value(ctx,*data,"reason")== (scenario==1?93:77));
                        }
                        JS_PopGCRef(ctx,&time_ref);JS_PopGCRef(ctx,&config_ref);
                    }
                    if(scenario==5)assert(value(ctx,*data,"flowId")==8);
                    if(scenario==6)assert(value(ctx,*data,"broadcastId")==32);
                    if(scenario==7)assert(value(ctx,*data,"reason")==65);
                    if(scenario==8)assert(value(ctx,*data,"reason")==65);
                    if(scenario==12)assert(JS_IsNull(JS_GetPropertyStr(ctx,*data,"reason")));
                    if(scenario==9) {
                        assert(value(ctx,*data,"flowIdBitmap")==0x81);
                        JSValue *times=JS_PushGCRef(ctx,&time_ref);*times=JS_GetPropertyStr(ctx,*data,"actualSuspendTimeMs");
                        assert(value(ctx,*times,"length")==8);
                        for(uint32_t i=0;i<8;i++) {JSValue item=JS_GetPropertyUint32(ctx,*times,i);
                            if(i==0 || i==7){uint32_t n;assert(!JS_ToUint32(ctx,&n,item) && n==(i==0?UINT32_MAX:0x80000000U));}
                            else assert(JS_IsNull(item));}
                        JS_PopGCRef(ctx,&time_ref);
                    }
                    if(scenario==10)assert(value(ctx,*data,"typeId")==TWT_TYPE_BROADCAST && value(ctx,*data,"flowId")==31);
                }
                JS_PopGCRef(ctx,&data_ref);
            }
            JS_PopGCRef(ctx,&r);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
        }
    }
    return 0;
}
