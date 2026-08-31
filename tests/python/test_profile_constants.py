import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "configs" / "profile-constants.json"


class ProfileConstantsTests(unittest.TestCase):
    def test_catalog_has_unique_registered_keys(self):
        catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
        keys = [entry["key"] for entry in catalog["definitions"]]

        self.assertEqual(len(keys), len(set(keys)))
        self.assertTrue(all(key.startswith("ESP32QJS_") for key in keys))

    def test_catalog_json_stays_within_server_limits(self):
        raw = CATALOG.read_text(encoding="utf-8")
        self.assertLess(len(raw.encode("utf-8")), 16_384)
        self.assertEqual(json.loads(raw)["version"], 1)

    def test_repository_does_not_store_board_pin_overlays(self):
        hardware_config = ROOT / "configs" / "hardware"
        self.assertFalse(hardware_config.exists())

    def test_mcu_defaults_only_disable_unsupported_modules(self):
        feature_lines = {}
        for mcu in ("esp32c3", "esp32c5", "esp32s3"):
            defaults = (
                ROOT / "configs" / "mcus" / mcu / "sdkconfig.defaults"
            ).read_text(encoding="utf-8")
            feature_lines[mcu] = {
                line for line in defaults.splitlines()
                if line.startswith("CONFIG_ESP32_MQUICKJS_FEATURE_")
            }
            self.assertNotIn("_PIN=", defaults)

        self.assertEqual(feature_lines["esp32c3"], {
            "CONFIG_ESP32_MQUICKJS_FEATURE_DAC=n",
            "CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA=n",
        })
        self.assertEqual(feature_lines["esp32c5"], {
            "CONFIG_ESP32_MQUICKJS_FEATURE_DAC=n",
            "CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA=n",
        })
        self.assertEqual(feature_lines["esp32s3"], {
            "CONFIG_ESP32_MQUICKJS_FEATURE_DAC=n",
        })

        self.assertFalse((ROOT / "apps").exists())
        self.assertFalse((ROOT / "shared" / "flash_data").exists())

    def test_optional_modules_require_explicit_project_selection(self):
        kconfig = (
            ROOT / "components" / "esp32_mquickjs" / "Kconfig.projbuild"
        ).read_text(encoding="utf-8")
        feature_sections = re.findall(
            r"config (ESP32_MQUICKJS_FEATURE_[A-Z0-9_]+)\n(.*?)(?=\nconfig |\nmenu |\nendmenu|\Z)",
            kconfig,
            flags=re.DOTALL,
        )

        self.assertGreaterEqual(len(feature_sections), 18)
        for symbol, section in feature_sections:
            self.assertRegex(section, r"(?m)^\s+default n\s*$", symbol)

    def test_camera_managed_component_is_available_before_kconfig_resolution(self):
        manifest = (
            ROOT / "components" / "esp32_mquickjs" / "idf_component.yml"
        ).read_text(encoding="utf-8")

        self.assertIn("espressif/esp32-camera:", manifest)
        self.assertIn('if: "target == esp32s3"', manifest)
        self.assertNotIn("$CONFIG{ESP32_MQUICKJS_FEATURE_CAMERA}", manifest)


if __name__ == "__main__":
    unittest.main()
