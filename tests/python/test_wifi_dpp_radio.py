"""Deferred production DPP Radio lease/channel/cleanup admission cases."""
import os
import re
import unittest
from test_wifi_vendor_ie import vendor_code, COMPONENT
from test_wifi_config_controls import sdk_types, structure
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, extract


class DppRadio(unittest.TestCase):
    def test_exact_owners_capture_retirement_and_channel_restore(self):
        if not os.environ.get('IDF_PATH'):
            self.skipTest('Set IDF_PATH to the reviewed SDK')
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = '#define CONFIG_ESP_WIFI_DPP_SUPPORT 1\n' + vendor_code('esp32c3/representative')
        before = sdk_types('esp32c3/representative')
        more = sdk_types('esp32c3/representative', ('wifi_ap_record_t', 'wifi_second_chan_t', 'wifi_ps_type_t'))
        assert more.startswith(before)
        extra = more[len(before):] + TYPES
        extra += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
        extra += structure(header, 'esp32_mquickjs_wifi_radio_operation_t')
        for name in ('connection', 'result', 'worker', 'radio'):
            extra += unit(COMPONENT / 'internal' / f'esp32_mquickjs_wifi_dpp_{name}.h')
        extra += structure(radio, 'wifi_radio_dpp_t') + GLOBALS
        code = code.replace('static struct {\n    int lock;', extra + '\nstatic struct {\n    int lock;', 1)
        code = code.replace('struct {unsigned identity,lease_identity;} operation;', 'esp32_mquickjs_wifi_radio_operation_t operation;')
        code = code.replace('generation,next_lease_identity,next_lifecycle_identity,wake_locks;',
            'generation,next_operation_identity,next_lease_identity,next_lifecycle_identity,wake_locks;')
        code = code.replace('bool driver_owned,', '''unsigned event_phase;wifi_mode_t event_live;
            bool stop_submitted,ap_reopen_pending,ap_reopen_attempted,ap_reopen_quiesced,ap_reopen_restore_complete;unsigned ap_stop_phase;
            unsigned ap_transition_application,ap_transition_station,ap_transition_access_point;
            esp_err_t ap_stop_error,cleanup_error;bool driver_owned,''', 1)
        code += BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for name in ('wifi_radio_record_fault', 'wifi_radio_cleanup_fault', 'wifi_radio_config_equal',
                     'esp32_mquickjs_wifi_radio_pmf_disable_allowed',
                     'wifi_radio_restore_disabled_pmf', 'wifi_radio_connection_owner_locked',
                     'esp32_mquickjs_wifi_radio_end_operation', 'wifi_radio_stop_owners_locked'):
            code += extract(radio, name)
        code += (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_dpp_radio.inc').read_text()
        compile_run(self, code + CASES)


TYPES = r'''
#include <stdlib.h>
#define ESP_ERR_WIFI_NOT_CONNECT -20
#define ESP_ERR_WIFI_NOT_STARTED -21
#define ESP_ERR_WIFI_CONN -24
#define ESP_ERR_INVALID_RESPONSE -22
#define ESP_ERR_NOT_FINISHED -23
#define RADIO_EVENTS_IDLE 0
#define RADIO_EVENTS_START 1
#define RADIO_EVENTS_STOP 2
#define AP_STOP_IDLE 0
typedef struct {uint8_t secret[32];} esp_dpp_config_data_t;
typedef struct {int unused;} wifi_event_action_tx_status_t;
'''
GLOBALS = r'''
static wifi_radio_dpp_t *s_dpp_radio;
static bool wifi_radio_dpp_stop_owner_locked(uint32_t identity);
#define WIFI_RADIO_DPP_PENDING (s_dpp_radio!=NULL)
#define WIFI_RADIO_WPS_PENDING false
#define WIFI_RADIO_SMARTCONFIG_PENDING false
#define WIFI_RADIO_EAP_PENDING false
'''
BOUNDARIES = r'''
struct esp32_mquickjs_wifi_dpp_worker {esp32_mquickjs_wifi_dpp_worker_status_t status;};
static struct esp32_mquickjs_wifi_dpp_worker worker;
static unsigned worker_live,prepares,channel_writes,closes,ap_info_reads;
static int prepare_error,close_error,channel_error;
static bool unknown,associated;
static wifi_config_t station_config;
static wifi_storage_t storage;
static unsigned config_writes,storage_writes;
static int config_error,storage_error;
static bool native_started=true,hold_stop,hold_start;
static unsigned stops,starts,pmf_writes,policy_writes;
static int pmf_error,start_error;
static int8_t power=60;
static wifi_ps_type_t power_save=WIFI_PS_MIN_MODEM;
static uint16_t inactive[2]={60,300};
static struct {unsigned pending;}s_inactive_history;
static uint8_t channel=6;
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t*out){++ap_info_reads;assert(locks&&!critical);memset(out,0,sizeof(*out));
 return !native_started?ESP_ERR_WIFI_NOT_STARTED:associated?ESP_OK:ESP_ERR_WIFI_NOT_CONNECT;}
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t*out){assert(locks&&!critical&&interface==WIFI_IF_STA);*out=station_config;return 0;}
static int (esp_wifi_set_config)(wifi_interface_t interface,wifi_config_t*config){assert(locks&&!critical&&interface==WIFI_IF_STA&&storage==WIFI_STORAGE_RAM);
 config_writes++;if(config_error)return config_error;station_config=*config;station_config.sta.pmf_cfg.capable=true;return 0;}
static int (esp_wifi_set_storage)(wifi_storage_t value){assert(locks&&!critical);storage_writes++;if(storage_error)return storage_error;storage=value;return 0;}
static int (esp_wifi_disable_pmf_config)(wifi_interface_t interface){assert(locks&&!critical&&!native_started&&interface==WIFI_IF_STA&&storage==WIFI_STORAGE_RAM);
 pmf_writes++;station_config.sta.pmf_cfg.capable=station_config.sta.pmf_cfg.required=false;return pmf_error;}
static int esp_wifi_get_ps(wifi_ps_type_t*out){assert(locks&&!critical);*out=power_save;return 0;}
static int (esp_wifi_set_ps)(wifi_ps_type_t value){assert(locks&&!critical&&storage==WIFI_STORAGE_RAM);power_save=value;policy_writes++;return 0;}
static int esp_wifi_get_max_tx_power(int8_t*out){assert(locks&&!critical&&native_started);*out=power;return 0;}
static int (esp_wifi_set_max_tx_power)(int8_t value){assert(locks&&!critical&&native_started&&storage==WIFI_STORAGE_RAM);power=value;policy_writes++;return 0;}
static int esp_wifi_get_inactive_time(wifi_interface_t interface,uint16_t*out){assert(locks&&!critical&&native_started);*out=inactive[interface==WIFI_IF_AP];return 0;}
static int (esp_wifi_set_inactive_time)(wifi_interface_t interface,uint16_t value){assert(locks&&!critical&&native_started&&storage==WIFI_STORAGE_RAM);
 inactive[interface==WIFI_IF_AP]=value;policy_writes++;return 0;}
static void wifi_radio_inactive_history_record(unsigned i,uint16_t value,int error){assert(locks&&!critical&&i<2);(void)value;(void)error;}
static void wifi_radio_set_state(esp32_mquickjs_wifi_radio_driver_state_t state){assert(locks&&!critical);s_radio.driver_state=state;}
/* Unrelated recovery owners cannot grant DPP admission. STOP itself and the
 * fault/cleanup ledger below are production functions, not test state machines. */
typedef struct {unsigned generation,identity;} esp32_mquickjs_wifi_action_token_t;
static struct {esp32_mquickjs_wifi_radio_lease_t lease;struct {unsigned generation,identity;bool dispatching,cancel_busy;}lane;}s_action;
static struct {bool stopped;}s_raw_tx_recovery;
static bool wifi_radio_raw_tx_recovery_owner_locked(const esp32_mquickjs_wifi_radio_lease_t*t){(void)t;return false;}
static bool wifi_radio_raw_tx_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t*t){(void)t;return false;}
static bool wifi_radio_action_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t*t){(void)t;return false;}
static bool wifi_radio_action_exact_locked(const esp32_mquickjs_wifi_action_token_t*t){(void)t;return false;}
static void wifi_radio_capture_stop_snapshot_locked(void){assert(locks&&!critical);}
static int esp_wifi_stop(void){assert(locks&&!critical);stops++;native_started=false;return 0;}
static int wifi_radio_begin_events(unsigned phase,wifi_mode_t mode){assert(locks&&!critical&&
 (phase==RADIO_EVENTS_START||phase==RADIO_EVENTS_STOP)&&
 mode==(phase==RADIO_EVENTS_STOP?s_radio.event_live:s_radio.effective_mode));
 s_radio.event_phase=phase;return 0;}
static int wifi_radio_wait_events(void){assert(!"DPP worker must not touch the JS runtime");return ESP_ERR_INVALID_STATE;}
static int wifi_radio_wait_events_inner(void *runtime){assert(!runtime&&locks&&!critical&&
 (s_radio.event_phase==RADIO_EVENTS_START||s_radio.event_phase==RADIO_EVENTS_STOP));
 if(s_radio.event_phase==RADIO_EVENTS_START?hold_start:hold_stop)return ESP_ERR_NOT_FINISHED;
 s_radio.event_live=s_radio.event_phase==RADIO_EVENTS_START?s_radio.effective_mode:WIFI_MODE_NULL;
 s_radio.event_phase=RADIO_EVENTS_IDLE;return 0;}
static int (esp_wifi_start)(void){assert(locks&&!critical&&!native_started&&!station_config.sta.pmf_cfg.capable);
 starts++;if(start_error)return start_error;native_started=true;power=8;power_save=WIFI_PS_NONE;inactive[0]=3;inactive[1]=10;channel=1;return 0;}
static int wifi_radio_get_channel_locked(uint8_t*p,wifi_second_chan_t*s,uint32_t*g){assert(locks&&!critical);*p=channel;*s=WIFI_SECOND_CHAN_NONE;*g=1;return 0;}
static int wifi_radio_validate_regulatory_channel(uint8_t p){assert(locks&&!critical);return p==6?0:ESP_ERR_INVALID_ARG;}
static int (esp_wifi_set_channel)(uint8_t p,wifi_second_chan_t s){assert(locks&&!critical&&s==WIFI_SECOND_CHAN_NONE);channel_writes++;
 if(channel_error)return channel_error;channel=p;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_create(const esp32_mquickjs_wifi_dpp_worker_options_t*o,esp32_mquickjs_wifi_dpp_worker_t**out){
 assert(!locks&&!critical&&!*out&&!worker_live);if(!o->channels[0])return ESP_ERR_INVALID_ARG;
 worker_live++;memset(&worker,0,sizeof(worker));*out=&worker;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_prepare(esp32_mquickjs_wifi_dpp_worker_t*w){assert(locks&&!critical&&w==&worker);prepares++;return prepare_error;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_listen(esp32_mquickjs_wifi_dpp_worker_t*w){assert(locks&&!critical&&w==&worker);channel=11;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_status(esp32_mquickjs_wifi_dpp_worker_t*w,esp32_mquickjs_wifi_dpp_worker_status_t*out){
 assert(locks&&!critical&&w==&worker);*out=w->status;out->handoff_unknown=unknown;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_finish_capture(esp32_mquickjs_wifi_dpp_worker_t*w){assert(locks&&!critical&&w==&worker);
 if(close_error)return close_error;w->status.capture_retired=true;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_close(esp32_mquickjs_wifi_dpp_worker_t*w){assert(locks&&!critical&&w==&worker);closes++;
 if(close_error)return close_error;w->status.capture_retired=w->status.retired=true;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_release(esp32_mquickjs_wifi_dpp_worker_t**w){assert(locks&&!critical&&*w==&worker&&worker.status.retired);
 worker_live--;*w=NULL;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_uri(esp32_mquickjs_wifi_dpp_worker_t*w,char*out,size_t cap,bool commit){
 assert(locks&&!critical&&w==&worker);if(!commit){assert(cap>6);memcpy(out,"DPP:X;",7);}return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_config(esp32_mquickjs_wifi_dpp_worker_t*w,unsigned i,esp_dpp_config_data_t*out){
 assert(locks&&!critical&&w==&worker&&w->status.capture_retired&&channel==6&&i==0);memset(out,1,sizeof(*out));return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_configs_commit(esp32_mquickjs_wifi_dpp_worker_t*w){assert(locks&&!critical&&w==&worker&&channel==6);return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_connection_prepare(const esp_dpp_config_data_t*r,esp32_mquickjs_wifi_dpp_auth_t requested,
 esp32_mquickjs_wifi_dpp_auth_t*selected,wifi_config_t*config){if(!r||r->secret[0]!=1)return ESP_ERR_INVALID_ARG;
 memset(config,0,sizeof(*config));memcpy(config->sta.ssid,"chosen",7);config->sta.pmf_cfg.capable=true;
 *selected=requested?requested:ESP32_MQUICKJS_DPP_AUTH_CONNECTOR;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_select(esp32_mquickjs_wifi_dpp_worker_t*w,const esp_dpp_config_data_t*r,
 esp32_mquickjs_wifi_dpp_auth_t requested,esp32_mquickjs_wifi_dpp_auth_t*selected,wifi_config_t*config){
 assert(locks&&!critical&&w==&worker&&w->status.capture_retired&&storage==WIFI_STORAGE_RAM);
 int e=esp32_mquickjs_wifi_dpp_connection_prepare(r,requested,selected,config);if(e)return e;
 w->status.capture_retired=false;w->status.connection_selected=true;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_check_connection(esp32_mquickjs_wifi_dpp_worker_t*w,
 esp32_mquickjs_wifi_dpp_auth_t selected,const uint8_t bssid[6]){
 assert(locks&&!critical&&w==&worker&&associated&&selected==ESP32_MQUICKJS_DPP_AUTH_CONNECTOR&&bssid[0]==2);return 0;}
'''
CASES = r'''
static esp32_mquickjs_wifi_radio_lease_t dpp_owners[3];
static void setup(void){assert(!s_dpp_radio&&!worker_live);reset_vendor();memset(dpp_owners,0,sizeof(dpp_owners));
 s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_operation_identity=1;
 wifi_radio_operation_lock();
 assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&dpp_owners[0]));
 assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&dpp_owners[1]));
 wifi_radio_operation_unlock();channel=6;prepares=channel_writes=closes=0;prepare_error=close_error=channel_error=0;unknown=associated=false;
 station_config=(wifi_config_t){.sta.ssid="saved",.sta.pmf_cfg.capable=true};storage=s_radio.storage=WIFI_STORAGE_FLASH;
 s_radio.storage_configured=true;s_radio.stop_required=true;config_writes=storage_writes=0;config_error=storage_error=0;
 native_started=true;s_radio.event_live=s_radio.effective_mode;hold_stop=hold_start=false;stops=starts=pmf_writes=policy_writes=0;pmf_error=start_error=0;
 power=60;power_save=WIFI_PS_MIN_MODEM;inactive[0]=60;inactive[1]=300;s_inactive_history.pending=0;}
static void pinned(void){wifi_radio_operation_lock();for(unsigned i=0;i<3;i++)if(dpp_owners[i].acquired){
 wifi_radio_release_locked(&dpp_owners[i]);assert(wifi_radio_lease_valid(&dpp_owners[i]));}wifi_radio_operation_unlock();}
int main(void){setup();esp32_mquickjs_wifi_dpp_worker_options_t o={.channels="6"};
 esp32_mquickjs_wifi_radio_operation_t token={0};esp32_mquickjs_wifi_dpp_radio_status_t status;
 o.channels[0]=0;assert(esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status)==ESP_ERR_INVALID_ARG&&!prepares&&!token.identity);
 memcpy(o.channels,"11",3);assert(esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status)==ESP_ERR_INVALID_ARG&&!prepares&&!worker_live);
 memcpy(o.channels,"6",2);assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status));pinned();
 /* Invalid-channel admission above already closed its unpublished worker. */
 assert(closes==1);unsigned before_stale_close=closes;
 esp32_mquickjs_wifi_radio_operation_t stale=token;stale.generation++;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&stale,&status)==ESP_ERR_INVALID_STATE&&closes==before_stale_close);
 esp32_mquickjs_wifi_radio_end_operation(&token);assert(token.identity);pinned();
 esp_dpp_config_data_t row;assert(esp32_mquickjs_wifi_radio_dpp_config(&token,0,&row)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_radio_dpp_listen(&token,&status)&&channel==11);
 close_error=ESP_ERR_NOT_FINISHED;assert(esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status)==ESP_ERR_NOT_FINISHED&&!channel_writes);pinned();
 close_error=0;channel_error=88;assert(esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status)==88&&token.identity);pinned();
 channel_error=0;assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status)&&channel==6&&status.channel_restored);
 unsigned writes=channel_writes;assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status)&&channel_writes==writes);
 assert(!esp32_mquickjs_wifi_radio_dpp_config(&token,0,&row)&&row.secret[0]==1);
 assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status)&&!token.identity&&!s_dpp_radio&&!worker_live);setup();
 /* Connect keeps the same Radio token. Native close is forbidden until the
  * exact Station generation retires, and restoration retries only its suffix. */
 assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_listen(&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status));
 memset(&row,1,sizeof(row));wifi_config_t selected_config;esp32_mquickjs_wifi_dpp_auth_t selected;
 station_config.sta.pmf_cfg.capable=false;
 assert(esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,false,&selected,&selected_config,&status)==ESP_ERR_NOT_SUPPORTED&&!storage_writes);
 assert(!selected&&!selected_config.sta.ssid[0]);station_config.sta.pmf_cfg.capable=true;
 assert(!esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,false,&selected,&selected_config,&status)&&storage==WIFI_STORAGE_RAM);
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_begin(&token));
 assert(esp32_mquickjs_wifi_radio_dpp_connection_begin(&token)==ESP_ERR_INVALID_STATE);
 associated=true;station_config=selected_config;channel=11;unsigned closed=closes;
 const uint8_t bssid[6]={2,3,4,5,6,7};assert(!esp32_mquickjs_wifi_radio_dpp_check_connection(&token,selected,bssid));
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_INVALID_STATE&&closes==closed);pinned();
 stale=token;stale.lease_identity++;
 assert(esp32_mquickjs_wifi_radio_dpp_connection_end(&stale)==ESP_ERR_INVALID_STATE);
 associated=false;assert(!esp32_mquickjs_wifi_radio_dpp_connection_end(&token));
 config_error=77;assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==77&&token.identity&&storage==WIFI_STORAGE_RAM);pinned();
 config_error=0;storage_error=88;assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==88&&token.identity);
 assert(!strcmp((char*)station_config.sta.ssid,"saved")&&channel==6&&status.config_restored&&!status.storage_restored);pinned();
 writes=config_writes;storage_error=0;assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status));
 assert(config_writes==writes&&storage==WIFI_STORAGE_FLASH&&!token.identity);setup();
 /* Eligible disabled PMF: exact dpp_owners stay pinned across delayed STOP,
  * partial PMF failure, delayed START and policy/storage restoration. */
 station_config.sta.pmf_cfg.capable=false;station_config.sta.disable_wpa3_compatible_mode=true;
 assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_listen(&token,&status));assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,false,&selected,&selected_config,&status)&&status.restore_requires_restart);
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_begin(&token));station_config=selected_config;
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_end(&token));hold_stop=true;
 /* A foreign or changed owner must fail before physical STOP. */
 s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=500;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_INVALID_STATE&&!stops);
 s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=0;
 wifi_radio_live_lease_t *station_owner=wifi_radio_promiscuous_owner(dpp_owners[1].identity);
 station_owner->client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_INVALID_STATE&&!stops);
 station_owner->client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED&&stops==1&&!config_writes);pinned();
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED&&stops==1);pinned();
 hold_stop=false;pmf_error=77;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==77&&status.restore_stopped&&config_writes==1&&pmf_writes==1&&!starts);pinned();
 pmf_error=0;hold_start=true;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED&&starts==1&&config_writes==1&&pmf_writes==1);pinned();
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED&&starts==1);hold_start=false;
 storage_error=88;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==88&&status.restore_started&&token.identity);pinned();
 unsigned restored_policies=policy_writes;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==88&&policy_writes==restored_policies&&stops==1&&starts==1);
 storage_error=0;assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status)&&!token.identity);
 assert(native_started&&!station_config.sta.pmf_cfg.capable&&storage==WIFI_STORAGE_FLASH&&channel==6);
 assert(power==60&&power_save==WIFI_PS_MIN_MODEM&&inactive[0]==60&&stops==1&&starts==1);setup();
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
 s_radio.effective_mode=WIFI_MODE_APSTA;wifi_radio_operation_lock();
 assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&dpp_owners[2]));wifi_radio_operation_unlock();
 station_config.sta.pmf_cfg.capable=false;station_config.sta.disable_wpa3_compatible_mode=true;
 assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,true,&o,&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_listen(&token,&status));assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status));
 assert(esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,false,&selected,&selected_config,&status)==ESP_ERR_INVALID_STATE&&!storage_writes);
 assert(!esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,true,&selected,&selected_config,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_begin(&token));station_config=selected_config;
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_end(&token));hold_stop=true;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED);pinned();
 hold_stop=false;assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status)&&inactive[1]==300&&stops==1&&starts==1);setup();
#endif
 /* Failed prepare never authorizes channel writes over a foreign owner. */
 prepare_error=77;assert(esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status)==77&&token.identity);pinned();
 channel=11;assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status)&&channel==11&&!channel_writes);setup();
 assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status));unknown=true;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_INVALID_STATE&&token.identity);pinned();
 unknown=false;assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status));setup();
 /* An SDK START error retains the original fault and every helper owner.
  * Removing the injected error is not authorization to repeat the mutation. */
 station_config.sta.pmf_cfg.capable=false;station_config.sta.disable_wpa3_compatible_mode=true;
 assert(!esp32_mquickjs_wifi_radio_dpp_begin(dpp_owners,false,&o,&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_listen(&token,&status));assert(!esp32_mquickjs_wifi_radio_dpp_finish_capture(&token,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_select(&token,&row,0,false,&selected,&selected_config,&status));
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_begin(&token));station_config=selected_config;
 assert(!esp32_mquickjs_wifi_radio_dpp_connection_end(&token));start_error=99;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==99&&stops==1&&starts==1&&token.identity);
 assert(s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_FAULTED&&s_radio.fault_error==99&&
     !strcmp(s_radio.fault_stage,"dpp-restore-start")&&!status.restore_started);pinned();
 start_error=0;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==99&&starts==1&&stops==1&&storage==WIFI_STORAGE_RAM);pinned();
 stale=token;stale.generation++;
 assert(esp32_mquickjs_wifi_radio_dpp_recover(&stale,&status)==ESP_ERR_INVALID_STATE);
 s_radio.fault_stage="another-source";
 assert(esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)==ESP_ERR_INVALID_STATE);
 s_radio.fault_stage="dpp-restore-start";unknown=true;
 assert(esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)==ESP_ERR_INVALID_STATE);unknown=false;
 s_dpp_radio->restore_recovery_attempts=UINT32_MAX;
 assert(esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)==ESP_ERR_NO_MEM&&!s_dpp_radio->restore_recovery_pending);
 s_dpp_radio->restore_recovery_attempts=0;
 assert(!esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)&&status.restore_recovery_pending&&status.restore_recovery_attempts==1);
 assert(!esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)&&status.restore_recovery_attempts==1&&stops==1&&starts==1);
 hold_stop=true;unsigned before_recovery_ap_reads=ap_info_reads;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==ESP_ERR_NOT_FINISHED&&stops==2&&starts==1&&s_radio.fault_error==99);pinned();
 assert(ap_info_reads==before_recovery_ap_reads); /* Failed START is retired by STOP/fence, not a live-link query. */
 assert(!esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)&&status.restore_recovery_attempts==1);
 hold_stop=false;start_error=98;
 assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==98&&stops==2&&starts==2&&s_radio.fault_error==98);
 assert(!status.restore_recovery_pending&&status.restore_start_failed&&status.restore_start_error==98);pinned();
 start_error=0;assert(esp32_mquickjs_wifi_radio_dpp_close(&token,&status)==98&&starts==2);
 assert(!esp32_mquickjs_wifi_radio_dpp_recover(&token,&status)&&status.restore_recovery_attempts==2);
 assert(!esp32_mquickjs_wifi_radio_dpp_close(&token,&status)&&!token.identity&&stops==3&&starts==3);
 assert(!s_radio.fault_stage&&!s_radio.cleanup_stage&&native_started&&storage==WIFI_STORAGE_FLASH&&
     !station_config.sta.pmf_cfg.capable&&power==60&&inactive[0]==60);
 assert(!critical&&!locks);return 0;
}
'''
