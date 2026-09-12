"""Deferred production DPP Session scheduling, result retention and close.

The real Session controls scheduling and ownership. Native Radio, runtime
Station helper, allocation and clock are injectable boundaries. AST only until
the concentrated Wi-Fi test wave; no RF/RTOS proof is implied.
"""
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2] / 'components/esp32_mquickjs'


def unit(path):
    source = path.read_text()
    if path.name == 'esp32_mquickjs_wifi_dpp_session.c':
        source = source.replace('#include "esp32_mquickjs_wifi_dpp_session_connection.inc"',
            path.with_name('esp32_mquickjs_wifi_dpp_session_connection.inc').read_text())
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', source, flags=re.M)


def headers():
    return ''.join(unit(ROOT / 'internal' / f'esp32_mquickjs_wifi_dpp_{name}.h')
        for name in ('connection', 'result', 'worker', 'radio', 'station', 'session'))


class DppSession(unittest.TestCase):
    def test_results_worker_storage_and_independent_close_suffixes(self):
        headers = ''.join(unit(ROOT / 'internal' / f'esp32_mquickjs_wifi_dpp_{name}.h')
            for name in ('connection', 'result', 'worker', 'radio', 'station', 'session'))
        compile_run(self, TYPES + headers + BOUNDARIES +
            unit(ROOT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_session.c') + CASES)


TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_DPP_SUPPORT 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define ESP_DPP_MAX_CONFIG_COUNT 3
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_ERR_INVALID_SIZE 6
#define ESP_FAIL 7
#define ESP_ERR_INVALID_RESPONSE 8
#define ESP_ERR_NOT_SUPPORTED 9
enum {ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS=1,ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_FAILURE,ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT};
#define MALLOC_CAP_8BIT 1
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int esp_err_t,portMUX_TYPE;
typedef struct {int unused;} wifi_config_t;
typedef struct {bool valid;uint8_t ssid[32],ssid_len,bssid[6],channel;uint16_t aid;}esp32_mquickjs_wifi_link_snapshot_t;
typedef struct {uint8_t ssid[32],ssid_len,key[128];} esp_dpp_config_data_t;
typedef struct {int unused;} wifi_event_action_tx_status_t;
typedef struct {uint32_t generation,identity;unsigned client;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
typedef struct {uint32_t generation,lease_identity,identity;unsigned kind;} esp32_mquickjs_wifi_radio_operation_t;
'''

BOUNDARIES = r'''
static unsigned locks,allocations,frees,starts,copies,commits,helper_identity;
static bool fail_alloc,fail_queue,radio_active,helper_ready,hold_native,hold_helper,terminal,uri_generated;
static int begin_error,row_error,authentication_error;
static int restore_error,recover_error;
static unsigned recoveries;
static bool native_connection,hold_connection;
static unsigned selections,connections,connection_ends,connection_terminal;
static int64_t now=100;
static size_t allocation_size;
static void (*scheduled)(void*),(*during_copy)(void);
static void *argument;
static esp32_mquickjs_wifi_dpp_radio_status_t radio;
#define portENTER_CRITICAL(p) do{(void)p;assert(!locks);locks++;}while(0)
#define portEXIT_CRITICAL(p) do{(void)p;assert(locks==1);locks--;}while(0)
static void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){volatile uint8_t*q=p;while(n--)*q++=0;}
static void*heap_caps_calloc(size_t n,size_t z,int caps){assert(!locks&&caps==1);if(fail_alloc)return NULL;
 void*p=calloc(n,z);assert(p);allocation_size=n*z;allocations++;return p;}
static void heap_caps_free(void*p){assert(!locks&&p);for(size_t i=0;i<allocation_size;i++)assert(!((uint8_t*)p)[i]);free(p);frees++;}
static int64_t esp_timer_get_time(void){return now;}
static bool esp32_mquickjs_submit_background_worker(void(*fn)(void*),void*p){
 assert(!locks&&!scheduled);if(fail_queue)return false;scheduled=fn;argument=p;return true;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_validate(const esp32_mquickjs_wifi_dpp_worker_options_t*o){
 return o&&!strcmp(o->channels,"6")?ESP_OK:ESP_ERR_INVALID_ARG;}
esp_err_t esp32_mquickjs_wifi_dpp_station_reserve(uint32_t*id,esp32_mquickjs_wifi_radio_lease_t owners[3]){
 assert(!locks&&!*id);if(helper_identity)return ESP_ERR_INVALID_STATE;helper_identity=*id=7;
 for(unsigned i=0;i<3;i++)owners[i]=(esp32_mquickjs_wifi_radio_lease_t){.identity=i+1,.generation=1,.acquired=true};return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_station_drain(uint32_t id){
 assert(!locks&&id==helper_identity);if(radio_active)assert(radio.worker.capture_retired||radio.worker.retired);
 if(hold_helper)return ESP_ERR_NOT_FINISHED;helper_ready=true;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_station_release(uint32_t*id){assert(!radio_active);
 int e=esp32_mquickjs_wifi_dpp_station_drain(*id);if(!e)helper_identity=*id=0;return e;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_begin(const esp32_mquickjs_wifi_radio_lease_t owners[3],bool allow,
 const esp32_mquickjs_wifi_dpp_worker_options_t*o,esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&helper_identity&&!t->identity&&owners[1].identity==2&&!allow&&!strcmp(o->channels,"6"));starts++;
 t->identity=13;t->generation=1;radio_active=true;radio=(esp32_mquickjs_wifi_dpp_radio_status_t){.operation=*t};*out=radio;return begin_error;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_status(const esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13&&radio_active);radio.worker.native.terminal=terminal;
 radio.worker.native.config_count=terminal?2:0;radio.worker.native.uri_length=uri_generated?6:0;
 *out=radio;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_listen(const esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13);*out=radio;return uri_generated?0:ESP_ERR_NOT_FINISHED;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_uri(const esp32_mquickjs_wifi_radio_operation_t*t,char*out,size_t cap,bool commit){
 assert(!locks&&t->identity==13&&uri_generated);if(commit){assert(!out&&!cap);radio.worker.native.uri_available=false;}
 else{assert(cap>6);memcpy(out,"DPP:X;",7);}return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_finish_capture(const esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13&&terminal);if(hold_native)return ESP_ERR_NOT_FINISHED;
 radio.worker.capture_retired=true;radio.channel_restored=true;*out=radio;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_config(const esp32_mquickjs_wifi_radio_operation_t*t,unsigned i,esp_dpp_config_data_t*out){
 assert(!locks&&t->identity==13&&radio.worker.capture_retired&&helper_ready&&i<2);copies++;
 if(row_error&&i==1)return row_error;memset(out,0,sizeof(*out));out->ssid_len=1;out->ssid[0]='A'+i;out->key[0]=i+1;
 if(during_copy){void(*fn)(void)=during_copy;during_copy=NULL;fn();}return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_configs_commit(const esp32_mquickjs_wifi_radio_operation_t*t){assert(!locks&&t->identity==13);commits++;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_prepare_close(const esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13&&!native_connection);if(hold_native)return ESP_ERR_NOT_FINISHED;
 radio.worker.retired=true;
 if(restore_error){radio.restore_start_failed=true;radio.restore_start_error=restore_error;
  radio.restore_recovery_pending=false;*out=radio;return restore_error;}
 radio.restore_start_failed=radio.restore_recovery_pending=false;radio.restore_start_error=0;
 helper_ready=false;*out=radio;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_recover(const esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13&&!native_connection&&radio.worker.retired&&radio.restore_start_failed);
 recoveries++;if(recover_error){*out=radio;return recover_error;}
 radio.restore_recovery_pending=true;radio.restore_recovery_attempts++;
 restore_error=0;*out=radio;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_close(esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13&&radio.worker.retired&&helper_ready);*out=radio;radio_active=false;t->identity=0;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_connection_prepare(const esp_dpp_config_data_t*r,esp32_mquickjs_wifi_dpp_auth_t requested,
 esp32_mquickjs_wifi_dpp_auth_t*selected,wifi_config_t*config){if(!r||!r->ssid_len)return ESP_ERR_INVALID_ARG;
 *selected=requested?requested:ESP32_MQUICKJS_DPP_AUTH_CONNECTOR;config->unused=1;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_select(const esp32_mquickjs_wifi_radio_operation_t*t,const esp_dpp_config_data_t*r,
 esp32_mquickjs_wifi_dpp_auth_t requested,bool allow_ap_restart,esp32_mquickjs_wifi_dpp_auth_t*selected,wifi_config_t*config,
 esp32_mquickjs_wifi_dpp_radio_status_t*out){assert(!locks&&radio_active&&helper_ready&&t->identity==13&&!allow_ap_restart);
 int e=esp32_mquickjs_wifi_dpp_connection_prepare(r,requested,selected,config);if(e)return e;
 selections++;terminal=false;radio.worker.capture_retired=false;radio.worker.connection_selected=true;
 radio.worker.native.identity=20+selections;*out=radio;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_connect_begin(uint32_t capture,const esp32_mquickjs_wifi_radio_operation_t*t,
 const wifi_config_t*config,uint32_t timeout,uint32_t*generation){assert(!locks&&capture==helper_identity&&t->identity==13&&!native_connection);
 assert(config->unused==1&&timeout&&!*generation);*generation=23;connections++;native_connection=true;connection_terminal=0;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_connect_status(const esp32_mquickjs_wifi_radio_operation_t*t,uint32_t generation,
 esp32_mquickjs_wifi_dpp_connection_status_t*out){assert(!locks&&t->identity==13&&generation==23&&native_connection);
 *out=(esp32_mquickjs_wifi_dpp_connection_status_t){.generation=23,.terminal=connection_terminal,
 .connected=connection_terminal==ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS,
 .link={.valid=true,.ssid={'A'},.ssid_len=1,.bssid={2,3,4,5,6,7},.channel=6,.aid=1}};return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_check_connection(const esp32_mquickjs_wifi_radio_operation_t*t,
 esp32_mquickjs_wifi_dpp_auth_t auth,const uint8_t bssid[6]){assert(!locks&&t->identity==13&&native_connection&&auth==ESP32_MQUICKJS_DPP_AUTH_CONNECTOR&&bssid[0]==2);return authentication_error;}
esp_err_t esp32_mquickjs_wifi_dpp_connect_end(const esp32_mquickjs_wifi_radio_operation_t*t,uint32_t generation){
 assert(!locks&&t->identity==13&&generation==23&&native_connection);if(hold_connection)return ESP_ERR_NOT_FINISHED;
 native_connection=false;connection_ends++;return 0;}

'''

CASES = r'''
static void run_worker(void){assert(scheduled);void(*fn)(void*)=scheduled;scheduled=NULL;fn(argument);argument=NULL;}
static void tick(void){now+=100001;esp32_mquickjs_wifi_dpp_service();if(scheduled)run_worker();}
static void reset(void){assert(!s_dpp_active&&!s_dpp_handles&&!s_dpp_workers&&!scheduled&&!helper_identity&&!radio_active&&allocations==frees);
 fail_alloc=fail_queue=hold_native=hold_helper=terminal=uri_generated=helper_ready=false;begin_error=row_error=0;copies=commits=starts=0;
 during_copy=NULL;authentication_error=0;hold_connection=false;selections=connections=connection_ends=connection_terminal=0;
 restore_error=recover_error=0;recoveries=0;
 assert(!native_connection);assert(!esp32_mquickjs_wifi_dpp_open_runtime());}
static esp32_mquickjs_wifi_dpp_session_t*create(void){
 esp32_mquickjs_wifi_dpp_worker_options_t o={.channels="6"};esp32_mquickjs_wifi_dpp_session_t*s=NULL;
 assert(!esp32_mquickjs_wifi_dpp_session_create(&o,false,1000,&s));assert(!esp32_mquickjs_wifi_dpp_session_activate(s));return s;}
static void close_all(esp32_mquickjs_wifi_dpp_session_t*s){esp32_mquickjs_wifi_dpp_session_close(s,false);
 for(unsigned i=0;i<8&&!s->status.retired;i++)tick();assert(s->status.retired&&!helper_identity&&!radio_active);
 esp32_mquickjs_wifi_dpp_session_release(s);assert(allocations==frees);}
static void close_in_copy(void){esp32_mquickjs_wifi_dpp_session_close(s_dpp_active,false);}
static void finish_capture(void){uri_generated=true;radio.worker.native.uri_available=true;tick();terminal=true;tick();tick();}
int main(void){
 reset();esp32_mquickjs_wifi_dpp_session_t*s=create();tick();assert(!s->status.listening);
 uri_generated=true;radio.worker.native.uri_available=true;tick();assert(s->status.listening&&s->status.uri_ready);
 char uri[32];assert(esp32_mquickjs_wifi_dpp_session_uri(s,uri,6,false)==ESP_ERR_INVALID_SIZE);
 assert(!esp32_mquickjs_wifi_dpp_session_uri(s,uri,sizeof(uri),false)&&!strcmp(uri,"DPP:X;"));
 assert(!esp32_mquickjs_wifi_dpp_session_uri(s,NULL,0,true));
 terminal=true;hold_native=true;tick();assert(!s->status.capture_finished&&!copies);
 hold_native=false;tick();hold_helper=true;tick();assert(!s->status.configs_ready&&!copies);
 hold_helper=false;tick();assert(s->status.configs_ready&&s->status.config_count==2&&copies==2&&commits==1);
 esp_dpp_config_data_t row;assert(!esp32_mquickjs_wifi_dpp_session_config(s,1,&row)&&row.ssid[0]=='B'&&row.key[0]==2);
 esp32_mquickjs_wifi_dpp_session_status_t observed;assert(esp32_mquickjs_wifi_dpp_session_wait_begin(s,false));
 assert(!esp32_mquickjs_wifi_dpp_session_observation(s,&observed));assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
 esp32_mquickjs_wifi_dpp_session_wait_end(s,false);assert(esp32_mquickjs_wifi_dpp_session_observation(s,&observed));
 assert(!esp32_mquickjs_wifi_dpp_session_config(s,0,&row)&&row.key[0]==1);close_all(s);reset();
 /* Selection uses the real scheduler and cannot complete on IP before the
  * native authentication check. Close must drain Station before native DPP. */
 s=create();finish_capture();assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
 assert(esp32_mquickjs_wifi_dpp_session_connect(s,2,0,1000,false)==ESP_ERR_INVALID_ARG&&!selections);
 assert(!esp32_mquickjs_wifi_dpp_session_connect(s,0,0,1000,false));tick();tick();
 assert(selections==1&&connections==1&&!s->status.connected);
 authentication_error=ESP_ERR_NOT_FINISHED;connection_terminal=ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS;tick();
 assert(s->status.connection_established&&!s->status.connection_verified&&!s->status.connected);
 authentication_error=0;tick();tick();assert(s->status.connected&&s->status.connection_verified);
 esp32_mquickjs_wifi_link_snapshot_t link;double elapsed;assert(!esp32_mquickjs_wifi_dpp_session_connection_result(s,&link,&elapsed)&&link.bssid[0]==2&&elapsed>0);
 assert(!esp32_mquickjs_wifi_dpp_session_connect(s,0,0,1000,false)&&selections==1&&connections==1);
 assert(esp32_mquickjs_wifi_dpp_session_connect(s,1,0,1000,false)==ESP_ERR_INVALID_STATE);
 hold_connection=true;esp32_mquickjs_wifi_dpp_session_close(s,false);tick();assert(native_connection&&!radio.worker.retired);
 hold_connection=false;close_all(s);assert(connection_ends==1);reset();
 s=create();finish_capture();assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
 assert(!esp32_mquickjs_wifi_dpp_session_connect(s,0,0,1000,false));tick();tick();
 connection_terminal=ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS;authentication_error=ESP_ERR_INVALID_RESPONSE;tick();
 assert(s->status.closing&&!s->status.connected&&native_connection);close_all(s);reset();
 s=create();finish_capture();assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
 assert(!esp32_mquickjs_wifi_dpp_session_connect(s,0,0,1000,false));tick();tick();s->deadline_us=now;
 esp32_mquickjs_wifi_dpp_service();if(scheduled)run_worker();assert(s->status.timed_out);close_all(s);reset();
 /* Known START failure parks the real service until explicit recovery.
  * Two requests before/during dispatch authorize only one native retry. */
 s=create();finish_capture();assert(esp32_mquickjs_wifi_dpp_session_recover(s)==ESP_ERR_INVALID_STATE);
 restore_error=88;esp32_mquickjs_wifi_dpp_session_close(s,false);tick();
 assert(s->status.native.restore_start_failed&&!s->status.retired&&!recoveries);
 tick();assert(!scheduled&&!recoveries&&s->status.cleanup_error==88);
 /* Queued admission can still be rejected after another source changes.
  * A drained capture helper alone must not bypass native-close restoration. */
 recover_error=77;assert(!esp32_mquickjs_wifi_dpp_session_recover(s));tick();
 assert(recoveries==1&&s->status.cleanup_error==77&&!s->status.native_closed&&radio_active&&helper_identity);
 assert(!s->status.recovery_pending);recover_error=0;recoveries=0;
 assert(!esp32_mquickjs_wifi_dpp_session_recover(s)&&s->status.recovery_pending&&!recoveries);
 assert(!esp32_mquickjs_wifi_dpp_session_recover(s));hold_native=true;tick();
 assert(recoveries==1&&s->status.recovery_pending&&!s->status.retired);
 assert(!esp32_mquickjs_wifi_dpp_session_recover(s));tick();assert(recoveries==1);
 assert(!esp32_mquickjs_wifi_dpp_prepare_runtime_destroy());
 assert(esp32_mquickjs_wifi_dpp_session_recover(s)==ESP_ERR_INVALID_STATE);
 if(scheduled)run_worker();hold_native=false;close_all(s);reset();
 /* Cancel before dispatch never admits a native protocol. */
 s=create();assert(esp32_mquickjs_wifi_dpp_service()&&scheduled);esp32_mquickjs_wifi_dpp_session_close(s,false);
 run_worker();close_all(s);assert(!starts);reset();
 s=create();fail_queue=true;assert(!esp32_mquickjs_wifi_dpp_service()&&!scheduled&&helper_identity);
 now+=2000000;esp32_mquickjs_wifi_dpp_service();assert(s->status.timed_out&&s->status.retired&&!starts);
 esp32_mquickjs_wifi_dpp_session_release(s);reset();
 /* Unknown/pending native close prevents runtime/helper retirement. */
 s=create();tick();hold_native=true;assert(!esp32_mquickjs_wifi_dpp_prepare_runtime_destroy());if(scheduled)run_worker();
 assert(helper_identity&&radio_active&&!s->status.retired);hold_native=false;tick();hold_helper=true;tick();
 assert(radio_active);hold_helper=false;close_all(s);reset();
 /* Partial row failure or close during copy never publishes partial secrets. */
 s=create();tick();row_error=77;finish_capture();assert(s->status.closing&&s->status.error==77&&!s->status.configs_ready);
 close_all(s);reset();s=create();tick();during_copy=close_in_copy;finish_capture();
 assert(s->status.closing&&!s->status.configs_ready);for(size_t i=0;i<sizeof(s->configs);i++)assert(!((uint8_t*)s->configs)[i]);
 close_all(s);reset();
 /* GC drops JS ownership while an independent worker still uses the object. */
 s=create();assert(esp32_mquickjs_wifi_dpp_service()&&scheduled);esp32_mquickjs_wifi_dpp_session_release(s);run_worker();
 for(unsigned i=0;i<8&&s_dpp_active;i++)tick();reset();return 0;
}
'''
