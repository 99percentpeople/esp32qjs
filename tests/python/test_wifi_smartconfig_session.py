"""Deferred full production SmartConfig Session worker/lifetime regressions."""
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / 'components/esp32_mquickjs'


def unit(path):
    return re.sub(r'^#(?:include|pragma)[^\n]*\n', '', path.read_text(), flags=re.M)


def headers():
    return ''.join(unit(COMPONENT / 'internal' / ('esp32_mquickjs_wifi_smartconfig_' + name + '.h'))
                   for name in ('events', 'decoder', 'radio', 'connection', 'session'))


class SmartConfigSession(unittest.TestCase):
    def test_production_service_and_runtime_teardown(self):
        production = unit(COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_session.c')
        compile_run(self, TYPES + headers() + BOUNDARIES + production + CASES)


TYPES = r'''
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define CONFIG_ESP_WIFI_ENABLE_WPA3_SAE 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_ERR_NOT_SUPPORTED 6
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS 1
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_FAILURE 2
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT 3
#define WIFI_ALL_CHANNEL_SCAN 1
#define WIFI_CONNECT_AP_BY_SIGNAL 0
#define WPA3_SAE_PWE_BOTH 3
#define MALLOC_CAP_8BIT 1
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int esp_err_t,portMUX_TYPE,wifi_second_chan_t;
typedef enum {WIFI_AUTH_OPEN,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA3_PSK} wifi_auth_mode_t;
typedef struct {struct {
 uint8_t ssid[32],password[64],bssid[6];bool bssid_set;
 int scan_method,sort_method,sae_pwe_h2e;
 struct {int rssi;wifi_auth_mode_t authmode;} threshold;
 struct {bool capable,required;} pmf_cfg;
} sta;} wifi_config_t;
typedef enum {SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS,SC_TYPE_ESPTOUCH_V2} smartconfig_type_t;
typedef struct {uint8_t ssid[32],password[64];bool bssid_set;uint8_t bssid[6];smartconfig_type_t type;uint8_t token,cellphone_ip[4];} smartconfig_event_got_ssid_pswd_t;
typedef struct {uint32_t generation,identity;unsigned client;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
typedef struct {uint32_t generation,lease_identity,identity;unsigned kind;} esp32_mquickjs_wifi_radio_operation_t;
'''

BOUNDARIES = r'''
static unsigned locks,allocs,frees,starts,finishes,closes,copies,commits,acks;
static bool alloc_fail,queue_fail,partial_start,capture_ready,hold_close,radio_consumed,ack_busy;
static int start_error,poll_error,finish_error;
static int64_t now=100;
static void (*scheduled)(void *),(*during_transfer)(void);
static void *scheduled_arg;
static esp32_mquickjs_wifi_smartconfig_radio_status_t radio_state;
static esp32_mquickjs_wifi_smartconfig_connection_status_t connection_state;
static unsigned connection_begins,connection_ends,connection_keeps;
static int connection_begin_error,connection_end_error;
static bool connection_partial,valid_password;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!locks);locks=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(locks);locks=0;}while(0)
static void *heap_caps_calloc(size_t n,size_t size,int caps){assert(!locks && caps==1);if(alloc_fail)return NULL;allocs++;return calloc(n,size);}
static void heap_caps_free(void *p){assert(!locks && p);frees++;free(p);}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static int64_t esp_timer_get_time(void){return now;}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg){assert(!locks && !scheduled);if(queue_fail)return false;scheduled=fn;scheduled_arg=arg;return true;}
esp_err_t esp32_mquickjs_wifi_smartconfig_capture_owners(esp32_mquickjs_wifi_radio_lease_t owners[3]){assert(!locks);for(unsigned i=0;i<3;i++)owners[i]=(esp32_mquickjs_wifi_radio_lease_t){.identity=i+1,.generation=5,.client=i,.acquired=true};return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_validate_options(const esp32_mquickjs_wifi_smartconfig_decoder_options_t *o){return o && o->channel_timeout_s>=15 ? ESP_OK : ESP_ERR_INVALID_ARG;}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_begin(const esp32_mquickjs_wifi_radio_lease_t owners[3],bool allow_ap,const esp32_mquickjs_wifi_smartconfig_decoder_options_t *options,esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){
 assert(!locks && allow_ap && owners[1].identity==2 && options->channel_timeout_s==60);starts++;
 if(options->key_length)assert(options->key_length==16 && options->key[0]=='A');
 memset(&radio_state,0,sizeof(radio_state));radio_consumed=false;
 if(!start_error || partial_start)*token=(esp32_mquickjs_wifi_radio_operation_t){.generation=5,.lease_identity=2,.identity=starts,.kind=3};
 radio_state.operation=*token;radio_state.decoder.started=token->identity!=0;radio_state.stage="native-start";*status=radio_state;return start_error;
}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_status(const esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){assert(!locks && token->identity);radio_state.events.credentials_received=capture_ready;radio_state.events.credentials_consumed=radio_consumed;radio_state.decoder.error=poll_error;radio_state.decoder.ack_busy=ack_busy;*status=radio_state;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){assert(!locks && token->identity && capture_ready);finishes++;radio_state.decoder.capture_stopped=true;radio_state.channel_restored=true;*status=radio_state;return finish_error;}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_credentials(const esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_credentials_t *out,bool commit){assert(!locks && token->identity && radio_state.channel_restored && !radio_consumed);if(commit){assert(!out);commits++;radio_consumed=true;if(during_transfer)during_transfer();}else{assert(out);copies++;memset(out,0,sizeof(*out));memset(out->network.ssid,'S',32);memset(out->network.password,valid_password?'A':'P',64);}return 0;}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_ack(const esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){assert(!locks && token->identity && radio_consumed);acks++;radio_state.decoder.ack_identity=31;radio_state.decoder.ack_busy=ack_busy=true;*status=radio_state;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_close(esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){assert(!locks && token->identity);closes++;radio_state.stage="native-close";radio_state.decoder.closing=true;*status=radio_state;if(hold_close)return ESP_ERR_NOT_FINISHED;memset(token,0,sizeof(*token));return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_connect_begin(const esp32_mquickjs_wifi_radio_operation_t *owner,const wifi_config_t *config,uint32_t timeout,uint32_t *generation){
 assert(!locks && owner->identity && config->sta.pmf_cfg.capable && timeout && !*generation);
 connection_begins++;if(!connection_begin_error || connection_partial)*generation=connection_state.generation=71;return connection_begin_error;
}
esp_err_t esp32_mquickjs_wifi_smartconfig_connect_status(const esp32_mquickjs_wifi_radio_operation_t *owner,uint32_t generation,esp32_mquickjs_wifi_smartconfig_connection_status_t *status){
 assert(!locks && owner->identity && generation==connection_state.generation);*status=connection_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_smartconfig_connect_end(const esp32_mquickjs_wifi_radio_operation_t *owner,uint32_t generation,bool keep){
 assert(!locks && owner->identity && generation==connection_state.generation);connection_ends++;
 if(connection_end_error)return connection_end_error;
 if(keep){assert(connection_state.connected && connection_state.terminal==1);connection_keeps++;}
 else connection_state.connected=false;
 connection_state.generation=0;return 0;
}
'''

CASES = r'''
static esp32_mquickjs_wifi_smartconfig_session_t *current;
static esp32_mquickjs_wifi_smartconfig_decoder_options_t options={.type=SC_TYPE_ESPTOUCH,.channel_timeout_s=60};
static void run_worker(void){assert(scheduled);void (*fn)(void *)=scheduled;void *arg=scheduled_arg;scheduled=NULL;scheduled_arg=NULL;fn(arg);assert(!s_sc_workers);}
static void tick(void){now+=200000;assert(esp32_mquickjs_wifi_smartconfig_service());run_worker();}
static esp32_mquickjs_wifi_smartconfig_session_t *create(void){esp32_mquickjs_wifi_smartconfig_session_t *s=NULL;assert(esp32_mquickjs_wifi_smartconfig_session_create(&options,true,10000,&s)==0 && s);return s;}
static void activate(esp32_mquickjs_wifi_smartconfig_session_t *s){assert(esp32_mquickjs_wifi_smartconfig_session_activate(s)==0);}
static void stop(esp32_mquickjs_wifi_smartconfig_session_t *s){esp32_mquickjs_wifi_smartconfig_session_close(s,false);if(!s->status.retired)tick();assert(s->status.retired && !s_sc_active);esp32_mquickjs_wifi_smartconfig_session_release(s);assert(!s_sc_handles);}
static void cancel_during_transfer(void){assert(current->status.worker_busy);esp32_mquickjs_wifi_smartconfig_session_close(current,false);}
static void reset(void){assert(!s_sc_active && !s_sc_handles && !scheduled);start_error=poll_error=finish_error=0;alloc_fail=queue_fail=partial_start=capture_ready=hold_close=radio_consumed=ack_busy=false;during_transfer=NULL;memset(&radio_state,0,sizeof(radio_state));memset(&connection_state,0,sizeof(connection_state));connection_begins=connection_ends=connection_keeps=0;connection_begin_error=connection_end_error=0;connection_partial=valid_password=false;assert(esp32_mquickjs_wifi_smartconfig_open_runtime()==0);}
static void pump(void){now+=200000;(void)esp32_mquickjs_wifi_smartconfig_service();if(scheduled)run_worker();}
static esp32_mquickjs_wifi_smartconfig_session_t *automatic(void){
 esp32_mquickjs_wifi_smartconfig_session_t *s=create();
 esp32_mquickjs_wifi_smartconfig_connection_options_t c={.timeout_ms=30000,.minimum_auth=WIFI_AUTH_WPA2_PSK};
 assert(!esp32_mquickjs_wifi_smartconfig_session_configure_connection(s,&c));activate(s);capture_ready=true;pump();return s;
}
int main(void){
 reset();alloc_fail=true;esp32_mquickjs_wifi_smartconfig_session_t *s=NULL;
 assert(esp32_mquickjs_wifi_smartconfig_session_create(&options,true,1000,&s)==ESP_ERR_NO_MEM && !s_sc_handles);alloc_fail=false;
 esp32_mquickjs_wifi_smartconfig_session_t *held[4];for(unsigned i=0;i<4;i++)held[i]=create();
 assert(esp32_mquickjs_wifi_smartconfig_session_create(&options,true,1000,&s)==ESP_ERR_NO_MEM);
 for(unsigned i=0;i<4;i++)esp32_mquickjs_wifi_smartconfig_session_release(held[i]);assert(!s_sc_handles);
 uint8_t key[16];memset(key,'A',16);options.type=SC_TYPE_ESPTOUCH_V2;options.key=key;options.key_length=16;s=create();memset(key,'B',16);assert(s->key[0]=='A');
 activate(s);assert(esp32_mquickjs_wifi_smartconfig_session_activate(s)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_smartconfig_service());esp32_mquickjs_wifi_smartconfig_session_close(s,false);run_worker();
 assert(!starts && s->status.retired && !s_sc_active);for(unsigned i=0;i<16;i++)assert(!s->key[i]);esp32_mquickjs_wifi_smartconfig_session_release(s);
 options.type=SC_TYPE_ESPTOUCH;options.key=NULL;options.key_length=0;
 reset();s=create();activate(s);queue_fail=true;assert(!esp32_mquickjs_wifi_smartconfig_service() && !s->status.worker_busy && s->status.cleanup_error==ESP_ERR_NO_MEM);
 queue_fail=false;tick();assert(starts==1 && s_sc_active==s);
 capture_ready=true;tick();assert(s->status.credentials_ready && finishes==1 && copies==1 && commits==1 && radio_consumed);
 esp32_mquickjs_wifi_smartconfig_credentials_t credential;assert(esp32_mquickjs_wifi_smartconfig_session_credentials(s,&credential,false)==0 && credential.network.password[0]=='P');
 assert(esp32_mquickjs_wifi_smartconfig_session_credentials(s,&credential,false)==0); /* Conversion retry. */
 assert(esp32_mquickjs_wifi_smartconfig_session_credentials(s,NULL,true)==0 && s->credentials.network.password[0]==0);
 assert(esp32_mquickjs_wifi_smartconfig_session_credentials(s,&credential,false)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_smartconfig_session_ack(s)==0);tick();assert(acks==1 && s->status.native.decoder.ack_busy);
 ack_busy=false;tick();assert(!s->status.native.decoder.ack_busy);assert(!esp32_mquickjs_wifi_smartconfig_service());stop(s);
 reset();s=create();activate(s);tick();hold_close=true;esp32_mquickjs_wifi_smartconfig_session_close(s,true);tick();assert(s->status.timed_out && s->status.error==ESP_ERR_TIMEOUT && !s->status.retired);
 assert(!esp32_mquickjs_wifi_smartconfig_prepare_runtime_destroy());assert(!esp32_mquickjs_wifi_smartconfig_service()); /* Cleanup worker already queued. */
 esp32_mquickjs_wifi_smartconfig_session_release(s);run_worker();assert(s_sc_active && s_sc_handles==1);
 hold_close=false;assert(!esp32_mquickjs_wifi_smartconfig_prepare_runtime_destroy());run_worker();assert(esp32_mquickjs_wifi_smartconfig_prepare_runtime_destroy());assert(!s_sc_handles);
 reset();s=create();activate(s);start_error=ESP_FAIL;partial_start=true;hold_close=true;tick();assert(s->status.error==ESP_FAIL && s->operation.identity && !s->status.retired);
 hold_close=false;tick();assert(s->status.retired);esp32_mquickjs_wifi_smartconfig_session_release(s);
 reset();s=create();activate(s);current=s;capture_ready=true;during_transfer=cancel_during_transfer;tick();assert(s->status.retired && !s->status.credentials_ready && !s->credentials.network.password[0]);esp32_mquickjs_wifi_smartconfig_session_release(s);
 reset();s=create();activate(s);s->deadline_us=now;tick();assert(s->status.retired && s->status.timed_out);esp32_mquickjs_wifi_smartconfig_session_release(s);
 reset();s=create();activate(s);tick();esp32_mquickjs_wifi_smartconfig_session_release(s);assert(s_sc_active && s_sc_active->status.closing);tick();assert(!s_sc_active && !s_sc_handles);
 /* Invalid credentials must retire even though no Station generation was acquired. */
 reset();s=automatic();pump();assert(s->status.closing && s->status.error==ESP_ERR_INVALID_ARG && !connection_begins);
 pump();assert(s->status.retired && !s_sc_active);esp32_mquickjs_wifi_smartconfig_session_release(s);
 /* Retry a pending capture/IP barrier without duplicating a native connection. */
 reset();valid_password=true;s=automatic();connection_begin_error=ESP_ERR_NOT_FINISHED;pump();assert(!s->status.closing && !s->status.connection_generation);
 connection_begin_error=0;pump();assert(connection_begins==2 && s->status.connection_started);
 assert(esp32_mquickjs_wifi_smartconfig_session_credentials(s,&credential,false)==ESP_ERR_INVALID_STATE);
 connection_state.terminal=1;connection_state.connected=true;pump();assert(s->ack_submitted && ack_busy);
 ack_busy=false;radio_state.decoder.ack_completed=true;pump();assert(!s->status.connection_transferred);
 queue_fail=true;pump();assert(s->status.connection_transferred && !s->status.completed && connection_keeps==1);
 queue_fail=false;pump();assert(s->status.completed && s->status.retired && !s_sc_active);
 assert(!esp32_mquickjs_wifi_smartconfig_session_credentials(s,&credential,false) && credential.network.password[0]=='A');
 assert(!esp32_mquickjs_wifi_smartconfig_session_credentials(s,NULL,true));stop(s);assert(connection_keeps==1 && connection_state.connected);
 /* Submission errors retaining a generation must drain it before Radio close. */
 reset();valid_password=true;s=automatic();connection_begin_error=ESP_FAIL;connection_partial=true;connection_end_error=ESP_ERR_NOT_FINISHED;pump();
 assert(s->status.closing && s->status.connection_generation && !s->status.retired);unsigned old_closes=closes;
 pump();assert(closes==old_closes && connection_ends==2);
 connection_end_error=0;pump();assert(s->status.retired && !connection_keeps);esp32_mquickjs_wifi_smartconfig_session_release(s);
 /* Acquisition deadline still applies after credentials, before ACK handoff. */
 reset();valid_password=true;s=automatic();pump();s->deadline_us=now;pump();
 assert(s->status.timed_out && s->status.retired && connection_ends==1 && !connection_keeps);esp32_mquickjs_wifi_smartconfig_session_release(s);
 assert(allocs==frees && !s_sc_workers);
 return 0;
}
'''
