"""Deferred fixed-SDK production DPP row conversion and installation transactions.

Allocator/driver boundaries are controlled; no independent test state machine.
These tests do not prove cryptographic negotiation or radio scheduling.
"""
import os
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]


def sdk_struct(text, name):
    end = text.index('} ' + name + ';') + len('} ' + name + ';')
    return text[text.rfind('typedef struct {', 0, end):end]


class DppConfigTransaction(unittest.TestCase):
    def test_complete_key_equality_failed_replacement_and_oversized_received_keys(self):
        sdk_path = os.environ.get('IDF_PATH')
        if not sdk_path:
            self.skipTest('Set IDF_PATH to the reviewed SDK')
        sys.path.insert(0, str(ROOT / 'scripts'))
        from patch_idf_dpp import patch_source, function
        sdk = Path(sdk_path)
        component = sdk / 'components/wpa_supplicant'
        public = (sdk / 'components/esp_wifi/include/esp_wifi_types_generic.h').read_text()
        source = patch_source('esp_supplicant/src/esp_dpp.c', (component / 'esp_supplicant/src/esp_dpp.c').read_bytes()).decode()
        code = TYPES + sdk_struct(public, 'esp_dpp_config_data_t') + BOUNDARIES
        for name in ('esp32qjs_dpp_config_valid', 'esp_dpp_stored_conf_matches_row',
                     'esp_dpp_conf_alloc_from_config_data', 'esp_supp_dpp_set_config',
                     'esp_dpp_process_config_obj', 'esp_dpp_handle_config_obj'):
            code += function(source, name)
        compile_run(self, code + MAIN)


