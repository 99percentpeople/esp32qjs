
#undef js_object_keys
int main(int argc,char **argv) {
    (void)argc;int mode=atoi(argv[1]);collect=atoi(argv[2]);int invalid=atoi(argv[3]),total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;inject=false;
        memset(&s_ble,0,sizeof(s_ble));s_ble.generation=s_ble.max_connections=1;s_ble.preferred_mtu=247;s_ble.connections[0].generation=1;
        JSGCRef self,args[3];*JS_AddGCRef(ctx,&self)=JS_NewObject(ctx);
        for(int i=0;i<3;i++)*JS_AddGCRef(ctx,&args[i])=JS_UNDEFINED;
        args[0].val=JS_NewInt32(ctx,1);
        if(mode==0 || mode==1 || mode==2 || mode==5 || mode==6) {
            unsigned o=mode==0 ? 0 : mode==1 ? 1 : mode==2 ? 2 : 0;
            char hole[512];memset(hole,'h',sizeof(hole));assert(!JS_IsException(JS_NewStringLen(ctx,hole,sizeof(hole))));
            args[o].val=JS_NewObject(ctx);
            JS_SetPropertyStr(ctx,args[o].val,mode==0 ? "capacity" : "timeoutMs",JS_NewInt32(ctx,invalid ? -1 : mode==0 ? 2 : 1000));
        }
        if(mode==6)JS_SetPropertyStr(ctx,args[0].val,"includeDescriptors",JS_TRUE);
        if(mode==2) { args[1].val=JS_NewArray(ctx,4);for(unsigned i=0;i<4;i++)JS_SetPropertyUint32(ctx,args[1].val,i,JS_NewInt32(ctx,i+1)); }
        if(mode==3)args[0].val=JS_NewInt32(ctx,invalid ? 65536 : 247);
        calls=0;fail_at=nth;moved_roots=0;inject=true;
        esp32_mquickjs_future_driver_state_t *state=NULL;bool ok=false;
        if(mode==0)ok=ble_scan_capture(ctx,&self,1,args,&state);
        if(mode==1)ok=ble_gatt_read_handle_capture(ctx,&self,2,args,&state);
        if(mode==2)ok=ble_gatt_write_handle_capture(ctx,&self,3,args,&state);
        if(mode==3)ok=ble_exchange_mtu_capture(ctx,&self,1,args,&state);
        if(mode==4)ok=ble_scanner_close_capture(ctx,&self,0,args,&state);
        if(mode==5)ok=ble_connection_operation_capture(ctx,&self,1,args,&state,BLE_OP_CONNECTION_CLOSE,1000);
        if(mode==6)ok=ble_discover_capture(ctx,&self,1,args,&state);
        inject=false;if(!nth)total=calls;
        if(nth || invalid) { if(ok || !JS_HasException(ctx)) {fprintf(stderr,"capture failure mode=%d nth=%d invalid=%d ok=%d\n",mode,nth,invalid,ok);return 1;}assert(!state);(void)JS_GetException(ctx); }
        else { assert(ok && state && state->owner_rooted);if(collect && mode<=2)assert(moved_roots>0);ble_future_state_release(state); }
        if(mode==6)ble_clear_remote_discovery(&s_ble.connections[0]);
        if(mode==0) {
            if(!ok)assert(!s_ble.scanner.allocated);
            ble_release_event_queue(ctx,&s_ble.scanner.queue,&s_ble.scanner.queue_ref,&s_ble.scanner.queue_rooted);
            heap_caps_free(s_ble.scanner.payloads);
        }
        for(int i=2;i>=0;i--)JS_DeleteGCRef(ctx,&args[i]);JS_DeleteGCRef(ctx,&self);JS_GC(ctx);
        assert(root_count==0 && native_live==0);JS_FreeContext(ctx);free(heap);
    }
    printf("capture mode=%d gc=%d invalid=%d boundaries=%d\n",mode,collect,invalid,total);
}
