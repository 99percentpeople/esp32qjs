"""Deferred production driver transaction + actual broker/parser/filter tests."""
from tests.support.fixtures import fixture_text
import unittest
import json
import tests.c.integration.wifi.monitor.test_wifi_rx_target as rx_target
import tests.c.integration.wifi.monitor.test_wifi_promiscuous_broker as broker_fixture
from tests.support.native_compile import compile_run


class WiFiPromiscuousDriver(unittest.TestCase):
    def test_driver_snapshot_readback_rollback_suffix_and_stable_callback(self):
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile):
                code = rx_target.WiFiRxTarget().production_code(profile) + broker_fixture.THREADS
                for name in ['wifi_rx_filter', 'wifi_promiscuous_broker']:
                    code += rx_target.unit(rx_target.INTERNAL / ('esp32_mquickjs_' + name + '.h'))
                    code += rx_target.unit(rx_target.COMMON / ('esp32_mquickjs_' + name + '.c'))
                symbols = json.loads((rx_target.ROOT / "docs/idf-wifi-api-inventory.json").read_text())["variants"][profile]["symbols"]
                code += "\n".join("#define " + v["declaration"] for k, v in symbols.items()
                                  if k.split("::")[-1].startswith(("WIFI_PROMIS_FILTER_MASK_", "WIFI_PROMIS_CTRL_FILTER_MASK_")) and v["kind"] == "macro") + "\n"
                code += SDK
                code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_wifi_promiscuous_driver.h')
                code += rx_target.unit(rx_target.COMMON / 'esp32_mquickjs_wifi_promiscuous_driver.c')
                compile_run(self, code + MAIN)


SDK = fixture_text('wifi/monitor/test_wifi_promiscuous_driver/sdk.inc')

MAIN = fixture_text('wifi/monitor/test_wifi_promiscuous_driver/main.inc')
