"""Production SoftAP parser, result conversion and native cleanup boundaries."""
import pathlib
import tempfile
import unittest
from wireless_vm_fixture import ROOT, CORE, build, extract, run
from test_wireless_control_regression import compile_run
AP=ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_ap.c'

class WiFiAPCleanup(unittest.TestCase):
    def test_cleanup_retains_exclusion_and_retries_only_remaining_resources(self):
        body=extract(AP.read_text(),'wifi_ap_retire_netif')+extract(AP.read_text(),'wifi_ap_cleanup')
        compile_run(self,r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
static void *s_ap_netif;
static struct { bool acquired; } s_ap_lease;
static struct { unsigned identity; } s_ap_lifecycle, s_ap_coordinator;
static bool s_ap_cleanup_pending;
static bool deauth_pending;
static struct {unsigned identity;} s_ap_raw_owner;
static bool esp32_mquickjs_wifi_radio_ap_deauth_poll(bool *pending) { *pending=deauth_pending;return false; }
static int s_ap_detach_error;
static bool esp32_mquickjs_wifi_configuration_pending(void) { return false; }
/* Independent AP/configuration scenarios; shared removal has its own fixture. */
static bool esp32_mquickjs_wifi_ap_stop_pending(void) { return false; }
static int esp32_mquickjs_wifi_stop_ap_shared(bool *handled) { *handled=false;return ESP_OK; }
static int esp32_mquickjs_wifi_cleanup_ap_configuration(void) { assert(0);return -1; }
#define ESP_ERR_INVALID_STATE -2
static const char *s_ap_stage;
static int failed,calls[6],freed,releases;
static int boundary(int n) { calls[n]++;return failed==n ? -n:0; }
static int esp32_mquickjs_wifi_radio_begin_lifecycle(const void *a,const void *s,const void *ap,void *t) { assert(!a && !s && ap && t);int e=boundary(1);if(!e)s_ap_lifecycle.identity=1;return e; }
static void esp32_mquickjs_wifi_radio_release(void *l) { assert(l);s_ap_lease.acquired=false;releases++; }
static int esp32_mquickjs_wifi_radio_quiesce_lifecycle(void *t) { assert(t && s_ap_lifecycle.identity);return boundary(2); }
static int esp_wifi_clear_default_wifi_driver_and_handlers(void *n) { assert(n && s_ap_lifecycle.identity);return boundary(3); }
static void esp_netif_destroy(void *n) { assert(n && s_ap_lifecycle.identity);freed++; }
/* SDK-facing retirement boundary; its scheduler is covered separately by
 * test_wifi_netif_retirement using the complete production translation unit. */
static int esp32_mquickjs_wifi_netif_retire(void **n,int *error) {
    if(*error)return *error;
    if(!*n)return ESP_OK;
    *error=esp_wifi_clear_default_wifi_driver_and_handlers(*n);
    if(!*error){esp_netif_destroy(*n);*n=NULL;}
    return *error;
}
static int esp32_mquickjs_wifi_radio_finish_lifecycle(void *t,bool shutdown) { assert(t && shutdown && !s_ap_netif);int e=boundary(4);if(!e)s_ap_lifecycle.identity=0;return e; }
''' +body+r'''
int main(void) {
    deauth_pending=true;s_ap_netif=(void *)1;s_ap_lease.acquired=true;
    assert(wifi_ap_cleanup()==ESP_ERR_INVALID_STATE&&!releases&&!freed&&!calls[1]);
    deauth_pending=false;
    for(int stage=1;stage<=4;stage++) {
        for(int i=0;i<6;i++)calls[i]=0;
        freed=releases=0;s_ap_netif=(void *)1;s_ap_lease.acquired=true;failed=stage;
        assert(wifi_ap_cleanup()==-stage && s_ap_cleanup_pending);
        assert(freed==(stage==4));
        if(stage==3) {
            assert(wifi_ap_cleanup()==-3 && calls[3]==1 && !freed);
            /* Simulate a device reboot only for the next independent test. */
            s_ap_detach_error=0;s_ap_lease.acquired=false;s_ap_netif=NULL;s_ap_lifecycle.identity=0;
            continue;
        }
        failed=0;assert(wifi_ap_cleanup()==0);
        assert(freed==1 && releases==1 && !s_ap_lifecycle.identity && !s_ap_cleanup_pending);
        assert(calls[1]==1+(stage==1));
        assert(calls[3]==1+(stage==3));
        assert(calls[4]==1+(stage==4));
        assert(wifi_ap_cleanup()==0 && freed==1 && releases==1);
    }
}
''')

# Full AP parser/validator/result VM coverage lives in test_wifi_ap_config.py.

class WiFiAPStartup(unittest.TestCase):
    def test_adapter_retains_accepted_configuration_and_wipes_caller_config(self):
        body=extract(AP.read_text(),'js_wifi_start_ap')
        compile_run(self,r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t,JSValue,JSContext;
typedef struct { unsigned char secret[64]; } wifi_config_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
#define JS_EXCEPTION -99
#define WIFI_MODE_AP 2
#define WIFI_STORAGE_RAM 1
static void *s_ap_netif;
static struct { bool acquired; } s_ap_lease;
static bool s_ap_cleanup_pending,pending;
static const char *s_ap_stage;
static struct { const char *cleanup_stage; } state;
static int fail_at,reserves,configures,cleanups,zeroed;
static bool allow_disconnect;
static bool esp32_mquickjs_wifi_parse_ap_config(void *ctx,JSValue options,wifi_config_t *c,bool *allow) { (void)ctx;(void)options;memset(c,0x55,sizeof(*c));*allow=allow_disconnect;return true; }
static int activations,activate_error;
static int esp32_mquickjs_wifi_activate_ap(wifi_config_t *c) {
    assert(allow_disconnect && c->secret[0]==0x55);activations++;
    if(activate_error)state.cleanup_stage="configuration-resume";
    return activate_error;
}
static bool esp32_mquickjs_wifi_radio_accept_ap_config(const wifi_config_t *a,const wifi_config_t *b) { return a && b; }
static int esp32_mquickjs_wifi_radio_reserve_ap(void *lease) { assert(lease);reserves++;if(fail_at==1)return -1;s_ap_lease.acquired=true;return 0; }
static bool esp32_mquickjs_wifi_configuration_pending(void) { return pending; }
static void *esp32_mquickjs_wifi_state(void) { return &state; }
#define esp32_mquickjs_wifi_state() (&state)
static int esp32_mquickjs_wifi_configure_interfaces(int mode,int storage,bool start,void *sta,void *accept_sta,wifi_config_t *ap,bool (*accept)(const wifi_config_t *,const wifi_config_t *),const void *controls,const void *start_controls,bool allow_disconnect) {
    assert(mode==WIFI_MODE_AP && storage==WIFI_STORAGE_RAM && start && !sta && !accept_sta && ap && accept && s_ap_lease.acquired);
    assert(!controls && !start_controls && !allow_disconnect);configures++;if(fail_at==2)return -2;
    if(fail_at==3){pending=true;s_ap_lease.acquired=false;state.cleanup_stage="configuration-commit";return -3;}
    s_ap_netif=(void *)1;return 0;
}
static int share_mode;
static int esp32_mquickjs_wifi_reopen_ap_shared(const wifi_config_t *config,bool *handled) { assert(config);*handled=share_mode!=0;return share_mode==2?-17:0; }
static int wifi_ap_cleanup(void) { assert(!pending);cleanups++;s_ap_lease.acquired=false;s_ap_netif=NULL;return 0; }
static int wifi_ap_result(void *ctx,const wifi_config_t *c) { (void)ctx;assert(c->secret[0]==0x55);return 123; }
static int wifi_ap_error(void *ctx,const char *op,int err) { (void)ctx;assert(!strcmp(op,"wifi.startAP") && err);return -99; }
static int JS_ThrowTypeError(void *ctx,const char *text) { (void)ctx;(void)text;return -99; }
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n) { memset(p,0,n);zeroed++; }
'''+body+r'''
int main(void) {
    JSValue options=1;
    for(int stage=1;stage<=3;stage++) {
        fail_at=stage;reserves=configures=cleanups=zeroed=0;
        assert(js_wifi_start_ap(NULL,NULL,1,&options)==-99 && zeroed==1);
        assert(reserves==1 && configures==(stage>1) && cleanups==(stage==2));
        if(stage==3)assert(pending && !s_ap_lease.acquired && !strcmp(s_ap_stage,"configuration-commit"));
        else assert(!pending && !s_ap_lease.acquired);
    }
    pending=false;state.cleanup_stage=NULL;fail_at=0;
    assert(js_wifi_start_ap(NULL,NULL,1,&options)==123 && s_ap_lease.acquired && s_ap_netif);
    assert(zeroed==2);
    s_ap_lease.acquired=false;s_ap_netif=NULL;
    for(share_mode=1;share_mode<=2;share_mode++) {
        int r=reserves,c=configures,done=cleanups,z=zeroed;
        assert(js_wifi_start_ap(NULL,NULL,1,&options)==(share_mode==1?123:-99));
        assert(reserves==r && configures==c && cleanups==done && zeroed==z+1);
    }
    /* Replacement is reachable only after explicit capture. Failure stays
     * with the coordinator; the adapter never invokes an automatic retry. */
    s_ap_netif=(void *)1;s_ap_lease.acquired=true;
    int r=reserves,c=configures,done=cleanups,z=zeroed;
    assert(js_wifi_start_ap(NULL,NULL,1,&options)==-99 && !activations);
    allow_disconnect=true;
    assert(js_wifi_start_ap(NULL,NULL,1,&options)==123 && activations==1);
    activate_error=-77;
    assert(js_wifi_start_ap(NULL,NULL,1,&options)==-99 && activations==2);
    assert(!strcmp(s_ap_stage,"configuration-resume"));
    assert(reserves==r && configures==c && cleanups==done && zeroed==z+3);
    return 0;
}
''')

