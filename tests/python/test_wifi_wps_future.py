"""Deferred production Wps Future callbacks + full native Session source.

Only clock, Radio and worker scheduling boundaries are injected. Execute during
concentrated Wi-Fi validation; AST parsing is not a passing runtime test.
"""
import re
import unittest
from test_wifi_wps_session import COMPONENT, TYPES, BOUNDARIES, CASES, headers, unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WpsFuture(unittest.TestCase):
    def test_wait_cancellation_deadlines_and_native_retention(self):
        source = (COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps.c').read_text()
        future = (COMPONENT / 'internal/esp32_mquickjs_future.h').read_text()
        boundaries = BOUNDARIES.replace('for(size_t i=0;i<allocation_size;i++)assert(!((uint8_t*)p)[i]);', '')
        code = TYPES + headers() + boundaries
        code += unit(COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_session.c')
        code += CASES[:CASES.index('int main(void)')]
        for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', future).group(0)
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;\n'
        code += 'static int JS_ThrowInternalError(JSContext *ctx,const char *s){(void)ctx;(void)s;return -1;}\n'
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        code += re.search(r'typedef enum \{[^}]*\} wps_operation_t;', source).group(0)
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        for name in ('wps_destroy', 'wps_start', 'wps_poll', 'wps_cancel', 'wps_timeout'):
            code += extract(source, name)
        compile_run(self, code + MAIN)


MAIN = r'''
static esp32_mquickjs_future_driver_state_t *captured(esp32_mquickjs_wifi_wps_session_t *session,wps_operation_t op){
    esp32_mquickjs_future_driver_state_t *state=heap_caps_calloc(1,sizeof(*state),1);assert(state);
    assert(esp32_mquickjs_wifi_wps_session_retain(session));
    assert(esp32_mquickjs_wifi_wps_session_wait_begin(session,op==WPS_CLOSE));
    state->observation_registered=true;state->session=session;state->operation=op;state->timeout_ms=10;return state;
}
static void activate(esp32_mquickjs_wifi_wps_session_t *s){assert(!esp32_mquickjs_wifi_wps_session_activate(s));tick();}
static void consume_pin(esp32_mquickjs_wifi_wps_session_t *s){assert(!esp32_mquickjs_wifi_wps_session_pin(s,NULL,true));}
int main(void){
    wps_destroy(NULL);
    reset();esp32_mquickjs_wifi_wps_session_t *s=create();activate(s);consume_pin(s);
    esp32_mquickjs_future_driver_state_t *wait=captured(s,WPS_RECEIVE);
    assert(wps_timeout(wait)==0 && wps_start(NULL,NULL,0,wait));
    now+=9999;assert(wps_poll(wait)==ESP32_MQUICKJS_FUTURE_PENDING);
    now++;assert(wps_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && wait->wait_timed_out);
    assert(!s->status.closing && !s->status.timed_out);wps_destroy(wait);
    wait=captured(s,WPS_RECEIVE);assert(wps_start(NULL,NULL,0,wait));
    assert(wps_cancel(wait)==ESP32_MQUICKJS_CANCELLED && !s->status.closing);wps_destroy(wait);
    wait=captured(s,WPS_CLOSE);assert(wps_timeout(wait)==10);
    assert(wps_cancel(wait)==ESP32_MQUICKJS_CANCELLED);wps_destroy(wait);assert(!s->status.closing);
    capture_ready=true;tick();tick();wait=captured(s,WPS_RECEIVE);assert(wps_start(NULL,NULL,0,wait));
    assert(wps_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && !wait->wait_timed_out && s->status.credentials_ready);
    esp32_mquickjs_wifi_wps_session_status_t observation;
    assert(!esp32_mquickjs_wifi_wps_session_observation(s,&observation));wps_destroy(wait);
    assert(esp32_mquickjs_wifi_wps_session_observation(s,&observation));finish_close(s);

    reset();s=create();activate(s);hold_close=true;
    wait=captured(s,WPS_CLOSE);assert(wps_start(NULL,NULL,0,wait) && s->status.closing);run_worker();
    assert(wps_poll(wait)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(wps_cancel(wait)==ESP32_MQUICKJS_CANCELLED);wps_destroy(wait);
    esp32_mquickjs_wifi_wps_session_release(s);assert(s_wps_active && s_wps_handles==1);
    hold_close=false;for(unsigned i=0;i<8 && s_wps_active;i++)tick();assert(!s_wps_active && !s_wps_handles);

    reset();s=create();activate(s);consume_pin(s);wait=captured(s,WPS_RECEIVE);assert(wps_start(NULL,NULL,0,wait));
    esp32_mquickjs_wifi_wps_session_release(s); /* GC releases public handle, not Future/native storage. */
    assert(!s->status.closing);wps_destroy(wait);assert(s->status.closing);
    for(unsigned i=0;i<8 && s_wps_active;i++)tick();assert(!s_wps_handles);

    reset();s=create();activate(s);consume_pin(s);wait=captured(s,WPS_RECEIVE);assert(wps_start(NULL,NULL,0,wait));
    s->deadline_us=now;assert(wps_poll(wait)==ESP32_MQUICKJS_FUTURE_READY && s->status.timed_out && !wait->wait_timed_out);
    wps_destroy(wait);if(scheduled)run_worker();finish_close(s);
    assert(!s_wps_handles && !s_wps_workers && allocs==frees);return 0;
}
'''
