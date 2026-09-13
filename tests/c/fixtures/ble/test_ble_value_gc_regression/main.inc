
#undef JS_NewString
#undef JS_NewArray
#undef JS_NewObject
#undef JS_SetPropertyUint32
#undef JS_SetPropertyStr
#undef JS_PushGCRef
#undef JS_PopGCRef
#undef JS_NewInt64
int main(int argc,char **argv) {
    (void)argc;int mode=atoi(argv[1]);collect=atoi(argv[2])!=0;
    int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        calls=0;fail_at=nth;moved_roots=0;
        ble_connection_slot_t slot={.open=true,.central=true,.mtu=247,.generation=1,
            .security={true,true,true},.terminal_pending=true};
        ble_connection_event_t event={.adapter_generation=1,.connection_generation=1,
            .sequence=2,.timestamp_us=INT64_C(1)<<45,.expires_at_us=INT64_C(1)<<45,
            .mtu=247,.request_id=3,.passkey=123456};
        s_ble.generation=1;
        JSGCRef result_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref);
        switch(mode) {
        case 0:*result=ble_properties_to_js(ctx,127);break;
        case 1:*result=ble_address_to_js(ctx,&slot.peer);break;
        case 2:*result=ble_security_to_js(ctx,&slot.security,&slot.peer);break;
        case 3:*result=ble_connection_status_to_js(ctx,&slot,0);break;
        case 10: {
            s_ble.connections[0]=slot;
            ble_connection_slot_t *gatt=&s_ble.connections[0];
            gatt->service_count=gatt->characteristic_count=gatt->descriptor_count=1;
            gatt->discovery_generation=3;
            gatt->services[0]=(ble_remote_service_t){.start_handle=1,.end_handle=10,.characteristic_count=1};
            gatt->characteristics[0]=(ble_remote_characteristic_t){.declaration_handle=2,.value_handle=3,.properties=127,.descriptor_count=1};
            gatt->descriptors[0]=(ble_remote_descriptor_t){.handle=4};
            esp32_mquickjs_future_driver_state_t state={0};
            *result=ble_discover_finish(ctx,&state);break;
        }
        default:
            event.kind=mode<7 ? mode-3 : 4;
            event.pairing_action=mode-6;
            *result=ble_connection_event_to_js(ctx,&event,&slot);break;
        }
        assert(!external_roots);
        if(nth==0) {
            total=calls;
            if(JS_IsException(*result) || JS_HasException(ctx)) {
                fprintf(stderr,"normal conversion failed mode=%d gc=%d\n",mode,collect);return 1;
            }
            if(collect && mode!=2) assert(moved_roots>0);
            JS_GC(ctx);
            if(mode==0) {
                assert(JS_IsArray(ctx,*result));
                static const char *names[]={"broadcast","read","write","write-without-response","notify","indicate","authenticated-signed-write"};
                for(int i=0;i<7;i++) {
                    JSCStringBuf buf;JSValue value=JS_GetPropertyUint32(ctx,*result,i);
                    const char *text=JS_ToCString(ctx,value,&buf);assert(text && !strcmp(text,names[i]));
                }
            }
            if(mode==10) {
                int32_t generation;
                assert(!JS_ToInt32(ctx,&generation,JS_GetPropertyStr(ctx,*result,"generation")) && generation==3);
                static const char *keys[]={"services","characteristics","descriptors"};
                JSGCRef array_ref;JSValue *array=JS_PushGCRef(ctx,&array_ref);
                for(int i=0;i<3;i++) {
                    *array=JS_GetPropertyStr(ctx,*result,keys[i]);
                    assert(JS_IsArray(ctx,*array));
                    int32_t length;
                    assert(!JS_ToInt32(ctx,&length,JS_GetPropertyStr(ctx,*array,"length")) && length==1);
                    assert(JS_GetClassID(ctx,JS_GetPropertyUint32(ctx,*array,0))>=0);
                }
                JS_PopGCRef(ctx,&array_ref);
            }
            if(mode==4) assert(!slot.terminal_pending);
        } else {
            if(!JS_IsException(*result) || !JS_HasException(ctx)) {
                fprintf(stderr,"failure swallowed mode=%d nth=%d/%d gc=%d\n",mode,nth,total,collect);return 1;
            }
            if(mode==4) assert(slot.terminal_pending);
            (void)JS_GetException(ctx);
        }
        JS_PopGCRef(ctx,&result_ref);JS_GC(ctx);JS_FreeContext(ctx);free(heap);
    }
    printf("mode=%d checked %d allocation boundaries; gc=%d\n",mode,total,collect);
}
