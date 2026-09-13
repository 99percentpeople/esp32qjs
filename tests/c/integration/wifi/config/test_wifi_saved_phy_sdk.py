"""Deferred saved-PHY production adapter and exact native ioctl dispatch.

Host-width structure access injects the two SDK readers. C5 layout assertions,
archive audit and target compilation are separate from these decision tests.
No fixture is imported, compiled or run during API implementation.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.tx.test_wifi_action_lane import PRELUDE
from tests.c.integration.wifi.tx.test_wifi_action_sdk import BOUNDARIES as ACTION_BOUNDARIES
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiSavedPhySdk(unittest.TestCase):
    def test_saved_both_bands_output_commit_and_private_dispatch(self):
        source = (COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action_sdk.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_action_sdk.h').read_text()
        for ap in (0, 1):
            code = PRELUDE + f'\n#define CONFIG_IDF_TARGET_ESP32C5 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {ap}\n#define ESP_ERR_NOT_SUPPORTED -4\n'
            code += sdk_types('esp32c5/representative', ('wifi_action_tx_req_t', 'wifi_roc_req_t', 'wifi_protocols_t', 'wifi_bandwidths_t'))
            code += structure(header, 'esp32_mquickjs_wifi_saved_phy_t')
            code += structure(source, 'action_sdk_phy_message_t') + structure(source, 'action_sdk_phy_request_t')
            code += '#define ACTION_SDK_SAVED_PHY_TYPE ((wifi_action_tx_t)(INT32_MAX - 2))\n'
            code += ACTION_BOUNDARIES + BOUNDARIES
            for name in ('esp32_mquickjs_wifi_action_receive', 'action_sdk_owner', 'action_sdk_guarded_callback', 'action_sdk_saved_phy_process',
                         '__wrap_wifi_action_tx_process', 'esp32_mquickjs_wifi_action_sdk_saved_phy'):
                code += extract(source, name)
            compile_run(self, code + MAIN)


BOUNDARIES = fixture_text('wifi/config/test_wifi_saved_phy_sdk/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_saved_phy_sdk/main.inc')
