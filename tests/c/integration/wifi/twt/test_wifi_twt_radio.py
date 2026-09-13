"""Deferred production Radio TWT probe admission/status/retirement glue.

The real lease registry and production Radio functions are used. SDK submit and
joint-retirement return boundaries are injected; test_wifi_twt_probe_retire
separately covers the real retirement coordinator. No SDK scheduling/RF proof.
Do not import, compile or execute before the Wi-Fi API stage.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_vendor_ie import vendor_code
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract

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
    code += fixture_text('wifi/twt/test_wifi_twt_radio/radio_code.inc')
    code += fixture_text('wifi/twt/test_wifi_twt_radio/radio_code-02.inc')
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


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_radio/boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_radio/main.inc')
