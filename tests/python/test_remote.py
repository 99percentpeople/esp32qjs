import importlib.util
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
REMOTE_PATH = ROOT / "scripts" / "remote.py"
SPEC = importlib.util.spec_from_file_location("esp32qjs_remote_test", REMOTE_PATH)
REMOTE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = REMOTE
SPEC.loader.exec_module(REMOTE)


class RemoteConfigTests(unittest.TestCase):
    def config(self):
        args, mcu, context = REMOTE.parse_args(["show-config"])
        return REMOTE.build_project_config(args, mcu, context)

    def test_component_dependency_locks_are_target_specific(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            'idf_build_set_property(DEPENDENCIES_LOCK "dependencies.lock.${IDF_TARGET}")',
            cmake,
        )
        self.assertFalse((ROOT / "dependencies.lock").exists())
        for target in ("esp32c3", "esp32c5", "esp32s3"):
            lock = (ROOT / f"dependencies.lock.{target}").read_text(encoding="utf-8")
            self.assertIn(f"target: {target}\n", lock)

    def test_default_context_is_fixed_layout_and_stays_under_build_root(self):
        config = self.config()
        context = ROOT / "tests" / "build-contexts" / "esp32s3"

        self.assertEqual(config.build_dir, ROOT / "build" / "esp32s3")
        self.assertEqual(config.build_context_dir, context)
        self.assertEqual(config.build_context_flash_data_dir, context / "flash_data")
        self.assertEqual(config.partition_table, context / "partitions.csv")
        self.assertEqual(config.profile_constants_file, context / "profile-constants.inc")
        self.assertEqual(
            [entry for entry in config.cmake_cache_entries if entry.startswith("-DESP32QJS_")],
            [
                "-DESP32QJS_MCU=esp32s3",
                f"-DESP32QJS_MCU_SDKCONFIG_DEFAULTS={ROOT / 'configs/mcus/esp32s3/sdkconfig.defaults'}",
                f"-DESP32QJS_BUILD_CONTEXT_DIR={context}",
            ],
        )
        self.assertFalse(any("APP_" in entry or "FLASH_DATA_DIR" in entry for entry in config.cmake_cache_entries))
        self.assertEqual(
            REMOTE.resolve_build_path("scratch/profile"),
            ROOT / "build" / "scratch" / "profile",
        )

    def test_external_build_directory_requires_explicit_opt_in(self):
        with tempfile.TemporaryDirectory() as temp_name:
            external = str(Path(temp_name) / "firmware-build")
            args, mcu, context = REMOTE.parse_args(
                ["--build-dir", external, "show-config"]
            )
            with self.assertRaisesRegex(SystemExit, "--allow-external-build-dir"):
                REMOTE.build_project_config(args, mcu, context)

            args, mcu, context = REMOTE.parse_args(
                [
                    "--build-dir",
                    external,
                    "--allow-external-build-dir",
                    "show-config",
                ]
            )
            config = REMOTE.build_project_config(args, mcu, context)
            self.assertTrue(config.allow_external_build_dir)

    def test_build_cleanup_is_limited_to_build_root_without_opt_in(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp_name:
            path = Path(temp_name)
            with self.assertRaisesRegex(SystemExit, "unsafe build directory"):
                REMOTE.safe_remove_build_dir(path, allow_external=False)
            self.assertTrue(path.exists())
        for path in (Path(path.anchor), Path.home(), ROOT, ROOT / "build"):
            with self.subTest(path=path), self.assertRaisesRegex(
                SystemExit, "unsafe build directory"
            ):
                REMOTE.safe_remove_build_dir(path, allow_external=True)

    def test_esptool_configuration_is_project_local(self):
        with tempfile.TemporaryDirectory() as temp_name:
            config_path = Path(temp_name) / "tooling" / "esptool.cfg"
            with (
                patch.object(REMOTE, "ESPTOOL_CONFIG_PATH", config_path),
                patch.dict(os.environ, {}, clear=False),
            ):
                os.environ.pop("ESPTOOL_CFGFILE", None)
                written = REMOTE.write_esptool_config()
                self.assertEqual(written, config_path)
                self.assertEqual(os.environ["ESPTOOL_CFGFILE"], str(config_path))
                self.assertEqual(config_path.read_text(encoding="ascii"), REMOTE.ESPTOOL_CONFIG_TEXT)

    def test_idf_actions_install_the_project_local_esptool_config(self):
        source = REMOTE_PATH.read_text(encoding="utf-8")
        action_start = source.index("def run_idf_action_with_stale_build_recovery(")
        action_end = source.index("\ndef build(", action_start)

        self.assertIn("write_esptool_config()", source[action_start:action_end])

    def test_rfc2217_stop_is_pid_scoped(self):
        source = REMOTE_PATH.read_text(encoding="utf-8")
        stop_start = source.index("def stop_server_processes()")
        stop_end = source.index("\ndef start_server(", stop_start)
        stop = source[stop_start:stop_end]

        self.assertIn("read_server_pid()", stop)
        self.assertIn("os.kill(pid, signal.SIGTERM)", stop)
        self.assertNotIn("pkill", stop)

    def test_explicit_context_drives_mcu_and_rejects_incomplete_input(self):
        with tempfile.TemporaryDirectory() as temp_name:
            context = Path(temp_name) / "context"
            context.mkdir()
            with self.assertRaisesRegex(SystemExit, "Build Context is incomplete"):
                REMOTE.load_build_context(str(context))

        selected = REMOTE.load_build_context(
            str(ROOT / "tests" / "build-contexts" / "esp32s3")
        )
        self.assertEqual(selected.mcu, "esp32s3")
        self.assertEqual(selected.flash_size_mb, 8)
        self.assertEqual(selected.psram_mode, "none")

    def test_check_js_includes_the_context_flash_tree(self):
        selected = ROOT / "tests" / "build-contexts" / "esp32s3" / "flash_data"
        with patch.object(REMOTE, "run_streaming", return_value=(0, "")) as streaming:
            REMOTE.run_js_syntax_check(selected)
        command = streaming.call_args.args[0]
        self.assertEqual(command[-2:], ["--extra-path", str(selected)])

    def test_wireless_hardware_e2e_is_opt_in_and_separate(self):
        args, _, _ = REMOTE.parse_args(
            ["test", "--scope", "js", "--module", "espnow",
             "--wireless-hardware"]
        )
        module = REMOTE.resolve_js_modules(["espnow"])[0]
        hardware_cases = [
            case for case in module.cases
            if "wireless-hardware" in case.required_capabilities
        ]

        self.assertEqual(
            REMOTE.resolve_js_test_capabilities(args),
            {"wireless-hardware"},
        )
        self.assertEqual(len(hardware_cases), 1)
        self.assertTrue(hardware_cases[0].path.endswith("-hardware.js"))
        self.assertEqual(
            REMOTE.JS_TEST_CAPABILITY_FLAGS["wireless-hardware"],
            "--wireless-hardware",
        )

    def test_csi_hardware_matrix_is_opt_in_and_separate(self):
        args, _, _ = REMOTE.parse_args(
            ["test", "--scope", "js", "--module", "wifi_csi",
             "--csi-hardware"]
        )
        module = REMOTE.resolve_js_modules(["wifi_csi"])[0]
        hardware_cases = [
            case for case in module.cases
            if "csi-hardware" in case.required_capabilities
        ]

        self.assertEqual(
            REMOTE.resolve_js_test_capabilities(args),
            {"csi-hardware"},
        )
        self.assertEqual(len(hardware_cases), 7)
        self.assertTrue(all(case.path.endswith("-hardware.js")
                            for case in hardware_cases))
        self.assertTrue(all(case.record_details for case in hardware_cases))
        self.assertEqual(
            REMOTE.JS_TEST_CAPABILITY_FLAGS["csi-hardware"],
            "--csi-hardware",
        )

    def test_csi_long_soak_requires_a_second_explicit_capability(self):
        args, _, _ = REMOTE.parse_args(
            ["test", "--scope", "js", "--module", "wifi_csi",
             "--csi-hardware", "--csi-soak"]
        )
        module = REMOTE.resolve_js_modules(["wifi_csi"])[0]
        soak = [case for case in module.cases if "csi-soak" in case.required_capabilities]

        self.assertEqual(
            REMOTE.resolve_js_test_capabilities(args),
            {"csi-hardware", "csi-soak"},
        )
        self.assertEqual(len(soak), 1)
        self.assertEqual(
            soak[0].required_capabilities,
            ("csi-hardware", "csi-soak"),
        )
        self.assertTrue(soak[0].record_details)
        self.assertGreaterEqual(soak[0].timeout_seconds, 3660.0)
        self.assertEqual(
            REMOTE.JS_TEST_CAPABILITY_FLAGS["csi-soak"],
            "--csi-soak",
        )

    def test_camera_csi_coexistence_requires_both_hardware_capabilities(self):
        args, _, _ = REMOTE.parse_args(
            ["test", "--scope", "js", "--module", "wifi_csi_camera",
             "--csi-hardware", "--media-hardware"]
        )
        module = REMOTE.resolve_js_modules(["wifi_csi_camera"])[0]

        self.assertEqual(
            REMOTE.resolve_js_test_capabilities(args),
            {"csi-hardware", "media-hardware"},
        )
        self.assertEqual(len(module.cases), 1)
        self.assertEqual(
            module.cases[0].required_capabilities,
            ("csi-hardware", "media-hardware"),
        )
        self.assertTrue(module.cases[0].record_details)

    def test_csi_repl_module_does_not_require_mutually_exclusive_usb_serial(self):
        module = REMOTE.resolve_js_modules(["wifi_csi"])[0]

        self.assertNotIn("usbSerial", module.required_features)
        self.assertIn("wifiCsi", module.required_features)
        self.assertIn("rpc", module.required_features)

    def test_csi_coexistence_modules_require_their_real_dependencies(self):
        expected = {
            "wifi_csi_ble": (
                ("wifiCsi", "wifi", "ble"),
                ("csi-hardware",),
            ),
            "wifi_csi_tls": (
                ("wifiCsi", "wifi", "socket", "tls"),
                ("csi-hardware", "network"),
            ),
            "wifi_csi_espnow": (
                ("wifiCsi", "wifi", "espNow"),
                ("csi-hardware",),
            ),
        }

        for module_name, (features, capabilities) in expected.items():
            with self.subTest(module=module_name):
                module = REMOTE.resolve_js_modules([module_name])[0]
                self.assertEqual(module.required_features, features)
                self.assertEqual(len(module.cases), 1)
                self.assertEqual(module.cases[0].required_capabilities, capabilities)
                self.assertTrue(module.cases[0].record_details)

    def test_espnow_modules_use_the_public_runtime_feature_key(self):
        runtime_features = {
            "fs": True,
            "wifiCsi": True,
            "wifi": True,
            "espNow": True,
        }

        for module_name in ("espnow", "wifi_csi_espnow"):
            with self.subTest(module=module_name):
                module = REMOTE.resolve_js_modules([module_name])[0]
                self.assertIn("espNow", module.required_features)
                enabled, note = REMOTE.resolve_js_modules_for_runtime(
                    (module,), runtime_features, True
                )
                self.assertEqual(enabled, (module,))
                self.assertEqual(note, "")

    def test_csi_hardware_pass_keeps_bounded_structured_evidence(self):
        case = REMOTE.JsTestCase(
            "modules/wifi_csi/lifecycle-hardware.js",
            record_details=True,
        )
        session = SimpleNamespace(process=SimpleNamespace(poll=lambda: None))
        output = StringIO()
        payload = (
            b'__TEST_PASS__:{"name":"wifi_csi/lifecycle-hardware",'
            b'"details":{"cycles":500,"internalFreeDelta":-128}}\n'
        )

        with (
            patch.object(REMOTE, "send_js_command"),
            patch.object(REMOTE, "read_monitor_chunk", return_value=payload),
            patch.object(REMOTE.time, "monotonic", side_effect=(0.0, 0.0, 0.0, 0.3)),
            redirect_stdout(output),
        ):
            result = REMOTE.run_js_test_case(session, case)

        self.assertEqual(
            result.details,
            {"cycles": 500, "internalFreeDelta": -128},
        )
        self.assertIn(
            'details={"cycles":500,"internalFreeDelta":-128}',
            output.getvalue(),
        )
        bounded = REMOTE.format_js_case_details({"payload": "x" * 5000})
        self.assertLessEqual(
            len(bounded.encode("utf-8")),
            REMOTE.JS_TEST_DETAILS_MAX_BYTES,
        )
        self.assertEqual(
            json.loads(bounded),
            {"bytes": 5014, "truncated": True},
        )

    def test_js_case_command_retries_once_when_start_marker_is_missing(self):
        case = REMOTE.JsTestCase("modules/core/eval.js", timeout_seconds=5.0)
        session = SimpleNamespace(process=SimpleNamespace(poll=lambda: None))
        payload = b'__TEST_PASS__:{"name":"core/eval"}\n'

        with (
            patch.object(REMOTE, "send_js_command") as send,
            patch.object(REMOTE, "read_monitor_chunk", side_effect=(b"", payload)),
            patch.object(
                REMOTE.time,
                "monotonic",
                side_effect=(0.0, 2.1, 2.1, 2.1, 2.4),
            ),
        ):
            result = REMOTE.run_js_test_case(session, case)

        self.assertEqual(result.status, "passed")
        self.assertEqual(send.call_count, 2)
        first_command = send.call_args_list[0].args[1]
        self.assertIn(REMOTE.JS_TEST_CASE_START_PREFIX, first_command)
        self.assertEqual(send.call_args_list[1].args[1], first_command)

    def test_js_test_build_creates_a_real_context_and_selected_tree(self):
        config = self.config()
        modules = REMOTE.resolve_js_modules(["bitmap", "camera-bitmap"])
        test_config = REMOTE.js_test_build_config(
            config,
            modules=modules,
            explicit_module_selection=True,
        )
        context = test_config.build_context_dir
        self.assertTrue((context / "manifest.json").is_file())
        self.assertTrue((context / "precompile.json").is_file())
        self.assertTrue((context / "flash_data/_test/harness.js").is_file())
        self.assertTrue((context / "flash_data/modules/bitmap/basic.js").is_file())
        self.assertTrue((context / "flash_data/modules/camera/bitmap-hardware.js").is_file())
        self.assertFalse((context / "flash_data/modules/wifi").exists())
        defaults = (context / "sdkconfig.defaults").read_text(encoding="utf-8")
        self.assertIn("CONFIG_ESP32_MQUICKJS_DEBUG_GC=y", defaults)
        self.assertNotIn("# CONFIG_ESP32_MQUICKJS_DEBUG_GC is not set", defaults)
        self.assertIn("CONFIG_ESP32QJS_JS_HEAP_SIZE=106496", defaults)
        self.assertIn('CONFIG_ESP32_MQUICKJS_PSRAM_MODE="none"', defaults)
        self.assertIn("CONFIG_SPIRAM=n", defaults)
        self.assertNotIn("CONFIG_SPIRAM=y", defaults)
        self.assertNotIn("CONFIG_SPIRAM_MODE_OCT=y", defaults)
        self.assertIn("CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=2", defaults)
        self.assertIn("CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=2", defaults)
        self.assertIn("CONFIG_ESP_WIFI_RX_BA_WIN=2", defaults)
        self.assertIn("CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=8", defaults)
        self.assertIn("CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=8", defaults)
        self.assertIn("CONFIG_ESP_WIFI_MGMT_SBUF_NUM=8", defaults)
        self.assertIn("CONFIG_ESP32QJS_SECONDARY_LITTLEFS=y", defaults)
        self.assertEqual(
            [
                line
                for line in defaults.splitlines()
                if "CONFIG_ESP32QJS_LITTLEFS_FORMAT_ON_MOUNT_FAIL" in line
            ][-1],
            "CONFIG_ESP32QJS_LITTLEFS_FORMAT_ON_MOUNT_FAIL=y",
        )
        self.assertIn(
            'CONFIG_ESP32QJS_SECONDARY_LITTLEFS_PARTITION_LABEL="workspace"',
            defaults,
        )
        self.assertIn(
            'CONFIG_ESP32QJS_SECONDARY_LITTLEFS_BASE_PATH="/workspace"',
            defaults,
        )
        self.assertEqual(
            json.loads((context / "precompile.json").read_text(encoding="utf-8"))["inline"],
            ["_test/harness.js"],
        )
        self.assertEqual(
            [entry for entry in test_config.cmake_cache_entries if entry.startswith("-DESP32QJS_BUILD_CONTEXT_DIR=")],
            [f"-DESP32QJS_BUILD_CONTEXT_DIR={context}"],
        )

    def test_js_test_build_disables_debug_gc_only_for_psram_contexts(self):
        config = self.config()
        with tempfile.TemporaryDirectory() as temp_name:
            product_defaults = Path(temp_name) / "sdkconfig.defaults"
            product_defaults.write_text(
                'CONFIG_ESP32_MQUICKJS_PSRAM_MODE="octal"\n'
                "CONFIG_SPIRAM=y\n"
                "CONFIG_SPIRAM_MODE_OCT=y\n"
                "CONFIG_ESP32QJS_JS_HEAP_SIZE=4194304\n",
                encoding="utf-8",
            )
            config = REMOTE.replace(
                config,
                psram_mode="octal",
                psram_size_bytes=8 * 1024 * 1024,
                build_context_sdkconfig_defaults=product_defaults,
            )
            test_config = REMOTE.js_test_build_config(config)

        defaults = test_config.build_context_sdkconfig_defaults.read_text(
            encoding="utf-8",
        )
        self.assertIn('CONFIG_ESP32_MQUICKJS_PSRAM_MODE="octal"', defaults)
        self.assertIn("CONFIG_SPIRAM=y", defaults)
        self.assertIn("CONFIG_SPIRAM_MODE_OCT=y", defaults)
        self.assertIn("CONFIG_ESP32QJS_JS_HEAP_SIZE=4194304", defaults)
        self.assertNotIn("CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=2", defaults)
        self.assertEqual(
            [
                line
                for line in defaults.splitlines()
                if "CONFIG_ESP32_MQUICKJS_DEBUG_GC" in line
            ][-1],
            "# CONFIG_ESP32_MQUICKJS_DEBUG_GC is not set",
        )

    def test_js_test_build_forces_serial_observability_for_product_contexts(self):
        config = self.config()
        with tempfile.TemporaryDirectory() as temp_name:
            product_defaults = Path(temp_name) / "sdkconfig.defaults"
            product_defaults.write_text(
                "# CONFIG_ESP32QJS_ENABLE_REPL is not set\n"
                "CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS=y\n",
                encoding="utf-8",
            )
            config = REMOTE.replace(
                config,
                build_context_sdkconfig_defaults=product_defaults,
            )
            test_config = REMOTE.js_test_build_config(config)

        defaults = test_config.build_context_sdkconfig_defaults.read_text(
            encoding="utf-8",
        )
        self.assertEqual(
            [
                line
                for line in defaults.splitlines()
                if "CONFIG_ESP32QJS_ENABLE_REPL" in line
            ][-1],
            "CONFIG_ESP32QJS_ENABLE_REPL=y",
        )
        self.assertEqual(
            [
                line
                for line in defaults.splitlines()
                if "CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS" in line
            ][-1],
            "# CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS is not set",
        )

    def test_js_tests_flash_and_monitor_the_instrumented_context(self):
        config = self.config()
        session = object()
        with (
            patch.object(REMOTE, "flash") as flash,
            patch.object(REMOTE, "start_monitor_session", return_value=session) as start,
            patch.object(REMOTE, "read_monitor_until_text", return_value="ready"),
            patch.object(REMOTE, "wait_for_optional_js_repl_banner"),
            patch.object(REMOTE, "ensure_js_test_runtime"),
            patch.object(REMOTE, "probe_js_runtime_features", return_value={"fs": True}),
            patch.object(REMOTE, "close_monitor_session") as close,
        ):
            REMOTE.run_js_tests(config, (), False, set(), True, False)
        flashed = flash.call_args.args[0]
        self.assertTrue((flashed.build_context_dir / "precompile.json").is_file())
        start.assert_called_once_with(flashed)
        close.assert_called_once_with(session)

    def test_monitor_no_reset_and_workspace_flash_semantics_remain_explicit(self):
        config = self.config()
        config = REMOTE.replace(config, target="/dev/ttyACM0")
        with (
            patch.object(REMOTE, "write_monitor_config"),
            patch.object(
                REMOTE,
                "idf_py_cmd",
                side_effect=lambda project_args, _config: project_args,
            ),
        ):
            self.assertIn(
                "monitor --no-reset",
                " ".join(REMOTE.monitor_cmd(config, no_reset=True)),
            )
        with (
            patch.object(REMOTE, "write_esptool_config"),
            patch.object(REMOTE, "load_flasher_args", return_value={}),
            patch.object(REMOTE, "resolve_flash_pairs", return_value=["0x0", "app.bin"]) as resolve,
            patch.object(REMOTE, "write_flash"),
        ):
            REMOTE.flash(config, build_first=False)
            self.assertEqual(resolve.call_args.kwargs["exclude_entries"], ("workspace",))
            REMOTE.flash(config, build_first=False, initialize_workspace=True)
            self.assertEqual(resolve.call_args.kwargs["exclude_entries"], ())

    def test_future_case_and_serial_helpers_keep_their_contracts(self):
        case = REMOTE.JS_TEST_MODULE_MAP["future"].cases[0]
        self.assertTrue(case.reset_before)
        self.assertTrue(case.reset_after)
        self.assertIsNone(REMOTE.find_last_output_line_with_prefix("__MARK__:r", "__MARK__:", complete_only=True))
        self.assertEqual(
            REMOTE.find_last_output_line_with_prefix("__MARK__:ready\r\njs> ", "__MARK__:", complete_only=True),
            "__MARK__:ready",
        )
        self.assertIsNone(REMOTE.merged_optional_int({"PORT": "  "}, {}, "PORT"))
        self.assertEqual(REMOTE.merged_optional_int({"PORT": " 42 "}, {}, "PORT"), 42)


if __name__ == "__main__":
    unittest.main()
