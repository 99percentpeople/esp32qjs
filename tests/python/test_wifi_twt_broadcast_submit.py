"""Deferred managed broadcast submission through production dispatch and timer.

The SDK public call/native builder are injected boundaries, not an alternative
state machine. No fixture import/compile/run before the Wi-Fi validation stage.
"""
from pathlib import Path
import unittest
from test_wifi_twt_setup_timer import PRELUDE
from test_wifi_twt_broadcast_event import broadcast_types, BOUNDARIES
from test_wifi_twt_broadcast_timer import MAIN as TIMER_MAIN
from test_wifi_config_controls import structure
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiTwtBroadcastSubmit(unittest.TestCase):
    def test_exact_dispatch_retained_result_and_uncertain_return(self):
        twt = COMPONENT / 'src/modules/wifi_twt'
        sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        code = PRELUDE + broadcast_types() + BOUNDARIES
        code += structure(sdk, 'wifi_btwt_setup_config_t')
        code += r'''
#define ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS 60000U
typedef struct {wifi_btwt_setup_config_t config;uint32_t timeout_ms;} esp32_mquickjs_wifi_btwt_options_t;
esp_err_t esp_wifi_sta_btwt_setup(wifi_btwt_setup_config_t *);
esp_err_t __real_wifi_sta_btwt_setup_process(void *);
int pm_get_sleep_type(void);
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(unsigned);
void esp32_mquickjs_wifi_twt_tx_broadcast_cancel_native(void);
bool esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native(uintptr_t);
bool esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(unsigned);
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_cancel_request_native(unsigned,uint32_t);
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_quiescent_native(unsigned,uint32_t,uint32_t *);
void esp32_mquickjs_wifi_twt_sdk_broadcast_tx_complete_native(uintptr_t,uint8_t,const uint8_t *,uint8_t);
'''
        code += extract((twt / 'esp32_mquickjs_wifi_twt_options.c').read_text(), 'esp32_mquickjs_wifi_btwt_options_valid')
        for name in ('teardown_tx', 'tx', 'broadcast_event', 'broadcast_timer', 'broadcast_submit'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        for name in ('broadcast_event', 'broadcast_timer', 'broadcast_rx', 'broadcast_submit'):
            code += unit(twt / f'esp32_mquickjs_wifi_twt_{name}.c')
        code += extract((twt / 'esp32_mquickjs_wifi_twt_sdk.c').read_text(), '__wrap_wifi_sta_btwt_setup_process')
        code += TIMER_MAIN.split('int main(void) {')[0]
        compile_run(self, code + MAIN)


MAIN = r'''
static int public_error,driver_error,early_return,sleep_policy=1;
static bool early_callback,duplicate,omit_output,reenter,foreign_bind,duplicate_bind;
static unsigned public_calls,driver_calls;
static uint32_t next_tx;
static const wifi_btwt_setup_config_t *caller_config;
int pm_get_sleep_type(void){assert(!locked);return sleep_policy;}
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(unsigned slot) {
    assert(!locked);if(esp32_mquickjs_wifi_btwt_setup_held(slot))return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_btwt_timer_available_native(slot);
}
static void cold(void) {
    reset();memset(&s_btwt_submit,0,sizeof(s_btwt_submit));
    public_error=driver_error=early_return=0;sleep_policy=1;
    early_callback=duplicate=omit_output=reenter=foreign_bind=duplicate_bind=false;current_task=(void *)1;public_calls=driver_calls=next_tx=0;
    for(unsigned i=0;i<32;++i)current[i].parameter[3]=1;
}
esp_err_t __real_wifi_sta_btwt_setup_process(void *message) {
    wifi_btwt_setup_config_t *config;memcpy(&config,(uint8_t *)message+12,sizeof(config));
    assert(!locked && config==&s_btwt_submit.result.config && config!=caller_config && s_btwt_submit.result.driver_called);
    ++driver_calls;
    if(duplicate)assert(__wrap_wifi_sta_btwt_setup_process(message)==ESP_ERR_INVALID_STATE);
    if(!omit_output) {
        unsigned slot=config->btwt_id;uint32_t identity=++next_tx;
        assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(slot,identity,current[slot].node,current[slot].parameter));
        if(foreign_bind) {
            current_task=(void *)2;
            assert(!esp32_mquickjs_wifi_btwt_submit_bind_native(slot,999,current[slot].parameter));
            assert(!s_btwt_submit.result.identity && !esp32_mquickjs_wifi_btwt_setup_held(slot));current_task=(void *)1;
        }
        assert(!esp32_mquickjs_wifi_btwt_submit_bind_native(slot,identity,current[slot].parameter));
        if(duplicate_bind)assert(esp32_mquickjs_wifi_btwt_submit_bind_native(slot,identity,current[slot].parameter)==ESP_ERR_INVALID_STATE);
        assert(esp32_mquickjs_wifi_btwt_setup_held(slot) && s_btwt_submit.result.identity==identity);
        if(early_callback)esp32_mquickjs_wifi_btwt_setup_tx_complete_native(slot,identity,current[slot].node,9,current[slot].parameter,77);
        esp32_mquickjs_wifi_btwt_setup_submitted_native(slot,identity,driver_error);
    }
    return driver_error;
}
esp_err_t esp_wifi_sta_btwt_setup(wifi_btwt_setup_config_t *config) {
    assert(!locked && config==&s_btwt_submit.result.config && config!=caller_config);++public_calls;
    if(reenter){esp32_mquickjs_wifi_btwt_dispatch_t other={0};
        assert(esp32_mquickjs_wifi_twt_sdk_broadcast_submit(caller_config,&other)==ESP_ERR_INVALID_STATE && !other.identity);}
    if(public_error)return public_error;
    if(early_return==1)return 0;
    if(early_return==2){bool managed=false;assert(!esp32_mquickjs_wifi_btwt_submit_enter_native(config,&managed) && managed);return 0;}
    uint8_t message[12+sizeof(void *)]={0};memcpy(message+12,&config,sizeof(config));
    return __wrap_wifi_sta_btwt_setup_process(message);
}
int main(void) {
    wifi_btwt_setup_config_t config={.setup_cmd=TWT_REQUEST,.btwt_id=3,.timeout_time_ms=5000};caller_config=&config;
    esp32_mquickjs_wifi_btwt_dispatch_t out={0};esp32_mquickjs_wifi_btwt_timer_result_t result;
    cold();early_callback=duplicate=reenter=foreign_bind=true;observation_error=91;
    assert(!esp32_mquickjs_wifi_twt_sdk_broadcast_submit(&config,&out));
    assert(out.identity==1 && out.native_entered && out.driver_called && out.native_completed && !out.handoff_error && !s_btwt_submit.occupied);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,out.identity,&result) && result.held && result.complete && result.event.reason==77 && result.observation_error==91);
    assert(public_calls==1 && driver_calls==1);
    uint32_t identity=out.identity;out=(esp32_mquickjs_wifi_btwt_dispatch_t){0};
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_submit(&config,&out)==ESP_ERR_INVALID_STATE && !out.identity && !out.driver_called);
    assert(!esp32_mquickjs_wifi_btwt_setup_held(4));
    assert(esp32_mquickjs_wifi_btwt_setup_begin_native(3,100,current[3].node,current[3].parameter)==ESP_ERR_INVALID_STATE);
    __wrap_ieee80211_close_all_twt_sessions();
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,identity,&result) && result.held);
    assert(!esp32_mquickjs_wifi_btwt_timer_available_native(3)); /* Timer quiet is not owner retirement. */
    cold();out=(esp32_mquickjs_wifi_btwt_dispatch_t){0};duplicate_bind=true;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_submit(&config,&out)==ESP_ERR_INVALID_STATE && out.identity && out.handoff_error);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,out.identity,&result) && result.held);
    cold();out=(esp32_mquickjs_wifi_btwt_dispatch_t){0};public_error=81;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_submit(&config,&out)==81 && out.sdk_error==81 && !out.identity && !out.native_entered && !s_btwt_submit.occupied);
    cold();out=(esp32_mquickjs_wifi_btwt_dispatch_t){0};sleep_policy=0;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_submit(&config,&out)==ESP_ERR_INVALID_STATE && out.native_completed && !out.driver_called && !driver_calls);
    cold();out=(esp32_mquickjs_wifi_btwt_dispatch_t){0};driver_error=82;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_submit(&config,&out)==82 && out.identity && out.driver_error==82);
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,out.identity,&result) && result.held && result.submit_error==82);
    cold();out=(esp32_mquickjs_wifi_btwt_dispatch_t){0};omit_output=true;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_submit(&config,&out)==ESP_ERR_INVALID_STATE && !out.identity && out.handoff_error && out.native_completed && !s_btwt_submit.occupied);
    for(int mode=1;mode<=2;++mode) {
        cold();out=(esp32_mquickjs_wifi_btwt_dispatch_t){0};early_return=mode;
        assert(esp32_mquickjs_wifi_twt_sdk_broadcast_submit(&config,&out)==ESP_ERR_INVALID_STATE && out.handoff_error && s_btwt_submit.occupied);
        bool managed=false;assert(esp32_mquickjs_wifi_btwt_submit_enter_native(&s_btwt_submit.result.config,&managed)==ESP_ERR_INVALID_STATE);
        esp32_mquickjs_wifi_btwt_dispatch_t again={0};assert(esp32_mquickjs_wifi_twt_sdk_broadcast_submit(&config,&again)==ESP_ERR_INVALID_STATE && public_calls==1);
    }
    cold();bool managed=false;assert(!esp32_mquickjs_wifi_btwt_submit_enter_native(&config,&managed) && !managed);
    assert(!esp32_mquickjs_wifi_btwt_submit_bind_native(3,999,current[3].parameter) && !esp32_mquickjs_wifi_btwt_setup_held(3));
    cold();assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,10,current[3].node,current[3].parameter));
    s_btwt_timer.snapshot.revision=UINT32_MAX;
    assert(esp32_mquickjs_wifi_btwt_setup_hold_native(3,10)==ESP_ERR_NO_MEM && !esp32_mquickjs_wifi_btwt_setup_held(3));
    cold();out=(esp32_mquickjs_wifi_btwt_dispatch_t){0};config.btwt_id=32;
    assert(esp32_mquickjs_wifi_twt_sdk_broadcast_submit(&config,&out)==ESP_ERR_INVALID_ARG && !public_calls && !s_btwt_submit.occupied);
    cold();assert(!native_live && !locked);return 0;
}
'''
