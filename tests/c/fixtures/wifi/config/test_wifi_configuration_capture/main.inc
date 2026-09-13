
#undef JS_GetPropertyStr
#undef JS_GetPropertyUint32
int main(int argc,char **argv) {
    assert(argc==4);bool expected=atoi(argv[2]);const char *kind=argv[3];int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef root_ref;JSValue *root=JS_PushGCRef(ctx,&root_ref);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"configuration",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        esp32_mquickjs_wifi_configuration_t capture;memset(&capture,0x55,sizeof(capture));
        policy_failure=0;validator_error=0;force_required=false;calls=0;fail_at=nth;collect=true;inject=true;
        bool ok=esp32_mquickjs_wifi_capture_configuration(ctx,*root,&capture);
        if(!nth) {
            total=calls;assert(ok==expected);
            if(ok && !strcmp(kind,"omitted"))assert(!capture.mode_set && !capture.storage_set && !capture.start_set && !capture.allow_disconnect);
            if(ok && !strcmp(kind,"explicit"))assert(capture.mode_set && capture.mode==WIFI_MODE_STA && capture.storage_set && capture.storage==WIFI_STORAGE_RAM && capture.start_set && !capture.start && !capture.allow_disconnect);
            if(ok && !strcmp(kind,"combined")) {
                assert(capture.mode==WIFI_MODE_APSTA && capture.storage==WIFI_STORAGE_FLASH && capture.start && capture.allow_disconnect);
                assert(capture.station_set && capture.access_point_set && capture.station.sta.ssid[0]=='s');
                assert(capture.access_point.ap.ssid_len==3 && capture.access_point.ap.ssid[0]==0 && capture.access_point.ap.ssid[2]==255);
                assert(capture.controls.country_set && capture.controls.country_by_code && capture.controls.country.policy==WIFI_COUNTRY_POLICY_MANUAL);
                assert(capture.controls.protocol_bands[0]==3 && capture.controls.bandwidths[0].ghz_2g==WIFI_BW40 && capture.controls.bandwidths[0].ghz_5g==WIFI_BW20);
                assert(capture.start_controls.tx_power_set && capture.start_controls.tx_power_quarter_dbm==73);
                assert(capture.controls.power_save_set && capture.controls.power_save==WIFI_PS_NONE);
            }
            if(ok && !strcmp(kind,"country"))assert(!capture.controls.country_by_code && capture.controls.country.schan==1 && capture.controls.country.nchan==13 && capture.controls.country.cc[2]=='I');
        }
        if(!ok) {
            for(size_t i=0;i<sizeof(capture);i++)assert(((uint8_t *)&capture)[i]==0);
            assert(JS_HasException(ctx));JS_GetException(ctx);
        }
        esp32_mquickjs_wireless_secure_zero(&capture,sizeof(capture));
        inject=false;collect=false;JS_PopGCRef(ctx,&root_ref);JS_GC(ctx);
        assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
    }
    return 0;
}
