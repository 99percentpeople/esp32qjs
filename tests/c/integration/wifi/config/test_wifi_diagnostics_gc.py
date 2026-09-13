"""Actual diagnostics composition in MQuickJS, moving GC and Nth OOM; deferred."""
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, build, extract, run


class WiFiDiagnosticsGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_diagnostics.c').read_text()
        cls.binaries = []
        for enabled in (0, 1):
            defines = ''.join('#define ' + key + ' ' + str(enabled) + '\n' for key in (
                'CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI', 'CONFIG_ESP_WIFI_FTM_ENABLE',
                'CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT', 'CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT',
                'CONFIG_ESP_WIFI_SOFTAP_SUPPORT', 'CONFIG_SOC_WIFI_HE_SUPPORT', 'CONFIG_IDF_TARGET_ESP32C5',
                'CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT', 'CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API',
                'CONFIG_LWIP_IPV4', 'CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR', 'CONFIG_ESP_WIFI_DPP_SUPPORT'))
            folder = cls.temp.name + '/' + str(enabled)
            Path(folder).mkdir()
            reset_type = re.search(r'typedef struct \{[^}]*\} wifi_diagnostics_reset_t;', source).group(0)
            code = defines + BOUNDARIES + reset_type + '\nstatic wifi_diagnostics_reset_t s_diagnostics_reset;\n'
            code += extract(source, 'wifi_diagnostics_reset_to_js') + extract(source, 'js_wifi_diagnostics_snapshot')
            cls.binaries.append(build(folder, code, MAIN))

    def test_nested_results_survive_collection_and_failure_never_repeats_a_provider(self):
        for enabled, binary in enumerate(self.binaries):
            for mode in (0, 1, 2):
                run([str(binary), str(enabled), str(mode)])


BOUNDARIES = fixture_text('wifi/config/test_wifi_diagnostics_gc/boundaries.inc')
MAIN = fixture_text('wifi/config/test_wifi_diagnostics_gc/main.inc')
