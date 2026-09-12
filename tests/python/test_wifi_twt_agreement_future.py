"""Deferred real Agreement worker/poll/cancel/cleanup over production Radio.

Native SDK boundaries and worker scheduling are injected. These cases do not
claim full Future core, JS object handoff, movable GC or real-device evidence.
"""
import re
import unittest
from test_wifi_twt_agreement_radio import agreement_radio_code, MAIN as RADIO_MAIN
from test_wifi_twt_radio import INTERNAL, SOURCE as RADIO_SOURCE
from test_wifi_config_controls import structure
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def agreement_future_code():
    source = (RADIO_SOURCE.parent.parent / 'wifi_twt/esp32_mquickjs_wifi_twt_agreement.c').read_text()
    header = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
    code = agreement_radio_code() + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
    code += 'typedef int JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;\n'
    code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
    for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += source[source.index('typedef struct {'):source.index('} twt_agreement_handle_t;') + len('} twt_agreement_handle_t;')]
    start = source.index('struct esp32_mquickjs_future_driver_state {')
    code += source[start:source.index('#define SET', start)]
    code += BOUNDARIES
    for name in ('agreement_request_close', 'agreement_present', 'broadcast_setup_accepted', 'agreement_cleanup_worker', 'esp32_mquickjs_wifi_twt_agreement_service',
                 'esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy', 'agreement_submit_worker',
                 'agreement_schedule', 'agreement_start', 'agreement_poll', 'agreement_cancel', 'agreement_destroy', 'agreement_timeout'):
        code += extract(source, name)
    return code


class WiFiTwtAgreementFuture(unittest.TestCase):
    def test_cancelled_submit_and_multi_owner_runtime_cleanup(self):
        compile_run(self, agreement_future_code() + MAIN)


BOUNDARIES = r'''
static int64_t now_us;
static void (*worker)(void *);
static void *worker_arg;
static int64_t esp_timer_get_time(void) {return now_us;}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg) {
    if(worker)return false;worker=fn;worker_arg=arg;return true;
}
static void work(void) {
    assert(worker);void (*fn)(void *)=worker;void *arg=worker_arg;worker=NULL;worker_arg=NULL;fn(arg);
}
'''
MAIN = r'''
static esp32_mquickjs_future_driver_state_t *capture(void) {
    esp32_mquickjs_future_driver_state_t *state=calloc(1,sizeof(*state));assert(state);
    state->handle=calloc(1,sizeof(*state->handle));assert(state->handle);
    state->options=(esp32_mquickjs_wifi_itwt_options_t){.config={.setup_cmd=TWT_REQUEST,.min_wake_dura=1,
        .wake_invl_mant=10256,.timeout_time_ms=100},.timeout_ms=6000};
    atomic_init(&state->worker_done,false);atomic_init(&state->cancel_requested,false);return state;
}
int main(void) {
    individual_reset();
    esp32_mquickjs_future_driver_state_t *state=capture();
    assert(agreement_start(NULL,NULL,0,state) && worker);
    assert(agreement_cancel(state)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);work();
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && !individual_submits);
    agreement_destroy(state);assert(!individual_owners());
    state=capture();assert(agreement_start(NULL,NULL,0,state));work();
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING && individual_owners()==1);
    individual_results[0].flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN;
    individual_results[0].event=(wifi_event_sta_itwt_setup_t){.config=state->options.config,.status=1};
    individual_results[0].event.config.setup_cmd=TWT_ACCEPT;
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && !state->error);
    state->result.native.flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED;
    individual_results[0].flags|=ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED;
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && state->error==ESP_ERR_INVALID_STATE);
    /* Public conversion failure/cancel leaves ownership in the Radio registry. */
    retirement_error=ESP_ERR_NOT_FINISHED;agreement_destroy(state);assert(worker && individual_owners()==1);work();
    assert(individual_owners()==1 && !worker);
    assert(!esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy());
    now_us+=100000;retirement_error=0;
    assert(esp32_mquickjs_wifi_twt_agreement_service() && worker);work();
    assert(!individual_owners() && esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy());
    esp32_mquickjs_wifi_twt_token_t a={0},b={0};state=capture();
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&state->options,&a));
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&state->options,&b));
    heap_caps_free(state->handle);state->handle=NULL;state->close=true;state->token=a;
    assert(agreement_start(NULL,NULL,0,state));assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(!esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy());
    now_us+=100000;(void)esp32_mquickjs_wifi_twt_agreement_service();assert(worker);work();
    assert(!individual_owners() && agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY);
    agreement_destroy(state);assert(!worker && !locks && !critical);free(s_twt_individual);return 0;
}
'''
