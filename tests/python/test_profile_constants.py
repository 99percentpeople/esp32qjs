import json
import re
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from profile_constants import (  # noqa: E402
    legacy_wiring_constants,
    load_catalog,
    render_c_include,
    validate_constants,
)


class ProfileConstantsTests(unittest.TestCase):
    def test_catalog_has_unique_registered_keys(self):
        catalog = load_catalog()
        keys = [entry["key"] for entry in catalog["definitions"]]

        self.assertEqual(len(keys), len(set(keys)))
        self.assertTrue(all(key.startswith("ESP32QJS_") for key in keys))

    def test_registered_and_custom_constants_keep_their_types(self):
        constants, warnings = validate_constants(
            {
                "ESP32QJS_LED_PIN": 8,
                "ESP32QJS_LED_ACTIVE_LOW": True,
                "APP_DISPLAY_DRIVER": "st7789",
            },
            "esp32c3",
        )

        self.assertEqual(constants["ESP32QJS_LED_PIN"], 8)
        self.assertIs(constants["ESP32QJS_LED_ACTIVE_LOW"], True)
        self.assertEqual(constants["APP_DISPLAY_DRIVER"], "st7789")
        self.assertEqual(warnings, ["ESP32QJS_LED_PIN uses boot or USB-related GPIO 8"])

    def test_reserved_unknown_key_and_reserved_gpio_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "unknown reserved"):
            validate_constants({"ESP32QJS_PRIVATE": 1}, "esp32c3")
        with self.assertRaisesRegex(ValueError, "reserved for flash or PSRAM"):
            validate_constants({"ESP32QJS_LED_PIN": 12}, "esp32c3")

    def test_legacy_wiring_is_migrated_without_disabled_defaults(self):
        constants = legacy_wiring_constants({
            "led": {"pin": -1, "activeLow": True},
            "i2c": {"sda": 4, "scl": 5},
            "spi": {"miso": -1},
        })

        self.assertNotIn("ESP32QJS_LED_PIN", constants)
        self.assertEqual(constants["ESP32QJS_LED_ACTIVE_LOW"], True)
        self.assertEqual(constants["ESP32QJS_I2C_SDA"], 4)
        self.assertEqual(constants["ESP32QJS_SPI_MISO"], -1)

    def test_generated_include_is_stable_and_c_escaped(self):
        output = render_c_include({
            "APP_LABEL": 'display "左"\n',
            "ESP32QJS_LED_ACTIVE_LOW": False,
            "ESP32QJS_LED_PIN": 7,
        })

        self.assertEqual(
            output,
            "/* Generated hardware-profile constants; do not edit. */\n"
            'ESP32_MQUICKJS_PROFILE_STRING("APP_LABEL", "display \\"\\345\\267\\246\\"\\012"),\n'
            'ESP32_MQUICKJS_PROFILE_BOOL("ESP32QJS_LED_ACTIVE_LOW", false),\n'
            'ESP32_MQUICKJS_PROFILE_INT("ESP32QJS_LED_PIN", 7),\n',
        )

    def test_catalog_json_stays_within_server_limits(self):
        raw = (ROOT / "configs" / "profile-constants.json").read_text(encoding="utf-8")
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

    def test_camera_managed_component_is_feature_gated(self):
        manifest = (
            ROOT / "components" / "esp32_mquickjs" / "idf_component.yml"
        ).read_text(encoding="utf-8")

        self.assertIn("espressif/esp32-camera:", manifest)
        self.assertIn(
            '$CONFIG{ESP32_MQUICKJS_FEATURE_CAMERA} == True',
            manifest,
        )


if __name__ == "__main__":
    unittest.main()
