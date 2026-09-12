"""Deferred real-VM WPS options and PIN/credentials conversion, Nth OOM and GC.

Links production Session copy/commit with injected result bytes. Does not model
RF negotiation or prove full Future/event reaper scheduling. AST only until the
concentrated Wi-Fi validation phase.
"""
import re
import tempfile
import unittest
from test_wifi_wps_session import COMPONENT, TYPES, headers, unit
from wireless_vm_fixture import CORE, build, extract, run


class WpsCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        public = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps.c').read_text()
        native = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_session.c').read_text()
        worker = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_worker.c').read_text()
        code = TYPES + headers() + BOUNDARIES
        a = native.index('struct esp32_mquickjs_wifi_wps_session {')
        b = native.index('static void wps_session_close_locked', a)
        code += native[a:b]
        code += extract(worker, 'esp32_mquickjs_wifi_wps_config_valid')
        for name in ('wps_session_close_locked', 'esp32_mquickjs_wifi_wps_open_runtime',
                     'esp32_mquickjs_wifi_wps_session_create', 'esp32_mquickjs_wifi_wps_session_retain',
                     'esp32_mquickjs_wifi_wps_session_release', 'esp32_mquickjs_wifi_wps_session_close',
                     'esp32_mquickjs_wifi_wps_session_status', 'wps_session_copy',
                     'esp32_mquickjs_wifi_wps_session_pin', 'esp32_mquickjs_wifi_wps_session_credentials'):
            code += extract(native, name)
        code += re.search(r'typedef struct \{[^}]*\} wps_options_t;', public).group(0)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', public, re.M).group(0) + '\n'
        code += 'typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define WPS_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        for name in ('wps_string', 'wps_options_for_role', 'wps_options', 'wps_identity', 'wps_status_to_js', 'wps_watch_to_js',
                     'wps_error', 'wps_bytes', 'wps_pin_to_js', 'wps_credentials_to_js'):
            code += extract(public, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_options_validate_before_native_actions_and_scrub_on_failure(self):
        cases = [('({})', 1), ('({method:"pin"})', 1), ('({method:"pin",pin:"12345670"})', 1),
                 ('({method:"pin",pin:"00000000"})', 1), ('({pin:"12345670"})', 0),
                 ('({method:"pin",pin:"12345671"})', 0), ('({method:"pin",pin:"1234"})', 0),
                 ('({method:"pin",pin:"1234567\\u0000"})', 0), ('({method:"pin",pin:12345670})', 0),
                 ('({method:"registrar"})', 0), ('({timeoutMs:0})', 0), ('({timeoutMs:3600001})', 0),
                 ('({timeoutMs:1.5})', 0), ('({timeoutMs:"20"})', 0), ('({allowApChannelChange:1})', 0),
                 ('({allowApChannelChange:true,timeoutMs:3600000})', 1),
                 ('({device:{manufacturer:"%s%n",modelName:"",deviceName:"device"}})', 1),
                 ('({device:{manufacturer:"' + 'x' * 64 + '"}})', 1),
                 ('({device:{manufacturer:"' + 'x' * 65 + '"}})', 0),
                 ('({device:{modelName:"' + 'é' * 16 + '"}})', 1),
                 ('({device:{modelName:"' + 'é' * 17 + '"}})', 0),
                 ('({device:{modelNumber:"a\\u0000b"}})', 0), ('({device:{unknown:1}})', 0),
                 ('({device:null})', 0), ('({unknown:1})', 0), ('null', 0), ('[]', 0),
                 ('({get method(){gc();return "pin";},get pin(){gc();return "12345670";},get device(){gc();return {get deviceName(){gc();return "dev";}};}})', 1),
                 ('({method:"pin",pin:"12345670",get device(){gc();throw new Error("sentinel");}})', 0)]
        for value, expected in cases:
            with self.subTest(value=value): run([str(self.binary), 'options', value, str(expected)])

    def test_ap_options_reject_station_controls_and_share_pin_validation(self):
        for value, expected in [('({})', 1), ('({method:"pin",pin:"12345670"})', 1),
                                ('({allowApChannelChange:false})', 0),
                                ('({allowApChannelChange:true})', 0),
                                ('({method:"pin",pin:"12345671"})', 0),
                                ('({timeoutMs:1.5})', 0), ('({device:{unknown:1}})', 0),
                                ('({get method(){gc();return "pin";},get pin(){gc();return "12345670";}})', 1)]:
            with self.subTest(value=value): run([str(self.binary), 'ap-options', value, str(expected)])

    def test_copy_commit_survives_gc_and_allocation_failure(self):
        for mode in ('pin', 'credentials', 'status', 'observation', 'error', 'closed', 'consumed', 'bad-count', 'bad-length'):
            run([str(self.binary), mode, '', '1'])


BOUNDARIES = r'''
static bool session_locked;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!session_locked);session_locked=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(session_locked);session_locked=false;}while(0)
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
static const char *esp_err_to_name(int e){(void)e;return "injected";}
'''
MAIN = r'''
int main(int argc,char **argv){
 assert(argc==4);int total=1,expected=atoi(argv[3]);bool ap_options=!strcmp(argv[1],"ap-options");bool options_mode=ap_options || !strcmp(argv[1],"options");
 bool pin_mode=!strcmp(argv[1],"pin"),credential_mode=!strcmp(argv[1],"credentials");
 for(int nth=0;nth<=total;nth++){
    void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    JSGCRef input_ref,result_ref,bytes_ref,entry_ref;
    JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref),*bytes=JS_PushGCRef(ctx,&bytes_ref),*entry=JS_PushGCRef(ctx,&entry_ref);
    esp32_mquickjs_wifi_wps_session_t *s=NULL;wps_options_t options;
    assert(!esp32_mquickjs_wifi_wps_open_runtime());
    if(options_mode){*input=JS_Eval(ctx,argv[2],strlen(argv[2]),"wps-options",JS_EVAL_RETVAL);assert(!JS_IsException(*input));}
    else {
        esp_wps_config_t cfg={.wps_type=WPS_TYPE_PIN};
        assert(!esp32_mquickjs_wifi_wps_session_create(&cfg,false,1000,&s));
        s->status.credentials_ready=s->status.pin_ready=true;s->status.native.worker.native.identity=UINT64_MAX;
        memcpy(s->pin,"12345670",8);s->credentials.count=3;
        for(unsigned i=0;i<3;i++){
            esp32_mquickjs_wifi_wps_credential_t *c=&s->credentials.entries[i];
            memset(c->ssid,0xff,32);memset(c->password,'P'+i,64);c->ssid[1]=0;c->password[63]=0;
            c->ssid_length=32;c->password_length=64;c->auth_type=0x20;c->encryption_type=8;c->key_index=i;
            memset(c->mac,2+i,6);
        }
        if(!strcmp(argv[1],"bad-count"))s->credentials.count=4;
        if(!strcmp(argv[1],"bad-length"))s->credentials.entries[2].password_length=65;
        if(!strcmp(argv[1],"closed"))esp32_mquickjs_wifi_wps_session_close(s,false);
        if(!strcmp(argv[1],"consumed"))assert(!esp32_mquickjs_wifi_wps_session_credentials(s,NULL,true));
    }
    calls=0;fail_at=nth;inject=collect=true;bool ok;
    if(options_mode){ok=ap_options?wps_options_for_role(ctx,&input_ref,&options,true):wps_options(ctx,&input_ref,&options);if(!nth){total=calls;assert(ok==expected);}}
    else if(!strcmp(argv[1],"status")){*result=wps_status_to_js(ctx,&s->status);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok);}}
    else if(!strcmp(argv[1],"observation")){wps_watch_event_t event={.sequence=9,.status=s->status};*result=wps_watch_to_js(ctx,&event,NULL);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok);}}
    else if(!strcmp(argv[1],"error")){*result=wps_error(ctx,"WiFiWpsSession.receive",s,ESP_ERR_TIMEOUT,true);ok=!JS_IsException(*result);assert(!ok);if(!nth)total=calls;}
    else {*result=pin_mode?wps_pin_to_js(ctx,s):wps_credentials_to_js(ctx,s);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok==(pin_mode || credential_mode));}}
    inject=collect=false;
    if(!ok){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
    if(options_mode){
        if(ok)assert(esp32_mquickjs_wifi_wps_config_valid(&options.config) && options.timeout_ms);
        else {const uint8_t *p=(const uint8_t *)&options;for(size_t i=0;i<sizeof(options);i++)assert(!p[i]);}
        esp32_mquickjs_wireless_secure_zero(&options,sizeof(options));
    }
    if(pin_mode){
        if(!ok){assert(!s->status.pin_consumed && !memcmp(s->pin,"12345670",8));*result=wps_pin_to_js(ctx,s);assert(!JS_IsException(*result));}
        assert(s->status.pin_consumed && !s->status.pin_ready);for(unsigned i=0;i<8;i++)assert(!s->pin[i]);
        *bytes=JS_GetPropertyStr(ctx,*result,"pin");JSCStringBuf buf;size_t n;const char *pin=JS_ToCStringLen(ctx,&n,*bytes,&buf);assert(pin && n==8 && !memcmp(pin,"12345670",8));
    }
    if(credential_mode){
        if(!ok){assert(!s->status.credentials_consumed && s->credentials.entries[2].password[0]=='R');*result=wps_credentials_to_js(ctx,s);assert(!JS_IsException(*result));}
        assert(s->status.credentials_consumed);for(size_t i=0;i<sizeof(s->credentials);i++)assert(!((uint8_t*)&s->credentials)[i]);
        *bytes=JS_GetPropertyStr(ctx,*result,"credentials");assert(JS_IsUndefined(JS_GetPropertyUint32(ctx,*bytes,3)));
        *entry=JS_GetPropertyUint32(ctx,*bytes,2);*bytes=JS_GetPropertyStr(ctx,*entry,"ssidBytes");double v;
        assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,1)) && v==0);
        assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,31)) && v==255);
        *bytes=JS_GetPropertyStr(ctx,*entry,"passwordBytes");assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,63)) && v==0);
        assert(JS_IsUndefined(JS_GetPropertyUint32(ctx,*bytes,64)));
        assert(JS_IsException(wps_credentials_to_js(ctx,s)));(void)JS_GetException(ctx);
    }
    if(s && (!strcmp(argv[1],"bad-count") || !strcmp(argv[1],"bad-length")))assert(!s->status.credentials_consumed);
    if(s && !strcmp(argv[1],"status") && ok){
        assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"pin")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"credentials")));
        *bytes=JS_GetPropertyStr(ctx,*result,"nativeIdentity");double v;assert(!JS_ToNumber(ctx,&v,JS_GetPropertyStr(ctx,*bytes,"high")) && v==4294967295.0);
    }
    if(s && !strcmp(argv[1],"observation") && ok){
        *bytes=JS_GetPropertyStr(ctx,*result,"status");assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"pin")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"credentials")));
        assert(!s->status.credentials_consumed && !s->status.pin_consumed);
    }
    if(s){esp32_mquickjs_wifi_wps_session_close(s,false);esp32_mquickjs_wifi_wps_session_release(s);}
    assert(!s_wps_handles && !native_live && !session_locked);
    JS_PopGCRef(ctx,&entry_ref);JS_PopGCRef(ctx,&bytes_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
    JS_FreeContext(ctx);free(heap);assert(!root_count);
 }
 return 0;
}
'''