TYPES = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#define MAX_SSID_LEN 32
#define MAX_PASSPHRASE_LEN 64
#define PMK_LEN 32
#define ESP_DPP_MAX_CONNECTOR_LEN 512
#define ESP_DPP_MAX_KEY_LEN 128
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define os_memchr memchr
#define os_memcpy memcpy
#define os_memcmp memcmp
#define os_strlen strlen
#define os_strnlen strnlen
#define wpa_printf(...) ((void)0)
#define wpa_hexdump_key(...) ((void)0)
#define ESP_DPP_AKM_DPP 1
#define ESP_DPP_AKM_PSK_SAE_DPP 6
typedef int esp_err_t;
enum dpp_akm{DPP_AKM_UNKNOWN,DPP_AKM_DPP,DPP_AKM_PSK,DPP_AKM_SAE,DPP_AKM_PSK_SAE,DPP_AKM_SAE_DPP,DPP_AKM_PSK_SAE_DPP};
struct wpabuf{size_t n;uint8_t bytes[];};
struct dpp_conf{char *connector;struct wpabuf *net_access_key,*c_sign_key;uint64_t net_access_key_expiry;size_t dpp_csign_len;uint8_t curr_chan;enum dpp_akm akm;};
struct dpp_config_store{struct dpp_conf *conf;};
struct dpp_config_obj{char *connector;uint8_t ssid[32],ssid_len;char passphrase[64];uint8_t psk[32];bool psk_set;enum dpp_akm akm;struct wpabuf *c_sign_key;};
struct dpp_authentication{struct wpabuf *net_access_key;uint64_t net_access_key_expiry;unsigned curr_chan;};
'''
BOUNDARIES = r'''
static bool esp32qjs_dpp_command_allowed_locked(void){return true;}
static struct dpp_config_store store;
static struct{bool dpp_deinit_pending;struct dpp_config_store *dpp_config_store;}s_dpp_ctx={false,&store};
static atomic_bool s_dpp_init_done=true,dpp_shutting_down;
static uint64_t s_dpp_config_revision;
static int fail_at,calls,live,notifies;static bool locked,driver_ready;
static int dpp_api_lock(void){assert(!locked);locked=true;return 0;}
static int dpp_api_unlock(void){assert(locked);locked=false;return 0;}
static void forced_memzero(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static void *os_malloc(size_t n){if(++calls==fail_at)return NULL;void *p=malloc(n);assert(p);live++;return p;}
static void *os_zalloc(size_t n){void *p=os_malloc(n);if(p)memset(p,0,n);return p;}
static void bin_clear_free(void *p,size_t n){if(p){forced_memzero(p,n);free(p);live--;}}
static struct wpabuf *wpabuf_alloc_copy(const void *data,size_t n){struct wpabuf *b=os_malloc(sizeof(*b)+n);if(b){b->n=n;memcpy(b->bytes,data,n);}return b;}
static void wpabuf_clear_free(struct wpabuf *b){if(b)bin_clear_free(b,sizeof(*b)+b->n);}
static size_t wpabuf_len(const struct wpabuf *b){return b->n;}
static const void *wpabuf_head(const struct wpabuf *b){return b->bytes;}
static void dpp_clear_confs(struct dpp_conf *c){if(c){if(c->connector)bin_clear_free(c->connector,strlen(c->connector)+1);wpabuf_clear_free(c->net_access_key);wpabuf_clear_free(c->c_sign_key);bin_clear_free(c,sizeof(*c));}}
static int dpp_akm_dpp(enum dpp_akm a){return a==DPP_AKM_DPP || a==DPP_AKM_SAE_DPP || a==DPP_AKM_PSK_SAE_DPP;}
static int dpp_akm_legacy(enum dpp_akm a){return a==DPP_AKM_PSK || a==DPP_AKM_SAE || a==DPP_AKM_PSK_SAE;}
static int dpp_akm_ver2(enum dpp_akm a){return a==DPP_AKM_SAE_DPP || a==DPP_AKM_PSK_SAE_DPP;}
static void esp_wifi_sta_notify_dpp_config_set_internal(bool value){assert(locked);driver_ready=value;notifies++;}
static size_t os_strlcpy(char *dst,const char *src,size_t n){size_t len=strlen(src);if(n){size_t m=len<n-1?len:n-1;memcpy(dst,src,m);dst[m]=0;}return len;}
static int wpa_snprintf_hex(char *out,size_t cap,const uint8_t *in,size_t n){if(cap<2*n+1)return -1;for(size_t i=0;i<n;i++)snprintf(out+2*i,cap-2*i,"%02x",in[i]);return n*2;}
'''
MAIN = r'''
int main(void){
 esp_dpp_config_data_t row={.connector="JWS",.connector_len=3,.net_access_key_len=2,.c_sign_key_len=2,.akm=1,.curr_chan=6,.net_access_key_expiry=99};
 row.net_access_key[0]=11;row.c_sign_key[0]=22;
 assert(!esp_supp_dpp_set_config(&row)&&driver_ready&&live==4&&s_dpp_config_revision==1);
 struct dpp_conf *old=store.conf;int old_calls=calls;
 assert(!esp_supp_dpp_set_config(&row)&&store.conf==old&&calls==old_calls&&s_dpp_config_revision==1);
 row.net_access_key[0]++;
 for(int nth=1;nth<=4;nth++){
  calls=0;fail_at=nth;assert(esp_supp_dpp_set_config(&row)==ESP_ERR_NO_MEM);
  assert(store.conf==old&&driver_ready&&live==4&&s_dpp_config_revision==1);
 }
 fail_at=0;assert(!esp_supp_dpp_set_config(&row)&&store.conf!=old&&s_dpp_config_revision==2);
 assert(((const uint8_t*)wpabuf_head(store.conf->net_access_key))[0]==12);
 row.c_sign_key[1]=33;assert(!esp_supp_dpp_set_config(&row)&&s_dpp_config_revision==3);
 row.net_access_key_expiry++;assert(!esp_supp_dpp_set_config(&row)&&s_dpp_config_revision==4);
 old=store.conf;row.connector[3]='x';assert(esp_supp_dpp_set_config(&row)==ESP_ERR_INVALID_ARG&&store.conf==old);row.connector[3]=0;
 row.net_access_key_len=129;assert(esp_supp_dpp_set_config(&row)==ESP_ERR_INVALID_ARG&&store.conf==old);row.net_access_key_len=2;
 s_dpp_ctx.dpp_deinit_pending=true;assert(esp_supp_dpp_set_config(NULL)==ESP_ERR_INVALID_STATE&&store.conf==old);s_dpp_ctx.dpp_deinit_pending=false;
 s_dpp_config_revision=UINT64_MAX;row.c_sign_key[0]++;assert(esp_supp_dpp_set_config(&row)==ESP_ERR_NO_MEM&&store.conf==old&&driver_ready);
 s_dpp_config_revision=4;assert(!esp_supp_dpp_set_config(NULL)&&!store.conf&&!live&&!driver_ready);
 uint8_t material[129]={1};struct wpabuf *key=wpabuf_alloc_copy(material,sizeof(material));assert(key);
 struct dpp_authentication auth={.net_access_key=key,.curr_chan=6};
 struct dpp_config_obj conf={.ssid={1},.ssid_len=1,.connector="JWS",.akm=DPP_AKM_DPP,.c_sign_key=key};
 memset(&row,0xff,sizeof(row));assert(esp_dpp_handle_config_obj(&auth,&conf,&row)<0);
 for(size_t i=0;i<sizeof(row);i++)assert(!((uint8_t*)&row)[i]);
 key->n=128;assert(!esp_dpp_handle_config_obj(&auth,&conf,&row)&&row.net_access_key_len==128&&row.c_sign_key_len==128);
 key->n=129;wpabuf_clear_free(key);assert(!live&&!locked);return 0;
}
'''
