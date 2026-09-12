"""Deferred production SmartConfig Future callbacks + full native Session source.

Only clock, Radio and worker scheduling boundaries are injected. Execute during
concentrated Wi-Fi validation; AST parsing is not a passing runtime test.
"""
import re
import unittest
from test_wifi_smartconfig_session import COMPONENT, TYPES, BOUNDARIES, CASES, headers, unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class SmartConfigFuture(unittest.TestCase):
    def test_wait_cancellation_deadlines_and_native_retention(self):
        source = (COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig.c').read_text()
        future = (COMPONENT / 'internal/esp32_mquickjs_future.h').read_text()
        code = TYPES + headers() + BOUNDARIES
        code += unit(COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_session.c')
        code += CASES[:CASES.index('int main(void)')]
        for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', future).group(0)
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;\n'
        code += 'static int JS_ThrowInternalError(JSContext *ctx,const char *s){(void)ctx;(void)s;return -1;}\n'
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        code += re.search(r'typedef enum \{[^}]*\} sc_operation_t;', source).group(0)
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        for name in ('sc_destroy', 'sc_start', 'sc_poll', 'sc_cancel', 'sc_timeout'):
            code += extract(source, name)
        compile_run(self, code + MAIN)


MAIN = r'''
static esp32_mquickjs_future_driver_state_t *captured(esp32_mquickjs_wifi_smartconfig_session_t *session,sc_operation_t op){
    esp32_mquickjs_future_driver_state_t *state=heap_caps_calloc(1,sizeof(*state),1);assert(state);
    assert(esp32_mquickjs_wifi_smartconfig_session_retain(session));
    state->session=session;state->operation=op;state->timeout_ms=10;return state;
}
int main(void){
    reset();esp32_mquickjs_wifi_smartconfig_session_t *s=create();activate(s);tick();
    esp32_mquickjs_future_driver_state_t *wait=captured(s,SC_RECEIVE);
    assert(sc_timeout(wait)==0 && sc_start(NULL,NULL,0,wait));
    now+=9999;assert(sc_poll(wait)==ESP32_MQUICKJS_FUTURE_PENDING);
    now++;assert(sc_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && wait->wait_timed_out);
    assert(!s->status.closing && !s->status.timed_out);sc_destroy(wait);
    wait=captured(s,SC_RECEIVE);assert(sc_start(NULL,NULL,0,wait));
    assert(sc_cancel(wait)==ESP32_MQUICKJS_CANCELLED && !s->status.closing);sc_destroy(wait);
    wait=captured(s,SC_CLOSE);assert(sc_timeout(wait)==10);
    assert(sc_cancel(wait)==ESP32_MQUICKJS_CANCELLED);sc_destroy(wait);assert(!s->status.closing);
    capture_ready=true;tick();wait=captured(s,SC_RECEIVE);assert(sc_start(NULL,NULL,0,wait));
    assert(sc_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && !wait->wait_timed_out && s->status.credentials_ready);
    sc_destroy(wait);stop(s);

    reset();s=create();activate(s);tick();hold_close=true;
    wait=captured(s,SC_CLOSE);assert(sc_start(NULL,NULL,0,wait) && s->status.closing);run_worker();
    assert(sc_poll(wait)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(sc_cancel(wait)==ESP32_MQUICKJS_CANCELLED);sc_destroy(wait);
    esp32_mquickjs_wifi_smartconfig_session_release(s);assert(s_sc_active && s_sc_handles==1);
    hold_close=false;tick();assert(!s_sc_active && !s_sc_handles);

    reset();s=create();activate(s);tick();wait=captured(s,SC_RECEIVE);assert(sc_start(NULL,NULL,0,wait));
    esp32_mquickjs_wifi_smartconfig_session_release(s); /* Public finalizer; Future still owns a ref. */
    assert(!s->status.closing);sc_destroy(wait);assert(s->status.closing);tick();assert(!s_sc_handles);

    reset();s=create();activate(s);tick();wait=captured(s,SC_RECEIVE);assert(sc_start(NULL,NULL,0,wait));
    s->deadline_us=now;assert(sc_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && s->status.timed_out && !wait->wait_timed_out);
    sc_destroy(wait);run_worker();assert(s->status.retired);esp32_mquickjs_wifi_smartconfig_session_release(s);
    assert(!s_sc_handles && !s_sc_workers && allocs==frees);return 0;
}
'''
