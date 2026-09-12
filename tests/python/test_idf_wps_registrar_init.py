"""Deferred registrar credential and initialization failure regressions.

Executes fixed-SDK production function bodies with allocator/driver/EAP boundaries.
Original branches document ignored errors/borrowed-context free before correction.
Not RF, callback retirement or managed registrar Session proof. AST only during
implementation; execute with the concentrated Wi-Fi suite.
"""
import os
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps import function
from patch_idf_wps_registrar import OUTPUTS, patch_source


class WpsRegistrarInit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed ESP-IDF')
        cls.component = Path(sdk) / 'components/wpa_supplicant'
        cls.original = {p: (cls.component / p).read_bytes() for p in OUTPUTS.values()}
        cls.patched = {p: patch_source(p, s).decode() for p, s in cls.original.items()}

    def test_hash_drift_and_double_patch_reject(self):
        for path, source in self.original.items():
            for invalid in (source + b'\n', self.patched[path].encode()):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    patch_source(path, invalid)

    def test_credential_bounds_security_atomic_replacement_and_secret_clear(self):
        source = self.patched['src/ap/wps_hostapd.c']
        code = TYPES + function(source, 'hostapd_wps_config_ap')
        compile_run(self, code + CREDENTIAL_CASES)

    def test_original_host_init_reports_success_after_eap_failure_and_frees_borrowed_context(self):
        source = self.original['src/ap/wps_hostapd.c'].decode()
        code = TYPES + HOST_BOUNDARIES + function(source, 'hostapd_init_wps')
        compile_run(self, code + ORIGINAL_HOST_CASES)

    def test_host_init_returns_failure_and_preserves_caller_context(self):
        source = self.patched['src/ap/wps_hostapd.c']
        code = TYPES + HOST_BOUNDARIES + function(source, 'hostapd_init_wps')
        compile_run(self, code + PATCHED_HOST_CASES)

    def test_registrar_pointer_failure_keeps_driver_ie_error(self):
        source = self.patched['src/ap/wps_hostapd.c']
        boundary = HOST_BOUNDARIES.replace('if(++step==fail_step)return NULL;registrars++;',
            'if(++step==fail_step){esp32qjs_wps_host_ie_error=-83;return NULL;}registrars++;')
        compile_run(self, TYPES + boundary + function(source, 'hostapd_init_wps') + r'''
int main(void){
 struct hostapd_bss_config conf={0};struct hostapd_data h={.conf=&conf};
 struct wps_context ctx={0};struct wps_data data={.wps=&ctx};
 fail_step=2;assert(hostapd_init_wps(&h,&data,&ctx)==-83);
 assert(!registered && !registrars && !borrowed_frees && !ie_clears);
 return 0;
}
''')

    def test_update_uses_matching_live_protocol_data_and_does_not_publish_failed_capture(self):
        relative = 'src/ap/wps_hostapd.c'
        for patched in (False, True):
            source = self.patched[relative] if patched else self.original[relative].decode()
            code = TYPES + UPDATE_BOUNDARIES + function(source, 'hostapd_update_wps')
            main = UPDATE_CASES.replace('PATCHED', '1' if patched else '0')
            with self.subTest(patched=patched): compile_run(self, code + main)

    def test_enable_preserves_mode_factory_and_initializer_errors(self):
        relative = 'esp_supplicant/src/esp_hostpad_wps.c'
        for patched in (False, True):
            source = self.patched[relative] if patched else self.original[relative].decode()
            code = TYPES + ENABLE_BOUNDARIES + function(source, 'wifi_ap_wps_enable_internal')
            main = ENABLE_CASES.replace('PATCHED', '1' if patched else '0')
            if patched: main = main.replace('wifi_ap_wps_enable_internal(&config)', 'wifi_ap_wps_enable_internal(&config, 0)')
            with self.subTest(patched=patched): compile_run(self, code + main)

    def test_native_init_retains_allocations_until_checked_deinit(self):
        relative = 'esp_supplicant/src/esp_hostpad_wps.c'
        for patched in (False, True):
            source = self.patched[relative] if patched else self.original[relative].decode()
            code = TYPES + NATIVE_BOUNDARIES + function(source, 'wifi_ap_wps_init')
            if patched: code += function(source, 'wifi_ap_wps_deinit')
            code += '\nint main(void){\n'
            code += 'esp_wps_config_t config={0};injected_host_error=ESP_ERR_NO_MEM;\n'
            code += 'int ret=wifi_ap_wps_init(&config);\n'
            if patched:
                code += 'assert(ret==ESP_ERR_NO_MEM && gWpsSm && !data_freed && !context_freed);\n'
                code += 'assert(wifi_ap_wps_deinit()==ESP_OK && !gWpsSm && data_freed && context_freed);\n'
            else:
                code += 'assert(ret==ESP_OK && gWpsSm && !data_freed && !context_freed);\n'
            code += 'return 0;}\n'
            with self.subTest(patched=patched): compile_run(self, code)


