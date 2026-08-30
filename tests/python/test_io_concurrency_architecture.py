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
        self.assertIn('JS_CFUNC_DEF("stats", 0, js_event_queue_stats)', stdlib)
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
        self.assertIn("s_wifi_disconnect_future_driver", wifi_driver)
        self.assertIn('JS_GetPropertyStr(ctx, *wifi, "disconnect")', wifi_driver)
        self.assertIn("ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED", wifi_driver)
        self.assertIn("ESP32_MQUICKJS_CANCEL_REJECTED", wifi_driver)
        self.assertIn(
            "state->kind == WIFI_FUTURE_DISCONNECT", wifi_driver
        )
        self.assertIn(
            "esp32_mquickjs_wifi_clear_connect_future();", wifi_driver
        )

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
        self.assertIn('JS_CFUNC_DEF("stats", 0, js_http_server_stats)', stdlib)
        self.assertNotIn('JS_CFUNC_DEF("get", 2, js_http_server_get)', stdlib)
        self.assertNotIn("staticFileHandler", stdlib)
        self.assertNotIn("staticFileHandler", declarations)
        self.assertNotIn("JSGCRef callback", server)
        self.assertIn("esp32_mquickjs_event_queue_new", server)
        self.assertIn("esp32_mquickjs_event_queue_register_receive_alias", server)
        self.assertIn("s_http_server_respond_driver", server)
        self.assertIn("esp32_mquickjs_future_submit_worker", server)
        self.assertIn("memory_order_release", server)
        self.assertIn("memory_order_acquire", server)
        self.assertIn("js_http_server_finalizer", stdlib)
        self.assertNotIn('"serverId"', server)
        self.assertNotIn('"serverGeneration"', server)
        self.assertIn("receive(timeoutMs?: number): Request | null", declarations)
        self.assertIn("class HttpServer implements EventQueue<Request>", declarations)
        self.assertIn("route(method: string, path: string): boolean", declarations)

    def test_http_server_initializes_network_runtime_before_listening(self):
        server = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_server.c"
        ).read_text(encoding="utf-8")
        function_start = server.index("static esp_err_t http_server_start_slot(")
        function_end = server.index("\nstatic esp_err_t http_server_stop_slot(", function_start)
        start = server[function_start:function_end]

        self.assertLess(
            start.index("esp32_mquickjs_net_ensure_initialized()"),
            start.index("httpd_start("),
        )
        self.assertNotIn("esp_netif_init()", start)
        self.assertNotIn("esp_event_loop_create_default()", start)

    def test_http_server_stop_failure_retains_native_state_for_retry(self):
        server = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_server.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS
            / "src/modules/http/esp32_mquickjs_http_server_resources.c"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "esp32_mquickjs_http_server_resources_stop", resources
        )
        self.assertIn("if (result != 0)", resources)
        self.assertLess(
            resources.index("if (result != 0)"),
            resources.index("resources->handle = NULL"),
        )

        stop_start = server.index(
            "static esp_err_t http_server_stop_slot(",
            server.index("static int http_server_native_stop("),
        )
        stop_end = server.index("\nstatic int http_server_clear_routes_for_server", stop_start)
        stop = server[stop_start:stop_end]
        self.assertIn("esp32_mquickjs_http_server_resources_stop", stop)
        self.assertNotIn("httpd_stop(", stop)
        self.assertLess(stop.index("if (err != ESP_OK)"), stop.index("server->started = false"))
        self.assertLess(stop.index("if (err != ESP_OK)"), stop.index("registered = false"))

        close_start = server.index(
            "static esp_err_t http_server_close_slot(", stop_end
        )
        close_end = server.index("\nstatic int http_server_server_id_from_object", close_start)
        close = server[close_start:close_end]
        self.assertLess(
            close.index("err = http_server_stop_slot(server)"),
            close.index("esp32_mquickjs_event_queue_close"),
        )
        self.assertLess(
            close.index("if (err != ESP_OK)"),
            close.index("esp32_mquickjs_event_queue_close"),
        )

        explicit_stop_start = server.index("JSValue js_http_server_stop(")
        explicit_stop_end = server.index("\nJSValue js_http_server_close(", explicit_stop_start)
        explicit_stop = server[explicit_stop_start:explicit_stop_end]
        self.assertLess(
            explicit_stop.index("if (err != ESP_OK)"),
            explicit_stop.index('"started", JS_FALSE'),
        )

        explicit_close_start = explicit_stop_end + 1
        explicit_close_end = server.index("\nJSValue js_http_server_receive(", explicit_close_start)
        explicit_close = server[explicit_close_start:explicit_close_end]
        self.assertLess(
            explicit_close.index("if (err != ESP_OK)"),
            explicit_close.index("JS_SetOpaque"),
        )

        deinit_start = server.index("void esp32_mquickjs_deinit_http_server_runtime(")
        deinit = server[deinit_start:]
        self.assertIn("err = http_server_cleanup_all(ctx)", deinit)
        cleanup_start = deinit.index("err = http_server_cleanup_all(ctx)")
        self.assertLess(
            deinit.index("if (err != ESP_OK)", cleanup_start),
            deinit.index("http_server_reset_state()", cleanup_start),
        )
        init_start = server.index("bool esp32_mquickjs_init_http_server_runtime(")
        init_end = server.index("\nJSValue js_http_server_constructor", init_start)
        init = server[init_start:init_end]
        self.assertIn("http_server_init_state(ctx, runtime)", init)
        self.assertIn("esp_err_to_name(err)", init)

    def test_ledc_runtime_teardown_retains_failed_resources_for_retry(self):
        ledc = (
            MQUICKJS / "src/modules/ledc/esp32_mquickjs_ledc.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS / "src/modules/ledc/esp32_mquickjs_ledc_resources.c"
        ).read_text(encoding="utf-8")
        core = (MQUICKJS / "src/core/esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("esp32_mquickjs_ledc_channel_resources_deinit", resources)
        self.assertIn("esp32_mquickjs_ledc_timer_resources_deinit", resources)
        self.assertLess(
            resources.index("resources->stopped = true"),
            resources.index("result = ops->deconfigure_channel"),
        )
        self.assertLess(
            resources.index("resources->paused = true"),
            resources.index("result = ops->deconfigure_timer"),
        )

        cleanup_start = ledc.index("static esp_err_t ledc_cleanup_all(")
        cleanup_end = ledc.index("\nbool esp32_mquickjs_init_ledc_runtime(", cleanup_start)
        cleanup = ledc[cleanup_start:cleanup_end]
        self.assertIn("ledc_cleanup_channel", cleanup)
        self.assertIn("ledc_cleanup_timer", cleanup)
        self.assertLess(
            cleanup.index("if (err != ESP_OK)"),
            cleanup.index("ledc_fade_func_uninstall"),
        )
        self.assertNotIn("memset(s_ledc_channels", cleanup)

        init_start = ledc.index("bool esp32_mquickjs_init_ledc_runtime(")
        init_end = ledc.index("\nJSValue js_ledc_timerConfig", init_start)
        init = ledc[init_start:init_end]
        self.assertIn("err = ledc_cleanup_all()", init)
        self.assertIn("esp_err_to_name(err)", init)
        self.assertIn("if (!esp32_mquickjs_init_ledc_runtime(ctx))", core)

    def test_storage_uses_resource_lanes_before_the_bounded_worker_pool(self):
        filesystem = (
            MQUICKJS / "src/modules/fs/esp32_mquickjs_fs.c"
        ).read_text(encoding="utf-8")
        nvs = (
            MQUICKJS / "src/modules/nvs/esp32_mquickjs_nvs.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_future_submit_worker", filesystem)
        self.assertIn("esp32_mquickjs_future_register_driver", filesystem)
        self.assertIn("fs_future_resource_key", filesystem)
        self.assertNotIn("s_fs_worker_lock", filesystem)
        self.assertIn("esp32_mquickjs_future_submit_worker", nvs)
        self.assertIn("esp32_mquickjs_future_register_driver", nvs)
        self.assertIn("nvs_future_resource_key", nvs)
        self.assertNotIn("s_nvs_worker_lock", nvs)

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
        self.assertNotIn("xSemaphoreTake", fs_worker)
        self.assertNotIn("xSemaphoreTake", nvs_worker)
        self.assertIn("memory_order_release", fs_worker)
        self.assertIn("memory_order_release", nvs_worker)

    def test_future_worker_pool_initialization_is_all_or_nothing(self):
        future = (
            MQUICKJS / "src/core/esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")
        init_start = future.index("static bool future_init_worker_pool(void)")
        init_end = future.index("\nstatic future_runtime_t *future_runtime(", init_start)
        init = future[init_start:init_end]

        self.assertIn("TaskHandle_t workers[", init)
        self.assertIn(
            "started != CONFIG_ESP32_MQUICKJS_FUTURE_WORKER_POOL_SIZE", init
        )
        self.assertIn(
            "esp32_mquickjs_future_worker_pool_cleanup_partial", init
        )
        self.assertIn("vTaskDelete(cleanup->workers[worker_index])", future)
        self.assertIn("vQueueDelete(cleanup->queue)", future)
        self.assertIn("s_future_worker_queue = NULL", init)
        self.assertNotIn("started > 0", init)

    def test_future_runtime_resources_are_created_all_or_nothing(self):
        future = (
            MQUICKJS / "src/core/esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS
            / "src/core/esp32_mquickjs_future_runtime_resources.c"
        ).read_text(encoding="utf-8")
        init_start = future.index("bool esp32_mquickjs_init_future_runtime(")
        init_end = future.index(
            "\nbool esp32_mquickjs_prepare_future_runtime_destroy(", init_start
        )
        init = future[init_start:init_end]
        deinit_start = future.index(
            "void esp32_mquickjs_deinit_future_runtime("
        )
        deinit_end = future.index(
            "\nbool esp32_mquickjs_get_future_status(", deinit_start
        )
        deinit = future[deinit_start:deinit_end]

        self.assertIn(
            "esp32_mquickjs_future_runtime_resources_init(", init
        )
        self.assertNotIn("heap_caps_calloc", init)
        self.assertNotIn("xQueueCreate", init)
        self.assertIn(
            "esp32_mquickjs_future_runtime_resources_deinit(", deinit
        )
        self.assertLess(
            resources.index("resources->ready"),
            resources.index("resources->submissions"),
        )
        self.assertLess(
            resources.index("resources->submissions"),
            resources.index("resources->slots"),
        )
        self.assertLess(
            resources.index("resources->slots"),
            resources.index("resources->runtime_state"),
        )

    def test_resource_lanes_are_bounded_fifo_and_do_not_occupy_workers(self):
        future = (MQUICKJS / "src/core/esp32_mquickjs_future.c").read_text(
            encoding="utf-8"
        )
        header = (MQUICKJS / "internal/esp32_mquickjs_future.h").read_text(
            encoding="utf-8"
        )
        scheduler = (
            MQUICKJS / "src/core/esp32_mquickjs_future_scheduler.c"
        ).read_text(encoding="utf-8")
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")
        dispatch_start = future.index("static void future_dispatch_call(")
        dispatch_end = future.index("\nstatic void future_sleep_timer_callback", dispatch_start)
        dispatch = future[dispatch_start:dispatch_end]
        waiting_start = future.index("static bool future_dispatch_waiting_lanes(")
        waiting_end = future.index(
            "\nbool esp32_mquickjs_init_future_runtime", waiting_start
        )
        waiting = future[waiting_start:waiting_end]

        self.assertIn("esp32_mquickjs_resource_key_t", header)
        self.assertIn("(*resource_key)(", header)
        self.assertIn("future_lane_admission(state, slot)", dispatch)
        self.assertIn("slot->lane_waiting = true", dispatch)
        self.assertNotIn("->start(", dispatch[: dispatch.index("slot->lane_waiting = true")])
        self.assertIn("esp32_mquickjs_future_scheduler_next_waiting", waiting)
        self.assertIn("submission_sequence", scheduler)
        self.assertIn("future_start_captured_driver", waiting)
        self.assertIn(
            "CONFIG_ESP32_MQUICKJS_FUTURE_RESOURCE_LANE_QUEUE_LEN", future
        )
        self.assertIn("ESP32_MQUICKJS_FUTURE_RESOURCE_LANE_QUEUE_LEN", kconfig)

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
        self.assertIn("stream_future_resource_key", stream)
        self.assertIn("slot->future_reservations++", stream)
        self.assertNotIn("slot->busy", stream)
        self.assertIn("s_stream_flush_driver", stream)
        self.assertIn("s_stream_close_driver", stream)
        self.assertIn("s_stream_seek_driver", stream)
        self.assertIn("_Atomic bool completed", stream)
        self.assertIn("memory_order_release", stream)
        self.assertIn("memory_order_acquire", stream)
        self.assertIn("js_stream_finalizer", stdlib)

    def test_binary_bus_reads_and_storage_use_bounded_owned_data(self):
        byte_source = (
            MQUICKJS / "src/core/esp32_mquickjs_byte_source.c"
        ).read_text(encoding="utf-8")
        stream = (
            MQUICKJS / "src/core/esp32_mquickjs_stream.c"
        ).read_text(encoding="utf-8")
        filesystem = (
            MQUICKJS / "src/modules/fs/esp32_mquickjs_fs.c"
        ).read_text(encoding="utf-8")
        atomic_write = (
            MQUICKJS / "src/core/esp32_mquickjs_fs_atomic_write.c"
        ).read_text(encoding="utf-8")
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        self.assertIn("bool closed;", byte_source)
        self.assertIn("view->closed = false", byte_source)
        self.assertIn("view->closed = true", byte_source)
        self.assertIn("return view != NULL && !view->closed", byte_source)

        for module in ("i2c", "spi", "uart"):
            source = (
                MQUICKJS
                / "src/modules"
                / module
                / f"esp32_mquickjs_{module}.c"
            ).read_text(encoding="utf-8")
            self.assertIn("esp32_mquickjs_new_owned_byte_view(", source)
            self.assertNotIn("bytes_to_array", source)

        self.assertIn("config ESP32_MQUICKJS_FS_READ_TEXT_MAX_BYTES", kconfig)
        self.assertIn("fs_parse_read_text_options", filesystem)
        self.assertIn('"FS_READ_LIMIT_EXCEEDED"', filesystem)
        self.assertIn("fs_read_text_bounded", filesystem)
        self.assertIn("fs_open_atomic_temp", filesystem)
        self.assertIn("O_EXCL", filesystem)
        self.assertIn("fsync(fileno(file))", filesystem)
        self.assertIn("rename(temp_path, target_path)", filesystem)
        self.assertIn("unlink(temp_path)", filesystem)
        self.assertIn("esp32_mquickjs_fs_atomic_write", filesystem)
        self.assertLess(
            atomic_write.index("ops->write_sync_close"),
            atomic_write.index("ops->replace"),
        )
        self.assertIn("ops->remove_temp(temp_path, opaque)", atomic_write)
        self.assertIn("maxBytes?: number", declarations)

        self.assertNotIn("stream_collect_span_source", stream)
        self.assertIn("stream_future_next_source_span", stream)
        self.assertIn("esp32_mquickjs_open_byte_span_source", stream)
        self.assertIn("esp32_mquickjs_byte_span_source_next", stream)
        self.assertIn("_Atomic bool span_worker_completed", stream)
        self.assertIn("state->io_length += written", stream)
        self.assertIn("ByteSource | ByteSpanSource", declarations)

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

    def test_gpio_and_i2s_callbacks_use_explicit_synchronization(self):
        gpio = (
            MQUICKJS / "src/modules/gpio/esp32_mquickjs_gpio.c"
        ).read_text(encoding="utf-8")
        i2s = (
            MQUICKJS / "src/modules/i2s/esp32_mquickjs_i2s.c"
        ).read_text(encoding="utf-8")

        self.assertNotIn("volatile", gpio)
        self.assertIn("portENTER_CRITICAL_ISR(&s_gpio_interrupt_lock)", gpio)
        self.assertIn("portENTER_CRITICAL(&s_gpio_interrupt_lock)", gpio)
        self.assertIn("uint32_t event_sequence;", gpio)
        self.assertIn("event.timestamp_us = esp_timer_get_time();", gpio)
        self.assertIn("event.level = gpio_ll_get_level(&GPIO", gpio)
        self.assertIn('event_obj, "sequence"', gpio)
        self.assertIn('event_obj, "timestampUs"', gpio)
        event_start = gpio.index("static JSValue gpio_make_interrupt_event(")
        event_end = gpio.index("\nstatic JSValue gpio_make_status(", event_start)
        self.assertNotIn("gpio_get_level(", gpio[event_start:event_end])

        self.assertNotIn("volatile", i2s)
        self.assertIn("#include <stdatomic.h>", i2s)
        self.assertIn("_Atomic uint32_t overruns", i2s)
        self.assertIn("_Atomic uint32_t send_queue_overflows", i2s)
        self.assertIn("atomic_fetch_add_explicit(&slot->overruns", i2s)
        self.assertIn("atomic_load_explicit(&slot->overruns", i2s)
        self.assertIn("portENTER_CRITICAL_ISR(&s_i2s_callback_lock)", i2s)
        self.assertIn("i2s_publish_rx_wake_target(slot, runtime, token)", i2s)
        self.assertIn("i2s_clear_rx_wake_target(slot)", i2s)
        self.assertIn("i2s_publish_tx_wake_target(slot, runtime, token)", i2s)
        self.assertIn("i2s_clear_tx_wake_target(slot)", i2s)

    def test_gpio_runtime_teardown_retains_failed_isr_handler_removal(self):
        gpio = (
            MQUICKJS / "src/modules/gpio/esp32_mquickjs_gpio.c"
        ).read_text(encoding="utf-8")
        release_start = gpio.index("static esp_err_t gpio_interrupt_release_slot(")
        release_end = gpio.index(
            "\nstatic void gpio_interrupt_close_queue(", release_start
        )
        release = gpio[release_start:release_end]
        deinit_start = gpio.index("void esp32_mquickjs_deinit_gpio_runtime(")
        deinit = gpio[deinit_start:]

        self.assertLess(
            release.index("gpio_isr_handler_remove(pin)"),
            release.index("slot->handler_installed = false"),
        )
        self.assertIn(
            "(void)gpio_interrupt_release_slot((gpio_num_t)pin, NULL)",
            deinit,
        )
        self.assertNotIn("slot->handler_installed = false", deinit)

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

        self.assertIn('JS_CFUNC_DEF("watch", 1, js_fs_watch)', stdlib)
        self.assertIn("esp32_mquickjs_event_queue_new", filesystem)
        self.assertIn("ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST", filesystem)
        self.assertIn("esp32_mquickjs_fs_notify_change", filesystem)
        self.assertIn("ESP32_MQUICKJS_FS_CHANGE_QUEUE_MAX_LEN 64U", filesystem)
        self.assertIn("uint32_t event_sequence;", filesystem)
        self.assertIn("timestamp_us = esp_timer_get_time();", filesystem)
        self.assertIn('result, "sequence"', filesystem)
        self.assertIn('result, "timestampUs"', filesystem)
        self.assertIn("fs_parse_watch_options", filesystem)
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
        self.assertIn("esp32_mquickjs_future_scheduler_find_free", future)

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

    def test_timer_poll_uses_a_queue_snapshot_before_running_idle_jobs(self):
        core = (MQUICKJS / "src/core/esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        start = core.index(
            "esp32_mquickjs_poll_result_t esp32_mquickjs_poll("
        )
        end = core.index("\nJSValue js_print(", start)
        poll = core[start:end]

        self.assertIn(
            "timer_budget = uxQueueMessagesWaiting(state->queue);",
            poll,
        )
        self.assertIn(
            "while (timer_budget > 0 &&",
            poll,
        )
        self.assertIn("timer_budget--;", poll)
        self.assertLess(
            poll.index("timer_budget = uxQueueMessagesWaiting(state->queue);"),
            poll.rindex("esp32_mquickjs_poll_registered(ctx, runtime)"),
        )
        self.assertLess(
            poll.rindex("esp32_mquickjs_poll_registered(ctx, runtime)"),
            poll.rindex("esp32_mquickjs_poll_idle_job(ctx, runtime)"),
        )

    def test_timer_queue_overflow_is_recovered_from_a_turn_snapshot(self):
        core = (MQUICKJS / "src/core/esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        callback_start = core.index("static void esp32_mquickjs_timer_cb(")
        callback_end = core.index("\nstatic JSValue js_value_to_delay_ms", callback_start)
        callback = core[callback_start:callback_end]
        poll_start = core.index("esp32_mquickjs_poll_result_t esp32_mquickjs_poll(")
        poll_end = core.index("\nJSValue js_print(", poll_start)
        poll = core[poll_start:poll_end]

        self.assertIn("_Atomic bool delivery_lost;", core)
        self.assertIn("slot->pending = true;", callback)
        self.assertIn(
            "atomic_store_explicit(&slot->delivery_lost, true, memory_order_release)",
            callback,
        )
        self.assertIn("atomic_exchange_explicit(&slot->delivery_lost", poll)
        self.assertIn("memory_order_acq_rel", poll)
        self.assertIn("lost_timer_count", poll)
        self.assertIn("esp32_mquickjs_dispatch_timer_event", poll)
        self.assertLess(
            poll.index("atomic_exchange_explicit(&slot->delivery_lost"),
            poll.index("while (timer_budget > 0 &&"),
            "overflow recovery must snapshot lost deliveries before callbacks run",
        )

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

    def test_native_future_drivers_capture_at_creation_and_confirm_cancellation(self):
        future = (MQUICKJS / "src/core/esp32_mquickjs_future.c").read_text(
            encoding="utf-8"
        )
        header = (MQUICKJS / "internal/esp32_mquickjs_future.h").read_text(
            encoding="utf-8"
        )
        call_start = future.index("JSValue js_future_call(")
        call = future[call_start:]
        dispatch_start = future.index("static void future_dispatch_call(")
        dispatch_end = future.index("\nstatic void future_sleep_timer_callback", dispatch_start)
        dispatch = future[dispatch_start:dispatch_end]
        cancel_start = future.index("static bool future_cancel_slot(")
        cancel_end = future.index("\nstatic void future_advance_timeout", cancel_start)
        cancel = future[cancel_start:cancel_end]
        poll_start = future.index("static bool future_poll_ready(")
        poll_end = future.index("\nstatic bool future_poll_active_drivers", poll_start)
        poll = future[poll_start:poll_end]

        self.assertIn("bool (*capture)(JSContext *ctx", header)
        self.assertNotIn("bool (*prepare)(JSContext *ctx", header)
        self.assertIn("ESP32_MQUICKJS_CANCEL_REJECTED", header)
        self.assertIn("ESP32_MQUICKJS_CANCELLED", header)
        self.assertIn("ESP32_MQUICKJS_CANCEL_REQUESTED", header)
        self.assertLess(
            call.index("future_capture_call_driver(ctx, state, slot)"),
            call.index("future_submit(slot)"),
        )
        self.assertNotIn("->capture(", dispatch)
        self.assertIn("result == ESP32_MQUICKJS_CANCEL_REJECTED", cancel)
        self.assertIn("result == ESP32_MQUICKJS_CANCEL_REQUESTED", cancel)
        self.assertIn("slot->cancel_requested = true", cancel)
        self.assertIn("if (slot->cancel_requested)", poll)
        self.assertIn("FUTURE_STATE_CANCELLED", poll)

        http = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_future.c"
        ).read_text(encoding="utf-8")
        http_cancel_start = http.index(
            "static esp32_mquickjs_cancel_result_t http_future_cancel("
        )
        http_cancel_end = http.index("\nstatic void http_future_destroy", http_cancel_start)
        http_cancel = http[http_cancel_start:http_cancel_end]
        self.assertLess(
            http_cancel.index("esp32_mquickjs_http_operation_cancel"),
            http_cancel.index("state->cancel_requested = true"),
        )
        self.assertIn("return ESP32_MQUICKJS_CANCEL_REQUESTED;", http_cancel)

        socket = (
            MQUICKJS / "src/modules/socket/esp32_mquickjs_socket.c"
        ).read_text(encoding="utf-8")
        self.assertIn("_Atomic bool cancel_requested;", socket)
        self.assertIn(
            "atomic_store_explicit(&state->tls_request->cancel_requested",
            socket,
        )
        socket_cancel_start = socket.rindex(
            "static esp32_mquickjs_cancel_result_t socket_future_cancel("
        )
        socket_cancel_end = socket.index(
            "\nstatic void socket_future_destroy", socket_cancel_start
        )
        socket_cancel = socket[socket_cancel_start:socket_cancel_end]
        self.assertIn(
            "return ESP32_MQUICKJS_CANCEL_REQUESTED;", socket_cancel
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

    def test_future_sleep_deadlines_make_progress_without_a_wake_token(self):
        future = (MQUICKJS / "src/core/esp32_mquickjs_future.c").read_text(
            encoding="utf-8"
        )
        poll_start = future.index("bool esp32_mquickjs_future_poll(")
        poll_end = future.index(
            "\nbool esp32_mquickjs_future_cooperate(", poll_start
        )
        poll = future[poll_start:poll_end]

        self.assertIn("static bool future_advance_sleep_deadlines(", future)
        self.assertIn("slot->kind != FUTURE_KIND_SLEEP", future)
        self.assertIn("slot->deadline_us > now_us", future)
        self.assertIn(
            "future_settle(ctx, slot, FUTURE_STATE_FULFILLED, JS_UNDEFINED)",
            future,
        )
        self.assertIn("future_advance_sleep_deadlines(ctx, runtime)", poll)
        self.assertLess(
            poll.index("future_advance_sleep_deadlines(ctx, runtime)"),
            poll.index("xQueueReceive(state->ready"),
            "expired sleeps must settle even when their ready token was dropped",
        )

    def test_http_operation_and_client_cleanup_resources_retain_failures(self):
        http = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http.c"
        ).read_text(encoding="utf-8")
        future = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_future.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS
            / "src/modules/http/esp32_mquickjs_http_operation_resources.c"
        ).read_text(encoding="utf-8")
        client_resources = (
            MQUICKJS
            / "src/modules/http/esp32_mquickjs_http_client_resources.c"
        ).read_text(encoding="utf-8")
        create_start = http.index(
            "esp32_mquickjs_http_operation_t *esp32_mquickjs_http_operation_create("
        )
        create_end = http.index(
            "\nbool esp32_mquickjs_http_operation_destroy(", create_start
        )
        create = http[create_start:create_end]
        destroy_start = create_end + 1
        destroy_end = http.index(
            "\nbool esp32_mquickjs_http_operation_cancel(", destroy_start
        )
        destroy = http[destroy_start:destroy_end]

        self.assertIn(
            "esp32_mquickjs_http_operation_resources_init(", create
        )
        self.assertNotIn("heap_caps_calloc", create)
        self.assertNotIn("xSemaphoreCreateMutex", create)
        self.assertIn(
            "esp32_mquickjs_http_operation_resources_deinit(", destroy
        )
        self.assertIn("http_operation_cleanup_client", destroy)
        self.assertLess(
            destroy.index("if (err != ESP_OK)"),
            destroy.index("esp32_mquickjs_http_operation_resources_deinit"),
        )
        self.assertLess(
            resources.index("resources->lock"),
            resources.index("resources->operation"),
        )
        self.assertLess(
            client_resources.index("if (result != 0)"),
            client_resources.index("resources->client = NULL"),
        )
        attach_start = http.index("static bool http_operation_attach_client(")
        attach_end = http.index(
            "\nstatic esp_err_t http_operation_cleanup_client(", attach_start
        )
        attach = http[attach_start:attach_end]
        self.assertLess(
            attach.index("operation->client = client"),
            attach.index("if (operation->cancel_requested)"),
        )
        future_destroy_start = future.index("static void http_future_destroy(")
        future_destroy_end = future.index(
            "\nstatic uint32_t http_future_timeout_ms", future_destroy_start
        )
        future_destroy = future[future_destroy_start:future_destroy_end]
        self.assertIn(
            "if (!esp32_mquickjs_http_operation_destroy(state->operation))",
            future_destroy,
        )
        self.assertLess(
            future_destroy.index("http_future_track_pending_cleanup(state)"),
            future_destroy.index("http_future_release_worker()"),
        )
        deinit_start = future.index("bool esp32_mquickjs_deinit_http_runtime(")
        deinit = future[deinit_start:]
        self.assertLess(
            deinit.index("http_future_retry_pending_cleanup()"),
            deinit.index("active = s_http_future_runtime.active_count > 0"),
        )

    def test_rmt_symbol_buffer_resources_are_created_all_or_nothing(self):
        rmt = (
            MQUICKJS / "src/modules/rmt/esp32_mquickjs_rmt.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS
            / "src/modules/rmt/esp32_mquickjs_rmt_symbol_buffer_resources.c"
        ).read_text(encoding="utf-8")
        create_start = rmt.index("JSValue js_rmt_create_symbols(")
        create_end = rmt.index("\nJSValue js_rmt_open(", create_start)
        create = rmt[create_start:create_end]
        release_start = rmt.index("static void rmt_symbol_buffer_release(")
        release_end = rmt.index(
            "\nstatic esp32_mquickjs_rmt_channel_slot_t", release_start
        )
        release = rmt[release_start:release_end]

        self.assertIn(
            "esp32_mquickjs_rmt_symbol_buffer_resources_init(", create
        )
        self.assertNotIn("heap_caps_calloc", create)
        self.assertNotIn("heap_caps_free", create)
        self.assertIn(
            "esp32_mquickjs_rmt_symbol_buffer_resources_deinit(", release
        )
        self.assertLess(
            resources.index("resources->symbols"),
            resources.index("resources->buffer"),
        )

    def test_rmt_channel_and_encoder_share_one_retry_safe_owner(self):
        rmt = (
            MQUICKJS / "src/modules/rmt/esp32_mquickjs_rmt.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS
            / "src/modules/rmt/esp32_mquickjs_rmt_channel_resources.c"
        ).read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        cleanup_start = rmt.index("static esp_err_t rmt_channel_cleanup(")
        cleanup_end = rmt.index(
            "\nstatic esp_err_t rmt_abort_active(", cleanup_start
        )
        cleanup = rmt[cleanup_start:cleanup_end]
        open_start = rmt.index("JSValue js_rmt_open(")
        open_end = rmt.rindex("\n#endif")
        open_path = rmt[open_start:open_end]
        close_start = rmt.index("JSValue js_rmt_channel_close(")
        close_end = rmt.index("\nJSValue js_rmt_capabilities(", close_start)
        close = rmt[close_start:close_end]

        self.assertIn(
            "esp32_mquickjs_rmt_channel_resources_init(", open_path
        )
        self.assertNotIn("rmt_new_tx_channel", open_path)
        self.assertNotIn("rmt_new_rx_channel", open_path)
        self.assertNotIn("rmt_new_copy_encoder", open_path)
        self.assertIn(
            "esp32_mquickjs_rmt_channel_resources_deinit(", cleanup
        )
        self.assertNotIn("rmt_disable", cleanup)
        self.assertNotIn("rmt_del_encoder", cleanup)
        self.assertNotIn("rmt_del_channel", cleanup)
        self.assertIn("err = rmt_channel_cleanup(slot)", close)
        self.assertLess(
            close.index("if (err != ESP_OK)"),
            close.index("JS_SetOpaque(ctx, *this_val, NULL)"),
        )
        self.assertLess(
            resources.index("ops->delete_encoder"),
            resources.index("ops->delete_channel"),
        )
        self.assertIn(
            "src/modules/rmt/esp32_mquickjs_rmt_channel_resources.c", cmake
        )

    def test_i2s_timeout_timers_are_created_all_or_nothing(self):
        i2s = (
            MQUICKJS / "src/modules/i2s/esp32_mquickjs_i2s.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS
            / "src/modules/i2s/esp32_mquickjs_i2s_timer_resources.c"
        ).read_text(encoding="utf-8")
        open_start = i2s.index("JSValue js_i2s_open(")
        open_end = i2s.rindex("\n#endif")
        open_path = i2s[open_start:open_end]
        cleanup_start = i2s.index("static esp_err_t i2s_cleanup_slot(")
        cleanup_end = i2s.index(
            "\nstatic void i2s_request_close(", cleanup_start
        )
        cleanup = i2s[cleanup_start:cleanup_end]

        self.assertIn(
            "esp32_mquickjs_i2s_timer_resources_init(", open_path
        )
        self.assertNotIn("esp_timer_create", open_path)
        self.assertIn(
            "esp32_mquickjs_i2s_timer_resources_deinit(", cleanup
        )
        self.assertNotIn("esp_timer_delete", cleanup)
        self.assertLess(
            resources.index("resources->tx_timer"),
            resources.index("resources->rx_timer"),
        )

    def test_i2s_channels_retain_partial_start_stop_and_delete_state(self):
        i2s = (
            MQUICKJS / "src/modules/i2s/esp32_mquickjs_i2s.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS
            / "src/modules/i2s/esp32_mquickjs_i2s_channel_resources.c"
        ).read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        cleanup_start = i2s.index("static esp_err_t i2s_cleanup_slot(")
        cleanup_end = i2s.index(
            "\nstatic void i2s_request_close(", cleanup_start
        )
        cleanup = i2s[cleanup_start:cleanup_end]
        start_start = i2s.index("JSValue js_i2s_channel_start(")
        start_end = i2s.index("\nJSValue js_i2s_channel_stop(", start_start)
        start = i2s[start_start:start_end]
        stop_start = start_end + 1
        stop_end = i2s.index("\nJSValue js_i2s_channel_read(", stop_start)
        stop = i2s[stop_start:stop_end]
        close_start = i2s.index("JSValue js_i2s_channel_close(")
        close_end = i2s.index("\nJSValue js_i2s_capabilities(", close_start)
        close = i2s[close_start:close_end]
        open_start = i2s.index("JSValue js_i2s_open(")
        open_end = i2s.rindex("\n#endif")
        open_path = i2s[open_start:open_end]

        self.assertIn(
            "esp32_mquickjs_i2s_channel_resources_init(", open_path
        )
        self.assertNotIn("i2s_new_channel", open_path)
        self.assertIn(
            "esp32_mquickjs_i2s_channel_resources_stop(", cleanup
        )
        self.assertIn(
            "esp32_mquickjs_i2s_channel_resources_delete(", cleanup
        )
        self.assertNotIn("i2s_channel_disable", cleanup)
        self.assertNotIn("i2s_del_channel", cleanup)
        self.assertIn(
            "esp32_mquickjs_i2s_channel_resources_start(", start
        )
        self.assertNotIn("i2s_channel_enable", start)
        self.assertIn(
            "esp32_mquickjs_i2s_channel_resources_stop(", stop
        )
        self.assertNotIn("i2s_channel_disable", stop)
        self.assertIn("err = i2s_cleanup_slot(slot)", close)
        self.assertLess(
            close.index("if (err != ESP_OK)"),
            close.index("JS_SetOpaque(ctx, *this_val, NULL)"),
        )
        resource_stop_start = resources.index(
            "int esp32_mquickjs_i2s_channel_resources_stop("
        )
        resource_delete_start = resources.index(
            "int esp32_mquickjs_i2s_channel_resources_delete("
        )
        resource_stop = resources[resource_stop_start:resource_delete_start]
        resource_deinit_start = resources.index(
            "int esp32_mquickjs_i2s_channel_resources_deinit("
        )
        resource_delete = resources[
            resource_delete_start:resource_deinit_start
        ]
        self.assertLess(
            resource_stop.index("resources->rx_enabled"),
            resource_stop.index("resources->tx_enabled"),
        )
        self.assertLess(
            resource_delete.index("resources->rx_channel"),
            resource_delete.index("resources->tx_channel"),
        )
        self.assertIn(
            "src/modules/i2s/esp32_mquickjs_i2s_channel_resources.c", cmake
        )

    def test_adc_unit_and_calibrations_share_one_retry_safe_owner(self):
        adc = (
            MQUICKJS / "src/modules/adc/esp32_mquickjs_adc.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS
            / "src/modules/adc/esp32_mquickjs_adc_unit_resources.c"
        ).read_text(encoding="utf-8")
        core = (MQUICKJS / "src/core/esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        close_state_start = adc.index("static esp_err_t adc_close_unit_state(")
        close_state_end = adc.index("\nstatic JSValue adc_throw_error(", close_state_start)
        close_state = adc[close_state_start:close_state_end]
        open_start = adc.index("JSValue js_adc_open(")
        open_end = adc.index("\nJSValue js_adc_close(", open_start)
        open_path = adc[open_start:open_end]
        close_start = open_end + 1
        close_end = adc.index("\nJSValue js_adc_status(", close_start)
        close = adc[close_start:close_end]
        configure_start = adc.index("JSValue js_adc_configure(")
        configure_end = adc.index("\nJSValue js_adc_read(", configure_start)
        configure = adc[configure_start:configure_end]

        self.assertIn("esp32_mquickjs_adc_unit_resources_deinit(", close_state)
        self.assertNotIn("adc_oneshot_del_unit", close_state)
        self.assertNotIn("adc_cali_delete_scheme", close_state)
        self.assertIn("err = adc_close_unit_state(unit)", open_path)
        self.assertLess(
            open_path.index("if (err != ESP_OK)"),
            open_path.index("esp32_mquickjs_adc_unit_resources_init("),
        )
        self.assertNotIn("adc_oneshot_new_unit", open_path)
        self.assertIn("err = adc_close_unit_state(unit)", close)
        self.assertIn("if (err != ESP_OK)", close)
        self.assertLess(
            configure.index("adc_release_cali_handle("),
            configure.index("adc_oneshot_config_channel("),
        )
        self.assertIn("if (!esp32_mquickjs_init_adc_runtime(ctx))", core)
        self.assertLess(
            resources.index("resources->calibrations"),
            resources.index("resources->unit"),
        )
        self.assertIn(
            "src/modules/adc/esp32_mquickjs_adc_unit_resources.c", cmake
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

    def test_websocket_owned_callback_payloads_use_the_queue_core(self):
        websocket = (
            MQUICKJS / "src/modules/websocket/esp32_mquickjs_websocket.c"
        ).read_text(encoding="utf-8")
        enqueue = websocket[
            websocket.index("static void websocket_enqueue(") : websocket.index(
                "\nstatic void websocket_enqueue_simple("
            )
        ]

        self.assertIn("esp32_mquickjs_event_queue_enqueue_from_callback(", enqueue)
        self.assertNotIn("xQueueSend", enqueue)
        self.assertIn("websocket_free_callback_event(event)", enqueue)
        self.assertIn("dropped_events", enqueue)

    def test_websocket_callback_uses_a_fixed_pool_and_quiesces_before_destroy(self):
        websocket = (
            MQUICKJS / "src/modules/websocket/esp32_mquickjs_websocket.c"
        ).read_text(encoding="utf-8")
        handler_start = websocket.index("static void websocket_handle_data(")
        handler_end = websocket.index(
            "\nstatic void websocket_event_handler(", handler_start
        )
        handler = websocket[handler_start:handler_end]
        event_handler_start = handler_end + 1
        event_handler_end = websocket.index(
            "\nstatic int websocket_client_resource_stop(", event_handler_start
        )
        event_handler = websocket[event_handler_start:event_handler_end]
        cleanup_start = websocket.index(
            "static esp_err_t websocket_cleanup_client_resources("
        )
        cleanup_end = websocket.index(
            "\nstatic void websocket_finish_close_source(", cleanup_start
        )
        cleanup = websocket[cleanup_start:cleanup_end]

        self.assertIn("esp32_mquickjs_native_pool_acquire(", handler)
        self.assertNotIn("memory_payload_alloc", handler)
        self.assertNotIn("heap_caps_malloc", handler)
        self.assertNotIn("heap_caps_calloc", handler)
        self.assertNotIn("portMAX_DELAY", handler)
        self.assertIn("callbacks_active", event_handler)
        self.assertIn(".quiesce = websocket_client_resource_quiesce", websocket)
        self.assertIn("websocket_cleanup_client_resources", cleanup)
        self.assertNotIn("waits++ <", websocket)

    def test_wifi_callbacks_only_publish_bounded_driver_events(self):
        wifi = (
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c"
        ).read_text(encoding="utf-8")
        handler_start = wifi.index("static void wifi_event_handler(")
        handler_end = wifi.index(
            "\nstatic void wifi_process_driver_event(", handler_start
        )
        handler = wifi[handler_start:handler_end]
        timer_start = wifi.index("static void wifi_connect_timeout_cb(")
        timer_end = wifi.index(
            "\nstatic void wifi_set_scanning_locked(", timer_start
        )
        timer_callback = wifi[timer_start:timer_end]

        for callback in (handler, timer_callback):
            self.assertIn("wifi_publish_driver_event_from_callback(", callback)
            self.assertNotIn("wifi_lock()", callback)
            self.assertNotIn("portMAX_DELAY", callback)
            self.assertNotIn("heap_caps_", callback)
        self.assertIn("static bool wifi_driver_event_poller(", wifi)
        self.assertIn("callbacks_active", wifi)

    def test_websocket_publishes_one_atomic_lifecycle_to_callbacks(self):
        websocket = (
            MQUICKJS / "src/modules/websocket/esp32_mquickjs_websocket.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS
            / "src/modules/websocket/esp32_mquickjs_websocket_client_resources.c"
        ).read_text(encoding="utf-8")
        core = (
            MQUICKJS / "src/core/esp32_mquickjs.c"
        ).read_text(encoding="utf-8")

        self.assertNotIn("volatile", websocket)
        self.assertIn(
            "_Atomic esp32_mquickjs_websocket_lifecycle_t lifecycle",
            websocket,
        )
        self.assertIn("WEBSOCKET_LIFECYCLE_CLOSING", websocket)
        self.assertIn("websocket_callback_begin(&generation)", websocket)
        self.assertIn("websocket_callback_set_connected", websocket)
        self.assertIn("websocket_generation_is_active(generation)", websocket)
        self.assertIn("memory_order_acq_rel", websocket)
        self.assertIn("_Atomic uint32_t dropped_events", websocket)
        self.assertIn("_Atomic uint32_t oversized_messages", websocket)
        self.assertIn("_Atomic bool close_worker_completed", websocket)
        self.assertIn("websocket_reset_fragment();\n    websocket_drain_queue();", websocket)
        close_source_start = websocket.index("static void websocket_close_source(")
        close_source_end = websocket.index(
            "\nstatic void websocket_close_internal(", close_source_start
        )
        close_source = websocket[close_source_start:close_source_end]
        self.assertIn("websocket_schedule_close_worker()", close_source)
        self.assertNotIn("esp_websocket_client_stop(", close_source)
        self.assertNotIn("esp_websocket_client_destroy(", close_source)
        self.assertIn("static void websocket_close_worker(", websocket)
        self.assertIn("websocket_cleanup_client_resources(state)", websocket)
        self.assertLess(
            resources.index("result = ops->stop"),
            resources.index("result = ops->unregister_events"),
        )
        self.assertLess(
            resources.index("result = ops->unregister_events"),
            resources.index("result = ops->destroy"),
        )
        for operation, marker in (
            ("result = ops->stop", "resources->started = false"),
            (
                "result = ops->unregister_events",
                "resources->events_registered = false",
            ),
            ("result = ops->destroy", "resources->client = NULL"),
        ):
            operation_index = resources.index(operation)
            self.assertLess(
                resources.index("if (result != 0)", operation_index),
                resources.index(marker, operation_index),
                marker,
            )
        self.assertIn("esp32_mquickjs_submit_background_worker(", websocket)
        self.assertIn("_Atomic int close_worker_result", websocket)
        self.assertIn("bool esp32_mquickjs_deinit_websocket_runtime", websocket)
        self.assertIn(
            "if (!esp32_mquickjs_deinit_websocket_runtime(ctx))", core
        )
        self.assertLess(
            websocket.index("s_websocket_state.events_registered = true"),
            websocket.index("s_websocket_state.client_started = true"),
        )
        self.assertIn('status, "closing"', websocket)
        self.assertIn(
            "&s_websocket_state.close_worker_completed, true,\n"
            "                          memory_order_release",
            websocket,
        )
        self.assertIn(
            "&s_websocket_state.close_worker_completed,\n"
            "                              memory_order_acquire",
            websocket,
        )
        self.assertNotIn("reconnecting", websocket)
        self.assertIn("config.disable_auto_reconnect = true", websocket)
        self.assertNotIn("autoReconnect", websocket)
        self.assertNotIn("reconnectMs", websocket)
        self.assertNotIn("bool auto_reconnect", websocket)
        self.assertNotIn("s_websocket_state.auto_reconnect", websocket)

    def test_background_cleanup_uses_generic_workers_without_fake_future_wakes(self):
        header = (MQUICKJS / "internal/esp32_mquickjs_future.h").read_text(
            encoding="utf-8"
        )
        future = (MQUICKJS / "src/core/esp32_mquickjs_future.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("esp32_mquickjs_submit_background_worker(", header)
        self.assertIn("bool wake_future;", future)
        self.assertIn("if (item.wake_future)", future)
        self.assertIn(".wake_future = true", future)
        self.assertIn(".wake_future = false", future)

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
        timer_start = socket.index("esp32_mquickjs_timer_resource_init(")
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

    def test_socket_future_poller_uses_the_shared_timer_owner(self):
        socket = (
            MQUICKJS / "src/modules/socket/esp32_mquickjs_socket.c"
        ).read_text(encoding="utf-8")
        start = socket[
            socket.index("static bool socket_future_start(") : socket.index(
                "\nstatic esp32_mquickjs_future_poll_t socket_future_poll("
            )
        ]
        stop = socket[
            socket.index("static void socket_future_stop_timer(") : socket.index(
                "\nstatic bool socket_future_needs_resolution("
            )
        ]

        self.assertIn("esp32_mquickjs_timer_resource_init", start)
        self.assertNotIn("esp_timer_create", start)
        self.assertNotIn("esp_timer_start_periodic", start)
        self.assertIn("esp32_mquickjs_timer_resource_deinit", stop)
        self.assertNotIn("esp_timer_stop", stop)
        self.assertNotIn("esp_timer_delete", stop)

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

    def test_event_queue_finalizer_only_requests_reaper_cleanup(self):
        event_queue = (
            MQUICKJS / "src/core/esp32_mquickjs_event_queue.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS / "src/core/esp32_mquickjs_event_queue_resources.c"
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
        reaper_start = event_queue.index("static bool event_queue_reap(")
        reaper_end = event_queue.index(
            "\nstatic void event_queue_request_reap(", reaper_start
        )
        reaper = event_queue[reaper_start:reaper_end]
        discard_start = event_queue.index(
            "size_t esp32_mquickjs_event_queue_discard_all("
        )
        discard_end = event_queue.index(
            "\nbool esp32_mquickjs_event_queue_is_closed(", discard_start
        )
        discard = event_queue[discard_start:discard_end]
        c_test = (ROOT / "tests/c/test_event_queue_drain.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("esp32_mquickjs_event_queue_resources_init(", create)
        self.assertIn(
            "resources->drain_scratch = ops->allocate(event_size, opaque)",
            resources,
        )
        self.assertIn("event_queue_request_reap(queue, true)", finalizer)
        self.assertIn("esp32_mquickjs_event_queue_drain(", reaper)
        self.assertIn("esp32_mquickjs_event_queue_drain(", discard)
        self.assertNotIn("heap_caps_malloc", finalizer)
        self.assertNotIn("portMAX_DELAY", finalizer)
        self.assertNotIn("xSemaphoreTake", finalizer)
        self.assertNotIn("esp32_mquickjs_event_queue_close", finalizer)
        self.assertNotIn("esp32_mquickjs_event_queue_discard_all", finalizer)
        self.assertNotIn("portMAX_DELAY", reaper)
        self.assertNotIn("heap_caps_malloc", discard)
        self.assertIn("queue.allocations_allowed = false", c_test)
        self.assertIn("assert(queue.drop_calls == 2)", c_test)
        self.assertIn("assert(queue.live_payloads == 0)", c_test)

    def test_event_queue_drop_oldest_reuses_creation_time_scratch(self):
        event_queue = (
            MQUICKJS / "src/core/esp32_mquickjs_event_queue.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS / "src/core/esp32_mquickjs_event_queue_resources.c"
        ).read_text(encoding="utf-8")
        send_start = event_queue.index(
            "bool esp32_mquickjs_event_queue_send("
        )
        send_end = event_queue.index(
            "\nbool esp32_mquickjs_event_queue_send_from_isr(", send_start
        )
        send = event_queue[send_start:send_end]
        create_start = event_queue.index("JSValue esp32_mquickjs_event_queue_new(")
        create_end = event_queue.index(
            "\nJSValue js_event_queue_constructor(", create_start
        )
        create = event_queue[create_start:create_end]

        self.assertIn("esp32_mquickjs_event_queue_resources_init(", create)
        self.assertIn(
            "resources->overflow_scratch = ops->allocate(event_size, opaque)",
            resources,
        )
        self.assertIn("resources->send_lock = ops->create_lock(opaque)", resources)
        self.assertIn("queue->resources.overflow_scratch", send)
        self.assertIn(
            "(SemaphoreHandle_t)queue->resources.send_lock", send
        )
        self.assertNotIn("heap_caps_malloc", send)
        self.assertNotIn("heap_caps_free", send)

    def test_event_queue_callback_send_is_nonblocking_drop_newest_only(self):
        event_queue = (
            MQUICKJS / "src/core/esp32_mquickjs_event_queue.c"
        ).read_text(encoding="utf-8")
        callback_start = event_queue.index(
            "bool esp32_mquickjs_event_queue_try_send_from_callback("
        )
        callback_end = event_queue.index(
            "\nbool esp32_mquickjs_event_queue_send_from_isr(", callback_start
        )
        callback_send = event_queue[callback_start:callback_end]

        self.assertIn("ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST", callback_send)
        self.assertIn("esp32_mquickjs_event_queue_enqueue_from_callback(", callback_send)
        self.assertIn("event_queue_wake_receiver(queue)", callback_send)
        self.assertNotIn("portMAX_DELAY", callback_send)
        self.assertNotIn("xSemaphoreTake", callback_send)
        self.assertNotIn("heap_caps_", callback_send)

    def test_native_pool_and_lease_are_shared_core_infrastructure(self):
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        pool = (
            MQUICKJS / "src/core/esp32_mquickjs_native_pool.c"
        ).read_text(encoding="utf-8")
        lease = (
            MQUICKJS / "src/core/esp32_mquickjs_native_lease.c"
        ).read_text(encoding="utf-8")
        wireless = (
            MQUICKJS / "internal/esp32_mquickjs_wireless_core.h"
        ).read_text(encoding="utf-8")

        self.assertIn("src/core/esp32_mquickjs_native_pool.c", cmake)
        self.assertIn("src/core/esp32_mquickjs_native_lease.c", cmake)
        self.assertIn("atomic_compare_exchange_weak_explicit", pool)
        self.assertIn("esp32_mquickjs_native_pool_release", lease)
        self.assertIn("close_requested", lease)
        self.assertIn("returned", lease)
        self.assertIn('#include "esp32_mquickjs_native_pool.h"', wireless)
        self.assertNotIn("esp32_mquickjs_wireless_pool_t", wireless)

    def test_event_queue_explicit_dispose_detaches_js_and_defers_pending_receive(self):
        event_queue = (
            MQUICKJS / "src/core/esp32_mquickjs_event_queue.c"
        ).read_text(encoding="utf-8")
        header = (
            MQUICKJS / "internal/esp32_mquickjs_event_queue.h"
        ).read_text(encoding="utf-8")

        self.assertIn("bool esp32_mquickjs_event_queue_dispose(", header)
        dispose_start = event_queue.index(
            "bool esp32_mquickjs_event_queue_dispose("
        )
        dispose_end = event_queue.index(
            "\nsize_t esp32_mquickjs_event_queue_discard_all(", dispose_start
        )
        dispose = event_queue[dispose_start:dispose_end]
        self.assertIn("JS_SetOpaque(ctx, value, NULL)", dispose)
        self.assertIn("queue->dispose_requested = true", dispose)
        self.assertIn("event_queue_unregister(queue)", dispose)
        self.assertIn(
            "event_queue_take_destroy_ownership_locked(queue)", dispose
        )
        self.assertIn("queue->receiver_registered", event_queue)

    def test_event_queue_native_producers_can_retain_disposed_storage(self):
        event_queue = (
            MQUICKJS / "src/core/esp32_mquickjs_event_queue.c"
        ).read_text(encoding="utf-8")
        header = (
            MQUICKJS / "internal/esp32_mquickjs_event_queue.h"
        ).read_text(encoding="utf-8")

        self.assertIn("uint32_t native_retain_count;", event_queue)
        self.assertIn("queue->native_retain_count == 0", event_queue)
        self.assertIn("esp32_mquickjs_event_queue_retain", header)
        self.assertIn("esp32_mquickjs_event_queue_release", header)
        finalizer_start = event_queue.index("void js_event_queue_finalizer(")
        finalizer_end = event_queue.index(
            "\nJSValue js_event_queue_receive(", finalizer_start
        )
        finalizer = event_queue[finalizer_start:finalizer_end]
        self.assertIn("event_queue_request_reap(queue, true)", finalizer)
        self.assertNotIn("event_queue_destroy_native(queue)", finalizer)
        self.assertIn("queue->reaper_hold = true", event_queue)
        self.assertIn("state->native_queue_retained = true", event_queue)
        self.assertIn("esp32_mquickjs_event_queue_release(queue)", event_queue)

    def test_event_queue_claims_destroy_ownership_with_last_guard_transition(self):
        event_queue = (
            MQUICKJS / "src/core/esp32_mquickjs_event_queue.c"
        ).read_text(encoding="utf-8")

        helper_start = event_queue.index(
            "static bool event_queue_take_destroy_ownership_locked("
        )
        helper_end = event_queue.index("\n}\n", helper_start) + 3
        helper = event_queue[helper_start:helper_end]
        self.assertIn("queue->native_retain_count == 0", helper)
        self.assertIn("queue->destroying = true", helper)
        self.assertNotIn("portENTER_CRITICAL", helper)
        self.assertNotIn("portEXIT_CRITICAL", helper)

        function_bounds = (
            (
                "void esp32_mquickjs_event_queue_release(",
                "\nstatic void event_queue_wake_receiver(",
            ),
            (
                "bool esp32_mquickjs_event_queue_dispose(",
                "\nsize_t esp32_mquickjs_event_queue_discard_all(",
            ),
            (
                "static bool event_queue_reap(",
                "\nstatic void event_queue_request_reap(",
            ),
        )
        for start_marker, end_marker in function_bounds:
            start = event_queue.index(start_marker)
            end = event_queue.index(end_marker, start)
            body = event_queue[start:end]
            claim = body.index(
                "event_queue_take_destroy_ownership_locked(queue)"
            )
            unlock = body.index("portEXIT_CRITICAL(&queue->lock)", claim)
            destroy = body.index("event_queue_destroy_native(queue)", unlock)
            self.assertLess(claim, unlock)
            self.assertLess(unlock, destroy)

        release_start = event_queue.index(
            "void esp32_mquickjs_event_queue_release("
        )
        release_end = event_queue.index(
            "\nstatic void event_queue_wake_receiver(", release_start
        )
        release = event_queue[release_start:release_end]
        self.assertLess(
            release.index("queue->native_retain_count--"),
            release.index("event_queue_take_destroy_ownership_locked(queue)"),
        )
        self.assertNotIn("event_queue_destroy_if_disposed", event_queue)

    def test_event_queue_close_callbacks_run_before_filesystem_runtime_teardown(self):
        core = (MQUICKJS / "src/core/esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        filesystem = (
            MQUICKJS / "src/modules/fs/esp32_mquickjs_fs.c"
        ).read_text(encoding="utf-8")
        destroy_start = core.index("static bool esp32_mquickjs_destroy_internal(")
        destroy_end = core.index("\nbool esp32_mquickjs_destroy(", destroy_start)
        destroy = core[destroy_start:destroy_end]
        callback_start = filesystem.index("static void fs_change_queue_closed(")
        callback_end = filesystem.index("\n}\n", callback_start) + 3
        callback = filesystem[callback_start:callback_end]

        self.assertIn("xSemaphoreTake(state->lock, portMAX_DELAY)", callback)
        self.assertLess(
            destroy.index("esp32_mquickjs_deinit_event_queue_runtime(runtime)"),
            destroy.index("esp32_mquickjs_deinit_fs_runtime(runtime)"),
        )

    def test_event_queue_exposes_queue_local_stats(self):
        event_queue = (
            MQUICKJS / "src/core/esp32_mquickjs_event_queue.c"
        ).read_text(encoding="utf-8")
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        self.assertIn("uint32_t capacity;", event_queue)
        self.assertIn("esp32_mquickjs_event_queue_get_stats(", event_queue)
        self.assertIn(
            "stats->queued = (uint32_t)uxQueueMessagesWaiting", event_queue
        )
        self.assertIn(
            "stats->receiver_pending = queue->receiver_registered", event_queue
        )
        self.assertIn("JSValue js_event_queue_stats(", event_queue)
        self.assertIn("stats(): EventQueueStats;", declarations)
        for field in (
            "open",
            "queued",
            "capacity",
            "dropped",
            "receiverPending",
        ):
            self.assertIn(f"{field}:", declarations)

    def test_uart_resources_retain_driver_delete_failure_for_retry(self):
        uart = (
            MQUICKJS / "src/modules/uart/esp32_mquickjs_uart.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS
            / "src/modules/uart/esp32_mquickjs_uart_port_resources.c"
        ).read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        open_start = uart.index("static JSValue uart_open(")
        open_end = uart.index("\nstatic esp_err_t uart_cleanup_all(", open_start)
        open_path = uart[open_start:open_end]
        cleanup_start = uart.index("static esp_err_t uart_cleanup_slot(")
        cleanup_end = uart.index(
            "\nstatic esp32_mquickjs_uart_slot_t *uart_get_slot(", cleanup_start
        )
        cleanup = uart[cleanup_start:cleanup_end]

        self.assertIn("esp32_mquickjs_uart_port_resources_init", open_path)
        self.assertNotIn("xSemaphoreCreateMutex", open_path)
        self.assertNotIn("uart_driver_install", open_path)
        self.assertIn("esp32_mquickjs_uart_port_resources_deinit", cleanup)
        self.assertNotIn("uart_driver_delete", cleanup)
        self.assertIn("slot->driver_installed = resources.driver_installed", cleanup)
        self.assertLess(
            cleanup.index("slot->driver_installed = resources.driver_installed"),
            cleanup.index("if (err != ESP_OK)"),
        )
        self.assertLess(
            cleanup.index("if (err != ESP_OK)"),
            cleanup.index("uart_init_slot(slot, port_id)"),
        )
        self.assertIn("result = ops->delete_driver", resources)
        self.assertLess(
            resources.index("if (result != 0)"),
            resources.index("resources->driver_installed = false"),
        )
        self.assertIn(
            "return cleanup_result != 0 ? cleanup_result : result", resources
        )
        self.assertLess(
            resources.index("ops->delete_driver"),
            resources.index("ops->delete_watch_lock"),
        )
        delete_start = uart.index("static int uart_port_delete_driver(")
        delete_end = uart.index(
            "\nstatic void uart_port_delete_watch_lock(", delete_start
        )
        delete_driver = uart[delete_start:delete_end]
        self.assertIn("err = uart_driver_delete(context->port_id)", delete_driver)
        self.assertNotIn("(void)uart_driver_delete", delete_driver)
        close_start = uart.index("JSValue js_uart_port_close(")
        close_end = uart.index("\nJSValue js_uart_port_status(", close_start)
        close = uart[close_start:close_end]
        self.assertIn("err = uart_cleanup_slot(slot)", close)
        self.assertLess(
            close.index("if (err != ESP_OK)"),
            close.index("port_ref_ptr = JS_GetOpaque"),
        )
        init_start = uart.index("bool esp32_mquickjs_init_uart_runtime(")
        init_end = uart.index("\nJSValue js_uart_port_constructor(", init_start)
        init = uart[init_start:init_end]
        self.assertIn("err = uart_cleanup_all()", init)
        self.assertIn("if (err != ESP_OK)", init)
        self.assertIn(
            "src/modules/uart/esp32_mquickjs_uart_port_resources.c", cmake
        )

    def test_uart_future_timer_create_and_start_share_one_owner(self):
        uart = (
            MQUICKJS / "src/modules/uart/esp32_mquickjs_uart.c"
        ).read_text(encoding="utf-8")
        helper = (
            MQUICKJS / "src/core/esp32_mquickjs_timer_resource.c"
        ).read_text(encoding="utf-8")
        start = uart[
            uart.index("static bool uart_future_start(") : uart.index(
                "\nstatic esp32_mquickjs_future_poll_t uart_future_poll("
            )
        ]
        release = uart[
            uart.index("static void uart_future_release(") : uart.index(
                "\nstatic const char *uart_future_write_api("
            )
        ]

        self.assertIn("esp32_mquickjs_timer_resource_init", start)
        self.assertNotIn("esp_timer_create", start)
        self.assertNotIn("esp_timer_start_once", start)
        self.assertNotIn("esp_timer_start_periodic", start)
        self.assertIn("esp32_mquickjs_timer_resource_deinit", release)
        self.assertNotIn("esp_timer_stop", release)
        self.assertNotIn("esp_timer_delete", release)
        self.assertLess(
            helper.index("ops->stop"), helper.index("ops->delete_timer")
        )

    def test_spi_rearmable_progress_timer_uses_the_shared_owner(self):
        spi = (
            MQUICKJS / "src/modules/spi/esp32_mquickjs_spi.c"
        ).read_text(encoding="utf-8")
        start = spi[
            spi.index("static bool spi_future_start(") : spi.index(
                "\nstatic esp32_mquickjs_future_poll_t spi_future_poll("
            )
        ]
        arm_start = spi.index("static bool spi_future_arm_progress_timer(")
        arm = spi[
            arm_start : spi.index(
                "\nstatic void spi_future_mark_device_fault(", arm_start
            )
        ]
        release = spi[
            spi.index("static void spi_future_release(") : spi.index(
                "\nstatic esp32_mquickjs_future_driver_state_t *spi_future_allocate("
            )
        ]

        self.assertIn("esp32_mquickjs_timer_resource_acquire", start)
        self.assertNotIn("esp_timer_create", start)
        self.assertIn("esp32_mquickjs_timer_resource_stop", arm)
        self.assertIn("esp32_mquickjs_timer_resource_start", arm)
        self.assertNotIn("esp_timer_start_once", arm)
        self.assertIn("esp32_mquickjs_timer_resource_deinit", release)
        self.assertNotIn("esp_timer_stop", release)
        self.assertNotIn("esp_timer_delete", release)

    def test_usb_serial_rearmable_stall_timer_uses_the_shared_owner(self):
        usb_serial = (
            MQUICKJS / "src/modules/usb_serial/esp32_mquickjs_usb_serial.c"
        ).read_text(encoding="utf-8")
        start = usb_serial[
            usb_serial.index("static bool usb_serial_future_start(") :
            usb_serial.index("\nstatic void usb_serial_future_step(")
        ]
        arm = usb_serial[
            usb_serial.index("static bool usb_serial_future_arm_timeout_wake(") :
            usb_serial.index("\nstatic bool usb_serial_future_start(")
        ]
        release = usb_serial[
            usb_serial.index("static void usb_serial_future_release(") :
            usb_serial.index("\nstatic bool usb_serial_future_prepare(")
        ]

        self.assertIn("esp32_mquickjs_timer_resource_acquire", start)
        self.assertNotIn("esp_timer_create", start)
        self.assertIn("esp32_mquickjs_timer_resource_stop", arm)
        self.assertIn("esp32_mquickjs_timer_resource_start", arm)
        self.assertNotIn("esp_timer_start_once", arm)
        self.assertIn("esp32_mquickjs_timer_resource_deinit", release)
        self.assertNotIn("esp_timer_stop", release)
        self.assertNotIn("esp_timer_delete", release)

    def test_uart_write_backpressure_and_read_readiness_are_cooperative(self):
        uart = (
            MQUICKJS / "src/modules/uart/esp32_mquickjs_uart.c"
        ).read_text(encoding="utf-8")
        write_start = uart.index("static void uart_write_future_step(")
        write_end = uart.index("\nstatic void uart_future_step(", write_start)
        write = uart[write_start:write_end]
        notifier_start = uart.index("static void IRAM_ATTR uart_notify_from_isr(")
        notifier_end = uart.index("\nstatic void uart_set_notifier(", notifier_start)
        notifier = uart[notifier_start:notifier_end]

        self.assertIn("uart_get_tx_buffer_free_size", write)
        self.assertIn("uart_tx_chars", write)
        self.assertNotIn("portMAX_DELAY", write)
        self.assertIn("esp32_mquickjs_future_wake_from_isr", notifier)
        self.assertIn("esp32_mquickjs_notify_active_runtime_from_isr", notifier)
        self.assertIn("esp_timer_start_once((esp_timer_handle_t)timer", uart)
        self.assertIn("slot->write_busy", uart)
        self.assertIn("uart_future_resource_key", uart)
        self.assertIn("&slot->rx_lane_key", uart)
        self.assertIn("&slot->tx_lane_key", uart)
        self.assertIn("slot->future_reservations++", uart)
        self.assertIn("esp32_mquickjs_byte_view_acquire_read", uart)
        self.assertIn("state->write_scope.deadline_us", write)
        self.assertIn("scope->bytes_written", uart)
        self.assertIn('"bytesWritten"', uart)
        self.assertIn("slot->timeout_ms", uart)
        self.assertNotIn("UART_WRITE_STALL_TIMEOUT_MS", uart)
        self.assertNotIn("span_copy", uart)

    def test_i2c_bus_lease_and_driver_handle_share_one_owner(self):
        i2c = (
            MQUICKJS / "src/modules/i2c/esp32_mquickjs_i2c.c"
        ).read_text(encoding="utf-8")
        helper = (
            MQUICKJS
            / "src/modules/i2c/esp32_mquickjs_i2c_bus_resources.c"
        ).read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        open_start = i2c.index(
            "static esp32_mquickjs_i2c_slot_t *i2c_alloc_slot("
        )
        open_end = i2c.index(
            "\nbool esp32_mquickjs_deinit_i2c_runtime(", open_start
        )
        open_path = i2c[open_start:open_end]
        cleanup_start = i2c.index(
            "static esp_err_t i2c_cleanup_slot(", open_start
        )
        cleanup_end = i2c.index(
            "\nstatic esp_err_t i2c_cleanup_device(", cleanup_start
        )
        cleanup = i2c[cleanup_start:cleanup_end]

        self.assertIn("esp32_mquickjs_i2c_bus_resources_init", open_path)
        self.assertNotIn("esp32_mquickjs_peripheral_lease_acquire", open_path)
        self.assertNotIn("i2c_new_master_bus", open_path)
        self.assertIn("esp32_mquickjs_i2c_bus_resources_deinit", cleanup)
        self.assertNotIn("i2c_del_master_bus", cleanup)
        self.assertNotIn("esp32_mquickjs_peripheral_lease_release", cleanup)
        self.assertLess(
            helper.index("ops->delete_bus"),
            helper.index("ops->release_lease"),
        )
        self.assertIn(
            "src/modules/i2c/esp32_mquickjs_i2c_bus_resources.c", cmake
        )

    def test_i2c_and_spi_runtime_destroy_retain_failed_native_cleanup(self):
        core = (MQUICKJS / "src/core/esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        i2c = (
            MQUICKJS / "src/modules/i2c/esp32_mquickjs_i2c.c"
        ).read_text(encoding="utf-8")
        spi = (
            MQUICKJS / "src/modules/spi/esp32_mquickjs_spi.c"
        ).read_text(encoding="utf-8")

        i2c_deinit_start = i2c.index(
            "bool esp32_mquickjs_deinit_i2c_runtime("
        )
        i2c_deinit_end = i2c.index(
            "\nstatic bool i2c_register_future_drivers(", i2c_deinit_start
        )
        i2c_deinit = i2c[i2c_deinit_start:i2c_deinit_end]
        spi_deinit_start = spi.index(
            "bool esp32_mquickjs_deinit_spi_runtime("
        )
        spi_deinit_end = spi.index(
            "\nbool esp32_mquickjs_init_spi_runtime(", spi_deinit_start
        )
        spi_deinit = spi[spi_deinit_start:spi_deinit_end]

        self.assertIn("err = i2c_cleanup_slot", i2c_deinit)
        self.assertIn("return false", i2c_deinit)
        self.assertIn("i2c_reset_slots", i2c_deinit)
        self.assertLess(
            i2c_deinit.index("return false"),
            i2c_deinit.index("i2c_reset_slots"),
        )
        self.assertIn("err = spi_cleanup_all()", spi_deinit)
        self.assertIn("return err == ESP_OK", spi_deinit)
        self.assertIn(
            "if (!esp32_mquickjs_deinit_i2c_runtime())", core
        )
        self.assertIn(
            "if (!esp32_mquickjs_deinit_spi_runtime())", core
        )

    def test_bus_objects_and_continuous_uart_events_match_phase_three_contract(self):
        i2c = (
            MQUICKJS / "src/modules/i2c/esp32_mquickjs_i2c.c"
        ).read_text(encoding="utf-8")
        spi = (
            MQUICKJS / "src/modules/spi/esp32_mquickjs_spi.c"
        ).read_text(encoding="utf-8")
        uart = (
            MQUICKJS / "src/modules/uart/esp32_mquickjs_uart.c"
        ).read_text(encoding="utf-8")
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )
        i2c_worker = i2c[
            i2c.index("static void i2c_future_worker(") : i2c.index(
                "\nstatic bool i2c_future_start("
            )
        ]
        i2c_key = i2c[
            i2c.index("static esp32_mquickjs_resource_key_t i2c_future_resource_key(") : i2c.index(
                "\n#define I2C_FUTURE_DRIVER"
            )
        ]
        spi_key = spi[
            spi.index("static esp32_mquickjs_resource_key_t spi_future_resource_key(") : spi.index(
                "\n#define SPI_FUTURE_DRIVER"
            )
        ]
        uart_convert = uart[
            uart.index("static JSValue uart_watch_event_to_js(") : uart.index(
                "\nJSValue js_uart_port_watch("
            )
        ]
        uart_errors = uart[
            uart.index("static bool uart_watch_error_from_driver(") : uart.index(
                "\nstatic void uart_watch_publish_error("
            )
        ]

        self.assertIn('JS_CFUNC_DEF("openBus", 1, js_i2c_open_bus)', stdlib)
        self.assertNotIn('JS_CFUNC_DEF("open", 1, js_i2c_open)', stdlib)
        self.assertIn('JS_CFUNC_DEF("openDevice", 1, js_i2c_bus_open_device)', stdlib)
        self.assertIn('JS_CFUNC_DEF("writeSegments", 1, js_i2c_device_write_segments)', stdlib)
        self.assertIn('JS_CFUNC_DEF("writeBatch", 1, js_i2c_device_write_batch)', stdlib)
        self.assertIn("i2c_future_resource_key", i2c)
        self.assertIn("device->future_reservations++", i2c)
        self.assertIn("i2c_get_slot(&state->bus_ref)", i2c_key)
        self.assertIn("I2C_FUTURE_WRITE_BATCH", i2c_worker)
        self.assertIn("i2c_master_transmit(device", i2c_worker)
        self.assertIn("I2C_FUTURE_WRITE_SEGMENTS", i2c_worker)
        self.assertIn("i2c_master_multi_buffer_transmit(", i2c_worker)
        self.assertEqual(i2c.count("I2C_FUTURE_DRIVER(s_i2c_"), 6)

        self.assertIn('"transfer", "write", "read", "writeChunks", "writeSource"', spi)
        self.assertIn("esp32_mquickjs_dma_cursor_next", spi)
        self.assertIn("spi_device_queue_trans", spi)
        self.assertIn("spi_device_get_trans_result", spi)
        self.assertIn(
            "spi_device_acquire_bus(device->handle, portMAX_DELAY)", spi
        )
        self.assertIn("SPI_TRANS_CS_KEEP_ACTIVE", spi)
        self.assertIn("spi_future_resource_key", spi)
        self.assertIn("esp32_mquickjs_open_byte_span_source", spi)
        self.assertIn("spi_get_bus_slot_by_ids", spi_key)
        self.assertEqual(spi.count("SPI_FUTURE_DRIVER(s_spi_"), 5)

        completion_start = spi.index(
            "static void IRAM_ATTR spi_future_transaction_done("
        )
        completion_end = spi.index("\nstatic bool spi_register_future_drivers(", completion_start)
        completion = spi[completion_start:completion_end]
        self.assertIn("device_config.post_cb = spi_future_transaction_done;", spi)
        self.assertIn("transaction->user = state;", spi)
        self.assertIn("esp32_mquickjs_future_wake_from_isr", completion)
        self.assertIn("portYIELD_FROM_ISR();", completion)
        self.assertNotIn("poll_timer", spi)
        self.assertNotIn("esp_timer_start_periodic", spi)
        self.assertIn("esp_timer_start_once", spi)

        self.assertIn('JS_CFUNC_DEF("watch", 1, js_uart_port_watch)', stdlib)
        self.assertIn("esp32_mquickjs_event_queue_new", uart)
        self.assertIn("UART_WATCH_EVENT_READABLE", uart)
        self.assertIn("UART_WATCH_ERROR_FIFO_OVERFLOW", uart)
        self.assertIn("slot->readable_queued", uart)
        self.assertIn("uart_watch_publish_readable(slot, true)", uart)
        self.assertIn("one watcher per port", uart)
        self.assertIn("ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST", uart)
        for driver_error in (
            "UART_FIFO_OVF",
            "UART_BUFFER_FULL",
            "UART_BREAK",
            "UART_PARITY_ERR",
            "UART_FRAME_ERR",
        ):
            self.assertIn(driver_error, uart_errors)
        self.assertIn("if (event->kind == UART_WATCH_EVENT_READABLE)", uart_convert)
        self.assertIn("rearm = slot->watcher != NULL", uart_convert)
        self.assertIn("uart_watch_publish_readable(slot, true)", uart_convert)

        self.assertIn("openBus(options?: I2COpenBusOptions)", declarations)
        self.assertIn("openDevice(options: I2CDeviceOptions)", declarations)
        self.assertIn("writeSegments(segments: ArrayLike<ByteSource>)", declarations)
        self.assertIn("writeBatch(chunks: ArrayLike<ByteSource>)", declarations)
        self.assertIn("watch(options?: UARTWatchOptions)", declarations)
        self.assertIn('type: "readable"', declarations)
        self.assertIn('type: "error"', declarations)

    def test_uart_usb_and_websocket_sends_have_native_future_drivers(self):
        uart = (
            MQUICKJS / "src/modules/uart/esp32_mquickjs_uart.c"
        ).read_text(encoding="utf-8")
        usb_serial = (
            MQUICKJS / "src/modules/usb_serial/esp32_mquickjs_usb_serial.c"
        ).read_text(encoding="utf-8")
        runtime = (
            MQUICKJS / "src/core/esp32_mquickjs.c"
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
        self.assertIn(
            "esp32_mquickjs_usb_serial_future_lifecycle_cancel(",
            usb_serial,
        )
        self.assertIn(
            "esp32_mquickjs_usb_serial_future_lifecycle_take_release(",
            usb_serial,
        )
        destroy_start = runtime.index(
            "static bool esp32_mquickjs_destroy_internal("
        )
        destroy_end = runtime.index(
            "\nbool esp32_mquickjs_destroy(", destroy_start
        )
        destroy = runtime[destroy_start:destroy_end]
        self.assertLess(
            destroy.index("esp32_mquickjs_prepare_future_runtime_destroy("),
            destroy.index("esp32_mquickjs_deinit_usb_serial_runtime("),
        )
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

    def test_runtime_has_a_fixed_capacity_bounded_orphan_reaper(self):
        runtime = (MQUICKJS / "src/core/esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        public_header = (MQUICKJS / "include/esp32_mquickjs.h").read_text(
            encoding="utf-8"
        )
        sys_module = (MQUICKJS / "src/core/esp32_mquickjs_sys.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("esp32_mquickjs_reaper_registry_t reapers", runtime)
        self.assertIn("ESP32_MQUICKJS_REAPER_BATCH_LIMIT", runtime)
        self.assertIn("esp32_mquickjs_register_reaper", public_header)
        self.assertIn("esp32_mquickjs_unregister_reaper", public_header)
        self.assertIn("status.orphans_pending", sys_module)
        self.assertIn('"orphans"', sys_module)

    def test_byte_view_is_an_owned_stable_snapshot_contract(self):
        byte_source = (
            MQUICKJS / "src/core/esp32_mquickjs_byte_source.c"
        ).read_text(encoding="utf-8")
        byte_source_header = (
            MQUICKJS / "internal/utils/esp32_mquickjs_byte_source.h"
        ).read_text(encoding="utf-8")
        bitmap = (
            MQUICKJS / "src/modules/bitmap/esp32_mquickjs_bitmap.c"
        ).read_text(encoding="utf-8")
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("esp32_mquickjs_new_byte_view", byte_source_header)
        self.assertNotIn("esp32_mquickjs_update_byte_view", byte_source_header)
        self.assertNotIn("esp32_mquickjs_update_byte_view", bitmap)
        self.assertNotIn("BITMAP_STAGED_VIEW_KEY", bitmap)
        self.assertNotIn("BITMAP_STAGED_CHUNKS_KEY", bitmap)
        self.assertNotIn("reuse?: boolean", declarations)
        self.assertIn("stable, immutable owned snapshot", declarations)
        self.assertNotIn(
            "ByteView.close() failed because the ByteView is busy", byte_source
        )

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
