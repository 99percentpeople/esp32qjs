"""Deferred production registrar timer identity/admission regressions.

AST only in the implementation wave. Execute with the concentrated Wi-Fi suite;
timer queue boundaries are controlled, production registry/arm/cancel/dispatch
and SDK PBC/PIN admission bodies remain the code under test.
"""
import os
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps import function
from patch_idf_wps_registrar import patch_source


class WpsRegistrarTimers(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to reviewed ESP-IDF')
        root = Path(sdk) / 'components/wpa_supplicant'
        cls.original = (root / 'src/wps/wps_registrar.c').read_bytes()
        cls.ap_original = (root / 'esp_supplicant/src/esp_hostpad_wps.c').read_bytes()
        cls.patched = patch_source('src/wps/wps_registrar.c', cls.original).decode()
        start = cls.patched.index('static struct wps_registrar *esp32qjs_wps_registrars;')
        end = cls.patched.index(function(cls.patched, 'esp32qjs_wps_registrar_timer_detach'))
        end += len(function(cls.patched, 'esp32qjs_wps_registrar_timer_detach'))
        cls.timers = cls.patched[start:end]

    def test_cancel_rearm_destroy_reuse_and_ticket_exhaustion(self):
        compile_run(self, TYPES + self.timers + r'''
int main(void) {
 struct wps_registrar a={0},b={0};a.esp32qjs_next=&b;esp32qjs_wps_registrars=&a;
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&a));
 struct pending old=last;uint64_t first=a.esp32qjs_timer_tickets[0];
 assert(last.ctx!=&a && last.ctx!=&b && last.arg!=&a && last.arg!=&b);
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&a));
 struct pending current=last;
 assert(a.esp32qjs_timer_tickets[0]>first);
 old.fn(old.ctx,old.arg);assert(!pbc_fired);
 current.fn(current.ctx,current.arg);assert(pbc_fired==1 && !a.esp32qjs_timer_tickets[0]);
 current.fn(current.ctx,current.arg);assert(pbc_fired==1);
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_set_selected_timeout,&b));
 struct pending other=last;
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&a));old=last;
 esp32qjs_wps_registrar_timer_detach(&a);
 assert(esp32qjs_wps_registrars==&b);
 memset(&a,0,sizeof(a));a.esp32qjs_next=&b;esp32qjs_wps_registrars=&a; /* Same allocator address, new native instance. */
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&a));current=last;
 old.fn(old.ctx,old.arg);assert(pbc_fired==1);
 other.fn(other.ctx,other.arg);assert(pin_fired==1);
 current.fn(current.ctx,current.arg);assert(pbc_fired==2);
 esp32qjs_wps_registrar_last_ticket=UINT32_MAX;
 assert(!esp32qjs_wps_registrar_timer_arm(1,0,wps_registrar_pbc_timeout,&a));
 assert((uintptr_t)last.ctx==1 && (uintptr_t)last.arg==0);last.fn(last.ctx,last.arg);assert(pbc_fired==3);
 esp32qjs_wps_registrar_last_ticket=UINT64_MAX-1;
 assert(!esp32qjs_wps_registrar_timer_arm(1,0,wps_registrar_pbc_timeout,&a));old=last;
 assert(a.esp32qjs_timer_tickets[0]==UINT64_MAX);
 assert(esp32qjs_wps_registrar_timer_arm(1,0,wps_registrar_pbc_timeout,&a)==ESP_ERR_NO_MEM);
 assert(a.esp32qjs_timer_tickets[0]==UINT64_MAX);old.fn(old.ctx,old.arg);assert(pbc_fired==4);
 esp32qjs_wps_registrar_timer_detach(&a);esp32qjs_wps_registrar_timer_detach(&b);
 assert(!esp32qjs_wps_registrars && esp32qjs_wps_registrar_last_ticket==UINT64_MAX);
 return 0;
}
''')

    def test_registration_failure_preserves_old_timer_and_consumes_identity(self):
        compile_run(self, TYPES + self.timers + r'''
int main(void) {
 struct wps_registrar reg={0};esp32qjs_wps_registrars=&reg;
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&reg));
 struct pending old=last;uint64_t ticket=reg.esp32qjs_timer_tickets[0];int cancels=cancel_calls;
 register_error=-73;
 assert(esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&reg)==-73);
 assert(reg.esp32qjs_timer_tickets[0]==ticket && cancel_calls==cancels);
 assert(esp32qjs_wps_registrar_last_ticket==ticket+1);
 old.fn(old.ctx,old.arg);assert(pbc_fired==1);
 esp32qjs_wps_registrar_timer_detach(&reg);
 assert(esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_pbc_timeout,&reg)==ESP_ERR_INVALID_STATE);
 return 0;
}
''')

    def test_original_pbc_ignores_timer_failure_patched_preserves_state(self):
        for patched in (False, True):
            src = self.patched if patched else self.original.decode()
            code = TYPES + (self.timers if patched else '') + PBC_BOUNDARIES
            code += function(src, 'wps_registrar_button_pushed')
            setup = 'esp32qjs_wps_registrars=&reg;' if patched else ''
            code += r'''
int main(void) {
 struct wps_registrar reg={.force_pbc_overlap=9};
''' + setup + r'''
 register_error=-73;int ret=wps_registrar_button_pushed(&reg,NULL);
''' + (r'''
 assert(ret==-73 && !reg.pbc && !reg.selected_registrar && reg.force_pbc_overlap==9);
 assert(!changed && !active_events && !authorized);
 register_error=0;assert(!wps_registrar_button_pushed(&reg,NULL));
 assert(reg.pbc && reg.selected_registrar && reg.esp32qjs_timer_tickets[0]);
 assert(changed==1 && active_events==1 && authorized==1);
''' if patched else r'''
 assert(ret==0 && reg.pbc && reg.selected_registrar);
 assert(changed==1 && active_events==1 && authorized==1);
''') + 'return 0;}\n'
            with self.subTest(patched=patched): compile_run(self, code)

    def test_pin_timer_failure_frees_new_secret_before_publication(self):
        code = TYPES + self.timers + PBC_BOUNDARIES + PIN_BOUNDARIES
        code += function(self.patched, 'wps_registrar_add_pin')
        compile_run(self, code + r'''
int main(void) {
 struct wps_registrar reg={.selected_registrar=1};esp32qjs_wps_registrars=&reg;
 const u8 pin[8]={'1','2','3','4','5','6','7','0'};
 assert(!esp32qjs_wps_registrar_timer_arm(120,0,wps_registrar_set_selected_timeout,&reg));
 uint64_t old=reg.esp32qjs_timer_tickets[1];
 register_error=-73;
 assert(wps_registrar_add_pin(&reg,NULL,NULL,pin,8,120)==-73);
 assert(!live && clears==1 && !linked && !invalidated && !changed && !authorized);
 assert(reg.esp32qjs_timer_tickets[1]==old && reg.selected_registrar==1);
 register_error=0;
 assert(!wps_registrar_add_pin(&reg,NULL,NULL,pin,8,120));
 assert(live==2 && linked==1 && invalidated==1 && changed==1 && authorized==1);
 assert(reg.esp32qjs_timer_tickets[1]>old);
 wps_free_pin(published);assert(!live && clears==2);
 esp32qjs_wps_registrar_timer_detach(&reg);
 return 0;
}
''')

    def test_start_propagates_mode_status_and_timer_errors(self):
        for patched in (False, True):
            source = patch_source('esp_supplicant/src/esp_hostpad_wps.c', self.ap_original).decode() if patched else self.ap_original.decode()
            code = TYPES + START_BOUNDARIES + function(source, 'wifi_ap_wps_start_internal')
            code += r'''
int main(void){
 mode_error=0x123;int ret=wifi_ap_wps_start_internal(NULL);
''' + ('assert(ret==0x123 && !status_calls);' if patched else 'assert(ret==ESP_ERR_WIFI_MODE);') + r'''
 mode_error=0;status_error=0x456;status_calls=0;ret=wifi_ap_wps_start_internal(NULL);
''' + ('assert(ret==0x456 && status_calls==1);' if patched else 'assert(ret==ESP_FAIL);') + r'''
 status_error=0;status_calls=0;start_error=-73;ret=wifi_ap_wps_start_internal(NULL);
''' + ('assert(ret==-73 && status_calls==2 && native_status==WPS_STATUS_DISABLE);' if patched else 'assert(ret==ESP_FAIL);') + r'''
 start_error=0;assert(wifi_ap_wps_start_internal(NULL)==ESP_OK);
 return 0;
}
'''
            if patched: code = code.replace('wifi_ap_wps_start_internal(NULL)', 'wifi_ap_wps_start_internal(NULL, 0)')
            with self.subTest(patched=patched): compile_run(self, code)


TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef void (*eloop_timeout_handler)(void*,void*);
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ETH_ALEN 6
#define WPS_UUID_LEN 16
#define WPS_PBC_WALK_TIME 120
#define os_memcpy memcpy
#define os_memset memset
#define MSG_DEBUG 0
#define wpa_printf(...) ((void)0)
#define wpa_hexdump(...) ((void)0)
#define wpa_hexdump_ascii_key(...) ((void)0)
struct dl_list{int unused;};
struct os_reltime{int sec;};
struct wps_registrar{struct wps_registrar *esp32qjs_next;uint64_t esp32qjs_timer_tickets[2];int pbc,selected_registrar,force_pbc_overlap;u8 p2p_dev_addr[6];void *wps;struct dl_list pins;};
struct pending{eloop_timeout_handler fn;void *ctx,*arg;};
static struct pending last;
static int register_error,cancel_calls,pbc_fired,pin_fired;
static int eloop_register_timeout(unsigned sec,unsigned usec,eloop_timeout_handler fn,void *ctx,void *arg){(void)sec;(void)usec;if(register_error)return register_error;last=(struct pending){fn,ctx,arg};return 0;}
static int eloop_cancel_timeout(eloop_timeout_handler fn,void *ctx,void *arg){(void)fn;(void)ctx;(void)arg;cancel_calls++;return 0;}
static void wps_registrar_pbc_timeout(void *ctx,void *arg){assert(ctx && !arg);pbc_fired++;}
static void wps_registrar_set_selected_timeout(void *ctx,void *arg){assert(ctx && !arg);pin_fired++;}
'''

PBC_BOUNDARIES = r'''
static int changed,active_events,authorized;
static int wps_registrar_pbc_overlap(struct wps_registrar *r,void *a,void *b){(void)r;(void)a;(void)b;return 0;}
static void wps_pbc_overlap_event(void *r){(void)r;assert(0);}
static void wps_registrar_add_authorized_mac(struct wps_registrar *r,const u8 *addr){(void)r;(void)addr;authorized++;}
static void wps_registrar_selected_registrar_changed(struct wps_registrar *r,int pw){(void)r;(void)pw;changed++;}
static void wps_pbc_active_event(void *r){(void)r;active_events++;}
'''

PIN_BOUNDARIES = r'''
#define PIN_EXPIRES 2
struct wps_uuid_pin{struct dl_list list;u8 uuid[16];int wildcard_uuid;u8 *pin;size_t pin_len;int flags;struct os_reltime expiration;u8 enrollee_addr[6];};
static int live,clears,linked,invalidated;
static struct wps_uuid_pin *published;
static void *os_zalloc(size_t n){void *p=calloc(1,n);assert(p);live++;return p;}
static void *os_memdup(const void *p,size_t n){void *q=os_zalloc(n);memcpy(q,p,n);return q;}
static void os_free(void *p){if(p){live--;free(p);}}
static void wps_free_pin(struct wps_uuid_pin *p){assert(p->pin_len==8);memset(p->pin,0,p->pin_len);clears++;os_free(p->pin);os_free(p);}
static void os_get_reltime(struct os_reltime *t){t->sec=1;}
static void wps_registrar_invalidate_unused(struct wps_registrar *r){(void)r;invalidated++;}
static void dl_list_add(struct dl_list *head,struct dl_list *entry){(void)head;published=(struct wps_uuid_pin*)entry;linked++;}
'''

START_BOUNDARIES = r'''
static bool esp32qjs_wps_ap_command_allowed(uint32_t id){return id==0;}
static bool current_task_is_wifi_task(void){return true;}
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){(void)p;return 0;}
static void *esp32qjs_wps_ap_result_context(uint32_t id){(void)id;return NULL;}
#define ESP_OK 0
#define ESP_FAIL -1
#define MSG_ERROR 0
#define ESP_ERR_WIFI_MODE 0x301
#define ESP_ERR_WIFI_WPS_SM 0x302
#define ESP_ERR_WIFI_WPS_TYPE 0x303
#define ESP_ERR_WIFI_STATE 0x304
#define WPS_TYPE_DISABLE 0
#define WPS_TYPE_PBC 1
#define WPS_TYPE_PIN 2
#define WPS_STATUS_DISABLE 0
#define WPS_STATUS_SCANNING 1
#define WPS_STATUS_PENDING 2
typedef enum {WIFI_MODE_NULL,WIFI_MODE_AP,WIFI_MODE_APSTA} wifi_mode_t;
enum wps_owner{WPS_OWNER_NONE,WPS_OWNER_ENROLLEE,WPS_OWNER_REGISTRAR};
struct protocol{u8 dev_password[8];};
struct wps_sm{struct protocol *wps;};
static struct protocol protocol;
static struct wps_sm owner={&protocol},*gWpsSm=&owner;
static int mode_error,status_error,start_error,status_calls,native_status;
static int esp_wifi_get_mode(wifi_mode_t *p){if(mode_error)return mode_error;*p=WIFI_MODE_AP;return 0;}
static enum wps_owner wps_get_owner(void){return WPS_OWNER_REGISTRAR;}
static int wps_get_type(void){return WPS_TYPE_PBC;}
static int wps_get_status(void){return native_status;}
static int wps_set_status(int state){status_calls++;if(status_error)return status_error;native_status=state;return 0;}
static int esp_wifi_get_user_init_flag_internal(void){return 1;}
static void *hostapd_get_hapd_data(void){return &owner;}
static int hostapd_wps_button_pushed(void *p,const u8 *arg){(void)p;(void)arg;return start_error;}
static int hostapd_wps_add_pin(void *p,const u8 *arg){(void)p;(void)arg;return start_error;}
'''
