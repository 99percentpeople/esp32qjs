"""Deferred production EAP snapshot; native task/getter boundaries injected.

Only AST-parse during the Wi-Fi implementation wave. Runtime execution belongs
to the concentrated stage and does not establish EAP server or RF acceptance.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit, INTERNAL
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import ROOT


class WiFiEapSdkSnapshot(unittest.TestCase):
    def test_actual_getter_and_failed_dispatch_do_not_invent_time_policy(self):
        folder = ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise'
        code = PRELUDE + unit(INTERNAL / 'esp32_mquickjs_wifi_eap_sdk.h')
        code += unit(folder / 'esp32_mquickjs_wifi_eap_sdk.c')
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/security/test_wifi_eap_sdk_snapshot/prelude.inc')


MAIN = fixture_text('wifi/security/test_wifi_eap_sdk_snapshot/main.inc')
