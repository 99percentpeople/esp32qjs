
int main(void) {
    vendor_watch_event_t event={.sequence=9007199254740991ULL,.timestamp_us=123,.generation=7,
        .rssi=-42,.length=257,.frame=4,.address={2,3,4,5,6,7},.bytes={221,255,1,2,3,4}};
    event.bytes[256]=255;
    for(int scenario=0;scenario<2;++scenario){
        int total=1;
        for(int nth=0;nth<=total;++nth){
            void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
            test_ctx=ctx;JSGCRef ref,data_ref;JSValue *result=JS_PushGCRef(ctx,&ref);
            calls=0;fail_at=nth;collect=inject=true;
            *result=scenario?esp32_mquickjs_wifi_vendor_ie_watch_status(ctx):vendor_watch_to_js(ctx,&event,NULL);
            if(nth==0){assert(!JS_IsException(*result));total=calls;}else assert(JS_IsException(*result));
            collect=inject=false;
            if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
            else if(!scenario){
                JSValue *data=JS_PushGCRef(ctx,&data_ref);*data=JS_GetPropertyStr(ctx,*result,"data");
                assert(JS_IsArray(ctx,*data));uint32_t value;
                assert(!JS_ToUint32(ctx,&value,JS_GetPropertyStr(ctx,*data,"length")) && value==257);
                assert(!JS_ToUint32(ctx,&value,JS_GetPropertyUint32(ctx,*data,256)) && value==255);
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"interface")));
                double sequence;assert(!JS_ToNumber(ctx,&sequence,JS_GetPropertyStr(ctx,*result,"sequence")) && sequence==9007199254740991.0);
                JS_PopGCRef(ctx,&data_ref);
            }
            uint8_t oui[3];assert(vendor_watch_oui(ctx,JS_NewString(ctx,"Aa:BB:cC"),oui) && oui[0]==170 && oui[2]==204);
            assert(!vendor_watch_oui(ctx,JS_NewString(ctx,"AA:BB:CC00"),oui));
            assert(!vendor_watch_oui(ctx,JS_NewString(ctx,"AA:BG:CC"),oui));
            JS_PopGCRef(ctx,&ref);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
        }
    }
    return 0;
}
