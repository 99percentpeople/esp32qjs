"""Deferred production Radio admission/retirement plus the actual Raw TX broker.

Uses pinned SDK types, injected SDK calls and pthread scheduling. The Radio
registry, lease checks, channel observation and send/retire/release are production
functions. AP startup and physical shutdown are outside this fixture's scope.
"""
from tests.support.fixtures import fixture_text
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.c.integration.wifi.tx.test_wifi_raw_tx_broker import production_code
from tests.c.integration.wifi.config.test_wifi_config_controls import ROOT, RADIO, HEADER, structure
from tests.support.wireless_vm_fixture import extract


def radio_code(profile, identity=False):
    code = production_code(profile, identity=identity)
    code = code.replace('static esp_err_t esp_wifi_80211_tx(',
                        'static bool expected_sequence=true;\nstatic void radio_send_hook(void);\nstatic esp_err_t esp_wifi_80211_tx(')
    code = code.replace('++sends;driver_bytes=bytes;', '++sends;driver_bytes=bytes;radio_send_hook();')
    code = code.replace('length==24 && sequence', 'length==24 && sequence==expected_sequence')
    # The baseline broker fixture asserts STA; this integration also sends AP.
    code = code.replace('interface==WIFI_IF_STA && length==24',
                        '(interface==WIFI_IF_STA || interface==WIFI_IF_AP) && length==24')
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    declarations = {}
    for key, entry in symbols.items():
        value = entry.get('declaration', '')
        if value.startswith('typedef '):
            name = key.split('::')[-1]
            if name not in declarations or '{' in value:
                declarations[name] = value
    seen = {'wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t'}
    extra = []

    def visit(name):
        if name in seen:
            return
        seen.add(name)
        for dependency in sorted(set(re.findall(r'\b\w+_t\b', declarations[name])) - {name}):
            if dependency in declarations:
                visit(dependency)
        extra.append(declarations[name] + ';\n')

    for name in ['wifi_mode_t', 'wifi_second_chan_t', 'wifi_ap_record_t', 'wifi_sta_list_t']:
        visit(name)
    code += ''.join(extra)
    header, radio = HEADER.read_text(), RADIO.read_text()
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', header).group(0)
    broker_header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_promiscuous_broker.h').read_text()
    code += structure(broker_header, 'esp32_mquickjs_wifi_promiscuous_token_t')
    code += structure(radio, 'wifi_radio_live_lease_t')
    for name in ['esp32_mquickjs_wifi_radio_lease_t', 'esp32_mquickjs_wifi_radio_promiscuous_lease_t']:
        code += structure(header, name)
    code += fixture_text('wifi/tx/test_wifi_raw_tx_radio/radio_code.inc')
    interval = (ROOT / "components/esp32_mquickjs/internal/esp32_mquickjs_wifi_interval.h").read_text()
    for name in ["esp32_mquickjs_wifi_interval_token_t", "esp32_mquickjs_wifi_interval_state_t"]:
        code += structure(interval, name)
    code += BOUNDARIES
    for name in ['wifi_radio_lease_valid', 'wifi_radio_acquire_locked', 'wifi_radio_promiscuous_owner',
                 'wifi_radio_refresh_channel', 'wifi_radio_get_channel_locked', 'wifi_radio_release_channel_locked',
                 'wifi_radio_release_locked', 'esp32_mquickjs_wifi_radio_release',
                 'wifi_radio_raw_tx_unpin',
                 'esp32_mquickjs_wifi_radio_raw_tx_submit', 'esp32_mquickjs_wifi_radio_raw_tx_retire']:
        code += extract(radio, name)
    return code + (MAIN[:MAIN.index('int main(void)')] + fixture_text('wifi/tx/test_wifi_raw_tx_radio/window.inc') if identity else MAIN)


class WiFiRawTxRadio(unittest.TestCase):
    def test_exact_lease_channel_pinning_association_and_concurrent_release(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                source.write_text(radio_code(profile))
                built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                        str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_window_holds_channel_and_owner_until_last_completion(self):
        from tests.support.native_compile import compile_run
        compile_run(self, radio_code('esp32c5/representative', identity=True))


BOUNDARIES = fixture_text('wifi/tx/test_wifi_raw_tx_radio/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_radio/main.inc')
