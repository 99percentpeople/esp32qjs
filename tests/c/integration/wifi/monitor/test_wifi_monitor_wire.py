"""Deferred production Monitor snapshot/PHY/wire tests using recorded SDK enums."""
from tests.support.fixtures import fixture_text
import json
import re
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL, COMMON, unit
from tests.support.native_compile import compile_run

MONITOR = ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor'


def production_monitor_wire(he=False):
    body = '#define CONFIG_ESP32_MQUICKJS_WIFI_RADIO 1\n'
    body += '#define CONFIG_SOC_WIFI_HE_SUPPORT ' + str(int(he)) + '\n'
    body += 'typedef int wifi_promiscuous_pkt_type_t;\n'
    if he:
        symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants']['esp32c5/representative']['symbols']
        body += next(v['declaration'] for k, v in symbols.items() if k.endswith('::wifi_rx_bb_format_t')) + ';\n'
    for name in ['esp32_mquickjs_wifi_rx.h', 'esp32_mquickjs_wifi_rx_target.h',
                 'esp32_mquickjs_wifi_rx_wire.h', 'esp32_mquickjs_wifi_csi_layout.h',
                 'esp32_mquickjs_wifi_rx_wire_metadata.h']:
        body += unit(INTERNAL / name)
    resources = (INTERNAL / 'esp32_mquickjs_wifi_monitor_resources.h').read_text()
    body += re.search(r'typedef struct \{\n    esp32_mquickjs_wifi_rx_driver_metadata_t driver;.*?\} esp32_mquickjs_wifi_monitor_info_t;',
                      resources, re.S).group(0)
    body += unit(INTERNAL / 'esp32_mquickjs_wifi_rx_vht_signal.h')
    body += unit(INTERNAL / 'esp32_mquickjs_wifi_rx_he_signal.h')
    body += unit(INTERNAL / 'esp32_mquickjs_wifi_monitor_wire.h')
    for name in ['esp32_mquickjs_wifi_rx.c', 'esp32_mquickjs_wifi_rx_wire.c', 'esp32_mquickjs_wifi_rx_wire_metadata.c']:
        body += unit(COMMON / name)
    return body + unit(MONITOR / 'esp32_mquickjs_wifi_monitor_wire.c')


class WiFiMonitorWire(unittest.TestCase):
    def test_legacy_and_he_snapshot_facts_lengths_phy_and_atomic_rejection(self):
        for he in [False, True]:
            with self.subTest(he=he):
                compile_run(self, PRELUDE + production_monitor_wire(he) + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
'''
MAIN = fixture_text('wifi/monitor/test_wifi_monitor_wire/main.inc')
