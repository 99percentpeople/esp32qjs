"""Deferred production WPS result/timer boundary checks.

Only AST-check during implementation. Does not prove SDK scan/TX retirement,
Radio admission, native IPC, public Futures, or RF negotiation.
"""
import os
from pathlib import Path
import re
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps_native import NATIVE_HEADER, NATIVE_SOURCE, AP_RESULT_HEADER, patch_source
from patch_idf_wps import function


def declarations(text):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', text, flags=re.M)


class IDFWPSNative(unittest.TestCase):
    def test_production_result_identity_and_secret_lifetime(self):
        self.production_case(MAIN)

    def test_ap_result_blocks_station_before_native_allocation_or_mutation(self):
        self.production_case(r'''
int main(void) {
    esp_wps_config_t config={.wps_type=WPS_TYPE_PIN};uint64_t id=0;
    ap_reserved=true;
    assert(esp32qjs_wps_native_begin(&config,&id)==ESP_ERR_INVALID_STATE);
    assert(!id && !allocations && !enable_calls && !s_wps_native);
    ap_reserved=false;
    assert(esp32qjs_wps_native_begin(&config,&id)==ESP_OK && id && allocations==1);
    fixture_dispose();return 0;
}
''', registrar=True)

    def production_case(self, main, registrar=False):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = Path(sdk) / 'components/wpa_supplicant'
        relative = 'esp_supplicant/src/esp_wps.c'
        patched = patch_source(relative, (component / relative).read_bytes()).decode()
        wps_header = (component / 'src/wps/wps.h').read_text()
        start = wps_header.index('struct wps_credential {')
        cred = wps_header[start:wps_header.index('\n};', start) + 3]
        os_header = (component / 'port/include/os.h').read_text()
        start = os_header.index('static void * (* const volatile memset_func)')
        zero = os_header[start:os_header.index('\n#endif', start)]
        native = declarations(NATIVE_SOURCE.read_text())
        # The target ABI is checked in real C3/S3/C5 builds. Host pointer width
        # differs; the production high/low helpers still carry 32-bit words.
        native = native.replace('_Static_assert(sizeof(uintptr_t) == 4, "review WPS timer identity encoding");', '')
        code = PRELUDE
        if registrar:
            code += '\n#define CONFIG_WPS_REGISTRAR 1\nstatic bool ap_reserved;\nbool esp32qjs_wps_ap_result_held(void){return ap_reserved;}\n'
        code += declarations((component / 'esp_supplicant/include/esp_wps.h').read_text())
        if registrar:
            code += declarations(AP_RESULT_HEADER.read_text())
            code += '\nint esp32qjs_wps_ap_result_status(uint32_t id, esp32_mquickjs_wifi_wps_ap_result_status_t *out){(void)id;(void)out;return ESP_ERR_INVALID_STATE;}\n'
        code += declarations(NATIVE_HEADER.read_text()) + '\n' + cred + '\n' + BOUNDARIES
        code += zero + native + function(patched, 'esp32qjs_wps_credential_valid')
        code += function(patched, 'wps_send_event_and_disable')
        code += function(patched, 'wifi_station_wps_timeout_body')
        code += function(patched, 'wifi_station_wps_timeout')
        compile_run(self, code + IMPLEMENTATIONS + main)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define CONFIG_IDF_TARGET_ESP32C5 1
typedef uint8_t u8;
typedef uint16_t u16;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_WIFI_STATE 0x3006
#define ESP_ERR_NOT_FINISHED 0x10a
#define SSID_MAX_LEN 32
#define ETH_ALEN 6
#define MAX_CRED_COUNT 3
#define WPS_OWNER_NONE 0
#define MSG_ERROR 0
#define wpa_printf(...) ((void)0)
#define WIFI_EVENT 7
#define OS_BLOCK UINT32_MAX
#define ELOOP_ALL_CTX ((void *)-1)
#define WPS_STATUS_DISABLE 0
#define WPS_STATUS_PENDING 1
#define WAIT_START 1
#define WPA_MESG 2
#define WIFI_APPIE_WPS_PR 1
#define WIFI_APPIE_WPS_AR 2
enum {WIFI_EVENT_STA_WPS_ER_SUCCESS=10,WIFI_EVENT_STA_WPS_ER_FAILED,
    WIFI_EVENT_STA_WPS_ER_TIMEOUT,WIFI_EVENT_STA_WPS_ER_PIN,WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP};
