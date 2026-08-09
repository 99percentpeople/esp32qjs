import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REMOTE_PATH = ROOT / "scripts" / "remote.py"
SPEC = importlib.util.spec_from_file_location("esp32qjs_remote_test", REMOTE_PATH)
REMOTE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = REMOTE
SPEC.loader.exec_module(REMOTE)


class RemoteConfigTests(unittest.TestCase):
    def test_bundled_application_profiles_are_valid(self):
        minimal = REMOTE.load_app_profile(
            "minimal", "xiao_esp32s3", "esp32s3"
        )
        demo = REMOTE.load_app_profile("demo", "xiao_esp32s3", "esp32s3")

        self.assertEqual(minimal.name, "minimal")
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

    def test_js_test_build_enables_debug_gc(self):
        args, board, app = REMOTE.parse_args([
            "--board", "xiao_esp32s3",
            "--app", "minimal",
            "show-config",
        ])
        config = REMOTE.build_project_config(args, board, app)
        test_config = REMOTE.js_test_build_config(config)

        self.assertEqual(test_config.flash_data_override, REMOTE.JS_TEST_FLASH_DATA_DIR)
        self.assertEqual(
            test_config.sdkconfig_defaults[-1],
            REMOTE.JS_TEST_SDKCONFIG_DEFAULTS,
        )
        self.assertIn(
            "CONFIG_ESP32_MQUICKJS_DEBUG_GC=y",
            REMOTE.JS_TEST_SDKCONFIG_DEFAULTS.read_text(),
        )

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
