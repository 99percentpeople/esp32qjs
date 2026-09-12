"""Deferred pinned SDK bootstrap parser and submit-only production entry cases."""
import importlib.util
import tempfile
import unittest
from test_idf_nan_control import NanNativeControl, ROOT, BASE, SDK
from wireless_vm_fixture import extract


class NanBootstrapSDK(unittest.TestCase):
    compile_run = NanNativeControl.compile_run

    def test_request_status_is_preserved_before_observer_and_no_cookie_is_invented(self):
        if not SDK.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('bootstrap_patch', ROOT / 'scripts/patch_idf_nan_pairing.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        original = (SDK / 'wifi_apps/nan_app/src/nan_pairing.c').read_text()
        with tempfile.TemporaryDirectory() as directory:
            from pathlib import Path
            prepared = patch.prepare(SDK.parent, Path(directory))['nan_pairing.c']
        for source, before in ((original, True), (prepared, False)):
            code = BOUNDARY
            if before:
                code += 'static void nan_app_bootstrap_notify(const wifi_nan_bootstrap_event_t*e){last_status=e->status;++notices;}\n'
            else:
                code += extract(source, 'nan_app_bootstrap_notify')
            for name in ('nan_app_bootstrap_indication', 'nan_app_bootstrap_completed', 'nan_app_parse_npba_from_receive'):
                code += extract(source, name)
            self.compile_run(code + r'''
int main(void){
 uint8_t peer[6]={2,3,4,5,6,7};struct nan_cb_npba_t npba={.type=1,.status=2,.methods=32};
 assert(nan_app_parse_npba_from_receive(7,9,peer,&npba));assert(notices==1&&last_status==2);
 npba.status=0;assert(nan_app_parse_npba_from_receive(7,9,peer,&npba)&&last_status==0);
 npba.type=2;npba.status=1;npba.methods=2;
 assert(nan_app_parse_npba_from_receive(7,9,peer,&npba)&&last_status==1);
 assert(!nan_app_parse_npba_from_receive(7,9,NULL,&npba));
 unsigned count=notices;nan_app_bootstrap_indication(9,7,NULL,32);nan_app_bootstrap_completed(0,9,7,NULL,2,0);
 assert(notices==count);return 0;
}
''', expect_failure=before)

    def test_exact_service_role_prevalidation_and_native_submission_error(self):
        code = BOUNDARY + SUBMIT_BOUNDARY
        code += (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_bootstrap_sdk.inc').read_text()
        self.compile_run(code + r'''
int main(void){
 uint32_t context=0;wifi_nan_followup_params_t p={.inst_id=7,.peer_inst_id=9,.peer_mac={2,3,4,5,6,7}};
 assert(esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,false,true)==ESP_ERR_INVALID_STATE&&!submits);
 peer_exists=true;native_error=-71;
 assert(esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,false,true)==-71&&submits==1);
 native_error=0;assert(!esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,false,true));
 assert(context==1234&&last_type==1&&last_status==0&&last_method==32&&!depth);
 assert(esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,false,true)==ESP_ERR_INVALID_ARG);
 context=0;assert(esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,true,true)==ESP_ERR_INVALID_STATE);
 own.type=ESP_NAN_PUBLISH;assert(!esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,true,false));
 assert(last_type==2&&last_status==1&&last_method==2);
 context=0;p.peer_mac[0]=1;assert(esp32_mquickjs_wifi_nan_sdk_bootstrap_send(&p,&context,true,true)==ESP_ERR_INVALID_ARG);
 assert(!depth&&submits==3);return 0;
}
''')


BOUNDARY = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define ESP_LOGI(...) ((void)0)
#define MACADDR_COPY(d,s) memcpy(d,s,6)
#define WIFI_NAN_NPBA_TYPE_REQUEST 1
#define WIFI_NAN_NPBA_TYPE_RESPONSE 2
#define WIFI_NAN_PAIRING_STATUS_ACCEPTED 0
#define WIFI_NAN_PAIRING_STATUS_REJECTED 1
#define WIFI_NAN_PAIRING_STATUS_COMEBACK 2
#define WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY 2
#define WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD 32
#define ESP32_MQUICKJS_NAN_BOOTSTRAP_REQUEST 1
#define ESP32_MQUICKJS_NAN_BOOTSTRAP_RESPONSE 2
#define ESP32_MQUICKJS_NAN_BOOTSTRAP_ACCEPTED 0
#define ESP32_MQUICKJS_NAN_BOOTSTRAP_REJECTED 1
#define ESP32_MQUICKJS_NAN_BOOTSTRAP_COMEBACK 2
#define ESP32_MQUICKJS_NAN_SDK_BOOTSTRAP 9
struct nan_cb_npba_t {uint8_t type,status;uint16_t methods;};
typedef struct {uint8_t type,status,peer_svc_id,own_svc_id,peer_nmi[6],reason_code;uint16_t methods;} wifi_nan_bootstrap_event_t;
typedef struct {uint8_t type,peer_service_id,status;uint16_t methods;} esp32_mquickjs_wifi_nan_sdk_bootstrap_t;
typedef struct {int kind;uint8_t service_id,peer[6];const void*data;size_t size;} esp32_mquickjs_wifi_nan_sdk_notice_t;
static unsigned notices;static uint8_t last_status;
static void (*s_bootstrap_cb)(const wifi_nan_bootstrap_event_t*);
static void esp32_mquickjs_wifi_nan_sdk_notice(const esp32_mquickjs_wifi_nan_sdk_notice_t*n){
 assert(n->kind==ESP32_MQUICKJS_NAN_SDK_BOOTSTRAP&&n->service_id==7&&n->peer[0]==2&&n->size==sizeof(esp32_mquickjs_wifi_nan_sdk_bootstrap_t));
 const esp32_mquickjs_wifi_nan_sdk_bootstrap_t*data=n->data;assert(data->peer_service_id==9);last_status=data->status;++notices;
}
'''

SUBMIT_BOUNDARY = r'''
typedef int esp_err_t;
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_NAN_PUBLISH 1
#define ESP_NAN_SUBSCRIBE 2
static int s_nan_data_lock=1,depth;
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
typedef struct {uint8_t inst_id,peer_inst_id,peer_mac[6];unsigned ssi_len;void*ssi,*vendor_ie;} wifi_nan_followup_params_t;
typedef struct {int type,status,method,reason_code;unsigned comeback_after,cookie;} wifi_nan_pairing_npba_params_t;
typedef struct {wifi_nan_pairing_npba_params_t*pairing_npba;} extra_params_internal_t;
struct own_svc_info {struct{bool pairing_setup;}pairing;int type;};
static struct own_svc_info own={.pairing.pairing_setup=true,.type=ESP_NAN_SUBSCRIBE};
static bool peer_exists;static unsigned submits;static int native_error,last_type,last_method;
static struct own_svc_info*nan_find_own_svc(uint8_t id){assert(depth==1&&id==7);return &own;}
static bool nan_find_peer_svc_exact(uint8_t own_id,uint8_t peer_id,const uint8_t*peer){
 assert(depth==1&&own_id==7&&peer_id==9&&peer[0]==2);return peer_exists;
}
static int esp_nan_internal_send_followup(wifi_nan_followup_params_t*p,uint32_t*context,extra_params_internal_t*extra){
 assert(!depth&&p&&extra&&extra->pairing_npba);++submits;
 wifi_nan_pairing_npba_params_t*n=extra->pairing_npba;last_type=n->type;last_status=n->status;last_method=n->method;
 assert(!n->reason_code&&!n->comeback_after&&!n->cookie);if(!native_error)*context=1234;return native_error;
}
'''
