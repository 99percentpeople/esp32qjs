"""Deferred full production AP WPS Session scheduler, result and teardown cases.

AST only during implementation. Radio/AP helper/clock/worker queue are injected
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
        unit(COMPONENT / 'internal' / ('esp32_mquickjs_wifi_wps_ap_' + name + '.h'))
        for name in ('result', 'sdk', 'worker', 'radio', 'session'))


class WPSAPSession(unittest.TestCase):
    def test_production_session_worker_and_helper_retirement(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        code = TYPES + headers()
        code += BOUNDARIES
        code += unit(COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_session.c')
        compile_run(self, code + CASES)


TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR 1
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

CASES = r'''
static void run_worker(void){
 assert(scheduled);void (*fn)(void*)=scheduled;void *arg=scheduled_arg;
 scheduled=NULL;scheduled_arg=NULL;fn(arg);
}
static void tick(void){now+=100000;(void)esp32_mquickjs_wifi_wps_ap_service();if(scheduled)run_worker();}
static esp32_mquickjs_wifi_wps_ap_session_t *create(void){
 esp_wps_config_t config={.wps_type=WPS_TYPE_PIN};esp32_mquickjs_wifi_wps_ap_session_t *s=NULL;
 assert(!esp32_mquickjs_wifi_wps_ap_session_create(&config,5000,&s));return s;
}
static void finish(esp32_mquickjs_wifi_wps_ap_session_t *s){
 esp32_mquickjs_wifi_wps_ap_session_close(s,false);
 for(int i=0;i<8 && !s->status.retired;i++)tick();
 assert(s->status.retired && !helper_identity && !radio_active && !s_wps_active);
 esp32_mquickjs_wifi_wps_ap_session_release(s);assert(allocs==frees);
}
int main(void){
 assert(!esp32_mquickjs_wifi_wps_ap_open_runtime());
 esp32_mquickjs_wifi_wps_ap_session_t *s=create();reserve_error=88;
 assert(esp32_mquickjs_wifi_wps_ap_session_activate(s)==88 && s->status.retired && !helper_identity);
 esp32_mquickjs_wifi_wps_ap_session_release(s);reserve_error=0;
 s=create();assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s) && helper_identity);
 tick();uint8_t pin[8],peer[6];
 assert(!esp32_mquickjs_wifi_wps_ap_session_pin(s,pin,false) && !memcmp(pin,"12345670",8));
 assert(!esp32_mquickjs_wifi_wps_ap_session_pin(s,NULL,true));
 capture_ready=true;finish_error=ESP_ERR_NOT_FINISHED;tick();
 assert(!s->status.result_ready && helper_identity && radio_active);
 finish_error=0;tick();assert(s->status.capture_finished && s->status.result_ready && !helper_drains);
 assert(!esp32_mquickjs_wifi_wps_ap_session_registered(s,peer,false) && !memcmp(peer,"ABCDEF",6));
 assert(!esp32_mquickjs_wifi_wps_ap_session_registered(s,peer,false));
 esp32_mquickjs_wifi_wps_ap_session_status_t observed;
 assert(esp32_mquickjs_wifi_wps_ap_session_wait_begin(s,false));
 assert(!esp32_mquickjs_wifi_wps_ap_session_observation(s,&observed));
 assert(!esp32_mquickjs_wifi_wps_ap_session_registered(s,NULL,true));
 esp32_mquickjs_wifi_wps_ap_session_wait_end(s,false);
 assert(s->status.result_consumed && !s->status.result_ready);
 hold_close=true;esp32_mquickjs_wifi_wps_ap_session_close(s,false);tick();
 assert(helper_identity && radio_active && !helper_releases);
 hold_close=false;tick();hold_helper=true;tick();
 assert(!radio_active && helper_identity && !s->status.retired);
 hold_helper=false;finish(s);capture_ready=false;

 /* Cancel before dispatch never enables SDK; queue pressure does not lose
  * the registry's native cleanup ownership or prevent a Session deadline. */
 unsigned old_starts=starts;s=create();assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s));
 assert(esp32_mquickjs_wifi_wps_ap_service() && scheduled);
 esp32_mquickjs_wifi_wps_ap_session_close(s,false);run_worker();finish(s);assert(starts==old_starts);
 s=create();assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s));queue_fail=true;
 assert(!esp32_mquickjs_wifi_wps_ap_service() && s->status.cleanup_error==ESP_ERR_NO_MEM);
 now+=6000000;(void)esp32_mquickjs_wifi_wps_ap_service();
 assert(s->status.retired && s->status.timed_out && !helper_identity && starts==old_starts);
 esp32_mquickjs_wifi_wps_ap_session_release(s);queue_fail=false;

 /* A rejected native admission with a token still owes Radio cleanup. */
 s=create();assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s));start_error=71;partial_start=true;hold_close=true;
 tick();assert(s->status.error==71 && s->status.closing && radio_active && helper_identity);
 start_error=0;partial_start=false;hold_close=false;finish(s);
 s=create();assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s));tick();hold_close=true;
 assert(!esp32_mquickjs_wifi_wps_ap_prepare_runtime_destroy());if(scheduled)run_worker();
 assert(radio_active && helper_identity);hold_close=false;finish(s);
 assert(esp32_mquickjs_wifi_wps_ap_prepare_runtime_destroy());
 assert(allocs==frees && !helper_drains);return 0;
}
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
static esp32_mquickjs_wifi_wps_ap_radio_status_t radio_state;
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
esp_err_t esp32_mquickjs_wifi_wps_ap_helper_reserve(uint32_t *identity,esp32_mquickjs_wifi_radio_lease_t owners[3]) {
    assert(!locks && !*identity && !helper_identity);
    if(reserve_error)return reserve_error;*identity=helper_identity=next_helper++;
    for(unsigned i=0;i<3;i++)owners[i]=(esp32_mquickjs_wifi_radio_lease_t){.identity=i+1,.generation=5,.client=i,.acquired=true};
    return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_ap_helper_release(uint32_t *identity) {
    assert(!locks && !radio_active && *identity==helper_identity);
    if(hold_helper)return ESP_ERR_NOT_FINISHED;
    helper_releases++;helper_identity=*identity=0;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_begin(const esp32_mquickjs_wifi_radio_lease_t owners[3],
    const esp_wps_config_t *config,esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_wps_ap_radio_status_t *status) {
    assert(!locks && helper_identity && !token->identity && owners[1].identity==2 && config->wps_type==WPS_TYPE_PIN);
    starts++;if(!start_error || partial_start){*token=(esp32_mquickjs_wifi_radio_operation_t){.identity=21,.generation=5};radio_active=true;}
    radio_state=(esp32_mquickjs_wifi_wps_ap_radio_status_t){.operation=*token};
    radio_state.worker.native.pin_available=true;*status=radio_state;return start_error;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status) {
    assert(!locks && token->identity==21 && radio_active);radio_state.worker.native.terminal=capture_ready;
    *status=radio_state;status->error=poll_error;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_pin(const esp32_mquickjs_wifi_radio_operation_t *token,uint8_t pin[8],bool commit) {
    assert(!locks && token->identity==21 && radio_active);
    if(commit){pin_commits++;radio_state.worker.native.pin_available=false;}else{pin_copies++;memcpy(pin,"12345670",8);}return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status) {
    assert(!locks && token->identity==21 && capture_ready);finishes++;
    if(finish_error)return finish_error;radio_state.worker.capture_retired=true;memcpy(radio_state.worker.native.peer,"ABCDEF",6);*status=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status) {
    assert(!locks && token->identity==21 && helper_identity && radio_active);
    closes++;if(hold_close)return ESP_ERR_NOT_FINISHED;
    radio_state.worker.retired=true;*status=radio_state;token->identity=0;radio_active=false;return 0;
}
'''
