"""Deferred production Radio TWT probe admission/status/retirement glue.

The real lease registry and production Radio functions are used. SDK submit and
joint-retirement return boundaries are injected; test_wifi_twt_probe_retire
separately covers the real retirement coordinator. No SDK scheduling/RF proof.
Do not import, compile or execute before the Wi-Fi API stage.
"""
from pathlib import Path
import re
import unittest
from test_wifi_vendor_ie import vendor_code
from test_wifi_config_controls import sdk_types, structure
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract

INTERNAL = COMPONENT / 'internal'
SOURCE = COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c'


def twt_types():
    code = '#include <stdatomic.h>\ntypedef void *esp_timer_handle_t;\n#define ESP_EVENT_DECLARE_BASE(name)\n'
    sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
    code += '#define ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST 32U\n'
    code += structure(sdk, 'esp_wifi_btwt_info_t')
    code += structure((INTERNAL / 'esp32_mquickjs_wifi_twt_sdk.h').read_text(),
                      'esp32_mquickjs_wifi_twt_broadcast_snapshot_t')
    lane = (INTERNAL / 'esp32_mquickjs_wifi_twt_lane.h').read_text()
    for name in ('esp32_mquickjs_wifi_twt_token_t', 'esp32_mquickjs_wifi_twt_identity_t'):
        code += structure(lane, name)
    code += structure((INTERNAL / 'esp32_mquickjs_wifi_twt_sdk.h').read_text(),
                      'esp32_mquickjs_wifi_twt_probe_cut_t')
    for name in ('probe_result', 'fence', 'probe_retire', 'radio', 'probe_timer', 'probe_wake'):
        code += unit(INTERNAL / f'esp32_mquickjs_wifi_twt_{name}.h')
    code += '\n#define ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS 60000U\n'
    return code


def radio_code(ap=True):
    radio = SOURCE.read_text()
    header = (INTERNAL / 'esp32_mquickjs_wifi_radio.h').read_text()
    code = '#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#define CONFIG_IDF_TARGET_ESP32C5 1\n'
    code += vendor_code('esp32c5/representative', ap)
    code = code.replace('assert(locks && !critical);critical=1;', 'assert(!critical);critical=1;')
    code = code.replace('assert(locks && critical);critical=0;', 'assert(critical);critical=0;')
    op = re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
    op += structure(header, 'esp32_mquickjs_wifi_radio_operation_t')
    index = code.index('static struct {')
    code = code[:index] + op + code[index:]
    code = code.replace('struct {unsigned identity,lease_identity;} operation;',
                        'esp32_mquickjs_wifi_radio_operation_t operation;uint32_t next_operation_identity;')
    extra = ('wifi_ap_record_t', 'wifi_second_chan_t', 'wifi_event_sta_itwt_probe_t')
    code += sdk_types('esp32c5/representative', extra)[len(sdk_types('esp32c5/representative')):]
    declaration = 'static bool wifi_radio_twt_individual_lease_retained(const esp32_mquickjs_wifi_radio_lease_t *lease);\n'
    declaration += 'static bool wifi_radio_twt_broadcast_lease_retained(const esp32_mquickjs_wifi_radio_lease_t *lease);\n'
    position = code.index('static bool wifi_radio_lease_valid(')
    code = code[:position] + declaration + code[position:]
    code += '\n#ifndef TEST_TWT_AGREEMENT_RADIO\nstatic bool wifi_radio_twt_individual_lease_retained(const esp32_mquickjs_wifi_radio_lease_t *lease) {(void)lease;return false;}\n#endif\n'
    code += '\n#ifndef TEST_TWT_AGREEMENT_RADIO\nstatic bool wifi_radio_twt_broadcast_lease_retained(const esp32_mquickjs_wifi_radio_lease_t *lease) {(void)lease;return false;}\n#endif\n'
    code += twt_types()
    start = radio.index('static esp32_mquickjs_wifi_twt_identity_t s_twt_identity')
    code += radio[start:radio.index('\n#endif', start)]
    code += BOUNDARIES
    for name in ('wifi_radio_twt_probe_exact_locked', 'esp32_mquickjs_wifi_radio_twt_probe_submit',
                 'esp32_mquickjs_wifi_radio_twt_probe_status', 'esp32_mquickjs_wifi_radio_twt_probe_snapshot',
                 'esp32_mquickjs_wifi_radio_twt_probe_request_close', 'esp32_mquickjs_wifi_radio_twt_probe_cleanup_token',
                 'esp32_mquickjs_wifi_radio_twt_probe_retire', 'esp32_mquickjs_wifi_radio_end_operation'):
        code += extract(radio, name)
    return code


