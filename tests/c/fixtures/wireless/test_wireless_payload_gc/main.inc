
int main(int argc,char **argv) {
    (void)argc;int mode=atoi(argv[1]),gc=atoi(argv[2]),invalid=atoi(argv[3]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        inject=false;collect=gc;calls=0;fail_at=nth;moved_roots=0;orphans=0;
        uint8_t bytes[64]={1,2,3,4};
        ble_scanner_t scanner={.generation=1,.capacity=1,.payloads=bytes};
        ble_subscription_t subscription=scanner;
        ble_gatt_server_t server={.event_capacity=1,.characteristic_count=1,.event_payloads=bytes,.characteristics={{"value"}}};
        espnow_rx_slot_t slot={.generation=1,.length=4,.sequence=2,.timestamp_us=INT64_C(1)<<45,.payload={1,2,3,4}};
        espnow_session_t session={.generation=1,.receive_capacity=1,.max_payload_bytes=64,.rx_slots=&slot};
        ble_scan_event_t scan_event={.generation=1,.length=4,.timestamp_us=INT64_C(1)<<45};
        ble_notification_event_t notify_event={.subscription_generation=1,.length=4,.timestamp_us=INT64_C(1)<<45};
        ble_server_event_t server_event={.adapter_generation=1,.kind=BLE_SERVER_EVENT_WRITE,.length=4,.timestamp_us=INT64_C(1)<<45};
        espnow_receive_event_t rx_event={.generation=1};
        s_ble.generation=s_ble.max_connections=1;
        s_ble.connections[0]=(ble_connection_slot_t){.generation=1,.open=true,.allocated=!invalid,.queue_ref={.val=JS_UNDEFINED}};
        esp32_mquickjs_native_pool_t *pool=mode==0 ? &scanner.free_slots : mode==1 ? &subscription.free_slots : mode==2 ? &server.free_slots : &session.rx_free;
        assert(esp32_mquickjs_native_pool_init(pool,1));uint16_t index;assert(esp32_mquickjs_native_pool_acquire(pool,&index));
        JSGCRef result_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref);
        inject=true;
        if(mode==0)*result=ble_scan_event_to_js(ctx,&scan_event,&scanner);
        if(mode==1)*result=ble_notification_event_to_js(ctx,&notify_event,&subscription);
        if(mode==2)*result=ble_server_event_to_js(ctx,&server_event,&server);
        if(mode==3)*result=espnow_receive_event_to_js(ctx,&rx_event,&session);
        inject=false;if(nth==0)total=calls;
        if(nth || invalid) {
            assert(JS_IsException(*result) && JS_HasException(ctx));(void)JS_GetException(ctx);
        } else {
            assert(!JS_IsException(*result) && !JS_HasException(ctx));
            if(gc)assert(moved_roots>0);
            JSValue data=JS_GetPropertyStr(ctx,*result,"data");const uint8_t *p;size_t n;
            assert(esp32_mquickjs_byte_view_acquire_read(ctx,data,"test",&p,&n));assert(n==4 && !memcmp(p,bytes,4));
            esp32_mquickjs_byte_view_release_read(ctx,data);
        }
        if(!esp32_mquickjs_native_pool_acquire(pool,&index)) {fprintf(stderr,"pool leaked mode=%d invalid=%d nth=%d\n",mode,invalid,nth);return 1;}
        assert(!esp32_mquickjs_native_pool_acquire(pool,&index));
        JS_PopGCRef(ctx,&result_ref);JS_GC(ctx);
        if(mode==2 && (nth || invalid) && orphans) {fprintf(stderr,"failed conversion closed adapter nth=%d\n",nth);return 1;}
        assert(root_count==0 && native_live==0);
        JS_FreeContext(ctx);free(heap);
    }
    printf("payload mode=%d gc=%d invalid=%d boundaries=%d\n",mode,gc,invalid,total);
}
