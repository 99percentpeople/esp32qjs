"""Production interface-configuration executor; SDK/helper boundaries injected."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import pathlib
import unittest
from tests.support.wireless_vm_fixture import extract
from tests.support.native_compile import compile_run
from tests.c.integration.wifi.config.test_wifi_config_controls import structure

ROOT = TEST_ROOT
WIFI = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'


class WiFiConfigurationExecutor(unittest.TestCase):
    def code(self):
        source = WIFI.read_text()
        body = ''.join(extract(source, name) for name in [
            'esp32_mquickjs_wifi_configuration_pending',
            'esp32_mquickjs_wifi_cleanup_ap_configuration',
            'wifi_configure_selected_interfaces', 'esp32_mquickjs_wifi_configure_interfaces',
            'esp32_mquickjs_wifi_activate_ap',
            'esp32_mquickjs_wifi_apply_configuration'])
        radio_header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_radio.h').read_text()
        wifi_header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi.h').read_text()
        structs = structure(radio_header, 'esp32_mquickjs_wifi_radio_configuration_selection_t')
        structs += structure(wifi_header, 'esp32_mquickjs_wifi_configuration_t')
        structs += structure(wifi_header, 'esp32_mquickjs_wifi_configuration_execution_t')
        return BOUNDARIES.replace('/* CAPTURE_STRUCTS */', structs) + body

    def test_prepare_commit_handoff_failures_and_cleanup_scope(self):
        compile_run(self,self.code()+MAIN)

    def test_explicit_ap_activation_retains_failure_suffix_and_does_not_write_station_config(self):
        compile_run(self,self.code()+fixture_text('wifi/config/test_wifi_configuration_executor/test_explicit_ap_activation_retains_failure_suffix_and_does_not_write_station_config.inc'))

    def test_initialize_requires_exact_stopped_unowned_lifecycle(self):
        radio = (WIFI.parent.parent / 'wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        compile_run(self, fixture_text('wifi/config/test_wifi_configuration_executor/test_initialize_requires_exact_stopped_unowned_lifecycle-02.inc') + extract(radio, 'esp32_mquickjs_wifi_radio_initialize_lifecycle') + fixture_text('wifi/config/test_wifi_configuration_executor/test_initialize_requires_exact_stopped_unowned_lifecycle.inc'))


BOUNDARIES = fixture_text('wifi/config/test_wifi_configuration_executor/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_configuration_executor/main.inc')
