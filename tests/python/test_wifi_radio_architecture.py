import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class WifiRadioArchitectureTests(unittest.TestCase):
    def test_shared_nvs_boot_helper_never_erases_application_state(self):
        header = (
            MQUICKJS / "internal/esp32_mquickjs_nvs_flash_boot.h"
        ).read_text(encoding="utf-8")
        source = (
            MQUICKJS
            / "src/modules/nvs/esp32_mquickjs_nvs_flash_boot.c"
        ).read_text(encoding="utf-8")
        wifi = (
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c"
        ).read_text(encoding="utf-8")
        public_nvs = (
            MQUICKJS / "src/modules/nvs/esp32_mquickjs_nvs.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_nvs_flash_ensure_initialized", header)
        self.assertIn("nvs_flash_init()", source)
        self.assertNotIn("nvs_flash_erase", source)
        self.assertNotIn("nvs_flash_erase", wifi)
        self.assertIn("esp32_mquickjs_nvs_flash_ensure_initialized", public_nvs)

    def test_wifi_driver_is_owned_by_the_boot_scoped_radio_service(self):
        header = (
            MQUICKJS / "internal/esp32_mquickjs_wifi_radio.h"
        ).read_text(encoding="utf-8")
        radio = (
            MQUICKJS
            / "src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c"
        ).read_text(encoding="utf-8")
        wifi = (
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c"
        ).read_text(encoding="utf-8")

        for token in (
            "esp32_mquickjs_wifi_radio_acquire",
            "esp32_mquickjs_wifi_radio_ensure_started",
            "esp32_mquickjs_wifi_radio_get_channel",
            "esp32_mquickjs_wifi_radio_set_channel",
            "esp32_mquickjs_wifi_radio_release",
            "esp32_mquickjs_wifi_radio_channel_key",
        ):
            self.assertIn(token, header)
        self.assertIn("esp_wifi_init(&config)", radio)
        self.assertIn("esp_wifi_start()", radio)
        self.assertIn("channel_generation", radio)
        self.assertNotIn("esp_wifi_init(", wifi)
        self.assertNotIn("err = esp_wifi_start();", wifi)
        self.assertIn("ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA", wifi)

    def test_wifi_reconciles_a_radio_started_before_its_event_handler(self):
        header = (
            MQUICKJS / "internal/esp32_mquickjs_wifi_radio.h"
        ).read_text(encoding="utf-8")
        radio = (
            MQUICKJS
            / "src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c"
        ).read_text(encoding="utf-8")
        wifi = (
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_wifi_radio_status_t", header)
        self.assertIn("esp32_mquickjs_wifi_radio_get_status", header)
        self.assertIn("esp32_mquickjs_wifi_radio_get_status", radio)

        init_once = wifi[
            wifi.index("static esp_err_t wifi_init_once") :
            wifi.index("static EventBits_t wifi_wait_for_bits")
        ]
        register = init_once.index("WIFI_EVENT_STA_START")
        reconcile = init_once.index("esp32_mquickjs_wifi_radio_get_status")
        self.assertLess(register, reconcile)
        self.assertIn("radio_status.started", init_once)

        ensure_started = wifi[
            wifi.index("esp_err_t esp32_mquickjs_wifi_ensure_started") :
            wifi.index("static const char *wifi_authmode_to_string")
        ]
        self.assertIn("esp32_mquickjs_wifi_radio_get_status", ensure_started)
        self.assertIn("radio_status.started", ensure_started)
        self.assertLess(
            ensure_started.index("radio_status.started"),
            ensure_started.index("wifi_wait_for_bits"),
        )

    def test_wifi_status_reports_the_boot_scoped_radio_snapshot(self):
        wifi = (
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c"
        ).read_text(encoding="utf-8")
        types = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        make_status = wifi[
            wifi.index("static JSValue wifi_make_status_object") :
            wifi.index("JSValue esp32_mquickjs_wifi_make_status_object")
        ]
        self.assertIn("esp32_mquickjs_wifi_radio_get_status", make_status)
        for field in ("mode", "channel", "channelGeneration", "clients"):
            self.assertIn(f'"{field}"', make_status)
        self.assertIn("interface WiFiRadioStatus", types)
        self.assertIn("radio: WiFiRadioStatus", types)

    def test_tx_power_uses_the_shared_radio_and_reports_actual_dbm(self):
        header = (
            MQUICKJS / "internal/esp32_mquickjs_wifi.h"
        ).read_text(encoding="utf-8")
        wifi = (
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c"
        ).read_text(encoding="utf-8")
        stdlib = (
            MQUICKJS / "src/core/mqjs_stdlib_esp32.c"
        ).read_text(encoding="utf-8")
        types = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        self.assertIn("js_wifi_set_tx_power", header)
        setter = wifi[
            wifi.index("JSValue js_wifi_set_tx_power") :
            wifi.index("JSValue js_wifi_status")
        ]
        self.assertIn("esp32_mquickjs_wifi_ensure_started", setter)
        self.assertIn("esp_wifi_set_max_tx_power", setter)
        self.assertIn("esp_wifi_get_max_tx_power", setter)
        self.assertIn('JS_CFUNC_DEF("setTxPower"', stdlib)
        self.assertIn("setTxPower(dbm: number): number", types)
        self.assertIn("maxTxPowerDbm: number | null", types)

    def test_wifi_controls_are_link_scoped_and_network_state_stays_in_net(self):
        header = (
            MQUICKJS / "internal/esp32_mquickjs_wifi_radio.h"
        ).read_text(encoding="utf-8")
        wifi = (
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi.c"
        ).read_text(encoding="utf-8")
        types = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        wifi_status = types[
            types.index("interface WiFiStatus") :
            types.index("interface WiFiScanResult")
        ]
        net_status = types[
            types.index("interface NetInterfaceStatus") :
            types.index("interface NetStatusEvent")
        ]
        make_status = wifi[
            wifi.index("static JSValue wifi_make_status_object") :
            wifi.index("JSValue esp32_mquickjs_wifi_make_status_object")
        ]

        for network_field in ("hostname", "ip", "netmask", "gateway"):
            self.assertNotIn(f"{network_field}:", wifi_status)
            self.assertNotIn(f'"{network_field}"', make_status)
        self.assertIn("ipv4: NetIPv4Status | null", net_status)
        self.assertIn("wifi_ps_type_t power_save", header)
        self.assertIn("esp_wifi_get_ps", wifi)
        self.assertIn("esp_wifi_set_ps", wifi)
        self.assertIn("setPowerSave(mode: WiFiPowerSaveMode)", types)

    def test_connect_and_scan_use_strict_structured_control_options(self):
        future = (
            MQUICKJS / "src/modules/wifi/esp32_mquickjs_wifi_future.c"
        ).read_text(encoding="utf-8")
        types = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        connect = future[
            future.index("static bool wifi_future_parse_connect") :
            future.index("static bool wifi_scan_future_prepare")
        ]
        scan = future[
            future.index("static bool wifi_scan_future_prepare") :
            future.index("static bool wifi_connect_future_prepare")
        ]

        self.assertIn("wifi_validate_option_keys", future)
        self.assertIn(
            "wifi.connect(ssid, options?) expects a string SSID and optional options object",
            connect,
        )
        for token in (
            "bssid_set",
            "scan_method",
            "sort_method",
            "threshold.rssi",
            "threshold.authmode",
            "pmf_cfg",
        ):
            self.assertIn(token, connect)
        for token in (
            "wifi_scan_config_t",
            "show_hidden",
            "scan_type",
            "scan_time",
            "timeout_ms",
        ):
            self.assertIn(token, scan)
        self.assertIn(
            "connect(ssid: string, options?: WiFiConnectOptions): WiFiStatus",
            types,
        )
        self.assertIn("scan(options?: WiFiScanOptions): WiFiScanResult[]", types)

    def test_internal_feature_is_a_transitive_wifi_dependency(self):
        catalog = json.loads(
            (MQUICKJS / "runtime-features.json").read_text(encoding="utf-8")
        )
        features = {entry["id"]: entry for entry in catalog["features"]}
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")

        self.assertEqual(features["wifi_radio"]["public"], False)
        self.assertEqual(features["wifi_radio"]["category"], "internal")
        self.assertEqual(features["wifi"]["requires"], ["net", "wifi_radio"])
        self.assertIn("config ESP32_MQUICKJS_WIFI_RADIO", kconfig)
        self.assertIn("select ESP32_MQUICKJS_WIFI_RADIO", kconfig)

    def test_channel_snapshot_initializes_optional_secondary_channel(self):
        radio = (
            MQUICKJS
            / "src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c"
        ).read_text(encoding="utf-8")
        get_channel = radio[
            radio.index("esp32_mquickjs_wifi_radio_get_channel(") :
            radio.index("esp32_mquickjs_wifi_radio_set_channel(")
        ]

        self.assertIn("uint8_t actual_primary = 0;", get_channel)
        self.assertIn(
            "wifi_second_chan_t actual_secondary = WIFI_SECOND_CHAN_NONE;",
            get_channel,
        )


if __name__ == "__main__":
    unittest.main()
