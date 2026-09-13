"""Deferred shared production VHT decoding, CSI and Monitor wire consumers.

SDK bitfield construction supplies independent inputs. No test state machine,
RF claims or fixture execution before the Wi-Fi staged validation.
"""
from tests.support.fixtures import fixture_text
import os
import re
import unittest
from pathlib import Path
from tests.c.integration.wifi.monitor.test_wifi_monitor_wire import PRELUDE, production_monitor_wire
from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiVhtSignal(unittest.TestCase):
    def test_sdk_layout_su_mu_reserved_mcs_and_nominal_width(self):
        sdk = Path(os.environ.get('IDF_PATH', '/home/zach/esp/esp-idf'))
        private = (sdk / 'components/esp_wifi/include/esp_private/esp_wifi_he_types_private.h').read_text()
        declaration = re.search(r'typedef struct \{[^}]+\} __attribute__\(\(packed\)\) esp_wifi_vht_siga1_t;', private).group(0)
        resources = (INTERNAL / 'esp32_mquickjs_wifi_csi_resources.h').read_text()
        metadata = re.search(r'typedef struct \{[^}]+\} esp32_mquickjs_wifi_csi_metadata_t;', resources).group(0)
        csi = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_csi/esp32_mquickjs_wifi_csi_target_config.c').read_text()
        body = PRELUDE + production_monitor_wire(he=True) + declaration + metadata
        body += extract(csi, 'esp32_mquickjs_wifi_csi_target_decode_he_signal')
        compile_run(self, body + MAIN)


MAIN = fixture_text('wifi/config/test_wifi_vht_signal/main.inc')
