import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class MediaArchitectureTests(SourceContractTestCase):
    def test_rmt_i2s_and_camera_are_independent_feature_gated_modules(self):
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        manifest = (MQUICKJS / "idf_component.yml").read_text(encoding="utf-8")
        s3_defaults = (ROOT / "configs/mcus/esp32s3/sdkconfig.defaults").read_text(
            encoding="utf-8"
        )
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("config ESP32_MQUICKJS_FEATURE_I2S", kconfig)
        self.assertIn("depends on SOC_I2S_SUPPORTED", kconfig)
        self.assertIn("config ESP32_MQUICKJS_FEATURE_RMT", kconfig)
        self.assertIn("depends on SOC_RMT_SUPPORTED", kconfig)
        self.assertIn("config ESP32_MQUICKJS_FEATURE_CAMERA", kconfig)
        self.assertIn("depends on IDF_TARGET_ESP32S3", kconfig)
        self.assertIn("src/modules/i2s/esp32_mquickjs_i2s.c", cmake)
        self.assertIn("src/modules/rmt/esp32_mquickjs_rmt.c", cmake)
        self.assertIn("esp_driver_rmt", cmake)
        self.assertIn("if(IDF_TARGET STREQUAL \"esp32s3\")", cmake)
        self.assertIn("src/modules/camera/esp32_mquickjs_camera.c", cmake)
        self.assertIn('version: "~2.1.7"', manifest)
        self.assertIn('if: "target == esp32s3"', manifest)
        self.assertNotIn("CONFIG_CAMERA_PSRAM_DMA", s3_defaults)
        self.assertNotIn("CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA=y", s3_defaults)
        self.assertIn('JS_OBJECT_DEF("i2s", js_i2s)', stdlib)
        self.assertIn('JS_OBJECT_DEF("rmt", js_rmt)', stdlib)
        self.assertIn('JS_OBJECT_DEF("camera", js_camera_module)', stdlib)
        self.assertNotIn("camera", (MQUICKJS / "src/modules/i2s/esp32_mquickjs_i2s.c").read_text(encoding="utf-8"))

    def test_s3_build_applies_version_checked_ov3660_psram_dma_workaround(self):
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        patch = (ROOT / "scripts/patch_esp32_camera_2_1_7.cmake").read_text(
            encoding="utf-8"
        )

        self.assertIn("patch_esp32_camera_2_1_7.cmake", root_cmake)
        self.assertIn('IDF_TARGET STREQUAL "esp32s3"', patch)
        self.assertIn("version:[ \\t]+['\\\"]?2\\\\.1\\\\.7", patch)
        self.assertIn("espressif/esp32-camera#853", patch)
        self.assertIn("cam_drop_psram_cache(dma_buffer->buf, dma_buffer->len);", patch)
        self.assertIn(
            "offset_e = cam_verify_jpeg_eoi(dma_buffer->buf,", patch
        )

    def test_i2s_isr_callbacks_only_wake_the_future_driver(self):
        source = (MQUICKJS / "src/modules/i2s/esp32_mquickjs_i2s.c").read_text(
            encoding="utf-8"
        )
        helper_start = source.index("static bool IRAM_ATTR i2s_wake_rx_from_isr(")
        helper_end = source.index("\nstatic bool i2s_to_u32(", helper_start)
        helpers = source[helper_start:helper_end]
        start = source.index("static bool IRAM_ATTR i2s_on_receive(")
        end = source.index("\nstatic JSValue i2s_status_object(", start)
        callbacks = source[start:end]

        self.assertIn("esp32_mquickjs_future_wake_from_isr", helpers)
        self.assertIn("i2s_wake_rx_from_isr", callbacks)
        self.assertIn("i2s_wake_tx_from_isr", callbacks)
        for isr_code in (helpers, callbacks):
            self.assertNotIn("JS_", isr_code)
            self.assertNotIn("heap_caps_", isr_code)
            self.assertNotIn("i2s_channel_read", isr_code)

    def test_rmt_uses_native_symbols_and_bounded_future_drivers(self):
        source = (MQUICKJS / "src/modules/rmt/esp32_mquickjs_rmt.c").read_text(
            encoding="utf-8"
        )
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )
        callbacks = source[
            source.index("static bool IRAM_ATTR rmt_on_transmit_done(") : source.index(
                "\nstatic bool rmt_parse_timeout_option", source.index("static bool IRAM_ATTR rmt_on_transmit_done(")
            )
        ]

        self.assertIn("rmt_symbol_word_t *symbols;", source)
        self.assertIn("RMT_MAX_DURATION_TICKS 32767U", source)
        self.assertIn("RMT_MAX_SYMBOLS 4096U", source)
        self.assertIn("SOC_RMT_SUPPORT_DMA", source)
        self.assertIn("state->buffer->capacity * sizeof(rmt_symbol_word_t)", source)
        self.assertIn("state->buffer->length * sizeof(rmt_symbol_word_t)", source)
        self.assertIn("buffer->length == 0", source)
        self.assertIn("state->truncated = true", source)
        self.assertIn(".timeout_ms = rmt_operation_timeout_ms", source)
        self.assertIn("rmt_abort_active(slot) != ESP_OK", source)
        self.assertIn("rmt_complete_cancelled(slot, state, false)", source)
        self.assertIn("rmt_operation_cancel", source)
        self.assertIn("rmt_operation_resource_key", source)
        self.assertIn("slot->future_reservations++", source)
        self.assertIn(".resource_key = rmt_operation_resource_key", source)
        self.assertIn("rmt_request_close(&s_rmt_channels[i])", source)
        self.assertIn('"timestampUs"', source)
        self.assertIn("state->timestamp_us = timestamp_us", callbacks)
        self.assertIn("portENTER_CRITICAL_ISR(&s_rmt_callback_lock)", callbacks)
        cancel = source[
            source.index("static esp32_mquickjs_cancel_result_t rmt_operation_cancel(") : source.index(
                "\nstatic void rmt_operation_destroy("
            )
        ]
        close = source[
            source.index("JSValue js_rmt_channel_close(") : source.index(
                "\nJSValue js_rmt_capabilities("
            )
        ]
        self.assertLess(
            cancel.index("rmt_abort_active(slot)"),
            cancel.index("rmt_complete_cancelled(slot, state, false)"),
        )
        self.assertIn("return ESP32_MQUICKJS_CANCEL_REJECTED", cancel)
        self.assertIn("rmt_request_close(slot)", close)
        self.assertNotIn("refused while an operation is pending", close)
        self.assertIn("timestampUs: number", declarations)
        self.assertIn("loop_count > INT32_MAX", source)
        self.assertNotIn("NEC", source)
        self.assertIn("esp32_mquickjs_future_wake_from_isr", callbacks)
        self.assertNotIn("JS_", callbacks)
        self.assertNotIn("heap_caps_", callbacks)

    def test_i2s_read_retains_partial_dma_data_reported_with_timeout(self):
        source = (MQUICKJS / "src/modules/i2s/esp32_mquickjs_i2s.c").read_text(
            encoding="utf-8"
        )
        start = source.index("static void i2s_read_step(")
        end = source.index("\nstatic bool i2s_read_start(", start)
        step = source[start:end]

        self.assertIn(
            "state->err == ESP_OK || state->err == ESP_ERR_TIMEOUT", step
        )
        self.assertIn("state->transferred_bytes += read_bytes;", step)
        self.assertLess(
            step.index("state->transferred_bytes += read_bytes;"),
            step.index("state->err = ESP_OK;", step.index("state->transferred_bytes += read_bytes;")),
        )

    def test_i2s_unifies_directional_io_and_delayed_close(self):
        source = (MQUICKJS / "src/modules/i2s/esp32_mquickjs_i2s.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("ESP32_MQUICKJS_I2S_DIRECTION_DUPLEX", source)
        self.assertIn("slot->rx_busy || slot->tx_busy", source)
        self.assertIn("i2s_new_channel(", source)
        self.assertIn("esp32_mquickjs_i2s_channel_resources_init(", source)
        self.assertIn(
            "slot->tx_handle = (i2s_chan_handle_t)resources->tx_channel",
            source,
        )
        self.assertIn(
            "slot->rx_handle = (i2s_chan_handle_t)resources->rx_channel",
            source,
        )
        self.assertIn("i2s_channel_init_std_mode(slot->tx_handle", source)
        self.assertIn("i2s_channel_init_std_mode(slot->rx_handle", source)
        self.assertIn("static const esp32_mquickjs_future_driver_t s_i2s_write_driver", source)
        self.assertIn("JS_CLASS_BYTE_SPAN_SOURCE", source)
        self.assertIn("JS_CLASS_BITMAP_SPAN_SOURCE", source)
        self.assertIn("state->requested_bytes % bytes_per_frame != 0", source)
        self.assertIn("i2s_channel_write(", source)
        self.assertIn("i2s_channel_read(", source)
        self.assertIn(
            "static esp32_mquickjs_cancel_result_t i2s_read_cancel", source
        )
        self.assertIn(
            "static esp32_mquickjs_cancel_result_t i2s_write_cancel", source
        )
        self.assertIn("state->timed_out = true;", source)
        self.assertIn("state->deadline_us", source)
        self.assertNotIn("state->timeout_timer", source)
        self.assertIn("slot->rx_timeout_timer", source)
        self.assertIn("slot->tx_timeout_timer", source)
        self.assertEqual(source.count("esp_timer_create("), 2)
        self.assertIn("channel_config.auto_clear_after_cb", source)
        self.assertIn('"sendQueueOverflows"', source)
        self.assertNotIn('"underruns"', source)
        self.assertIn("I2SChannel.write() requires a tx or duplex channel", source)
        self.assertIn("i2s.open(pdm) only accepts direction", source)
        self.assertIn("static void i2s_request_close", source)
        self.assertIn("slot->rx_cancel_requested = true;", source)
        self.assertIn("slot->tx_cancel_requested = true;", source)
        self.assertIn("i2s_request_close(&s_i2s_slots[i]);", source)
        read_start = source[
            source.index("static bool i2s_read_start(") : source.index(
                "\nstatic esp32_mquickjs_future_poll_t i2s_read_poll("
            )
        ]
        write_start = source[
            source.index("static bool i2s_write_start(") : source.index(
                "\nstatic esp32_mquickjs_future_poll_t i2s_write_poll("
            )
        ]
        close = source[
            source.index("JSValue js_i2s_channel_close(") : source.index(
                "\nJSValue js_i2s_capabilities("
            )
        ]
        self.assertIn("if (slot->release_pending)", read_start)
        self.assertIn("if (slot->release_pending)", write_start)
        self.assertIn("i2s_request_close(slot)", close)
        self.assertNotIn("refused while I/O is pending", close)
        self.assertIn("slot->generation = i2s_take_generation();", source)
        self.assertIn("esp32_mquickjs_memory_payload_alloc", source)
        self.assertIn("ESP32_MQUICKJS_MEMORY_EXTERNAL", source)
        self.assertIn("esp32_mquickjs_memory_reserve_internal_dma", source)
        self.assertIn("esp32_mquickjs_memory_commit_driver_pinned", source)
        self.assertIn("esp32_mquickjs_memory_release_driver_pinned", source)
        self.assertIn("i2s_channel_get_info", source)
        self.assertIn('JS_NewString(ctx, "internal")', source)
        self.assertIn('"bufferBytes"', source)
        self.assertIn('"totalBufferBytes"', source)
        self.assertIn("I2S_NO_MEMORY", source)
        self.assertIn("esp32_mquickjs_i2s_channel_resources_delete(", source)
        self.assertEqual(source.count("i2s_del_channel("), 1)
        self.assertIn("i2s_future_resource_key", source)
        self.assertIn("&slot->rx_lane_key", source)
        self.assertIn("&slot->tx_lane_key", source)
        self.assertIn("slot->future_reservations++", source)
        self.assertEqual(
            source.count(".resource_key = i2s_future_resource_key"), 2
        )

    def test_camera_capture_and_frame_lease_are_bounded(self):
        source = (MQUICKJS / "src/modules/camera/esp32_mquickjs_camera.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("esp32_mquickjs_future_submit_worker", source)
        capture_worker = source[
            source.index("static void camera_capture_worker") : source.index(
                "\nstatic bool camera_capture_prepare"
            )
        ]
        self.assertIn("esp_camera_available_frames()", capture_worker)
        self.assertIn("cam_take(1)", capture_worker)
        self.assertIn("camera_future_cancelled(state)", capture_worker)
        self.assertIn("state->timeout_ms", capture_worker)
        self.assertNotIn("esp_camera_fb_get()", capture_worker)
        capture_cancel = source[
            source.index(
                "static esp32_mquickjs_cancel_result_t camera_capture_cancel"
            ) : source.index(
                "\nstatic void camera_capture_destroy"
            )
        ]
        self.assertIn("s_camera.release_pending = true;", capture_cancel)
        self.assertNotIn("state->completed || state->cancelled", capture_cancel)
        self.assertIn("esp_camera_fb_return", source)
        self.assertIn("CAMERA_MAX_COPY_BYTES (32U * 1024U)", source)
        self.assertIn("CameraFrame.close() refused while a source is active", source)
        self.assertIn("camera_release_frame();", source)
        self.assertIn("static void camera_cleanup_if_ready(void)", source)
        self.assertIn("static void camera_revoke_frame(void)", source)
        self.assertIn("s_camera.frame_revoked = true;", source)
        self.assertIn("camera_frame_storage_matches", source)
        frame_revoke = source[
            source.index("static void camera_revoke_frame") : source.index(
                "\nstatic void camera_release_leases"
            )
        ]
        self.assertIn("source->consumed = true;", frame_revoke)
        self.assertIn("if (!source->iterator_active)", frame_revoke)
        self.assertNotIn("source->destroy_requested = true;", frame_revoke)
        self.assertIn("camera_revoke_frame();", source)
        self.assertIn("s_camera.release_pending = true;", source)
        camera_close = source[source.index("JSValue js_camera_close"):source.index("JSValue js_camera_frame_constructor")]
        self.assertIn("esp32_mquickjs_future_call_and_wait", camera_close)
        self.assertNotIn("refused while capture is pending", camera_close)
        self.assertNotIn("refused while a frame is leased", camera_close)
        self.assertIn("static const esp32_mquickjs_future_driver_t s_camera_close_driver", source)
        self.assertIn("camera_close_worker", source)
        self.assertIn("s_camera.capture_state = state;", source)
        self.assertIn("camera_capture_cancel(s_camera.capture_state)", source)
        close_cancel = source[
            source.index(
                "static esp32_mquickjs_cancel_result_t camera_close_cancel"
            ) : source.index(
                "\nstatic void camera_close_destroy"
            )
        ]
        self.assertIn("return ESP32_MQUICKJS_CANCEL_REJECTED;", close_cancel)
        self.assertIn(
            "Future.call(cam.close, cam, [])",
            (ROOT / "docs/c-api.md").read_text(encoding="utf-8"),
        )
        self.assertIn("does not accept a sensor model; the driver probes it", source)
        self.assertIn("OV2640_PID", source)
        self.assertIn("OV3660_PID", source)

    def test_camera_deinit_failure_retains_driver_and_leases_for_retry(self):
        source = (
            MQUICKJS / "src/modules/camera/esp32_mquickjs_camera.c"
        ).read_text(encoding="utf-8")
        resources = (
            MQUICKJS
            / "src/modules/camera/esp32_mquickjs_camera_driver_resources.c"
        ).read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        cleanup_start = source.index("static esp_err_t camera_cleanup(void)")
        cleanup_end = source.index("\nstatic int camera_from_value(", cleanup_start)
        cleanup = source[cleanup_start:cleanup_end]
        close_start = source.index("static bool camera_close_start(")
        close_end = source.index(
            "\nstatic const esp32_mquickjs_future_driver_t s_camera_close_driver",
            close_start,
        )
        close = source[close_start:close_end]
        runtime_start = source.index("bool esp32_mquickjs_init_camera_runtime(")
        runtime_end = source.index(
            "\nvoid esp32_mquickjs_deinit_camera_runtime(", runtime_start
        )
        runtime_init = source[runtime_start:runtime_end]

        self.assertIn(
            "esp32_mquickjs_camera_driver_resources_deinit(", cleanup
        )
        self.assertNotIn("esp_camera_deinit", cleanup)
        self.assertLess(
            cleanup.index("if (err != ESP_OK)"),
            cleanup.index("camera_release_leases()"),
        )
        self.assertNotIn(
            "JS_SetOpaque", close[0 : close.index("camera_close_poll(")]
        )
        self.assertIn("if (state->close_result != ESP_OK)", close)
        self.assertLess(
            close.index("if (state->close_result != ESP_OK)"),
            close.index("camera_release_leases()"),
        )
        self.assertIn("return JS_ThrowInternalError", close)
        self.assertIn("err = camera_cleanup()", runtime_init)
        self.assertIn("if (err != ESP_OK)", runtime_init)
        self.assertNotIn("memset(&s_camera", runtime_init)
        self.assertIn("resources->initialized = false", resources)
        self.assertIn(
            "src/modules/camera/esp32_mquickjs_camera_driver_resources.c",
            cmake,
        )

    def test_bitmap_worker_borrows_raw_camera_frames_with_checked_leases(self):
        camera = (MQUICKJS / "src/modules/camera/esp32_mquickjs_camera.c").read_text(
            encoding="utf-8"
        )
        image = (
            MQUICKJS
            / "src/modules/bitmap/esp32_mquickjs_bitmap_image.c"
        ).read_text(encoding="utf-8")
        core = (
            MQUICKJS
            / "src/modules/bitmap/esp32_mquickjs_bitmap_image_core.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_camera_frame_acquire_bitmap_view", camera)
        self.assertIn("bitmap_read_leases", camera)
        self.assertIn("camera_frame_from_value(ctx, value, api_name, &ref)", camera)
        self.assertIn("JS_CLASS_CAMERA_FRAME", image)
        self.assertIn("esp32_mquickjs_camera_frame_acquire_bitmap_view", image)
        self.assertIn("esp32_mquickjs_future_submit_worker", image)
        self.assertIn("bitmap_acquire_read", image)
        self.assertIn("bitmap_acquire_write", image)
        self.assertIn("esp32_mquickjs_byte_view_acquire_read", image)
        self.assertIn("esp32_mquickjs_bitmap_transform", image)
        self.assertNotIn("js_camera_frame_read", image)
        self.assertNotIn("toArray", image)
        self.assertIn("sample_nearest", core)
        self.assertIn("sample_bilinear", core)
        self.assertIn("s_bayer_4x4", core)
        self.assertIn("inverse_rotate", core)

    def test_media_uses_shared_generation_checked_peripheral_leases(self):
        lease = (
            MQUICKJS / "src/core/esp32_mquickjs_peripheral_lease.c"
        ).read_text(encoding="utf-8")
        sources = [
            MQUICKJS / "src/modules/i2s/esp32_mquickjs_i2s.c",
            MQUICKJS / "src/modules/camera/esp32_mquickjs_camera.c",
            MQUICKJS / "src/modules/i2c/esp32_mquickjs_i2c.c",
            MQUICKJS / "src/modules/ledc/esp32_mquickjs_ledc.c",
        ]

        self.assertIn("entry->generation == lease->generation", lease)
        for path in sources:
            self.assertIn(
                "esp32_mquickjs_peripheral_lease_",
                path.read_text(encoding="utf-8"),
                str(path.relative_to(ROOT)),
            )

    def test_binary_http_and_tcp_share_close_on_terminal_source_semantics(self):
        http = (MQUICKJS / "src/modules/http/esp32_mquickjs_http.c").read_text(
            encoding="utf-8"
        )
        server = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_server.c"
        ).read_text(encoding="utf-8")
        socket = (
            MQUICKJS / "src/modules/socket/esp32_mquickjs_socket.c"
        ).read_text(encoding="utf-8")
        request_response = (
            MQUICKJS / "src/core/esp32_mquickjs_request_response.c"
        ).read_text(encoding="utf-8")
        core = (MQUICKJS / "src/core/esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("http_materialize_body", http)
        self.assertIn("Content-Length (%llu) does not match", http)
        self.assertIn("esp32_mquickjs_memory_payload_alloc", http)
        self.assertIn("ESP32_MQUICKJS_MEMORY_EXTERNAL", http)
        self.assertIn("httpd_send(req, data, length)", server)
        self.assertIn("http_server_send_known_length_response", server)
        self.assertIn("socket_future_load_send_span", socket)
        self.assertIn("socket_future_release_send_source", socket)
        self.assertIn("esp32_mquickjs_byte_span_source_close", socket)
        self.assertIn("Response.bytes(body, init?)", request_response)
        self.assertIn("rr_make_bytes_result", request_response)
        self.assertIn('JS_CFUNC_DEF("receive", 1, js_http_server_receive)', stdlib)
        self.assertIn("*events_obj, argc, argv", server)
        self.assertNotIn(
            'esp32_mquickjs_set_property_ref(ctx, server_obj, "receive"', server
        )
        self.assertLess(
            core.index("esp32_mquickjs_deinit_stream_runtime();"),
            core.index("esp32_mquickjs_deinit_camera_runtime(ctx);"),
        )

    def test_media_owned_wrappers_release_native_allocations_on_close(self):
        byte_source = (
            MQUICKJS / "src/core/esp32_mquickjs_byte_source.c"
        ).read_text(encoding="utf-8")
        camera = (
            MQUICKJS / "src/modules/camera/esp32_mquickjs_camera.c"
        ).read_text(encoding="utf-8")
        i2s = (MQUICKJS / "src/modules/i2s/esp32_mquickjs_i2s.c").read_text(
            encoding="utf-8"
        )
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )

        self.assertIn('JS_CFUNC_DEF("close", 0, js_byte_view_close)', stdlib)
        self.assertIn("JS_SetOpaque(ctx, *this_val, NULL);", byte_source)
        self.assertIn("byte_view_release(view);", byte_source)
        self.assertIn("heap_caps_free(ref);", camera[camera.index("JSValue js_camera_frame_close"):])
        self.assertIn("heap_caps_free(ref);", i2s[i2s.index("JSValue js_i2s_channel_close"):])


if __name__ == "__main__":
    unittest.main()
