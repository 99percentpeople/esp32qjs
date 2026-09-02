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

        self.assertEqual(feature["requires"], ["wifi"])
        self.assertEqual(feature["targets"], ["esp32c3", "esp32c5", "esp32s3"])
        self.assertEqual(feature["sdkconfig"], ["CONFIG_ESP_WIFI_CSI_ENABLED=y"])
        self.assertIn("config ESP32_MQUICKJS_FEATURE_WIFI_CSI", kconfig)
        self.assertIn("depends on ESP32_MQUICKJS_FEATURE_WIFI", kconfig)
        self.assertIn("range 2 128", kconfig)
        self.assertIn("range 128 4096", kconfig)
        self.assertIn("ESP32_MQUICKJS_WIFI_CSI_ALLOW_PROMISCUOUS", kconfig)
        self.assertNotIn("ESP32_MQUICKJS_WIFI_CSI_ALLOW_FIXED_CHANNEL", kconfig)

        source = (MODULE / "esp32_mquickjs_wifi_csi.c").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("WIFI_CSI_ALLOW_FIXED_CHANNEL", source)
        self.assertIn('supports, "fixedChannel"', source)
        self.assertIn("JS_TRUE", source)

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

    def test_indexed_properties_do_not_allocate_in_the_setter_arguments(self):
        source = (MODULE / "esp32_mquickjs_wifi_csi.c").read_text(
            encoding="utf-8"
        )

        self.assertNotRegex(
            source,
            r"JS_SetPropertyUint32\(\s*ctx,\s*\*[A-Za-z_][A-Za-z0-9_]*,"
            r"\s*[^,]+,\s*JS_New[A-Za-z0-9_]+\(",
        )

    def test_option_parsers_root_values_across_allocating_lookups(self):
        source = (MODULE / "esp32_mquickjs_wifi_csi.c").read_text(
            encoding="utf-8"
        )
        for name in (
            "wifi_csi_parse_mac_values",
            "wifi_csi_parse_filter",
            "wifi_csi_parse_legacy_capture",
            "wifi_csi_parse_he_capture",
            "wifi_csi_parse_open_options",
        ):
            start = source.index(f"static bool {name}(")
            end = source.index("\n}\n", start) + 3
            parser = source[start:end]

            with self.subTest(name=name):
                self.assertIn("JSGCRef value_ref;", parser)
                self.assertIn("*rooted_value = value;", parser)
                self.assertIn("JS_PopGCRef(ctx, &value_ref);", parser)

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

    def test_receive_batch_releases_temporary_gc_roots_in_lifo_order(self):
        source = (MODULE / "esp32_mquickjs_wifi_csi.c").read_text(
            encoding="utf-8"
        )
        receive_batch = source[
            source.index("JSValue js_wifi_csi_session_receive_batch") :
            source.index("bool esp32_mquickjs_init_wifi_csi_runtime")
        ]
        success = receive_batch[
            receive_batch.index("atomic_fetch_add_explicit(") :
            receive_batch.index("fail_batch:")
        ]

        self.assertLess(
            success.index("JS_PopGCRef(ctx, &options_ref)"),
            success.index("JS_PopGCRef(ctx, &object_ref)"),
        )
        self.assertLess(
            success.index("JS_PopGCRef(ctx, &object_ref)"),
            success.index("JS_PopGCRef(ctx, &first_ref)"),
        )

    def test_receive_batch_is_object_only_and_bounds_aggregation_times(self):
        source = (MODULE / "esp32_mquickjs_wifi_csi.c").read_text(
            encoding="utf-8"
        )
        receive_batch = source[
            source.index("JSValue js_wifi_csi_session_receive_batch") :
            source.index("bool esp32_mquickjs_init_wifi_csi_runtime")
        ]

        self.assertIn('"maximumFrames", "minimumFrames"', receive_batch)
        self.assertIn('"timeoutMs", "maximumLatencyMs"', receive_batch)
        self.assertIn("argc > 1", receive_batch)
        self.assertIn("minimum_frames > maximum_frames", receive_batch)
        self.assertGreaterEqual(receive_batch.count("INT32_MAX"), 2)
        self.assertIn("latency_deadline_us", receive_batch)
        self.assertIn("esp32_mquickjs_event_queue_try_receive", receive_batch)

    def test_in_flight_event_is_dropped_if_receive_future_is_destroyed(self):
        event_queue = (
            MQUICKJS / "src/core/esp32_mquickjs_event_queue.c"
        ).read_text(encoding="utf-8")
        destroy = event_queue[
            event_queue.index("static void event_queue_future_destroy") :
            event_queue.index("static const esp32_mquickjs_future_driver_t")
        ]

        self.assertIn("state->received && !state->event_finished", destroy)
        self.assertIn("queue->drop(state->event, queue->opaque)", destroy)

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
        docs = (ROOT / "docs/api/wifi-csi.md").read_text(encoding="utf-8")
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
            "readonly csi: WiFiCsiModule",
        ):
            self.assertIn(token, types)
        self.assertNotIn("var wifiCsi: ESP32QJS.WiFiCsiModule", types)
        self.assertIn("# Wi-Fi CSI", docs)
        wifi_table = stdlib[
            stdlib.index("static const JSPropDef js_wifi[]") :
            stdlib.index("static const JSClassDef js_wifi_obj")
        ]
        globals_table = stdlib[
            stdlib.index("static const JSPropDef js_global_object_extra[]") :
        ]
        self.assertIn('JS_PROP_CLASS_DEF("csi", &js_wifi_csi_obj)', wifi_table)
        self.assertNotIn(
            'JS_PROP_CLASS_DEF("wifiCsi", &js_wifi_csi_obj)', globals_table
        )
        self.assertIn('JS_CGETSET_MAGIC_DEF("wifiCsi", js_sys_feature_get',
                      stdlib)
        self.assertIn('"wifiCsi": "CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI"',
                      generator)
        self.assertIn(
            '"js_wifi_csi": ("wifi.csi", "WiFiCsiModule", "wifiCsi", "wifi.csi")',
            generator,
        )

    def test_batch_protocol_is_fixed_little_endian_and_has_a_host_parser(self):
        source = (MODULE / "esp32_mquickjs_wifi_csi.c").read_text(
            encoding="utf-8"
        )
        parser = (ROOT / "scripts/esp32qjs_csi.py").read_text(encoding="utf-8")

        self.assertIn('memcpy(source->control, "E32QCSI1", 8U)', source)
        self.assertIn("wifi_csi_write_u16_le", source)
        self.assertIn("wifi_csi_write_u32_le", source)
        self.assertIn("wifi_csi_write_u64_le", source)
        self.assertIn("WIFI_CSI_BATCH_METADATA_BYTES 192U", source)
        self.assertIn('MAGIC = b"E32QCSI1"', parser)
        self.assertIn("METADATA_BYTES = 192", parser)
        self.assertIn('struct.unpack_from("<Q", record, 4)', parser)
        self.assertIn('struct.unpack_from("<8sHHIII"', parser)

    def test_generic_span_length_contract_is_used_by_csi_hardware_tests(self):
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )
        byte_span_source = declarations[
            declarations.index("interface ByteSpanSource {") :
            declarations.index("interface BitmapSpanSource", declarations.index(
                "interface ByteSpanSource {"
            ))
        ]
        directory = ROOT / "tests/js/flash_data/modules/wifi_csi"
        associated = (directory / "associated-hardware.js").read_text(
            encoding="utf-8"
        )
        batch = (directory / "batch-transport-hardware.js").read_text(
            encoding="utf-8"
        )
        throughput = (directory / "throughput-hardware.js").read_text(
            encoding="utf-8"
        )

        self.assertIn("readonly byteLength: number", byte_span_source)
        self.assertIn("interface RPCFileSource extends ByteSpanSource", declarations)
        self.assertIn("sourceInfo(source: RPCFileSource)", declarations)
        self.assertIn("source.byteLength", associated)
        self.assertIn("source.byteLength", batch)
        self.assertIn("source.byteLength", throughput)
        self.assertNotIn("rpc.sourceInfo(source)", associated)
        self.assertNotIn("rpc.sourceInfo(source)", batch)
        self.assertNotIn("rpc.sourceInfo(source)", throughput)

    def test_hardware_batch_transport_covers_file_usb_and_streamed_rpc(self):
        hardware_test = (
            ROOT
            / "tests/js/flash_data/modules/wifi_csi/batch-transport-hardware.js"
        ).read_text(encoding="utf-8")

        self.assertIn('workspaceFs = fs.volume("/workspace")', hardware_test)
        self.assertIn('workspaceFs.open(path, "wb")', hardware_test)
        self.assertIn('workspaceFs.open(rpcPath, "wb")', hardware_test)
        self.assertIn("stream.write(source)", hardware_test)
        self.assertIn("if (sys.info.features.usbSerial)", hardware_test)
        self.assertIn('usbSerial.open({ mode: "binary"', hardware_test)
        self.assertIn("serial.send(usbSource)", hardware_test)
        self.assertIn("codec.encode(1, 1, 0, { data: rpcInputSource })", hardware_test)
        self.assertIn("rpcOutputSource instanceof _ByteSpanSource", hardware_test)

    def test_associated_hardware_generates_addressed_network_traffic(self):
        hardware_test = (
            ROOT
            / "tests/js/flash_data/modules/wifi_csi/associated-hardware.js"
        ).read_text(encoding="utf-8")

        self.assertIn("socket.openTCP({ localPort: 0 })", hardware_test)
        self.assertIn('trafficClient.connect("example.com", 80', hardware_test)
        self.assertIn("trafficClient.send(", hardware_test)
        self.assertIn("trafficClient.recv(", hardware_test)
        self.assertIn("trafficRequestText.charCodeAt", hardware_test)
        self.assertIn("trafficResponseBytes", hardware_test)
        self.assertIn("frame = session.receive(8000)", hardware_test)

    def test_hardware_saturation_holds_and_releases_the_complete_pool(self):
        hardware_test = (
            ROOT
            / "tests/js/flash_data/modules/wifi_csi/saturation-hardware.js"
        ).read_text(encoding="utf-8")

        self.assertIn("stats.leasedFrames", hardware_test)
        self.assertIn("stats.freePoolSlots", hardware_test)
        self.assertIn("stats.droppedPoolFull", hardware_test)
        self.assertIn("frames[i].close()", hardware_test)
        self.assertIn('test.requireConfig("wifiSsid", "wifiPassword")', hardware_test)
        self.assertIn("wifi.connect(cfg.wifiSsid", hardware_test)
        self.assertIn("wifi.scan({", hardware_test)
        self.assertIn("channel: status.effective.channel", hardware_test)
        self.assertIn("session.stop()", hardware_test)
        self.assertIn("trafficClient.send(", hardware_test)
        self.assertIn("trafficRequestText.charCodeAt", hardware_test)
        self.assertIn(
            "frames.length < status.effective.poolCapacity", hardware_test
        )
        self.assertIn(
            "fillScanAttempts < status.effective.poolCapacity * 12", hardware_test
        )
        self.assertIn(
            "dropScanAttempts < status.effective.poolCapacity * 12", hardware_test
        )
        self.assertLess(
            hardware_test.index("dropsBefore = stats.droppedPoolFull"),
            hardware_test.rindex("wifi.scan({"),
        )

    def test_short_promiscuous_hardware_cases_generate_ap_channel_traffic(self):
        directory = ROOT / "tests/js/flash_data/modules/wifi_csi"
        for name in (
            "promiscuous-hardware.js",
            "batch-transport-hardware.js",
            "throughput-hardware.js",
        ):
            source = (directory / name).read_text(encoding="utf-8")
            self.assertIn('test.requireConfig("wifiSsid", "wifiPassword")', source)
            self.assertIn("wifi.connect(cfg.wifiSsid", source)
            self.assertIn("wifi.scan({", source)

    def test_hardware_throughput_records_rates_drops_bytes_and_memory(self):
        hardware_test = (
            ROOT
            / "tests/js/flash_data/modules/wifi_csi/throughput-hardware.js"
        ).read_text(encoding="utf-8")

        self.assertIn("callbackRateHz", hardware_test)
        self.assertIn("acceptedRateHz", hardware_test)
        self.assertIn("batchTransportBytesPerSecond", hardware_test)
        self.assertIn("dropRatio", hardware_test)
        self.assertIn("sys.status.memory", hardware_test)

    def test_hardware_batch_receives_respect_the_build_context_limit(self):
        directory = ROOT / "tests/js/flash_data/modules/wifi_csi"
        for name in (
            "throughput-hardware.js",
            "soak-hardware.js",
            "tls-coexistence-hardware.js",
        ):
            source = (directory / name).read_text(encoding="utf-8")
            self.assertIn("caps.limits.maxBatchFrames", source)
            self.assertIn("maximumFrames: batchFrames", source)
            self.assertNotIn("session.receiveBatch(16", source)

    def test_hardware_soak_tracks_memory_floor_without_retaining_batches(self):
        hardware_test = (
            ROOT
            / "tests/js/flash_data/modules/wifi_csi/soak-hardware.js"
        ).read_text(encoding="utf-8")

        self.assertIn("csiSoakDurationMs", hardware_test)
        self.assertIn("cfg.csiSoakDurationMs <= 3600000", hardware_test)
        self.assertIn("minimumInternalLargest", hardware_test)
        self.assertIn("minimumDmaLargest", hardware_test)
        self.assertIn("batch.close()", hardware_test)
        self.assertIn("stats.leasedFrames", hardware_test)

    def test_associated_hardware_uses_explicit_router_traffic(self):
        associated = (
            ROOT
            / "tests/js/flash_data/modules/wifi_csi/associated-hardware.js"
        ).read_text(encoding="utf-8")

        self.assertIn("preTrafficFrames", associated)
        self.assertIn("trafficResponseBytes", associated)
        self.assertIn("while ((frame = session.receive(0)) !== null)", associated)
        self.assertNotIn("if (frame === null)", associated)
        self.assertLess(
            associated.index("trafficClient.send"),
            associated.index("frame = session.receive(8000)"),
        )

    def test_hardware_coexistence_covers_ble_tls_and_espnow_policy(self):
        directory = ROOT / "tests/js/flash_data/modules/wifi_csi"
        ble = (directory / "ble-coexistence-hardware.js").read_text(
            encoding="utf-8"
        )
        tls = (directory / "tls-coexistence-hardware.js").read_text(
            encoding="utf-8"
        )
        espnow = (directory / "espnow-conflict-hardware.js").read_text(
            encoding="utf-8"
        )

        self.assertIn("adapter.scan", ble)
        self.assertIn("session.receive", ble)
        self.assertIn('socket.openTCP({ tls: true })', tls)
        self.assertIn("session.receiveBatch", tls)
        self.assertIn("espNow.open", espnow)
        self.assertIn("WIFI_CSI_RADIO_CONFLICT", espnow)
        self.assertIn(
            "wifi.status().radio.clients.espNow, 0", espnow
        )
        self.assertIn(
            'channel: espnowChannel,\n      conflict: "fail"', espnow
        )

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
        self.assertIn("fixed_channel_owner", radio)
        self.assertIn("lease->identity", radio)
        self.assertIn("wifi_radio_validate_regulatory_channel", radio)
        self.assertIn("promiscuous_claimed", radio)
        self.assertIn("promiscuous_client", radio)
        self.assertIn("esp_wifi_sta_get_ap_info", radio)
        self.assertIn("esp_wifi_get_config(WIFI_IF_AP", radio)

    def test_hardware_lab_explicitly_enables_csi_qualification(self):
        workflow = (ROOT / ".github/workflows/hardware-lab.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("--csi-hardware", workflow)
        self.assertIn("--csi-soak", workflow)
        self.assertIn("TEST_JS_CONFIG", workflow)
        self.assertIn('"csiSoakDurationMs":3600000', workflow)
        self.assertIn("scripts/hardware_lab_evidence.py", workflow)
        self.assertIn("CSI_ROUTER_LABEL", workflow)
        self.assertIn("CSI_PEER_LABEL", workflow)
        self.assertIn("CSI_CHANNEL_BAND", workflow)


if __name__ == "__main__":
    unittest.main()
