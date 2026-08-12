import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CMAKE = ROOT / "components" / "esp32_mquickjs" / "CMakeLists.txt"


class FeatureDependencyArchitectureTests(unittest.TestCase):
    def test_optional_feature_headers_are_available_during_early_expansion(self):
        source = CMAKE.read_text(encoding="utf-8")
        match = re.search(
            r"set\(ESP32_MQUICKJS_PRIV_REQUIRES\n(?P<body>.*?)\n\)",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        dependencies = set(
            re.findall(
                r"^\s{4}([a-z][a-z0-9_]*)\s*$",
                match.group("body"),
                re.MULTILINE,
            )
        )
        self.assertTrue(
            {
                "joltwallet__littlefs",
                "nvs_flash",
                "esp_driver_gpio",
                "esp_driver_ledc",
                "esp_adc",
                "esp_driver_dac",
                "esp_driver_i2c",
                "esp_driver_spi",
                "esp_driver_uart",
                "esp_http_client",
                "esp_http_server",
                "esp_event",
                "esp_netif",
                "esp_wifi",
                "esp_driver_usb_serial_jtag",
                "esp_websocket_client",
                "lwip",
            }.issubset(dependencies)
        )


if __name__ == "__main__":
    unittest.main()
