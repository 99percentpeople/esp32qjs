import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class TransportArchitectureTests(unittest.TestCase):
    def test_usb_serial_and_repl_are_mutually_exclusive(self):
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")
        interactive_cmake = (
            ROOT / "components" / "esp32qjs_interactive" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "depends on SOC_USB_SERIAL_JTAG_SUPPORTED && !ESP32QJS_ENABLE_REPL",
            kconfig,
        )
        self.assertTrue(
            interactive_cmake.startswith("if(CONFIG_ESP32QJS_ENABLE_REPL)")
        )

    def test_websocket_is_an_optional_managed_feature(self):
        manifest = (MQUICKJS / "idf_component.yml").read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("espressif/esp_websocket_client", manifest)
        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET", cmake)
        self.assertIn("src/modules/websocket/esp32_mquickjs_websocket.c", cmake)

    def test_transport_callbacks_use_the_guarded_call_gateway(self):
        sources = (
            MQUICKJS / "src" / "modules" / "usb_serial" / "esp32_mquickjs_usb_serial.c",
            MQUICKJS / "src" / "modules" / "websocket" / "esp32_mquickjs_websocket.c",
        )

        for source in sources:
            text = source.read_text(encoding="utf-8")
            self.assertIn("esp32_mquickjs_call(", text, source)
            self.assertNotIn("JS_Call(", text, source)


if __name__ == "__main__":
    unittest.main()
