"""Deferred pending-setup retirement with production result/marker/executor.

SDK quiescence has its separate native implementation fixture. Here only its
boundary, native timer scheduling and event delivery are injected. No fixture
import, compilation or execution before the Wi-Fi API validation stage.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.twt.test_wifi_twt_setup_result import PRELUDE
from tests.c.integration.wifi.twt.test_wifi_twt_fence import BOUNDARIES as TIMER_BOUNDARIES
from tests.support.native_compile import compile_run


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


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_setup_retire/boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_setup_retire/main.inc')
