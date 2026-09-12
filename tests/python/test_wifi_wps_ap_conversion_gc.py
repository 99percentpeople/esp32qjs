"""Deferred AP production converters + Session copy/commit in the real VM.

Inject GC and every JS allocation failure. Runtime/RF scheduling remains a
separate test obligation. Do not execute until concentrated Wi-Fi validation.
"""
import re
import tempfile
import unittest
from test_wifi_wps_ap_session import COMPONENT, TYPES, headers
from test_wifi_wps_capture_gc import BOUNDARIES
from wireless_vm_fixture import CORE, build, extract, run


class WpsAPConversionGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        public = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap.c').read_text()
        native = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_session.c').read_text()
        worker = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_worker.c').read_text()
        code = TYPES + headers() + BOUNDARIES
        a = native.index('struct esp32_mquickjs_wifi_wps_ap_session {')
        b = native.index('static void wps_session_close_locked', a)
        code += native[a:b]
        code += extract(worker, 'esp32_mquickjs_wifi_wps_config_valid')
        for name in ('wps_session_close_locked', 'esp32_mquickjs_wifi_wps_ap_open_runtime',
                     'esp32_mquickjs_wifi_wps_ap_session_create', 'esp32_mquickjs_wifi_wps_ap_session_retain',
                     'esp32_mquickjs_wifi_wps_ap_session_release', 'esp32_mquickjs_wifi_wps_ap_session_close',
                     'esp32_mquickjs_wifi_wps_ap_session_status', 'wps_session_copy',
                     'esp32_mquickjs_wifi_wps_ap_session_pin', 'esp32_mquickjs_wifi_wps_ap_session_registered'):
            code += extract(native, name)
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', public, re.M).group(0) + '\n'
        code += 'typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define WPS_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        for name in ('wps_identity', 'wps_status_to_js', 'wps_watch_to_js',
                     'wps_error', 'wps_pin_to_js', 'wps_registered_to_js'):
            code += extract(public, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_gc_and_every_js_allocation_failure_preserve_unconsumed_results(self):
        for mode in ('pin', 'registered', 'status', 'observation', 'error', 'closed', 'consumed'):
            with self.subTest(mode=mode): run([str(self.binary), mode])


MAIN = r'''
int main(int argc,char **argv){
 assert(argc==2);int total=1;
 bool pin_mode=!strcmp(argv[1],"pin"),registered_mode=!strcmp(argv[1],"registered");
 for(int nth=0;nth<=total;nth++){
    void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    JSGCRef result_ref,field_ref;
    JSValue *result=JS_PushGCRef(ctx,&result_ref),*field=JS_PushGCRef(ctx,&field_ref);
    esp32_mquickjs_wifi_wps_ap_session_t *s=NULL;
    assert(!esp32_mquickjs_wifi_wps_ap_open_runtime());
    esp_wps_config_t cfg={.wps_type=WPS_TYPE_PIN};
    assert(!esp32_mquickjs_wifi_wps_ap_session_create(&cfg,1000,&s));
    s->status.result_ready=s->status.pin_ready=true;s->status.native.worker.native.identity=UINT32_MAX;
    memcpy(s->pin,"12345670",8);memcpy(s->peer,"ABCDEF",6);
    if(!strcmp(argv[1],"closed"))esp32_mquickjs_wifi_wps_ap_session_close(s,false);
    if(!strcmp(argv[1],"consumed"))assert(!esp32_mquickjs_wifi_wps_ap_session_registered(s,NULL,true));
    calls=0;fail_at=nth;inject=collect=true;bool ok;
    if(!strcmp(argv[1],"status"))*result=wps_status_to_js(ctx,&s->status);
    else if(!strcmp(argv[1],"observation")){
        wps_watch_event_t event={.sequence=9,.status=s->status};*result=wps_watch_to_js(ctx,&event,NULL);
    }else if(!strcmp(argv[1],"error"))*result=wps_error(ctx,"WiFiWpsAPSession.receive",s,ESP_ERR_TIMEOUT,true);
    else *result=pin_mode?wps_pin_to_js(ctx,s):wps_registered_to_js(ctx,s);
    ok=!JS_IsException(*result);inject=collect=false;
    if(!nth){total=calls;assert(ok==(pin_mode || registered_mode || !strcmp(argv[1],"status") || !strcmp(argv[1],"observation")));}
    if(!ok){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
    if(pin_mode){
        if(!ok){assert(!s->status.pin_consumed && !memcmp(s->pin,"12345670",8));*result=wps_pin_to_js(ctx,s);assert(!JS_IsException(*result));}
        assert(s->status.pin_consumed && !s->status.pin_ready);for(unsigned i=0;i<8;i++)assert(!s->pin[i]);
        *field=JS_GetPropertyStr(ctx,*result,"pin");JSCStringBuf buf;size_t n;
        const char *pin=JS_ToCStringLen(ctx,&n,*field,&buf);assert(pin && n==8 && !memcmp(pin,"12345670",8));
    }
    if(registered_mode){
        if(!ok){assert(!s->status.result_consumed && !memcmp(s->peer,"ABCDEF",6));*result=wps_registered_to_js(ctx,s);assert(!JS_IsException(*result));}
        assert(s->status.result_consumed && !s->status.result_ready);for(unsigned i=0;i<6;i++)assert(!s->peer[i]);
        *field=JS_GetPropertyStr(ctx,*result,"mac");JSCStringBuf buf;size_t n;
        const char *mac=JS_ToCStringLen(ctx,&n,*field,&buf);assert(mac && n==17 && !memcmp(mac,"41:42:43:44:45:46",17));
        assert(JS_IsException(wps_registered_to_js(ctx,s)));(void)JS_GetException(ctx);
    }
    if(ok && (!strcmp(argv[1],"status") || !strcmp(argv[1],"observation"))){
        *field=!strcmp(argv[1],"status")?*result:JS_GetPropertyStr(ctx,*result,"status");
        assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*field,"pin")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*field,"password")));
        assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*field,"configRestored")));
        assert(!s->status.result_consumed && !s->status.pin_consumed);
    }
    esp32_mquickjs_wifi_wps_ap_session_close(s,false);esp32_mquickjs_wifi_wps_ap_session_release(s);
    assert(!s_wps_handles && !native_live && !session_locked);
    JS_PopGCRef(ctx,&field_ref);JS_PopGCRef(ctx,&result_ref);JS_FreeContext(ctx);free(heap);assert(!root_count);
 }
 return 0;
}
'''