class WiFiTwtRadio(unittest.TestCase):
    def test_admission_exact_leases_submit_errors_and_retirement(self):
        for ap in (False, True):
            with self.subTest(softap=ap):
                compile_run(self, radio_code(ap) + MAIN)


BOUNDARIES = r'''
static unsigned submits, retires, next_native=1;
static int mode_error, channel_error, association_error, submit_error, retire_error;
static bool accept_native=true, early;
static esp32_mquickjs_wifi_twt_probe_result_snapshot_t native_result;
static esp32_mquickjs_wifi_twt_probe_timer_snapshot_t native_timer;
static esp32_mquickjs_wifi_twt_probe_wake_snapshot_t native_wake;
static int esp_wifi_get_mode(wifi_mode_t *out) {assert(locks && !critical);*out=s_radio.effective_mode;return mode_error;}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *out) {(void)out;assert(locks && !critical);return association_error;}
static int wifi_radio_get_channel_locked(uint8_t *out,wifi_second_chan_t *second,uint32_t *gen) {
    assert(locks && !critical);*out=6;*second=WIFI_SECOND_CHAN_NONE;*gen=s_radio.generation;return channel_error;
}
void esp32_mquickjs_wifi_twt_probe_result_snapshot(esp32_mquickjs_wifi_twt_probe_result_snapshot_t *out) {
    assert(!critical);*out=native_result;
}
void esp32_mquickjs_wifi_twt_probe_timer_snapshot(esp32_mquickjs_wifi_twt_probe_timer_snapshot_t *out) {
    assert(!critical);*out=native_timer;
}
void esp32_mquickjs_wifi_twt_probe_wake_snapshot(esp32_mquickjs_wifi_twt_probe_wake_snapshot_t *out) {
    assert(!critical);*out=native_wake;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_submit(uint32_t ms,uint32_t *id) {
    assert(locks && !critical && ms>=1 && ms<=60000);++submits;
    assert(s_radio.operation.identity && s_twt_probe.lease.acquired);
    assert(s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT]==1);
    if(early) {
        esp32_mquickjs_wifi_twt_probe_radio_state_t s;
        assert(esp32_mquickjs_wifi_radio_twt_probe_status(&s_twt_probe.state.token,&s));
        assert(s.dispatching && !s.native_identity);
    }
    if(accept_native) {
        *id=next_native++;
        native_result=(esp32_mquickjs_wifi_twt_probe_result_snapshot_t){.identity=*id,.owned=true,
            .event_seen=early,.event={.status=ITWT_PROBE_SUCCESS}};
    }
    return submit_error;
}
esp_err_t esp32_mquickjs_wifi_twt_probe_retire_poll(esp32_mquickjs_wifi_twt_probe_retire_t *s,
    const esp32_mquickjs_wifi_twt_token_t *t,uint32_t id) {
    assert(locks && !critical && s==&s_twt_probe.retirement && t->identity==s_twt_probe.state.token.identity);
    assert(id==native_result.identity && s_radio.operation.identity && s_twt_probe.lease.acquired);
    ++retires;s->stage="injected-native-suffix";
    if(!retire_error)native_result.owned=false;
    return retire_error;
}
'''
MAIN = r'''
static void reset_twt(void) {
    reset_vendor();s_radio.next_operation_identity=1;
    s_twt_probe=(__typeof__(s_twt_probe)){0};s_twt_identity.next_identity=1;
    native_result=(esp32_mquickjs_wifi_twt_probe_result_snapshot_t){0};
    native_timer=(esp32_mquickjs_wifi_twt_probe_timer_snapshot_t){0};
    native_wake=(esp32_mquickjs_wifi_twt_probe_wake_snapshot_t){0};
    submits=retires=0;mode_error=channel_error=association_error=submit_error=retire_error=0;
    accept_native=true;early=false;
}
int main(void) {
    reset_twt();esp32_mquickjs_wifi_twt_token_t token={0},old;
    assert(esp32_mquickjs_wifi_radio_twt_probe_submit(0,&token)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_twt_probe_submit(60001,&token)==ESP_ERR_INVALID_ARG && !submits);
    association_error=71;assert(esp32_mquickjs_wifi_radio_twt_probe_submit(1,&token)==71 && !token.identity);
    association_error=0;mode_error=72;assert(esp32_mquickjs_wifi_radio_twt_probe_submit(1,&token)==72);
    mode_error=0;channel_error=73;assert(esp32_mquickjs_wifi_radio_twt_probe_submit(1,&token)==73);
    channel_error=0;s_radio.next_operation_identity=0;
    assert(esp32_mquickjs_wifi_radio_twt_probe_submit(1,&token)==ESP_ERR_NO_MEM && !submits);
    s_radio.next_operation_identity=1;s_twt_identity.next_identity=0;
    assert(esp32_mquickjs_wifi_radio_twt_probe_submit(1,&token)==ESP_ERR_NO_MEM && !submits);
    reset_twt();esp32_mquickjs_wifi_radio_lease_t sta={0};
    wifi_radio_operation_lock();assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&sta));
    wifi_radio_live_lease_t *owner=wifi_radio_promiscuous_owner(sta.identity);
    owner->fixed_channel=true;owner->primary_channel=7;wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_twt_probe_submit(1,&token)==ESP_ERR_INVALID_STATE && !submits);
    owner->primary_channel=6;owner->raw_tx_identity=8;
    assert(esp32_mquickjs_wifi_radio_twt_probe_submit(1,&token)==ESP_ERR_INVALID_STATE && !submits);
    owner->raw_tx_identity=0;owner->channel_conflict=true;
    assert(esp32_mquickjs_wifi_radio_twt_probe_submit(1,&token)==ESP_ERR_INVALID_STATE && !submits);
    owner->channel_conflict=false;early=true;submit_error=74;
    assert(esp32_mquickjs_wifi_radio_twt_probe_submit(60000,&token)==74 && token.identity && submits==1);
    assert(s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA]==1);
    esp32_mquickjs_wifi_twt_probe_radio_state_t s;
    assert(esp32_mquickjs_wifi_radio_twt_probe_status(&token,&s) && s.native.owned && s.native.event_seen && s.submit_error==74);
    assert(!s.dispatching && s.native_identity);
    old=token;++old.identity;
    assert(!esp32_mquickjs_wifi_radio_twt_probe_status(&old,&s));
    assert(esp32_mquickjs_wifi_radio_twt_probe_retire(&old)==ESP_ERR_INVALID_STATE && !retires);
    esp32_mquickjs_wifi_radio_operation_t op=s_radio.operation;
    esp32_mquickjs_wifi_radio_end_operation(&op);assert(op.identity && s_radio.operation.identity);
    esp32_mquickjs_wifi_radio_lease_t borrowed=s_twt_probe.lease;
    wifi_radio_operation_lock();wifi_radio_release_locked(&borrowed);wifi_radio_operation_unlock();
    assert(borrowed.acquired && s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT]==1);
    retire_error=75;assert(esp32_mquickjs_wifi_radio_twt_probe_retire(&token)==75 && token.identity);
    assert(esp32_mquickjs_wifi_radio_twt_probe_status(&token,&s) && s.cleanup_pending && s.cleanup_error==75);
    assert(!strcmp(s.cleanup_stage,"injected-native-suffix"));
    native_timer.fault=76;assert(esp32_mquickjs_wifi_radio_twt_probe_status(&token,&s) && s.native_error==76);
    native_result.control_error=77;assert(esp32_mquickjs_wifi_radio_twt_probe_status(&token,&s) && s.native_error==77);
    native_result.control_error=native_timer.fault=0;old=token;retire_error=0;
    assert(!esp32_mquickjs_wifi_radio_twt_probe_retire(&token) && !token.identity);
    assert(!s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT] && s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA]==1);
    assert(esp32_mquickjs_wifi_radio_twt_probe_retire(&old)==ESP_ERR_INVALID_STATE);
    accept_native=false;submit_error=78;
    assert(esp32_mquickjs_wifi_radio_twt_probe_submit(1,&token)==78 && token.identity>old.identity);
    unsigned calls_before=retires;assert(!esp32_mquickjs_wifi_radio_twt_probe_retire(&token) && retires==calls_before);
    s_twt_identity.next_identity=UINT64_MAX;s_radio.next_operation_identity=UINT32_MAX;
    assert(esp32_mquickjs_wifi_radio_twt_probe_submit(1,&token)==78 && token.identity==UINT64_MAX);
    assert(!s_twt_identity.next_identity && !s_radio.next_operation_identity);
    assert(!esp32_mquickjs_wifi_radio_twt_probe_retire(&token));
    assert(esp32_mquickjs_wifi_radio_twt_probe_submit(1,&token)==ESP_ERR_NO_MEM);
    esp32_mquickjs_wifi_radio_twt_probe_snapshot(&s);assert(!s.token.identity && !s.cleanup_pending);
    assert(!locks && !critical);return 0;
}
'''
