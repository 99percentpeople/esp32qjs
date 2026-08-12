import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class IoConcurrencyArchitectureTests(unittest.TestCase):
    def test_global_future_and_event_queue_are_native_classes(self):
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn('JS_PROP_CLASS_DEF("Future", &js_future_class)', stdlib)
        self.assertIn('JS_PROP_CLASS_DEF("EventQueue", &js_event_queue_class)', stdlib)
        self.assertIn('JS_CFUNC_DEF("call", 3, js_future_call)', stdlib)
        self.assertIn('JS_CFUNC_DEF("receive", 1, js_event_queue_receive)', stdlib)
        self.assertIn("src/core/esp32_mquickjs_future.c", cmake)
        self.assertIn("src/core/esp32_mquickjs_event_queue.c", cmake)

    def test_module_specific_async_namespaces_are_removed(self):
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        self.assertNotIn('JS_OBJECT_DEF("async"', stdlib)
        self.assertNotIn('JS_PROP_CLASS_DEF("async"', stdlib)
        self.assertNotIn("WiFiAsyncModule", declarations)
        self.assertNotIn("HttpAsyncModule", declarations)

    def test_http_and_wifi_share_the_future_driver_path(self):
        http = (MQUICKJS / "src/modules/http/esp32_mquickjs_http.c").read_text(
            encoding="utf-8"
        )
        wifi = (MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c").read_text(
            encoding="utf-8"
        )
        http_driver = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_future.c"
        ).read_text(encoding="utf-8")
        wifi_driver = (
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi_future.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_future_call_and_wait", http)
        self.assertIn("esp32_mquickjs_future_call_and_wait", wifi)
        self.assertIn("esp32_mquickjs_future_register_driver", http_driver)
        self.assertIn("esp32_mquickjs_future_register_driver", wifi_driver)

    def test_legacy_deferred_globals_and_gpio_callbacks_are_removed(self):
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )
        gpio = (MQUICKJS / "src/modules/gpio/esp32_mquickjs_gpio.c").read_text(
            encoding="utf-8"
        )
        core = (MQUICKJS / "src/core/esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )

        self.assertNotIn('JS_CFUNC_DEF("defer"', stdlib)
        self.assertNotIn('JS_CFUNC_DEF("waitFor"', stdlib)
        self.assertNotIn('JS_CFUNC_DEF("attachInterrupt"', stdlib)
        self.assertIn('JS_CFUNC_DEF("watch", 2, js_gpio_watch)', stdlib)
        self.assertNotIn("JSGCRef callback", gpio)
        self.assertNotIn("JS_CLASS_DEFERRED", stdlib)
        self.assertNotIn("js_make_deferred", core)

    def test_http_server_uses_declarative_routes_and_an_event_queue(self):
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )
        server = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_server.c"
        ).read_text(encoding="utf-8")
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        self.assertIn('JS_CFUNC_DEF("route", 2, js_http_server_route)', stdlib)
        self.assertIn('JS_CFUNC_DEF("respond", 2, js_http_server_respond)', stdlib)
        self.assertNotIn('JS_CFUNC_DEF("get", 2, js_http_server_get)', stdlib)
        self.assertNotIn("staticFileHandler", stdlib)
        self.assertNotIn("staticFileHandler", declarations)
        self.assertNotIn("JSGCRef callback", server)
        self.assertIn("esp32_mquickjs_event_queue_new", server)
        self.assertIn("receive(timeoutMs?: number): Request | null", declarations)

    def test_http_server_initializes_network_runtime_before_listening(self):
        server = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_server.c"
        ).read_text(encoding="utf-8")
        function_start = server.index("static bool http_server_start_slot(")
        function_end = server.index("\nstatic void http_server_stop_slot(", function_start)
        start = server[function_start:function_end]

        self.assertLess(start.index("esp_netif_init()"), start.index("httpd_start("))
        self.assertLess(
            start.index("esp_event_loop_create_default()"), start.index("httpd_start(")
        )

    def test_storage_waits_use_the_bounded_future_worker_pool(self):
        filesystem = (
            MQUICKJS / "src/modules/fs/esp32_mquickjs_fs.c"
        ).read_text(encoding="utf-8")
        nvs = (
            MQUICKJS / "src/modules/nvs/esp32_mquickjs_nvs.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_future_submit_worker", filesystem)
        self.assertIn("esp32_mquickjs_future_register_driver", filesystem)
        self.assertIn("s_fs_worker_lock", filesystem)
        self.assertIn("esp32_mquickjs_future_submit_worker", nvs)
        self.assertIn("esp32_mquickjs_future_register_driver", nvs)
        self.assertIn("s_nvs_worker_lock", nvs)

    def test_internal_sync_adapters_have_reserved_future_capacity(self):
        future = (MQUICKJS / "src/core/esp32_mquickjs_future.c").read_text(
            encoding="utf-8"
        )
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")

        self.assertIn("config ESP32_MQUICKJS_INTERNAL_FUTURE_RESERVE", kconfig)
        self.assertIn("CONFIG_ESP32_MQUICKJS_INTERNAL_FUTURE_RESERVE", future)
        self.assertIn("internal_allocation_depth", future)
        self.assertIn("future_find_free_slot(state, internal)", future)

    def test_io_modules_do_not_invoke_javascript_callbacks_directly(self):
        module_sources = (MQUICKJS / "src/modules").rglob("*.c")

        for source_path in module_sources:
            source = source_path.read_text(encoding="utf-8")
            self.assertNotIn(
                "JS_Call(",
                source,
                f"{source_path.relative_to(ROOT)} must dispatch JavaScript through "
                "the Future or timer core",
            )
            self.assertNotIn(
                "JSGCRef callback",
                source,
                f"{source_path.relative_to(ROOT)} must not retain an I/O callback",
            )


if __name__ == "__main__":
    unittest.main()
