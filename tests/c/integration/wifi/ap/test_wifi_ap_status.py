"""Production AP sampling and rooted JS status conversion; phase execution deferred."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, RADIO, HEADER, sdk_types, structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run

AP = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_ap.c'
CONFIG = AP.with_name('esp32_mquickjs_wifi_config.c')


class WiFiAPStatusNative(unittest.TestCase):
    def code(self):
        radio, header = RADIO.read_text(), HEADER.read_text()
        client = re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', header).group(0)
        types = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
        types += 'typedef unsigned esp32_mquickjs_wifi_promiscuous_token_t;\n'
        types += client + structure(header, 'esp32_mquickjs_wifi_radio_lease_t')
        types += structure(radio, 'wifi_radio_live_lease_t')
        types += structure(header, 'esp32_mquickjs_wifi_ap_snapshot_t')
        zero = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        return PRELUDE + sdk_types('esp32c5/representative') + types + NATIVE_BOUNDARIES + zero + ''.join(
            extract(radio, name) for name in ['wifi_radio_lease_valid', 'esp32_mquickjs_wifi_radio_sample_ap'])

    def test_exact_admission_sdk_failures_secret_wipe_and_current_channel(self):
        compile_run(self, self.code() + fixture_text('wifi/ap/test_wifi_ap_status/test_exact_admission_sdk_failures_secret_wipe_and_current_channel.inc'))


NATIVE_BOUNDARIES = fixture_text('wifi/ap/test_wifi_ap_status/native_boundaries.inc')


class WiFiAPStatusVM(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(); cls.addClassCleanup(cls.temp.cleanup)
        header = HEADER.read_text()
        body = VM_BOUNDARIES + structure(header, 'esp32_mquickjs_wifi_ap_snapshot_t') + VM_SAMPLE
        body += extract(CONFIG.read_text(), 'esp32_mquickjs_wifi_ssid_text')
        body += ''.join(extract(AP.read_text(), name) for name in ['wifi_ap_auth_name', 'esp32_mquickjs_wifi_ap_status'])
        cls.binary = build(cls.temp.name, body, VM_MAIN)
        cls.text_binary = build(cls.temp.name + '/text', '#include "cutils.h"\n' +
                                extract(CONFIG.read_text(), 'esp32_mquickjs_wifi_ssid_text'), TEXT_MAIN)

    def test_binary_ssid_text_checks_exact_span_before_vm_string_construction(self):
        run([str(self.text_binary)])

    def test_absent_running_failed_closing_gc_and_allocation_failures(self):
        for scenario in range(5): run([str(self.binary), str(scenario)])


VM_BOUNDARIES = fixture_text('wifi/ap/test_wifi_ap_status/vm_boundaries.inc')
VM_SAMPLE = fixture_text('wifi/ap/test_wifi_ap_status/vm_sample.inc')
VM_MAIN = fixture_text('wifi/ap/test_wifi_ap_status/vm_main.inc')

TEXT_MAIN = fixture_text('wifi/ap/test_wifi_ap_status/text_main.inc')