class WiFiAPErrorFlags(unittest.TestCase):
    def test_restart_required_combines_driver_and_netif_failures(self):
        # Production converter, real movable-GC VM; only Radio query/error
        # envelope boundaries are replaced. This does not execute a Wi-Fi SDK.
        with tempfile.TemporaryDirectory() as directory:
            boundary = r'''
typedef int esp_err_t;
#define ESP_OK 0
typedef struct { bool restart_required; } esp32_mquickjs_wifi_radio_status_t;
static bool driver_restart_required;
static int s_ap_detach_error;
static const char *s_ap_stage = "init";
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *s) {
    s->restart_required=driver_restart_required;return ESP_OK;
}
static JSValue esp32_mquickjs_throw_native_error(JSContext *ctx,const char *code,
    const char *operation,const char *message,JSValue details) {
    (void)ctx;(void)message;assert(!strcmp(code,"WIFI_AP_FAILED"));
    assert(!strcmp(operation,"wifi.startAP"));return details;
}
'''
            main = r'''
int main(void) {
    for(int mode=0;mode<4;mode++) {
        int total=1;
        for(int nth=0;nth<=total;nth++) {
            void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);
            assert(ctx);test_ctx=ctx;driver_restart_required=(mode&1)!=0;s_ap_detach_error=(mode&2)?-3:0;
            JSGCRef root_ref;JSValue *result=JS_PushGCRef(ctx,&root_ref);
            calls=0;fail_at=nth;collect=1;inject=1;
            *result=wifi_ap_error(ctx,"wifi.startAP",-7);
            if(nth==0){total=calls;assert(!JS_IsException(*result));}
            if(JS_IsException(*result))JS_GetException(ctx);
            else {
                JSValue required=JS_GetPropertyStr(ctx,*result,"restartRequired");
                assert(JS_IsBool(required));assert(required==JS_NewBool(mode!=0));
            }
            inject=0;collect=0;JS_PopGCRef(ctx,&root_ref);assert(!root_count);
            JS_FreeContext(ctx);free(heap);
        }
    }
    return 0;
}
'''
            binary=build(directory,boundary+extract(AP.read_text(),'wifi_ap_error'),main)
            run([str(binary)])
