"""Deferred real closeAll Future helpers over the production Radio registry.

Only SDK outcomes/retirement and background scheduling are injected. No claim
of full Future core/VM/GC/RF coverage; import/compile/run at Wi-Fi stage only.
"""
import re
import unittest
from test_wifi_twt_agreement_radio import agreement_radio_code, MAIN as RADIO_MAIN
from test_wifi_twt_radio import INTERNAL, SOURCE
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiTwtCloseAll(unittest.TestCase):
    def test_snapshot_successors_cancel_and_retained_failure(self):
        source = (SOURCE.parent.parent / 'wifi_twt/esp32_mquickjs_wifi_twt_close.c').read_text()
        code = agreement_radio_code() + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;\n'
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        header = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
        for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
        begin = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[begin:source.index('};', begin) + 2]
        code += '\nbool esp32_mquickjs_wifi_twt_agreement_service(void){return false;}\n'
        for name in ('close_start', 'close_poll', 'close_cancel', 'close_destroy', 'close_timeout_ms'):
            code += extract(source, name)
        compile_run(self, code + MAIN)


MAIN = r'''
int main(void) {
    individual_reset();
    esp32_mquickjs_future_driver_state_t state={.timeout_ms=6000};
    assert(close_start(NULL,NULL,0,&state));
    assert(close_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && !individual_submits && !broadcast_submits);
    esp32_mquickjs_wifi_itwt_options_t individual={.config={.setup_cmd=TWT_REQUEST,.min_wake_dura=1,
        .wake_invl_mant=10256,.timeout_time_ms=100},.timeout_ms=6000};
    esp32_mquickjs_wifi_btwt_options_t broadcast={.config={.setup_cmd=TWT_REQUEST,.btwt_id=1,
        .timeout_time_ms=5000},.timeout_ms=6000};
    esp32_mquickjs_wifi_twt_token_t a={0},b={0},later={0};
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&individual,&a));
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_submit(&broadcast,&b));
    state=(esp32_mquickjs_future_driver_state_t){.timeout_ms=6000};
    assert(close_start(NULL,NULL,0,&state));
    assert(state.group.individual_count==1 && state.group.broadcast_count==1);
    assert(s_twt_individual[0].closing && s_twt_broadcast[1]->closing);
    assert(individual_results[0].flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_CLOSE_REQUESTED);
    assert(close_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING);
    retirement_error=78;assert(esp32_mquickjs_wifi_radio_twt_individual_close(&a)==78);
    assert(close_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && individual_owners()==2);
    retirement_error=0;assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&a));
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&individual,&later));
    assert(!s_twt_individual[0].closing); /* Same slot, distinct boot identity. */
    assert(esp32_mquickjs_wifi_radio_twt_close_pending(&state.group)==1);
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&b));
    assert(close_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && individual_owners()==1);
    assert(close_start(NULL,NULL,0,&state)); /* Repeated start must not capture successor. */
    assert(!s_twt_individual[0].closing);
    state=(esp32_mquickjs_future_driver_state_t){0};
    assert(close_cancel(&state)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(close_start(NULL,NULL,0,&state) && !state.started && !s_twt_individual[0].closing);
    state=(esp32_mquickjs_future_driver_state_t){0};
    assert(close_start(NULL,NULL,0,&state) && s_twt_individual[0].closing);
    assert(close_cancel(&state)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(close_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && individual_owners()==1);
    assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&later));
    assert(!individual_owners() && !critical && !locks);
    free(s_twt_individual);free(s_twt_broadcast);return 0;
}
'''
