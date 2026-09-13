
int main(int argc,char **argv) {
    assert(argc==4);int total=1,expected=atoi(argv[3]);
    bool capture=!strcmp(argv[1],"capture"),report=!strcmp(argv[1],"report"),status_mode=!strcmp(argv[1],"status");
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef root_ref,result_ref,list_ref,item_ref;
        JSValue *root=JS_PushGCRef(ctx,&root_ref),*result=JS_PushGCRef(ctx,&result_ref);
        JSValue *list=JS_PushGCRef(ctx,&list_ref),*item=JS_PushGCRef(ctx,&item_ref);
        esp32_mquickjs_wifi_rrm_request_t *request=NULL;
        if(capture){*root=JS_Eval(ctx,argv[2],strlen(argv[2]),"neighbor-options",JS_EVAL_RETVAL);assert(!JS_IsException(*root));}
        else {
            assert(esp32_mquickjs_wifi_rrm_create(4096,&request)==ESP_OK);
            static const uint8_t bytes[]={77,52,16,2,0,0,0,0,1,255,255,255,255,81,6,9,3,1,255};
            memcpy(request->report,bytes,sizeof(bytes));
            request->status.started=request->status.retired=true;
            request->status.terminal=ESP32_MQUICKJS_WIFI_RRM_REPORT;
            request->status.received_bytes=sizeof(bytes);
            request->status.operation=(esp32_mquickjs_wifi_radio_operation_t){.identity=7,.generation=9};
            if(!strcmp(argv[1],"truncated"))request->status.received_bytes--;
            if(!strcmp(argv[1],"duplicate-preference")) {
                request->report[2]+=3;request->report[19]=3;request->report[20]=1;request->report[21]=0;
                request->status.received_bytes+=3;
            }
            if(!strcmp(argv[1],"too-many")) {
                size_t n=1;for(unsigned i=0;i<65;i++){request->report[n++]=52;request->report[n++]=13;memcpy(request->report+n,bytes+3,13);n+=13;}
                request->status.received_bytes=n;
            }
        }
        esp32_mquickjs_wifi_rrm_status_t status={0};
        if(request)assert(esp32_mquickjs_wifi_rrm_status(request,&status));
        esp32_mquickjs_future_driver_state_t state={.request=request};
        calls=0;fail_at=nth;inject=collect=true;bool ok;
        if(capture) {
            uint32_t timeout,capacity;ok=neighbor_options(ctx,1,&root_ref,true,&timeout,&capacity);
            if(!nth){total=calls;assert(ok==expected);}
            if(ok)assert(timeout>=1 && timeout<=60000 && capacity>=1 && capacity<=4096);
        } else {
            *result=status_mode ? neighbor_snapshot(ctx,&status) : neighbor_report(ctx,&state,&status);
            ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok==(report||status_mode));}
        }
        inject=collect=false;
        if(!ok){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
        if(report) {
            if(!ok){*result=neighbor_report(ctx,&state,&status);assert(!JS_IsException(*result));}
            *list=JS_GetPropertyStr(ctx,*result,"neighbors");*item=JS_GetPropertyUint32(ctx,*list,0);
            double information;assert(!JS_ToNumber(ctx,&information,JS_GetPropertyStr(ctx,*item,"bssidInformation")));
            assert(information==4294967295.0 && request->report[0]==77 && request->status.retired);
        }
        if(request){esp32_mquickjs_wifi_rrm_close(request);esp32_mquickjs_wifi_rrm_release(request);}
        assert(!s_rrm_handles && !s_rrm_reserved_bytes && !native_live && !request_locked);
        JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&list_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&root_ref);
        JS_FreeContext(ctx);free(heap);assert(!root_count);
    }
    return 0;
}
