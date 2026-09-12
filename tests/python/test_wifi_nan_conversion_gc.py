"""Deferred real-VM NAN options/status/errors: production code, moving GC, Nth OOM.

Native readiness is supplied at the status boundary; this does not simulate
NAN protocol completion or replace the production Session scheduling fixture.
"""
import re
import tempfile
import unittest
from test_wifi_nan_session import BASE, PREFIX, without_includes
from wireless_vm_fixture import CORE, build, extract, run


class NanConversionGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        code = PREFIX + '\n#include <math.h>\n#include "mquickjs_priv.h"\n'
        for name in ('esp32_mquickjs_wifi_nan_sdk.h', 'esp32_mquickjs_wifi_nan_ndp.h', 'esp32_mquickjs_wifi_nan_tx.h', 'esp32_mquickjs_wifi_nan_radio.h',
                     'esp32_mquickjs_wifi_nan_session.h', 'esp32_mquickjs_wifi_nan_discovery.h', 'esp32_mquickjs_wifi_nan_message.h', 'esp32_mquickjs_wifi_nan_path.h'):
            code += without_includes(BASE / 'internal' / name)
        code += r'''
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
static const char *esp_err_to_name(int e){(void)e;return "injected";}
void esp32_mquickjs_wifi_nan_tx_status(esp32_mquickjs_wifi_nan_tx_status_t *s){memset(s,0,sizeof(*s));}
void esp32_mquickjs_wifi_nan_sdk_observer_status(esp32_mquickjs_wifi_nan_sdk_observer_status_t *s){memset(s,0,sizeof(*s));}
'''
        native = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_session.c').read_text()
        start = native.index('struct esp32_mquickjs_wifi_nan_session {')
        code += native[start:native.index('static void nan_message_worker', start)]
        for name in ('nan_session_close_locked', 'esp32_mquickjs_wifi_nan_open_runtime',
                     'esp32_mquickjs_wifi_nan_global_status', 'nan_session_create',
                     'esp32_mquickjs_wifi_nan_session_create',
                     'esp32_mquickjs_wifi_nan_session_release', 'esp32_mquickjs_wifi_nan_session_status'):
            code += extract(native, name)
        code += without_includes(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        public = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan.c').read_text()
        code += re.search(r'^#define SET\(.*$', public, re.M).group(0) + '\n'
        for name in ('nan_open_options', 'nan_status_to_js', 'nan_error', 'nan_tx_status_to_js',
                     'js_wifi_nan_global_status', 'js_wifi_nan_capabilities'):
            code += extract(public, name)
        service_public = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_service_public.inc').read_text()
        for name in ('nan_service_status_to_js', 'nan_event_bytes', 'nan_service_event_to_js'):
            code += extract(service_public, name)
        message_public = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_message_public.inc').read_text()
        code += extract(message_public, 'nan_message_result_to_js')
        path_public = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_path_public.inc').read_text()
        code += extract(path_public, 'nan_path_status_to_js')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_strict_options_and_getter_gc_before_native_admission(self):
        cases = [('undefined', 1), ('({})', 1), ('null', 0), ('[]', 0),
                 ('({mode:"unsynchronized"})', 1), ('({mode:"unsynchronized",channel:6})', 0),
                 ('({mode:"unknown"})', 0), ('({mode:"unsynchronized",get timeoutMs(){gc();return 1000;}})', 1),
                 ('({channel:0})', 0), ('({channel:256})', 0), ('({channel:149})', 1),
                 ('({channel:"6"})', 0), ('({channel:NaN})', 0), ('({channel:Infinity})', 0),
                 ('({channel:6.5})', 0), ('({masterPreference:256})', 0), ('({scanTimeSeconds:-1})', 0),
                 ('({warmUpSeconds:65536})', 0), ('({timeoutMs:0})', 0), ('({timeoutMs:120001})', 0),
                 ('({randomizeMac:1})', 0), ('({resetCurrentNvsCreds:true})', 0),
                 ('({channel:255,masterPreference:255,scanTimeSeconds:255,warmUpSeconds:65535,timeoutMs:120000,randomizeMac:false})', 1),
                 ('({get channel(){gc();return 6;},get timeoutMs(){gc();return 1000;}})', 1),
                 ('({get channel(){gc();throw new Error("sentinel");}})', 0)]
        for value, expected in cases:
            with self.subTest(value=value):
                run([str(self.binary), 'options', value, str(expected)])

    def test_status_capability_and_error_conversion_gc_and_nth_allocation(self):
        for mode in ('status', 'global', 'capabilities', 'error', 'service-status', 'service-event', 'message-result', 'path-status'):
            with self.subTest(mode=mode):
                run([str(self.binary), mode, 'undefined', '1'])


MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==4);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,result_ref,field_ref;
        JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref),*field=JS_PushGCRef(ctx,&field_ref);
        *input=JS_Eval(ctx,argv[2],strlen(argv[2]),"case",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        assert(!esp32_mquickjs_wifi_nan_open_runtime());
        wifi_nan_sync_config_t config={.op_channel=6,.master_pref=2,.scan_time=3,.warm_up_sec=5};uint32_t timeout=10000;
        esp32_mquickjs_wifi_nan_session_t *s=NULL;bool option_mode=!strcmp(argv[1],"options");
        esp32_mquickjs_wifi_nan_discovery_status_t service_status={.identity=UINT32_MAX,.session_identity=1,.native_id=7,.ready=true};
        esp32_mquickjs_wifi_nan_discovery_event_t event={.identity=42,.native_id=7,.peer_id=8,.kind=3,.ssi_len=2048};
        memset(event.ssi,0xa5,sizeof(event.ssi));
        esp32_mquickjs_wifi_nan_path_status_t path={.identity=UINT32_MAX,.service_identity=42,.session_identity=1,
            .ssi_len=512,.ready=true,.native={.identity=UINT32_MAX,.ndp_id=255,.accepted=true}};
        memset(path.ssi,0x5a,sizeof(path.ssi));
        esp32_mquickjs_wifi_nan_message_status_t message={.identity=UINT32_MAX,.service_identity=42,.bytes=2048,
            .service_id=7,.peer_id=3,.submitted=true,.done=true,.tx={.tx_done=true,.tx_succeeded=true}};
        if(!option_mode) {
            assert(!esp32_mquickjs_wifi_nan_session_create(&config,timeout,&s));
            s->status.ready=true;s->status.native.operation.identity=UINT32_MAX;
            s->status.stage="injected-stage";s->status.cleanup_error=ESP_ERR_TIMEOUT;s->status.cleanup_stage="nan-stop-events";
            if(!strcmp(argv[1],"global"))s_nan_active=s;
        }
        calls=0;fail_at=nth;inject=collect=true;bool ok;
        bool usd=false;
        if(option_mode)ok=nan_open_options(ctx,&input_ref,&config,&timeout,&usd);
        else {
            if(!strcmp(argv[1],"status"))*result=nan_status_to_js(ctx,&s->status);
            else if(!strcmp(argv[1],"global"))*result=js_wifi_nan_global_status(ctx,NULL,0,NULL);
            else if(!strcmp(argv[1],"capabilities"))*result=js_wifi_nan_capabilities(ctx,NULL,0,NULL);
            else if(!strcmp(argv[1],"message-result"))*result=nan_message_result_to_js(ctx,&message);
            else if(!strcmp(argv[1],"path-status"))*result=nan_path_status_to_js(ctx,&path);
            else if(!strcmp(argv[1],"service-status")){
                *result=nan_service_status_to_js(ctx,&service_status);
            } else if(!strcmp(argv[1],"service-event")){
                *result=nan_service_event_to_js(ctx,&event,NULL);
            }
            else *result=nan_error(ctx,"WiFiNanSession.ready",s,ESP_ERR_TIMEOUT,true);
            ok=!JS_IsException(*result);
        }
        if(!nth){total=calls;assert(ok==(option_mode?atoi(argv[3]):strcmp(argv[1],"error")!=0));}
        inject=collect=false;
        if(!ok){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
        if(option_mode){assert(!s_nan_active&&!s_nan_handles&&!native_live);}
        else {
            assert(s->status.ready&&!s->status.closing&&s->references==1);
            if(!ok&&strcmp(argv[1],"error")){
                if(!strcmp(argv[1],"service-status"))*result=nan_service_status_to_js(ctx,&service_status);
                else if(!strcmp(argv[1],"service-event"))*result=nan_service_event_to_js(ctx,&event,NULL);
                else if(!strcmp(argv[1],"message-result"))*result=nan_message_result_to_js(ctx,&message);
                else if(!strcmp(argv[1],"path-status"))*result=nan_path_status_to_js(ctx,&path);
                else *result=nan_status_to_js(ctx,&s->status);
                assert(!JS_IsException(*result));
            }
            if(!strcmp(argv[1],"service-status")){
                *field=JS_GetPropertyStr(ctx,*result,"identity");double n;assert(!JS_ToNumber(ctx,&n,*field)&&n==4294967295.0);
                assert(service_status.ready&&service_status.native_id==7);
            }
            if(!strcmp(argv[1],"message-result")){
                *field=JS_GetPropertyStr(ctx,*result,"identity");double n;assert(!JS_ToNumber(ctx,&n,*field)&&n==4294967295.0);
                assert(JS_GetPropertyStr(ctx,*result,"txSucceeded")==JS_TRUE);
                assert(JS_GetPropertyStr(ctx,*result,"bufferRetired")==JS_FALSE);
                assert(message.done&&message.tx.tx_succeeded&&!message.tx.buffer_retired);
            }
            if(!strcmp(argv[1],"path-status")){
                *field=JS_GetPropertyStr(ctx,*result,"identity");double n;assert(!JS_ToNumber(ctx,&n,*field)&&n==4294967295.0);
                *field=JS_GetPropertyStr(ctx,*result,"ssi");assert(JS_IsArray(ctx,*field));
                JSValue value=JS_GetPropertyStr(ctx,*field,"length");assert(!JS_ToNumber(ctx,&n,value)&&n==512);
                value=JS_GetPropertyUint32(ctx,*field,511);assert(!JS_ToNumber(ctx,&n,value)&&n==0x5a);
                assert(path.ready&&path.ssi_len==512&&path.ssi[511]==0x5a&&path.native.ndp_id==255);
            }
            if(!strcmp(argv[1],"service-event")){
                *field=JS_GetPropertyStr(ctx,*result,"ssi");assert(JS_IsArray(ctx,*field));
                JSValue value=JS_GetPropertyStr(ctx,*field,"length");double n;assert(!JS_ToNumber(ctx,&n,value)&&n==2048);
                value=JS_GetPropertyUint32(ctx,*field,0);assert(!JS_ToNumber(ctx,&n,value)&&n==0xa5);
                value=JS_GetPropertyUint32(ctx,*field,2047);assert(!JS_ToNumber(ctx,&n,value)&&n==0xa5);
                assert(event.ssi_len==2048&&event.ssi[0]==0xa5&&event.ssi[2047]==0xa5);
            }
            if(!strcmp(argv[1],"status")){
                *field=JS_GetPropertyStr(ctx,*result,"operation");double n;assert(!JS_ToNumber(ctx,&n,*field)&&n==4294967295.0);
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"credentials")));
            }
            s_nan_active=NULL;esp32_mquickjs_wifi_nan_session_release(s);
        }
        assert(!s_nan_handles&&!native_live&&!lock_depth);
        JS_PopGCRef(ctx,&field_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
        JS_FreeContext(ctx);free(heap);assert(!root_count);
    }
}
'''
