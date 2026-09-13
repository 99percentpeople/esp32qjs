
static uint32_t number(JSContext *ctx,JSValue object,const char *key) {
    uint32_t value;assert(!JS_ToUint32(ctx,&value,JS_GetPropertyStr(ctx,object,key)));return value;
}
int main(void) {
    for(int scenario=0;scenario<12;scenario++) {
        int total=1;
        for(int nth=0;nth<=total;nth++) {
            union {wifi_event_ftm_report_t ftm;wifi_event_action_tx_status_t action;
                wifi_event_roc_done_t roc;wifi_event_ap_wrong_password_t wrong;
                wifi_event_sta_beacon_offset_unstable_t beacon;wifi_event_sta_connected_t connected;} native={0};
            int id=WIFI_EVENT_FTM_REPORT;const void *input=&native;
            switch(scenario) {
            case 0:case 1:
                native.ftm.peer_mac[0]=2;native.ftm.status=scenario?FTM_STATUS_FAIL:FTM_STATUS_SUCCESS;
                native.ftm.rtt_raw=UINT32_MAX;native.ftm.rtt_est=0x80000000U;
                native.ftm.dist_est=123;native.ftm.ftm_report_num_entries=255;break;
            case 2:id=WIFI_EVENT_ACTION_TX_STATUS;native.action=(wifi_event_action_tx_status_t){
                .ifx=WIFI_IF_AP,.context=0xdeadbeef,.status=WIFI_ACTION_TX_DURATION_COMPLETED,.op_id=255,.channel=177};break;
            case 3:id=WIFI_EVENT_ROC_DONE;native.roc=(wifi_event_roc_done_t){.context=0xdeadbeef,.status=0,.op_id=254,.channel=36};break;
            case 4:id=WIFI_EVENT_AP_WRONG_PASSWORD;native.wrong.mac[0]=2;native.wrong.mac[5]=66;break;
            case 5:case 6:id=WIFI_EVENT_STA_BEACON_OFFSET_UNSTABLE;native.beacon.beacon_success_rate=scenario==5?87.5f:NAN;break;
            case 7:input=NULL;break;
            case 8:id=WIFI_EVENT_DPP_FAILED;input=(void *)1;break;
            case 9:id=WIFI_EVENT_STA_WPS_ER_SUCCESS;input=(void *)1;break;
            case 10:id=-777;input=(void *)1;break;
            case 11:id=WIFI_EVENT_STA_CONNECTED;native.connected.ssid_len=2;
                native.connected.ssid[0]=255;native.connected.ssid[1]=65;native.connected.bssid[0]=2;
                native.connected.channel=6;native.connected.aid=9;break;
            }
            sends=wakes=0;calls=0;inject=1;collect=0;fail_at=0;
            esp32_mquickjs_wifi_watch_capture(id,input,91);
            assert(!locked && sends==1 && wakes==1 && calls==0);
            assert(queued_event.control_generation==91 && queued_event.timestamp_us==456);
            if(scenario==1)assert(!queued_event.unsigned_values[1] && !queued_event.unsigned_values[2] && !queued_event.unsigned_values[3] && !queued_event.count);
            if(scenario==2 || scenario==3) {
                uint32_t cookie=0xdeadbeef;
                for(size_t i=0;i+sizeof(cookie)<=sizeof(queued_event);i++)assert(memcmp((char *)&queued_event+i,&cookie,sizeof(cookie)));
            }
            memset(&native,0,sizeof(native));queued_event.raw_requested=true;
            void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
            test_ctx=ctx;JSGCRef result_ref,data_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref);
            calls=0;fail_at=nth;collect=1;inject=1;
            *result=wifi_watch_to_js(ctx,&queued_event,NULL);
            if(nth==0){assert(!JS_IsException(*result));total=calls;}else assert(JS_IsException(*result));
            collect=0;inject=0;
            if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
            else {
                JSValue *data=JS_PushGCRef(ctx,&data_ref);*data=JS_GetPropertyStr(ctx,*result,"data");
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"rawEventData")));
                assert(JS_IsString(ctx,JS_GetPropertyStr(ctx,*result,"rawEventDataUnavailableReason")));
                if(scenario>=6 && scenario<=10)assert(JS_IsNull(*data));
                else {
                    assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"dataUnavailableReason")));
                    assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*data,"context")));
                    assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*data,"password")));
                    if(scenario==0)assert(number(ctx,*data,"rttRawNs")==UINT32_MAX && number(ctx,*data,"rttEstimatedNs")==0x80000000U && number(ctx,*data,"distanceCm")==123 && number(ctx,*data,"reportEntries")==255);
                    if(scenario==1)assert(JS_IsNull(JS_GetPropertyStr(ctx,*data,"rttRawNs")) && JS_IsNull(JS_GetPropertyStr(ctx,*data,"reportEntries")));
                    if(scenario==2)assert(number(ctx,*data,"interfaceId")==WIFI_IF_AP && number(ctx,*data,"operationId")==255 && number(ctx,*data,"channel")==177);
                    if(scenario==3)assert(number(ctx,*data,"operationId")==254 && number(ctx,*data,"channel")==36);
                    if(scenario==4)assert(JS_IsString(ctx,JS_GetPropertyStr(ctx,*data,"address")));
                    if(scenario==5){double value;assert(!JS_ToNumber(ctx,&value,JS_GetPropertyStr(ctx,*data,"beaconSuccessRate")) && value==87.5);}
                    if(scenario==11)assert(JS_IsNull(JS_GetPropertyStr(ctx,*data,"ssid")) && JS_IsArray(ctx,JS_GetPropertyStr(ctx,*data,"ssidBytes")));
                }
                JS_PopGCRef(ctx,&data_ref);
            }
            JS_PopGCRef(ctx,&result_ref);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
        }
    }
    uint64_t before=s_sequence;uint32_t dropped=atomic_load(&s_ingress_dropped);
    sends=wakes=0;lock_fail=true;esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_FTM_REPORT,NULL,0);
    assert(!sends && !wakes && s_sequence==before && atomic_load(&s_ingress_dropped)==dropped+1);
    lock_fail=false;queue_fail=true;esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_FTM_REPORT,NULL,0);
    assert(sends==1 && !wakes && s_sequence==before+1 && atomic_load(&s_ingress_dropped)==dropped+2 && !locked);
    atomic_store(&s_ingress_dropped,UINT32_MAX);esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_FTM_REPORT,NULL,0);
    assert(atomic_load(&s_ingress_dropped)==UINT32_MAX);
    queue_fail=false;s_sequence=WIFI_WATCH_MAX_SEQUENCE;before=sends;
    esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_FTM_REPORT,NULL,0);assert(sends==before && !locked);
    return 0;
}
