import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class TransportArchitectureTests(unittest.TestCase):
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
            "js_socket_get_max_message_bytes",
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
        self.assertNotIn("token", source)

    def test_websocket_control_frames_are_not_application_errors(self):
        source = (
            MQUICKJS / "src" / "modules" / "websocket" / "esp32_mquickjs_websocket.c"
        ).read_text(encoding="utf-8")

        control_check = """if (data->op_code == WEBSOCKET_OPCODE_CLOSE ||
        data->op_code == WEBSOCKET_OPCODE_PING ||
        data->op_code == WEBSOCKET_OPCODE_PONG) {
        return;
    }"""
        self.assertIn(control_check, source)
        self.assertLess(
            source.index(control_check),
            source.index('websocket_enqueue_error("only complete WebSocket text'),
        )

    def test_transport_callbacks_use_the_guarded_call_gateway(self):
        sources = (
            MQUICKJS / "src" / "modules" / "usb_serial" / "esp32_mquickjs_usb_serial.c",
            MQUICKJS / "src" / "modules" / "websocket" / "esp32_mquickjs_websocket.c",
        )

        for source in sources:
            text = source.read_text(encoding="utf-8")
            self.assertIn("esp32_mquickjs_call(", text, source)
            self.assertNotIn("JS_Call(", text, source)


if __name__ == "__main__":
    unittest.main()
