import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"
BLE_SOURCE = MQUICKJS / "src" / "modules" / "ble"


class BLEArchitectureTests(unittest.TestCase):
    def source(self) -> str:
        return "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted(BLE_SOURCE.glob("*.c"))
        )

    def test_feature_selects_locked_nimble_stack(self):
        catalog = json.loads(
            (MQUICKJS / "runtime-features.json").read_text(encoding="utf-8")
        )
        features = {entry["id"]: entry for entry in catalog["features"]}
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")

        self.assertEqual(features["ble"]["requires"], [])
        self.assertEqual(features["ble"]["targets"], ["esp32c3", "esp32s3"])
        self.assertIn(
            "depends on SOC_BLE_SUPPORTED && BT_NIMBLE_ENABLED && BT_CONTROLLER_ENABLED",
            kconfig,
        )
        for assignment in (
            "CONFIG_BT_ENABLED=y",
            "CONFIG_BT_NIMBLE_ENABLED=y",
            "CONFIG_BT_CONTROLLER_ENABLED=y",
            "CONFIG_BT_BLUEDROID_ENABLED=n",
            "CONFIG_BT_CONTROLLER_ONLY=n",
            "CONFIG_BT_CONTROLLER_DISABLED=n",
        ):
            self.assertIn(assignment, features["ble"]["sdkconfig"])
        for symbol in (
            "BT_NIMBLE_ROLE_CENTRAL",
            "BT_NIMBLE_ROLE_PERIPHERAL",
            "BT_NIMBLE_ROLE_OBSERVER",
            "BT_NIMBLE_ROLE_BROADCASTER",
            "BT_NIMBLE_GATT_CLIENT",
            "BT_NIMBLE_GATT_SERVER",
            "BT_NIMBLE_SECURITY_ENABLE",
            "BT_NIMBLE_NVS_PERSIST",
        ):
            self.assertIn(f"select {symbol}", kconfig)

    def test_gap_callbacks_use_fixed_pools_without_js_or_allocation(self):
        source = self.source()
        callback_start = source.index(
            "static int ble_gap_event_callback(struct ble_gap_event *event, void *arg)\n{"
        )
        callback_end = source.index("static void ble_host_task", callback_start)
        callback = source[callback_start:callback_end]

        self.assertIn("ble_scan_pool_acquire", callback)
        self.assertIn("BLE_HS_ADV_MAX_SZ", source)
        self.assertIn("ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST", source)
        self.assertNotIn("JS_Call(", callback)
        self.assertNotIn("JS_New", callback)
        self.assertNotIn("heap_caps_malloc", callback)

    def test_gap_and_connection_operations_are_native_future_drivers(self):
        source = self.source()

        for driver in (
            "s_ble_open_driver",
            "s_ble_scan_driver",
            "s_ble_scanner_close_driver",
            "s_ble_advertise_driver",
            "s_ble_advertiser_close_driver",
            "s_ble_connect_driver",
            "s_ble_connection_close_driver",
            "s_ble_pair_driver",
            "s_ble_exchange_mtu_driver",
            "s_ble_read_rssi_driver",
        ):
            self.assertIn(driver, source)
        self.assertIn("esp32_mquickjs_future_register_driver", source)
        self.assertIn("esp32_mquickjs_event_queue_register_receive_alias", source)

    def test_gap_operations_share_one_lane_and_stop_waits_for_callback(self):
        source = self.source()

        for driver in (
            "s_ble_scan_driver",
            "s_ble_scanner_close_driver",
            "s_ble_advertise_driver",
            "s_ble_advertiser_close_driver",
            "s_ble_connect_driver",
        ):
            start = source.index(
                f"static const esp32_mquickjs_future_driver_t {driver} ="
            )
            end = source.index("};", start)
            self.assertIn(".resource_key = ble_gap_resource_key", source[start:end])

        callback_start = source.index(
            "static int ble_gap_event_callback(struct ble_gap_event *event, void *arg)\n{"
        )
        callback_end = source.index("static void ble_host_task", callback_start)
        callback = source[callback_start:callback_end]
        self.assertIn("s_ble_scanner_stop_state", callback)
        self.assertIn("s_ble_advertiser_stop_state", callback)
        self.assertIn("BLE_GAP_EVENT_DISC_COMPLETE", callback)
        self.assertIn("BLE_GAP_EVENT_ADV_COMPLETE", callback)

    def test_closed_gap_handles_release_their_singleton_slots(self):
        source = self.source()

        scanner_start = source.rindex("static JSValue ble_scanner_close_finish")
        scanner_finish = source[
            scanner_start:source.index("static JSValue ble_bool_finish", scanner_start)
        ]
        advertiser_start = source.rindex(
            "static JSValue ble_advertiser_close_finish"
        )
        advertiser_finish = source[
            advertiser_start:
            source.index(
                "static const esp32_mquickjs_future_driver_t s_ble_advertise_driver",
                advertiser_start,
            )
        ]
        self.assertIn("ble_release_scanner(&s_ble)", scanner_finish)
        self.assertIn("ble_release_advertiser(&s_ble)", advertiser_finish)

    def test_gap_close_completes_when_nimble_reports_already_stopped(self):
        source = self.source()

        scanner_start = source.index("static bool ble_scanner_close_start")
        scanner_close = source[
            scanner_start:
            source.index("static JSValue ble_scanner_close_finish", scanner_start)
        ]
        advertiser_start = source.index("static bool ble_advertiser_close_start")
        advertiser_close = source[
            advertiser_start:
            source.index(
                "static JSValue ble_advertiser_close_finish", advertiser_start
            )
        ]
        self.assertIn(
            "if (rc == BLE_HS_EALREADY) s_ble.scanner.active = false;",
            scanner_close,
        )
        self.assertIn(
            "if (rc == BLE_HS_EALREADY) s_ble.advertiser.active = false;",
            advertiser_close,
        )

    def test_connect_options_are_applied(self):
        source = self.source()

        self.assertIn('ble_get_bool(ctx, options, "autoPair"', source)
        self.assertIn("state->auto_pair", source)
        self.assertIn("ble_hs_id_infer_auto(1", source)
        self.assertIn("ble_hs_id_set_rnd", source)

    def test_connection_status_tracks_gatt_lane_and_valid_rssi(self):
        source = self.source()

        self.assertIn("gatt_pending", source)
        self.assertIn("gattActive", source)
        self.assertIn("active_gatt_state", source)
        self.assertIn("rssi_valid", source)

    def test_gatt_operations_use_per_connection_lane_and_payload_bounds(self):
        source = self.source()

        self.assertIn("gatt_lane_key", source)
        for token in (
            "ble_gattc_disc_all_svcs",
            "ble_gattc_read",
            "ble_gattc_write_flat",
            "ble_gattc_exchange_mtu",
            "BLE_PAYLOAD_TOO_LARGE",
            "s_ble_gatt_read_driver",
            "s_ble_gatt_write_driver",
            "s_ble_subscribe_driver",
            "s_ble_subscription_close_driver",
        ):
            self.assertIn(token, source)

        timeout_start = source.index("static JSValue ble_future_on_timeout(")
        timeout_end = source.index(
            "static bool ble_allocate_connection_queues", timeout_start
        )
        timeout = source[timeout_start:timeout_end]
        for operation in (
            "BLE_OP_PAIR",
            "BLE_OP_EXCHANGE_MTU",
            "BLE_OP_READ_RSSI",
            "BLE_OP_SERVER_NOTIFY",
        ):
            self.assertIn(operation, timeout)

    def test_server_access_uses_native_cache_and_never_calls_js(self):
        source = self.source()
        callback_start = source.rindex(
            "static int ble_gatt_server_access(uint16_t conn_handle, uint16_t attr_handle,"
        )
        callback_end = source.index("static void ble_gatt_server_register", callback_start)
        callback = source[callback_start:callback_end]

        self.assertIn("store_writes", callback)
        self.assertIn("os_mbuf_append", callback)
        self.assertIn("ble_server_event_pool_acquire", callback)
        self.assertNotIn("JS_Call(", callback)
        self.assertNotIn("JS_New", callback)
        self.assertNotIn("heap_caps_malloc", callback)
        self.assertIn("s_ble_server_notify_driver", source)

    def test_close_stops_host_before_freeing_native_pools(self):
        source = self.source()
        worker_start = source.index("static void ble_close_worker(void *opaque)\n{")
        worker_end = source.index("static bool ble_adapter_close_capture", worker_start)
        worker = source[worker_start:worker_end]
        finish_start = source.index("static JSValue ble_adapter_close_finish")
        finish_end = source.index("static esp32_mquickjs_resource_key_t", finish_start)
        finish = source[finish_start:finish_end]

        self.assertLess(worker.index("nimble_port_stop"), worker.index("nimble_port_deinit"))
        self.assertIn("memory_order_release", worker)
        self.assertIn("esp32_mquickjs_future_submit_worker", source)
        self.assertIn("ble_free_pools", finish)

    def test_server_indications_use_a_single_confirm_lane(self):
        source = self.source()
        start = source.index(
            "static esp32_mquickjs_resource_key_t ble_server_notify_resource_key"
        )
        end = source.index("static const esp32_mquickjs_future_driver_t", start)
        resource_key = source[start:end]

        self.assertIn("return &s_ble_server_lane_key", resource_key)
        self.assertNotIn("gatt_lane_key", resource_key)


if __name__ == "__main__":
    unittest.main()
