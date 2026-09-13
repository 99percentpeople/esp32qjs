
static struct {uint16_t report_len;uint8_t bytes[4097];} report;
static size_t add_neighbor(size_t offset,bool preference) {
    uint8_t *p=report.bytes+offset;
    p[0]=52;p[1]=preference?16:13;
    memset(p+2,0,13);p[2]=2;p[7]=66;
    memset(p+8,255,4);p[12]=115;p[13]=36;p[14]=9;
    if(preference){p[15]=3;p[16]=1;p[17]=255;}
    return offset+2+p[1];
}
static uint32_t number(JSContext *ctx,JSValue object,const char *key) {
    uint32_t n;assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,object,key)));return n;
}
static void capture(void) {
    sends=wakes=0;calls=0;inject=1;fail_at=0;collect=0;
    esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_STA_NEIGHBOR_REP,&report,91);
    assert(!critical && !locked && sends==1 && wakes==1+(s_sources[1]!=NULL) && calls==0);
    inject=0;
}
int main(void) {
    _Static_assert(offsetof(wifi_event_neighbor_report_t,n_report)==offsetof(__typeof__(report),bytes),"SDK report offset");
    inject=1;collect=0;calls=0;fail_at=1;
    assert(wifi_watch_neighbor_open()==ESP_ERR_NO_MEM && s_neighbor_state==NEIGHBOR_NONE && !native_live);
    inject=0;fail_at=0;
    for(int scenario=0;scenario<17;scenario++) {
        int total=1;
        for(int nth=0;nth<=total;nth++) {
            assert(wifi_watch_neighbor_open()==ESP_OK && s_neighbor_state==NEIGHBOR_OPEN);
            memset(&report,0,sizeof(report));report.bytes[0]=77;
            report.report_len=add_neighbor(1,true);
            const void *input=&report;
            wifi_watch_neighbor_slot_t *holds[2]={0};
            switch(scenario) {
            case 1:report.report_len=1;break;
            case 2:input=NULL;break;
            case 3:report.report_len=0;break;
            case 4:report.report_len=4097;break;
            case 5:report.report_len--;break;
            case 6:report.bytes[2]=12;report.report_len=15;break;
            case 7:report.bytes[17]=2;break; /* preference length over remaining span */
            case 8:report.bytes[2]+=3;report.bytes[19]=3;report.bytes[20]=1;report.bytes[21]=99;report.report_len=22;break;
            case 9:case 10: {
                size_t n=1;for(int i=0;i<(scenario==9?64:65);i++)n=add_neighbor(n,false);
                report.report_len=n;break;
            }
            case 11:report.bytes[1]=221;report.bytes[2]=2;report.bytes[3]=0xde;report.bytes[4]=0xad;report.report_len=5;break;
            case 12: {
                size_t n=1;
                while(n<4096){size_t len=4096-n-2;if(len>255)len=255;report.bytes[n]=221;report.bytes[n+1]=len;memset(report.bytes+n+2,0xa5,len);n+=len+2;}
                report.report_len=n;break;
            }
            case 13:holds[0]=wifi_watch_neighbor_acquire();holds[1]=wifi_watch_neighbor_acquire();assert(holds[0] && holds[1]);break;
            case 14:report.bytes[2]+=4;report.bytes[19]=221;report.bytes[20]=2;report.bytes[21]=0xde;report.bytes[22]=0xad;report.report_len=23;break;
            case 15:report.report_len=add_neighbor(1,false);break;
            case 16:report.bytes[2]++;report.bytes[19]=221;report.report_len=20;break;
            default:break;
            }
            sends=wakes=0;calls=0;collect=0;inject=1;fail_at=0;
            esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_STA_NEIGHBOR_REP,input,91);
            assert(sends==1 && wakes==1 && !calls && !locked && !critical);
            inject=0;
            bool valid=scenario<=2 || scenario==9 || scenario==11 || scenario==12 || scenario==14 || scenario==15;
            assert(queued_event.has_data==valid);
            if(queued_event.neighbor) {
                assert(queued_event.neighbor->refs==1 && s_neighbor_active==1);
                if(scenario==0 || scenario==14 || scenario==15) {
                    const wifi_watch_neighbor_t *v=&queued_event.neighbor->neighbors[0];
                    assert(v->bssid_information==UINT32_MAX && v->channel==36 && v->operating_class==115);
                    assert(v->has_preference==(scenario!=15));
                    assert(v->skipped_subelements==(scenario==14?1:0));
                }
            }
            wifi_watch_neighbor_release(holds[0]);wifi_watch_neighbor_release(holds[1]);
            memset(&report,0,sizeof(report)); /* SDK storage gone before JS work. */
            wifi_watch_neighbor_retire();
            if(queued_event.neighbor)assert(s_neighbor_state==NEIGHBOR_RETIRED && wifi_watch_neighbor_open()==ESP_ERR_INVALID_STATE);
            void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
            test_ctx=ctx;JSGCRef r,d,l,e;JSValue *result=JS_PushGCRef(ctx,&r);
            calls=0;fail_at=nth;collect=1;inject=1;
            *result=wifi_watch_to_js(ctx,&queued_event,NULL);
            if(!nth){assert(!JS_IsException(*result));total=calls;}else assert(JS_IsException(*result));
            collect=0;inject=0;
            assert(!queued_event.neighbor && !s_neighbor_active && s_neighbor_state==NEIGHBOR_NONE && !native_live);
            wifi_watch_event_drop(&queued_event,NULL); /* already consumed record */
            if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
            else {
                JSValue *data=JS_PushGCRef(ctx,&d);*data=JS_GetPropertyStr(ctx,*result,"data");
                if(!valid) {
                    assert(JS_IsNull(*data));
                    assert(JS_IsString(ctx,JS_GetPropertyStr(ctx,*result,"dataUnavailableReason")));
                    assert(queued_event.data_result==(scenario==4 || scenario==10?WATCH_DATA_TOO_LARGE:scenario==13?WATCH_DATA_CAPACITY:WATCH_DATA_INVALID));
                } else {
                    assert(JS_GetPropertyStr(ctx,*data,"received")== (scenario==2?JS_FALSE:JS_TRUE));
                    assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"dataUnavailableReason")));
                    if(scenario==2)assert(!number(ctx,*data,"reportLength") && JS_IsNull(JS_GetPropertyStr(ctx,*data,"dialogToken")));
                    else assert(number(ctx,*data,"dialogToken")==77);
                    JSValue *list=JS_PushGCRef(ctx,&l);*list=JS_GetPropertyStr(ctx,*data,"neighbors");
                    uint32_t count=scenario==9?64:(scenario==0 || scenario==14 || scenario==15?1:0);
                    assert(number(ctx,*list,"length")==count);
                    if(count) {
                        JSValue *entry=JS_PushGCRef(ctx,&e);*entry=JS_GetPropertyUint32(ctx,*list,0);
                        assert(number(ctx,*entry,"bssidInformation")==UINT32_MAX && number(ctx,*entry,"channel")==36);
                        if(scenario==9 || scenario==15)assert(JS_IsNull(JS_GetPropertyStr(ctx,*entry,"candidatePreference")));
                        else assert(number(ctx,*entry,"candidatePreference")==255);
                        assert(number(ctx,*entry,"skippedSubelements")== (scenario==14?1:0));
                        assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*entry,"rawEventData")));
                        JS_PopGCRef(ctx,&e);
                    }
                    if(scenario==11)assert(number(ctx,*data,"skippedElements")==1);
                    if(scenario==12)assert(number(ctx,*data,"reportLength")==4096 && number(ctx,*data,"skippedElements")==16);
                    JS_PopGCRef(ctx,&l);
                }
                JS_PopGCRef(ctx,&d);
            }
            JS_PopGCRef(ctx,&r);assert(!root_count);JS_FreeContext(ctx);free(heap);
        }
    }
    /* Actual production fanout keeps ingress until each attempted send owns a
     * separate reference. Rejected sends/filtering and close drain release it. */
    assert(wifi_watch_neighbor_open()==ESP_OK);
    report.bytes[0]=1;report.report_len=add_neighbor(1,true);
    capture();subscriber_fail=true;subscriber_sends=0;
    assert(wifi_watch_poll(NULL,source.runtime,NULL) && subscriber_sends==1 && !s_neighbor_active);
    subscriber_fail=false;subscriber_sends=0;
    wifi_watch_source_t other=source;s_sources[1]=&other;
    capture();assert(wifi_watch_poll(NULL,source.runtime,NULL) && subscriber_sends==2 && s_neighbor_active==1);
    assert(subscriber_events[0].neighbor==subscriber_events[1].neighbor && subscriber_events[0].neighbor->refs==2);
    wifi_watch_neighbor_retire();assert(wifi_watch_neighbor_open()==ESP_ERR_INVALID_STATE);
    wifi_watch_event_drop(&subscriber_events[0],NULL);assert(s_neighbor_active==1);
    wifi_watch_event_drop(&subscriber_events[1],NULL);assert(!s_neighbor_active && !native_live);
    s_sources[1]=NULL;
    assert(wifi_watch_neighbor_open()==ESP_OK);queue_fail=true;
    esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_STA_NEIGHBOR_REP,&report,0);assert(!s_neighbor_active);queue_fail=false;
    capture();source.after_sequence=queued_event.sequence;subscriber_sends=0;
    assert(wifi_watch_poll(NULL,source.runtime,NULL) && !subscriber_sends && !s_neighbor_active);source.after_sequence=0;
    source.all=false;source.mask=0;sends=0;
    esp32_mquickjs_wifi_watch_capture(WIFI_EVENT_STA_NEIGHBOR_REP,(void *)1,0);assert(!sends && !s_neighbor_active);
    source.all=true;wifi_watch_neighbor_retire();assert(!native_live && !critical && !locked);
    /* Last subscription close drains ingress, retires the pool, and leaves a
     * subscriber copy alive without referring to the freed source. */
    assert(wifi_watch_neighbor_open()==ESP_OK);
    wifi_watch_source_t *owned=heap_caps_malloc(sizeof(*owned),MALLOC_CAP_8BIT);assert(owned);
    *owned=source;s_sources[0]=owned;subscriber_sends=0;
    capture();assert(wifi_watch_poll(NULL,source.runtime,NULL));
    capture();assert(s_neighbor_active==2);
    wifi_watch_closed(owned);assert(!s_ingress && !s_sources[0] && !queued_ready);
    assert(s_neighbor_active==1 && s_neighbor_state==NEIGHBOR_RETIRED && native_live==1);
    assert(wifi_watch_neighbor_open()==ESP_ERR_INVALID_STATE);
    wifi_watch_event_drop(&subscriber_events[0],NULL);
    assert(!native_live && !s_neighbor_active && s_neighbor_state==NEIGHBOR_NONE);
    return 0;
}
