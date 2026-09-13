"""Deferred production worker scheduling/ownership tests; AST only during implementation.

SDK/IPC/timer calls are controllable boundaries. The production worker itself
chooses sequencing, stores arguments, copies results and releases storage.
Does not qualify RF, driver scan internals or the future public Radio Session.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT


def declarations(text):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', text, flags=re.M)


class WPSWorker(unittest.TestCase):
    def test_production_worker_retains_ipc_and_orders_retirement(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        sdk_header = Path(sdk) / 'components/wpa_supplicant/esp_supplicant/include/esp_wps.h'
        internal = ROOT / 'components/esp32_mquickjs/internal'
        source = declarations((ROOT / 'components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_worker.c').read_text())
        # Real target builds check the 12-byte IPC ABI; this fixture runs with
        # host pointer widths and checks retained ownership and call ordering.
        source = source.replace('_Static_assert(sizeof(wps_ipc_config_t) == 12, "review WPS IPC ABI");', '')
        code = PRELUDE + declarations(sdk_header.read_text())
        code += declarations((internal / 'esp32_mquickjs_wifi_wps_sdk.h').read_text())
        code += declarations((internal / 'esp32_mquickjs_wifi_wps_worker.h').read_text())
        compile_run(self, code + BOUNDARIES + source + IMPLEMENTATIONS + MAIN)


PRELUDE = fixture_text('wifi/provisioning/wps/test_wifi_wps_worker/prelude.inc')

BOUNDARIES = fixture_text('wifi/provisioning/wps/test_wifi_wps_worker/boundaries.inc')

IMPLEMENTATIONS = fixture_text('wifi/provisioning/wps/test_wifi_wps_worker/implementations.inc')

MAIN = fixture_text('wifi/provisioning/wps/test_wifi_wps_worker/main.inc')
