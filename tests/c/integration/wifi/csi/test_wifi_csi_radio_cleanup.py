"""Deferred CSI production stop/close suffix tests with retained Radio failures."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import unittest
from pathlib import Path
from tests.support.native_compile import compile_run
from tests.support.c_source import extract as function

ROOT = TEST_ROOT


class WiFiCsiRadioCleanup(unittest.TestCase):
    def test_failed_promiscuous_release_preserves_channel_queue_control_and_retry(self):
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_csi/esp32_mquickjs_wifi_csi.c').read_text()
        code = PRELUDE + '\n'.join(function(source, name) for name in [
            'wifi_csi_radio_cleanup_failed', 'wifi_csi_finish_stop', 'wifi_csi_finish_close', 'wifi_csi_begin_stop'])
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/csi/test_wifi_csi_radio_cleanup/prelude.inc')

MAIN = fixture_text('wifi/csi/test_wifi_csi_radio_cleanup/main.inc')
