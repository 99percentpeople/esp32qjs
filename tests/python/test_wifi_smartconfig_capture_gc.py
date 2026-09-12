"""Deferred real MQuickJS option/credential/status conversion with GC/Nth OOM.

Production Session ownership, copy/commit and public converters are linked.
Injected producer bytes are not RF or full Future-core execution evidence.
"""
import re
import tempfile
import unittest
from test_wifi_smartconfig_session import COMPONENT, TYPES, headers, unit
from wireless_vm_fixture import CORE, build, extract, run


class SmartConfigCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        public = (COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig.c').read_text()
        native = (COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_session.c').read_text()
        decoder = (COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_decoder.c').read_text()
        code = TYPES + headers() + BOUNDARIES
        a = native.index('struct esp32_mquickjs_wifi_smartconfig_session {')
        b = native.index('static void sc_session_close_locked', a)
        code += native[a:b]
        code += extract(decoder, 'esp32_mquickjs_wifi_smartconfig_decoder_validate_options')
        for name in ('esp32_mquickjs_wifi_smartconfig_connection_validate_options',
                     'sc_session_close_locked', 'esp32_mquickjs_wifi_smartconfig_open_runtime',
                     'esp32_mquickjs_wifi_smartconfig_session_create', 'esp32_mquickjs_wifi_smartconfig_session_retain',
                     'esp32_mquickjs_wifi_smartconfig_session_release', 'esp32_mquickjs_wifi_smartconfig_session_close',
                     'esp32_mquickjs_wifi_smartconfig_session_status', 'esp32_mquickjs_wifi_smartconfig_session_credentials'):
            code += extract(native, name)
        code += re.search(r'typedef struct \{[^}]*\} sc_options_t;', public).group(0)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', public, re.M).group(0) + '\n'
        code += 'typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define SC_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        for name in ('sc_options', 'sc_identity', 'sc_status_to_js', 'sc_watch_to_js', 'sc_error', 'sc_bytes', 'sc_credentials_to_js'):
            code += extract(public, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_options_reject_invalid_inputs_and_clear_failed_key_copies(self):
        cases = [('({})', 1), ('({protocol:"esptouch-v2",aesKey:"1234567890abcdef"})', 1),
                 ('({autoConnect:true})', 1),
                 ('({autoConnect:true,minimumAuthMode:"wpa3-psk"})', 1),
                 ('({autoConnect:true,allowOpenNetwork:true})', 1),
                 ('({autoConnect:true,minimumAuthMode:"wpa3-psk",pmf:"optional"})', 0),
                 ('({autoConnect:true,allowOpenNetwork:true,pmf:"required"})', 0),
                 ('({autoConnect:true,connectionTimeoutMs:0})', 0),
                 ('({autoConnect:true,connectionTimeoutMs:3600001})', 0),
                 ('({autoConnect:true,minimumAuthMode:"open"})', 0),
                 ('({autoConnect:false,connectionTimeoutMs:10})', 0),
                 ('({allowOpenNetwork:false})', 0),
                 ('({protocol:"airkiss",aesKey:"1234567890abcdef"})', 0),
                 ('({protocol:"esptouch-v2",aesKey:"1234567890abcde\\u0000"})', 0),
                 ('({protocol:"esptouch-v2",aesKey:"short"})', 0),
                 ('({protocol:"esptouch-v2",aesKey:"1234567890abcdeé"})', 0),
                 ('({protocol:"bad"})', 0), ('({timeoutMs:0})', 0), ('({timeoutMs:3600001})', 0),
                 ('({timeoutMs:1.5})', 0), ('({timeoutMs:"20"})', 0), ('({fastMode:1})', 0),
                 ('({channelTimeoutSeconds:14})', 0), ('({channelTimeoutSeconds:256})', 0),
                 ('({allowApChannelChange:true,channelTimeoutSeconds:255,timeoutMs:3600000})', 1),
                 ('({unknown:1})', 0), ('null', 0), ('[]', 0),
                 ('({get protocol(){gc();return "esptouch-v2";},get aesKey(){gc();return "1234567890abcdef";}})', 1),
                 ('({get aesKey(){gc();throw new Error("sentinel");}})', 0)]
        for value, expected in cases:
            with self.subTest(value=value): run([str(self.binary), 'options', value, str(expected)])

    def test_credential_transfer_retry_exact_bytes_and_metadata_only_status(self):
        for mode in ('credentials', 'credentials-empty-custom', 'credentials-airkiss', 'status', 'observation', 'error', 'closed', 'consumed'):
            run([str(self.binary), mode, '', '1'])


BOUNDARIES = r'''
static bool session_locked;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!session_locked);session_locked=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(session_locked);session_locked=false;}while(0)
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
static const char *esp_err_to_name(int e){(void)e;return "injected";}
esp_err_t esp32_mquickjs_wifi_smartconfig_capture_owners(esp32_mquickjs_wifi_radio_lease_t owners[3]){memset(owners,0,3*sizeof(*owners));return ESP_OK;}
'''
MAIN = r'''
int main(int argc,char **argv){
 assert(argc==4);int total=1,expected=atoi(argv[3]);bool options_mode=!strcmp(argv[1],"options"),credential_mode=!strncmp(argv[1],"credentials",11);
 bool empty_custom=!strcmp(argv[1],"credentials-empty-custom"),airkiss=!strcmp(argv[1],"credentials-airkiss");
 for(int nth=0;nth<=total;nth++){
    void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    JSGCRef input_ref,result_ref,bytes_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref),*bytes=JS_PushGCRef(ctx,&bytes_ref);
    esp32_mquickjs_wifi_smartconfig_session_t *s=NULL;sc_options_t options;
    assert(!esp32_mquickjs_wifi_smartconfig_open_runtime());
    if(options_mode){*input=JS_Eval(ctx,argv[2],strlen(argv[2]),"sc-options",JS_EVAL_RETVAL);assert(!JS_IsException(*input));}
    else {
        esp32_mquickjs_wifi_smartconfig_decoder_options_t cfg={.type=SC_TYPE_ESPTOUCH,.channel_timeout_s=15};
        assert(!esp32_mquickjs_wifi_smartconfig_session_create(&cfg,false,1000,&s));
        s->status.credentials_ready=true;s->status.native.decoder.token.identity=UINT64_MAX;
        memset(s->credentials.network.ssid,0xff,32);memset(s->credentials.network.password,'P',64);
        s->credentials.network.type=SC_TYPE_ESPTOUCH_V2;s->credentials.network.bssid_set=true;memset(s->credentials.network.bssid,2,6);
        s->credentials.custom_length=64;for(unsigned i=0;i<64;i++)s->credentials.custom_data[i]=(uint8_t)i;
        s->credentials.custom_data[63]=0;
        if(empty_custom)s->credentials.custom_length=0;
        if(airkiss){s->credentials.network.type=SC_TYPE_AIRKISS;s->credentials.custom_length=0;}
        if(!strcmp(argv[1],"closed"))esp32_mquickjs_wifi_smartconfig_session_close(s,false);
        if(!strcmp(argv[1],"consumed"))assert(!esp32_mquickjs_wifi_smartconfig_session_credentials(s,NULL,true));
    }
    calls=0;fail_at=nth;inject=collect=true;bool ok;
    if(options_mode){ok=sc_options(ctx,&input_ref,&options);if(!nth){total=calls;assert(ok==expected);}}
    else if(!strcmp(argv[1],"status")){*result=sc_status_to_js(ctx,&s->status);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok);}}
    else if(!strcmp(argv[1],"observation")){sc_watch_event_t event={.sequence=9,.status=s->status};*result=sc_watch_to_js(ctx,&event,NULL);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok);}}
    else if(!strcmp(argv[1],"error")){*result=sc_error(ctx,"WiFiSmartConfigSession.receive",s,ESP_ERR_TIMEOUT,true);ok=!JS_IsException(*result);assert(!ok);if(!nth)total=calls;}
    else {*result=sc_credentials_to_js(ctx,s);ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok==credential_mode);}}
    inject=collect=false;
    if(!ok){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
    if(options_mode){
        if(ok){assert(options.timeout_ms && options.decoder.channel_timeout_s>=15);if(options.decoder.key_length)assert(options.decoder.key==options.key);}
        else {const uint8_t *p=(const uint8_t *)&options;for(size_t i=0;i<sizeof(options);i++)assert(!p[i]);}
        esp32_mquickjs_wireless_secure_zero(&options,sizeof(options));
    }
    if(credential_mode){
        if(!ok){assert(!s->status.credentials_consumed && s->credentials.network.password[0]=='P' && s->credentials.custom_length==((empty_custom || airkiss)?0:64) && s->credentials.custom_data[62]==62);*result=sc_credentials_to_js(ctx,s);assert(!JS_IsException(*result));}
        assert(s->status.credentials_consumed && !s->credentials.network.password[0]);
        *bytes=JS_GetPropertyStr(ctx,*result,"ssidBytes");double v;assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,31)) && v==255);
        *bytes=JS_GetPropertyStr(ctx,*result,"passwordBytes");assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,63)) && v=='P');
        *bytes=JS_GetPropertyStr(ctx,*result,"customDataBytes");
        if(airkiss)assert(JS_IsNull(*bytes));
        else if(empty_custom)assert(JS_IsUndefined(JS_GetPropertyUint32(ctx,*bytes,0)));
        else {
            assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,0)) && v==0);
            assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,62)) && v==62);
            assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,63)) && v==0);
            assert(JS_IsUndefined(JS_GetPropertyUint32(ctx,*bytes,64)));
        }
        assert(!s->credentials.custom_length);
        for(unsigned i=0;i<64;i++)assert(!s->credentials.custom_data[i]);
        assert(JS_IsException(sc_credentials_to_js(ctx,s)));assert(JS_HasException(ctx));(void)JS_GetException(ctx);
    }
    if(s && !strcmp(argv[1],"status") && ok){
        assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"passwordBytes")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"ssidBytes")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"customDataBytes")));
        *bytes=JS_GetPropertyStr(ctx,*result,"decoderIdentity");double v;assert(!JS_ToNumber(ctx,&v,JS_GetPropertyStr(ctx,*bytes,"high")) && v==4294967295.0);
    }
    if(s && !strcmp(argv[1],"observation") && ok){
        double v;assert(!JS_ToNumber(ctx,&v,JS_GetPropertyStr(ctx,*result,"sequence")) && v==9);
        *bytes=JS_GetPropertyStr(ctx,*result,"status");
        assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"customDataBytes")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"passwordBytes")));
        assert(!s->status.credentials_consumed && s->credentials.custom_length==64);
    }
    if(s){esp32_mquickjs_wifi_smartconfig_session_close(s,false);esp32_mquickjs_wifi_smartconfig_session_release(s);}
    assert(!s_sc_handles && !native_live && !session_locked);
    JS_PopGCRef(ctx,&bytes_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
    JS_FreeContext(ctx);free(heap);assert(!root_count);
 }
 return 0;
}
'''
