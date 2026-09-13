
#undef JS_NewString
#undef JS_NewArray
#undef JS_NewObject
#undef JS_SetPropertyUint32
#undef JS_SetPropertyStr
#undef JS_PushGCRef
#undef JS_PopGCRef
int main(int argc,char **argv) {
    (void)argc;
    collect=atoi(argv[1])==1;
    int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        if(atoi(argv[1])==2) {
            JSGCRef target_ref;
            JSValue *target=JS_PushGCRef(ctx,&target_ref);
            *target=JS_NewObject(ctx);
            JSValue invalid=JS_ThrowOutOfMemory(ctx);
            assert(!esp32_mquickjs_set_property(ctx,*target,"bad",invalid));
            assert(!esp32_mquickjs_set_property_ref(ctx,target,"bad",invalid));
            assert(!esp32_mquickjs_set_property_ref(ctx,&invalid,"bad",JS_TRUE));
            assert(!esp32_mquickjs_set_property(ctx,invalid,"bad",JS_TRUE));
            assert(calls==0 && JS_HasException(ctx));
            (void)JS_GetException(ctx);
            assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*target,"bad")));
            JS_PopGCRef(ctx,&target_ref);
            JS_FreeContext(ctx);free(heap);
            puts("Exception values rejected before allocation-capable API boundaries");
            return 0;
        }
        calls=0;fail_at=nth;
        s_ble.role_central=true;s_ble.role_peripheral=true;
        s_ble.lifecycle=BLE_LIFECYCLE_ACTIVE;s_ble.max_connections=2;
        s_ble.rejected_connection_error=17;
        strcpy(s_ble.device_name,"test");
        JSGCRef result_ref;
        JSValue *result=JS_PushGCRef(ctx,&result_ref);
        *result=ble_adapter_status_to_js(ctx);
        assert(external_roots==0);
        if(nth==0) {
            total=calls;
            if(collect) assert(moved_roots>0);
            assert(!JS_IsException(*result) && !JS_HasException(ctx));
            JS_GC(ctx);
            JSGCRef roles_ref;
            JSValue *roles=JS_PushGCRef(ctx,&roles_ref);
            *roles=JS_GetPropertyStr(ctx,*result,"roles");
            assert(JS_IsArray(ctx,*roles));
            for(uint32_t i=0;i<2;i++) {
                JSValue role=JS_GetPropertyUint32(ctx,*roles,i);
                JSCStringBuf buf;
                const char *str=JS_ToCString(ctx,role,&buf);
                assert(str && !strcmp(str,i ? "peripheral" : "central"));
            }
            JS_PopGCRef(ctx,&roles_ref);
            JSValue code=JS_GetPropertyStr(ctx,*result,"rejectedConnectionError");
            int32_t raw;assert(!JS_ToInt32(ctx,&raw,code) && raw==17);
        } else {
            if(!JS_IsException(*result) || !JS_HasException(ctx)) {
                fprintf(stderr,"status conversion swallowed allocation failure %d/%d\n",nth,total);
                return 1;
            }
            (void)JS_GetException(ctx);
        }
        JS_PopGCRef(ctx,&result_ref);
        JS_GC(ctx);
        JS_FreeContext(ctx);free(heap);
    }
    printf("Checked %d allocation-capable API boundaries; moving GC=%d\n",total,collect);
}
