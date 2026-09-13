"""Deferred actual multi-owner Radio setup/close paths; native calls are boundaries.

No fixture import, compilation or execution until the Wi-Fi validation stage.
Does not stand in for native SDK scheduling, RF teardown or full Future core.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.c.integration.wifi.twt.test_wifi_twt_radio import radio_code, INTERNAL, SOURCE
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def agreement_radio_code():
    source = SOURCE.read_text()
    sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
    code = '#define TEST_TWT_AGREEMENT_RADIO 1\n' + radio_code()
    code += '\n#include <stdlib.h>\n#define ESP_ERR_NOT_FINISHED 0x104\n'
    code += re.search(r'typedef enum \{[^}]*\} wifi_twt_setup_cmds_t;', sdk).group(0)
    code += structure(sdk, 'wifi_twt_setup_config_t')
    code += '\ntypedef wifi_twt_setup_config_t wifi_itwt_setup_config_t;\n'
    code += structure(sdk, 'wifi_event_sta_itwt_setup_t')
    code += structure((INTERNAL / 'esp32_mquickjs_wifi_twt_options.h').read_text(), 'esp32_mquickjs_wifi_itwt_options_t')
    code += structure((INTERNAL / 'esp32_mquickjs_wifi_twt_sdk.h').read_text(), 'esp32_mquickjs_wifi_twt_setup_cut_t')
    code += structure(sdk, 'wifi_btwt_setup_config_t')
    code += re.search(r'typedef enum \{[^}]*\} wifi_btwt_setup_status_t;', sdk).group(0)
    code += structure(sdk, 'wifi_event_sta_btwt_setup_t')
    code += structure((INTERNAL / 'esp32_mquickjs_wifi_twt_options.h').read_text(), 'esp32_mquickjs_wifi_btwt_options_t')
    for name in ('teardown_tx', 'broadcast_timer', 'broadcast_submit', 'broadcast_retire', 'information_timer', 'information', 'setup_result', 'setup_submit', 'setup_retire', 'agreement_radio'):
        code += unit(INTERNAL / f'esp32_mquickjs_wifi_twt_{name}.h')
    code += structure(source, 'wifi_radio_twt_individual_t')
    code += '\nstatic wifi_radio_twt_individual_t *s_twt_individual;\n'
    code += '#define ESP32_MQUICKJS_WIFI_TWT_MAX_SLEEP_US (UINT64_C(1)<<35)\n'
    options = (SOURCE.parent.parent / 'wifi_twt/esp32_mquickjs_wifi_twt_options.c').read_text()
    for name in ('esp32_mquickjs_wifi_itwt_interval_us', 'esp32_mquickjs_wifi_itwt_duration_us', 'esp32_mquickjs_wifi_itwt_options_valid'):
        code += extract(options, name)
    code += structure(source, 'wifi_radio_twt_broadcast_t')
    code += '\nstatic wifi_radio_twt_broadcast_t **s_twt_broadcast;\n'
    code += extract(options, 'esp32_mquickjs_wifi_btwt_options_valid')
    code += BOUNDARIES
    code += BROADCAST_BOUNDARIES
    for name in ('wifi_radio_twt_individual_lease_retained', 'wifi_radio_twt_individual_find',
                 'esp32_mquickjs_wifi_radio_twt_individual_submit', 'esp32_mquickjs_wifi_radio_twt_individual_status',
                 'esp32_mquickjs_wifi_radio_twt_individual_tokens', 'esp32_mquickjs_wifi_radio_twt_individual_request_close',
                 'wifi_radio_twt_individual_retire', 'esp32_mquickjs_wifi_radio_twt_individual_close',
                 'wifi_radio_twt_individual_information', 'esp32_mquickjs_wifi_radio_twt_individual_suspend',
                 'esp32_mquickjs_wifi_radio_twt_individual_resume', 'wifi_radio_twt_broadcast_lease_retained',
                 'wifi_radio_twt_broadcast_find', 'esp32_mquickjs_wifi_radio_twt_broadcast_submit',
                 'esp32_mquickjs_wifi_radio_twt_broadcast_status', 'esp32_mquickjs_wifi_radio_twt_broadcast_tokens',
                 'esp32_mquickjs_wifi_radio_twt_broadcast_request_close', 'wifi_radio_twt_broadcast_retire',
                 'esp32_mquickjs_wifi_radio_twt_broadcast_close', 'esp32_mquickjs_wifi_radio_twt_close_agreements',
                 'esp32_mquickjs_wifi_radio_twt_close_pending'):
        code += extract(source, name)
    return code


class WiFiTwtAgreementRadio(unittest.TestCase):
    def test_multiple_owners_early_status_exact_close_and_failed_suffix(self):
        compile_run(self, agreement_radio_code() + MAIN)


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_agreement_radio/boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_agreement_radio/main.inc')

BOUNDARIES += fixture_text('wifi/twt/test_wifi_twt_agreement_radio/fragment.inc')

BROADCAST_BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_agreement_radio/broadcast_boundaries.inc')
