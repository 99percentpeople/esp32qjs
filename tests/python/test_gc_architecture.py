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

    def test_fs_future_prepare_uses_its_rooted_receiver(self):
        source = (
            MQUICKJS / "src" / "modules" / "fs" / "esp32_mquickjs_fs.c"
        ).read_text(encoding="utf-8")
        start = source.index("static bool fs_future_prepare_common(")
        end = source.index("\n#define FS_PREPARE", start)
        prepare = source[start:end]

        self.assertIn("JSGCRef *receiver_ref", prepare)
        self.assertIn("receiver_ref->val", prepare)
        self.assertNotRegex(prepare, r"\bJSValue receiver\b")

    def test_fs_volume_copies_movable_root_before_object_allocation(self):
        source = (
            MQUICKJS / "src" / "modules" / "fs" / "esp32_mquickjs_fs.c"
        ).read_text(encoding="utf-8")
        start = source.index("JSValue js_fs_volume(")
        end = source.index("\nJSValue js_fs_volume_constructor(", start)
        volume = source[start:end]

        self.assertIn("char root[ESP32_MQUICKJS_FS_ROOT_MAX];", volume)
        self.assertIn("snprintf(root, sizeof(root), \"%s\", path);", volume)
        self.assertIn("return fs_make_volume(ctx, root);", volume)
        self.assertLess(
            volume.index("snprintf(root, sizeof(root), \"%s\", path);"),
            volume.index("fs_make_volume(ctx, root)"),
        )

    def test_rpc_codec_parsers_dereference_rooted_arrays_after_lookup(self):
        source = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_rpc.c"
        ).read_text(encoding="utf-8")
        fields_start = source.index("static bool rpc_codec_copy_fields(")
        fields_end = source.index("\nstatic bool rpc_codec_mark_dynamic_fields(", fields_start)
        fields = source[fields_start:fields_end]
        dynamic_start = fields_end + 1
        dynamic_end = source.index("\nJSValue js_rpc_create_codec(", dynamic_start)
        dynamic = source[dynamic_start:dynamic_end]

        self.assertIn("JSGCRef *fields_ref", fields)
        self.assertIn("fields_ref->val", fields)
        self.assertNotRegex(fields, r"\bJSValue fields_value\b")
        self.assertIn("JSGCRef *dynamic_ref", dynamic)
        self.assertIn("dynamic_ref->val", dynamic)
        self.assertNotRegex(dynamic, r"\bJSValue dynamic_value\b")

    def test_rpc_encoder_dereferences_rooted_values_after_allocations(self):
        source = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_rpc.c"
        ).read_text(encoding="utf-8")
        start = source.index("static bool rpc_encode_value(")
        end = source.index("\nstatic bool rpc_decode_unsigned(", start)
        encoder = source[start:end]

        self.assertIn("JSGCRef *value_ref", encoder)
        self.assertIn("value_ref->val", encoder)
        self.assertNotRegex(encoder, r"\bJSValue value\b")
        self.assertIn("rpc_encode_value(ctx, codec, &item_ref", encoder)
        self.assertIn("*JS_AddGCRef(ctx, &buffer->stream_ref) = value_ref->val", encoder)
        self.assertIn("JS_DeleteGCRef(ctx, &buffer->stream_ref)", source)

    def test_socket_option_parsers_keep_their_input_rooted(self):
        source = (
            MQUICKJS / "src" / "modules" / "socket" /
            "esp32_mquickjs_socket.c"
        ).read_text(encoding="utf-8")

        for function_name, next_function, property_name in (
            ("socket_open_options", "socket_listen_options", "option"),
            ("socket_listen_options", "socket_connect_options", "option"),
            ("socket_connect_options", "socket_class_matches_protocol", "timeout"),
        ):
            start = source.index(f"static bool {function_name}(")
            end = source.index(f"\nstatic bool {next_function}(", start)
            parser = source[start:end]

            self.assertIn("JSGCRef options_ref;", parser)
            self.assertIn("*rooted_options = options;", parser)
            self.assertIn(f"JSGCRef {property_name}_ref;", parser)
            self.assertIn(
                f"{property_name} = JS_PushGCRef(ctx, &{property_name}_ref);",
                parser,
            )
            self.assertRegex(
                parser,
                rf"\*{property_name} = JS_GetPropertyStr\(ctx, "
                r"\*rooted_options,",
            )
            self.assertIn("JS_GetPropertyStr(ctx, *rooted_options,", parser)
            self.assertNotRegex(
                parser,
                r"JS_GetPropertyStr\(ctx, options,",
            )
            rooted_parser = parser[parser.index("*rooted_options = options;") :]
            self.assertNotRegex(
                rooted_parser,
                r"(?:JS_GetClassID|JS_IsArray)\(ctx, options\)",
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

    def test_socket_sync_future_gateway_roots_receiver_before_method_lookup(self):
        source = (
            MQUICKJS / "src" / "modules" / "socket" /
            "esp32_mquickjs_socket.c"
        ).read_text(encoding="utf-8")
        function_start = source.index("static JSValue socket_future_call_and_wait(")
        function_end = source.index("\ntypedef enum {", function_start)
        gateway = source[function_start:function_end]

        self.assertIn("JSGCRef receiver_ref;", gateway)
        self.assertIn("*receiver = this_value;", gateway)
        self.assertIn(
            "JS_GetPropertyStr(ctx, *receiver, method_name)",
            gateway,
        )
        self.assertIn(
            "*method,\n                                                     *receiver,",
            gateway,
        )
        rooted_gateway = gateway[gateway.index("*receiver = this_value;") :]
        self.assertNotIn(
            "JS_GetPropertyStr(ctx, this_value, method_name)",
            rooted_gateway,
        )

    def test_usb_serial_open_releases_temporary_roots_in_lifo_order(self):
        source = (
            MQUICKJS
            / "src"
            / "modules"
            / "usb_serial"
            / "esp32_mquickjs_usb_serial.c"
        ).read_text(encoding="utf-8")
        function_start = source.index("JSValue js_usb_serial_open(")
        function_end = source.index("\nJSValue js_usb_serial_close(", function_start)
        success = source[
            source.index("s_usb_serial_state.opened = true;", function_start):function_end
        ]

        self.assertIn(
            "return_value = JS_PopGCRef(ctx, &handle_ref);",
            success,
        )
        self.assertLess(
            success.index("JS_PopGCRef(ctx, &handle_ref)"),
            success.index("JS_PopGCRef(ctx, &queue_ref)"),
        )
        self.assertIn("return return_value;", success)

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

    def test_framework_does_not_schedule_garbage_collection(self):
        core = (MQUICKJS / "src" / "core" / "esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        future = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")
        byte_source = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_byte_source.c"
        ).read_text(encoding="utf-8")
        gc_start = core.index("JSValue js_gc(")
        gc_end = core.index("\nJSValue js_load(", gc_start)
        framework_automatic_paths = core[:gc_start] + core[gc_end:]

        self.assertNotIn("JS_GC(", framework_automatic_paths)
        self.assertNotIn("JS_GC(", future)
        self.assertNotIn("native_gc_", core)
        self.assertNotIn("native_gc_", future)
        self.assertNotIn("gc_pressure", byte_source)
        self.assertNotIn("run_pending_gc", core)

    def test_explicit_gc_remains_a_diagnostic_single_collection(self):
        core = (MQUICKJS / "src" / "core" / "esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        gc_start = core.index("JSValue js_gc(")
        gc_end = core.index("\nJSValue js_load(", gc_start)
        gc_function = core[gc_start:gc_end]

        self.assertEqual(gc_function.count("JS_GC(ctx);"), 1)
        self.assertNotIn("native_gc", gc_function)
        self.assertNotIn("byte_source_take_gc_request", gc_function)

    def test_load_copies_movable_path_before_filesystem_lookup(self):
        core = (MQUICKJS / "src" / "core" / "esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        function_start = core.index("JSValue js_load(")
        function_end = core.index("\nJSValue js_sleep(", function_start)
        load_function = core[function_start:function_end]

        self.assertIn(
            "char command[ESP32_MQUICKJS_MAX_SCRIPT_PATH];",
            load_function,
        )
        self.assertIn("JS_ToCStringLen(ctx, &command_len", load_function)
        self.assertIn("memcpy(command, source, command_len);", load_function)
        self.assertLess(
            load_function.index("memcpy(command, source, command_len);"),
            load_function.index("esp32_mquickjs_load_from_active_fs("),
        )

    def test_mquickjs_collects_automatically_on_allocation_pressure(self):
        engine = (
            MQUICKJS / "vendor" / "mquickjs" / "mquickjs.c"
        ).read_text(encoding="utf-8")
        check_start = engine.index("static int check_free_mem(")
        check_end = engine.index("\n/* check that 'len' values", check_start)
        check = engine[check_start:check_end]
        malloc_start = engine.index("static void *js_malloc(")
        malloc_end = engine.index("\nstatic void *js_mallocz(", malloc_start)
        malloc = engine[malloc_start:malloc_end]

        self.assertIn("JS_GC(ctx);", check)
        self.assertIn("check_free_mem(ctx, ctx->stack_bottom, size)", malloc)

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

    def test_abandoned_future_handle_is_owned_only_by_its_js_object(self):
        future = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_future.c"
        ).read_text(encoding="utf-8")
        abandon_start = future.index("static void future_abandon_handle(")
        abandon_end = future.index("\nstatic future_handle_t *future_handle_from_value(", abandon_start)
        abandon = future[abandon_start:abandon_end]

        self.assertIn("The JS object owns the handle until MQuickJS", abandon)
        self.assertIn("slot->handle = NULL;", abandon)
        self.assertNotIn("native_gc", abandon)

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

    def test_future_gc_regression_uses_engine_allocation_pressure(self):
        test_source = (
            ROOT / "tests" / "js" / "flash_data" / "modules" / "timers" /
            "automatic-gc.js"
        ).read_text(encoding="utf-8")

        self.assertNotIn("gc()", test_source)
        self.assertIn("pressure = prefix + i;", test_source)
        self.assertIn("automatic MQuickJS GC", test_source)

    def test_scheduler_only_advances_runtime_work(self):
        core = (MQUICKJS / "src" / "core" / "esp32_mquickjs.c").read_text(
            encoding="utf-8"
        )
        poll_start = core.index("esp32_mquickjs_poll_result_t esp32_mquickjs_poll(")
        poll_end = core.index("\nJSValue js_print(", poll_start)
        poll = core[poll_start:poll_end]

        self.assertIn("esp32_mquickjs_future_poll(ctx, runtime)", poll)
        self.assertIn("esp32_mquickjs_poll_registered(ctx, runtime)", poll)
        self.assertNotIn("JS_GC(", poll)
        self.assertNotIn("run_pending_gc", poll)

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
