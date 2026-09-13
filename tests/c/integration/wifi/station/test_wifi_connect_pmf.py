"""Production connect preparation; executor boundary verifies handoff only.

Deferred phase fixture. Native stopped PMF transaction, lifecycle admission and
post-START connect readback are covered by their separate production fixtures.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.support.wireless_vm_fixture import ROOT, extract
from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, sdk_types, structure
from tests.support.native_compile import compile_run


class WiFiConnectPmf(unittest.TestCase):
    def test_preparation_security_and_exact_executor_request(self):
        base = ROOT / 'components/esp32_mquickjs'
        radio = (base / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        wifi = (base / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        header = (base / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = PRELUDE + '#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n' + sdk_types('esp32c5/representative')
        code += structure(header, 'esp32_mquickjs_wifi_radio_configuration_selection_t')
        code += fixture_text('wifi/station/test_wifi_connect_pmf/test_preparation_security_and_exact_executor_request.inc')
        code += extract(radio, 'esp32_mquickjs_wifi_radio_pmf_disable_allowed')
        code += extract(wifi, 'esp32_mquickjs_wifi_prepare_connect')
        code += fixture_text('wifi/station/test_wifi_connect_pmf/test_preparation_security_and_exact_executor_request-02.inc')
        compile_run(self, code)
