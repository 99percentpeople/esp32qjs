import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase

ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class TransportArchitectureTests(SourceContractTestCase):
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
        self.assertIn("socket.tcp", source)
        self.assertIn("socket.udp", source)
        for operation in (
            "js_socket_open",
            "js_socket_close",
            "js_socket_status",
            "js_socket_get_max_transfer_bytes",
            "js_socket_tcp_connect",
            "js_socket_tcp_listen",
            "js_socket_tcp_accept",
            "js_socket_tcp_send",
            "js_socket_tcp_recv",
            "js_socket_udp_sendto",
            "js_socket_udp_recvfrom",
        ):
            self.assertIn(operation, source)
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
        public_header = MQUICKJS / "include" / "esp32qjs_rpc_wire.h"

        self.assertIn("config ESP32_MQUICKJS_FEATURE_RPC", kconfig)
        self.assertIn(
            "esp32_mquickjs_append_feature_component(\n"
            "    CONFIG_ESP32_MQUICKJS_FEATURE_RPC",
            cmake,
        )
        self.assertTrue(public_header.is_file())
        self.assertIn("rpc.createCodec(options)", source)
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

    def test_websocket_control_frames_are_not_application_errors(self):
        source = (
            MQUICKJS / "src" / "modules" / "websocket" / "esp32_mquickjs_websocket.c"
        ).read_text(encoding="utf-8")
        function_start = source.index("static void websocket_handle_data(")
        function_end = source.index("\nstatic void websocket_event_handler(", function_start)
        handler = source[function_start:function_end]
        error_position = handler.index(
            'websocket_enqueue_error("only complete WebSocket text'
        )

        for opcode in (
            "WEBSOCKET_OPCODE_CLOSE",
            "WEBSOCKET_OPCODE_PING",
            "WEBSOCKET_OPCODE_PONG",
        ):
            self.assertLess(handler.index(opcode), error_position)

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

    def test_i2c_reuses_the_active_address_handle(self):
        source = (
            MQUICKJS / "src" / "modules" / "i2c" / "esp32_mquickjs_i2c.c"
        ).read_text(encoding="utf-8")
        worker_start = source.index("static void i2c_future_worker(")
        worker_end = source.index("\nstatic bool i2c_future_start(", worker_start)
        worker = source[worker_start:worker_end]

        self.assertIn("cached_device_handle", source)
        self.assertIn("cached_device_address", source)
        self.assertIn("i2c_get_cached_device", worker)
        self.assertNotIn("i2c_master_bus_add_device", worker)
        self.assertNotIn("i2c_master_bus_rm_device", worker)
        self.assertIn(
            "i2c_master_bus_rm_device(slot->cached_device_handle)", source
        )


if __name__ == "__main__":
    unittest.main()
