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
                    "PARTITION_TABLE=partitions/{board}.csv",
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
        (app_dir / "partitions" / "xiao_esp32s3.csv").write_text(
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
            "minimal", "xiao_esp32s3", "esp32s3"
        )
        demo = REMOTE.load_app_profile("demo", "xiao_esp32s3", "esp32s3")

        self.assertEqual(minimal.name, "minimal")
        self.assertEqual(minimal.directory, ROOT / "apps" / "minimal")
        self.assertTrue((minimal.flash_data_dir / "index.js").is_file())
        self.assertTrue(minimal.sdkconfig_defaults.is_file())
        self.assertEqual(minimal.partition_table.name, "xiao_esp32s3.csv")
        self.assertEqual(demo.name, "demo")
        self.assertTrue((demo.flash_data_dir / "demo" / "ui_immediate.js").is_file())
        self.assertTrue(demo.sdkconfig_defaults.is_file())
        self.assertEqual(demo.partition_table.name, "xiao_esp32s3.csv")

    def test_app_selection_reaches_cmake(self):
        args, board, app = REMOTE.parse_args([
            "--board", "xiao_esp32s3",
            "--app", "demo",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, board, app)

        self.assertEqual(config.app, "demo")
        self.assertIn("-DESP32QJS_BOARD=xiao_esp32s3", config.cmake_cache_entries)
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
            f"-DESP32QJS_PARTITION_TABLE={app.partition_table}",
            config.cmake_cache_entries,
        )
        self.assertEqual(
            config.sdkconfig_defaults,
            (board.sdkconfig_defaults, app.sdkconfig_defaults),
        )
        self.assertEqual(
            config.generated_sdkconfig.name,
            "sdkconfig.xiao_esp32s3.demo",
        )
        self.assertIn("-DESP32QJS_FLASH_DATA_DIR=", config.cmake_cache_entries)

    def test_external_application_profile_is_resolved_by_path(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            app_dir = self.write_external_app(Path(temp_dir))
            args, board, app = REMOTE.parse_args([
                "--board", "xiao_esp32s3",
                "--app", str(app_dir),
                "show-config",
            ])
            config = REMOTE.build_project_config(args, board, app)

            self.assertEqual(app.name, "external_agent")
            self.assertEqual(app.directory, app_dir.resolve())
            self.assertEqual(config.app, "external_agent")
            self.assertEqual(config.app_profile_dir, app_dir.resolve())
            self.assertEqual(config.app_flash_data_dir, app_dir / "flash_data")
            self.assertEqual(
                config.generated_sdkconfig.name,
                "sdkconfig.xiao_esp32s3.external_agent",
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

    def test_app_file_environment_selects_external_profile(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            app_dir = self.write_external_app(Path(temp_dir))
            with patch.dict(os.environ, {"APP_FILE": str(app_dir)}, clear=True):
                app = REMOTE.load_app_profile(None, "xiao_esp32s3", "esp32s3")
                self.assertEqual(app.name, "external_agent")
                self.assertEqual(app.directory, app_dir.resolve())

                overridden = REMOTE.load_app_profile(
                    "minimal", "xiao_esp32s3", "esp32s3"
                )
                self.assertEqual(overridden.name, "minimal")

    def test_invalid_external_app_id_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            app_dir = self.write_external_app(Path(temp_dir), app_id="bad app")
            with self.assertRaisesRegex(SystemExit, "APP_ID"):
                REMOTE.load_app_profile(str(app_dir), "xiao_esp32s3", "esp32s3")

    def test_js_test_build_enables_debug_gc(self):
        args, board, app = REMOTE.parse_args([
            "--board", "xiao_esp32s3",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, board, app)
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
        test_defaults = REMOTE.JS_TEST_SDKCONFIG_DEFAULTS.read_text()
        self.assertIn("CONFIG_ESP32_MQUICKJS_DEBUG_GC=y", test_defaults)
        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET=y", test_defaults)
        self.assertIn("websocket", REMOTE.JS_TEST_MODULE_MAP)
        self.assertIn(
            "-DESP32QJS_FLASH_DATA_INCLUDE_SHARED=ON",
            test_config.cmake_cache_entries,
        )

    def test_c3_js_test_build_keeps_the_board_heap_budget(self):
        args, board, app = REMOTE.parse_args([
            "--board", "esp32c3_supermini",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, board, app)
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
        args, board, app = REMOTE.parse_args([
            "--board", "xiao_esp32s3",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, board, app)
        session = object()

        with (
            patch.object(REMOTE, "flash") as flash,
            patch.object(REMOTE, "start_monitor_session", return_value=session) as start,
            patch.object(REMOTE, "read_monitor_until_text", return_value="ready"),
            patch.object(REMOTE, "wait_for_optional_js_repl_banner"),
            patch.object(REMOTE, "probe_js_runtime_features", return_value={"fs": True}),
            patch.object(REMOTE, "close_monitor_session") as close,
        ):
            REMOTE.run_js_tests(config, (), False, set(), True, False)

        flashed_config = flash.call_args.args[0]
        self.assertEqual(flashed_config.build_dir.name, "build-js-test")
        self.assertIn(
            REMOTE.JS_TEST_SDKCONFIG_DEFAULTS,
            flashed_config.sdkconfig_defaults,
        )
        start.assert_called_once_with(flashed_config)
        close.assert_called_once_with(session)

    def test_complete_flash_data_override_is_preserved(self):
        override = ROOT / "tests" / "js" / "flash_data"
        args, board, app = REMOTE.parse_args([
            "--app", "minimal",
            "--flash-data-dir", str(override),
            "show-config",
        ])
        config = REMOTE.build_project_config(args, board, app)

        self.assertEqual(config.flash_data_override, override)
        self.assertIn(
            f"-DESP32QJS_FLASH_DATA_DIR={override}",
            config.cmake_cache_entries,
        )

    def test_partition_profile_tracks_the_selected_board(self):
        args, board, app = REMOTE.parse_args([
            "--board", "esp32c3_supermini",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, board, app)

        self.assertEqual(config.partition_table.name, "esp32c3_supermini.csv")
        self.assertIn("0x1F0000", config.partition_table.read_text())

    def test_missing_board_partition_fails_before_build(self):
        with self.assertRaises(SystemExit):
            REMOTE.load_app_profile("minimal", "missing_board", "esp32s3")

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
