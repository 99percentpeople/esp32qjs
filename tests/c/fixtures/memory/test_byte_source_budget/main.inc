
static unsigned destroyed,inner_closed;
static bool fail_open,bad_iterator;
static void destroy(JSContext *ctx,void *p){(void)ctx;++destroyed;esp32_mquickjs_memory_payload_free(p);}
static bool next(JSContext *ctx,void *p,esp32_mquickjs_byte_span_t *out){(void)ctx;(void)p;(void)out;return false;}
static void close_inner(JSContext *ctx,void *p){(void)ctx;(void)p;++inner_closed;}
static bool open_inner(JSContext *ctx,JSValue value,void *p,esp32_mquickjs_byte_span_source_t *out,JSValue *error){
    (void)ctx;(void)value;if(fail_open){*error=JS_EXCEPTION;return false;}
    *out=(esp32_mquickjs_byte_span_source_t){.opaque=p,.next=bad_iterator?NULL:next,.close=close_inner};return true;
}
static const esp32_mquickjs_byte_span_source_object_ops_t ops={.open=open_inner,.destroy=destroy};
static void *pool(void){
    void *p=esp32_mquickjs_memory_wireless_alloc("wifi.csi",32,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
    assert(p);return p;
}
int main(void){
    esp32_mquickjs_memory_init();
    void *probe=pool();size_t overhead=status().wireless.regions[0].reserved-32;
    esp32_mquickjs_memory_payload_free(probe);empty();
    for(unsigned source=0;source<2;++source)for(unsigned failure=0;failure<4;++failure){
        JSContext ctx={0};void *payload=pool();unsigned before=destroyed;
        ctx.fail_object=failure==0;ctx.fail_property=failure==3;
        allocation_calls=0;fail_at=failure==1?1:failure==2?2:0;
        JSValue value=source
            ?esp32_mquickjs_new_wireless_byte_span_source("wifi.csi",&ctx,JS_TRUE,&ops,payload)
            :esp32_mquickjs_new_wireless_owned_byte_view("wifi.csi",&ctx,payload,32);
        if(!source && failure==3){
            assert(value!=JS_EXCEPTION);assert(js_byte_view_close(&ctx,&value,0,NULL)==JS_TRUE);
        }else assert(value==JS_EXCEPTION);
        if(source)assert(destroyed==before+1);
        fail_at=0;assert(!ctx.roots);empty();
    }
    {
        JSContext ctx={0};uint8_t *payload=pool();
        JSValue value=esp32_mquickjs_new_wireless_retained_byte_view("wifi.csi",&ctx,payload,32,
            esp32_mquickjs_memory_payload_free,payload);assert(value!=JS_EXCEPTION);
        assert(esp32_mquickjs_memory_wireless_retire(payload));
        size_t held_bytes=status().wireless.regions[0].reserved;
        assert(status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_RETIRED_POOL]>0);
        const uint8_t *data;size_t length;
        assert(esp32_mquickjs_byte_view_acquire_read(&ctx,value,"test",&data,&length));
        assert(data==payload && length==32);
        assert(js_byte_view_close(&ctx,&value,0,NULL)==JS_TRUE);
        assert(js_byte_view_close(&ctx,&value,0,NULL)==JS_TRUE);
        esp32_mquickjs_memory_release_generation();
        assert(status().wireless.regions[0].reserved==held_bytes);
        esp32_mquickjs_byte_view_release_read(&ctx,value);assert(!JS_GetOpaque(&ctx,value));empty();
    }
    {
        JSContext ctx={0};void *payload=pool();unsigned before=destroyed;
        JSValue value=esp32_mquickjs_new_wireless_byte_span_source("wifi.csi",&ctx,JS_UNDEFINED,&ops,payload);
        assert(value!=JS_EXCEPTION);size_t baseline=status().wireless.regions[0].reserved;
        esp32_mquickjs_byte_span_source_t stream;JSValue error;
        for(unsigned nth=1;nth<=2;++nth){
            allocation_calls=0;fail_at=nth;
            assert(!esp32_mquickjs_open_byte_span_source(&ctx,value,"test",&stream,&error));
            assert(!ctx.roots && status().wireless.regions[0].reserved==baseline);
        }
        fail_at=0;fail_open=true;
        assert(!esp32_mquickjs_open_byte_span_source(&ctx,value,"test",&stream,&error));
        fail_open=false;bad_iterator=true;
        assert(!esp32_mquickjs_open_byte_span_source(&ctx,value,"test",&stream,&error));
        bad_iterator=false;assert(inner_closed==1 && !ctx.roots);
        assert(status().wireless.regions[0].reserved==baseline);
        size_t data_bytes=baseline-
            status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL];
        /* Existing source handles use the control reserve, not the data
         * budget. Fill the latter while leaving room for the read lease. */
        void *fill=esp32_mquickjs_memory_wireless_alloc("fill",3584-data_bytes-overhead,
            ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);assert(fill);
        assert(!esp32_mquickjs_memory_wireless_alloc("copy",1,
            ESP32_MQUICKJS_MEMORY_DEFAULT,ESP32_MQUICKJS_MEMORY_BUDGET_COPY));
        assert(esp32_mquickjs_open_byte_span_source(&ctx,value,"test",&stream,&error));
        assert(ctx.roots==1 && status().wireless.regions[0].reserved>3584);
        assert(js_byte_span_source_close(&ctx,&value,0,NULL)==JS_EXCEPTION && destroyed==before);
        esp32_mquickjs_byte_span_source_close(&ctx,&stream);
        esp32_mquickjs_byte_span_source_close(&ctx,&stream);
        assert(inner_closed==2 && !ctx.roots);
        ctx.fail_property=true;
        assert(js_byte_span_source_close(&ctx,&value,0,NULL)==JS_EXCEPTION);
        assert(destroyed==before+1 && !JS_GetOpaque(&ctx,value));
        esp32_mquickjs_memory_payload_free(fill);empty();
    }
    {
        JSContext ctx={0};void *payload=pool();
        JSValue value=esp32_mquickjs_new_wireless_byte_span_source("wifi.csi",&ctx,JS_UNDEFINED,&ops,payload);
        assert(value!=JS_EXCEPTION);
        js_byte_span_source_finalizer(&ctx,JS_GetOpaque(&ctx,value));empty();
        value=esp32_mquickjs_new_owned_byte_view(&ctx,heap_caps_malloc(32,MALLOC_CAP_8BIT),32);
        assert(value!=JS_EXCEPTION && !status().wireless.regions[0].reserved);
        js_byte_view_finalizer(&ctx,JS_GetOpaque(&ctx,value));empty();
    }
    return 0;
}
