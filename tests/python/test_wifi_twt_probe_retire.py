"""Deferred production probe retirement executor with real result and marker.

Only native SDK state/calls, timers, and event delivery are controlled. The
native quiescence predicate has its separate production SDK fixture. Do not
import, compile, or execute until the Wi-Fi API stage; AST is not runtime proof.
"""
import unittest
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wifi_config_controls import structure
from test_wifi_twt_probe_result import PRELUDE
from test_wifi_twt_fence import BOUNDARIES as TIMER_BOUNDARIES
from test_wireless_control_regression import compile_run


class WiFiTwtProbeRetire(unittest.TestCase):
    def test_pins_marker_event_ordering_failed_suffix_and_changed_cut(self):
        code = PRELUDE + '\n#include <stdatomic.h>\n'
        code += structure((COMPONENT / 'internal/esp32_mquickjs_wifi_twt_lane.h').read_text(),
                          'esp32_mquickjs_wifi_twt_token_t')
        code += structure((COMPONENT / 'internal/esp32_mquickjs_wifi_twt_sdk.h').read_text(),
                          'esp32_mquickjs_wifi_twt_probe_cut_t')
        code += TIMER_BOUNDARIES + BOUNDARIES
        for name in ('probe_result', 'fence', 'probe_retire'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        for name in ('probe_result', 'fence', 'probe_retire'):
            code += unit(COMPONENT / f'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_{name}.c')
        compile_run(self, code + MAIN)


BOUNDARIES = r'''
static esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_cancel(uint32_t);
static esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_quiescent(uint32_t,esp32_mquickjs_wifi_twt_probe_cut_t *);
static esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_release(uint32_t,const esp32_mquickjs_wifi_twt_probe_cut_t *,uint32_t);
'''
MAIN = r'''
static unsigned cancels,queries,releases,event_posts;
static int cancel_error,query_error,release_error,event_error;
static esp32_mquickjs_wifi_twt_probe_cut_t cut={17,3};
static esp32_mquickjs_wifi_twt_probe_event_fence_t event;
static bool event_queued;
int __real_wifi_event_post(int id,void *data,size_t size) {(void)id;(void)data;(void)size;assert(0);return -1;}
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned ticks) {
    assert(!locked && base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT && id==4 && ticks==0 && size==8);
    ++event_posts;
    assert(!timer_native.live && !timer_native.running && native_calls);
    if(event_error)return event_error;
    event=*(const esp32_mquickjs_wifi_twt_probe_event_fence_t *)data;event_queued=true;return ESP_OK;
}
static esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_cancel(uint32_t id) {
    ++cancels;assert(!locked && s_probe_result.identity==id && s_probe_result.owned);
    assert(esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(id)==ESP_OK);
    esp32_mquickjs_wifi_twt_probe_result_cancel_end_native(id,cancel_error);return cancel_error;
}
static esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_quiescent(uint32_t id,esp32_mquickjs_wifi_twt_probe_cut_t *out) {
    ++queries;assert(!locked && id==s_probe_result.identity && s_probe_result.owned && s_probe_result.cancel_complete);
    if(query_error)return query_error;
    *out=cut;return ESP_OK;
}
static esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_release(uint32_t id,const esp32_mquickjs_wifi_twt_probe_cut_t *expected,uint32_t seq) {
    ++releases;assert(!locked && id==s_probe_result.identity && s_probe_result.owned);
    if(release_error)return release_error;
    if(expected->tx_revision!=cut.tx_revision || expected->timer_identity!=cut.timer_identity)return ESP_ERR_NOT_FINISHED;
    return esp32_mquickjs_wifi_twt_probe_result_release_native(id,seq)?ESP_OK:ESP_ERR_NOT_FINISHED;
}
static void deliver(void) {
    assert(event_queued);event_queued=false;esp32_mquickjs_wifi_twt_probe_result_observe_fence(&event);
}
static uint32_t submit(void) {
    uint32_t id=0;assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(&id)==ESP_OK);
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(id,0);
    assert(esp32_mquickjs_wifi_twt_probe_result_claim_native(id));return id;
}
#define poll(state,token,id) esp32_mquickjs_wifi_twt_probe_retire_poll(state,token,id)
int main(void) {
    esp32_mquickjs_wifi_twt_token_t token={123,7},stale={124,7};
    esp32_mquickjs_wifi_twt_probe_retire_t state={0};
    uint32_t id=submit();cancel_error=71;
    assert(poll(&state,&token,id)==71 && s_probe_result.owned && cancels==1 && creates==0);
    assert(poll(&state,&stale,id)==ESP_ERR_INVALID_STATE && cancels==1);
    assert(poll(&state,&token,id+1)==ESP_ERR_INVALID_STATE && cancels==1);
    cancel_error=0;query_error=ESP_ERR_NOT_FINISHED;
    assert(poll(&state,&token,id)==ESP_ERR_NOT_FINISHED && cancels==2 && creates==0);
    query_error=0;create_error=72;
    assert(poll(&state,&token,id)==72 && cancels==2 && s_probe_result.owned);
    create_error=0;early_callback=true;hold_callback_exit=true;
    assert(poll(&state,&token,id)==ESP_ERR_TIMEOUT && !event_posts && !native_calls);
    timer_native.running=false;hold_callback_exit=false;delete_error=73;
    assert(poll(&state,&token,id)==73 && !event_posts && !native_calls);
    unsigned accepted_stops=stops;delete_error=0;native_error=74;
    assert(poll(&state,&token,id)==74 && stops==accepted_stops && !event_posts);
    native_error=0;event_error=75;
    assert(poll(&state,&token,id)==75 && !s_probe_result.fence_pending && s_probe_result.owned);
    unsigned accepted_native=native_calls;event_error=0;
    assert(poll(&state,&token,id)==ESP_ERR_NOT_FINISHED && event_queued && s_probe_result.owned);
    assert(native_calls==accepted_native && cancels==2);
    deliver();release_error=76;
    assert(poll(&state,&token,id)==76 && s_probe_result.owned);
    release_error=0;
    assert(poll(&state,&token,id)==ESP_OK && state.released && !s_probe_result.owned && !state.fence.timer);
    unsigned accepted_releases=releases;
    assert(poll(&state,&token,id)==ESP_OK && releases==accepted_releases);

    state=(esp32_mquickjs_wifi_twt_probe_retire_t){0};++token.identity;id=submit();
    assert(poll(&state,&token,id)==ESP_ERR_NOT_FINISHED && event_queued);
    esp32_mquickjs_wifi_twt_probe_event_fence_t old=event;
    ++cut.tx_revision;
    assert(poll(&state,&token,id)==ESP_ERR_NOT_FINISHED && s_probe_result.owned && !state.fence.token.identity);
    deliver();
    assert(poll(&state,&token,id)==ESP_ERR_NOT_FINISHED && event_queued && event.sequence!=old.sequence);
    esp32_mquickjs_wifi_twt_probe_result_observe_fence(&old);
    assert(!s_probe_result.event_fenced && s_probe_result.owned);
    deliver();assert(poll(&state,&token,id)==ESP_OK && !s_probe_result.owned);
    /* A changed cut while the TASK marker is still running retains storage
     * through failed clear, then starts a fresh marker and event sequence. */
    state=(esp32_mquickjs_wifi_twt_probe_retire_t){0};++token.identity;id=submit();
    hold_callback_exit=true;
    assert(poll(&state,&token,id)==ESP_ERR_TIMEOUT && timer_native.running);
    ++cut.tx_revision;
    assert(poll(&state,&token,id)==ESP_ERR_TIMEOUT && state.fence.clearing && s_probe_result.owned);
    timer_native.running=false;hold_callback_exit=false;
    assert(poll(&state,&token,id)==ESP_ERR_NOT_FINISHED && event_queued && !state.fence.clearing);
    deliver();assert(poll(&state,&token,id)==ESP_OK && !s_probe_result.owned);
    return 0;
}
'''