typedef enum {WPS_FAIL_REASON_NORMAL,WPS_FAIL_REASON_RECV_M2D,WPS_FAIL_REASON_RECV_DEAUTH,
    WPS_FAIL_REASON_MAX} wifi_event_sta_wps_fail_reason_t;
typedef void (*eloop_timeout_handler)(void *,void *);
typedef struct {struct {uint8_t ssid[32],password[64];} sta;} wifi_config_t;
'''

BOUNDARIES = r'''
static struct wps_sm {struct wps_credential creds[MAX_CRED_COUNT];u8 ap_cred_cnt;void *wps;int state;u8 bssid[6];} sample,*gWpsSm;
static bool on_native=true,fail_alloc,foreign_owner,dpp_enabled,success_pending;
static int enable_error,start_error,disconnect_error,type_error,disable_error,timer_error;
static unsigned allocations,enable_calls,start_calls,disconnect_calls,type_calls,disable_calls,posts;
static void *allocation,*timer_high,*timer_low;
static size_t allocation_size;
static eloop_timeout_handler timer_handler;
static void wifi_station_wps_success(void *a,void *b) {(void)a;(void)b;}
static void wifi_station_wps_timeout(void *a,void *b);
static void wifi_station_wps_msg_timeout(void *a,void *b) {(void)a;(void)b;}
static void wifi_wps_scan(void *a,void *b) {(void)a;(void)b;}
static void wifi_station_wps_eapol_start_handle(void *a,void *b) {(void)a;(void)b;}
static void wifi_station_wps_post_m8_timeout(void *a,void *b) {(void)a;(void)b;}
static struct {void *wpa_sm_wps_disable;} gWpaSm;
static wps_factory_information_t *s_factory_info;
static unsigned char s_wps_success_evt[64];
static unsigned char scan_storage[72];
void *g_scan=scan_storage;
static void *s_wps_sm_cb;
static int wps_get_type(void) {return WPS_TYPE_DISABLE;}
static int native_wps_status,associated_error;
static uint8_t associated_bssid[6];
static int wps_get_status(void) {return native_wps_status;}
static int esp_wifi_get_assoc_bssid_internal(uint8_t *out) {
    memcpy(out,associated_bssid,6);return associated_error;
}
static void bin_clear_free(void *p,size_t n) {
    assert(p==allocation && n==allocation_size && allocations==1);
    memset(p,0,n);free(p);allocation=NULL;allocations=0;
}
static unsigned cleanup_calls[ESP32QJS_WPS_CLEANUP_DONE],deinit_calls;
static int cleanup_fail_stage=-1,deinit_error;
static unsigned cancel_count,fail_cancel_at=UINT32_MAX;
static int cleanup_step(unsigned stage) {
    cleanup_calls[stage]++;
    return (int)stage==cleanup_fail_stage ? 300+(int)stage : ESP_OK;
}
static int esp_wifi_set_wps_start_flag_internal(bool flag) {
    assert(!flag);return cleanup_step(ESP32QJS_WPS_CLEANUP_START_FLAG);
}
static int esp_wifi_scan_stop(void) {return cleanup_step(ESP32QJS_WPS_CLEANUP_SCAN);}
static int eloop_cancel_timeout(eloop_timeout_handler f,void *a,void *b) {
    assert(f && a==ELOOP_ALL_CTX && b==ELOOP_ALL_CTX);
    int error=cleanup_step(ESP32QJS_WPS_CLEANUP_TIMERS);
    if(error)return -error;
    if(cancel_count==fail_cancel_at)return -301;
    cancel_count++;return 0;
}
static int esp_wifi_disarm_sta_connection_timer_internal(void) {
    return cleanup_step(ESP32QJS_WPS_CLEANUP_CONNECTION_TIMER);
}
static int esp_wifi_set_wps_cb_internal(void *cb) {
    assert(!cb);disable_calls++;
    int error=cleanup_step(ESP32QJS_WPS_CLEANUP_CALLBACKS);
    return error?error:disable_error;
}
static int esp_wifi_unset_appie_internal(int ie) {
    return cleanup_step(ie==WIFI_APPIE_WPS_PR ? ESP32QJS_WPS_CLEANUP_PROBE_IE : ESP32QJS_WPS_CLEANUP_ASSOC_IE);
}
static int wps_set_status(int status) {
    assert(status==WPS_STATUS_DISABLE);int error=cleanup_step(ESP32QJS_WPS_CLEANUP_STATUS);
    if(!error)native_wps_status=status;return error;
}
static void wps_set_owner(int owner) {assert(owner==WPS_OWNER_NONE);}
static void os_free(void *p) {free(p);}
static int wifi_station_wps_deinit(void) {
    deinit_calls++;if(!deinit_error)gWpsSm=NULL;return deinit_error;
}
static struct wps_sm *wps_sm_get(void) {return gWpsSm;}
bool current_task_is_wifi_task(void) {return on_native;}
static int wps_get_owner(void) {return foreign_owner?2:0;}
static bool is_dpp_enabled(void) {return dpp_enabled;}
static int esp_wifi_get_user_init_flag_internal(void) {return 1;}
static int wps_check_wifi_mode(void) {return ESP_OK;}
static void *os_zalloc(size_t n) {
    if(fail_alloc)return NULL;assert(!allocation);
    allocation=calloc(1,n);assert(allocation);allocations++;allocation_size=n;return allocation;
}
static int eloop_is_timeout_registered(eloop_timeout_handler f,void *a,void *b) {
    assert(f==wifi_station_wps_success && !a && !b);return success_pending;
}
static int eloop_register_timeout(unsigned s,unsigned us,eloop_timeout_handler f,void *a,void *b) {
    (void)s;(void)us;timer_handler=f;timer_high=a;timer_low=b;return timer_error;
}
static int esp_wifi_disconnect(void) {
    disconnect_calls++;int error=cleanup_step(ESP32QJS_WPS_CLEANUP_DISCONNECT);
    return error?error:disconnect_error;
}
static int wps_set_type(uint32_t type) {
    assert(type==WPS_TYPE_DISABLE);type_calls++;int error=cleanup_step(ESP32QJS_WPS_CLEANUP_TYPE);
    return error?error:type_error;
}
static int wifi_wps_disable_internal(void *ctx,void *data) {
    (void)ctx;(void)data;disable_calls++;if(!disable_error)gWpsSm=NULL;return disable_error;
}
static int esp_event_post(int base,int id,const void *p,size_t n,uint32_t wait) {
    (void)base;(void)id;(void)p;(void)n;(void)wait;posts++;return ESP_ERR_TIMEOUT;
}
static int wifi_wps_enable_internal(void *ctx,void *data);
static int wifi_station_wps_start(void *data,void *user);
'''

IMPLEMENTATIONS = r'''
static int wifi_wps_enable_internal(void *ctx,void *data) {
    assert(ctx==s_wps_native && data);enable_calls++;
    if(!enable_error) {
        gWpsSm=&sample;
        assert(esp32qjs_wps_native_pin("12345670",8));
    }
    return enable_error;
}
static int wifi_station_wps_start(void *data,void *user) {
    assert(data==s_wps_native && !user);start_calls++;
    esp32qjs_wps_register_timeout(120,0,wifi_station_wps_timeout);
    return start_error;
}
/* Fixture disposal separates cases; this is not a production release path or
 * evidence of scan/TX retirement. Production intentionally exposes no release. */
