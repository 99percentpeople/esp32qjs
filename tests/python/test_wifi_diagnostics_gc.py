"""Actual diagnostics composition in MQuickJS, moving GC and Nth OOM; deferred."""
from pathlib import Path
import re
import tempfile
import unittest
from wireless_vm_fixture import ROOT, build, extract, run


class WiFiDiagnosticsGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_diagnostics.c').read_text()
        cls.binaries = []
        for enabled in (0, 1):
            defines = ''.join('#define ' + key + ' ' + str(enabled) + '\n' for key in (
                'CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI', 'CONFIG_ESP_WIFI_FTM_ENABLE',
                'CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT', 'CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT',
                'CONFIG_ESP_WIFI_SOFTAP_SUPPORT', 'CONFIG_SOC_WIFI_HE_SUPPORT', 'CONFIG_IDF_TARGET_ESP32C5',
                'CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT', 'CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API',
                'CONFIG_LWIP_IPV4', 'CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR', 'CONFIG_ESP_WIFI_DPP_SUPPORT'))
            folder = cls.temp.name + '/' + str(enabled)
            Path(folder).mkdir()
            reset_type = re.search(r'typedef struct \{[^}]*\} wifi_diagnostics_reset_t;', source).group(0)
            code = defines + BOUNDARIES + reset_type + '\nstatic wifi_diagnostics_reset_t s_diagnostics_reset;\n'
            code += extract(source, 'wifi_diagnostics_reset_to_js') + extract(source, 'js_wifi_diagnostics_snapshot')
            cls.binaries.append(build(folder, code, MAIN))

    def test_nested_results_survive_collection_and_failure_never_repeats_a_provider(self):
        for enabled, binary in enumerate(self.binaries):
            for mode in (0, 1, 2):
                run([str(binary), str(enabled), str(mode)])


BOUNDARIES = r'''
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
'''
MAIN = r'''
int main(int argc,char **argv){
    assert(argc==3);int enabled=atoi(argv[1]);mode=atoi(argv[2]);int total=0;
    const char *names[]={"memory","wifi","monitor","vendorIe","csi","ftmInitiator","ftmResponder","twt","enterprise","smartConfig","wpsStation","wpsAccessPoint","dpp"};
    for(int nth=0;nth<=total;nth++){
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        memset(provider_calls,0,sizeof(provider_calls));queue_calls=0;clock_us=0;
        JSGCRef result_ref,item_ref;JSValue *r=JS_PushGCRef(ctx,&result_ref),*item=JS_PushGCRef(ctx,&item_ref);
        inject=true;collect=true;calls=0;fail_at=nth;
        *r=js_wifi_diagnostics_snapshot(ctx,NULL,mode==1?1:0,NULL);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*r)==(mode!=0));}
        if(JS_IsException(*r)){assert(JS_HasException(ctx));JS_GetException(ctx);}
        else {
            assert(queue_calls==1);
            for(unsigned i=0;i<13;i++){
                *item=JS_GetPropertyStr(ctx,*r,names[i]);
                if(i>=4&&!enabled){assert(JS_IsNull(*item)&&!provider_calls[i]);continue;}
                assert(provider_calls[i]==1 && JS_GetClassID(ctx,*item)==JS_CLASS_OBJECT);
                uint32_t n;assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*item,"source"))&&n==i);
                JSCStringBuf b;const char *text=JS_ToCString(ctx,JS_GetPropertyStr(ctx,*item,"marker"),&b);assert(text&&!strcmp(text,"live"));
            }
            *item=JS_GetPropertyStr(ctx,*r,"runtimeQueues");uint32_t n;
            assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*item,"dropped"))&&n==7);
            assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*item,"highWater"))&&n==5);
            *item=JS_GetPropertyStr(ctx,*r,"counterReset");
            assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*item,"count"))&&n==0);
            assert(JS_IsNull(JS_GetPropertyStr(ctx,*item,"startedUs")));
            assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*r,"finishedUs"))&&n==2);
        }
        if(mode){assert(!queue_calls);for(unsigned i=0;i<14;i++)assert(!provider_calls[i]);}
        JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&result_ref);assert(!root_count&&!native_live);
        JS_FreeContext(ctx);free(heap);
    }
}
'''
