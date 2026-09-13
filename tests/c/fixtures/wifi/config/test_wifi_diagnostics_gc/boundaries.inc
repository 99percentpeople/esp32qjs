
typedef int esp32_mquickjs_runtime_t;
typedef struct {uint32_t open,dropped,queued,capacity,high_water;} esp32_mquickjs_event_queue_status_t;
static esp32_mquickjs_runtime_t runtime;
static int mode,provider_calls[14],queue_calls;static int64_t clock_us;
static int64_t esp_timer_get_time(void){return ++clock_us;}
static esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void){return mode==2?NULL:&runtime;}
static bool esp32_mquickjs_get_event_queue_status(void *r,esp32_mquickjs_event_queue_status_t *s){assert(r==&runtime);queue_calls++;*s=(esp32_mquickjs_event_queue_status_t){3,7,2,9,5};return true;}
static JSValue provider(JSContext *ctx,unsigned index){
    assert(index<14 && !provider_calls[index]++);
    JSGCRef ref;JSValue *r=JS_PushGCRef(ctx,&ref);*r=JS_NewObject(ctx);
    if(JS_IsException(*r)||!esp32_mquickjs_set_property_ref(ctx,r,"source",JS_NewUint32(ctx,index))||
       !esp32_mquickjs_set_property_ref(ctx,r,"marker",JS_NewString(ctx,"live"))){JS_PopGCRef(ctx,&ref);return JS_EXCEPTION;}
    return JS_PopGCRef(ctx,&ref);
}
#define METHOD(name, index) static JSValue name(JSContext *c,JSValue *s,int n,JSValue *a){assert(!s&&!n&&!a);return provider(c,index);}
#define HELPER(name, index) static JSValue name(JSContext *c){return provider(c,index);}
METHOD(js_sys_memory_manager,0)
HELPER(esp32_mquickjs_wifi_make_status_object,1)
HELPER(esp32_mquickjs_wifi_monitor_diagnostics,2)
HELPER(esp32_mquickjs_wifi_vendor_ie_status,3)
HELPER(esp32_mquickjs_wifi_csi_diagnostics,4)
METHOD(js_wifi_ftm_global_status,5)
METHOD(js_wifi_ftm_responder_offset_status,6)
METHOD(js_wifi_twt_status,7)
METHOD(js_wifi_enterprise_status,8)
METHOD(js_wifi_smartconfig_global_status,9)
METHOD(js_wifi_wps_global_status,10)
METHOD(js_wifi_wps_ap_global_status,11)
METHOD(js_wifi_dpp_global_status,12)
