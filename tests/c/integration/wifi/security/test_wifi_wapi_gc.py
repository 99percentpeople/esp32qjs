"""Deferred real moving-GC and nth-allocation failure WAPI status conversion."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from pathlib import Path
from tests.support.wireless_vm_fixture import build, extract, run

BASE = TEST_ROOT / 'components/esp32_mquickjs'


class WiFiWapiGC(unittest.TestCase):
    def test_status_preserves_unknown_native_state_and_roots_every_result(self):
        header = (BASE / 'internal/esp32_mquickjs_wifi_wapi.h').read_text()
        status = re.search(r'typedef struct \{[^}]+\} esp32_mquickjs_wifi_wapi_status_t;', header).group(0)
        public = (BASE / 'src/modules/wifi/esp32_mquickjs_wifi_wapi_public.inc').read_text()
        code = 'typedef int esp_err_t;\n' + status + fixture_text('wifi/security/test_wifi_wapi_gc/test_status_preserves_unknown_native_state_and_roots_every_result-code.inc')
        code += re.search(r'^#define WAPI_SET[^\n]*', public, re.M).group(0) + '\n'
        code += extract(public, 'wifi_wapi_status')
        with tempfile.TemporaryDirectory() as directory:
            binary = build(directory, code, MAIN)
            run([str(binary)])


MAIN = fixture_text('wifi/security/test_wifi_wapi_gc/main.inc')
