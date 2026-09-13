"""Deferred exact production Radio rate borrow/stop/restore/release tests.

Injected SDK start/stop, event wait, channel read and locks; no replacement rate,
lease, stop or rollback state machine. Physical callbacks/RF remain separate.
"""
from tests.support.fixtures import fixture_text
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.c.integration.wifi.tx.test_wifi_tx_rate import COMPONENT, ROOT, rate_code
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.wireless_vm_fixture import extract
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit


def radio_rate_code(profile):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    raw = (COMPONENT / 'internal/esp32_mquickjs_wifi_raw_tx_validate.h').read_text()
    broker = (COMPONENT / 'internal/esp32_mquickjs_wifi_promiscuous_broker.h').read_text()
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    declarations = {key.split('::')[-1]: value['declaration'] for key, value in symbols.items()}
    code = rate_code(profile)
    for name in ('wifi_mode_t', 'wifi_second_chan_t'):
        code += declarations[name] + ';\n'
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_raw_tx_interface_t;', raw).group(0)
    for name in ('esp32_mquickjs_wifi_radio_client_t', 'esp32_mquickjs_wifi_radio_driver_state_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += structure(broker, 'esp32_mquickjs_wifi_promiscuous_token_t')
    code += structure(radio, 'wifi_radio_live_lease_t')
    for name in ('esp32_mquickjs_wifi_radio_lease_t', 'esp32_mquickjs_wifi_radio_promiscuous_lease_t'):
        code += structure(header, name)
    code += structure(header, 'esp32_mquickjs_wifi_radio_lifecycle_t')
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_action_lane.h')
    code += re.search(r'static struct \{\n    esp32_mquickjs_wifi_action_lane_t[^}]*\} s_action = [^;]*;', radio).group(0)
    code += fixture_text('wifi/tx/test_wifi_raw_tx_rate_lease/radio_rate_code.inc')
    code += ACTION_BOUNDARIES + BOUNDARIES
    for name in ('wifi_radio_lease_valid', 'wifi_radio_acquire_locked', 'wifi_radio_promiscuous_owner',
                 'wifi_radio_record_fault', 'wifi_radio_cleanup_fault', 'wifi_radio_release_locked',
                 'wifi_radio_stop_owners_locked', 'wifi_radio_stop_lease_locked', 'wifi_radio_stop_locked', 'wifi_radio_write_tx_rate',
                 'wifi_radio_restore_tx_rate_locked', 'esp32_mquickjs_wifi_radio_release_and_stop_idle',
                 'esp32_mquickjs_wifi_radio_raw_tx_acquire', 'esp32_mquickjs_wifi_radio_tx_rate_status'):
        code += extract(radio, name)
    return code


class WiFiRawTxRateLease(unittest.TestCase):
    def test_no_mutation_admission_known_restore_exact_owner_and_cleanup_suffixes(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                source.write_text(radio_rate_code(profile) + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                        str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


BOUNDARIES = fixture_text('wifi/tx/test_wifi_raw_tx_rate_lease/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_rate_lease/main.inc')

# This fixture exercises only the temporary-rate exception. The Action recovery
# exception is covered with the real registry/ledger in test_wifi_action_recovery.
ACTION_BOUNDARIES = fixture_text('wifi/tx/test_wifi_raw_tx_rate_lease/action_boundaries.inc')
