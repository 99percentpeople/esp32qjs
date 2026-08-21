import importlib.util
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
REMOTE_PATH = ROOT / "scripts" / "remote.py"
SPEC = importlib.util.spec_from_file_location("esp32qjs_remote_test", REMOTE_PATH)
REMOTE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = REMOTE
SPEC.loader.exec_module(REMOTE)


class RemoteConfigTests(unittest.TestCase):
    def test_component_dependency_locks_are_target_specific(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            'idf_build_set_property(DEPENDENCIES_LOCK "dependencies.lock.${IDF_TARGET}")',
            cmake,
        )
        self.assertFalse((ROOT / "dependencies.lock").exists())
        for target in ("esp32c3", "esp32s3"):
            lock = (ROOT / f"dependencies.lock.{target}").read_text(encoding="utf-8")
            self.assertIn(f"target: {target}\n", lock)

    def test_generated_build_outputs_stay_under_the_build_root(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32s3",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, mcu, app)
        self.assertEqual(config.build_dir, ROOT / "build" / "esp32s3")
        self.assertEqual(
            REMOTE.js_test_build_config(config).build_dir,
            ROOT / "build" / "esp32s3" / "minimal-js-test",
        )
        self.assertEqual(
            REMOTE.resolve_build_path("scratch/profile"),
            ROOT / "build" / "scratch" / "profile",
        )
        self.assertEqual(
            REMOTE.resolve_build_path("build/esp32c3"),
            ROOT / "build" / "esp32c3",
        )

    def write_external_app(self, root: Path, app_id: str = "external_agent") -> Path:
        app_dir = root / "device"
        (app_dir / "flash_data").mkdir(parents=True)
        (app_dir / "partitions").mkdir()
        (app_dir / "app.env").write_text(
            "\n".join(
                (
                    f"APP_ID={app_id}",
                    "APP_LABEL=External Agent",
                    "FLASH_DATA_DIR=flash_data",
                    "APP_SDKCONFIG_DEFAULTS=sdkconfig.defaults",
                    "PARTITION_TABLE=partitions/{mcu}.csv",
                    "",
                )
            ),
            encoding="utf-8",
        )
        (app_dir / "flash_data" / "index.js").write_text(
            'print("external agent");\n', encoding="utf-8"
        )
        (app_dir / "sdkconfig.defaults").write_text(
            "CONFIG_ESP32QJS_AUTORUN_INDEX_JS=y\n", encoding="utf-8"
        )
        (app_dir / "partitions" / "esp32s3.csv").write_text(
            "storage,data,littlefs,,0x100000,\n", encoding="utf-8"
        )
        return app_dir

    def test_check_js_command_is_available(self):
        args, _, _ = REMOTE.parse_args(["check-js"])

        self.assertEqual(args.command, "check-js")
        self.assertTrue(REMOTE.JS_SYNTAX_CHECK_SCRIPT.is_file())

    def test_js_syntax_check_includes_selected_application(self):
        selected = ROOT / "apps" / "minimal" / "flash_data"
        with patch.object(REMOTE, "run_streaming", return_value=(0, "")) as streaming:
            REMOTE.run_js_syntax_check(selected)

        command = streaming.call_args.args[0]
        self.assertEqual(command[-2:], ["--extra-path", str(selected)])
        self.assertEqual(streaming.call_args.kwargs["cwd"], ROOT)

    def test_bundled_application_profiles_are_valid(self):
        minimal = REMOTE.load_app_profile(
            "minimal", "esp32s3", "esp32s3"
        )
        demo = REMOTE.load_app_profile("demo", "esp32s3", "esp32s3")

        self.assertEqual(minimal.name, "minimal")
        self.assertEqual(minimal.directory, ROOT / "apps" / "minimal")
        self.assertTrue((minimal.flash_data_dir / "index.js").is_file())
        self.assertTrue(minimal.sdkconfig_defaults.is_file())
        self.assertEqual(minimal.partition_layout, "storage")
        self.assertEqual(demo.name, "demo")
        self.assertTrue((demo.flash_data_dir / "index.js").is_file())
        self.assertTrue(demo.sdkconfig_defaults.is_file())
        self.assertEqual(demo.partition_layout, "storage")

    def test_app_selection_reaches_cmake(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32s3",
            "--app", "demo",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, mcu, app)

        self.assertEqual(config.app, "demo")
        self.assertIn("-DESP32QJS_MCU=esp32s3", config.cmake_cache_entries)
        self.assertIn("-DESP32QJS_APP=demo", config.cmake_cache_entries)
        self.assertEqual(config.app_profile_dir, app.directory)
        self.assertIn(
            f"-DESP32QJS_APP_PROFILE_DIR={app.directory}",
            config.cmake_cache_entries,
        )
        self.assertIn(
            f"-DESP32QJS_APP_FLASH_DATA_DIR={app.flash_data_dir}",
            config.cmake_cache_entries,
        )
        self.assertIn(
            f"-DESP32QJS_APP_SDKCONFIG_DEFAULTS={app.sdkconfig_defaults}",
            config.cmake_cache_entries,
        )
        self.assertIn(
            f"-DESP32QJS_PARTITION_TABLE={config.partition_table}",
            config.cmake_cache_entries,
        )
        self.assertEqual(
            config.sdkconfig_defaults,
            (mcu.sdkconfig_defaults, app.sdkconfig_defaults, config.hardware_sdkconfig_defaults),
        )
        self.assertEqual(
            config.generated_sdkconfig.name,
            "sdkconfig.esp32s3.demo",
        )
        self.assertIn("-DESP32QJS_FLASH_DATA_DIR=", config.cmake_cache_entries)

    def test_external_application_profile_is_resolved_by_path(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            app_dir = self.write_external_app(Path(temp_dir))
            args, mcu, app = REMOTE.parse_args([
                "--mcu", "esp32s3",
                "--app", str(app_dir),
                "show-config",
            ])
            config = REMOTE.build_project_config(args, mcu, app)

            self.assertEqual(app.name, "external_agent")
            self.assertEqual(app.directory, app_dir.resolve())
            self.assertEqual(config.app, "external_agent")
            self.assertEqual(config.app_profile_dir, app_dir.resolve())
            self.assertEqual(config.app_flash_data_dir, app_dir / "flash_data")
            self.assertEqual(
                config.generated_sdkconfig.name,
                "sdkconfig.esp32s3.external_agent",
            )
            self.assertIn(
                f"-DESP32QJS_APP_PROFILE_DIR={app_dir.resolve()}",
                config.cmake_cache_entries,
            )

            output = io.StringIO()
            with redirect_stdout(output):
                REMOTE.list_apps(str(app_dir))
            self.assertIn("* external_agent: External Agent", output.getvalue())
            self.assertIn("[external:", output.getvalue())

    def test_external_app_sdkconfig_can_track_the_selected_mcu(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            app_dir = self.write_external_app(Path(temp_dir))
            app_file = app_dir / "app.env"
            app_file.write_text(
                app_file.read_text(encoding="utf-8").replace(
                    "APP_SDKCONFIG_DEFAULTS=sdkconfig.defaults",
                    "APP_SDKCONFIG_DEFAULTS=sdkconfig.{mcu}.defaults",
                ),
                encoding="utf-8",
            )
            (app_dir / "sdkconfig.defaults").rename(
                app_dir / "sdkconfig.esp32s3.defaults"
            )
            (app_dir / "sdkconfig.esp32c3.defaults").write_text(
                "CONFIG_ESP32QJS_JS_HEAP_SIZE=122880\n",
                encoding="utf-8",
            )
            (app_dir / "partitions" / "esp32c3.csv").write_text(
                "storage,data,littlefs,,0x100000,\n", encoding="utf-8"
            )

            s3 = REMOTE.load_app_profile(str(app_dir), "esp32s3", "esp32s3")
            c3 = REMOTE.load_app_profile(
                str(app_dir), "esp32c3", "esp32c3"
            )

            self.assertEqual(s3.sdkconfig_defaults.name,
                             "sdkconfig.esp32s3.defaults")
            self.assertEqual(c3.sdkconfig_defaults.name,
                             "sdkconfig.esp32c3.defaults")

    def test_app_file_environment_selects_external_profile(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            app_dir = self.write_external_app(Path(temp_dir))
            with patch.dict(os.environ, {"APP_FILE": str(app_dir)}, clear=True):
                app = REMOTE.load_app_profile(None, "esp32s3", "esp32s3")
                self.assertEqual(app.name, "external_agent")
                self.assertEqual(app.directory, app_dir.resolve())

                overridden = REMOTE.load_app_profile(
                    "minimal", "esp32s3", "esp32s3"
                )
                self.assertEqual(overridden.name, "minimal")

    def test_invalid_external_app_id_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            app_dir = self.write_external_app(Path(temp_dir), app_id="bad app")
            with self.assertRaisesRegex(SystemExit, "APP_ID"):
                REMOTE.load_app_profile(str(app_dir), "esp32s3", "esp32s3")

    def test_js_test_build_enables_debug_gc(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32s3",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, mcu, app)
        test_config = REMOTE.js_test_build_config(config)

        self.assertEqual(test_config.flash_data_override, REMOTE.JS_TEST_FLASH_DATA_DIR)
        self.assertIn(
            REMOTE.JS_TEST_SDKCONFIG_DEFAULTS,
            test_config.sdkconfig_defaults,
        )
        self.assertEqual(
            test_config.sdkconfig_defaults[-1].name,
            "sdkconfig.esp32s3.defaults",
        )
        self.assertIn(
            "CONFIG_ESP32QJS_JS_HEAP_SIZE=524288",
            test_config.sdkconfig_defaults[-1].read_text(),
        )
        self.assertIn(
            "# CONFIG_ESP32_MQUICKJS_DEBUG_GC is not set",
            test_config.sdkconfig_defaults[-1].read_text(),
        )
        test_defaults = REMOTE.JS_TEST_SDKCONFIG_DEFAULTS.read_text()
        self.assertIn("CONFIG_ESP32_MQUICKJS_DEBUG_GC=y", test_defaults)
        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET=y", test_defaults)
        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET=y", test_defaults)
        self.assertIn("socket", REMOTE.JS_TEST_MODULE_MAP)
        self.assertIn("websocket", REMOTE.JS_TEST_MODULE_MAP)
        self.assertEqual(
            REMOTE.JS_TEST_MODULE_MAP["camera-bitmap"].required_features,
            ("camera", "bitmap"),
        )
        self.assertEqual(
            REMOTE.JS_TEST_MODULE_MAP["camera-bitmap"].cases[0].required_capabilities,
            ("media-hardware",),
        )
        self.assertIn(
            "-DESP32QJS_FLASH_DATA_INCLUDE_SHARED=ON",
            test_config.cmake_cache_entries,
        )

    def test_explicit_js_modules_stage_only_selected_test_data(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32s3",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, mcu, app)
        modules = REMOTE.resolve_js_modules(["bitmap", "camera-bitmap"])
        test_config = REMOTE.js_test_build_config(
            config,
            modules=modules,
            explicit_module_selection=True,
        )

        staged = test_config.flash_data_override
        self.assertIsNotNone(staged)
        self.assertTrue((staged / "index.js").is_file())
        self.assertTrue((staged / "_test" / "harness.js").is_file())
        self.assertTrue((staged / "modules" / "bitmap" / "basic.js").is_file())
        self.assertTrue(
            (staged / "modules" / "camera" / "bitmap-hardware.js").is_file()
        )
        self.assertFalse((staged / "modules" / "wifi").exists())

    def test_c3_js_test_build_keeps_the_mcu_heap_budget(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32c3",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, mcu, app)
        test_config = REMOTE.js_test_build_config(config)

        self.assertEqual(
            test_config.sdkconfig_defaults[-1],
            REMOTE.JS_TEST_SDKCONFIG_DEFAULTS,
        )
        self.assertFalse(
            any(path.name == "sdkconfig.esp32c3.defaults"
                for path in test_config.sdkconfig_defaults)
        )

    def test_js_tests_flash_and_monitor_the_instrumented_build(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32s3",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, mcu, app)
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

        flashed_config = flash.call_args.args[0]
        self.assertEqual(
            flashed_config.build_dir,
            ROOT / "build" / "esp32s3" / "minimal-js-test",
        )
        self.assertIn(
            REMOTE.JS_TEST_SDKCONFIG_DEFAULTS,
            flashed_config.sdkconfig_defaults,
        )
        start.assert_called_once_with(flashed_config)
        close.assert_called_once_with(session)

    def test_automated_monitor_session_does_not_reset_usb_serial_jtag(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32s3",
            "--app", "minimal",
            "--target", "/dev/ttyACM0",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, mcu, app)

        command = REMOTE.monitor_cmd(config, no_reset=True)

        rendered = " ".join(command)
        self.assertIn("monitor --no-reset", rendered)

    def test_future_capacity_case_is_reset_isolated(self):
        case = REMOTE.JS_TEST_MODULE_MAP["future"].cases[0]

        self.assertTrue(case.reset_before)
        self.assertTrue(case.reset_after)

    def test_complete_flash_data_override_is_preserved(self):
        override = ROOT / "tests" / "js" / "flash_data"
        args, mcu, app = REMOTE.parse_args([
            "--app", "minimal",
            "--flash-data-dir", str(override),
            "show-config",
        ])
        config = REMOTE.build_project_config(args, mcu, app)

        self.assertEqual(config.flash_data_override, override)
        self.assertIn(
            f"-DESP32QJS_FLASH_DATA_DIR={override}",
            config.cmake_cache_entries,
        )

    def test_partition_profile_tracks_the_selected_mcu(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32c3",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, mcu, app)

        self.assertEqual(config.partition_table.name, "partitions.csv")
        self.assertIn("0x1F0000", config.partition_table.read_text())

    def test_flash_preserves_workspace_by_default(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32s3",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, mcu, app)

        with (
            patch.object(REMOTE, "write_esptool_config"),
            patch.object(REMOTE, "load_flasher_args", return_value={}),
            patch.object(REMOTE, "resolve_flash_pairs", return_value=["0x0", "app.bin"]) as resolve,
            patch.object(REMOTE, "write_flash"),
        ):
            REMOTE.flash(config, build_first=False)

        self.assertEqual(resolve.call_args.kwargs["exclude_entries"], ("workspace",))

    def test_flash_can_explicitly_initialize_workspace(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32s3",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, mcu, app)

        with (
            patch.object(REMOTE, "write_esptool_config"),
            patch.object(REMOTE, "load_flasher_args", return_value={}),
            patch.object(REMOTE, "resolve_flash_pairs", return_value=["0x0", "app.bin"]) as resolve,
            patch.object(REMOTE, "write_flash"),
        ):
            REMOTE.flash(config, build_first=False, initialize_workspace=True)

        self.assertEqual(resolve.call_args.kwargs["exclude_entries"], ())

    def test_workspace_flash_commands_are_explicit(self):
        flash_args, _, _ = REMOTE.parse_args(["flash", "--erase-workspace"])
        workspace_args, _, _ = REMOTE.parse_args(["flash-workspace", "--no-build"])

        self.assertTrue(flash_args.erase_workspace)
        self.assertEqual(workspace_args.command, "flash-workspace")
        self.assertTrue(workspace_args.no_build)

    def test_partition_profile_rejects_unsupported_flash_size(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32c3",
            "--flash-size-mb", "4",
            "show-config",
        ])
        args.flash_size_mb = 2
        with self.assertRaisesRegex(SystemExit, "flash-size-mb"):
            REMOTE.build_project_config(args, mcu, app)

    def test_generated_hardware_profile_uses_memory_and_profile_constants(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            wiring = Path(temp_dir) / "wiring.json"
            wiring.write_text(
                '{"led":{"pin":21,"activeLow":true},"i2c":{"sda":4,"scl":5}}',
                encoding="utf-8",
            )
            build_dir = Path(temp_dir) / "build"
            args, mcu, app = REMOTE.parse_args([
                "--mcu", "esp32s3",
                "--flash-size-mb", "16",
                "--psram-mode", "octal",
                "--psram-size", str(8 * 1024 * 1024),
                "--hardware-constants", str(wiring),
                "--build-dir", str(build_dir),
                "show-config",
            ])
            config = REMOTE.build_project_config(args, mcu, app)
            defaults = config.hardware_sdkconfig_defaults.read_text(encoding="utf-8")
            partitions = config.partition_table.read_text(encoding="utf-8")

            self.assertIn("CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y", defaults)
            self.assertIn('CONFIG_ESP32_MQUICKJS_PSRAM_MODE="octal"', defaults)
            self.assertIn("CONFIG_SPIRAM_MODE_OCT=y", defaults)
            self.assertIn("CONFIG_ESP32QJS_JS_HEAP_SIZE=4194304", defaults)
            constants = config.profile_constants_file.read_text(encoding="utf-8")
            self.assertNotIn("CONFIG_ESP32_MQUICKJS_USER_LED_PIN", defaults)
            self.assertIn('ESP32_MQUICKJS_PROFILE_INT("ESP32QJS_LED_PIN", 21)', constants)
            self.assertIn('ESP32_MQUICKJS_PROFILE_INT("ESP32QJS_I2C_SDA", 4)', constants)
            self.assertIn("0xDF0000", partitions)

    def test_generated_profile_leaves_unconfigured_pins_unset(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args, mcu, app = REMOTE.parse_args([
                "--mcu", "esp32c3",
                "--flash-size-mb", "4",
                "--build-dir", str(Path(temp_dir) / "build"),
                "show-config",
            ])
            config = REMOTE.build_project_config(args, mcu, app)
            defaults = config.hardware_sdkconfig_defaults.read_text(encoding="utf-8")
            self.assertNotIn("CONFIG_SPIRAM", defaults)
            self.assertNotIn("CONFIG_ESP32_MQUICKJS_USER_LED_PIN", defaults)
            self.assertEqual(
                config.profile_constants_file.read_text(encoding="utf-8"),
                "/* Generated hardware-profile constants; do not edit. */\n",
            )

    def test_psram_profiles_are_rejected_for_unsupported_mcus(self):
        args, mcu, app = REMOTE.parse_args([
            "--mcu", "esp32c3",
            "--psram-mode", "quad",
            "--psram-size", str(2 * 1024 * 1024),
            "show-config",
        ])
        with self.assertRaisesRegex(SystemExit, "does not support"):
            REMOTE.build_project_config(args, mcu, app)

    def test_serial_prefix_reader_ignores_partial_lines(self):
        prefix = "__MARK__:"

        self.assertIsNone(
            REMOTE.find_last_output_line_with_prefix(
                "__MARK__:r", prefix, complete_only=True
            )
        )
        self.assertEqual(
            REMOTE.find_last_output_line_with_prefix(
                "__MARK__:ready\r\n\x1b[?25ljs> ",
                prefix,
                complete_only=True,
            ),
            "__MARK__:ready",
        )

    def test_blank_optional_integer_is_unset(self):
        self.assertIsNone(REMOTE.merged_optional_int({"PORT": "  "}, {}, "PORT"))
        self.assertEqual(REMOTE.merged_optional_int({"PORT": " 42 "}, {}, "PORT"), 42)


if __name__ == "__main__":
    unittest.main()
