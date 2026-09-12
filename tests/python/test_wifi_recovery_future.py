"""Deferred real recovery Future polling over the production runtime stepper.

The clock and wait-scope are controllable; Radio/SDK boundaries come from the
runtime fixture. Future-core dispatch/timeout settlement remains a separate gate.
"""
import re
import unittest
from test_wifi_recovery_runtime import recovery_runtime_code
from test_wifi_configuration_cleanup import WIFI
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiRecoveryFuture(unittest.TestCase):
    def test_recovery_yields_to_original_owner_and_cancel_does_not_resume(self):
        source = (WIFI.parent.parent / 'wifi_common/esp32_mquickjs_wifi_recovery.c').read_text()
        header = (WIFI.parents[3] / 'internal/esp32_mquickjs_future.h').read_text()
        code = recovery_runtime_code(1)
        for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;\n'
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        code += BOUNDARIES
        code += ''.join(extract(source, name) for name in ('recovery_start', 'recovery_poll', 'recovery_cancel'))
        compile_run(self, code + MAIN)


BOUNDARIES = r'''
static int64_t clock_us;
static int wait_error;static unsigned wait_begins,wait_ends,last_wait;
static int64_t esp_timer_get_time(void) {return clock_us;}
static int esp32_mquickjs_wifi_wait_begin(uint32_t milliseconds) {
    ++wait_begins;last_wait=milliseconds;return wait_error;
}
static void esp32_mquickjs_wifi_wait_end(void) {++wait_ends;}
'''

MAIN = r'''
static void check_kind(esp32_mquickjs_wifi_recovery_kind_t kind) {
    original.kind=kind;clock_us=0;wait_begins=wait_ends=0;
    setup_restart();recovery_mode=WIFI_MODE_STA;recovery_owner_pending=true;
    esp32_mquickjs_future_driver_state_t state={.operation={9,51,kind},.timeout_ms=1000};
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && !begin_calls);
    assert(recovery_start(NULL,NULL,0,&state));
    clock_us=1234;
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && state.recovery.execution.admitted);
    assert(last_wait==999 && wait_begins==wait_ends);
    for(unsigned i=0;i<4;++i)assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && !phase_calls[2]);
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && !phase_calls[2]);
    recovery_owner_pending=false;
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && state.complete && !state.error);
    assert(phase_calls[2]==1 && wait_begins==wait_ends);
    esp32_mquickjs_wifi_recovery_dispose(&state.recovery);

    setup_restart();recovery_admitted=false;recovery_owner_pending=true;
    state=(esp32_mquickjs_future_driver_state_t){.operation={9,51,kind},.timeout_ms=1000};
    assert(recovery_cancel(&state)==ESP32_MQUICKJS_CANCELLED);
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && !begin_calls);
    state.cancelled=false;assert(recovery_start(NULL,NULL,0,&state));
    wait_error=77;assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && state.error==77 && !begin_calls);
    wait_error=state.error=0;
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && begin_calls==1);
    unsigned before=stop_calls;
    clock_us=state.deadline_us;
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING && stop_calls==before);
    assert(recovery_cancel(&state)==ESP32_MQUICKJS_CANCELLED);
    assert(recovery_poll(&state)==ESP32_MQUICKJS_FUTURE_READY && !phase_calls[1]);
    esp32_mquickjs_wifi_recovery_dispose(&state.recovery);
    assert(s_wifi_configuration_cleanup && !temporary);
    assert(wifi_finish_configuration_cleanup()==ESP_ERR_TIMEOUT);
    recovery_owner_pending=false;
    assert(wifi_finish_configuration_cleanup()==ESP_OK);
}
int main(void) {
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_ACTION);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX);
    check_kind(ESP32_MQUICKJS_WIFI_RECOVERY_FTM);
    return 0;
}
'''
