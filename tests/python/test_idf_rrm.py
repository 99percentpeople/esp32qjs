"""Deferred production SDK RRM regression; execute with the Wi-Fi phase suite.

The SDK request, response, timeout and reset bodies are compiled unchanged (or
through the production hash-gated patch). Only allocation, TX, timer, profile
and observation boundaries are injected. This file is not a replacement FSM.
"""
import os
import pathlib
import re
import sys
import unittest

from test_wireless_control_regression import compile_run

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_rrm import patch_source


def sdk_function(source, name):
    match = re.search(r'^(?:static )?(?:void|int|esp_err_t) ' + name
                      + r'\([^;{}]*\)\n\{', source, re.M)
    if not match:
        raise AssertionError(name)
    return source[match.start():source.index('\n}\n', match.start()) + 3]


class IDFRRM(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = pathlib.Path(sdk) / 'components/wpa_supplicant'
        cls.source = (component / 'src/common/rrm.c').read_bytes()
        cls.patched = patch_source(cls.source)
        cls.public = (component / 'esp_supplicant/src/esp_common.c').read_text()

    def test_source_drift_and_double_patch_rejected(self):
        for source in [self.source + b'\n', self.patched]:
            with self.subTest(length=len(source)), self.assertRaises(ValueError):
                patch_source(source)

    def test_production_request_response_timeout_and_exact_retirement(self):
        for patched, content in [(False, self.source), (True, self.patched)]:
            with self.subTest(patched=patched):
                source = content.decode()
                body = 'static bool *esp32qjs_rrm_tx_attempted;\n' if patched else ''
                for name in ['wpas_rrm_neighbor_rep_timeout_handler', 'wpas_rrm_reset',
                             'wpas_rrm_process_neighbor_rep', 'wpas_rrm_send_neighbor_rep_request']:
                    body += sdk_function(source, name)
                body += sdk_function(self.public, 'esp_rrm_send_neighbor_report_request')
                if patched:
                    body += 'typedef void (*esp32qjs_rrm_callback_t)(void *, const u8 *, size_t);\n'
                    for name in ['esp32qjs_rrm_query', 'esp32qjs_rrm_request',
                                 'esp32qjs_rrm_cancel', 'esp32qjs_rrm_publish_observation']:
                        body += sdk_function(source, name)
                compile_run(self, f'#define PATCHED {int(patched)}\n' + BOUNDARIES + body + MAIN)


class RRMDispatch(unittest.TestCase):
    def test_production_dispatch_keeps_sdk_result_and_unknown_ownership_separate(self):
        header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_rrm_sdk.h').read_text()
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_roaming/esp32_mquickjs_wifi_rrm_sdk.c').read_text()
        types = header[header.index('typedef void'):header.index('/* Internal only.')]
        call_type = source[source.index('typedef struct {'):source.index('} rrm_sdk_call_t;') + len('} rrm_sdk_call_t;')]
        compile_run(self, DISPATCH_BOUNDARIES + types + call_type
                    + sdk_function(source, 'rrm_sdk_dispatch')
                    + sdk_function(source, 'esp32_mquickjs_wifi_rrm_sdk_command')
                    + DISPATCH_MAIN)


DISPATCH_BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
static int dispatch_mode, calls, queries, native_rc, native_ownership;
static void callback(void *id,const uint8_t *data,size_t n) {(void)id;(void)data;(void)n;}
static int identity;
static int esp32qjs_rrm_request(void (*cb)(void *,const uint8_t *,size_t),void *id,bool *attempted) {
    assert(cb==callback && id==&identity);calls++;*attempted=true;return native_rc;
}
static int esp32qjs_rrm_cancel(void (*cb)(void *,const uint8_t *,size_t),void *id) {
    assert(cb==callback && id==&identity);calls++;return native_rc;
}
static int esp32qjs_rrm_query(void (*cb)(void *,const uint8_t *,size_t),void *id) {
    assert(cb==callback && id==&identity);queries++;return native_ownership;
}
static int eloop_register_timeout_blocking(int (*fn)(void *,void *),void *data,void *ctx) {
    if(dispatch_mode==1)return -1;
    if(dispatch_mode==2)return 0; /* Even success without entry is not proof. */
    return fn(data,ctx);
}
'''

DISPATCH_MAIN = r'''
int main(void) {
    esp32_mquickjs_wifi_rrm_sdk_result_t r;
    for(dispatch_mode=1;dispatch_mode<=2;dispatch_mode++) {
        assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_SUBMIT,callback,&identity,&r)==ESP_FAIL);
        assert(!r.entered && !r.tx_attempted && r.ownership==-1 && !calls && !queries);
    }
    dispatch_mode=0;native_rc=-12;native_ownership=0;
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_SUBMIT,callback,&identity,&r)==ESP_OK);
    assert(r.entered && r.tx_attempted && r.code==-12 && !r.ownership && calls==1 && queries==1);
    native_ownership=-16;native_rc=-16;
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_CANCEL,callback,&identity,&r)==ESP_OK);
    assert(r.entered && !r.tx_attempted && r.code==-16 && r.ownership==-16);
    native_ownership=1;
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_QUERY,callback,&identity,&r)==ESP_OK);
    assert(r.code==1 && r.ownership==1 && !r.tx_attempted && calls==2 && queries==4);
    assert(esp32_mquickjs_wifi_rrm_sdk_command(-1,callback,&identity,&r)==ESP_ERR_INVALID_ARG);
    assert(!r.entered && r.ownership==-1);
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_QUERY,NULL,&identity,&r)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_QUERY,callback,NULL,&r)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_QUERY,callback,&identity,NULL)==ESP_ERR_INVALID_ARG);
    return 0;
}
'''


BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
#define wpa_printf(...) ((void)0)
#define wpa_dbg(...) ((void)0)
#define wpa_hexdump(...) ((void)0)
#define wpa_msg(...) ((void)0)
#define os_memcpy memcpy
#define WLAN_RRM_CAPS_NEIGHBOR_REPORT 2
#define WLAN_ACTION_RADIO_MEASUREMENT 5
#define WLAN_RRM_NEIGHBOR_REPORT_REQUEST 4
#define WLAN_EID_SSID 0
#define WLAN_EID_MEASURE_REQUEST 38
#define MEASURE_REQUEST_LCI_LEN 8
#define MEASURE_REQUEST_CIVIC_LEN 8
#define MEASURE_TYPE_LCI 8
#define MEASURE_TYPE_LOCATION_CIVIC 11
#define LOCATION_SUBJECT_REMOTE 1
#define LCI_REQ_SUBELEM_MAX_AGE 4
#define RRM_NEIGHBOR_REPORT_TIMEOUT 1
typedef void (*callback_t)(void *, const u8 *, size_t);
struct rrm_data {
    unsigned rrm_used:1;
    callback_t notify_neighbor_rep;
    void *neighbor_rep_cb_ctx;
    u8 next_neighbor_rep_token;
};
struct wpa_supplicant { struct rrm_data rrm; u8 rrm_ie[1]; void *current_bss; };
struct wpa_ssid_value { u8 ssid[32]; size_t ssid_len; };
struct wifi_ssid { u8 ssid[32]; u8 len; };
static struct wpa_supplicant g_wpa_supp;
static struct wifi_ssid profile = {{'a',0,'b'},3};
static struct wifi_ssid *esp_wifi_sta_get_prof_ssid_internal(void) { return &profile; }
static void wpas_clear_beacon_rep_data(struct wpa_supplicant *s) { (void)s; }
struct wpabuf { u8 data[128]; size_t length, capacity; };
static struct wpabuf packet;
static int fail_alloc, tx_rc, timer_rc, allocations, transmissions, observations;
static bool queue_full;
static u8 sent_token;
static struct wpabuf *wpabuf_alloc(size_t n) {
    if (fail_alloc) return NULL;
    assert(!allocations && n<=sizeof(packet.data)); allocations++;
    packet=(struct wpabuf){.capacity=n}; return &packet;
}
static void wpabuf_free(struct wpabuf *p) { assert(p==&packet && allocations==1); allocations--; }
static void wpabuf_put_u8(struct wpabuf *p,unsigned v) {
    assert(p->length<p->capacity); p->data[p->length++]=(u8)v;
}
static void wpabuf_put_le16(struct wpabuf *p,unsigned v) { wpabuf_put_u8(p,v);wpabuf_put_u8(p,v>>8); }
static void wpabuf_put_data(struct wpabuf *p,const void *data,size_t n) {
    assert(n<=p->capacity-p->length);memcpy(p->data+p->length,data,n);p->length+=n;
}
static const void *wpabuf_head(struct wpabuf *p) { return p->data; }
static size_t wpabuf_len(struct wpabuf *p) { return p->length; }
static int wpa_drv_send_action(struct wpa_supplicant *s,int a,int b,const void *data,size_t n,int c) {
    (void)s;(void)a;(void)b;(void)c;assert(n>=3);sent_token=((const u8 *)data)[2];
    transmissions++;return tx_rc;
}
static struct { void (*fn)(void *,void *);void *data,*ctx; } timer;
static int eloop_register_timeout(int sec,int usec,void (*fn)(void *,void *),void *data,void *ctx) {
    assert(sec==1 && !usec && !timer.fn);
    if (timer_rc) return timer_rc;
    timer.fn=fn;timer.data=data;timer.ctx=ctx;return 0;
}
static int eloop_cancel_timeout(void (*fn)(void *,void *),void *data,void *ctx) {
    if(timer.fn==fn && timer.data==data && timer.ctx==ctx){timer.fn=NULL;return 1;}return 0;
}
static void fire_timer(void) {
    assert(timer.fn);void (*fn)(void *,void *)=timer.fn;timer.fn=NULL;fn(timer.data,timer.ctx);
}
void neighbor_report_recvd_cb(void *ctx,const u8 *data,size_t n) {
    assert(!ctx);assert((data!=NULL)==(n!=0));if(!queue_full)observations++;
}
static int identities[2],callbacks,null_callbacks;
static void record(void *ctx,const u8 *data,size_t n) {
    assert(ctx==&identities[0]);assert(g_wpa_supp.rrm.notify_neighbor_rep==record);
    /* SDK owns callback storage throughout this call, including timeout. */
    assert(g_wpa_supp.rrm.neighbor_rep_cb_ctx==ctx);
    callbacks++;if(!data){assert(!n);null_callbacks++;}
}
'''

