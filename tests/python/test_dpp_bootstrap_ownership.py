"""Deferred production bootstrap parsing/job ownership; SDK/eloop boundaries.

The 32-bit ABI assertion is omitted only for the host's wider uintptr_t. Numeric
high/low identities still exercise the production code. This is not RF proof.
"""
from pathlib import Path
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
PART = ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp/esp32_mquickjs_dpp_bootstrap.inc'


class DppBootstrapOwnership(unittest.TestCase):
    def test_strict_input_job_cancellation_stale_ticket_and_secret_release(self):
        source = PART.read_text().replace('_Static_assert(sizeof(uintptr_t) == 4, "review DPP eloop ticket encoding");', '')
        header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_dpp_result.h').read_text()
        limits = '\n'.join(line for line in header.splitlines() if line.startswith('#define ESP32QJS_DPP_')) + '\n'
        compile_run(self, BOUNDARIES + limits + source + MAIN)


BOUNDARIES = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#define ESP_DPP_MAX_CHAN_COUNT 5
#define ETH_ALEN 6
#define ESP_OK 0
#define ESP_ERR_NOT_SUPPORTED 1
#define ESP_ERR_DPP_INVALID_LIST 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_NOT_FINISHED 4
#define ESP_ERR_INVALID_STATE 5
#define ESP_ERR_DPP_FAILURE 6
#define ESP_ERR_INVALID_SIZE 7
#define ESP_ERR_NO_MEM 8
#define WIFI_IF_STA 0
#define WIFI_EVENT 1
#define WIFI_EVENT_DPP_URI_READY 2
#define os_strnlen strnlen
#define os_strlen strlen
#define os_snprintf snprintf
#define os_memcpy memcpy
#define CHANNEL_TO_BIT_NUMBER(c) (((c)>=1 && (c)<=14)?(c):0)
typedef int esp_err_t;
typedef uint8_t u8;
typedef enum {DPP_BOOTSTRAP_QR_CODE,DPP_BOOTSTRAP_PKEX}esp_supp_dpp_bootstrap_t;
struct dpp_bootstrap_params_t {int type;uint8_t chan_list[14],num_chan,mac[6];};
struct dpp_global{int dummy;};
typedef struct{uint32_t uri_data_len;char uri[];}wifi_event_dpp_uri_ready_t;
static struct dpp_global global;
static struct{struct dpp_bootstrap_params_t bootstrap_params;struct dpp_global *dpp_global;void *dpp_auth;int id;bool dpp_deinit_pending,bootstrap_done,dpp_listen_ongoing;}s_dpp_ctx;
static atomic_bool s_dpp_init_done=true,dpp_shutting_down,roc_in_progress;
static bool locked,queue_fail,cancel_fail;
static struct {struct {bool retained;uint32_t observation_drops;}status;} result;
#define s_dpp_result (&result)
static esp_err_t esp32qjs_dpp_result_uri_locked(const char *uri,size_t length){assert(uri&&length);return 0;}
static void esp32qjs_dpp_result_count(uint32_t *value){if(*value!=UINT32_MAX)++*value;}
static int mac_error,post_error,fail_at,calls,live,starts,events,failures,removed;
static void (*scheduled)(void*,void*);static void *scheduled_high,*scheduled_low;
static esp_err_t dpp_api_lock(void){assert(!locked);locked=true;return 0;}
static esp_err_t dpp_api_unlock(void){assert(locked);locked=false;return 0;}
static void forced_memzero(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static void *os_malloc(size_t n){if(++calls==fail_at)return NULL;void *p=malloc(n);assert(p);live++;return p;}
static void *os_zalloc(size_t n){void *p=os_malloc(n);if(p)memset(p,0,n);return p;}
static void os_free(void *p){if(p){live--;free(p);}}
static void bin_clear_free(void *p,size_t n){if(p){forced_memzero(p,n);for(size_t i=0;i<n;i++)assert(!((uint8_t*)p)[i]);os_free(p);}}
static u8 get_operating_class(unsigned c,int offset){(void)offset;return c<=14?81:0;}
static int hexstr2bin(const char *s,uint8_t *out,size_t n){for(size_t i=0;i<n;i++){unsigned a,b;char x=s[2*i],y=s[2*i+1];if(x>='0'&&x<='9')a=x-'0';else if(x>='a'&&x<='f')a=x-'a'+10;else return -1;if(y>='0'&&y<='9')b=y-'0';else if(y>='a'&&y<='f')b=y-'a'+10;else return -1;out[i]=16*a+b;}return 0;}
static int esp_wifi_get_mac(int iface,uint8_t *out){assert(locked && iface==0);if(mac_error)return mac_error;memset(out,2,6);return 0;}
static int eloop_register_timeout(int s,int us,void (*fn)(void*,void*),void *a,void *b){assert(locked&&!scheduled&&!s&&!us);if(queue_fail)return -1;scheduled=fn;scheduled_high=a;scheduled_low=b;return 0;}
static int eloop_cancel_timeout(void (*fn)(void*,void*),void *a,void *b){assert(locked);if(cancel_fail)return 0;assert(scheduled==fn&&a==scheduled_high&&b==scheduled_low);scheduled=NULL;return 1;}
int esp32qjs_dpp_bootstrap_generate(struct dpp_global *g,const char *channels,const u8 *mac,const u8 *key,size_t key_length,const char *info){
 assert(locked&&g==&global&&!strcmp(channels,"81/1,81/6,81/11")&&mac[0]==2);
 assert(key&&key_length==51&&key[0]==0x30&&key[7]==0x11&&key[50]==0x07&&!strcmp(info,"name key=untrusted type=pkex"));starts++;return 100+starts;
}
static const char *dpp_bootstrap_get_uri(struct dpp_global *g,int id){assert(locked&&g==&global&&id>100);return "DPP:K:example;;";}
static void dpp_bootstrap_remove(struct dpp_global *g,const char *id){assert(locked&&g==&global&&atoi(id)>=0);removed++;}
static int esp_event_post(int base,int event,const void *data,size_t n,int wait){
 const wifi_event_dpp_uri_ready_t *e=data;assert(locked&&base==1&&event==2&&!wait);
 assert(s_dpp_ctx.bootstrap_done&&s_dpp_ctx.id>100&&n==sizeof(*e)+e->uri_data_len&&!e->uri[e->uri_data_len-1]);events++;return post_error;
}
static void dpp_post_dpp_failed_event(uint32_t error){assert(!locked&&error);failures++;}
'''

MAIN = r'''
static const char *key="30310201010420" "1111111111111111111111111111111111111111111111111111111111111111" "a00a06082a8648ce3d030107";
static int submit(void){return esp_supp_dpp_bootstrap_gen("1,6,11",DPP_BOOTSTRAP_QR_CODE,key,"name key=untrusted type=pkex");}
static void dispatch(void){assert(scheduled);void (*fn)(void*,void*)=scheduled;scheduled=NULL;fn(scheduled_high,scheduled_low);}
int main(void){
 assert(!esp32qjs_dpp_bootstrap_validate("1,6,11",key,"valid"));
 assert(esp32qjs_dpp_bootstrap_validate("1,1",key,NULL)==ESP_ERR_DPP_INVALID_LIST);
 assert(!calls&&!live&&!scheduled&&!locked);
 s_dpp_ctx.dpp_global=&global;s_dpp_ctx.id=7;s_dpp_ctx.bootstrap_done=true;s_dpp_ctx.bootstrap_params.num_chan=1;s_dpp_ctx.bootstrap_params.chan_list[0]=3;
 const char *bad[]={"","1,","1,6,x","1,1","1 6","0","256","12345678901234567890","1,2,3,4,5,6"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(esp_supp_dpp_bootstrap_gen(bad[i],0,key,NULL)==ESP_ERR_DPP_INVALID_LIST);
 assert(esp_supp_dpp_bootstrap_gen("1",1,key,NULL)==ESP_ERR_NOT_SUPPORTED);
 assert(esp_supp_dpp_bootstrap_gen("1",0,"123",NULL)==ESP_ERR_INVALID_ARG);
 assert(esp_supp_dpp_bootstrap_gen("1",0,key,"bad;I:value")==ESP_ERR_INVALID_ARG);
 assert(!live&&!calls&&!scheduled&&s_dpp_ctx.id==7);
 mac_error=42;assert(submit()==42);mac_error=0;
 calls=0;fail_at=1;assert(submit()==ESP_ERR_NO_MEM&&!live&&!scheduled);fail_at=0;
 queue_fail=true;assert(submit()==ESP_ERR_NO_MEM&&!live&&!scheduled&&s_dpp_ctx.id==7);queue_fail=false;
 assert(!submit()&&s_dpp_bootstrap_job&&live==1&&s_dpp_ctx.id==7);
 assert(submit()==ESP_ERR_INVALID_STATE);
 void *old_high=scheduled_high,*old_low=scheduled_low;
 cancel_fail=true;dpp_api_lock();assert(esp32qjs_dpp_bootstrap_cancel_locked()==ESP_ERR_INVALID_STATE&&s_dpp_bootstrap_job);dpp_api_unlock();
 cancel_fail=false;dpp_api_lock();assert(!esp32qjs_dpp_bootstrap_cancel_locked());dpp_api_unlock();assert(!live&&!scheduled);
 assert(!submit());esp_dpp_bootstrap_gen(old_high,old_low);assert(s_dpp_bootstrap_job&&scheduled&&!starts);
 post_error=43;dispatch();assert(!live&&!s_dpp_bootstrap_job&&s_dpp_ctx.id>100&&s_dpp_ctx.bootstrap_params.chan_list[0]==1);post_error=0;
 calls=0;fail_at=2;assert(!submit());dispatch();assert(!live&&s_dpp_ctx.id>100);fail_at=0;
 assert(!submit());dispatch();assert(!live&&s_dpp_ctx.id>100&&s_dpp_ctx.bootstrap_params.num_chan==3);
 assert(!failures&&result.status.observation_drops==2&&!locked&&!scheduled);
 s_dpp_bootstrap_last_identity=UINT64_MAX;assert(submit()==ESP_ERR_NO_MEM&&!live);
 return 0;
}
'''