TYPES = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define WPA_PROTO_WPA 1
#define WPA_PROTO_RSN 2
#define WPA_KEY_MGMT_PSK 1
#define WPA_KEY_MGMT_PSK_SHA256 2
#define WPA_KEY_MGMT_SAE 4
#define WPA_CIPHER_TKIP 1
#define WPA_CIPHER_CCMP 2
#define WPS_AUTH_OPEN 1
#define WPS_AUTH_WPAPSK 2
#define WPS_AUTH_WPA2PSK 0x20
#define WPS_ENCR_NONE 1
#define WPS_ENCR_TKIP 4
#define WPS_ENCR_AES 8
#define ETH_ALEN 6
#define WPS_UUID_LEN 16
#define WPS_STATE_CONFIGURED 2
#define MSG_DEBUG 0
#define MSG_ERROR 0
#define os_memcpy memcpy
#define os_memset memset
#define os_strlen strlen
#define os_strnlen strnlen
#define os_free free
#define wpa_printf(...) ((void)0)
static bool oom;
static unsigned clears,freed;
static void forced_memzero(void *p,size_t n){assert(p);volatile uint8_t *v=p;while(n--)*v++=0;clears++;}
static void bin_clear_free(void *p,size_t n){if(p){forced_memzero(p,n);for(size_t i=0;i<n;i++)assert(!((uint8_t*)p)[i]);free(p);freed++;}}
static void *os_malloc(size_t n){return oom?NULL:malloc(n);}
static void *os_zalloc(size_t n){return oom?NULL:calloc(1,n);}
struct wps_credential {u8 ssid[32],key[64];size_t ssid_len,key_len;uint16_t auth_type,encr_type;};
struct wps_device_data {int unused,rf_bands;};
struct wps_context {u8 ssid[32],uuid[16];size_t ssid_len;struct wps_device_data dev;void *registrar;void (*event_cb)(void);int (*rf_band_cb)(void);void *cb_ctx;int wps_state,ap_setup_locked,ap;};
struct wps_data {struct wps_context *wps;struct wps_credential *use_cred;int registrar;u8 *dev_password;};
struct hostapd_ssid {u8 ssid[32];size_t ssid_len;char *wpa_passphrase;};
struct wpa_auth_config {int wpa,wpa_key_mgmt,wpa_pairwise,rsn_pairwise;};
struct hostapd_bss_config {struct hostapd_ssid ssid;};
struct wpa_authenticator {struct wpa_auth_config conf;};
struct hostapd_data {struct hostapd_bss_config *conf;struct wpa_authenticator *wpa_auth;struct wps_context *wps;void *eapol_auth;};
struct wps_registrar_config {int (*set_ie_cb)(void);void (*reg_success_cb)(void);void *cb_ctx;int disable_auto_conf;};
'''

CREDENTIAL_CASES = r'''
int main(void){
 struct hostapd_bss_config conf={0};struct wpa_authenticator auth={.conf={WPA_PROTO_RSN,WPA_KEY_MGMT_PSK,WPA_CIPHER_TKIP,WPA_CIPHER_CCMP}};
 struct wps_context context={0};struct wps_data data={.wps=&context};struct hostapd_data hapd={.conf=&conf,.wpa_auth=&auth};
 char pass[66];memset(pass,'a',64);pass[64]=0;conf.ssid.wpa_passphrase=pass;conf.ssid.ssid_len=32;memset(conf.ssid.ssid,0xff,32);conf.ssid.ssid[1]=0;
 assert(hostapd_wps_config_ap(&hapd,NULL)==ESP_ERR_INVALID_ARG);
 assert(hostapd_wps_config_ap(&hapd,&data)==ESP_OK && data.use_cred && data.use_cred->key_len==64 && data.use_cred->ssid_len==32);
 assert(!memcmp(data.use_cred->ssid,conf.ssid.ssid,32) && data.use_cred->encr_type==WPS_ENCR_AES && data.use_cred->auth_type==WPS_AUTH_WPA2PSK);
 struct wps_credential *previous=data.use_cred;struct wps_context saved=context;struct wps_credential old=*previous;
 for(int mode=0;mode<7;mode++){
   size_t ssid_len=conf.ssid.ssid_len;int auth_mode=auth.conf.wpa_key_mgmt,cipher=auth.conf.rsn_pairwise;
   if(mode==0)oom=true;
   if(mode==1)conf.ssid.ssid_len=33;
   if(mode==2)auth.conf.wpa_key_mgmt=WPA_KEY_MGMT_SAE;
   if(mode==3)auth.conf.rsn_pairwise=0;
   if(mode==4){memset(pass,'a',65);pass[65]=0;}
   if(mode==5)pass[2]='g'; /* 64-byte keys must be hex. */
   if(mode==6)pass[7]=0;
   unsigned before=clears;assert(hostapd_wps_config_ap(&hapd,&data)!=ESP_OK && clears>before);
   assert(data.use_cred==previous && !memcmp(&context,&saved,sizeof(saved)) && !memcmp(data.use_cred,&old,sizeof(old)));
   oom=false;conf.ssid.ssid_len=ssid_len;auth.conf.wpa_key_mgmt=auth_mode;auth.conf.rsn_pairwise=cipher;memset(pass,'a',64);pass[64]=0;
 }
 conf.ssid.ssid_len=3;memcpy(conf.ssid.ssid,"new",3);strcpy(pass,"new-pass");unsigned before=freed;
 assert(hostapd_wps_config_ap(&hapd,&data)==ESP_OK && freed==before+1 && data.use_cred!=previous);
 assert(data.use_cred->ssid_len==3 && data.use_cred->key_len==8 && context.ssid_len==3);
 for(unsigned i=3;i<32;i++)assert(!data.use_cred->ssid[i] && !context.ssid[i]);
 for(unsigned i=8;i<64;i++)assert(!data.use_cred->key[i]);
 auth.conf.wpa=WPA_PROTO_WPA|WPA_PROTO_RSN;auth.conf.wpa_key_mgmt=WPA_KEY_MGMT_PSK|WPA_KEY_MGMT_SAE;
 assert(!hostapd_wps_config_ap(&hapd,&data) && data.use_cred->auth_type==(WPS_AUTH_WPAPSK|WPS_AUTH_WPA2PSK));
 assert(data.use_cred->encr_type==(WPS_ENCR_AES|WPS_ENCR_TKIP));
 bin_clear_free(data.use_cred,sizeof(*data.use_cred));return 0;
}
'''

HOST_BOUNDARIES = r'''
static uint32_t esp32qjs_wps_host_methods_identity;
static int esp32qjs_wps_host_ie_error;
#define EAP_VENDOR_IETF 0
#define EAP_TYPE_IDENTITY 1
#define EAP_VENDOR_WFA 2
#define EAP_VENDOR_TYPE_WSC 3
static int fail_step,step,borrowed_frees,registered,registrars,ie_clears;
static uint32_t esp32qjs_wps_ap_result_identity(const void *hapd){(void)hapd;return 1;}
static int hostapd_wps_config_ap(struct hostapd_data *hapd,struct wps_data *data){(void)hapd;(void)data;return ++step==fail_step?ESP_ERR_NO_MEM:ESP_OK;}
static void hostapd_wps_event_cb(void){}
static int hostapd_wps_rf_band_cb(void){return 1;}
static int esp32qjs_wps_ap_rf_band_read(int *band){*band=1;return ESP_OK;}
static int hostapd_wps_set_ie_cb(void){return 0;}
static void hostapd_wps_reg_success_cb(void){}
static void *eap_server_get_eap_method(int vendor,int method){(void)vendor;(void)method;return NULL;}
static void *wps_registrar_init(struct wps_context *ctx,struct wps_registrar_config *cfg){(void)ctx;(void)cfg;if(++step==fail_step)return NULL;registrars++;return malloc(1);}
static void wps_registrar_deinit(void *p){if(p){assert(registrars);registrars--;free(p);}}
static int eap_server_identity_register(void){if(++step==fail_step)return -1;registered++;return 0;}
static int eap_server_wsc_register(void){if(++step==fail_step)return -1;registered++;return 0;}
static void eap_server_unregister_methods(void){registered=0;}
static int ieee802_1x_init(struct hostapd_data *hapd){(void)hapd;return ++step==fail_step?-1:0;}
static void hostapd_wps_clear_ies(struct hostapd_data *hapd,int only){(void)hapd;(void)only;ie_clears++;}
static void hostapd_free_wps(struct wps_context *ctx){(void)ctx;borrowed_frees++;}
'''
ORIGINAL_HOST_CASES = r'''
int main(void){
 struct hostapd_bss_config conf={0};struct hostapd_data h={.conf=&conf};struct wps_context ctx={0};struct wps_data data={.wps=&ctx};
 fail_step=5;assert(hostapd_init_wps(&h,&data,&ctx)==0 && h.wps==&ctx); /* EAPOL failure discarded. */
 wps_registrar_deinit(ctx.registrar);memset(&ctx,0,sizeof(ctx));h.wps=NULL;step=registered=0;
 fail_step=2;assert(hostapd_init_wps(&h,&data,&ctx)==-1 && borrowed_frees==1);
 return 0;
}
'''
PATCHED_HOST_CASES = r'''
int main(void){
 for(int failure=1;failure<=5;failure++){
   struct hostapd_bss_config conf={0};struct hostapd_data h={.conf=&conf};struct wps_context ctx={0};struct wps_data data={.wps=&ctx};
   fail_step=failure;step=0;
   assert(hostapd_init_wps(&h,&data,&ctx)!=ESP_OK);
   assert(!h.wps && !ctx.registrar && !registered && !registrars && !borrowed_frees);
 }
 struct hostapd_bss_config conf={0};struct hostapd_data h={.conf=&conf};struct wps_context ctx={0};struct wps_data data={.wps=&ctx};
 step=fail_step=0;assert(hostapd_init_wps(&h,&data,&ctx)==ESP_OK && h.wps==&ctx && registered==2 && registrars==1);
 wps_registrar_deinit(ctx.registrar);eap_server_unregister_methods();return 0;
}
'''
NATIVE_BOUNDARIES = r'''
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){return p!=NULL;}
static bool esp32qjs_wps_ap_result_deinit_allowed(void *p){return p!=NULL;}
static void esp32qjs_wps_ap_result_cleanup_error(void *p,int e){(void)p;(void)e;}
static int esp32qjs_wps_ap_hostap_release(void *p){return p?ESP_OK:ESP_ERR_INVALID_STATE;}
static int esp32qjs_wps_ap_result_bind(void *p){(void)p;return ESP_OK;}
static void esp32qjs_wps_ap_result_detach(void *p,int err){(void)p;(void)err;}
static int esp32qjs_wps_ap_result_event(void *p,int event,const void *data,size_t n){(void)p;(void)event;(void)data;(void)n;return ESP_OK;}
#define WSC_ID_REGISTRAR_LEN 3
#define WSC_ID_REGISTRAR "reg"
#define WIFI_IF_AP 1
#define WPS_TYPE_PIN 2
#define WIFI_EVENT 1
#define WIFI_EVENT_AP_WPS_RG_PIN 2
#define OS_BLOCK 0
struct wps_sm {u8 ownaddr[6],identity[32],uuid[16];size_t identity_len;struct wps_context *wps_ctx;struct wps_device_data *dev;struct wps_data *wps;};
struct wps_config {int registrar;struct wps_context *wps;u8 pin[9];};
typedef struct {u8 pin[9];} esp_wps_config_t;
typedef struct {u8 pin_code[8];} wifi_event_sta_wps_er_pin_t;
static struct wps_sm *gWpsSm;static struct hostapd_data h;
static int injected_host_error;static bool data_freed,context_freed;
static struct hostapd_data *hostapd_get_hapd_data(void){return &h;}
static int esp_wifi_get_macaddr_internal(int iface,u8 mac[6]){(void)iface;memset(mac,2,6);return 0;}
static int wps_dev_init(void){gWpsSm->dev=&gWpsSm->wps_ctx->dev;return 0;}
static int wps_init_cfg_pin(struct wps_config *cfg){(void)cfg;return 0;}
static struct wps_data *wps_init(struct wps_config *cfg){struct wps_data *d=calloc(1,sizeof(*d));d->wps=cfg->wps;return d;}
static int hostapd_init_wps(struct hostapd_data *hapd,struct wps_data *data,struct wps_context *ctx){(void)hapd;(void)data;(void)ctx;return injected_host_error;}
static int wps_get_type(void){return 1;}
static void esp_event_post(int base,int id,const void *p,size_t n,int wait){(void)base;(void)id;(void)p;(void)n;(void)wait;}
static void wps_deinit(struct wps_data *data){assert(!context_freed && data->wps==gWpsSm->wps_ctx);data_freed=true;free(data);}
static int wps_dev_deinit(struct wps_device_data *dev){assert(data_freed && !context_freed && dev==&gWpsSm->wps_ctx->dev);return 0;}
static void native_clear_free(void *p,size_t n){if(gWpsSm && p==gWpsSm->wps_ctx){assert(data_freed);context_freed=true;}bin_clear_free(p,n);}
#define bin_clear_free native_clear_free
'''

UPDATE_BOUNDARIES = r'''
struct wps_sm {struct wps_context *wps_ctx;struct wps_data *wps;};
static struct wps_sm sm;
static unsigned captures,null_captures,ie_updates;
static int capture_error;
static struct wps_sm *wps_sm_get(void){return &sm;}
static int hostapd_wps_config_ap(struct hostapd_data *hapd,struct wps_data *data){(void)hapd;captures++;if(!data){null_captures++;return ESP_ERR_INVALID_ARG;}assert(data==sm.wps);return capture_error;}
static void wps_registrar_update_ie(void *reg){(void)reg;ie_updates++;}
'''
UPDATE_CASES = r'''
int main(void){
 struct hostapd_bss_config conf={0};struct hostapd_data h={.conf=&conf};struct wps_context ctx={0};struct wps_data data={.wps=&ctx};
 h.wps=&ctx;sm.wps=&data;sm.wps_ctx=&ctx;ctx.ssid_len=3;memcpy(ctx.ssid,"old",3);conf.ssid.ssid_len=3;memcpy(conf.ssid.ssid,"new",3);
 hostapd_update_wps(&h);assert(captures==1 && null_captures==(PATCHED?0:1) && ie_updates==1);
 if(PATCHED){
   unsigned old_updates=ie_updates;capture_error=ESP_ERR_NO_MEM;hostapd_update_wps(&h);assert(ie_updates==old_updates && !memcmp(ctx.ssid,"old",3));
   unsigned old_captures=captures;sm.wps_ctx=NULL;hostapd_update_wps(&h);assert(captures==old_captures);
 }
 return 0;
}
'''

ENABLE_BOUNDARIES = r'''
static bool esp32qjs_wps_ap_command_allowed(uint32_t id){return id==0;}
static bool current_task_is_wifi_task(void){return true;}
static struct hostapd_data enable_hapd;
static void *hostapd_get_hapd_data(void){return &enable_hapd;}
static int esp32qjs_wps_ap_result_bind(void *p){return p?ESP_OK:ESP_ERR_INVALID_STATE;}
static void esp32qjs_wps_ap_init_failed(struct hostapd_data *h,int error){assert(h==&enable_hapd && error);}
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){(void)p;return 0;}
static void *esp32qjs_wps_ap_result_context(uint32_t id){(void)id;return NULL;}
static bool esp32qjs_wps_ap_result_can_bind(void){return true;}
typedef int wifi_mode_t;
#define WIFI_MODE_NULL 0
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3
#define WIFI_AUTH_OPEN 0
#define WPS_TYPE_DISABLE 0
#define WPS_TYPE_PBC 1
#define WPS_TYPE_PIN 2
#define WPS_STATUS_DISABLE 0
#define ESP_ERR_WIFI_STATE 41
#define ESP_ERR_WIFI_MODE 42
#define ESP_ERR_WIFI_WPS_TYPE 43
enum wps_owner {WPS_OWNER_NONE,WPS_OWNER_ENROLLEE,WPS_OWNER_REGISTRAR};
typedef struct {int wps_type;} esp_wps_config_t;
static int mode_error,factory_error,init_error,initialized;
static int esp_wifi_get_user_init_flag_internal(void){return 1;}
static int esp_wifi_get_mode(wifi_mode_t *mode){*mode=WIFI_MODE_AP;return mode_error;}
static int esp_wifi_ap_get_prof_authmode_internal(void){return 2;}
static enum wps_owner wps_get_owner(void){return WPS_OWNER_NONE;}
static void wps_set_owner(enum wps_owner owner){assert(owner==WPS_OWNER_REGISTRAR);}
static int wps_set_factory_info(const esp_wps_config_t *config){(void)config;return factory_error;}
static int wps_set_type(int type){(void)type;return 0;}
static int wps_set_status(int status){(void)status;return 0;}
static int wifi_ap_wps_init(const esp_wps_config_t *config){(void)config;initialized++;return init_error;}
'''
ENABLE_CASES = r'''
int main(void){
 esp_wps_config_t config={.wps_type=WPS_TYPE_PBC};
 mode_error=101;assert(wifi_ap_wps_enable_internal(&config)==(PATCHED?101:ESP_FAIL) && !initialized);mode_error=0;
 factory_error=102;assert(wifi_ap_wps_enable_internal(&config)==(PATCHED?102:ESP_FAIL) && !initialized);factory_error=0;
 init_error=103;assert(wifi_ap_wps_enable_internal(&config)==(PATCHED?103:ESP_FAIL) && initialized==1);init_error=0;
 assert(wifi_ap_wps_enable_internal(&config)==ESP_OK && initialized==2);return 0;
}
'''
