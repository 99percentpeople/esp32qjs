"""Deferred real VM EAP capture, GC, byte leases and Nth allocation failure.

Production options, ByteView, profile builder and secure-zero bodies. Only native
allocation/RTOS lock boundaries are injected. No SDK/Radio/authentication model.
"""
from pathlib import Path
import tempfile
import unittest
from test_wifi_rx_target import unit, INTERNAL
from wireless_vm_fixture import ROOT, CORE, build, extract, run


class WiFiEAPCaptureGC(unittest.TestCase):
    def test_getter_gc_allocation_failure_and_native_secret_cleanup(self):
        sdk = Path('/home/zach/esp/esp-idf/components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h')
        folder = ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise'
        base = '{methods:["peap"],username:"u",password:[1,0,2],caCertificate:view}'
        cases = [(base, True),
            ('{methods:["peap"],username:"ü",caCertificate:view}', True),
            ('{methods:["peap"],username:"u",defaultCertificateBundle:true}', True),
            ('{methods:["peap"],username:"u"}', False),
            (base.replace('["peap"]','["peap","peap"]'), False),
            (base.replace('["peap"]','["peap\\x00"]'), False),
            (base.replace('["peap"]','[]'), False),
            (base.replace('password:[1,0,2]', 'password:{length:4294967295}'), False),
            (base.replace('password:[1,0,2]', 'password:[1,256]'), False),
            (base.replace('password:[1,0,2]', 'password:[1,1.5]'), False),
            (base.replace('password:[1,0,2]', 'password:[1,undefined,2]'), False),
            (base.replace('password:[1,0,2]', 'password:null'), False),
            (base.replace('password:[1,0,2]', 'get password(){gc();return [1,0,2];}'), True),
            (base.replace('password:[1,0,2]', 'get password(){throw 12345;}'), False),
            (base.replace('password:[1,0,2]', 'password:{length:2,get 0(){gc();return 1;},get 1(){throw 12345;}}'), False),
            (base.replace('password:[1,0,2]', 'password:{length:1,get 0(){view.close();gc();return 1;}}'), False),
            (base.replace('password:[1,0,2]', 'password:{length:1,get 0(){this[1]=255;gc();return 1;}}'), True),
            (base.replace('username:"u"', 'get username(){view.close();return "u";}'), False),
            (base.replace('caCertificate:view', 'caCertificate:view,domain:"a\\x00b"'), False),
            (base.replace('caCertificate:view', 'caCertificate:view,checkCertificateTime:1'), False),
            (base.replace('caCertificate:view', 'caCertificate:view,okc:"true"'), False),
            (base.replace('caCertificate:view', 'caCertificate:view,fast:{}'), False),
            (base.replace('caCertificate:view', 'caCertificate:view,unknown:true'), False),
            ('{methods:["tls"],caCertificate:view,clientCertificate:view,privateKey:view,privateKeyPassword:"p\\x00q"}', False),
            ('(function(){var reads=0;return {methods:["peap"],username:"u",caCertificate:view,password:{get length(){if(++reads!==1)throw 12345;gc();return 1;},get 0(){gc();return 9;}}};})()', True),
        ]
        extra = PRELUDE + unit(sdk) + unit(INTERNAL / 'esp32_mquickjs_wifi_enterprise_profile.h')
        extra += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        extra += ALLOCATOR
        # Profile allocation reaches the heap through the managed allocator;
        # retirement now calls payload_free directly. Track that same boundary
        # so secret-zero verification runs before the actual native free.
        extra += '#define heap_caps_calloc eap_profile_alloc\n#define esp32_mquickjs_memory_payload_free eap_profile_free\n'
        extra += unit(folder / 'esp32_mquickjs_wifi_enterprise_profile.c')
        extra += '#undef heap_caps_calloc\n#undef esp32_mquickjs_memory_payload_free\n'
        extra += unit(CORE / 'esp32_mquickjs_options.c')
        extra += unit(folder / 'esp32_mquickjs_wifi_eap_options.c')
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, extra, MAIN)
            for expression, valid in cases:
                with self.subTest(expression=expression):
                    run([str(binary), '(' + expression + ')', str(int(valid)), str(int('12345' in expression and not valid))])


PRELUDE = r'''
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
#define CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT 1
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 1
#define MALLOC_CAP_8BIT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_NO_MEM 4
typedef int esp_err_t,portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool critical;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=false;}while(0)
'''
ALLOCATOR = r'''
static void *profile_allocation;
static size_t profile_allocation_bytes;
static unsigned profile_wipes;
static void *eap_profile_alloc(size_t n,size_t size,int caps){
    assert(!critical && !profile_allocation && n==1 && caps==MALLOC_CAP_8BIT);
    void *p=heap_caps_calloc(n,size,caps);
    if(p){profile_allocation=p;profile_allocation_bytes=size;}return p;
}
static void eap_profile_free(void *p){
    assert(!critical && p==profile_allocation);
    for(size_t i=0;i<profile_allocation_bytes;i++)assert(((uint8_t *)p)[i]==0);
    profile_wipes++;profile_allocation=NULL;profile_allocation_bytes=0;heap_caps_free(p);
}
'''
MAIN = r'''
int main(int argc,char **argv){
    assert(argc==4);bool expected=atoi(argv[2]),sentinel=atoi(argv[3]);int total=1;
    for(int nth=0;nth<=total;nth++){
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef view_ref,input_ref,error_ref;
        JSValue *view=JS_PushGCRef(ctx,&view_ref),*input=JS_PushGCRef(ctx,&input_ref),*error=JS_PushGCRef(ctx,&error_ref);
        uint8_t *data=heap_caps_malloc(4,1);data[0]=0x30;data[1]=2;data[2]=1;data[3]=0;
        *view=esp32_mquickjs_new_owned_byte_view(ctx,data,4);assert(!JS_IsException(*view));
        assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"view",*view)));
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"enterprise-capture",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        esp32_mquickjs_wifi_eap_profile_t *profile=NULL;
        calls=0;fail_at=nth;inject=collect=true;
        bool ok=esp32_mquickjs_wifi_eap_capture(ctx,*input,&profile);
        inject=collect=false;
        if(!nth){total=calls;assert(ok==expected);}else assert(!ok);
        if(!ok){assert(!profile && JS_HasException(ctx));*error=JS_GetException(ctx);if(!nth && sentinel)assert(*error==JS_NewInt32(ctx,12345));}
        else {
            esp32_mquickjs_wifi_eap_input_t native;
            assert(profile && esp32_mquickjs_wifi_eap_profile_view(profile,&native));
            assert(!native.policy.disable_time_check && !native.policy.okc);
            /* Native copy survives subsequent source close and a moving GC. */
            assert(!JS_IsException(js_byte_view_close(ctx,view,0,NULL)));JS_GC(ctx);
            if(native.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT].length)
                assert(native.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT].data[0]==0x30);
        }
        esp32_mquickjs_wifi_eap_profile_release(profile);
        assert(!profile_allocation && !s_eap_profile_counts.profiles && !s_eap_profile_counts.reserved_bytes);
        assert(!JS_IsException(js_byte_view_close(ctx,view,0,NULL)));
        JS_PopGCRef(ctx,&error_ref);JS_PopGCRef(ctx,&input_ref);JS_PopGCRef(ctx,&view_ref);
        JS_FreeContext(ctx);free(heap);assert(!root_count && !native_live && !critical);
    }
    return 0;
}
'''
