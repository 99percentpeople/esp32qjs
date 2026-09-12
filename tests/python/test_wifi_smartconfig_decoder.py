"""Deferred SmartConfig worker coordinator checks against full production code.

SDK IPC/driver and the separately tested timer/event/ACK boundaries are injected.
Host execution does not qualify native SDK scheduling or RF retirement.
"""
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]


class SmartConfigDecoder(unittest.TestCase):
    def test_production_decoder(self):
        paths = [
            'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_smartconfig_decoder.h',
            'components/esp32_mquickjs/src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_decoder.c',
        ]
        production = ''.join(re.sub(r'^#(?:include|pragma)[^\n]*\n', '', (ROOT / p).read_text(), flags=re.M) for p in paths)
        # Host pointer width is unrelated to the separately target-compiled ABI.
        production = production.replace('_Static_assert(sizeof(sc_ipc_config_t) == 12, "review SmartConfig native IPC ABI");', '')
        events = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_smartconfig_events.h').read_text()
        bundle = re.search(r'typedef struct \{[^}]*\} esp32_mquickjs_wifi_smartconfig_credentials_t;', events).group(0)
        status = re.search(r'typedef struct \{[^}]*\} esp32_mquickjs_wifi_smartconfig_events_status_t;', events).group(0)
        boundaries = BOUNDARIES.replace('static int locked,', '#define ESP32_MQUICKJS_SMARTCONFIG_CUSTOM_DATA_MAX 64\n' + bundle + '\n' + status + '\nstatic int locked,', 1)
        compile_run(self, boundaries + production + NATIVE + CASES)


