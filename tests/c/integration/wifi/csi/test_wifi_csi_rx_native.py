"""Deferred production CSI native bridge: task lanes and synchronous identity."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = TEST_ROOT
BASE = ROOT / 'components/esp32_mquickjs'


class WiFiCsiNativeReceipt(unittest.TestCase):
    def test_exact_callback_identity_consumption_nesting_and_bounded_lanes(self):
        for metadata, prefix, c5 in ((48, 44, 0), (64, 56, 1)):
            with self.subTest(metadata=metadata), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / 'freertos').mkdir()
                (root / 'sdkconfig.h').write_text('#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI 1\n'
                    f'#define CONFIG_IDF_TARGET_ESP32C5 {c5}\n#define METADATA {metadata}\n#define PREFIX {prefix}\n')
                (root / 'esp_wifi.h').write_text('#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n'
                    f'typedef struct {{uint8_t bytes[{metadata}];}} wifi_pkt_rx_ctrl_t;\n'
                    'typedef struct {const void *buf,*hdr;uint32_t len,payload_len;} wifi_csi_info_t;\n')
                (root / 'esp_attr.h').write_text('#define IRAM_ATTR\n')
                (root / 'freertos/FreeRTOS.h').write_text(fixture_text('wifi/csi/test_wifi_csi_rx_native/test_exact_callback_identity_consumption_nesting_and_bounded_lanes.inc'))
                (root / 'freertos/task.h').write_text(fixture_text('wifi/csi/test_wifi_csi_rx_native/test_exact_callback_identity_consumption_nesting_and_bounded_lanes-02.inc'))
                source = root / 'native.c'
                source.write_text(CODE)
                binary = root / 'native'
                result = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                    '-I' + str(root), '-I' + str(BASE / 'internal'), str(source),
                    str(BASE / 'src/modules/wifi_csi/esp32_mquickjs_wifi_csi_rx_native.c'),
                    str(BASE / 'src/modules/wifi_csi/esp32_mquickjs_wifi_csi_rx_span.c'), '-o', str(binary)],
                    capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


CODE = fixture_text('wifi/csi/test_wifi_csi_rx_native/code.inc')
