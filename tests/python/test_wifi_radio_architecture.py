import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class WifiRadioArchitectureTests(unittest.TestCase):
    def test_shared_nvs_boot_helper_never_erases_application_state(self):
        header = (
            MQUICKJS / "internal/esp32_mquickjs_nvs_flash_boot.h"
        ).read_text(encoding="utf-8")
        source = (
            MQUICKJS
            / "src/modules/nvs/esp32_mquickjs_nvs_flash_boot.c"
        ).read_text(encoding="utf-8")
        wifi = (
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c"
        ).read_text(encoding="utf-8")
        public_nvs = (
            MQUICKJS / "src/modules/nvs/esp32_mquickjs_nvs.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_nvs_flash_ensure_initialized", header)
        self.assertIn("nvs_flash_init()", source)
        self.assertNotIn("nvs_flash_erase", source)
        self.assertNotIn("nvs_flash_erase", wifi)
        self.assertIn("esp32_mquickjs_nvs_flash_ensure_initialized", public_nvs)

    def test_wifi_driver_is_owned_by_the_boot_scoped_radio_service(self):
        header = (
            MQUICKJS / "internal/esp32_mquickjs_wifi_radio.h"
        ).read_text(encoding="utf-8")
        radio = (
            MQUICKJS
            / "src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c"
        ).read_text(encoding="utf-8")
        wifi = (
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c"
        ).read_text(encoding="utf-8")

        for token in (
            "esp32_mquickjs_wifi_radio_acquire",
            "esp32_mquickjs_wifi_radio_ensure_started",
            "esp32_mquickjs_wifi_radio_get_channel",
            "esp32_mquickjs_wifi_radio_set_channel",
            "esp32_mquickjs_wifi_radio_release",
            "esp32_mquickjs_wifi_radio_channel_key",
        ):
            self.assertIn(token, header)
        self.assertIn("esp_wifi_init(&config)", radio)
        self.assertIn("esp_wifi_start()", radio)
        self.assertIn("channel_generation", radio)
        self.assertNotIn("esp_wifi_init(", wifi)
        self.assertNotIn("err = esp_wifi_start();", wifi)
        self.assertIn("ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA", wifi)

    def test_internal_feature_is_a_transitive_wifi_dependency(self):
        catalog = json.loads(
            (MQUICKJS / "runtime-features.json").read_text(encoding="utf-8")
        )
        features = {entry["id"]: entry for entry in catalog["features"]}
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")

        self.assertEqual(features["wifi_radio"]["public"], False)
        self.assertEqual(features["wifi_radio"]["category"], "internal")
        self.assertEqual(features["wifi"]["requires"], ["net", "wifi_radio"])
        self.assertIn("config ESP32_MQUICKJS_WIFI_RADIO", kconfig)
        self.assertIn("select ESP32_MQUICKJS_WIFI_RADIO", kconfig)


if __name__ == "__main__":
    unittest.main()
