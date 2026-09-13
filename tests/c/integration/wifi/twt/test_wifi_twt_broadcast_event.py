"""Deferred production broadcast event normalization and synchronous capture.

No fixture import, compilation or execution until Wi-Fi staged validation.
SDK field declarations are read from the pinned SDK; production static asserts
and target builds, rather than Host layout, establish the C5 ABI.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.twt.test_wifi_twt_setup_timer import PRELUDE
from tests.support.native_compile import compile_run


def broadcast_types(commands=True):
    sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
    names = ['wifi_btwt_setup_status_t', 'wifi_btwt_teardown_status_t']
    if commands:
        names.insert(0, 'wifi_twt_setup_cmds_t')
    code = ''.join(re.search(r'typedef enum \{[^}]*\} ' + name + ';', sdk).group(0) for name in names)
    for name in ('wifi_event_sta_btwt_setup_t', 'wifi_event_sta_btwt_teardown_t'):
        code += structure(sdk, name)
    return code


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_broadcast_event/boundaries.inc')


class WiFiTwtBroadcastEvent(unittest.TestCase):
    def test_scopes_padding_duplicates_foreign_task_and_saturation(self):
        code = PRELUDE + broadcast_types() + BOUNDARIES
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_broadcast_event.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_event.c')
        compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/twt/test_wifi_twt_broadcast_event/main.inc')
