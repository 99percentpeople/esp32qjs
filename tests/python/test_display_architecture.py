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
            "i2c.openBus",
            "gpio.pinMode",
            "gpio.digitalWrite",
        )
        for name in ("ssd1306.js", "st7789.js"):
            source = (DISPLAY_DIR / "drivers" / name).read_text(encoding="utf-8")
            for token in forbidden:
                self.assertNotIn(token, source, f"{name} must not contain {token}")
            self.assertIn("display.drivers.register", source)
            self.assertIn("prototype.present", source)

    def test_shared_display_tree_contains_no_board_profiles(self):
        self.assertFalse((DISPLAY_DIR / "profiles").exists())
        for name in ("wlk1501spi8p.js", "m5sticks3.js"):
            self.assertFalse((DISPLAY_DIR / name).exists(), name)
        all_js = (DISPLAY_DIR / "all.js").read_text(encoding="utf-8")
        self.assertIn('load("_sys/display/ssd1306.js")', all_js)
        self.assertIn('load("_sys/display/st7789.js")', all_js)
        self.assertNotIn("wlk1501spi8p", all_js)
        self.assertNotIn("m5sticks3", all_js)

    def test_demo_app_overlays_its_own_board_profile(self):
        profile = (
            ROOT / "apps" / "demo" / "flash_data" / "_sys" / "display"
            / "profiles" / "wlk1501spi8p.js"
        )
        source = profile.read_text(encoding="utf-8")
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
            "all.js",
        )
        for name in expected:
            self.assertTrue((DISPLAY_DIR / name).is_file(), name)

    def test_ssd1306_keeps_frame_bytes_native_through_i2c(self):
        driver = (DISPLAY_DIR / "drivers" / "ssd1306.js").read_text(
            encoding="utf-8"
        )
        transport = (DISPLAY_DIR / "transports" / "i2c.js").read_text(
            encoding="utf-8"
        )

        self.assertIn("payload = frame.readRect", driver)
        self.assertIn("this.transport.write(payload)", driver)
        self.assertIn("payload.close()", driver)
        self.assertNotIn("frame.readRect(rect.x, y0, rect.width, y1 - y0).toArray()", driver)
        self.assertIn("device.writeSegments", transport)
        self.assertIn("global.i2c.openBus", transport)
        self.assertIn("this.bus.openDevice", transport)
        self.assertIn("[[this.dataPrefix & 0xff], body]", transport)

    def test_panel_drivers_close_single_byte_view_payloads(self):
        for name in ("ssd1306.js", "st7789.js"):
            driver = (DISPLAY_DIR / "drivers" / name).read_text(encoding="utf-8")
            self.assertIn("payload = frame.readRect", driver)
            self.assertIn("this.transport.write(payload)", driver)
            self.assertIn("payload.close()", driver)


if __name__ == "__main__":
    unittest.main()
