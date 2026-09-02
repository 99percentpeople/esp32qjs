import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase

ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class TransportArchitectureTests(SourceContractTestCase):
    def test_hardware_identity_uses_six_byte_factory_mac(self):
        source = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_sys.c"
        ).read_text(encoding="utf-8")
        function_start = source.index("static bool esp32_hardware_id(")
        function_end = source.index("\nstatic JSValue sys_profile_js_value(", function_start)
        function = source[function_start:function_end]

        self.assertIn("esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY)", function)
        self.assertNotIn("esp_efuse_mac_get_default", function)

    def test_usb_serial_and_repl_are_mutually_exclusive(self):
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")
        interactive_cmake = (
            ROOT / "components" / "esp32qjs_interactive" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "depends on SOC_USB_SERIAL_JTAG_SUPPORTED && !ESP32QJS_ENABLE_REPL",
            kconfig,
        )
        self.assertTrue(
            interactive_cmake.startswith("if(CONFIG_ESP32QJS_ENABLE_REPL)")
        )

    def test_websocket_is_an_optional_managed_feature(self):
        manifest = (MQUICKJS / "idf_component.yml").read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("espressif/esp_websocket_client", manifest)
        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET", cmake)
        self.assertIn("src/modules/websocket/esp32_mquickjs_websocket.c", cmake)

    def test_socket_is_a_framework_feature_without_application_protocol(self):
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        stdlib = (
            MQUICKJS / "src" / "core" / "mqjs_stdlib_esp32.c"
        ).read_text(encoding="utf-8")
        socket_dir = MQUICKJS / "src" / "modules" / "socket"
        source = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted(socket_dir.glob("*.c"))
        )

        self.assertIn("CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET", cmake)
        self.assertIn("src/modules/socket/esp32_mquickjs_socket.c", cmake)
        self.assertIn('JS_PROP_CLASS_DEF("sys", &js_sys_obj)', stdlib)
        self.assertNotIn('JS_PROP_CLASS_DEF("esp32",', stdlib)
        self.assertIn('JS_PROP_CLASS_DEF("socket", &js_socket_obj)', stdlib)
        for class_name in ("TCPSocket", "TCPListener", "UDPSocket"):
            self.assertIn(f'JS_CLASS_DEF("{class_name}"', stdlib)
        for operation in (
            "js_socket_open_tcp",
            "js_socket_listen_tcp",
            "js_socket_open_udp",
            "js_socket_handle_close",
            "js_socket_handle_status",
            "js_socket_handle_finalizer",
            "js_socket_get_max_transfer_bytes",
            "js_socket_tcp_connect",
            "js_socket_tcp_accept",
            "js_socket_tcp_send",
            "js_socket_tcp_recv",
            "js_socket_udp_send_to",
            "js_socket_udp_receive_from",
        ):
            self.assertIn(operation, source)
        self.assertIn("entry_generation", source)
        self.assertIn("socket_find_entry_generation", source)
        self.assertIn("future_reservations", source)
        self.assertIn("socket_future_resource_key", source)
        self.assertIn("control_lane_key", source)
        self.assertIn("accept_lane_key", source)
        self.assertIn("tx_lane_key", source)
        self.assertIn("rx_lane_key", source)
        self.assertNotIn("socket_require_entry", source)
        self.assertNotIn("return JS_NewInt32(ctx, client->id)", source)
        self.assertNotIn("tcpClient", source)
        self.assertNotIn("agent.", source)
        self.assertNotIn("deviceId", source)
        self.assertNotIn("Authorization", source)
        self.assertNotIn("Bearer ", source)

    def test_rpc_is_an_optional_codec_without_agent_business_rules(self):
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")
        cmake = (MQUICKJS / "CMakeLists.txt").read_text(encoding="utf-8")
        source = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_rpc.c"
        ).read_text(encoding="utf-8")
        stdlib = (
            MQUICKJS / "src" / "core" / "mqjs_stdlib_esp32.c"
        ).read_text(encoding="utf-8")
        declarations = (ROOT / "types" / "esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )
        docs = (ROOT / "docs" / "api" / "rpc.md").read_text(encoding="utf-8")
        public_header = MQUICKJS / "include" / "esp32qjs_rpc_wire.h"

        self.assertIn("config ESP32_MQUICKJS_FEATURE_RPC", kconfig)
        self.assertIn(
            "esp32_mquickjs_append_feature_component(\n"
            "    CONFIG_ESP32_MQUICKJS_FEATURE_RPC",
            cmake,
        )
        self.assertTrue(public_header.is_file())
        self.assertIn("rpc.createCodec(options)", source)
        self.assertIn('JS_CLASS_DEF("RPCCodec"', stdlib)
        self.assertIn('JS_CLASS_DEF("RPCDecoder"', stdlib)
        self.assertIn('JS_CFUNC_DEF("createDecoder", 0, js_rpc_create_decoder)', stdlib)
        self.assertIn('JS_CFUNC_DEF("encode", 4, js_rpc_encode)', stdlib)
        self.assertIn('JS_CFUNC_DEF("feed", 1, js_rpc_feed)', stdlib)
        self.assertIn('JS_CFUNC_DEF("reset", 0, js_rpc_reset_decoder)', stdlib)
        for removed_api in (
            'JS_CFUNC_DEF("releaseCodec"',
            'JS_CFUNC_DEF("releaseDecoder"',
            'JS_CFUNC_DEF("resetDecoder"',
            'JS_CFUNC_DEF("feed", 2',
            'JS_CFUNC_DEF("encode", 5',
        ):
            self.assertNotIn(removed_api, stdlib)
        self.assertIn("class RPCCodec", declarations)
        self.assertIn("class RPCDecoder", declarations)
        self.assertIn("interface RPCModule", declarations)
        self.assertIn("readonly rpc: boolean", declarations)
        self.assertIn("var decoder = codec.createDecoder();", docs)
        self.assertIn("var frames = codec.encode(1, 7, 0,", docs)
        self.assertIn("var messages = decoder.feed(incomingChunk);", docs)
        self.assertIn("generation", source)
        self.assertIn("js_rpc_codec_finalizer", source)
        self.assertIn("js_rpc_decoder_finalizer", source)
        self.assertNotIn("expects an active codec id", source)
        self.assertNotIn("expects an active decoder id", source)
        for application_rule in (
            '"hardwareId"',
            '"tool"',
            '"result"',
            '"/workspace"',
            "0x0101U",
            ".esp32qjs-agent-rpc-",
            "agent.server/1",
        ):
            self.assertNotIn(application_rule, source)

    def test_rpc_encoder_grows_payload_storage_on_demand(self):
        source = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_rpc.c"
        ).read_text(encoding="utf-8")
        append_start = source.index("static bool rpc_buffer_append(")
        append_end = source.index("\nstatic bool rpc_buffer_byte(", append_start)
        append = source[append_start:append_end]
        encode_start = source.index("JSValue js_rpc_encode(")
        encode_end = source.index("\nJSValue js_rpc_bytes(", encode_start)
        encode = source[encode_start:encode_end]

        self.assertIn("RPC_BUFFER_INITIAL_BYTES", append)
        self.assertIn("realloc(buffer->data, capacity)", append)
        self.assertIn("ESP32_MQUICKJS_RPC_MESSAGE_BYTES - buffer->length", append)
        self.assertNotIn("malloc(ESP32_MQUICKJS_RPC_MESSAGE_BYTES)", encode)
        self.assertIn("if (payload.out_of_memory)", encode)

    def test_rpc_stream_sender_uses_bounded_working_segments(self):
        source = (
            MQUICKJS / "src" / "core" / "esp32_mquickjs_rpc.c"
        ).read_text(encoding="utf-8")

        self.assertIn("#define RPC_STREAM_SEGMENT_BYTES 4096U", source)
        self.assertIn("uint8_t segment[RPC_STREAM_SEGMENT_BYTES];", source)
        self.assertIn("uint8_t frame[RPC_STREAM_MAX_FRAME_BYTES];", source)
        self.assertNotIn(
            "uint8_t segment[ESP32_MQUICKJS_RPC_SEGMENT_BYTES];",
            source,
        )

    def test_websocket_control_frames_are_not_application_errors(self):
        source = (
            MQUICKJS / "src" / "modules" / "websocket" / "esp32_mquickjs_websocket.c"
        ).read_text(encoding="utf-8")
        function_start = source.index("static void websocket_handle_data(")
        function_end = source.index("\nstatic void websocket_event_handler(", function_start)
        handler = source[function_start:function_end]
        error_position = handler.index(
            'websocket_enqueue_error("unsupported WebSocket data opcode"'
        )

        for opcode in (
            "WEBSOCKET_OPCODE_CLOSE",
            "WEBSOCKET_OPCODE_PING",
            "WEBSOCKET_OPCODE_PONG",
        ):
            self.assertLess(handler.index(opcode), error_position)

    def test_websocket_and_usb_use_receive_with_typed_binary_payloads(self):
        websocket = (
            MQUICKJS / "src" / "modules" / "websocket" / "esp32_mquickjs_websocket.c"
        ).read_text(encoding="utf-8")
        usb = (
            MQUICKJS / "src" / "modules" / "usb_serial" / "esp32_mquickjs_usb_serial.c"
        ).read_text(encoding="utf-8")
        declarations = (ROOT / "types" / "esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        self.assertIn("WEBSOCKET_OPCODE_BINARY", websocket)
        self.assertIn("esp_websocket_client_send_bin(", websocket)
        self.assertIn("esp32_mquickjs_new_owned_byte_view(", websocket)
        self.assertIn("websocket_free_callback_event(data);", websocket)
        self.assertNotIn('JS_SetPropertyStr(ctx, *queue_object, "recv"', websocket)
        self.assertNotIn('JS_SetPropertyStr(ctx, *queue_object, "recv"', usb)
        self.assertIn('binary ? "chunkBytes" : "maxFrameBytes"', usb)
        self.assertIn("interface USBSerialTextHandle extends EventQueue<string>", declarations)
        self.assertIn("interface USBSerialBinaryHandle extends EventQueue<ByteView>", declarations)
        self.assertIn("send(data: string | ByteSource | ByteSpanSource): number", declarations)
        self.assertNotIn("recv(timeoutMs?: number): WebSocketClientEvent", declarations)
        self.assertNotIn("recv(timeoutMs?: number): string | null", declarations)

    def test_repeated_input_transports_use_event_queues_without_callbacks(self):
        sources = (
            MQUICKJS / "src" / "modules" / "usb_serial" / "esp32_mquickjs_usb_serial.c",
            MQUICKJS / "src" / "modules" / "websocket" / "esp32_mquickjs_websocket.c",
        )

        for source in sources:
            text = source.read_text(encoding="utf-8")
            self.assertIn("esp32_mquickjs_event_queue", text, source)
            self.assertNotIn("JS_Call(", text, source)
            self.assertNotIn("JSGCRef callback", text, source)

    def test_usb_serial_output_waits_cooperatively_for_tx_capacity(self):
        source = (
            MQUICKJS / "src" / "modules" / "usb_serial" / "esp32_mquickjs_usb_serial.c"
        ).read_text(encoding="utf-8")

        self.assertIn("USJ_SELECT_WRITE_NOTIF", source)
        self.assertIn("esp32_mquickjs_notify_active_runtime_from_isr", source)
        self.assertIn("usb_serial_jtag_is_connected()", source)
        self.assertIn("esp32_mquickjs_poll(ctx, runtime)", source)
        self.assertIn("esp32_mquickjs_wait_for_activity(runtime, wait_ms)", source)
        self.assertIn("usb_serial_jtag_write_bytes(\n            data + offset,\n            chunk,\n            0)", source)
        self.assertIn("if (written > 0)", source)
        self.assertIn("offset += (size_t)written", source)
        self.assertNotIn("pdMS_TO_TICKS(USB_SERIAL_BINARY_WRITE", source)
        self.assertLess(
            source.index("if (now_us >= progress_deadline_us)"),
            source.index("if (poll_result != ESP32_MQUICKJS_POLL_NONE)"),
            "ready timers must not starve the finite USB TX stall deadline",
        )

    def test_usb_serial_future_owns_tx_lane_and_timeout_wake(self):
        source = (
            MQUICKJS / "src" / "modules" / "usb_serial" / "esp32_mquickjs_usb_serial.c"
        ).read_text(encoding="utf-8")

        self.assertIn("usb_serial_future_resource_key", source)
        self.assertIn(".resource_key = usb_serial_future_resource_key", source)
        self.assertIn("usb_serial_future_timeout_wake", source)
        self.assertIn("esp_timer_start_once", source)
        self.assertIn("esp32_mquickjs_future_wake", source)
        capture_start = source.index("static bool usb_serial_future_prepare(")
        capture_end = source.index(
            "\nstatic void usb_serial_future_timeout_wake", capture_start
        )
        self.assertNotIn("s_usb_serial_state.sending", source[capture_start:capture_end])

    def test_i2c_device_owns_a_stable_driver_handle(self):
        source = (
            MQUICKJS / "src" / "modules" / "i2c" / "esp32_mquickjs_i2c.c"
        ).read_text(encoding="utf-8")
        open_start = source.index("JSValue js_i2c_bus_open_device(")
        open_end = source.index("\nJSValue js_i2c_device_constructor(", open_start)
        open_device = source[open_start:open_end]
        worker_start = source.index("static void i2c_future_worker(")
        worker_end = source.index("\nstatic bool i2c_future_start(", worker_start)
        worker = source[worker_start:worker_end]

        self.assertIn("i2c_master_bus_add_device", open_device)
        self.assertIn("&device->handle", open_device)
        self.assertIn("device->next = bus->devices", open_device)
        self.assertIn("bus->devices = device", open_device)
        self.assertIn("device_slot->handle", worker)
        self.assertIn("i2c_get_device(&state->device_ref", worker)
        self.assertNotIn("i2c_master_bus_add_device", worker)
        self.assertNotIn("i2c_master_bus_rm_device", worker)
        self.assertIn("i2c_master_bus_rm_device(device->handle)", source)
        self.assertNotIn("cached_device_handle", source)


if __name__ == "__main__":
    unittest.main()
