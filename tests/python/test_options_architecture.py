import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class NativeOptionsArchitectureTests(unittest.TestCase):
    def test_shared_options_helper_uses_engine_intrinsic(self):
        source = (
            MQUICKJS / "src/core/esp32_mquickjs_options.c"
        ).read_text(encoding="utf-8")
        helper = source[
            source.index("JSValue esp32_mquickjs_own_property_keys(") :
            source.index("\nbool esp32_mquickjs_validate_plain_options(")
        ]

        self.assertIn("JSGCRef value_ref;", helper)
        self.assertIn("*rooted_value = value;", helper)
        self.assertIn("js_object_keys(ctx, NULL, 1, rooted_value)", helper)
        self.assertIn("JS_PopGCRef(ctx, &value_ref);", helper)
        self.assertNotIn("js_object_keys(ctx, NULL, 1, &value)", helper)
        self.assertIn("JS_GetClassID(ctx, options) != JS_CLASS_OBJECT", source)
        self.assertNotIn("JS_GetGlobalObject", source)
        self.assertNotIn('JS_GetPropertyStr(ctx, *object, "keys")', source)

    def test_native_modules_do_not_resolve_object_keys_from_global(self):
        sources = [
            MQUICKJS / "src/modules/ble/esp32_mquickjs_ble.c",
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c",
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi_future.c",
            MQUICKJS / "src/modules/http/esp32_mquickjs_http.c",
            MQUICKJS / "src/core/esp32_mquickjs_rpc.c",
        ]

        for path in sources:
            source = path.read_text(encoding="utf-8")
            with self.subTest(path=path.name):
                self.assertIn("esp32_mquickjs_options.h", source)
                self.assertNotIn('JS_GetPropertyStr(ctx, *object, "keys")', source)
                self.assertNotIn('JS_GetPropertyStr(ctx, *object_ctor, "keys")', source)

    def test_strict_option_users_share_one_validator(self):
        sources = [
            MQUICKJS / "src/core/esp32_mquickjs_sys.c",
            MQUICKJS / "src/modules/fs/esp32_mquickjs_fs.c",
            MQUICKJS / "src/modules/spi/esp32_mquickjs_spi.c",
            MQUICKJS / "src/modules/ble/esp32_mquickjs_ble.c",
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c",
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi_future.c",
        ]

        for path in sources:
            source = path.read_text(encoding="utf-8")
            with self.subTest(path=path.name):
                self.assertIn("esp32_mquickjs_validate_plain_options", source)
                self.assertNotIn("js_object_keys(ctx", source)

    def test_timeout_entry_points_share_the_bounded_integer_parser(self):
        sources = [
            MQUICKJS / "src/core/esp32_mquickjs_sys.c",
            MQUICKJS / "src/core/esp32_mquickjs_future.c",
            MQUICKJS / "src/core/esp32_mquickjs_event_queue.c",
            MQUICKJS / "src/modules/time/esp32_mquickjs_time.c",
            MQUICKJS / "src/modules/http/esp32_mquickjs_http.c",
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c",
            MQUICKJS / "src/modules/spi/esp32_mquickjs_spi.c",
            MQUICKJS / "src/modules/rmt/esp32_mquickjs_rmt.c",
            MQUICKJS / "src/modules/ble/esp32_mquickjs_ble.c",
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c",
        ]

        for path in sources:
            source = path.read_text(encoding="utf-8")
            with self.subTest(path=path.name):
                self.assertIn(
                    "esp32_mquickjs_value_to_bounded_u32", source
                )


if __name__ == "__main__":
    unittest.main()
