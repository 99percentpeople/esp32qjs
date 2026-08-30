from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class HttpNamespaceArchitectureTests(SourceContractTestCase):
    def test_native_globals_leave_fetch_alias_to_javascript_libraries(self):
        source = (
            MQUICKJS / "src/core/mqjs_stdlib_esp32.c"
        ).read_text(encoding="utf-8")
        start = source.index("static const JSPropDef js_global_object_extra[]")
        end = source.index("\nstatic size_t count_prop_defs", start)
        native_globals = source[start:end]

        self.assertNotIn('JS_CFUNC_DEF("fetch"', native_globals)

    def test_native_http_future_driver_does_not_require_global_fetch_alias(self):
        future = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_future.c"
        ).read_text(encoding="utf-8")
        start = future.index("bool esp32_mquickjs_init_http_future_runtime(")
        end = future.index("\nbool esp32_mquickjs_deinit_http_runtime", start)
        initialization = future[start:end]

        self.assertIn('JS_GetPropertyStr(ctx, *global, "http")', initialization)
        self.assertIn('JS_GetPropertyStr(ctx, *http, "fetch")', initialization)
        self.assertNotIn('JS_GetPropertyStr(ctx, *global, "fetch")', initialization)
        self.assertEqual(
            initialization.count("esp32_mquickjs_future_register_driver("),
            1,
        )

    def test_synchronous_http_fetch_dispatches_through_namespace_driver(self):
        source = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http.c"
        ).read_text(encoding="utf-8")
        start = source.index("JSValue js_http_fetch(")
        end = source.index("\n#endif", start)
        fetch = source[start:end]

        self.assertIn('JS_GetPropertyStr(ctx, *global, "http")', fetch)
        self.assertIn('JS_GetPropertyStr(ctx, *http, "fetch")', fetch)
        self.assertNotIn('JS_GetPropertyStr(ctx, *global, "fetch")', fetch)
        self.assertIn("*fetch,\n                                                     *http,", fetch)


if __name__ == "__main__":
    import unittest

    unittest.main()
