from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class WirelessCoexistenceArchitectureTests(unittest.TestCase):
    def test_coexistence_is_enabled_only_for_ble_plus_wifi_radio(self):
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")

        self.assertIn("config ESP32_MQUICKJS_WIRELESS_COEX", kconfig)
        self.assertIn(
            "default y if ESP32_MQUICKJS_FEATURE_BLE && ESP32_MQUICKJS_WIFI_RADIO",
            kconfig,
        )
        self.assertIn("select ESP_COEX_SW_COEXIST_ENABLE", kconfig)

    def test_ble_teardown_precedes_espnow_and_wifi(self):
        source = (
            MQUICKJS / "src/core/esp32_mquickjs.c"
        ).read_text(encoding="utf-8")

        ble = source.index("esp32_mquickjs_deinit_ble_runtime")
        espnow = source.index("esp32_mquickjs_deinit_espnow_runtime")
        wifi = source.index("esp32_mquickjs_deinit_wifi_runtime")
        self.assertLess(ble, espnow)
        self.assertLess(espnow, wifi)


if __name__ == "__main__":
    unittest.main()
