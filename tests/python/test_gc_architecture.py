import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class GcArchitectureTests(unittest.TestCase):
    def test_rooted_property_targets_are_dereferenced_after_value_creation(self):
        unsafe = re.compile(
            r"esp32_mquickjs_set_property\(ctx,\s*\*[A-Za-z_][A-Za-z0-9_]*,",
            re.MULTILINE,
        )
        matches = []
        for source in (MQUICKJS / "src").rglob("*.c"):
            if unsafe.search(source.read_text(encoding="utf-8")):
                matches.append(source.relative_to(ROOT).as_posix())

        self.assertEqual(matches, [])

    def test_callback_gateway_roots_values_before_stack_compaction(self):
        core = (MQUICKJS / "src" / "core" / "esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        function_start = core.index("JSValue esp32_mquickjs_call(")
        function_end = core.index("\nbool esp32_mquickjs_set_property(", function_start)
        gateway = core[function_start:function_end]

        self.assertLess(gateway.index("JS_PushGCRef"), gateway.index("JS_StackCheck"))
        self.assertIn("argument_refs[i].val", gateway)

    def test_debug_gc_preserves_nested_stack_reservations(self):
        engine = (
            MQUICKJS / "vendor" / "mquickjs" / "mquickjs.c"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "if (new_stack_bottom > ctx->stack_bottom)\n"
            "        new_stack_bottom = ctx->stack_bottom;",
            engine,
        )
        self.assertIn("ctx->parse_state == NULL && JS_IsPtr(ctx->dummy_block)", engine)


if __name__ == "__main__":
    unittest.main()
