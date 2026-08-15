import json
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


if __name__ == "__main__":
    unittest.main()
