import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class TlsArchitectureTests(SourceContractTestCase):
    def test_tls_is_an_independent_build_capability(self):
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        socket = (
            MQUICKJS / "src/modules/socket/esp32_mquickjs_socket.c"
        ).read_text(encoding="utf-8")
        http = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http.c"
        ).read_text(encoding="utf-8")
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        self.assertIn("config ESP32_MQUICKJS_FEATURE_TLS", kconfig)
        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_TLS", cmake)
        catalog = (
            MQUICKJS / "runtime-features.json"
        ).read_text(encoding="utf-8")
        self.assertIn('"id": "tls"', catalog)
        self.assertIn('"kconfig": "CONFIG_ESP32_MQUICKJS_FEATURE_TLS"', catalog)
        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_TLS", socket)
        self.assertIn("TLS sockets require the TLS firmware capability", socket)
        self.assertIn("HTTPS requires the TLS firmware capability", http)
        self.assertIn("readonly tls: boolean", declarations)

    def test_tls_errors_share_stable_categories_and_native_detail(self):
        helper = (
            MQUICKJS / "src/utils/esp32_mquickjs_tls_error.c"
        ).read_text(encoding="utf-8")
        socket = (
            MQUICKJS / "src/modules/socket/esp32_mquickjs_socket.c"
        ).read_text(encoding="utf-8")
        http = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http.c"
        ).read_text(encoding="utf-8")
        websocket = (
            MQUICKJS / "src/modules/websocket/esp32_mquickjs_websocket.c"
        ).read_text(encoding="utf-8")

        for code in (
            "TLS_ALLOC_FAILED",
            "TLS_TIME_INVALID",
            "TLS_VERIFY_FAILED",
            "TLS_HANDSHAKE_FAILED",
            "TLS_TIMEOUT",
        ):
            self.assertIn(code, helper)
        self.assertIn("MBEDTLS_X509_BADCERT_EXPIRED", helper)
        self.assertIn("MBEDTLS_X509_BADCERT_FUTURE", helper)
        self.assertIn("tls_error_is_mbedtls", helper)
        self.assertIn("-MBEDTLS_ERR_SSL_TIMEOUT", helper)
        self.assertIn("tls_system_time_is_valid", helper)
        self.assertIn("TLS_VALID_AFTER_UNIX", helper)
        self.assertIn('"espTlsError"', helper)
        self.assertIn('"mbedtlsError"', helper)
        self.assertIn('"verifyFlags"', helper)
        self.assertIn("esp32_mquickjs_throw_native_error", helper)
        self.assertIn("esp_tls_get_and_clear_last_error", helper)
        self.assertIn("esp_crt_verify_callback", helper)
        self.assertIn("tls_verify_capture_take", helper)
        self.assertIn("tls_crt_is_synthetic_bundle_anchor", helper)
        self.assertIn("crt->raw.p == NULL && crt->raw.len == 0", helper)
        self.assertIn("*flags &= ~MBEDTLS_X509_BADCERT_EXPIRED", helper)
        self.assertIn("Real peer and", helper)
        self.assertIn("intermediate certificates always retain raw DER", helper)
        self.assertIn("esp32_mquickjs_tls_crt_bundle_attach", http)
        self.assertIn("esp32_mquickjs_tls_crt_bundle_attach", socket)
        self.assertIn("esp32_mquickjs_tls_crt_bundle_attach", websocket)
        self.assertNotIn("= esp_crt_bundle_attach", websocket)
        self.assertNotIn("useCertBundle", websocket)
        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET", helper)
        self.assertIn("esp32_mquickjs_tls_error_capture", socket)
        self.assertIn("esp_http_client_get_and_clear_last_tls_error", http)

    def test_tls_bundle_verification_is_supported_by_the_native_layer(self):
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")
        self.assertNotIn("select MBEDTLS_X509_TRUSTED_CERT_CALLBACK", kconfig)

    def test_tls_terminal_paths_release_native_contexts(self):
        socket = (
            MQUICKJS / "src/modules/socket/esp32_mquickjs_socket.c"
        ).read_text(encoding="utf-8")
        http = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http.c"
        ).read_text(encoding="utf-8")
        http_resources = (
            MQUICKJS
            / "src/modules/http/esp32_mquickjs_http_client_resources.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp_tls_conn_destroy(entry->tls)", socket)
        self.assertIn("socket_future_cancel", socket)
        self.assertIn("socket_future_destroy", socket)
        self.assertIn("esp_http_client_cleanup", http)
        self.assertIn(
            "esp32_mquickjs_http_client_resources_deinit", http
        )
        self.assertIn("ops->cleanup(resources->client", http_resources)
        self.assertIn("esp32_mquickjs_http_operation_is_cancelled", http)

    def test_system_time_sync_is_future_driven_and_provider_neutral(self):
        source = (
            MQUICKJS / "src/modules/time/esp32_mquickjs_time.c"
        ).read_text(encoding="utf-8")
        stdlib = (
            MQUICKJS / "src/core/mqjs_stdlib_esp32.c"
        ).read_text(encoding="utf-8")

        self.assertIn('JS_CFUNC_DEF("sync", 1, js_sys_time_sync)', stdlib)
        self.assertNotIn('JS_CFUNC_DEF("syncTime", 1, js_wifi_sync_time)', stdlib)
        self.assertIn("esp_netif_sntp_init", source)
        self.assertIn("esp_netif_sntp_deinit", source)
        self.assertIn("s_time.in_progress", source)
        self.assertIn("s_time.active", source)
        self.assertIn('ctx, "TIME_SYNC_BUSY"', source)
        self.assertIn('"code", JS_NewString(ctx, code)', source)
        self.assertNotIn("time_waiter_t", source)
        self.assertNotIn("time_add_waiter_locked", source)
        self.assertNotIn("time_remove_waiter", source)
        self.assertNotIn("TIME_MAX_WAITERS", source)
        self.assertIn("s_time_future_driver", source)
        self.assertIn("esp32_mquickjs_net_is_ready", source)
        self.assertIn("#define TIME_MIN_TIMEOUT_MS 1U", source)
        self.assertIn("#define TIME_MAX_TIMEOUT_MS 60000U", source)
        self.assertIn("esp32_mquickjs_value_to_bounded_u32", source)
        self.assertIn("return state->timeout_ms;", source)
        self.assertNotIn("state->timeout_ms == 0 ? 1", source)
        self.assertNotIn("IP_EVENT_STA_GOT_IP", source)
        self.assertNotIn('"pool.ntp.org"', source)
        self.assertNotIn('"time.google.com"', source)

    def test_public_contract_documents_time_and_memory_boundaries(self):
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )
        docs = (ROOT / "docs/c-api.md").read_text(encoding="utf-8")

        self.assertIn("interface SysTimeSyncOptions", declarations)
        self.assertIn("interface SysTimeStatus", declarations)
        self.assertIn("sync(options: SysTimeSyncOptions)", declarations)
        self.assertIn("Integer timeout from 1 through 60000", declarations)
        self.assertIn("timeoutMs` is an integer from 1 through 60000", docs)
        self.assertIn('error.code === "TIME_SYNC_BUSY"', docs)
        self.assertIn("TIME_SYNC_BUSY if another operation is active", declarations)
        self.assertNotIn("interface WiFiTimeSyncOptions", declarations)
        self.assertIn("largestFreeBlockBytes", docs)
        self.assertIn("minimumFreeBytes", docs)
        self.assertIn("active network attacker", docs)
        self.assertIn("physical memory access", docs)
        self.assertNotIn("insecure: true", docs)


if __name__ == "__main__":
    unittest.main()
