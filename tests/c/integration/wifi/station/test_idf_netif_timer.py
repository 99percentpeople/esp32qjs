"""Production SDK timer/stop/destroy regression; run in the Wi-Fi phase suite."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
import pathlib
import sys
import unittest

from tests.support.native_compile import compile_run
from tests.support.c_source import extract as function

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_netif_timer import patch_source


class IDFNetifTimer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed SDK for production timer regression')
        cls.source = (pathlib.Path(sdk) / 'components/esp_netif/lwip/esp_netif_lwip.c').read_bytes()
        cls.patched = patch_source(cls.source)

    def test_source_drift_and_double_patch_are_rejected(self):
        for content in [self.source + b'\n', self.patched]:
            with self.subTest(length=len(content)), self.assertRaises(ValueError):
                patch_source(content)

    def test_original_address_reuse_and_patched_stop_destroy(self):
        for patched, content in [(False, self.source), (True, self.patched)]:
            for enabled in [False, True]:
                with self.subTest(patched=patched, timer_enabled=enabled):
                    source = content.decode()
                    body = 'static void esp_netif_ip_lost_timer(void *arg);\n'
                    if patched:
                        body += function(source, 'esp32qjs_netif_cancel_ip_lost_timer')
                    for name in ['esp_netif_destroy_api', 'esp_netif_stop_api',
                                 'esp_netif_ip_lost_timer', 'esp_netif_start_ip_lost_timer']:
                        body += function(source, name)
                    compile_run(self, f'#define PATCHED {int(patched)}\n'
                                f'#define CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE {int(enabled)}\n'
                                + BOUNDARIES + body + MAIN)


BOUNDARIES = fixture_text('wifi/station/test_idf_netif_timer/boundaries.inc')

MAIN = fixture_text('wifi/station/test_idf_netif_timer/main.inc')