static void fixture_dispose(void) {
    assert(s_wps_native==allocation && allocations==1);
    forced_memzero(allocation,allocation_size);free(allocation);allocation=NULL;
    s_wps_native=NULL;atomic_store(&s_wps_native_held,false);allocations=0;gWpsSm=NULL;
    enable_error=start_error=disconnect_error=type_error=disable_error=timer_error=0;
    enable_calls=start_calls=disconnect_calls=type_calls=disable_calls=posts=0;
    cleanup_fail_stage=-1;deinit_calls=0;deinit_error=0;cancel_count=0;fail_cancel_at=UINT32_MAX;
    memset(cleanup_calls,0,sizeof(cleanup_calls));
    memset(scan_storage,0,sizeof(scan_storage));
    native_wps_status=associated_error=0;memset(associated_bssid,0,sizeof(associated_bssid));
    memset(&sample,0,sizeof(sample));
}
'''

MAIN = r'''
int main(void) {
    esp_wps_config_t config={.wps_type=WPS_TYPE_PIN};uint64_t id=0;
    esp32_mquickjs_wifi_wps_native_status_t status;
    esp32_mquickjs_wifi_wps_credentials_t credentials;
    uint8_t pin[8];
    on_native=false;assert(esp32qjs_wps_native_begin(&config,&id)==ESP_ERR_INVALID_STATE && !id);
    on_native=true;foreign_owner=true;
    assert(esp32qjs_wps_native_begin(&config,&id)==ESP_ERR_INVALID_STATE && !allocations);
    foreign_owner=false;dpp_enabled=true;
    assert(esp32qjs_wps_native_begin(&config,&id)==ESP_ERR_INVALID_STATE && !allocations);
    dpp_enabled=false;success_pending=true;
    assert(esp32qjs_wps_native_begin(&config,&id)==ESP_ERR_INVALID_STATE && !allocations);
    success_pending=false;fail_alloc=true;
    assert(esp32qjs_wps_native_begin(&config,&id)==ESP_ERR_NO_MEM && !id);
    fail_alloc=false;s_wps_native_last_identity=UINT64_C(0x123400000005);
    assert(esp32qjs_wps_native_begin(&config,&id)==ESP_OK && id==UINT64_C(0x123400000006));
    assert(esp32qjs_wps_native_held() && allocations==1);
    assert(esp32qjs_wps_native_pin_copy(id,pin)==ESP_OK && !memcmp(pin,"12345670",8));
    assert(esp32qjs_wps_native_pin_copy(id,pin)==ESP_OK); /* Copy is not consumption. */
    assert(esp32qjs_wps_native_pin_commit(id+1)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_wps_native_pin_commit(id)==ESP_OK);
    for(unsigned i=0;i<8;i++)assert(!s_wps_native->pin[i]);
    assert(esp32qjs_wps_native_pin_copy(id,pin)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_wps_unmanaged_disable(NULL,NULL)==ESP_ERR_INVALID_STATE && !type_calls);
    assert(esp32qjs_wps_native_start(id)==ESP_OK && start_calls==1);
    assert(esp32qjs_wps_native_start(id)==ESP_ERR_INVALID_STATE && start_calls==1);
    assert((uintptr_t)timer_high==0x1234 && (uintptr_t)timer_low==6);
    sample.wps=&sample;sample.state=WAIT_START;native_wps_status=WPS_STATUS_PENDING;
    memset(sample.bssid,0x22,6);memcpy(associated_bssid,sample.bssid,6);
    uint8_t foreign_bssid[6]={1,2,3,4,5,6};
    assert(esp32qjs_wps_rx_peer(&sample,sample.bssid));
    assert(!esp32qjs_wps_rx_peer(&sample,foreign_bssid));
    associated_error=55;assert(!esp32qjs_wps_rx_peer(&sample,sample.bssid));associated_error=0;
    associated_bssid[5]++;assert(!esp32qjs_wps_rx_peer(&sample,sample.bssid));associated_bssid[5]--;
    sample.state=99;assert(!esp32qjs_wps_rx_peer(&sample,sample.bssid));sample.state=WAIT_START;
    native_wps_status=WPS_STATUS_DISABLE;assert(!esp32qjs_wps_rx_peer(&sample,sample.bssid));
    assert(esp32qjs_wps_timer_exact(timer_high,timer_low));
    timer_handler(timer_high,(void *)(uintptr_t)5);
    assert(!s_wps_native->status.terminal_seen && !disable_calls);
    sample.ap_cred_cnt=3;
    for(unsigned i=0;i<3;i++) {
        sample.creds[i].ssid_len=32;sample.creds[i].key_len=64;
        memset(sample.creds[i].ssid,0x40+i,32);memset(sample.creds[i].key,0x60+i,64);
        sample.creds[i].auth_type=0x20;sample.creds[i].encr_type=8;
    }
    disconnect_error=201;
    assert(esp32qjs_wps_native_finish(&sample)==201 && !disable_calls);
    assert(esp32qjs_wps_native_credentials_copy(id,&credentials)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_wps_native_status(id,&status)==ESP_OK && status.terminal_seen && status.cleanup_error==201);
    assert(!esp32qjs_wps_timer_exact(timer_high,timer_low));
    disconnect_error=0;type_error=202;
    assert(esp32qjs_wps_native_stop_sdk()==202 && disconnect_calls==2 && !disable_calls);
    type_error=0;disable_error=203;
    assert(esp32qjs_wps_native_stop_sdk()==203 && disconnect_calls==2 && type_calls==2 && disable_calls==1);
    disable_error=0;
    assert(esp32qjs_wps_native_stop_sdk()==ESP_OK && disconnect_calls==2 && type_calls==2 && disable_calls==2);
    assert(esp32qjs_wps_native_credentials_copy(id,&credentials)==ESP_OK && credentials.count==3);
    assert(credentials.entries[2].ssid_length==32 && credentials.entries[2].password_length==64);
    assert(!memcmp(credentials.entries[2].password,sample.creds[2].key,64));
    assert(esp32qjs_wps_native_credentials_copy(id,&credentials)==ESP_OK);
    assert(esp32qjs_wps_native_credentials_commit(id+1)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_wps_native_credentials_commit(id)==ESP_OK);
    for(size_t i=0;i<sizeof(s_wps_native->credentials);i++)assert(!((u8 *)&s_wps_native->credentials)[i]);
    assert(esp32qjs_wps_native_close(id+1)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_wps_native_close(id)==ESP_OK && disable_calls==2 && !posts);
    assert(esp32qjs_wps_native_close(id)==ESP_OK && disable_calls==2);
    uint64_t next=0;assert(esp32qjs_wps_native_begin(&config,&next)==ESP_ERR_INVALID_STATE && !next);
    fixture_dispose();
    assert(esp32qjs_wps_native_begin(&config,&next)==ESP_OK && next>id);
    assert(esp32qjs_wps_native_close(id)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_wps_native_pin_copy(next,pin)==ESP_OK);
    assert(esp32qjs_wps_native_start(next)==ESP_OK);
    timer_handler(timer_high,timer_low);
    assert(esp32qjs_wps_native_status(next,&status)==ESP_OK && status.error==ESP_ERR_TIMEOUT);
    assert(status.terminal_seen && status.inputs_stopped && !status.pin_available && !posts);
    assert(status.callback_depth==0 && !status.sdk_state_retired && gWpsSm);
    assert(esp32qjs_wps_callback_enter());
    assert(esp32qjs_wps_native_retire_state(next)==ESP_ERR_INVALID_STATE && !deinit_calls);
    esp32qjs_wps_callback_leave();
    deinit_error=401;
    assert(esp32qjs_wps_native_retire_state(next)==401 && gWpsSm);
    deinit_error=0;
    assert(esp32qjs_wps_native_retire_state(next)==ESP_OK && !gWpsSm && deinit_calls==2);
    assert(esp32qjs_wps_native_retire_state(next)==ESP_OK && deinit_calls==2);
    assert(esp32qjs_wps_native_held()); /* SDK state retirement does not release result. */
    assert(!esp32qjs_wps_timer_exact(timer_high,timer_low));
    fixture_dispose();next=0;timer_error=-1;
    assert(esp32qjs_wps_native_begin(&config,&next)==ESP_OK);
    assert(esp32qjs_wps_native_start(next)==-1);
    assert(esp32qjs_wps_native_status(next,&status)==ESP_OK && status.terminal_seen && status.error==-1);
    assert(status.error_stage==ESP32QJS_WPS_STAGE_TIMER);
    assert(esp32qjs_wps_record_error(999,ESP32QJS_WPS_STAGE_CONNECT)==999);
    assert(s_wps_native->status.error==-1 && s_wps_native->status.error_stage==ESP32QJS_WPS_STAGE_TIMER);
    assert(esp32qjs_wps_native_close(next)==ESP_OK && !posts);
    fixture_dispose();next=0;enable_error=204;
    assert(esp32qjs_wps_native_begin(&config,&next)==204 && next);
    assert(esp32qjs_wps_native_held() && esp32qjs_wps_native_start(next)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_wps_native_close(next)==ESP_OK && !disconnect_calls);
    fixture_dispose();
    for(unsigned stage=0;stage<ESP32QJS_WPS_CLEANUP_DONE;stage++) {
        next=0;assert(esp32qjs_wps_native_begin(&config,&next)==ESP_OK);
        assert(esp32qjs_wps_native_start(next)==ESP_OK);
        cleanup_fail_stage=stage;
        assert(esp32qjs_wps_native_close(next)!=ESP_OK);
        assert(s_wps_native->status.cleanup_stage==stage && !s_wps_native->status.inputs_stopped);
        assert(esp32qjs_wps_native_retire_state(next)==ESP_ERR_INVALID_STATE && !deinit_calls);
        unsigned before[ESP32QJS_WPS_CLEANUP_DONE];memcpy(before,cleanup_calls,sizeof(before));
        cleanup_fail_stage=-1;
        assert(esp32qjs_wps_native_close(next)==ESP_OK);
        for(unsigned prior=0;prior<stage;prior++)assert(cleanup_calls[prior]==before[prior]);
        assert(cancel_count==6 && gWpsSm && !deinit_calls);
        assert(esp32qjs_wps_native_retire_state(next)==ESP_OK && deinit_calls==1);
        fixture_dispose();
    }
    next=0;assert(esp32qjs_wps_native_begin(&config,&next)==ESP_OK);
    fail_cancel_at=3;
    assert(esp32qjs_wps_native_close(next)==-301 && s_wps_native->timers_cancelled==3);
    fail_cancel_at=UINT32_MAX;
    assert(esp32qjs_wps_native_close(next)==ESP_OK && cancel_count==6);
    s_wps_native->status.callback_depth=UINT16_MAX;
    assert(!esp32qjs_wps_callback_enter() && s_wps_native->status.tracking_fault);
    assert(s_wps_native->status.callback_depth==UINT16_MAX);
    s_wps_native->status.callback_depth=0;
    assert(esp32qjs_wps_native_retire_state(next)==ESP_ERR_INVALID_STATE && !deinit_calls);
    fixture_dispose();next=0;
    scan_storage[60]=1;
    assert(esp32qjs_wps_native_begin(&config,&next)==ESP_ERR_INVALID_STATE && !allocations);
    scan_storage[60]=0;
    assert(esp32qjs_wps_native_begin(&config,&next)==ESP_OK);
    assert(esp32qjs_wps_native_start(next)==ESP_OK);
    assert(esp32qjs_wps_native_close(next)==ESP_OK);
    assert(esp32qjs_wps_native_retire_state(next)==ESP_OK);
    uint32_t revision=UINT32_MAX;
    scan_storage[70]=1;
    assert(esp32qjs_wps_native_checkpoint(next,&revision)==ESP_ERR_NOT_FINISHED);
    scan_storage[70]=0;
    assert(esp32qjs_wps_native_checkpoint(next,&revision)==ESP_OK);
    assert(esp32qjs_wps_callback_enter());esp32qjs_wps_callback_leave();
    assert(esp32qjs_wps_native_release(next,revision)==ESP_ERR_NOT_FINISHED && allocations==1);
    assert(esp32qjs_wps_native_checkpoint(next,&revision)==ESP_OK);
    assert(esp32qjs_wps_native_release(next+1,revision)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_wps_native_release(next,revision)==ESP_OK && !allocations && !esp32qjs_wps_native_held());
    assert(esp32qjs_wps_native_release(next,revision)==ESP_ERR_INVALID_STATE);
    next=0;s_wps_native_last_identity=UINT64_MAX;
    assert(esp32qjs_wps_native_begin(&config,&next)==ESP_ERR_NO_MEM && !allocations && !next);
    return 0;
}
'''
