"""Deferred SmartConfig worker coordinator checks against full production code.

SDK IPC/driver and the separately tested timer/event/ACK boundaries are injected.
Host execution does not qualify native SDK scheduling or RF retirement.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT


class SmartConfigDecoder(unittest.TestCase):
    def test_production_decoder(self):
        paths = [
            'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_smartconfig_decoder.h',
            'components/esp32_mquickjs/src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_decoder.c',
        ]
        production = ''.join(re.sub(r'^#(?:include|pragma)[^\n]*\n', '', (ROOT / p).read_text(), flags=re.M) for p in paths)
        # Host pointer width is unrelated to the separately target-compiled ABI.
        production = production.replace('_Static_assert(sizeof(sc_ipc_config_t) == 12, "review SmartConfig native IPC ABI");', '')
        events = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_smartconfig_events.h').read_text()
        bundle = re.search(r'typedef struct \{[^}]*\} esp32_mquickjs_wifi_smartconfig_credentials_t;', events).group(0)
        status = re.search(r'typedef struct \{[^}]*\} esp32_mquickjs_wifi_smartconfig_events_status_t;', events).group(0)
        boundaries = BOUNDARIES.replace('static int locked,', '#define ESP32_MQUICKJS_SMARTCONFIG_CUSTOM_DATA_MAX 64\n' + bundle + '\n' + status + '\nstatic int locked,', 1)
        compile_run(self, boundaries + production + NATIVE + CASES)


BOUNDARIES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_decoder/boundaries.inc')

NATIVE = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_decoder/native.inc')

CASES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_decoder/cases.inc')
