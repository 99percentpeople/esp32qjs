"""Production callback span adapter/parser using recorded SDK types; run deferred.

Host bitfield construction checks the adapter contract, not real ESP32 RF/ABI.
C5 target object compilation uses actual SDK headers; C3/S3 builds are deferred.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import json
import os
import re
import unittest
from pathlib import Path
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
INTERNAL = ROOT / 'components/esp32_mquickjs/internal'
COMMON = ROOT / 'components/esp32_mquickjs/src/modules/wifi_common'


def unit(path):
    return '\n'.join(line for line in path.read_text().splitlines()
                     if not line.startswith(('#include ', '#pragma once'))) + '\n'


class WiFiRxTarget(unittest.TestCase):
    def production_code(self, profile):
        symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
        he = profile.startswith('esp32c5/')
        types = {key.split('::')[-1]: entry.get('declaration', '') for key, entry in symbols.items()}
        declarations = ''
        if he:
            # The inventory records named fields; restore the pinned SDK's
            # explicit packed attribute rather than assuming host default padding.
            declarations += types['wifi_rx_bb_format_t'] + ';\n'
            declarations += types['esp_wifi_rxctrl_t'].replace(
                '} esp_wifi_rxctrl_t', '} __attribute__((packed)) esp_wifi_rxctrl_t') + ';\n'
        for name in ['wifi_pkt_rx_ctrl_t', 'wifi_promiscuous_pkt_t', 'wifi_promiscuous_pkt_type_t']:
            declarations += types[name] + ';\n'
        return PRELUDE + '#define CONFIG_SOC_WIFI_HE_SUPPORT ' + str(int(he)) + '\n' + declarations + \
            unit(INTERNAL / 'esp32_mquickjs_wifi_rx.h') + unit(INTERNAL / 'esp32_mquickjs_wifi_rx_target.h') + \
            unit(COMMON / 'esp32_mquickjs_wifi_rx.c') + unit(COMMON / 'esp32_mquickjs_wifi_rx_target.c')

    def code(self, profile):
        code = self.production_code(profile)
        if profile.startswith('esp32c5/'):
            sdk = Path(os.environ.get('IDF_PATH', '/home/zach/esp/esp-idf'))
            header = (sdk / 'components/esp_wifi/include/esp_private/esp_wifi_he_types_private.h').read_text()
            code += re.search(r'typedef struct \{[^}]+\} esp_wifi_htsig_t;', header).group(0)
        return code + MAIN

    def test_metadata_only_exact_payload_spans_errors_and_unaligned_headers(self):
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile): compile_run(self, self.code(profile))


PRELUDE = fixture_text('wifi/monitor/test_wifi_rx_target/prelude.inc')

MAIN = fixture_text('wifi/monitor/test_wifi_rx_target/main.inc')
