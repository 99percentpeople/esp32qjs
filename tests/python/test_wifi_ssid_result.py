"""Deferred production SSID delivery and connect result with SDK sampling boundary."""
import tempfile
import unittest
from wireless_vm_fixture import ROOT, build, extract, run

BOUNDARIES = r'''
#include "cutils.h"
#define ESP_ERR_INVALID_RESPONSE -7
#define ESP_OK 0
typedef int wifi_phy_mode_t;
enum {WIFI_PHY_MODE_LR,WIFI_PHY_MODE_11B,WIFI_PHY_MODE_11G,WIFI_PHY_MODE_11A,
    WIFI_PHY_MODE_HT20,WIFI_PHY_MODE_HT40,WIFI_PHY_MODE_HE20,WIFI_PHY_MODE_VHT20};
typedef struct {bool valid;uint8_t ssid[32],ssid_len,bssid[6],channel;uint16_t aid;} esp32_mquickjs_wifi_link_snapshot_t;
typedef struct {bool rssi_valid,phy_valid;int rssi;wifi_phy_mode_t phy;} esp32_mquickjs_wifi_link_sample_t;
static struct {int radio_lease;} s_wifi_state;
static int esp32_mquickjs_wifi_radio_sample_station_link(const int *lease,const uint8_t *bssid,
    uint8_t channel,esp32_mquickjs_wifi_link_sample_t *sample) {
    assert(lease==&s_wifi_state.radio_lease && bssid[0]==2 && channel==6);
    memset(sample,0,sizeof(*sample));return -1;
}
static JSValue esp32_mquickjs_wifi_throw_operation_error(JSContext *ctx,const char *code,
    const char *operation,int error,int reason,uint32_t status) {
    assert(!strcmp(code,"WIFI_CONNECT_FAILED") && !strcmp(operation,"wifi.connect"));
    assert(error==ESP_ERR_INVALID_RESPONSE);(void)reason;(void)status;
    return JS_ThrowTypeError(ctx,"invalid connection snapshot");
}
'''

MAIN = r'''
int main(void) {
    static const struct {uint8_t bytes[32];uint8_t length;bool text;} cases[]={
        {{'A'},1,true},{{255,'A'},2,false},{{'A',0,'B'},3,true},
        {{0xc3,0xa9},2,true},{{0xed,0xa0,0x80},3,false},{{0xe2,0x82},2,false},
        {{'A','A','A','A','A','A','A','A','A','A','A','A','A','A','A','A',
          'A','A','A','A','A','A','A','A','A','A','A','A','A','A','A','A'},32,true}
    };
    for(size_t c=0;c<sizeof(cases)/sizeof(cases[0]);c++) {
        int total=1;
        for(int nth=0;nth<=total;nth++) {
            void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
            test_ctx=ctx;JSGCRef r,bytes_ref;JSValue *result=JS_PushGCRef(ctx,&r);
            esp32_mquickjs_wifi_link_snapshot_t link={.valid=true,.ssid_len=cases[c].length,.bssid={2},.channel=6,.aid=9};
            memcpy(link.ssid,cases[c].bytes,cases[c].length);
            calls=0;fail_at=nth;collect=1;inject=1;
            *result=esp32_mquickjs_wifi_make_connect_result(ctx,&link,123.5);
            if(nth==0){assert(!JS_IsException(*result));total=calls;}
            else assert(JS_IsException(*result));
            inject=0;collect=0;
            if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
            else {
                memset(&link,0,sizeof(link)); /* Result owns byte values, not native pointers. */
                JSValue text=JS_GetPropertyStr(ctx,*result,"ssid");
                assert(JS_IsString(ctx,text)==cases[c].text);
                if(!cases[c].text)assert(JS_IsNull(text));
                JSValue *bytes=JS_PushGCRef(ctx,&bytes_ref);*bytes=JS_GetPropertyStr(ctx,*result,"ssidBytes");
                uint32_t length;assert(!JS_ToUint32(ctx,&length,JS_GetPropertyStr(ctx,*bytes,"length")) && length==cases[c].length);
                for(uint32_t i=0;i<length;i++){uint32_t byte;assert(!JS_ToUint32(ctx,&byte,JS_GetPropertyUint32(ctx,*bytes,i)) && byte==cases[c].bytes[i]);}
                JS_PopGCRef(ctx,&bytes_ref);
                assert(JS_GetPropertyStr(ctx,*result,"connected")==JS_TRUE);
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"password")));
            }
            JS_PopGCRef(ctx,&r);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
        }
    }
    return 0;
}
'''


class WiFiSsidResult(unittest.TestCase):
    def test_binary_span_text_nullable_owned_bytes_and_failure_roots(self):
        base = ROOT / 'components/esp32_mquickjs/src/modules/wifi'
        config = (base / 'esp32_mquickjs_wifi_config.c').read_text()
        wifi = (base / 'esp32_mquickjs_wifi.c').read_text()
        code = BOUNDARIES
        code += ''.join(extract(config, name) for name in (
            'esp32_mquickjs_wifi_ssid_text', 'esp32_mquickjs_wifi_set_ssid_properties'))
        code += ''.join(extract(wifi, name) for name in (
            'wifi_negotiated_phy_name', 'wifi_link_snapshot_valid', 'wifi_set_link_properties',
            'esp32_mquickjs_wifi_make_connect_result'))
        with tempfile.TemporaryDirectory() as tmp:
            run([str(build(tmp, code, MAIN))])
