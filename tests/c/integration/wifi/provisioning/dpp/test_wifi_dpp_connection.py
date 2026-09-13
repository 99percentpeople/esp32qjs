"""Deferred production DPP selection validation; no SDK mutation or RF proof."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT / 'components/esp32_mquickjs'


def unit(path):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', path.read_text(), flags=re.M)


class DppConnection(unittest.TestCase):
    def test_authentication_selection_and_unrepresentable_credentials(self):
        for sae in (0, 1):
            with self.subTest(sae=sae):
                code = '#define CONFIG_ESP_WIFI_ENABLE_WPA3_SAE %d\n' % sae + TYPES
                code += unit(ROOT / 'internal/esp32_mquickjs_wifi_dpp_connection.h')
                code += unit(ROOT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_connection.c')
                compile_run(self, code + MAIN)


ROW = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_connection/row.inc')
TYPES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_connection/types.inc') + ROW
MAIN = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_connection/main.inc')
