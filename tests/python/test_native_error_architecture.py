import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class NativeErrorArchitectureTests(SourceContractTestCase):
    def source(self, relative_path: str) -> str:
        return (MQUICKJS / relative_path).read_text(encoding="utf-8")

    def test_shared_helper_owns_the_v1_base_shape(self):
        header = self.source("internal/esp32_mquickjs_core.h")
        core = self.source("src/core/esp32_mquickjs.c")

        self.assertIn("esp32_mquickjs_throw_native_error", header)
        self.assertIn('error, "code"', core)
        self.assertIn('error, "operation"', core)
        self.assertIn('error, "details"', core)

    def test_operational_modules_use_the_shared_shape(self):
        sources = {
            "spi": self.source("src/modules/spi/esp32_mquickjs_spi.c"),
            "tls": self.source("src/utils/esp32_mquickjs_tls_error.c"),
            "ble": self.source("src/modules/ble/esp32_mquickjs_ble.c"),
            "espnow": self.source("src/modules/espnow/esp32_mquickjs_espnow.c"),
            "wifi": self.source("src/modules/wifi/esp32_mquickjs_wifi.c"),
            "http": self.source("src/modules/http/esp32_mquickjs_http_future.c"),
        }

        for name, source in sources.items():
            with self.subTest(module=name):
                self.assertIn("esp32_mquickjs_throw_native_error", source)
                self.assertIn('details = JS_NewObject(ctx)', source)

    def test_timeout_failures_have_stable_module_codes(self):
        wifi = self.source("src/modules/wifi/esp32_mquickjs_wifi_future.c")
        http = self.source("src/modules/http/esp32_mquickjs_http_future.c")
        tls = self.source("src/utils/esp32_mquickjs_tls_error.c")
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        self.assertIn('"WIFI_CONNECT_TIMEOUT"', wifi)
        self.assertIn('"HTTP_TIMEOUT"', http)
        self.assertIn('return "TLS_TIMEOUT"', tls)
        self.assertIn("interface NativeError extends Error", declarations)
        for error_name in (
            "SPIError",
            "TlsError",
            "WiFiError",
            "EspNowError",
            "BLEError",
            "HTTPError",
        ):
            self.assertIn(
                f"interface {error_name} extends NativeError", declarations
            )


if __name__ == "__main__":
    unittest.main()
