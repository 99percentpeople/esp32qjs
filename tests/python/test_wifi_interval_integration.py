"""Deferred production Radio interval/ESP-NOW adapters; SDK and state storage injected."""
import unittest
from test_wifi_connection_controls import control_code
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def integration_code():
    code = control_code('esp32c5/representative', False)
    code = code.replace('wifi_mode_t effective_mode;', 'wifi_mode_t effective_mode;bool stop_required;')
    code = code.replace('client;} wifi_radio_live_lease_t;', 'client;bool fixed_channel,channel_conflict;} wifi_radio_live_lease_t;')
    code += '\n#define CONFIG_ESP32_MQUICKJS_WIFI_RADIO 1\n#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_interval.h')
    code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_interval.c')
    code += BOUNDARIES
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    for name in ('wifi_radio_interval_writer', 'wifi_radio_interval_owner',
                 'esp32_mquickjs_wifi_radio_interval_configure', 'esp32_mquickjs_wifi_radio_interval_release',
                 'esp32_mquickjs_wifi_radio_interval_status', 'esp32_mquickjs_wifi_radio_write_interval'):
        code += extract(radio, name)
    wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
    code += extract(wifi, 'esp32_mquickjs_wifi_apply_interval')
    now = (COMPONENT / 'src/modules/espnow/esp32_mquickjs_espnow.c').read_text()
    for name in ('espnow_restore_power_save', 'espnow_apply_power_save'):
        code += extract(now, name)
    return code


class WiFiIntervalIntegration(unittest.TestCase):
    def test_real_admission_exact_tokens_partial_write_and_restore_without_native_module(self):
        compile_run(self, integration_code() + MAIN)


BOUNDARIES = r'''
static esp32_mquickjs_wifi_interval_state_t s_interval;
static uint16_t native_interval,native_window;
static unsigned interval_calls,window_calls;
static int interval_error,window_error;
static int esp_wifi_connectionless_module_set_wake_interval(uint16_t value) {
    assert(locks==1 && !critical && !helper_locks);
    ++interval_calls;native_interval=value;return interval_error;
}
typedef struct {
    esp32_mquickjs_wifi_radio_lease_t radio_lease;
    esp32_mquickjs_wifi_interval_token_t interval_token;
    bool power_save_fault,power_save_cleanup_required,now_initialized;
    uint8_t close_power_phase;
} espnow_session_t;
static int esp_now_set_wake_window(uint16_t value) {
    assert(!locks && !critical && !helper_locks);
    ++window_calls;native_window=value;return window_error;
}
'''

MAIN = r'''
static espnow_session_t session;
static const char *stage;
static void baseline(void) {
    reset();memset(&s_interval,0,sizeof(s_interval));memset(&session,0,sizeof(session));
    interval_calls=window_calls=0;interval_error=window_error=0;
    assert(esp32_mquickjs_wifi_apply_interval(300,&result)==ESP_OK && native_interval==300);
    session.now_initialized=true;
    session.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){7,21,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,true};
    s_radio.leases[2]=(wifi_radio_live_lease_t){21,session.radio_lease.client};
}
int main(void) {
    baseline();
    assert(esp32_mquickjs_wifi_apply_interval(600,&result)==ESP_ERR_INVALID_STATE && interval_calls==1);
    esp32_mquickjs_wifi_radio_lease_t forged=session.radio_lease;forged.generation++;
    assert(esp32_mquickjs_wifi_radio_interval_configure(&forged,100,&session.interval_token)==ESP_ERR_INVALID_STATE);
    s_radio.wake_locks=1;
    assert(espnow_apply_power_save(&session,true,20,100,&stage)==ESP_ERR_INVALID_STATE && !session.power_save_fault && !session.interval_token.identity);
    s_radio.wake_locks=0;
    assert(espnow_apply_power_save(&session,true,20,100,&stage)==ESP_OK && native_interval==100 && native_window==20);
    s_radio.operation.identity=2;
    assert(espnow_apply_power_save(&session,true,40,200,&stage)==ESP_ERR_INVALID_STATE && !session.power_save_fault && native_interval==100);
    s_radio.operation.identity=0;
    esp32_mquickjs_wifi_interval_token_t old=session.interval_token;
    assert(espnow_apply_power_save(&session,true,40,200,&stage)==ESP_OK && s_interval.previous==300);
    window_error=77;
    assert(espnow_apply_power_save(&session,true,50,400,&stage)==77 && session.power_save_fault && session.interval_token.identity);
    unsigned before=interval_calls;window_error=0;
    assert(espnow_apply_power_save(&session,true,60,500,&stage)==ESP_ERR_INVALID_STATE && interval_calls==before);
    interval_error=88;
    assert(espnow_restore_power_save(&session,&stage)==88 && session.close_power_phase==1 && session.interval_token.identity && s_interval.uncertain);
    before=window_calls;interval_error=0;
    assert(espnow_restore_power_save(&session,&stage)==ESP_OK && window_calls==before && native_interval==300 && !session.interval_token.identity);
    session.power_save_fault=false;
    assert(espnow_apply_power_save(&session,true,10,100,&stage)==ESP_OK);
    before=interval_calls;
    assert(esp32_mquickjs_wifi_radio_interval_release(&session.radio_lease,&old)==ESP_ERR_INVALID_STATE && interval_calls==before);
    session.now_initialized=false;before=window_calls;
    assert(espnow_restore_power_save(&session,&stage)==ESP_OK && window_calls==before && native_interval==300);
    baseline();interval_error=66;
    assert(espnow_apply_power_save(&session,true,10,100,&stage)==66 && !window_calls && session.interval_token.identity && session.power_save_fault);
    interval_error=0;
    assert(espnow_restore_power_save(&session,&stage)==ESP_OK && !window_calls && native_interval==300);
    baseline();before=interval_calls;
    assert(espnow_apply_power_save(&session,false,0,0,&stage)==ESP_OK && interval_calls==before && native_interval==300 && native_window==UINT16_MAX);
    s_radio.leases[2].identity=0;interval_error=99;
    assert(esp32_mquickjs_wifi_apply_interval(800,&result)==99 && result.mutation_attempted && s_interval.uncertain);
    interval_error=0;
    assert(esp32_mquickjs_wifi_apply_interval(900,&result)==ESP_OK && s_interval.known && !s_interval.uncertain);
    assert(!locks && !critical && !helper_locks);
    return 0;
}
'''