MAIN = r'''
static void prepare(unsigned token) {
    assert(!allocations && !timer.fn);
    g_wpa_supp=(struct wpa_supplicant){.rrm={.next_neighbor_rep_token=(u8)token},
        .rrm_ie={WLAN_RRM_CAPS_NEIGHBOR_REPORT},.current_bss=&identities[1]};
    fail_alloc=tx_rc=timer_rc=callbacks=null_callbacks=transmissions=observations=0;
    queue_full=false;
}
static int submit(void) {
    return wpas_rrm_send_neighbor_rep_request(&g_wpa_supp,NULL,0,0,record,&identities[0]);
}
int main(void) {
    for(unsigned token=0;token<256;token++) {
        prepare(token);assert(submit()==0 && sent_token==token && !allocations);
        u8 report[2]={(u8)token,0};
        wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,0);assert(!callbacks && timer.fn);
        report[0]=(u8)(token+1);wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,2);
        assert(!callbacks && timer.fn);
        report[0]=(u8)token;wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,2);
        if(!PATCHED && token==255){assert(!callbacks && timer.fn);fire_timer();assert(null_callbacks==1);}
        else {assert(callbacks==1 && !null_callbacks && !timer.fn);}
        assert(!g_wpa_supp.rrm.notify_neighbor_rep && !g_wpa_supp.rrm.neighbor_rep_cb_ctx);
        wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,2);assert(callbacks==1);
    }
    prepare(1);timer_rc=-1;int rc=submit();assert(!allocations && !timer.fn && transmissions==1);
    if(PATCHED){assert(rc==-ENOMEM && !g_wpa_supp.rrm.notify_neighbor_rep);}
    else {assert(rc==0 && g_wpa_supp.rrm.notify_neighbor_rep==record);assert(submit()==-EBUSY);}
    wpas_rrm_reset(&g_wpa_supp);assert(null_callbacks==(PATCHED?0:1));
    prepare(1);fail_alloc=1;assert(submit()==-ENOMEM && !transmissions && !timer.fn);
    prepare(1);tx_rc=-1;assert(submit()==-ECANCELED && transmissions==1 && !allocations && !timer.fn);
    prepare(1);assert(submit()==0);assert(submit()==-EBUSY && transmissions==1);
    wpas_rrm_reset(&g_wpa_supp);assert(null_callbacks==1 && !timer.fn && g_wpa_supp.rrm.next_neighbor_rep_token==1);
#if PATCHED
    bool attempted;
    prepare(1);g_wpa_supp.current_bss=NULL;
    assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==-2 && !attempted);
    prepare(1);fail_alloc=1;
    assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==-ENOMEM && !attempted);
    prepare(1);timer_rc=-1;
    assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==-ENOMEM && attempted);
    assert(esp32qjs_rrm_query(record,&identities[0])==0);
    prepare(1);assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==0 && attempted);
    assert(packet.length==8 && !memcmp(packet.data+5,profile.ssid,3));
    assert(esp32qjs_rrm_query(record,&identities[0])==1);
    assert(esp32qjs_rrm_cancel(record,&identities[1])==-EBUSY && timer.fn);
    assert(esp32qjs_rrm_cancel(neighbor_report_recvd_cb,&identities[0])==-EBUSY && timer.fn);
    assert(esp32qjs_rrm_request(record,&identities[1],&attempted)==-EBUSY && !attempted);
    assert(esp32qjs_rrm_cancel(record,&identities[0])==0 && !timer.fn && !callbacks);
    assert(esp32qjs_rrm_cancel(record,&identities[0])==0);
    u8 report[2]={sent_token,0};wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,2);assert(!callbacks);
    assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==0);report[0]=sent_token;
    queue_full=true;wpas_rrm_process_neighbor_rep(&g_wpa_supp,report,2);
    assert(callbacks==1 && !observations && esp32qjs_rrm_query(record,&identities[0])==0);
    esp32qjs_rrm_publish_observation(report,2);assert(!observations);
    queue_full=false;esp32qjs_rrm_publish_observation(report,2);assert(observations==1);
    esp32qjs_rrm_publish_observation(NULL,2);esp32qjs_rrm_publish_observation(report,0);assert(observations==1);
    assert(esp32qjs_rrm_request(record,&identities[0],&attempted)==0);fire_timer();
    assert(callbacks==2 && null_callbacks==1 && esp32qjs_rrm_query(record,&identities[0])==0);
    assert(esp32qjs_rrm_query(NULL,&identities[0])==-EINVAL);
    g_wpa_supp.rrm.neighbor_rep_cb_ctx=&identities[1];
    assert(esp32qjs_rrm_query(record,&identities[0])==-EIO);
    assert(esp32qjs_rrm_cancel(record,&identities[0])==-EIO);
#endif
    assert(!allocations);return 0;
}
'''
