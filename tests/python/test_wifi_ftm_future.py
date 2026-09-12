"""Deferred actual FTM Future start/poll/cancel/destroy over Session and Radio.

Clock/SDK/worker scheduling are injected. Full Future-core settlement, JS class
handoff and device RF are separate gates; no fixture is executed this wave.
"""
import re
import unittest
from test_wifi_ftm_session import session_code, MAIN as SESSION_MAIN
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiFtmFuture(unittest.TestCase):
    def test_receive_deadline_end_and_close_have_distinct_native_effects(self):
        source = (COMPONENT / 'src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_future.h').read_text()
        code = session_code('esp32c5/representative')
        code += SESSION_MAIN[:SESSION_MAIN.index('int main(void)')]
        for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
            code += re.search(r'typedef enum \{[^}]*\} '+name+';', header).group(0)
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;\n'
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        code += re.search(r'typedef enum \{[^}]*\} ftm_operation_t;', source).group(0)
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start)+3]
        code += ''.join(extract(source, name) for name in ('ftm_destroy', 'ftm_start', 'ftm_poll', 'ftm_cancel', 'ftm_timeout'))
        compile_run(self, code + MAIN)


MAIN = r'''
static esp32_mquickjs_future_driver_state_t *captured(esp32_mquickjs_wifi_ftm_session_t *session,ftm_operation_t operation) {
    esp32_mquickjs_future_driver_state_t *state=heap_caps_calloc(1,sizeof(*state),1);assert(state);
    assert(esp32_mquickjs_wifi_ftm_retain(session));state->session=session;state->operation=operation;state->timeout_ms=10;return state;
}
int main(void) {
    reset_ftm();wifi_ftm_initiator_cfg_t cfg={.resp_mac={2,3,4,5,6,7},.channel=6,.frm_count=32};
    esp32_mquickjs_wifi_ftm_session_t *session=NULL;
    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,4,&session));assert(!esp32_mquickjs_wifi_ftm_start(session));service();
    esp32_mquickjs_future_driver_state_t *receive=captured(session,FTM_RECEIVE);
    unsigned ends=ftm_end_calls;assert(ftm_timeout(receive)==0);assert(ftm_start(NULL,NULL,0,receive));
    now_us+=9999;assert(ftm_poll(receive)==ESP32_MQUICKJS_FUTURE_PENDING);
    ++now_us;assert(ftm_poll(receive)==ESP32_MQUICKJS_FUTURE_READY && receive->receive_timed_out);
    assert(!session->status.end_requested && !session->status.close_requested && ftm_end_calls==ends && owners()==1);
    ftm_destroy(receive);
    receive=captured(session,FTM_RECEIVE);assert(ftm_start(NULL,NULL,0,receive));
    assert(ftm_cancel(receive)==ESP32_MQUICKJS_CANCEL_REQUESTED && !session->status.end_requested);ftm_destroy(receive);
    esp32_mquickjs_future_driver_state_t *end=captured(session,FTM_END);
    assert(ftm_timeout(end)==10);assert(ftm_start(NULL,NULL,0,end));if(worker_fn)work();
    assert(session->status.end_requested && !session->status.close_requested && ftm_end_calls==ends+1);
    assert(ftm_cancel(end)==ESP32_MQUICKJS_CANCEL_REQUESTED);ftm_destroy(end);
    assert(!session->status.close_requested && session->entries);drain();
    assert(session->status.report_ready && !session->status.close_requested);
    end=captured(session,FTM_END);assert(ftm_start(NULL,NULL,0,end) && ftm_poll(end)==ESP32_MQUICKJS_FUTURE_READY);
    ftm_destroy(end);esp32_mquickjs_wifi_ftm_release(session);session=NULL;empty();

    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,4,&session));
    esp32_mquickjs_future_driver_state_t *open=heap_caps_calloc(1,sizeof(*open),1);assert(open);
    open->session=session;open->operation=FTM_OPEN;open->timeout_ms=10;session=NULL;
    assert(ftm_start(NULL,NULL,0,open) && worker_fn);
    assert(ftm_cancel(open)==ESP32_MQUICKJS_CANCEL_REQUESTED);ftm_destroy(open);work();empty();

    assert(!esp32_mquickjs_wifi_ftm_create(&cfg,4,&session));assert(!esp32_mquickjs_wifi_ftm_start(session));service();
    esp32_mquickjs_future_driver_state_t *close=captured(session,FTM_CLOSE);
    assert(ftm_start(NULL,NULL,0,close));if(worker_fn)work();
    assert(session->status.close_requested && !session->entries && owners()==1);
    assert(ftm_cancel(close)==ESP32_MQUICKJS_CANCEL_REQUESTED);ftm_destroy(close);
    esp32_mquickjs_wifi_ftm_release(session);session=NULL;
 /* Cancel/destruction already queued cleanup with its own native reference. */
 assert(worker_fn);work();drain();empty();return 0;
}
'''
