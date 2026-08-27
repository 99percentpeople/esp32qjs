import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MQUICKJS = ROOT / "components" / "esp32_mquickjs"


class EspNowArchitectureTests(unittest.TestCase):
    def test_feature_is_public_and_uses_internal_wifi_radio(self):
        catalog = json.loads(
            (MQUICKJS / "runtime-features.json").read_text(encoding="utf-8")
        )
        features = {entry["id"]: entry for entry in catalog["features"]}
        kconfig = (MQUICKJS / "Kconfig.projbuild").read_text(encoding="utf-8")

        self.assertEqual(features["espnow"]["requires"], ["wifi_radio"])
        self.assertNotEqual(features["espnow"].get("public"), False)
        self.assertIn("config ESP32_MQUICKJS_FEATURE_ESPNOW", kconfig)
        self.assertIn("select ESP32_MQUICKJS_WIFI_RADIO", kconfig)

    def test_receive_callback_uses_fixed_pool_and_never_calls_js(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        callback_start = source.index("static void espnow_receive_callback")
        callback_end = source.index("static void espnow_send_callback")
        callback = source[callback_start:callback_end]

        self.assertIn("esp32_mquickjs_wireless_pool_acquire", callback)
        self.assertIn("ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST", source)
        self.assertIn("esp32_mquickjs_event_queue_send", callback)
        self.assertIn("session->generation", callback)
        self.assertNotIn("JS_Call(", callback)
        self.assertNotIn("JS_New", callback)
        self.assertNotIn("heap_caps_malloc", callback)

    def test_receive_pool_has_drop_release_and_generation_lifecycle(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")

        self.assertIn("espnow_receive_event_drop", source)
        self.assertIn("esp32_mquickjs_wireless_pool_release", source)
        self.assertIn("esp32_mquickjs_new_owned_byte_view", source)
        self.assertIn("esp_now_unregister_recv_cb", source)
        self.assertLess(
            source.index("esp_now_unregister_recv_cb"),
            source.index("esp_now_deinit()"),
        )
        self.assertIn("esp32_mquickjs_wifi_radio_release", source)

    def test_open_and_receive_are_native_future_drivers(self):
        header = (
            MQUICKJS / "internal/esp32_mquickjs_espnow.h"
        ).read_text(encoding="utf-8")
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_init_espnow_runtime", header)
        self.assertIn("esp32_mquickjs_deinit_espnow_runtime", header)
        for callback in ("capture", "start", "poll", "finish", "cancel", "destroy"):
            self.assertIn(f".{callback} = espnow_open_", source)
        self.assertIn("esp32_mquickjs_future_register_driver", source)
        self.assertIn("esp32_mquickjs_event_queue_register_receive_alias", source)

    def test_open_failure_reports_structured_error_and_failed_native_step(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        open_initialize = source[
            source.index("static void espnow_open_initialize") :
            source.index("static bool espnow_open_start")
        ]
        open_finish = source[
            source.index("static JSValue espnow_open_finish") :
            source.index("static esp32_mquickjs_cancel_result_t espnow_open_cancel")
        ]

        for step in (
            "wifi_radio_ensure_started",
            "wifi_radio_set_channel",
            "wifi_radio_get_channel",
            "esp_now_init",
            "esp_now_register_recv_cb",
            "esp_now_register_send_cb",
            "esp_now_set_pmk",
            "esp_now_add_broadcast_peer",
            "esp_now_set_wake_window",
            "esp_now_set_wake_interval",
            "wifi_radio_confirm_channel",
        ):
            self.assertIn(f'failed_step = "{step}"', open_initialize)
        self.assertIn("ESP_LOGE", open_initialize)
        self.assertIn('espnow_throw_error(ctx, "ESPNOW_NOT_OPEN"', open_finish)

    def test_reopen_waits_for_native_deinit_quiescence_without_blocking_start(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        open_start = source[
            source.index("static bool espnow_open_start") :
            source.index("static esp32_mquickjs_future_poll_t espnow_open_poll")
        ]
        close_native = source[
            source.index("static void espnow_close_native") :
            source.index("static bool espnow_allocate_receive_pool")
        ]

        self.assertIn("#define ESPNOW_REOPEN_QUIESCE_MS 50U", source)
        self.assertIn("espnow_note_native_deinit", close_native)
        self.assertIn("state->open_not_before_us", open_start)
        self.assertNotIn("vTaskDelay", open_start)
        self.assertIn("espnow_open_initialize(state)", open_start)
        self.assertIn("espnow_open_initialize(state)", source[
            source.index("static esp32_mquickjs_future_poll_t espnow_open_poll") :
            source.index("static JSValue espnow_open_finish")
        ])

    def test_open_confirms_channel_generation_after_espnow_initialization(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        open_initialize = source[
            source.index("static void espnow_open_initialize") :
            source.index("static bool espnow_open_start")
        ]

        confirm = open_initialize.rindex("esp32_mquickjs_wifi_radio_get_channel")
        self.assertGreater(confirm, open_initialize.index("esp_now_init"))
        self.assertGreater(confirm, open_initialize.index("esp_now_add_peer"))
        self.assertIn("state->channel_fixed &&", open_initialize)
        self.assertIn("actual_channel != state->channel", open_initialize)
        self.assertIn("session->channel_generation = actual_generation", open_initialize)

    def test_key_material_is_cleared_and_not_exposed(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")

        self.assertIn("esp32_mquickjs_wireless_secure_zero", source)
        status = source[source.rindex("static JSValue espnow_status_to_js"):]
        self.assertNotIn('"pmk"', status)
        self.assertNotIn('"lmk"', status)

    def test_peer_slots_are_generation_checked_and_keys_are_scrubbed(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")

        self.assertIn("espnow_peer_slot_t", source)
        self.assertIn("peer_generation", source)
        self.assertIn("esp_now_add_peer", source)
        self.assertIn("esp_now_mod_peer", source)
        self.assertIn("esp_now_del_peer", source)
        self.assertIn("ESPNOW_STALE_PEER", source)
        self.assertIn("esp32_mquickjs_wireless_secure_zero(peer->lmk", source)

    def test_send_uses_one_lane_and_callback_publishes_atomically(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        callback_start = source.index("static void espnow_send_callback")
        callback_end = source.index("static void espnow_reset_session_storage")
        callback = source[callback_start:callback_end]

        self.assertIn("s_espnow_tx_lane_key", source)
        self.assertIn("esp_now_send", source)
        self.assertIn("memory_order_release", callback)
        self.assertIn("esp32_mquickjs_future_wake", callback)
        self.assertNotIn("JS_Call(", callback)
        self.assertNotIn("JS_New", callback)
        self.assertNotIn("heap_caps_malloc", callback)

    def test_send_timeout_rebuilds_session_before_releasing_lane(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")

        recovery_start = source.index("static esp_err_t espnow_restore_native_session")
        recovery_end = source.index("static JSValue espnow_status_to_js", recovery_start)
        recovery = source[recovery_start:recovery_end]
        for token in (
            "esp_now_unregister_recv_cb",
            "esp_now_unregister_send_cb",
            "esp_now_deinit",
            "esp_now_init",
            "esp_now_register_recv_cb",
            "esp_now_register_send_cb",
            "esp_now_set_pmk",
            "esp_now_add_peer",
        ):
            self.assertIn(token, recovery)
        self.assertIn("espnow_restore_native_session(session)", recovery)
        self.assertIn("espnow_note_native_deinit", recovery)
        self.assertIn("s_espnow_reopen_not_before_us", recovery)
        self.assertIn("esp32_mquickjs_wireless_tx_begin_recovery", recovery)
        self.assertIn("esp32_mquickjs_wireless_tx_finish_recovery", recovery)

    def test_every_espnow_control_operation_has_a_future_driver(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")

        for driver in (
            "s_espnow_open_driver",
            "s_espnow_peer_add_driver",
            "s_espnow_peer_update_driver",
            "s_espnow_peer_close_driver",
            "s_espnow_send_driver",
            "s_espnow_power_save_driver",
            "s_espnow_session_close_driver",
        ):
            self.assertIn(driver, source)
        self.assertGreaterEqual(
            source.count("esp32_mquickjs_future_register_driver"), 2
        )


if __name__ == "__main__":
    unittest.main()
