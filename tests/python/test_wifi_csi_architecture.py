import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"
MODULE = MQUICKJS / "src" / "modules" / "wifi_csi"


class WiFiCsiArchitectureTests(unittest.TestCase):
    def test_feature_catalog_and_build_gates_cover_only_supported_targets(self):
        catalog = json.loads(
            (MQUICKJS / "runtime-features.json").read_text(encoding="utf-8")
        )
        feature = {item["id"]: item for item in catalog["features"]}["wifi_csi"]
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")

        self.assertEqual(feature["requires"], ["wifi_radio"])
        self.assertEqual(feature["targets"], ["esp32c3", "esp32c5", "esp32s3"])
        self.assertEqual(feature["sdkconfig"], ["CONFIG_ESP_WIFI_CSI_ENABLED=y"])
        self.assertIn("config ESP32_MQUICKJS_FEATURE_WIFI_CSI", kconfig)
        self.assertIn("range 2 128", kconfig)
        self.assertIn("range 128 4096", kconfig)
        self.assertIn("ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS", kconfig)
        self.assertIn("ESP32_MQUICKJS_WIFI_CSI_ALLOW_FIXED_CHANNEL", kconfig)

    def test_callback_has_no_js_allocation_or_blocking_send(self):
        source = (MODULE / "esp32_mquickjs_wifi_csi.c").read_text(
            encoding="utf-8"
        )
        callback = source[
            source.index("static void wifi_csi_rx_callback") :
            source.index("static void wifi_csi_event_drop")
        ]

        self.assertIn("esp32_mquickjs_wifi_csi_callback_enter", callback)
        self.assertIn("esp32_mquickjs_wifi_csi_callback_publish", callback)
        self.assertIn("esp32_mquickjs_wifi_csi_callback_leave", callback)
        self.assertNotRegex(callback, r"\bJS_[A-Za-z0-9_]+\s*\(")
        self.assertNotRegex(callback, r"heap_caps_(?:malloc|calloc|free)\s*\(")
        self.assertNotIn("esp32_mquickjs_event_queue_send(", callback)
        self.assertNotIn("vTaskDelay", callback)

    def test_publish_path_filters_before_fixed_pool_copy_and_never_allocates(self):
        source = (MODULE / "esp32_mquickjs_wifi_csi_resources.c").read_text(
            encoding="utf-8"
        )
        publish_start = source.index(
            "esp32_mquickjs_wifi_csi_callback_publish("
        )
        publish = source[
            publish_start : source.index(
                "esp32_mquickjs_wifi_csi_slot_from_event(", publish_start
            )
        ]

        filtering = publish.index("esp32_mquickjs_wifi_csi_filter_accept")
        acquire = publish.index("esp32_mquickjs_native_pool_acquire")
        copying = publish.index("memcpy")
        queue_publish = publish.index("publish(&event")
        self.assertLess(filtering, acquire)
        self.assertLess(acquire, copying)
        self.assertLess(copying, queue_publish)
        self.assertNotRegex(publish, r"\b(?:malloc|calloc|free)\s*\(")

    def test_teardown_unregisters_and_waits_without_a_finite_escape(self):
        source = (MODULE / "esp32_mquickjs_wifi_csi.c").read_text(
            encoding="utf-8"
        )
        begin_stop = source[
            source.index("static esp_err_t wifi_csi_begin_stop") :
            source.index("static bool wifi_csi_poll_stop")
        ]
        teardown = source[
            source.index("static void wifi_csi_finish_stop") :
            source.index("static bool wifi_csi_close_native")
        ]
        destroy = source[
            source.index("static void wifi_csi_maybe_destroy_resources") :
            source.index("static esp32_mquickjs_wifi_csi_slot_t *")
        ]

        self.assertLess(begin_stop.index("esp_wifi_set_csi(false)"),
                        begin_stop.index("esp_wifi_set_csi_rx_cb(NULL, NULL)"))
        self.assertNotRegex(teardown, r"waits\s*\+\+\s*<")
        self.assertIn("callbacks_active", teardown)
        self.assertIn("esp32_mquickjs_submit_background_worker", teardown)
        self.assertIn("wifi_csi_cleanup_worker", teardown)
        self.assertIn("wifi_csi_finish_stop", teardown)
        self.assertIn("callbacks_active", destroy)
        self.assertIn("leased_frames", destroy)
        self.assertIn("resources_destroying = true", destroy)
        self.assertIn("esp32_mquickjs_wifi_csi_resources_deinit", destroy)

    def test_finalizers_only_release_leases_or_schedule_cleanup(self):
        source = (MODULE / "esp32_mquickjs_wifi_csi.c").read_text(
            encoding="utf-8"
        )
        session_finalizer = source[
            source.index("void js_wifi_csi_session_finalizer") :
            source.index("JSValue js_wifi_csi_frame_constructor")
        ]
        frame_finalizer = source[
            source.index("void js_wifi_csi_frame_finalizer") :
            source.index("JSValue js_wifi_csi_frame_samples")
        ]
        batch_finalizer = source[
            source.index("void js_wifi_csi_batch_finalizer") :
            source.index("JSValue js_wifi_csi_batch_info")
        ]

        self.assertIn("wifi_csi_request_reap", session_finalizer)
        self.assertNotIn("wifi_csi_close_native", session_finalizer)
        self.assertIn("wifi_csi_lease_request_close", frame_finalizer)
        self.assertIn("wifi_csi_batch_release_owner", batch_finalizer)

    def test_target_adapters_keep_legacy_and_he_native_structs_separate(self):
        legacy = (MODULE / "esp32_mquickjs_wifi_csi_target_legacy.c").read_text(
            encoding="utf-8"
        )
        he = (MODULE / "esp32_mquickjs_wifi_csi_target_he.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("wifi_csi_config_t", legacy)
        self.assertNotIn("wifi_csi_acquire_config_t", legacy)
        self.assertIn("esp_wifi_he_types.h", he)
        self.assertIn("acquire_csi_legacy", he)
        self.assertIn("CONFIG_SOC_WIFI_MAC_VERSION_NUM", he)
        self.assertIn("rx_channel_estimate_info_vld", he)

    def test_public_contract_is_present_in_types_docs_manifest_generator(self):
        types = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(encoding="utf-8")
        docs = (ROOT / "docs/c-api.md").read_text(encoding="utf-8")
        ai_docs = (ROOT / "docs/ai/wireless.md").read_text(encoding="utf-8")
        generator = (ROOT / "scripts/generate_api_manifest.py").read_text(
            encoding="utf-8"
        )
        stdlib = (
            MQUICKJS / "src/core/mqjs_stdlib_esp32.c"
        ).read_text(encoding="utf-8")

        for token in (
            "interface WiFiCsiCapabilities",
            "class WiFiCsiSession",
            "class WiFiCsiFrame",
            "class WiFiCsiBatch",
            "var wifiCsi: ESP32QJS.WiFiCsiModule",
        ):
            self.assertIn(token, types)
        self.assertIn("## `wifiCsi` Module", docs)
        self.assertIn("## Wi-Fi CSI", ai_docs)
        self.assertIn('JS_CGETSET_MAGIC_DEF("wifiCsi", js_sys_feature_get',
                      stdlib)
        self.assertIn('"wifiCsi": "CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI"',
                      generator)

    def test_batch_protocol_is_fixed_little_endian_and_has_a_host_parser(self):
        source = (MODULE / "esp32_mquickjs_wifi_csi.c").read_text(
            encoding="utf-8"
        )
        parser = (ROOT / "scripts/esp32qjs_csi.py").read_text(encoding="utf-8")

        self.assertIn('memcpy(source->control, "E32QCSI1", 8U)', source)
        self.assertIn("wifi_csi_write_u16_le", source)
        self.assertIn("wifi_csi_write_u32_le", source)
        self.assertIn('MAGIC = b"E32QCSI1"', parser)
        self.assertIn('struct.unpack_from("<8sHHIII"', parser)

    def test_radio_policy_has_explicit_csi_promiscuous_and_channel_owners(self):
        header = (MQUICKJS / "internal/esp32_mquickjs_wifi_radio.h").read_text(
            encoding="utf-8"
        )
        radio = (
            MQUICKJS / "src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c"
        ).read_text(encoding="utf-8")

        self.assertIn("ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI", header)
        self.assertIn("esp32_mquickjs_wifi_radio_acquire_promiscuous", header)
        self.assertIn("esp32_mquickjs_wifi_radio_release_promiscuous", header)
        self.assertIn("fixed_channel_claimed", radio)
        self.assertIn("fixed_channel_client", radio)
        self.assertIn("promiscuous_claimed", radio)
        self.assertIn("promiscuous_client", radio)
        self.assertIn("esp_wifi_sta_get_ap_info", radio)
        self.assertIn("esp_wifi_get_config(WIFI_IF_AP", radio)


if __name__ == "__main__":
    unittest.main()
