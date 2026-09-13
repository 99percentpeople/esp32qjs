"""Deferred production setup result registry and native event-post routing.

Only allocation, locks and event delivery are injected. Release checks exercise
the proof consumer, not actual SDK retirement. No import/compile/run before the
Wi-Fi API stage; AST validation is not runtime evidence.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiTwtSetupResult(unittest.TestCase):
    def test_native_capture_queue_failures_identity_retention_and_foreign_events(self):
        sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        code = PRELUDE
        code += re.search(r'typedef enum \{[^}]*\} wifi_twt_setup_cmds_t;', sdk).group(0)
        code += structure(sdk, 'wifi_twt_setup_config_t')
        code += '\ntypedef wifi_twt_setup_config_t wifi_itwt_setup_config_t;\n'
        code += structure(sdk, 'wifi_event_sta_itwt_setup_t')
        code += re.search(r'typedef enum \{[^}]*\} wifi_itwt_teardown_status_t;', sdk).group(0)
        code += structure(sdk, 'wifi_event_sta_itwt_teardown_t')
        code += re.search(r'typedef enum \{[^}]*\} wifi_itwt_probe_status_t;', sdk).group(0)
        code += structure(sdk, 'wifi_event_sta_itwt_probe_t')
        for name in ('setup_result', 'probe_result'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        for name in ('setup_result', 'probe_result'):
            code += unit(COMPONENT / f'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_{name}.c')
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        code += RADIO_BOUNDARIES + extract(radio, 'wifi_radio_lifecycle_fence')
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/twt/test_wifi_twt_setup_result/prelude.inc')
RADIO_BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_setup_result/radio_boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_setup_result/main.inc')