BOUNDARIES = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define ESP32_MQUICKJS_SMARTCONFIG_TIMERS 9U
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NOT_FINISHED 4
#define ESP_ERR_WIFI_NOT_INIT 5
#define ESP_ERR_TIMEOUT 6
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int esp_err_t, portMUX_TYPE;
typedef enum {WIFI_MODE_NULL,WIFI_MODE_STA,WIFI_MODE_AP,WIFI_MODE_APSTA} wifi_mode_t;
typedef enum {SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS,SC_TYPE_ESPTOUCH_V2} smartconfig_type_t;
typedef struct {void *timer_arg;} ETSTimer;
typedef struct {uint64_t identity;uint32_t radio_generation;} esp32_mquickjs_wifi_smartconfig_token_t;
typedef struct {esp_err_t error;} esp32_mquickjs_wifi_smartconfig_timer_status_t;
typedef struct {bool enable_log,esp_touch_v2_enable_crypt;char *esp_touch_v2_key;} smartconfig_start_config_t;
typedef struct {uint8_t ssid[32],password[64];smartconfig_type_t type;uint8_t token,cellphone_ip[4];} smartconfig_event_got_ssid_pswd_t;
static int locked,allocs,frees,start_calls,stop_calls,fence_calls,scan_calls,rx_calls,list_calls;
static int native_status,ipc_error,start_error,stop_error,scan_error,rx_error,list_error,timer_error,drain_error;
static int native_allocation_error;
static bool oom_on_start;
static bool alloc_error,promiscuous,event_owned,event_closed,timer_owned,timer_closed,ack_busy;
static uint64_t ack_owner,ack_receipt;
static bool credential_ready;
static wifi_mode_t mode=WIFI_MODE_STA;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!locked);locked=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(locked);locked=0;}while(0)
static void *heap_caps_calloc(size_t n,size_t s,int caps){assert(!locked && caps==3);if(alloc_error)return NULL;allocs++;return calloc(n,s);}
static void heap_caps_free(void *p){assert(!locked);frees++;free(p);}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
static esp_err_t esp_wifi_get_mode(wifi_mode_t *out){assert(!locked);*out=mode;return 0;}
static esp_err_t esp_wifi_get_promiscuous(bool *out){assert(!locked);*out=promiscuous;return 0;}
static esp_err_t esp_wifi_set_promiscuous(bool enabled){assert(!locked && !enabled);rx_calls++;if(!rx_error)promiscuous=false;return rx_error;}
static esp_err_t esp_wifi_scan_stop(void){assert(!locked);scan_calls++;return scan_error;}
static esp_err_t esp_wifi_clear_ap_list(void){assert(!locked);list_calls++;return list_error;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_events_begin(uint32_t gen,esp32_mquickjs_wifi_smartconfig_token_t *t){
 assert(!locked && !event_owned && !t->identity);event_owned=true;event_closed=false;native_allocation_error=0;t->identity=123;t->radio_generation=gen;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_events_status(const esp32_mquickjs_wifi_smartconfig_token_t *t,esp32_mquickjs_wifi_smartconfig_events_status_t *out){
 assert(!locked && event_owned && t->identity==123);memset(out,0,sizeof(*out));out->token=*t;out->allocation_error=native_allocation_error;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_events_close(const esp32_mquickjs_wifi_smartconfig_token_t *t){assert(t->identity==123 && event_owned);event_closed=true;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_events_release(esp32_mquickjs_wifi_smartconfig_token_t *t){assert(event_closed && event_owned && !timer_owned && !ack_owner);event_owned=false;memset(t,0,sizeof(*t));return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_timers_begin(const esp32_mquickjs_wifi_smartconfig_token_t *t,ETSTimer *const timers[9]){
 assert(!locked && event_owned && t->identity==123 && !timer_owned);for(int i=0;i<9;i++){assert(timers[i]);for(int j=0;j<i;j++)assert(timers[i]!=timers[j]);}
 timer_owned=true;timer_closed=false;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_timers_status(const esp32_mquickjs_wifi_smartconfig_token_t *t,esp32_mquickjs_wifi_smartconfig_timer_status_t *s){assert(t->identity==123 && timer_owned);s->error=timer_error;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_timers_close(const esp32_mquickjs_wifi_smartconfig_token_t *t){assert(t->identity==123 && timer_owned);timer_closed=true;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_timers_cleanup(const esp32_mquickjs_wifi_smartconfig_token_t *t){assert(t->identity==123 && timer_closed);return drain_error;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_timers_release(const esp32_mquickjs_wifi_smartconfig_token_t *t){assert(t->identity==123 && timer_closed && !native_status && !drain_error);timer_owned=false;return 0;}
static esp_err_t esp32qjs_smartconfig_ack_reserve(uint64_t owner){if(ack_owner || ack_busy)return ESP_ERR_INVALID_STATE;ack_owner=owner;return 0;}
static esp_err_t esp32qjs_smartconfig_ack_release(uint64_t owner){assert(owner==ack_owner && !ack_busy);ack_owner=0;return 0;}
static esp_err_t esp32qjs_smartconfig_ack_start(uint64_t owner,smartconfig_type_t type,uint8_t token,const uint8_t *phone,uint64_t *receipt){assert(owner==ack_owner && type==SC_TYPE_ESPTOUCH && token==7 && phone[0]==192 && !*receipt);*receipt=ack_receipt=99;ack_busy=true;return 0;}
static esp_err_t esp32qjs_smartconfig_ack_stop(uint64_t owner,uint64_t receipt){assert(owner==ack_owner && receipt==99);return 0;}
static uint64_t esp32qjs_smartconfig_ack_status(bool *busy,bool *stopping,bool *completed,esp_err_t *error,esp_err_t *observation,int *socket_errno){(void)stopping;(void)completed;(void)error;(void)observation;(void)socket_errno;*busy=ack_busy;return ack_receipt;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_credentials_commit(const esp32_mquickjs_wifi_smartconfig_token_t *t){assert(t->identity==123 && credential_ready);credential_ready=false;return ESP_OK;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_credentials_copy(const esp32_mquickjs_wifi_smartconfig_token_t *t,esp32_mquickjs_wifi_smartconfig_credentials_t *c){assert(t->identity==123);if(!credential_ready)return ESP_ERR_NOT_FINISHED;memset(c,0x55,sizeof(*c));c->network.type=SC_TYPE_ESPTOUCH;c->network.token=7;c->network.cellphone_ip[0]=192;return 0;}
'''

NATIVE = r'''
ETSTimer channel_timer,Restart_delay_timer,TouchRestart_ht20_timer,TouchRestart_ht40_timer,
 TouchUdpTimer,KissRes_ht20_timer,weixin_timer,restart_ht20_timer,restart_ht40_timer;
int esp_wifi_ipc_internal(sc_ipc_config_t *config,bool sync){assert(!locked && sync && !config->arg_size);if(ipc_error)return ipc_error;return config->fn(config->arg);}
int smartconfig_get_status(void){assert(!locked);return native_status;}
int esp_smartconfig_set_type_local(void *type){assert(!locked && (uintptr_t)type<=3);return 0;}
int esp_smartconfig_fast_mode_local(void *fast){assert(!locked && (uintptr_t)fast<=1);return 0;}
int esp_esptouch_set_timeout_local(void *seconds){assert(!locked && (uintptr_t)seconds>=15 && (uintptr_t)seconds<=255);return 0;}
int esp_smartconfig_start_local(void *config){smartconfig_start_config_t *c=config;assert(!locked && !c->enable_log && event_owned && timer_owned && ack_owner);if(c->esp_touch_v2_enable_crypt)assert(strlen(c->esp_touch_v2_key)==16);start_calls++;native_status=1;promiscuous=true;if(oom_on_start)native_allocation_error=ESP_ERR_NO_MEM;return start_error;}
int esp_smartconfig_stop_local(void *unused){assert(!unused && !locked && timer_closed && event_owned && !drain_error && !promiscuous);stop_calls++;if(!stop_error)native_status=0;return stop_error;}
int sc_aes_crypt_init(void){assert(!locked && !native_status);fence_calls++;return 0;}
'''

CASES = r'''
static esp32_mquickjs_wifi_smartconfig_decoder_options_t options={.type=SC_TYPE_ESPTOUCH,.channel_timeout_s=60};
static esp32_mquickjs_wifi_smartconfig_decoder_t *create(void){esp32_mquickjs_wifi_smartconfig_decoder_t *d=NULL;assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&d)==0 && d);return d;}
static void release(esp32_mquickjs_wifi_smartconfig_decoder_t **d){assert(esp32_mquickjs_wifi_smartconfig_decoder_close(*d)==0);for(size_t i=0;i<sizeof((*d)->key);i++)assert(!(*d)->key[i]);assert(esp32_mquickjs_wifi_smartconfig_decoder_release(d)==0 && !*d && !s_sc_decoder_reserved);}
int main(void){
 esp32_mquickjs_wifi_smartconfig_decoder_t *d=NULL,*other=NULL;
 options.channel_timeout_s=14;assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&d)==ESP_ERR_INVALID_ARG && !allocs);options.channel_timeout_s=60;
 uint8_t key[16]={0};options.type=SC_TYPE_ESPTOUCH_V2;options.key=key;options.key_length=16;
 assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&d)==ESP_ERR_INVALID_ARG && !allocs);
 memset(key,'A',16);d=create();assert(d->config.esp_touch_v2_key==d->key && !memcmp(d->key,key,16));memset(key,'B',16);assert(d->key[0]=='A');release(&d);
 options.type=SC_TYPE_ESPTOUCH;options.key=NULL;options.key_length=0;
 alloc_error=true;assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&d)==ESP_ERR_NO_MEM && !s_sc_decoder_reserved);alloc_error=false;
 d=create();assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&other)==ESP_ERR_INVALID_STATE && !other);
 native_status=1;assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_ERR_INVALID_STATE && !event_owned && !start_calls);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_release(&d)==ESP_ERR_INVALID_STATE);release(&d);assert(!stop_calls && native_status==1);native_status=0;
 d=create();ipc_error=ESP_ERR_NO_MEM;assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_ERR_NO_MEM && !d->handoff_unknown);ipc_error=0;release(&d);
 d=create();assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==0 && d->started);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_ERR_INVALID_STATE && start_calls==1);
 esp32_mquickjs_wifi_smartconfig_decoder_status_t status;
 timer_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_decoder_status(d,&status)==0 && status.error==ESP_FAIL);timer_error=0;
 assert(esp32_mquickjs_wifi_smartconfig_decoder_ack(d)==ESP_ERR_INVALID_STATE && !d->ack_attempted);
 drain_error=ESP_ERR_NOT_FINISHED;assert(esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(d)==ESP_ERR_NOT_FINISHED && !stop_calls && !event_closed);
 drain_error=0;scan_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(d)==ESP_FAIL && !stop_calls);
 scan_error=0;rx_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(d)==ESP_FAIL && !stop_calls);int scans=scan_calls,lists=list_calls;
 rx_error=0;stop_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(d)==ESP_FAIL && stop_calls==1);
 stop_error=0;assert(esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(d)==0 && stop_calls==2 && fence_calls==1 && timer_owned);
 assert(d->capture_stopped && !d->started && !event_closed && event_owned && ack_owner);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_ack(d)==ESP_ERR_NOT_FINISHED && !d->ack_attempted && d->error==0);
 credential_ready=true;esp32_mquickjs_wifi_smartconfig_credentials_t credential;
 assert(esp32_mquickjs_wifi_smartconfig_decoder_credentials(d,&credential,false)==0);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_credentials(d,NULL,true)==0 && !credential_ready);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_ack(d)==0 && d->ack_identity==99);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_ack(d)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_status(d,&status)==0 && status.capture_stopped && status.ack_busy);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_close(d)==ESP_ERR_NOT_FINISHED && event_closed && stop_calls==2);
 assert(scan_calls==scans && list_calls==lists);assert(esp32_mquickjs_wifi_smartconfig_decoder_release(&d)==ESP_ERR_INVALID_STATE);
 ack_busy=false;release(&d);assert(stop_calls==2 && fence_calls==1);
 d=create();start_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_FAIL && d->decoder_owned);start_error=0;release(&d);assert(stop_calls==3);
 /* SDK success after an internal failed allocation must not publish started. */
 d=create();oom_on_start=true;
 assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_ERR_NO_MEM && d->decoder_owned && !d->started);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_status(d,&status)==0 && status.error==ESP_ERR_NO_MEM && !strcmp(status.stage,"smartconfig-allocation"));
 oom_on_start=false;release(&d);
 /* A later native failure is visible before any observer/event queue. */
 d=create();assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==0);
 native_allocation_error=ESP_ERR_NO_MEM;
 assert(esp32_mquickjs_wifi_smartconfig_decoder_status(d,&status)==0 && status.error==ESP_ERR_NO_MEM && !strcmp(status.stage,"smartconfig-allocation"));
 release(&d);
 assert(allocs==frees);
 /* Unconfirmed native dispatch retains all storage and blocks reuse/release. */
 d=create();ipc_error=ESP_ERR_TIMEOUT;assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_ERR_TIMEOUT);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_status(d,&status)==0 && status.handoff_unknown && status.token.identity==0);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_close(d)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_release(&d)==ESP_ERR_INVALID_STATE && d);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&other)==ESP_ERR_INVALID_STATE);
 return 0;
}
'''
