"""Controlled scheduling of the production NAN Session. Execution is deferred."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'components/esp32_mquickjs'


def without_includes(path):
    source = path.read_text()
    if path.name == 'esp32_mquickjs_wifi_nan_radio.h':
        from test_wifi_nan_query import query_types
        source = query_types() + source
    if path.name == 'esp32_mquickjs_wifi_nan_discovery.h':
        source = (BASE / 'internal/esp32_mquickjs_wifi_nan_service_config.h').read_text() + '\n' + source
    if path.name == 'esp32_mquickjs_wifi_nan_session.c':
        for name in ('esp32_mquickjs_wifi_nan_discovery.inc', 'esp32_mquickjs_wifi_nan_message.inc', 'esp32_mquickjs_wifi_nan_path.inc', 'esp32_mquickjs_wifi_nan_pairing.inc', 'esp32_mquickjs_wifi_nan_credentials.inc', 'esp32_mquickjs_wifi_nan_bootstrap.inc'):
            source = source.replace('#include "' + name + '"', (path.parent / name).read_text())
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', source, flags=re.M)


class NanSessionLifecycle(unittest.TestCase):
    def test_query_requires_active_ready_session_and_serializes_outside_critical(self):
        self.compile_case(r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t cfg={.op_channel=6};
 esp32_mquickjs_wifi_nan_session_t*s=NULL;assert(!esp32_mquickjs_wifi_nan_session_create(&cfg,1000,&s));
 esp32_mquickjs_wifi_nan_query_t q={.kind=ESP32_MQUICKJS_NAN_QUERY_SERVICE,.service_id=7};
 esp32_mquickjs_wifi_nan_query_result_t out;
 assert(esp32_mquickjs_wifi_nan_session_query(s,&q,&out)==ESP_ERR_INVALID_STATE&&!query_reads);
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));
 clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);assert(s->status.ready);
 assert(!esp32_mquickjs_wifi_nan_session_query(s,&q,&out)&&query_reads==1&&out.service_id==7);
 s->status.usd=true;assert(esp32_mquickjs_wifi_nan_session_query(s,&q,&out)==ESP_ERR_NOT_SUPPORTED&&query_reads==1);
 s->status.usd=false;s_nan_runtime_closing=true;
 assert(esp32_mquickjs_wifi_nan_session_query(s,&q,&out)==ESP_ERR_INVALID_STATE&&query_reads==1);
 s_nan_runtime_closing=false;esp32_mquickjs_wifi_nan_session_close(s,false);
 assert(esp32_mquickjs_wifi_nan_session_query(s,&q,&out)==ESP_ERR_INVALID_STATE&&query_reads==1);
 clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);assert(s->status.retired);
 assert(esp32_mquickjs_wifi_nan_session_query(s,&q,&out)==ESP_ERR_INVALID_STATE&&query_reads==1);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
''')

    def test_native_timer_or_pool_fault_starts_close_without_observation_event(self):
        self.compile_case(r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t cfg={.op_channel=6};
 esp32_mquickjs_wifi_nan_session_t*s=NULL;assert(!esp32_mquickjs_wifi_nan_session_create(&cfg,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));
 clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);assert(s->status.ready);
 native_pool_error=-73;clock_us+=100000;
 assert(esp32_mquickjs_wifi_nan_service()&&s->status.closing&&s->status.error==-73);
 run_queued(NULL);assert(s->status.retired&&!s_nan_active);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
''')

    def test_production_session_oom_queue_timeout_close_and_runtime_teardown(self):
        self.compile_case(MAIN)

    def test_production_discovery_early_event_cancel_retirement_and_gc_owners(self):
        self.compile_case(DISCOVERY_MAIN)

    def test_production_message_timeout_late_completion_and_parent_close(self):
        self.compile_case(MESSAGE_MAIN)

    def test_usd_only_admission_and_failed_cleanup_retain_native_owner(self):
        self.compile_case(r'''
int main(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());
 wifi_nan_sync_config_t cfg={.op_channel=6};
 esp32_mquickjs_wifi_nan_session_t*s=NULL;
 assert(esp32_mquickjs_wifi_nan_session_create(&cfg,1000,&s)==ESP_ERR_NOT_SUPPORTED);
 assert(!s&&!allocation_calls&&!begins&&!s_nan_handles);
 fail_allocation=1;
 assert(esp32_mquickjs_wifi_nan_session_create_usd(1000,&s)==ESP_ERR_NO_MEM);
 assert(!s&&!allocations&&!s_nan_handles);
 fail_allocation=0;
 assert(!esp32_mquickjs_wifi_nan_session_create_usd(1000,&s)&&s->status.usd);
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));
 assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
 assert(s->status.ready&&s->status.native.usd&&begins==1);
 esp32_mquickjs_wifi_nan_global_status_t global;
 esp32_mquickjs_wifi_nan_global_status(&global);
 assert(!global.active_paths&&!global.path_handles);
 close_error=-77;esp32_mquickjs_wifi_nan_session_close(s,false);
 clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
 assert(!s->status.retired&&s_nan_active==s&&s->status.cleanup_error==-77);
 assert(native_started&&allocations&&begins==1);
 close_error=0;clock_us+=100000;
 assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
 assert(s->status.retired&&!s_nan_active&&!native_started);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
''', usd_only=True)

    def compile_case(self, main, *, security=False, usd_only=False, pairing=False):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('Host C compiler unavailable')
        source = PREFIX.replace("#define CONFIG_ESP_WIFI_NAN_SECURITY 0", "#define CONFIG_ESP_WIFI_NAN_SECURITY 1") if security else PREFIX
        if pairing:
            source += "\n#define CONFIG_ESP_WIFI_NAN_PAIRING 1\n"
        if usd_only:
            source = source.replace('#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1',
                '#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 0\n#define CONFIG_ESP_WIFI_NAN_USD_ENABLE 1')
        for name in ('esp32_mquickjs_wifi_nan_pasn_sdk.h', 'esp32_mquickjs_wifi_nan_sdk.h', 'esp32_mquickjs_wifi_nan_ndp.h', 'esp32_mquickjs_wifi_nan_tx.h', 'esp32_mquickjs_wifi_nan_radio.h',
                     'esp32_mquickjs_wifi_nan_session.h', 'esp32_mquickjs_wifi_nan_discovery.h', 'esp32_mquickjs_wifi_nan_message.h', 'esp32_mquickjs_wifi_nan_path.h', 'esp32_mquickjs_wifi_nan_pairing.h'):
            source += without_includes(BASE / 'internal' / name)
        source += BOUNDARY
        if pairing:
            from test_wifi_nan_pairing_session import PAIRING_BOUNDARY
            source += PAIRING_BOUNDARY
        source += without_includes(BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_session.c')
        source += main
        with tempfile.TemporaryDirectory() as folder:
            path, executable = Path(folder) / 'case.c', Path(folder) / 'case'
            path.write_text(source)
            result = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                str(path), '-o', str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)


PREFIX = r'''
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define CONFIG_ESP_WIFI_NAN_SECURITY 0
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 101
#define ESP_ERR_INVALID_STATE 102
#define ESP_ERR_NO_MEM 103
#define ESP_ERR_TIMEOUT 104
#define ESP_ERR_NOT_SUPPORTED 105
#define ESP_ERR_INVALID_RESPONSE 106
#define ESP_ERR_NOT_FOUND 107
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 1
typedef int esp_err_t;
#define ESP_WIFI_NAN_MAX_SVC_SUPPORTED 2
#define ESP_WIFI_NAN_DATAPATH_MAX_PEERS 2
#define ESP_WIFI_MAX_SVC_NAME_LEN 256
#define ESP_WIFI_MAX_FILTER_LEN 256
#define ESP_WIFI_MAX_SVC_SSI_LEN 512
#define ESP_WIFI_MAX_FUP_SSI_LEN 2048
#define NAN_VENDOR_IE_MAX_BODY_LEN 255
#define ESP_WIFI_NAN_MAX_CREDS_PER_SVC 4
#define WIFI_NAN_CSID_NCS_SK_128 1
#define WIFI_NAN_CSID_NCS_PK_PASN_128 7
#define WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY 2
#define WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD 32
#define ESP_WIFI_NAN_NDP_PMK_LEN 32
typedef struct {uint8_t csid;uint8_t use_pmk:1,reserved:7;char passphrase[64];uint8_t pmk[32];}wifi_nan_credential_t;
typedef struct {uint8_t group_data_prot:1,group_mgmt_prot:1,reserved:6;uint8_t num_credentials;wifi_nan_credential_t creds[4];}wifi_nan_discovery_security_params_t;
typedef struct {bool pairing_setup,npk_nik_caching,pairing_verification;uint8_t reserved;uint16_t bootstrapping_methods,comeback_delay;}wifi_nan_pairing_cfg_t;
typedef struct {uint8_t vendor_oui[3];uint16_t body_len;uint8_t *body;}nan_vendor_ie_t;
#define WIFI_EVENT_NAN_SVC_MATCH 1
#define WIFI_EVENT_NAN_REPLIED 2
#define WIFI_EVENT_NAN_RECEIVE 3
typedef struct {uint16_t ghz_2_channels;uint32_t ghz_5_channels;}wifi_scan_channel_bitmap_t;
typedef struct {uint8_t usd_default_channel,n_min,n_max,m_min,m_max;wifi_scan_channel_bitmap_t usd_chan_bitmap;}wifi_nan_usd_config_t;
typedef struct {
    char service_name[256],matching_filter[256];uint8_t *ssi;uint16_t ssi_len;int type;
    wifi_nan_discovery_security_params_t *security_cfg;wifi_nan_pairing_cfg_t *pairing;nan_vendor_ie_t *vendor_ie;
    bool security_reqd,usd_discovery_flag,datapath_reqd,ndp_resp_needed,single_replied_event,single_match_event;
    uint32_t ttl;wifi_nan_usd_config_t usd_publish_config,usd_subscribe_config;
} wifi_nan_publish_cfg_t;
typedef wifi_nan_publish_cfg_t wifi_nan_subscribe_cfg_t;
typedef struct {uint8_t pub_id,peer_mac[6];bool confirm_required;} wifi_nan_datapath_req_t;
typedef struct {uint8_t inst_id,peer_inst_id,peer_mac[6];uint16_t ssi_len;uint8_t *ssi;nan_vendor_ie_t *vendor_ie;}wifi_nan_followup_params_t;
typedef struct {uint8_t subscribe_id,publish_id,pub_if_mac[6],ssi_version;
    bool datapath_reqd,security_reqd,fsd_reqd,fsd_gas,ndpe_support;uint16_t ssi_len;uint8_t ssi[];}wifi_event_nan_svc_match_t;
typedef struct {uint8_t publish_id,subscribe_id,sub_if_mac[6];uint16_t ssi_len;uint8_t ssi[];}wifi_event_nan_replied_t;
typedef struct {uint8_t inst_id,peer_inst_id,peer_if_mac[6];uint16_t ssi_len;uint8_t ssi[];}wifi_event_nan_receive_t;
typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;
typedef struct {uint32_t generation, lease_identity, identity; int kind;} esp32_mquickjs_wifi_radio_operation_t;
typedef struct {
    uint8_t op_channel, master_pref, scan_time; uint16_t warm_up_sec;
    bool disable_random_mac, reset_current_nvs_creds, use_nvs_for_caching, group_mgmt_prot;
} wifi_nan_sync_config_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
static _Thread_local unsigned lock_depth;
#define portENTER_CRITICAL(p) do {assert(!lock_depth);assert(!pthread_mutex_lock(p));++lock_depth;} while(0)
#define portEXIT_CRITICAL(p) do {assert(lock_depth==1);--lock_depth;assert(!pthread_mutex_unlock(p));} while(0)
'''

BOUNDARY = r'''
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
static esp_err_t native_pool_error;
static unsigned query_reads;
esp_err_t esp32_mquickjs_wifi_radio_nan_query(const esp32_mquickjs_wifi_radio_operation_t *token,
    const esp32_mquickjs_wifi_nan_query_t *query, esp32_mquickjs_wifi_nan_query_result_t *out){
    assert(!lock_depth&&token->identity&&query);++query_reads;
    *out=(esp32_mquickjs_wifi_nan_query_result_t){.service_id=7};return ESP_OK;
}
void esp32_mquickjs_wifi_nan_tx_status(esp32_mquickjs_wifi_nan_tx_status_t *out){
    *out=(esp32_mquickjs_wifi_nan_tx_status_t){.error=native_pool_error};
}
#endif
static unsigned allocations, allocation_calls, fail_allocation, begins, closes;
static int64_t clock_us;
static int64_t start_advance_us;
static bool queue_full, pause_begin, begin_entered, resume_begin;
static int begin_error, close_error;
static bool native_started;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static void (*queued)(void *);
static void *queued_arg;
static esp32_mquickjs_wifi_nan_sdk_observer_fn native_observer;
static void *native_opaque;
static int64_t esp_timer_get_time(void) {return clock_us;}
static void *esp32_mquickjs_memory_wireless_calloc(const char *owner, size_t count, size_t size, int cls, int role) {
    assert(!lock_depth && !strcmp(owner, "wifi.nan") && !cls && role==1);
    if (++allocation_calls==fail_allocation) return NULL;
    void *p=calloc(count,size); if(p)++allocations; return p;
}
static void esp32_mquickjs_memory_payload_free(void *p) {assert(!lock_depth&&p&&allocations);--allocations;free(p);}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n) {memset(p,0,n);}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *p) {
    assert(!lock_depth); if(queue_full)return false;assert(!queued);queued=fn;queued_arg=p;return true;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_begin(const wifi_nan_sync_config_t *config,
    esp32_mquickjs_wifi_nan_sdk_observer_fn observer,void *opaque,
    esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_nan_radio_status_t *status) {
    assert(!lock_depth&&config->op_channel==6);++begins;
    native_observer=observer;native_opaque=opaque;
    if(begin_error){status->error=begin_error;status->stage="nan-admission";return begin_error;}
    native_started=true;
    *token=(esp32_mquickjs_wifi_radio_operation_t){.generation=7,.identity=begins,.lease_identity=3};
    *status=(esp32_mquickjs_wifi_nan_radio_status_t){.operation=*token,.start_attempted=true,.start_accepted=true,.ready=true,.reserved_bytes=32};
    esp32_mquickjs_wifi_nan_sdk_notice_t notice={.kind=ESP32_MQUICKJS_NAN_SDK_STARTED};
    observer(begins,&notice,opaque);
    pthread_mutex_lock(&gate);
    if(pause_begin){begin_entered=true;pthread_cond_broadcast(&condition);while(!resume_begin)pthread_cond_wait(&condition,&gate);}
    pthread_mutex_unlock(&gate);
    clock_us += start_advance_us;
    return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_usd_begin(esp32_mquickjs_wifi_nan_sdk_observer_fn observer,void *opaque,
    esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_nan_radio_status_t *status){
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    wifi_nan_sync_config_t config={.op_channel=6};
    esp_err_t error=esp32_mquickjs_wifi_radio_nan_begin(&config,observer,opaque,token,status);
    status->usd=true;return error;
#else
    (void)observer;(void)opaque;(void)token;(void)status;return ESP_ERR_NOT_SUPPORTED;
#endif
}
esp_err_t esp32_mquickjs_wifi_radio_nan_poll(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_nan_radio_status_t *status){(void)token;(void)status;
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
esp_err_t esp32_mquickjs_wifi_radio_nan_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_nan_radio_status_t *status) {
    assert(!lock_depth&&token->identity&&allocations);++closes;
    status->ready=false;status->closing=true;
    status->cleanup_error=close_error;status->cleanup_stage=close_error?"nan-stop-events":NULL;
    if(!close_error){native_started=false;memset(token,0,sizeof(*token));status->operation=*token;status->reserved_bytes=0;}
    return close_error;
}
static void *run_queued(void *unused) {
    (void)unused;assert(queued);void (*fn)(void *)=queued;void *arg=queued_arg;queued=NULL;queued_arg=NULL;fn(arg);return NULL;
}
'''

SERVICE_BOUNDARY = r'''
struct esp32_mquickjs_event_queue {unsigned references,sent;bool closed,full;esp32_mquickjs_wifi_nan_discovery_event_t last;};
static unsigned service_starts,service_cancels;
static bool service_cancel_pending,omit_binding,emit_early=true;
static int service_start_error;
static int64_t service_advance_us;
static uint8_t next_service_id=7;
bool esp32_mquickjs_event_queue_retain(esp32_mquickjs_event_queue_t*q){assert(q&&q->references);++q->references;return true;}
void esp32_mquickjs_event_queue_release(esp32_mquickjs_event_queue_t*q){assert(q&&q->references);--q->references;}
bool esp32_mquickjs_event_queue_is_closed(const esp32_mquickjs_event_queue_t*q){return q->closed;}
void esp32_mquickjs_event_queue_request_close(esp32_mquickjs_event_queue_t*q){q->closed=true;}
bool esp32_mquickjs_event_queue_try_send_from_callback(esp32_mquickjs_event_queue_t*q,const void *event){
 assert(!lock_depth);if(q->closed||q->full)return false;++q->sent;q->last=*(const esp32_mquickjs_wifi_nan_discovery_event_t*)event;return true;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_service_start(const esp32_mquickjs_wifi_radio_operation_t*t,
 const wifi_nan_publish_cfg_t*p,const wifi_nan_subscribe_cfg_t*u,uint8_t*id){
 assert(!lock_depth&&t->identity&&(!p!=!u)&&id&&!*id);++service_starts;
 if(service_start_error)return service_start_error;
 *id=next_service_id;
 esp32_mquickjs_wifi_nan_sdk_notice_t n={.kind=ESP32_MQUICKJS_NAN_SDK_SERVICE_BOUND,.service_id=*id,.context=p?2:1};
 if(!omit_binding)native_observer(begins,&n,native_opaque);
 if(emit_early){
  struct{wifi_event_nan_replied_t h;uint8_t tail[4];}e={.h={.publish_id=*id,.subscribe_id=3,.ssi_len=3},.tail={1,2,3,0}};
  memcpy(e.h.ssi,"abc",3);n=(esp32_mquickjs_wifi_nan_sdk_notice_t){.kind=ESP32_MQUICKJS_NAN_SDK_EVENT,.event_id=WIFI_EVENT_NAN_REPLIED,.data=&e,.size=sizeof(e)};
  native_observer(begins,&n,native_opaque);
 }
 clock_us+=service_advance_us;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_service_close(const esp32_mquickjs_wifi_radio_operation_t*t,uint8_t id,bool*cancelled){
 assert(!lock_depth&&t->identity&&id&&cancelled);if(!*cancelled){++service_cancels;*cancelled=true;}
 return service_cancel_pending?ESP_ERR_TIMEOUT:0;
}
'''
MESSAGE_BOUNDARY = r'''
static esp32_mquickjs_wifi_nan_message_tx_status_t message_tx;
static unsigned message_sends;
static bool message_inline;
bool esp32_mquickjs_wifi_nan_tx_message_status(uint32_t id,esp32_mquickjs_wifi_nan_message_tx_status_t*out){
 assert(!lock_depth);if(!native_started||message_tx.identity!=id)return false;*out=message_tx;return true;
}
esp_err_t esp32_mquickjs_wifi_nan_tx_message_release(uint32_t id){
 assert(!lock_depth);if(message_tx.identity!=id)return ESP_ERR_INVALID_STATE;
 if(!message_tx.buffer_retired)return ESP_ERR_TIMEOUT;
 memset(&message_tx,0,sizeof(message_tx));return 0;
}
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
bool esp32_mquickjs_wifi_nan_usd_message_status(uint32_t id,esp32_mquickjs_wifi_nan_message_tx_status_t*out){
 return esp32_mquickjs_wifi_nan_tx_message_status(id,out);
}
esp_err_t esp32_mquickjs_wifi_nan_usd_message_release(uint32_t id){
 return esp32_mquickjs_wifi_nan_tx_message_release(id);
}
#endif
esp_err_t esp32_mquickjs_wifi_radio_nan_message_send(const esp32_mquickjs_wifi_radio_operation_t*op,
 wifi_nan_followup_params_t*params,uint32_t id,uint32_t*context){
 assert(!lock_depth&&native_started&&op->identity&&params->inst_id==7&&params->peer_inst_id==3&&id&&context&&!*context);
 ++message_sends;*context=1234;
 message_tx=(esp32_mquickjs_wifi_nan_message_tx_status_t){.identity=id,.ticket=message_sends,.entered=true,.allocated=true,
  .tx_done=message_inline,.tx_succeeded=message_inline,.completed_us=clock_us};return 0;
}
'''
PATH_BOUNDARY = r'''
/* Inject protocol outcomes at the Radio/ledger boundary. The complete
 * production Session/path worker and ownership transfer run above it. */
static esp32_mquickjs_wifi_nan_ndp_status_t path_native[2];
static uint32_t path_next_native=100;
static unsigned path_requests,path_responses,path_ends,path_releases;
static int path_request_error,path_response_error,path_end_error,path_release_error;
static bool path_inline,path_response_accept;
static esp32_mquickjs_wifi_nan_ndp_status_t *path_find(uint32_t id){
 for(unsigned i=0;i<2;i++){if(id&&path_native[i].identity==id)return &path_native[i];}
 return NULL;
}
bool esp32_mquickjs_wifi_nan_ndp_status(uint32_t id,esp32_mquickjs_wifi_nan_ndp_status_t*out){
 assert(!lock_depth);esp32_mquickjs_wifi_nan_ndp_status_t*p=path_find(id);
 if(!p||!native_started)return false;
 *out=*p;return true;
}
unsigned esp32_mquickjs_wifi_nan_ndp_list(esp32_mquickjs_wifi_nan_ndp_status_t out[ESP_WIFI_NAN_DATAPATH_MAX_PEERS]){
 assert(!lock_depth);unsigned count=0;
 if(native_started){for(unsigned i=0;i<2;i++){if(path_native[i].identity)out[count++]=path_native[i];}}
 return count;
}
bool esp32_mquickjs_wifi_nan_ndp_ssi(uint32_t id,uint8_t out[ESP_WIFI_MAX_SVC_SSI_LEN],uint16_t*length){
 assert(!lock_depth&&out&&length);if(!path_find(id))return false;*length=3;memcpy(out,"ndp",3);return true;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_request(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint8_t service,wifi_nan_datapath_req_t*request,uint32_t*identity){
 assert(!lock_depth&&t->identity&&service==7&&request->pub_id==3&&identity&&!*identity);++path_requests;
 if(path_request_error)return path_request_error;
 assert(!path_native[0].identity);*identity=path_next_native++;
 path_native[0]=(esp32_mquickjs_wifi_nan_ndp_status_t){.identity=*identity,.service_id=service,
  .publisher_id=3,.ndp_id=9,.native_bound=true,.host_bound=true,.outgoing=true,.submitted=true,
  .started_us=clock_us,.accepted=path_inline,.completed_us=path_inline?clock_us:0};
 memcpy(path_native[0].peer,request->peer_mac,6);return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_response(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint32_t id,bool accept,const uint8_t*ssi,uint16_t length){
 assert(!lock_depth&&t->identity&&(!length||ssi));++path_responses;path_response_accept=accept;
 esp32_mquickjs_wifi_nan_ndp_status_t*p=path_find(id);assert(p&&!p->response_attempted);
 p->response_attempted=true;return path_response_error;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_end(const esp32_mquickjs_wifi_radio_operation_t*t,uint32_t id){
 assert(!lock_depth&&t->identity);++path_ends;esp32_mquickjs_wifi_nan_ndp_status_t*p=path_find(id);
 assert(p&&!p->end_attempted);p->end_attempted=true;p->end_submitted=!path_end_error;return path_end_error;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_release(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint32_t id,esp32_mquickjs_wifi_nan_ndp_status_t*out){
 assert(!lock_depth&&t->identity&&out);++path_releases;
 if(path_release_error)return path_release_error;
 esp32_mquickjs_wifi_nan_ndp_status_t*p=path_find(id);assert(p);
 if(!p->native_deleted||!p->frames_retired)return ESP_ERR_TIMEOUT;
 *out=*p;memset(p,0,sizeof(*p));return 0;
}
'''
BOUNDARY += SERVICE_BOUNDARY + MESSAGE_BOUNDARY + '\n#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE\n' + PATH_BOUNDARY + '\n#endif\n'

DISCOVERY_MAIN = r'''
static void tick(void){clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);}
int main(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t config={.op_channel=6};
 esp32_mquickjs_wifi_nan_session_t*s=NULL;assert(!esp32_mquickjs_wifi_nan_session_create(&config,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));tick();
 esp32_mquickjs_wifi_nan_service_config_t cfg={.publish={.service_name="test"}};
 esp32_mquickjs_wifi_nan_discovery_t*d=NULL,*next=NULL;
 fail_allocation=allocation_calls+1;
 assert(esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d)==ESP_ERR_NO_MEM&&!d&&!s_nan_discovery_handles);
 fail_allocation=0;assert(!esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d));
 esp32_mquickjs_event_queue_t q={.references=1};assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));
 tick();assert(d->status.ready&&d->status.native_id==7&&q.sent==1&&q.last.identity==d->status.identity);
 assert(q.last.ssi_len==3&&!memcmp(q.last.ssi,"abc",3));
 esp32_mquickjs_wifi_nan_discovery_close(d);service_cancel_pending=true;tick();
 assert(d->status.cancelled&&!d->status.retired&&service_cancels==1&&d->parent==s);
 assert(!esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&next));
 esp32_mquickjs_event_queue_t other={.references=1};
 assert(esp32_mquickjs_wifi_nan_discovery_activate(next,&other)==ESP_ERR_INVALID_STATE&&other.references==1);
 service_cancel_pending=false;tick();assert(d->status.retired&&!d->parent&&q.closed&&service_cancels==1);
 esp32_mquickjs_wifi_nan_discovery_queue_closed(d);assert(q.references==1);
 assert(!esp32_mquickjs_wifi_nan_discovery_activate(next,&other));tick();
 assert(next->status.ready&&next->status.native_id==7&&next->status.identity!=d->status.identity);
 esp32_mquickjs_wifi_nan_discovery_release(d);
 /* Parent runtime teardown retires child native state; a closed handle keeps
  * its own snapshot but no parent reference or Radio owner. */
 assert(!esp32_mquickjs_wifi_nan_prepare_runtime_destroy());run_queued(NULL);
 assert(next->status.retired&&!next->parent&&other.closed);
 esp32_mquickjs_wifi_nan_discovery_queue_closed(next);
 esp32_mquickjs_wifi_nan_session_release(s);s=NULL;
 assert(!s_nan_handles&&s_nan_discovery_handles==1&&!s_nan_active);
 assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_nan_discovery_release(next);next=NULL;
 assert(!allocations&&!s_nan_discovery_handles&&esp32_mquickjs_wifi_nan_open_runtime()==0);
}
'''

MESSAGE_MAIN = r'''
static void tick(void){clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);}
int main(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t cfg={.op_channel=6};
 esp32_mquickjs_wifi_nan_session_t*s=NULL;assert(!esp32_mquickjs_wifi_nan_session_create(&cfg,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));tick();
 esp32_mquickjs_wifi_nan_service_config_t config={.publish={.service_name="test"}};
 esp32_mquickjs_wifi_nan_discovery_t*d=NULL;assert(!esp32_mquickjs_wifi_nan_discovery_create(s,true,&config,1000,&d));
 esp32_mquickjs_event_queue_t q={.references=1};assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));tick();
 uint8_t bytes[3]={'a','b','c'};wifi_nan_followup_params_t params={.peer_inst_id=3,.peer_mac={2,3,4,5,6,7},.ssi_len=3,.ssi=bytes};
 esp32_mquickjs_wifi_nan_message_t*m=NULL,*next=NULL;
 assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 esp32_mquickjs_wifi_nan_message_cancel(m,false);
 assert(esp32_mquickjs_wifi_nan_message_activate(m)==ESP_ERR_INVALID_STATE&&!message_sends);
 esp32_mquickjs_wifi_nan_message_release(m);m=NULL;
 assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 bytes[0]='z';assert(m->params.ssi[0]=='a');message_inline=true;
 assert(!esp32_mquickjs_wifi_nan_message_activate(m));tick();
 assert(m->status.done&&!m->status.error&&!m->status.retired&&m->status.tx.tx_done);
 assert(!d->status.send_pending&&d->status.send_cleanup_pending&&message_sends==1);
 assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&next));
 assert(esp32_mquickjs_wifi_nan_message_activate(next)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_nan_message_release(next);next=NULL;
 message_tx.buffer_retired=true;tick();assert(m->status.retired&&!s->message&&!d->status.send_identity);
 esp32_mquickjs_wifi_nan_message_release(m);m=NULL;
 /* Completion time belongs to the native callback; a late cleanup worker
  * must not turn an already timely success into a timeout. */
 message_inline=false;assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 assert(!esp32_mquickjs_wifi_nan_message_activate(m));tick();
 message_tx.tx_done=message_tx.tx_succeeded=true;message_tx.completed_us=clock_us;
 clock_us=m->deadline_us+1;esp32_mquickjs_wifi_nan_message_status_t snapshot;
 esp32_mquickjs_wifi_nan_message_status(m,&snapshot);
 assert(snapshot.done&&!snapshot.error&&!snapshot.timed_out&&!snapshot.retired);
 message_tx.buffer_retired=true;tick();esp32_mquickjs_wifi_nan_message_release(m);m=NULL;
 message_inline=false;assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 assert(!esp32_mquickjs_wifi_nan_message_activate(m));tick();
 esp32_mquickjs_wifi_nan_message_cancel(m,true);assert(m->status.timed_out&&m->status.done&&!m->status.retired);
 esp32_mquickjs_wifi_nan_message_release(m);m=NULL;assert(s_nan_message_handles==1);
 message_tx.tx_done=message_tx.tx_succeeded=message_tx.buffer_retired=true;tick();
 assert(!s_nan_message_handles&&!s->message&&s->status.ready&&d->status.ready);
 assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 assert(!esp32_mquickjs_wifi_nan_message_activate(m));tick();
 esp32_mquickjs_wifi_nan_session_close(s,false);tick();
 assert(m->status.retired&&m->status.done&&m->status.cancelled&&m->status.tx.buffer_retired&&!s->message);
 assert(d->status.retired&&!d->parent&&q.closed);
 esp32_mquickjs_wifi_nan_discovery_queue_closed(d);
 esp32_mquickjs_wifi_nan_session_release(s);esp32_mquickjs_wifi_nan_discovery_release(d);
 esp32_mquickjs_wifi_nan_message_release(m);
 assert(!s_nan_message_handles&&!s_nan_discovery_handles&&!s_nan_handles&&!allocations);
}
'''

MAIN = r'''
int main(void) {
    wifi_nan_sync_config_t config={.op_channel=6};
    esp32_mquickjs_wifi_nan_session_t *s=NULL;
    esp32_mquickjs_wifi_nan_session_status_t status;
    assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_OK);
    config.op_channel=0;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_ERR_INVALID_ARG&&!allocation_calls);
    config.op_channel=6;config.group_mgmt_prot=true;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_ERR_NOT_SUPPORTED&&!allocation_calls);
    config.group_mgmt_prot=false;fail_allocation=1;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_ERR_NO_MEM&&!s&&!allocations&&!s_nan_handles);
    fail_allocation=0;
    /* Preserve the SDK channel field instead of inferring a 2.4 GHz-only
     * target. Radio/SDK admission, tested separately, decides RF support. */
    config.op_channel=149;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    esp32_mquickjs_wifi_nan_session_release(s);s=NULL;config.op_channel=6;

    /* Full worker queue: close before native dispatch, then drain without START. */
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_session_activate(s)==ESP_OK);
    queue_full=true;assert(!esp32_mquickjs_wifi_nan_service()&&!queued&&allocations==1);
    assert(esp32_mquickjs_wifi_nan_session_status(s,&status)&&!status.worker_busy&&status.cleanup_error==ESP_ERR_NO_MEM);
    esp32_mquickjs_wifi_nan_session_close(s,false);esp32_mquickjs_wifi_nan_session_release(s);s=NULL;
    queue_full=false;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
    assert(!begins&&!closes&&!allocations&&!s_nan_active&&!s_nan_workers);

    /* Real production worker is entered while another thread times out/closes
     * and drops the public root. A delayed Radio cleanup keeps native storage. */
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_session_activate(s)==ESP_OK);
    pause_begin=true;close_error=ESP_ERR_TIMEOUT;
    assert(esp32_mquickjs_wifi_nan_service());pthread_t worker;
    assert(!pthread_create(&worker,NULL,run_queued,NULL));
    pthread_mutex_lock(&gate);while(!begin_entered)pthread_cond_wait(&condition,&gate);pthread_mutex_unlock(&gate);
    esp32_mquickjs_wifi_nan_session_close(s,true);
    assert(esp32_mquickjs_wifi_nan_session_status(s,&status)&&status.timed_out&&status.closing&&!status.ready);
    esp32_mquickjs_wifi_nan_session_release(s);
    assert(allocations==1);
    pthread_mutex_lock(&gate);resume_begin=true;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&gate);
    assert(!pthread_join(worker,NULL));
    assert(allocations==1&&s_nan_active==s&&s->references==1&&s->operation.identity&&begins==1&&closes==1);
    esp32_mquickjs_wifi_nan_global_status_t global;
    esp32_mquickjs_wifi_nan_global_status(&global);
    assert(global.active&&global.handles==1&&!global.workers&&global.session.closing&&global.session.timed_out);
    assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_nan_prepare_runtime_destroy());
    assert(queued);close_error=ESP_OK;run_queued(NULL);s=NULL;
    assert(!allocations&&!s_nan_active&&!s_nan_workers&&begins==1&&closes==2);
    assert(esp32_mquickjs_wifi_nan_prepare_runtime_destroy());
    assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_OK);

    /* Ready Sessions keep the driver owner after startup; teardown owns close. */
    pause_begin=false;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_session_activate(s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
    assert(esp32_mquickjs_wifi_nan_session_status(s,&status)&&status.ready&&!status.retired);
    assert(!esp32_mquickjs_wifi_nan_prepare_runtime_destroy());run_queued(NULL);
    assert(esp32_mquickjs_wifi_nan_prepare_runtime_destroy());
    assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_ERR_INVALID_STATE); /* public handle remains */
    esp32_mquickjs_wifi_nan_session_release(s);s=NULL;
    assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_OK&&!allocations);

    /* No runtime poll occurs during this late START: the worker must still
     * enforce the original deadline before publishing readiness. */
    start_advance_us=2000000;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_session_activate(s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
    assert(esp32_mquickjs_wifi_nan_session_status(s,&status)&&status.timed_out&&status.retired&&!status.ready);
    esp32_mquickjs_wifi_nan_session_release(s);s=NULL;start_advance_us=0;

    /* Failed native admission cannot stop another owner's Radio. */
    unsigned closed=closes;begin_error=ESP_ERR_INVALID_STATE;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_session_activate(s)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
    assert(esp32_mquickjs_wifi_nan_session_status(s,&status)&&status.retired&&status.error==begin_error&&closes==closed);
    esp32_mquickjs_wifi_nan_session_release(s);s=NULL;
    s_nan_next_identity=UINT32_MAX;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_OK&&s->status.identity==UINT32_MAX);
    esp32_mquickjs_wifi_nan_session_release(s);s=NULL;
    assert(esp32_mquickjs_wifi_nan_session_create(&config,1000,&s)==ESP_ERR_NO_MEM&&!s&&!allocations);
}
'''
