"""Deferred pending-setup retirement with production result/marker/executor.

SDK quiescence has its separate native implementation fixture. Here only its
boundary, native timer scheduling and event delivery are injected. No fixture
import, compilation or execution before the Wi-Fi API validation stage.
"""
from pathlib import Path
import re
import unittest
from test_wifi_config_controls import structure
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wifi_twt_setup_result import PRELUDE
from test_wifi_twt_fence import BOUNDARIES as TIMER_BOUNDARIES
from test_wireless_control_regression import compile_run


class WiFiTwtSetupRetire(unittest.TestCase):
    def test_exact_owner_cut_changes_event_revocation_and_failed_cleanup_suffix(self):
        sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        code = PRELUDE + '\n#include <stdatomic.h>\n#undef ESP_ERR_NOT_FINISHED\n'
        code += re.search(r'typedef enum \{[^}]*\} wifi_twt_setup_cmds_t;', sdk).group(0)
        code += structure(sdk, 'wifi_twt_setup_config_t')
        code += '\ntypedef wifi_twt_setup_config_t wifi_itwt_setup_config_t;\n'
        code += structure(sdk, 'wifi_event_sta_itwt_setup_t')
        code += re.search(r'typedef enum \{[^}]*\} wifi_itwt_teardown_status_t;', sdk).group(0)
        code += structure(sdk, 'wifi_event_sta_itwt_teardown_t')
        for file, name in [('lane', 'esp32_mquickjs_wifi_twt_token_t'),
                           ('sdk', 'esp32_mquickjs_wifi_twt_setup_cut_t')]:
            code += structure((COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{file}.h').read_text(), name)
        code += TIMER_BOUNDARIES
        for name in ('setup_result', 'fence', 'setup_retire'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        code += BOUNDARIES
        for name in ('setup_result', 'fence', 'setup_retire'):
            code += unit(COMPONENT / f'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_{name}.c')
        compile_run(self, code + MAIN)


BOUNDARIES = r'''
static unsigned cancels,queries,releases,event_posts;
static int cancel_error,query_error,release_error,event_error;
static esp32_mquickjs_wifi_twt_setup_cut_t current_cut={.tx_revision=17,.timer_revision=3};
static unsigned tx_releases;
static int tx_release_error;
static esp32_mquickjs_wifi_twt_setup_event_fence_t queued;
static bool event_queued;
static esp32_mquickjs_wifi_twt_setup_result_t result(uint32_t id) {
    esp32_mquickjs_wifi_twt_setup_result_t out;
    assert(esp32_mquickjs_wifi_twt_setup_result_read(id,&out)==0);return out;
}
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned ticks) {
    assert(!locked && ticks==0);
    if(base==WIFI_EVENT){assert((id==28 && size==32) || (id==29 && size==8));return 0;}
    assert(base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT && id==5 && size==8);
    assert(!timer_native.live && !timer_native.running && native_calls);++event_posts;
    if(event_error)return event_error;
    queued=*(const esp32_mquickjs_wifi_twt_setup_event_fence_t *)data;event_queued=true;return 0;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_cancel(uint32_t id) {
    assert(!locked);++cancels;
    if(cancel_error)return cancel_error;
    assert(esp32_mquickjs_wifi_twt_setup_result_cancelled_native(id,8));return 0;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_quiescent(uint32_t id,esp32_mquickjs_wifi_twt_setup_cut_t *out) {
    assert(!locked && (result(id).flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED));++queries;
    if(query_error)return query_error;*out=current_cut;return 0;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_release(uint32_t id,const esp32_mquickjs_wifi_twt_setup_cut_t *cut,uint32_t sequence) {
    assert(!locked);++releases;
    if(release_error)return release_error;
    if(memcmp(cut,&current_cut,sizeof(*cut)))return ESP_ERR_NOT_FINISHED;
    esp32_mquickjs_wifi_twt_setup_result_t got=result(id);
    return esp32_mquickjs_wifi_twt_setup_result_release_native(id,got.revision,sequence)?0:ESP_ERR_NOT_FINISHED;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_teardown_tx_release(uint32_t id,uint32_t revision) {
    esp32_mquickjs_wifi_twt_setup_result_t gone;
    assert(!locked && revision==current_cut.teardown_revision);
    assert(esp32_mquickjs_wifi_twt_setup_result_read(id,&gone)==ESP_ERR_INVALID_STATE);
    ++tx_releases;return tx_release_error;
}
'''
MAIN = r'''
static void deliver(void) {
    assert(event_queued);event_queued=false;esp32_mquickjs_wifi_twt_setup_result_observe_fence(&queued);
}
static uint32_t begin(int16_t request_id) {
    uint32_t identity=0;assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(request_id,&identity)==0);
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(identity,0);return identity;
}
#define poll(state,token,id) esp32_mquickjs_wifi_twt_setup_retire_poll(state,token,id)
int main(void) {
    uint32_t identity=begin(0);
    esp32_mquickjs_wifi_twt_token_t token={123,7},foreign={124,7};
    esp32_mquickjs_wifi_twt_setup_retire_t state={0};
    cancel_error=71;assert(poll(&state,&token,identity)==71 && !creates && cancels==1);
    assert(poll(&state,&foreign,identity)==ESP_ERR_INVALID_STATE && cancels==1);
    assert(poll(&state,&token,identity+1)==ESP_ERR_INVALID_STATE && cancels==1);
    cancel_error=0;query_error=ESP_ERR_NOT_FINISHED;
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && cancels==2 && !creates);
    query_error=0;create_error=72;
    assert(poll(&state,&token,identity)==72 && cancels==2 && state.fence.token.identity);
    create_error=0;hold_callback_exit=true;
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && creates==2 && !event_posts);
    fire();assert(poll(&state,&token,identity)==ESP_ERR_TIMEOUT && timer_native.live && !event_posts);
    timer_native.running=false;hold_callback_exit=false;delete_error=73;
    assert(poll(&state,&token,identity)==73 && timer_native.live && state.fence.stopped);
    unsigned stopped=stops;delete_error=0;native_error=74;
    assert(poll(&state,&token,identity)==74 && stops==stopped && !timer_native.live && !event_posts);
    native_error=0;event_error=75;
    assert(poll(&state,&token,identity)==75 && !state.event_sequence);
    event_error=0;assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && event_queued);
    esp32_mquickjs_wifi_twt_setup_event_fence_t old=queued;
    ++current_cut.timer_revision;
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && !state.fence.token.identity && !state.event_sequence);
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && timer_native.live);
    fire();assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && event_queued);
    esp32_mquickjs_wifi_twt_setup_result_observe_fence(&old);
    assert(!(result(identity).flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_SEEN));
    deliver();release_error=76;
    assert(poll(&state,&token,identity)==76 && !state.released && !timer_native.live);
    release_error=0;assert(poll(&state,&token,identity)==0 && state.released && !state.fence.token.identity);
    unsigned before=releases;assert(poll(&state,&token,identity)==0 && releases==before);
    /* A new native observation after cancellation revokes old proof and
     * requires fresh cancellation/ordering, even if its payload is repeated. */
    identity=begin(1);state=(esp32_mquickjs_wifi_twt_setup_retire_t){0};
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED);fire();
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && event_queued);old=queued;
    wifi_event_sta_itwt_setup_t event={.config={.twt_id=1,.flow_id=3},.status=1};
    assert(esp32_mquickjs_wifi_twt_setup_result_post(&event,sizeof(event))==0);
    before=cancels;
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && cancels==before+1 && timer_native.live);
    esp32_mquickjs_wifi_twt_setup_result_observe_fence(&old);
    fire();assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && event_queued);
    deliver();assert(poll(&state,&token,identity)==0 && state.released);
    /* Teardown shares the ordering executor. Release failure after removing
     * the result must retry TX only, without reading/releasing that result. */
    identity=begin(2);state=(esp32_mquickjs_wifi_twt_setup_retire_t){0};
    event.config.twt_id=2;assert(esp32_mquickjs_wifi_twt_setup_result_post(&event,sizeof(event))==0);
    assert(esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(identity,3)==0);
    esp32_mquickjs_wifi_twt_setup_result_teardown_submitted_native(identity,0);
    wifi_event_sta_itwt_teardown_t teardown={.flow_id=3,.status=1};
    assert(esp32_mquickjs_wifi_twt_setup_result_teardown_post(&teardown,sizeof(teardown))==0);
    current_cut.information_revision=5;current_cut.teardown_revision=19;
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED);fire();
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED);deliver();
    tx_release_error=81;
    assert(poll(&state,&token,identity)==81 && state.result_released && !state.released && tx_releases==1);
    before=releases;unsigned queried=queries;
    tx_release_error=0;assert(poll(&state,&token,identity)==0 && state.released && tx_releases==2);
    assert(releases==before && queries==queried);
    heap_caps_free(s_setup_results.entries);assert(!live && !locked && !timer_native.live);return 0;
}
'''
