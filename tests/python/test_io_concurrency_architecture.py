import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class IoConcurrencyArchitectureTests(SourceContractTestCase):
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

    def test_internal_idle_jobs_only_run_outside_javascript_execution(self):
        core = (MQUICKJS / "src/core/esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        self.assertIn("state->execution_depth != 0", core)
        self.assertIn("esp32_mquickjs_execution_enter(runtime);", core)
        self.assertIn("esp32_mquickjs_execution_leave(runtime);", core)
        self.assertIn("esp32_mquickjs_poll_idle_job(ctx, runtime)", core)
        self.assertIn('JS_CFUNC_DEF("_deferIdle", 1, js_runtime_defer_idle)', stdlib)
        self.assertNotIn("_deferIdle", declarations)

    def test_future_wait_checks_its_deadline_after_each_scheduler_pump(self):
        future = (MQUICKJS / "src/core/esp32_mquickjs_future.c").read_text(
            encoding="utf-8"
        )
        start = future.index("JSValue js_future_wait(")
        end = future.index("\nJSValue js_future_cancel(", start)
        wait = future[start:end]

        self.assertLess(
            wait.index("if (handle->terminal)"),
            wait.index("if (wait_deadline_us > 0 && now_us >= wait_deadline_us)"),
        )
        self.assertLess(
            wait.index("if (wait_deadline_us > 0 && now_us >= wait_deadline_us)"),
            wait.index("if (poll_result != ESP32_MQUICKJS_POLL_NONE)"),
            "ready work must not bypass a finite wait deadline",
        )

    def test_uart_write_backpressure_and_read_readiness_are_cooperative(self):
        uart = (
            MQUICKJS / "src/modules/uart/esp32_mquickjs_uart.c"
        ).read_text(encoding="utf-8")
        write_start = uart.index("static esp32_mquickjs_uart_write_result_t uart_write_cooperative(")
        write_end = uart.index("\nstatic JSValue uart_write_error(", write_start)
        write = uart[write_start:write_end]
        notifier_start = uart.index("static void IRAM_ATTR uart_notify_from_isr(")
        notifier_end = uart.index("\nstatic void uart_set_notifier(", notifier_start)
        notifier = uart[notifier_start:notifier_end]

        self.assertIn("uart_get_tx_buffer_free_size", write)
        self.assertIn("uart_tx_chars", write)
        self.assertIn("esp32_mquickjs_poll(ctx, runtime)", write)
        self.assertIn("esp32_mquickjs_wait_for_activity(runtime, wait_ms)", write)
        self.assertNotIn("portMAX_DELAY", write)
        self.assertIn("esp32_mquickjs_future_wake_from_isr", notifier)
        self.assertIn("esp32_mquickjs_notify_active_runtime_from_isr", notifier)
        self.assertIn("esp_timer_start_once(state->poll_timer", uart)
        self.assertIn("slot->write_busy", uart)
        self.assertIn("esp32_mquickjs_byte_view_acquire_read", uart)
        self.assertIn("scope->deadline_us", write)
        self.assertIn("scope->bytes_written", uart)
        self.assertIn('"bytesWritten"', uart)
        self.assertIn("slot->timeout_ms", uart)
        self.assertNotIn("UART_WRITE_STALL_TIMEOUT_MS", uart)
        self.assertNotIn("span_copy", uart)

    def test_byte_span_sources_hold_an_explicit_iterator_read_lease(self):
        byte_source = (
            MQUICKJS / "src/core/esp32_mquickjs_byte_source.c"
        ).read_text(encoding="utf-8")
        bitmap = (
            MQUICKJS / "src/modules/bitmap/esp32_mquickjs_bitmap.c"
        ).read_text(encoding="utf-8")
        usb_serial = (
            MQUICKJS / "src/modules/usb_serial/esp32_mquickjs_usb_serial.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_leased_byte_span_source_t", byte_source)
        self.assertIn("source->read_leases++", byte_source)
        self.assertIn("source->owner->read_leases--", byte_source)
        self.assertIn(
            'ByteSpanSource.close() failed because the source is busy',
            byte_source,
        )
        self.assertIn(
            'BitmapSpanSource.setRect() failed because the source is busy',
            byte_source,
        )
        self.assertIn("bitmap_acquire_read", bitmap)
        self.assertIn("bitmap_release_read", bitmap)
        self.assertNotIn("span_copy", usb_serial)

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

    def test_ledc_status_does_not_enter_the_live_driver_read_path(self):
        ledc = (
            MQUICKJS / "src/modules/ledc/esp32_mquickjs_ledc.c"
        ).read_text(encoding="utf-8")
        status_start = ledc.index("static JSValue ledc_make_timer_status(")
        status_end = ledc.index(
            "\nstatic JSValue ledc_make_channel_status(", status_start
        )
        timer_status = ledc[status_start:status_end]

        self.assertNotIn("ledc_get_freq", timer_status)
        self.assertIn("state->freq_hz", timer_status)


if __name__ == "__main__":
    unittest.main()
