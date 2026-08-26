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
        self.assertIn('JS_CFUNC_DEF("map", 1, js_future_map)', stdlib)
        self.assertIn('JS_CFUNC_DEF("flatMap", 1, js_future_flat_map)', stdlib)
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
        self.assertIn("esp32_mquickjs_event_queue_register_receive_alias", server)
        self.assertIn("receive(timeoutMs?: number): Request | null", declarations)

    def test_http_server_initializes_network_runtime_before_listening(self):
        server = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_server.c"
        ).read_text(encoding="utf-8")
        function_start = server.index("static bool http_server_start_slot(")
        function_end = server.index("\nstatic void http_server_stop_slot(", function_start)
        start = server[function_start:function_end]

        self.assertLess(
            start.index("esp32_mquickjs_net_ensure_initialized()"),
            start.index("httpd_start("),
        )
        self.assertNotIn("esp_netif_init()", start)
        self.assertNotIn("esp_event_loop_create_default()", start)

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

        fs_worker = filesystem[
            filesystem.index("static void fs_future_worker") : filesystem.index(
                "\nstatic bool fs_future_start"
            )
        ]
        nvs_worker = nvs[
            nvs.index("static void nvs_future_worker") : nvs.index(
                "\nstatic bool nvs_future_start"
            )
        ]
        self.assertNotIn("esp32_mquickjs_future_wake", fs_worker)
        self.assertNotIn("esp32_mquickjs_future_wake", nvs_worker)
        self.assertIn("memory_order_release", fs_worker)
        self.assertIn("memory_order_release", nvs_worker)

    def test_file_stream_operations_use_real_future_drivers(self):
        stream = (MQUICKJS / "src/core/esp32_mquickjs_stream.c").read_text(
            encoding="utf-8"
        )
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("esp32_mquickjs_init_stream_runtime", stream)
        self.assertIn("esp32_mquickjs_future_submit_worker", stream)
        self.assertIn("s_stream_read_driver", stream)
        self.assertIn("s_stream_write_driver", stream)
        self.assertIn("s_stream_flush_driver", stream)
        self.assertIn("s_stream_close_driver", stream)
        self.assertIn("s_stream_seek_driver", stream)
        self.assertIn("_Atomic bool completed", stream)
        self.assertIn("memory_order_release", stream)
        self.assertIn("memory_order_acquire", stream)
        self.assertIn("js_stream_finalizer", stdlib)

    def test_cross_task_future_completion_uses_c11_atomics(self):
        sources = [
            MQUICKJS / "src/core/esp32_mquickjs_event_queue.c",
            MQUICKJS / "src/core/esp32_mquickjs_stream.c",
            MQUICKJS / "src/modules/fs/esp32_mquickjs_fs.c",
            MQUICKJS / "src/modules/nvs/esp32_mquickjs_nvs.c",
            MQUICKJS / "src/modules/i2c/esp32_mquickjs_i2c.c",
            MQUICKJS / "src/modules/rmt/esp32_mquickjs_rmt.c",
            MQUICKJS / "src/modules/camera/esp32_mquickjs_camera.c",
            MQUICKJS / "src/modules/bitmap/esp32_mquickjs_bitmap_image.c",
        ]

        for path in sources:
            source = path.read_text(encoding="utf-8")
            self.assertNotIn("volatile bool completed", source)
            self.assertIn("memory_order_release", source)
            self.assertIn("memory_order_acquire", source)

    def test_filesystem_changes_use_the_generic_event_queue(self):
        filesystem = (
            MQUICKJS / "src/modules/fs/esp32_mquickjs_fs.c"
        ).read_text(encoding="utf-8")
        stream = (MQUICKJS / "src/core/esp32_mquickjs_stream.c").read_text(
            encoding="utf-8"
        )
        rpc = (MQUICKJS / "src/core/esp32_mquickjs_rpc.c").read_text(
            encoding="utf-8"
        )
        stream_header = (
            MQUICKJS / "internal/esp32_mquickjs_stream.h"
        ).read_text(encoding="utf-8")
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )

        self.assertIn('JS_CFUNC_DEF("watch", 0, js_fs_watch)', stdlib)
        self.assertIn("esp32_mquickjs_event_queue_new", filesystem)
        self.assertIn("ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST", filesystem)
        self.assertIn("esp32_mquickjs_fs_notify_change", filesystem)
        self.assertIn("esp32_mquickjs_fs_notify_change", stream)
        self.assertIn("esp32_mquickjs_fs_notify_change", rpc)
        for source in (filesystem, stream, rpc, stream_header):
            self.assertNotIn("esp32_mquickjs_fs_revision", source)
            self.assertNotIn("esp32_mquickjs_fs_mark_mutated", source)
            self.assertNotIn("s_fs_revision", source)

    def test_application_text_policy_is_not_implemented_by_the_framework(self):
        stream_header = (
            MQUICKJS / "internal/esp32_mquickjs_stream.h"
        ).read_text(encoding="utf-8")
        stream = (MQUICKJS / "src/core/esp32_mquickjs_stream.c").read_text(
            encoding="utf-8"
        )
        filesystem = (
            MQUICKJS / "src/modules/fs/esp32_mquickjs_fs.c"
        ).read_text(encoding="utf-8")
        rpc = (MQUICKJS / "src/core/esp32_mquickjs_rpc.c").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("esp32_mquickjs_utf8_validate_prefix", stream_header)
        self.assertNotIn("esp32_mquickjs_utf8_validate_prefix", stream)
        self.assertNotIn("esp32_mquickjs_utf8_validate_prefix", filesystem)
        self.assertIn("cbor_value_validate", rpc)
        self.assertIn("CborValidateUtf8", rpc)

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

    def test_future_drivers_make_progress_when_a_wake_token_is_lost(self):
        future = (MQUICKJS / "src/core/esp32_mquickjs_future.c").read_text(
            encoding="utf-8"
        )
        poll_start = future.index("bool esp32_mquickjs_future_poll(")
        poll_end = future.index(
            "\nbool esp32_mquickjs_future_cooperate(", poll_start
        )
        poll = future[poll_start:poll_end]

        self.assertIn("static bool future_poll_active_drivers(", future)
        self.assertIn("future_poll_ready(ctx, runtime, future_token(slot))", future)
        self.assertIn("future_poll_active_drivers(ctx, runtime)", poll)
        self.assertLess(
            poll.index("future_poll_active_drivers(ctx, runtime)"),
            poll.index("future_advance_combinators(ctx, runtime)"),
            "completed or cancelled native drivers must be reaped at each safe point",
        )

    def test_http_and_websocket_workers_publish_results_with_c11_atomics(self):
        http = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_future.c"
        ).read_text(encoding="utf-8")
        websocket = (
            MQUICKJS / "src/modules/websocket/esp32_mquickjs_websocket.c"
        ).read_text(encoding="utf-8")

        for source in (http, websocket):
            self.assertIn("#include <stdatomic.h>", source)
            self.assertIn("memory_order_release", source)
            self.assertIn("memory_order_acquire", source)

        self.assertIn("_Atomic bool completed;", http)
        self.assertIn(
            "atomic_store_explicit(&state->completed, true, memory_order_release)",
            http,
        )
        self.assertIn(
            "atomic_load_explicit(&state->completed, memory_order_acquire)",
            http,
        )
        self.assertNotIn("volatile bool completed;", http)

        self.assertIn("_Atomic bool worker_completed;", websocket)
        self.assertIn("_Atomic bool cancelled;", websocket)
        self.assertIn("&state->worker_completed, true, memory_order_release", websocket)
        self.assertIn(
            "&state->worker_completed, memory_order_acquire", websocket
        )
        self.assertIn("&state->cancelled, true, memory_order_release", websocket)
        self.assertIn("&state->cancelled, memory_order_acquire", websocket)
        self.assertNotIn("volatile bool worker_completed;", websocket)
        self.assertNotIn("volatile int sent;", websocket)

    def test_socket_hostname_resolution_runs_in_the_lwip_task(self):
        socket = (
            MQUICKJS / "src/modules/socket/esp32_mquickjs_socket.c"
        ).read_text(encoding="utf-8")
        publish_start = socket.index("static void socket_future_publish_resolution(")
        publish_end = socket.index("\nstatic void socket_future_dns_found(", publish_start)
        publish = socket[publish_start:publish_end]
        tls_worker_start = socket.index("static void socket_tls_connect_worker(")
        tls_worker_end = socket.index("\nstatic BaseType_t socket_tls_create_worker(", tls_worker_start)
        tls_worker = socket[tls_worker_start:tls_worker_end]

        self.assertNotIn("getaddrinfo(", socket)
        self.assertNotIn("socket_future_resolve_address", socket)
        self.assertIn("tcpip_try_callback(socket_future_dns_request, request)", socket)
        self.assertIn("dns_gethostbyname_addrtype(request->host", socket)
        self.assertIn("LWIP_DNS_ADDRTYPE_IPV4", socket)
        for phase in (
            "SOCKET_CONNECT_RESOLVING",
            "SOCKET_CONNECT_CONNECTING",
            "SOCKET_CONNECT_TLS_HANDSHAKE",
            "SOCKET_CONNECT_READY",
        ):
            self.assertIn(phase, socket)

        self.assertIn("memory_order_release", publish)
        self.assertIn("memory_order_acquire", socket)
        release = publish.index("atomic_store_explicit(")
        self.assertNotIn("esp32_mquickjs_future_wake", publish[release:])
        self.assertIn("socket_dns_request_release(request);", publish[release:])
        self.assertIn("atomic_init(&request->references, 2)", socket)
        self.assertIn("socket_dns_request_release(state->resolver);", socket)
        self.assertIn("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT", socket)
        self.assertIn("strlen(state->host) + 1U, ESP32_MQUICKJS_MEMORY_EXTERNAL", socket)
        timer_start = socket.index("esp_timer_start_periodic(state->poll_timer")
        resolver_start = socket.index(
            "socket_future_begin_resolution(ctx, state)", timer_start
        )
        self.assertLess(timer_start, resolver_start)
        self.assertIn("request->config.common_name = request->host", socket)
        self.assertIn("esp_tls_conn_new_async(\n                request->resolved_host", tls_worker)
        self.assertNotIn("SOCKET_TLS_CONNECT_POLL_TIMEOUT_MS", socket)
        self.assertIn("request->config.timeout_ms =", tls_worker)
        self.assertIn("atomic_init(&request->references, 2)", socket)
        self.assertIn("memory_order_release", tls_worker)
        self.assertIn("memory_order_acquire", socket)
        self.assertIn("socket_tls_request_release(state->tls_request);", socket)
        self.assertIn("xTaskCreateWithCaps(socket_tls_connect_worker", socket)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", socket)

    def test_future_combinators_observe_every_attached_input_rejection(self):
        future = (MQUICKJS / "src/core/esp32_mquickjs_future.c").read_text(
            encoding="utf-8"
        )
        observe_start = future.index("static void future_observe_inputs(")
        observe_end = future.index(
            "\nstatic const esp32_mquickjs_future_driver_t", observe_start
        )
        observe_inputs = future[observe_start:observe_end]
        copy_start = future.index("static void future_copy_terminal(")
        copy_end = future.index("\nstatic void future_advance_all(", copy_start)
        copy_terminal = future[copy_start:copy_end]
        runner = (ROOT / "scripts/remote.py").read_text(encoding="utf-8")
        runtime_test = (
            ROOT / "tests/js/flash_data/modules/timers/runtime.js"
        ).read_text(encoding="utf-8")

        self.assertIn("future_mark_observed", observe_inputs)
        self.assertEqual(future.count("future_observe_inputs(ctx, slot);"), 3)
        self.assertIn("future_mark_observed(source)", copy_terminal)
        self.assertIn("slot->observed = true", future)
        self.assertIn("!slot->observed", future)
        self.assertIn("JS_TEST_FORBIDDEN_OUTPUT_MARKER", runner)
        for scenario in ("all", "race-winner", "timeout-input", "race-loser"):
            self.assertIn(
                f"__ESP32QJS_HANDLED_FUTURE_REJECTION__:{scenario}",
                runtime_test,
            )

    def test_future_timeout_diagnostics_use_saturated_duration_math(self):
        future = (MQUICKJS / "src/core/esp32_mquickjs_future.c").read_text(
            encoding="utf-8"
        )
        timeout_math = (
            MQUICKJS / "src/core/esp32_mquickjs_future_timeout.c"
        ).read_text(encoding="utf-8")

        self.assertEqual(
            future.count("esp32_mquickjs_future_elapsed_timeout_ms("), 2
        )
        self.assertNotIn("slot->deadline_us - slot->submitted_us", future)
        self.assertIn("deadline_us <= submitted_us", timeout_math)
        self.assertIn("duration_ms > UINT32_MAX", timeout_math)

    def test_event_queue_finalizer_drains_without_allocating(self):
        event_queue = (
            MQUICKJS / "src/core/esp32_mquickjs_event_queue.c"
        ).read_text(encoding="utf-8")
        create_start = event_queue.index("JSValue esp32_mquickjs_event_queue_new(")
        create_end = event_queue.index(
            "\nJSValue js_event_queue_constructor(", create_start
        )
        create = event_queue[create_start:create_end]
        finalizer_start = event_queue.index("void js_event_queue_finalizer(")
        finalizer_end = event_queue.index(
            "\nJSValue js_event_queue_receive(", finalizer_start
        )
        finalizer = event_queue[finalizer_start:finalizer_end]
        c_test = (ROOT / "tests/c/test_event_queue_drain.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("queue->drain_scratch = heap_caps_malloc", create)
        self.assertIn("esp32_mquickjs_event_queue_drain(", finalizer)
        self.assertNotIn("heap_caps_malloc", finalizer)
        self.assertIn("queue.allocations_allowed = false", c_test)
        self.assertIn("assert(queue.drop_calls == 2)", c_test)
        self.assertIn("assert(queue.live_payloads == 0)", c_test)

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

    def test_uart_usb_and_websocket_sends_have_native_future_drivers(self):
        uart = (
            MQUICKJS / "src/modules/uart/esp32_mquickjs_uart.c"
        ).read_text(encoding="utf-8")
        usb_serial = (
            MQUICKJS / "src/modules/usb_serial/esp32_mquickjs_usb_serial.c"
        ).read_text(encoding="utf-8")
        websocket = (
            MQUICKJS / "src/modules/websocket/esp32_mquickjs_websocket.c"
        ).read_text(encoding="utf-8")

        self.assertIn("s_uart_write_driver", uart)
        self.assertIn("s_uart_write_chunks_driver", uart)
        self.assertIn("s_uart_write_source_driver", uart)
        self.assertGreaterEqual(
            uart.count("esp32_mquickjs_future_register_driver("), 5
        )
        self.assertIn("s_usb_serial_send_driver", usb_serial)
        self.assertIn("esp32_mquickjs_future_register_driver(", usb_serial)
        self.assertIn("s_websocket_send_driver", websocket)
        self.assertIn("esp32_mquickjs_future_register_driver(", websocket)

    def test_gpio_pins_are_strict_numbers_and_docs_use_future_watch(self):
        gpio = (
            MQUICKJS / "src/modules/gpio/esp32_mquickjs_gpio.c"
        ).read_text(encoding="utf-8")
        docs = (ROOT / "docs/c-api.md").read_text(encoding="utf-8")

        self.assertIn("JS_IsNumber(ctx, value)", gpio)
        self.assertIn("gpio.watch(pin, mode = gpio.CHANGE)", docs)
        self.assertIn(
            "Future.call(interrupts.receive, interrupts, [])", docs
        )
        self.assertNotIn("gpio.attachInterrupt", docs)
        self.assertNotIn("gpio.detachInterrupt", docs)

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
