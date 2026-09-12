"""Deferred checks of production WPS SDK init/scan/TX/RSN error paths.

Implementation phase: AST only. Uses the production generator, original SDK
function bodies and native error owner; no independent test state machine.
"""
import os
from pathlib import Path
import re
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps_native import NATIVE_HEADER, NATIVE_SOURCE, patch_source
from patch_idf_wps import function


def declarations(text):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', text, flags=re.M)


class WPSErrors(unittest.TestCase):
    def test_production_init_scan_and_transmit_failures(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = Path(sdk) / 'components/wpa_supplicant'
        relative = 'esp_supplicant/src/esp_wps.c'
        patched = patch_source(relative, (component / relative).read_bytes()).decode()
        native = NATIVE_SOURCE.read_text()
        record = native[native.index('typedef struct {'):native.index('} esp32qjs_wps_native_t;') + len('} esp32qjs_wps_native_t;')]
        code = PRELUDE + declarations((component / 'esp_supplicant/include/esp_wps.h').read_text())
        code += declarations(NATIVE_HEADER.read_text()) + record + BOUNDARIES
        for name in ('esp32qjs_wps_native_scrub','esp32qjs_wps_record_error','esp32qjs_wps_native_pin'):
            code += function(native, name)
        for name in ('wps_build_ic_appie_wps_pr','wps_build_ic_appie_wps_ar','wifi_station_wps_init',
                     'wifi_station_wps_deinit','wifi_wps_scan_done_body','is_ap_supports_sae'):
            code += function(patched, name)
        first = patched.index('static inline int wps_sm_ether_send(')
        last = patched.index('static inline u8 *wps_sm_alloc_eapol(', first)
        code += patched[first:last] + function(patched, 'wps_send_eapol_frame')
        compile_run(self, code + IMPLEMENTATIONS + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;typedef uint16_t u16;typedef int esp_err_t;typedef int ETS_STATUS;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_ARG 0x102
#define WIFI_IF_STA 0
#define ETH_ALEN 6
#define ETH_P_EAPOL 0x888e
#define WIFI_APPIE_WPS_PR 1
#define WIFI_APPIE_WPS_AR 2
#define WSC_ID_ENROLLEE_LEN 8
#define WSC_ID_ENROLLEE "enrollee"
#define WPS_UUID_LEN 16
#define WPS_STATUS_DISABLE 0
#define WPS_STATUS_PENDING 1
#define WPS_STATUS_SCANNING 2
#define WAIT_START 1
#define WPS_CONNECT_TIMEOUT_SECS 30
#define WPS_SCAN_RETRY_TIMEOUT_SECS 2
#define DEV_PW_PUSHBUTTON 4
#define DEV_PW_DEFAULT 0
#define WPS_REQ_ENROLLEE 1
#define WPA_KEY_MGMT_SAE 8
#define WIFI_EVENT 1
#define WIFI_EVENT_STA_WPS_ER_PIN 2
#define WIFI_EVENT_STA_WPS_ER_FAILED 3
#define WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP 4
#define OS_BLOCK 0
#define ELOOP_ALL_CTX ((void *)-1)
#define wpa_printf(...) ((void)0)
#define os_memcpy memcpy
#define os_memset memset
static void forced_memzero(void *p,size_t n) {memset(p,0,n);}
struct wpabuf {size_t used;uint8_t data[64];};
static size_t wpabuf_len(const struct wpabuf *b) {return b->used;}
static const void *wpabuf_head(const struct wpabuf *b) {return b->data;}
static void wpabuf_free(struct wpabuf *b) {(void)b;}
static void wpabuf_clear_free(struct wpabuf *b) {if(b)memset(b->data,0,sizeof(b->data));}
static struct wpabuf probe,extra,assoc;
static int probe_oom,resize_error,assoc_oom,appie_error;
struct wps_device_data {unsigned config_methods;};
struct wps_context {struct wps_device_data dev;uint8_t uuid[16];unsigned config_methods;void *cred_cb;struct wpabuf *dh_privkey;};
struct wps_config {struct wps_context *wps;uint8_t pin[9];};
struct wps_data {struct wps_context *wps;uint8_t dev_password[8];};
struct wps_sm {
    struct wps_device_data *dev;struct wps_context *wps_ctx;struct wps_data *wps;
    uint8_t ownaddr[6],identity[16],uuid[16],bssid[6];size_t identity_len;
    struct {struct wpabuf *in_buf;} wsc_frag;
    struct {uint8_t ssid[32];size_t ssid_len;} creds[3];
    unsigned discover_ssid_cnt,channel;bool wps_pbc_overlap;int state;
};
struct wps_funcs {void *wps_parse_scan_result,*wifi_station_wps_start,*wps_sm_rx_eapol,*wps_start_pending;};
struct wps_sm_funcs {void *wps_sm_notify_deauth;};
typedef struct {uint8_t pin_code[8];} wifi_event_sta_wps_er_pin_t;
typedef struct {struct {uint8_t bssid[6],ssid[32],bssid_set,channel,failure_retry_cnt;} sta;} wifi_config_t;
struct wps_scan_ie {uint8_t *rsn;};
struct wpa_ie_data {unsigned key_mgmt;};
'''

BOUNDARIES = r'''
static esp32qjs_wps_native_t record,*s_wps_native=&record;
static struct wps_sm *gWpsSm;
static struct wps_sm_funcs *s_wps_sm_cb;
static struct {uint8_t own_addr[6];} gWpaSm;
static uint8_t s_wps_success_evt[32];
static struct wps_data protocol;
static unsigned nth,fail_nth,live,config_calls,connect_calls,timers,tx_frees;
static int mac_error,device_error,pin_error,protocol_null,callback_error,status_error,config_error,connect_error;
static int current_status,associated_error,tx_error,parse_error;
static bool tx_oom;
static void *adopted;
static uint8_t tx_storage[128];static size_t tx_length;
static void *os_zalloc(size_t size) {nth++;if(nth==fail_nth)return NULL;void *p=calloc(1,size);assert(p);live++;return p;}
static void os_free(void *p) {if(p){assert(live);live--;free(p);}}
static void bin_clear_free(void *p,size_t size) {if(p){memset(p,0,size);os_free(p);}}
static struct wps_sm *wps_sm_get(void) {return gWpsSm;}
static int wps_get_type(void) {return WPS_TYPE_PIN;}
static int wps_get_status(void) {return current_status;}
static int wps_set_status(int status) {if(!status_error)current_status=status;return status_error;}
static struct wpabuf *wps_build_probe_req_ie(int id,void *dev,void *uuid,int req,int pbc,void *extra) {
    (void)id;(void)dev;(void)uuid;(void)req;(void)pbc;(void)extra;return probe_oom?NULL:&probe;
}
static struct wpabuf *wps_build_assoc_req_ie(int req) {(void)req;return assoc_oom?NULL:&assoc;}
static int wpabuf_resize(struct wpabuf **b,size_t size) {(void)size;if(!resize_error)*b=&extra;return resize_error;}
static void wpabuf_put_buf(struct wpabuf *b,const struct wpabuf *v) {(void)b;(void)v;}
static int esp_wifi_set_appie_internal(int type,uint8_t *data,size_t len,int flag) {
    (void)type;(void)data;(void)len;(void)flag;return appie_error;
}
static void *esp_wifi_get_appie_internal(int type) {(void)type;return NULL;}
static int esp_wifi_unset_appie_internal(int type) {(void)type;return ESP_OK;}
static int esp_wifi_get_macaddr_internal(int itf,uint8_t *out) {(void)itf;memset(out,1,6);return mac_error;}
static int wps_dev_init(void) {gWpsSm->dev=&gWpsSm->wps_ctx->dev;return device_error;}
static int wps_dev_deinit(void *dev) {(void)dev;return 0;}
static int wps_init_cfg_pin(struct wps_config *cfg) {(void)cfg;return pin_error;}
static struct wps_data *wps_init(struct wps_config *cfg) {
    protocol.wps=cfg->wps;memcpy(protocol.dev_password,"12345670",8);return protocol_null?NULL:&protocol;
}
static void wps_deinit(struct wps_data *p) {(void)p;}
static int save_credentials_cb(void) {return 0;}
static int wps_delete_timer(void) {return 0;}
static int wps_parse_scan_result(void) {return 0;}
static int wps_sm_rx_eapol(void) {return 0;}
static int wps_start_pending(void) {return 0;}
static int esp_wifi_set_wps_cb_internal(void *p) {
    if(p && callback_error)return callback_error;os_free(adopted);adopted=p;return 0;
}
static int esp_event_post(int base,int event,void *p,size_t len,int wait) {
    (void)base;(void)event;(void)p;(void)len;(void)wait;assert(0);return 0;
}
static int esp_wifi_set_config(int itf,const wifi_config_t *cfg) {
    assert(itf==WIFI_IF_STA && cfg->sta.failure_retry_cnt==2);config_calls++;return config_error;
}
static int esp_wifi_connect(void) {connect_calls++;return connect_error;}
static void wifi_station_wps_msg_timeout(void *a,void *b) {(void)a;(void)b;}
static void wifi_wps_scan(void *a,void *b) {(void)a;(void)b;}
static int eloop_cancel_timeout(void (*f)(void*,void*),void *a,void *b) {(void)f;(void)a;(void)b;return 0;}
static int esp32qjs_wps_register_timeout(unsigned s,unsigned us,void (*f)(void*,void*)) {(void)s;(void)us;(void)f;timers++;return 0;}
static int wps_send_event_and_disable(int event,void *p,size_t size) {(void)event;(void)p;(void)size;return 0;}
static int wpa_parse_wpa_ie_rsn(const uint8_t *ie,size_t len,struct wpa_ie_data *info) {
    (void)ie;(void)len;info->key_mgmt=WPA_KEY_MGMT_SAE;return parse_error;
}
static int esp_wifi_get_assoc_bssid_internal(uint8_t *out) {memset(out,2,6);return associated_error;}
static int wpa_ether_send(void *sm,const uint8_t *bssid,uint16_t proto,const uint8_t *data,size_t len) {
    (void)sm;(void)bssid;(void)proto;assert(data==tx_storage && len==tx_length);return tx_error;
}
static uint8_t *wps_sm_alloc_eapol(struct wps_sm *sm,uint8_t type,const void *data,uint16_t size,size_t *len,void **pos) {
    (void)sm;(void)type;(void)pos;if(tx_oom)return NULL;assert(size+4<=sizeof(tx_storage));
    memset(tx_storage,0xaa,sizeof(tx_storage));memcpy(tx_storage+4,data,size);*len=tx_length=size+4;return tx_storage;
}
static void wps_sm_free_eapol(uint8_t *buf) {
    assert(buf==tx_storage);for(size_t i=0;i<tx_length;i++)assert(!buf[i]);tx_frees++;
}
'''

IMPLEMENTATIONS = r'''
static void reset(void) {
    assert(!live && !gWpsSm && !adopted);
    memset(&record,0,sizeof(record));nth=fail_nth=0;
    mac_error=device_error=pin_error=protocol_null=callback_error=0;
    probe_oom=resize_error=assoc_oom=appie_error=0;
    current_status=status_error=config_error=connect_error=0;
    config_calls=connect_calls=timers=tx_frees=0;
    associated_error=tx_error=parse_error=0;tx_oom=false;
}
static void retire_fixture_sdk(void) {
    esp_wifi_set_wps_cb_internal(NULL);
    record.status.inputs_stopped=true;
    if(gWpsSm)assert(wifi_station_wps_deinit()==ESP_OK);
    assert(!live && !s_wps_sm_cb);
}
'''

MAIN = r'''
int main(void) {
    esp_wps_config_t config={.wps_type=WPS_TYPE_PIN};
    for(unsigned i=1;i<=4;i++) {
        reset();fail_nth=i;
        assert(wifi_station_wps_init(&config)==ESP_ERR_NO_MEM);
        assert(record.status.error==ESP_ERR_NO_MEM && record.status.terminal_seen);
        assert(record.status.error_stage==(i<=2?ESP32QJS_WPS_STAGE_CONTEXT:ESP32QJS_WPS_STAGE_CALLBACKS));
        retire_fixture_sdk();
    }
    const int stages[]={ESP32QJS_WPS_STAGE_MAC,ESP32QJS_WPS_STAGE_DEVICE,ESP32QJS_WPS_STAGE_PIN,
        ESP32QJS_WPS_STAGE_CALLBACKS,ESP32QJS_WPS_STAGE_PROBE_IE,ESP32QJS_WPS_STAGE_ASSOC_IE};
    for(unsigned i=0;i<6;i++) {
        reset();int expected=601+i;
        if(i==0)mac_error=expected;if(i==1)device_error=expected;if(i==2)pin_error=expected;
        if(i==3)callback_error=expected;if(i==4)appie_error=expected;
        if(i==5){assoc_oom=1;expected=ESP_ERR_NO_MEM;}
        assert(wifi_station_wps_init(&config)==expected);
        assert(record.status.error==expected && record.status.error_stage==stages[i]);
        assert(esp32qjs_wps_record_error(999,ESP32QJS_WPS_STAGE_CONNECT)==999);
        assert(record.status.error==expected && !record.status.pin_available);
        retire_fixture_sdk();
    }
    reset();assert(wifi_station_wps_init(&config)==ESP_OK);
    gWpsSm->discover_ssid_cnt=1;gWpsSm->creds[0].ssid_len=3;
    config_error=701;wifi_wps_scan_done_body(NULL,0);
    assert(config_calls==1 && !connect_calls && record.status.error==701 && record.status.error_stage==ESP32QJS_WPS_STAGE_CONFIG);
    retire_fixture_sdk();
    reset();assert(wifi_station_wps_init(&config)==ESP_OK);
    gWpsSm->discover_ssid_cnt=1;connect_error=702;wifi_wps_scan_done_body(NULL,0);
    assert(config_calls==1 && connect_calls==1 && !timers && record.status.error_stage==ESP32QJS_WPS_STAGE_CONNECT);
    retire_fixture_sdk();
    reset();assert(wifi_station_wps_init(&config)==ESP_OK);
    wifi_wps_scan_done_body(NULL,9);
    assert(!config_calls && !connect_calls && record.status.error==9 && record.status.error_stage==ESP32QJS_WPS_STAGE_SCAN_DONE);
    retire_fixture_sdk();
    for(unsigned i=0;i<4;i++) {
        reset();assert(wifi_station_wps_init(&config)==ESP_OK);
        if(i==0)associated_error=801;if(i==1)tx_error=802;if(i==2)tx_oom=true;
        int expected=i==0?801:i==1?802:i==2?ESP_ERR_NO_MEM:ESP_ERR_INVALID_SIZE;
        assert(wps_send_eapol_frame(0,"secret",i==3?65536:6)==expected);
        assert(record.status.error==expected && record.status.terminal_seen);
        assert(tx_frees==(i<2?1:0));retire_fixture_sdk();
    }
    uint8_t rsn[]={48,0};struct wps_scan_ie scan={.rsn=rsn};
    parse_error=-1;assert(!is_ap_supports_sae(&scan));
    parse_error=0;assert(is_ap_supports_sae(&scan));
    return 0;
}
'''
