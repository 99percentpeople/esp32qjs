import re
import unittest
from pathlib import Path

from source_contract_test_case import SourceContractTestCase


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class NetArchitectureTests(SourceContractTestCase):
    def test_network_consumers_depend_on_net_instead_of_wifi(self):
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")

        self.assertIn("config ESP32_MQUICKJS_FEATURE_NET", kconfig)
        for feature in ("WIFI", "TLS", "SOCKET", "HTTP", "HTTP_SERVER", "WEBSOCKET"):
            block = re.search(
                rf"config ESP32_MQUICKJS_FEATURE_{feature}\n(?P<body>.*?)(?=\nconfig |\nendmenu)",
                kconfig,
                re.DOTALL,
            )
            self.assertIsNotNone(block)
            self.assertIn("ESP32_MQUICKJS_FEATURE_NET", block.group("body"))
        for feature in ("HTTP", "HTTP_SERVER", "WEBSOCKET"):
            block = re.search(
                rf"config ESP32_MQUICKJS_FEATURE_{feature}\n(?P<body>.*?)(?=\nconfig |\nendmenu)",
                kconfig,
                re.DOTALL,
            )
            self.assertNotIn("depends on ESP32_MQUICKJS_FEATURE_WIFI", block.group("body"))

    def test_net_owns_stack_initialization_and_safe_interface_enumeration(self):
        net = (MQUICKJS / "src/modules/net/esp32_mquickjs_net.c").read_text(
            encoding="utf-8"
        )
        wifi = (MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c").read_text(
            encoding="utf-8"
        )
        server = (
            MQUICKJS / "src/modules/http/esp32_mquickjs_http_server.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp_netif_init()", net)
        self.assertIn("esp_event_loop_create_default()", net)
        self.assertIn("esp_netif_tcpip_exec", net)
        self.assertIn("esp_netif_next_unsafe", net)
        self.assertIn("esp_netif_get_all_preferred_ip6", net)
        self.assertNotIn("esp_netif_init()", wifi)
        self.assertNotIn("esp_netif_init()", server)

    def test_net_public_api_uses_bounded_convergent_event_queues(self):
        net = (MQUICKJS / "src/modules/net/esp32_mquickjs_net.c").read_text(
            encoding="utf-8"
        )
        stdlib = (MQUICKJS / "src/core/mqjs_stdlib_esp32.c").read_text(
            encoding="utf-8"
        )
        declarations = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        self.assertIn('JS_CFUNC_DEF("status", 0, js_net_status)', stdlib)
        self.assertIn('JS_CFUNC_DEF("watch", 0, js_net_watch)', stdlib)
        self.assertIn('JS_PROP_CLASS_DEF("net", &js_net_obj)', stdlib)
        self.assertIn("CONFIG_ESP32_MQUICKJS_NET_MAX_INTERFACES", net)
        self.assertIn("ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST", net)
        self.assertIn("interface NetInterfaceStatus", declarations)
        self.assertIn("watch(): EventQueue<NetStatusEvent>", declarations)

    def test_ip_event_callback_never_blocks_on_watchers(self):
        source = (
            MQUICKJS / "src/modules/net/esp32_mquickjs_net.c"
        ).read_text(encoding="utf-8")
        start = source.index("static void net_ip_event_handler(")
        end = source.index(
            "\nesp_err_t esp32_mquickjs_net_ensure_initialized(", start
        )
        handler = source[start:end]

        self.assertIn("net_try_lock()", handler)
        self.assertIn(
            "esp32_mquickjs_event_queue_try_send_from_callback(", handler
        )
        self.assertNotIn("net_lock()", handler)
        self.assertNotIn("esp32_mquickjs_event_queue_send(", handler)
        self.assertNotIn("portMAX_DELAY", handler)

    def test_time_and_websocket_use_transport_neutral_readiness(self):
        time = (MQUICKJS / "src/modules/time/esp32_mquickjs_time.c").read_text(
            encoding="utf-8"
        )
        websocket = (
            MQUICKJS / "src/modules/websocket/esp32_mquickjs_websocket.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_net_is_ready", time)
        self.assertIn("esp32_mquickjs_net_is_ready", websocket)
        self.assertNotIn("esp32_mquickjs_wifi_get_status", websocket)
        self.assertNotIn("IP_EVENT_STA_GOT_IP", time)


if __name__ == "__main__":
    unittest.main()
