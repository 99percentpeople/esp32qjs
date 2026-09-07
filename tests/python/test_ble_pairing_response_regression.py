"""Production pairing response rejects identity changes during JS conversion."""
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from test_wireless_control_regression import function,compile_run
BLE=pathlib.Path(__file__).resolve().parents[2]/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c'
SDK=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define portMAX_DELAY 0
#define BLE_HS_ETIMEOUT 1
#define BLE_HS_EINVAL 2
#define BLE_SM_IOACT_NUMCMP 1
#define BLE_SM_IOACT_INPUT 2
#define JS_EXCEPTION -1
#define JS_FALSE 0
#define JS_TRUE 1
#define JS_IsBool(v) ((v)==0 || (v)==1)
#define JS_VALUE_GET_SPECIAL_VALUE(v) (v)
static int s_ble_connection_mutex,locks;
#define xSemaphoreTakeRecursive(m,t) ((void)(m),(void)(t),locks++)
#define xSemaphoreGiveRecursive(m) ((void)(m),assert(locks>0),locks--)
typedef void JSContext;
typedef int JSValue;
typedef struct { bool open;unsigned generation,pairing_request_id,pairing_action,pairing_passkey;int64_t pairing_expires_at_us;int conn_handle; } ble_connection_slot_t;
static ble_connection_slot_t slot={true,1,7,BLE_SM_IOACT_INPUT,123456,100,9};
struct ble_sm_io { int action,passkey,numcmp_accept; };
static int converted,mutate,injected,zeroed;
static ble_connection_slot_t *ble_connection_from_value(void *ctx,int v,void *r,bool fail) { (void)ctx;(void)v;(void)r;(void)fail;return &slot; }
static bool ble_to_u32(void *ctx,int v,unsigned *out) {
    (void)ctx;*out=v;
    if(++converted==2 && mutate) {
        if(mutate==1) slot.open=false;
        if(mutate==2) slot.generation++;
        if(mutate==3) slot.pairing_request_id++;
        if(mutate==4) slot.pairing_expires_at_us=0;
    }
    return true;
}
static int64_t esp_timer_get_time(void) { return 50; }
static bool JS_HasException(void *ctx) { (void)ctx;return false; }
static JSValue JS_ThrowTypeError(void *ctx,const char *text) { (void)ctx;(void)text;return JS_EXCEPTION; }
static JSValue JS_ThrowRangeError(void *ctx,const char *text) { (void)ctx;(void)text;return JS_EXCEPTION; }
static JSValue ble_throw_error(void *ctx,const char *code,int h,int a,int c,int attr) { (void)ctx;(void)code;(void)h;(void)a;(void)c;(void)attr;return JS_EXCEPTION; }
static int ble_sm_inject_io(int h,const struct ble_sm_io *r) { assert(locks && h==9);injected++;if(r->action==BLE_SM_IOACT_NUMCMP)assert(!r->numcmp_accept);return 0; }
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n) { memset(p,0,n);zeroed++; }
'''
class BlePairingResponseRegression(unittest.TestCase):
    def production(self):
        return function(BLE.read_text().replace('JSValue js_ble_connection_respond_pairing(',
            'static JSValue js_ble_connection_respond_pairing('),'js_ble_connection_respond_pairing')

    def test_disconnect_generation_request_change_and_expiry_during_conversion_are_rejected(self):
        compile_run(self,SDK+self.production()+r'''
int main(void) {
    for(mutate=1;mutate<=4;mutate++) {
        slot=(ble_connection_slot_t){true,1,7,BLE_SM_IOACT_INPUT,123456,100,9};
        converted=injected=zeroed=0;
        JSValue self=1,args[]={7,123456};
        assert(js_ble_connection_respond_pairing(NULL,&self,2,args)==JS_EXCEPTION);
        assert(!injected && !locks && zeroed==1);
    }
}
''')

    def test_numeric_comparison_requires_explicit_boolean_and_preserves_rejection(self):
        compile_run(self,SDK+self.production()+r'''
int main(void) {
    slot.pairing_action=BLE_SM_IOACT_NUMCMP;
    JSValue self=1,args[]={7,9};
    assert(js_ble_connection_respond_pairing(NULL,&self,2,args)==JS_EXCEPTION);
    assert(!injected && slot.pairing_request_id==7);
    args[1]=JS_FALSE;
    assert(js_ble_connection_respond_pairing(NULL,&self,2,args)==JS_TRUE);
    assert(injected==1 && zeroed==1 && !slot.pairing_request_id && !locks);
}
''')
