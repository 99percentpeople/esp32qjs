"""Cross-language frame-name contract; native behavior belongs in tests/c."""
import re
import unittest

from tests.support.paths import ROOT


class WiFiFrameContract(unittest.TestCase):
    def test_public_names_match_the_shared_native_catalogue(self):
        types = (ROOT / "types/esp32qjs-c-api.d.ts").read_text()
        native = (ROOT / "components/esp32_mquickjs/src/modules/wifi_common/esp32_mquickjs_wifi_rx.c").read_text()
        declaration = re.search(r"type WiFiFrameName\s*=([^;]+);", types).group(1)
        public_names = re.findall(r'"([a-z0-9-]+)"', declaration)
        native_names = []
        for category in ("management", "control", "data"):
            table = re.search(r"s_" + category + r"_names\[16\]\s*=\s*\{([^}]+)\}", native).group(1)
            native_names.extend(re.findall(r'"([a-z0-9-]+)"', table))
        self.assertEqual(len(public_names), len(set(public_names)))
        self.assertEqual(set(public_names), set(native_names))

    def test_monitor_and_csi_share_the_filter_contract(self):
        types = (ROOT / "types/esp32qjs-c-api.d.ts").read_text()
        for name in ("WiFiMonitorOptions", "WiFiCsiOpenOptions"):
            declaration = types.split("interface " + name + " {", 1)[1].split("\n  }", 1)[0]
            self.assertIn("filter?: WiFiRxFilter;", declaration)
        declaration = types.split("interface WiFiRxFilter {", 1)[1].split("\n  }", 1)[0]
        self.assertIn("frames?: WiFiFrameSelector[];", declaration)
