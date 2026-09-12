"""Deferred full production WPS Session scheduler, result and teardown cases.

AST only during implementation. Radio/Station/clock/worker queue are injected
boundaries; the production Session controls the ordering and native storage.
"""
import os
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / 'components/esp32_mquickjs'


def unit(path):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', path.read_text(), flags=re.M)


def headers():
    sdk = Path(os.environ['IDF_PATH'])
    return unit(sdk / 'components/wpa_supplicant/esp_supplicant/include/esp_wps.h') + ''.join(
        unit(COMPONENT / 'internal' / ('esp32_mquickjs_wifi_wps_' + name + '.h'))
        for name in ('sdk', 'worker', 'radio', 'station', 'session'))


class WPSSession(unittest.TestCase):
    def test_production_session_worker_and_helper_retirement(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        code = TYPES + headers()
        code += BOUNDARIES
        code += unit(COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_session.c')
        compile_run(self, code + CASES)


TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NOT_FINISHED 5
#define MALLOC_CAP_8BIT 1
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int esp_err_t,portMUX_TYPE,wifi_second_chan_t;
typedef struct {uint32_t generation,identity;unsigned client;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
typedef struct {uint32_t generation,lease_identity,identity;unsigned kind;} esp32_mquickjs_wifi_radio_operation_t;
'''

BOUNDARIES = r'''
static unsigned locks,allocs,frees,starts,finishes,prepares,closes,pin_copies,pin_commits,copies,commits;
static unsigned helper_identity,next_helper=1,helper_drains,helper_releases;
static bool alloc_fail,queue_fail,partial_start,capture_ready,hold_close,hold_helper,radio_active,helper_ready;
static int start_error,poll_error,finish_error,reserve_error;
static int64_t now=100;
static size_t allocation_size;
static void (*scheduled)(void *),(*during_transfer)(void);
static void *scheduled_arg;
static esp32_mquickjs_wifi_wps_radio_status_t radio_state;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!locks);locks=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(locks);locks=0;}while(0)
static void *heap_caps_calloc(size_t n,size_t size,int caps) {
    assert(!locks && caps==1);if(alloc_fail)return NULL;allocs++;allocation_size=n*size;return calloc(n,size);
}
static void heap_caps_free(void *p) {
    assert(!locks && p);for(size_t i=0;i<allocation_size;i++)assert(!((uint8_t*)p)[i]);frees++;free(p);
}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n) {volatile uint8_t *v=p;while(n--)*v++=0;}
static int64_t esp_timer_get_time(void) {return now;}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg) {
    assert(!locks && !scheduled);if(queue_fail)return false;scheduled=fn;scheduled_arg=arg;return true;
}
bool esp32_mquickjs_wifi_wps_config_valid(const esp_wps_config_t *c) {return c && c->wps_type==WPS_TYPE_PIN;}
esp_err_t esp32_mquickjs_wifi_wps_station_reserve(uint32_t *identity,esp32_mquickjs_wifi_radio_lease_t owners[3]) {
    assert(!locks && !*identity && !helper_identity);
    if(reserve_error)return reserve_error;*identity=helper_identity=next_helper++;
    for(unsigned i=0;i<3;i++)owners[i]=(esp32_mquickjs_wifi_radio_lease_t){.identity=i+1,.generation=5,.client=i,.acquired=true};
    return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_station_drain(uint32_t identity) {
    assert(!locks && identity==helper_identity && identity);helper_drains++;
    if(radio_active)assert(radio_state.worker.capture_retired || radio_state.worker.retired);
    if(hold_helper)return ESP_ERR_NOT_FINISHED;helper_ready=true;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_station_release(uint32_t *identity) {
    assert(!locks && !radio_active);int error=esp32_mquickjs_wifi_wps_station_drain(*identity);
    if(error)return error;helper_releases++;helper_identity=*identity=0;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_begin(const esp32_mquickjs_wifi_radio_lease_t owners[3],bool allow,
    const esp_wps_config_t *config,esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(!locks && helper_identity && !token->identity && owners[1].identity==2 && config->wps_type==WPS_TYPE_PIN);
    (void)allow;starts++;if(!start_error || partial_start){*token=(esp32_mquickjs_wifi_radio_operation_t){.identity=21,.generation=5};radio_active=true;}
    radio_state=(esp32_mquickjs_wifi_wps_radio_status_t){.operation=*token};
    radio_state.worker.native.pin_available=true;*status=radio_state;return start_error;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(!locks && token->identity==21 && radio_active);radio_state.worker.native.terminal_seen=capture_ready;
    *status=radio_state;status->error=poll_error;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_pin(const esp32_mquickjs_wifi_radio_operation_t *token,uint8_t pin[8],bool commit) {
    assert(!locks && token->identity==21 && radio_active);
    if(commit){pin_commits++;radio_state.worker.native.pin_available=false;}else{pin_copies++;memcpy(pin,"12345670",8);}return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(!locks && token->identity==21 && capture_ready);finishes++;
    if(finish_error)return finish_error;radio_state.worker.capture_retired=true;*status=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_credentials(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_credentials_t *out,bool commit) {
    assert(!locks && token->identity==21 && radio_state.worker.capture_retired && helper_ready);
    if(commit)commits++;else {copies++;memset(out,0,sizeof(*out));out->count=1;memcpy(out->entries[0].ssid,"network",7);out->entries[0].ssid_length=7;}
    if(during_transfer){void (*fn)(void)=during_transfer;during_transfer=NULL;fn();}return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_prepare_close(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(!locks && token->identity==21 && radio_active);prepares++;
    if(hold_close)return ESP_ERR_NOT_FINISHED;radio_state.worker.retired=true;helper_ready=false;*status=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(!locks && token->identity==21 && radio_state.worker.retired && helper_ready && helper_identity);
    closes++;*status=radio_state;token->identity=0;radio_active=false;return 0;
}
'''

CASES = r'''
static void run_worker(void) {
    assert(scheduled);void (*fn)(void *)=scheduled;void *arg=scheduled_arg;scheduled=NULL;scheduled_arg=NULL;fn(arg);
}
static void tick(void) {now+=200000;(void)esp32_mquickjs_wifi_wps_service();if(scheduled)run_worker();}
static void reset(void) {
    assert(!s_wps_active && !s_wps_workers && !s_wps_handles && !helper_identity && !scheduled && !radio_active);
    alloc_fail=queue_fail=partial_start=capture_ready=hold_close=hold_helper=false;
    start_error=poll_error=finish_error=reserve_error=0;helper_ready=false;during_transfer=NULL;
    starts=finishes=prepares=closes=pin_copies=pin_commits=copies=commits=helper_drains=helper_releases=0;
    assert(!esp32_mquickjs_wifi_wps_open_runtime());
}
static esp32_mquickjs_wifi_wps_session_t *create(void) {
    esp_wps_config_t config={.wps_type=WPS_TYPE_PIN};esp32_mquickjs_wifi_wps_session_t *s=NULL;
    assert(!esp32_mquickjs_wifi_wps_session_create(&config,false,1000,&s));return s;
}
static void finish_close(esp32_mquickjs_wifi_wps_session_t *s) {
    esp32_mquickjs_wifi_wps_session_close(s,false);
    for(unsigned i=0;i<8 && !s->status.retired;i++)tick();
    assert(s->status.retired && !helper_identity && !radio_active && !s_wps_active);
    esp32_mquickjs_wifi_wps_session_release(s);assert(allocs==frees);
}
static void close_during_transfer(void) {esp32_mquickjs_wifi_wps_session_close(s_wps_active,false);}
int main(void) {
    reset();esp32_mquickjs_wifi_wps_session_t *s=create();reserve_error=88;
    assert(esp32_mquickjs_wifi_wps_session_activate(s)==88 && s->status.retired && !helper_identity);
    esp32_mquickjs_wifi_wps_session_release(s);reset();
    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s) && helper_identity);
    assert(esp32_mquickjs_wifi_wps_service() && scheduled);run_worker();
    uint8_t pin[8];assert(!esp32_mquickjs_wifi_wps_session_pin(s,pin,false) && !memcmp(pin,"12345670",8));
    assert(!esp32_mquickjs_wifi_wps_session_pin(s,pin,false) && pin_copies==1 && pin_commits==1);
    assert(!esp32_mquickjs_wifi_wps_session_pin(s,NULL,true));
    assert(esp32_mquickjs_wifi_wps_session_pin(s,pin,false)==ESP_ERR_NOT_FINISHED);
    capture_ready=true;tick();assert(s->status.capture_finished && !s->status.credentials_ready);
    hold_helper=true;tick();assert(!scheduled && !copies && radio_active);
    esp32_mquickjs_wifi_wps_credentials_t credentials;
    assert(esp32_mquickjs_wifi_wps_session_credentials(s,&credentials,false)==ESP_ERR_NOT_FINISHED);
    hold_helper=false;tick();assert(s->status.credentials_ready && copies==1 && commits==1);
    assert(!esp32_mquickjs_wifi_wps_session_credentials(s,&credentials,false) && credentials.count==1);
    assert(!esp32_mquickjs_wifi_wps_session_credentials(s,&credentials,false) && copies==1);
    esp32_mquickjs_wifi_wps_session_status_t observed;
    assert(esp32_mquickjs_wifi_wps_session_wait_begin(s,false));
    assert(!esp32_mquickjs_wifi_wps_session_observation(s,&observed));
    assert(!esp32_mquickjs_wifi_wps_session_credentials(s,NULL,true));
    esp32_mquickjs_wifi_wps_session_wait_end(s,false);
    assert(esp32_mquickjs_wifi_wps_session_observation(s,&observed));
    assert(esp32_mquickjs_wifi_wps_open_runtime()==ESP_ERR_INVALID_STATE);
    finish_close(s);reset();

    /* Closing a queued, not-yet-dispatched Session never starts WPS. */
    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s));
    assert(esp32_mquickjs_wifi_wps_service() && scheduled);
    esp32_mquickjs_wifi_wps_session_close(s,false);run_worker();
    finish_close(s);assert(!starts);reset();

    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s));queue_fail=true;
    assert(!esp32_mquickjs_wifi_wps_service() && !scheduled && helper_identity);
    assert(s->status.cleanup_error==ESP_ERR_NO_MEM);
    now+=2000000;(void)esp32_mquickjs_wifi_wps_service();
    assert(s->status.timed_out && s->status.error==ESP_ERR_TIMEOUT && s->status.retired && !starts);
    esp32_mquickjs_wifi_wps_session_release(s);reset();

    /* Native close and the helper fence have independent retained suffixes. */
    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s));tick();hold_close=true;
    assert(!esp32_mquickjs_wifi_wps_prepare_runtime_destroy());if(scheduled)run_worker();
    assert(helper_identity && radio_active && !helper_releases);
    hold_close=false;tick();hold_helper=true;tick();assert(!closes && radio_active);
    hold_helper=false;finish_close(s);assert(esp32_mquickjs_wifi_wps_prepare_runtime_destroy());reset();

    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s));tick();capture_ready=true;tick();
    during_transfer=close_during_transfer;tick();
    assert(s->status.closing && !s->status.credentials_ready);
    for(size_t i=0;i<sizeof(s->credentials);i++)assert(!((uint8_t*)&s->credentials)[i]);
    finish_close(s);reset();

    /* GC/final-owner release while a worker holds storage requests close after
     * that worker returns; only final Radio and helper retirement free it. */
    s=create();assert(!esp32_mquickjs_wifi_wps_session_activate(s));
    assert(esp32_mquickjs_wifi_wps_service() && scheduled);esp32_mquickjs_wifi_wps_session_release(s);run_worker();
    for(unsigned i=0;i<8 && s_wps_active;i++)tick();
    assert(!s_wps_active && !helper_identity && allocs==frees);reset();
    return 0;
}
'''
