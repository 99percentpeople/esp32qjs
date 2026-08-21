import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
DISPLAY_DIR = ROOT / "shared" / "flash_data" / "_sys" / "display"


class DisplayArchitectureTests(SourceContractTestCase):
    def test_panel_drivers_do_not_own_rendering_or_native_buses(self):
        forbidden = (
            "display.Surface.call",
            "bitmap.create",
            ".prototype.drawText",
            ".prototype.fillRect",
            "spi.openBus",
            "i2c.open",
            "gpio.pinMode",
            "gpio.digitalWrite",
        )
        for name in ("ssd1306.js", "st7789.js"):
            source = (DISPLAY_DIR / "drivers" / name).read_text(encoding="utf-8")
            for token in forbidden:
                self.assertNotIn(token, source, f"{name} must not contain {token}")
            self.assertIn("display.drivers.register", source)
            self.assertIn("prototype.present", source)

    def test_wlk_module_is_a_profile_not_a_driver_alias(self):
        source = (DISPLAY_DIR / "profiles" / "wlk1501spi8p.js").read_text(
            encoding="utf-8"
        )
        self.assertIn('display.profiles.register("wlk1501spi8p"', source)
        self.assertNotIn('display.drivers.register("wlk1501spi8p"', source)

    def test_display_entry_point_is_core_only(self):
        source = (
            ROOT / "shared" / "flash_data" / "_sys" / "display.js"
        ).read_text(encoding="utf-8")
        self.assertIn('load("_sys/display/core/namespace.js")', source)
        self.assertIn('load("_sys/display/core/display.js")', source)
        self.assertNotIn("drivers/", source)
        self.assertNotIn("transports/", source)

    def test_driver_specific_entry_points_are_available(self):
        expected = (
            "ssd1306.js",
            "st7789.js",
            "wlk1501spi8p.js",
            "all.js",
        )
        for name in expected:
            self.assertTrue((DISPLAY_DIR / name).is_file(), name)


if __name__ == "__main__":
    unittest.main()
