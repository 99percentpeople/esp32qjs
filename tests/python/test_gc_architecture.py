import re
import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class GcArchitectureTests(SourceContractTestCase):
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

    def test_hidden_future_wrappers_root_receivers_before_property_lookup(self):
        for module in ("fs", "i2c", "nvs", "spi", "uart"):
            source = (
                MQUICKJS / "src" / "modules" / module / f"esp32_mquickjs_{module}.c"
            ).read_text(encoding="utf-8")

            self.assertIn("JSGCRef receiver_ref;", source)
            self.assertIn("*rooted_receiver = receiver;", source)
            self.assertIn(
                "JS_GetPropertyStr(ctx, *rooted_receiver, method_name)",
                source,
            )

    def test_hidden_future_call_roots_receiver_before_allocating_arguments(self):
        future = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")
        function_start = future.index("JSValue esp32_mquickjs_future_call_and_wait(")
        function_end = future.index("\nJSValue js_future_constructor(", function_start)
        gateway = future[function_start:function_end]

        self.assertLess(
            gateway.index("rooted_receiver = JS_PushGCRef"),
            gateway.index("*args_array = JS_NewArray"),
        )

    def test_http_server_roots_request_while_attaching_native_identity(self):
        source = (
            MQUICKJS / "src" / "modules" / "http" / "esp32_mquickjs_http_server.c"
        ).read_text(encoding="utf-8")
        function_start = source.index("static JSValue http_server_event_to_js(")
        function_end = source.index("\nstatic void http_server_event_drop(", function_start)
        gateway = source[function_start:function_end]

        self.assertIn("JSGCRef result_ref;", gateway)
        self.assertIn("result = JS_PushGCRef(ctx, &result_ref);", gateway)
        self.assertIn("esp32_mquickjs_set_property_ref(ctx, result,", gateway)
        self.assertNotIn("esp32_mquickjs_set_property(ctx, result,", gateway)

    def test_deferred_gc_roots_result_before_execution_safe_point(self):
        core = (MQUICKJS / "src" / "core" / "esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        finish_start = core.index("static JSValue esp32_mquickjs_finish_execution(")
        finish_end = core.index("\nstatic void esp32_mquickjs_clear_idle_jobs(", finish_start)
        finish = core[finish_start:finish_end]

        self.assertIn("*rooted_result = result;", finish)
        self.assertLess(
            finish.index("*rooted_result = result;"),
            finish.index("esp32_mquickjs_execution_leave(runtime);"),
        )
        self.assertLess(
            finish.index("esp32_mquickjs_execution_leave(runtime);"),
            finish.index("run_pending_gc(ctx, runtime);"),
        )
        self.assertIn("return JS_PopGCRef(ctx, &result_ref);", finish)

    def test_terminal_futures_use_native_pressure_instead_of_a_fixed_batch(self):
        future = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")
        publish_start = future.index("static void future_publish_terminal(")
        publish_end = future.index("\nstatic void future_store_result(", publish_start)
        publish = future[publish_start:publish_end]

        self.assertIn("esp32_mquickjs_native_gc_reclaimable(slot->runtime);", publish)
        self.assertNotIn("FUTURE_GC_BATCH", future)
        self.assertNotIn("terminal_handles_since_gc", future)

    def test_explicit_gc_remains_a_diagnostic_single_collection(self):
        core = (MQUICKJS / "src" / "core" / "esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        gc_start = core.index("JSValue js_gc(")
        gc_end = core.index("\nJSValue js_load(", gc_start)
        gc_function = core[gc_start:gc_end]

        self.assertEqual(gc_function.count("JS_GC(ctx);"), 1)
        self.assertNotIn("request_gc", gc_function)
        self.assertIn("state->native_gc_pending = false;", gc_function)
        self.assertIn("state->native_gc_debt_bytes = 0;", gc_function)

    def test_terminal_future_result_is_a_traced_child_not_a_context_root(self):
        future = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")
        stdlib = (
            MQUICKJS / "src" / "core" / "mqjs_stdlib_esp32.c"
        ).read_text(encoding="utf-8")

        self.assertIn("JSValue result;", future)
        self.assertIn("visit(visitor_opaque, &handle->result);", future)
        self.assertNotIn("JS_AddGCRef(ctx, &handle->result)", future)
        self.assertNotIn("JS_DeleteGCRef(ctx, &handle->result)", future)
        self.assertIn("JS_CLASS_TRACE_DEF(\"Future\"", stdlib)

    def test_abandoned_future_handle_remains_accounted_until_finalization(self):
        future = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")
        abandon_start = future.index("static void future_abandon_handle(")
        abandon_end = future.index("\nstatic future_handle_t *future_handle_from_value(", abandon_start)
        abandon = future[abandon_start:abandon_end]

        self.assertIn("esp32_mquickjs_native_gc_reclaimable(slot->runtime);", abandon)
        self.assertNotIn("esp32_mquickjs_native_gc_free", abandon)
        self.assertNotIn("->runtime = NULL", abandon)

    def test_user_gc_trace_participates_in_mark_and_compaction(self):
        engine = (
            MQUICKJS / "vendor" / "mquickjs" / "mquickjs.c"
        ).read_text(encoding="utf-8")
        builder = (
            MQUICKJS / "vendor" / "mquickjs" / "mquickjs_build.c"
        ).read_text(encoding="utf-8")

        self.assertIn("gc_trace_user_object(s->ctx", engine)
        self.assertIn("gc_mark_native_value", engine)
        self.assertIn("gc_trace_user_object(ctx, p, gc_thread_native_value", engine)
        self.assertIn("static void dump_c_gc_traces", builder)
        self.assertIn("js_c_gc_trace_table", builder)

    def test_upstream_example_rectangle_remains_unmodified(self):
        example = (
            MQUICKJS / "vendor" / "mquickjs" / "example.c"
        ).read_text(encoding="utf-8")
        rectangle_start = example.index("typedef struct {\n    int x;")
        rectangle_end = example.index("} RectangleData;", rectangle_start)
        rectangle = example[rectangle_start:rectangle_end]

        self.assertNotIn("JSValue", rectangle)
        self.assertNotIn("child", rectangle)

    def test_scheduler_consumes_gc_before_advancing_futures(self):
        core = (MQUICKJS / "src" / "core" / "esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        poll_start = core.index("esp32_mquickjs_poll_result_t esp32_mquickjs_poll(")
        poll_end = core.index("\nJSValue js_print(", poll_start)
        poll = core[poll_start:poll_end]

        self.assertLess(
            poll.index("run_pending_gc(ctx, runtime);"),
            poll.index("esp32_mquickjs_future_poll(ctx, runtime)"),
        )
        self.assertEqual(poll.count("run_pending_gc(ctx, runtime);"), 1)

    def test_compacting_gc_finalizes_every_coalesced_user_object(self):
        engine = (
            MQUICKJS / "vendor" / "mquickjs" / "mquickjs.c"
        ).read_text(encoding="utf-8")
        sweep_start = engine.index("/* reset the gc marks")
        sweep_end = engine.index("\nstatic JSValue js_value_from_pval", sweep_start)
        sweep = engine[sweep_start:sweep_end]
        merge_start = sweep.index("/* merge all the consecutive free blocks */")
        merge_end = sweep.index("set_free_block(b, size);", merge_start)
        first_finalizer = sweep[:merge_start]
        merge = sweep[merge_start:merge_end]

        self.assertIn("/* call the user finalizer if needed */", first_finalizer)
        self.assertIn(
            "ctx->c_finalizer_table[p->class_id - JS_CLASS_USER](",
            first_finalizer,
        )
        self.assertIn("ptr1 = ptr + size;", merge)
        self.assertIn("while (ptr1 < ctx->heap_free", merge)
        self.assertIn(
            "ctx->c_finalizer_table[p->class_id - JS_CLASS_USER](", merge
        )
        self.assertIn("ptr1 += get_mblock_size(ptr1);", merge)
        self.assertLess(
            merge.index("ctx->c_finalizer_table[p->class_id - JS_CLASS_USER]("),
            merge.index("ptr1 += get_mblock_size(ptr1);"),
        )

if __name__ == "__main__":
    unittest.main()
