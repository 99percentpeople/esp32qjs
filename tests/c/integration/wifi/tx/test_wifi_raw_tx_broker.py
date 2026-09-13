"""Deferred tests of the production TX broker, validator and callback snapshot.

Only SDK, clock, allocator and task locks are substituted. No fixture state
machine stands in for the broker. Run with the Wi-Fi stage, not during API work.
"""
from tests.support.fixtures import fixture_text
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL, COMMON, unit

RAW = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx'


def production_code(profile, identity=False):
    inventory = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants']
    symbols = {key.split('::')[-1]: value['declaration']
               for key, value in inventory[profile]['symbols'].items()}
    code = PRELUDE
    if identity: code += "#define ESP32_MQUICKJS_RAW_TX_DESCRIPTOR_IDENTITY 1\n"
    for name in ['wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t',
                 'wifi_tx_info_t', 'esp_80211_tx_info_t']:
        code += symbols[name] + ';\n'
    for name in ['wifi_rx', 'wifi_raw_tx_validate', 'wifi_raw_tx_snapshot', 'wifi_raw_tx_broker']:
        code += unit(INTERNAL / ('esp32_mquickjs_' + name + '.h'))
    boundaries = BOUNDARIES
    if identity:
        boundaries = boundaries[:boundaries.index('static esp_err_t esp_wifi_80211_tx(')]
        boundaries += fixture_text('wifi/tx/test_wifi_raw_tx_broker/identity.inc')
    code += boundaries
    code += unit(COMMON / 'esp32_mquickjs_wifi_rx.c')
    for name in ['validate', 'snapshot', 'broker']:
        if name == 'broker': code += '#define esp32_mquickjs_memory_payload_free heap_caps_free\n'
        code += unit(RAW / ('esp32_mquickjs_wifi_raw_tx_' + name + '.c'))
        if name == 'broker': code += '#undef esp32_mquickjs_memory_payload_free\n'
    return code


class WiFiRawTxBroker(unittest.TestCase):
    def test_native_completion_storage_quarantine_and_registration_fence(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                source.write_text(production_code(profile) + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                        str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_descriptor_identity_out_of_order_reuse_and_all_packet_deinit(self):
        from tests.support.native_compile import compile_run
        compile_run(self, production_code('esp32c5/representative', identity=True) +
                    fixture_text('wifi/tx/test_wifi_raw_tx_broker/window.inc'))


PRELUDE = fixture_text('wifi/tx/test_wifi_raw_tx_broker/prelude.inc')

BOUNDARIES = fixture_text('wifi/tx/test_wifi_raw_tx_broker/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_broker/main.inc')
