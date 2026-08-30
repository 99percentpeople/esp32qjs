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

        self.assertIn("esp32_mquickjs_native_pool_acquire", callback)
        self.assertIn("ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST", source)
        self.assertIn(
            "esp32_mquickjs_event_queue_try_send_from_callback", callback
        )
        self.assertNotIn("esp32_mquickjs_event_queue_send(", callback)
        self.assertIn("session->generation", callback)
        self.assertNotIn("JS_Call(", callback)
        self.assertNotIn("JS_New", callback)
        self.assertNotIn("heap_caps_malloc", callback)

    def test_receive_sequence_accounts_for_pool_exhaustion(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        callback_start = source.index("static void espnow_receive_callback")
        callback_end = source.index("static void espnow_send_callback")
        callback = source[callback_start:callback_end]

        sequence = callback.index("&session->sequence")
        pool_acquire = callback.index("esp32_mquickjs_native_pool_acquire")
        self.assertLess(
            sequence,
            pool_acquire,
            "valid native packets must consume a sequence before a full RX pool drops them",
        )

    def test_receive_pool_has_drop_release_and_generation_lifecycle(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")

        self.assertIn("espnow_receive_event_drop", source)
        self.assertIn("esp32_mquickjs_native_pool_release", source)
        self.assertIn("esp32_mquickjs_new_owned_byte_view", source)
        self.assertIn("esp_now_unregister_recv_cb", source)
        self.assertLess(
            source.index("esp_now_unregister_recv_cb"),
            source.index("esp_now_deinit()"),
        )
        self.assertIn("esp32_mquickjs_wifi_radio_release", source)

    def test_close_retains_storage_until_callbacks_are_quiescent(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        begin_start = source.index("static void espnow_begin_close")
        finish_start = source.index("static bool espnow_finish_close", begin_start)
        worker_start = source.index("static void espnow_close_worker", begin_start)
        begin = source[begin_start:finish_start]
        finish = source[finish_start:worker_start]
        worker = source[
            worker_start : source.index(
                "static bool espnow_schedule_background_close", worker_start
            )
        ]

        self.assertIn("esp_now_unregister_recv_cb", begin)
        self.assertIn("esp_now_unregister_send_cb", begin)
        self.assertNotIn("espnow_reset_session_storage", begin)
        self.assertIn("callbacks_active", worker)
        self.assertIn("espnow_finish_close(session)", worker)
        self.assertIn("callbacks_active", finish)
        self.assertIn("espnow_reset_session_storage", finish)
        self.assertIn("esp32_mquickjs_event_queue_retain", source)
        self.assertIn("esp32_mquickjs_event_queue_release", finish)
        self.assertIn("ESPNOW_CLEANUP_PENDING", source)
        self.assertIn(".on_timeout = espnow_close_on_timeout", source)

    def test_send_timeout_recovery_retains_future_until_callbacks_are_quiescent(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        begin_start = source.index("static esp_err_t espnow_begin_timeout_recovery")
        worker_start = source.index("static void espnow_send_recovery_worker")
        schedule_start = source.index("static bool espnow_schedule_send_recovery")
        poll_start = source.index("static esp32_mquickjs_future_poll_t espnow_send_poll")
        begin = source[begin_start:worker_start]
        worker = source[worker_start:schedule_start]
        schedule = source[schedule_start:poll_start]
        poll = source[
            poll_start : source.index("static JSValue espnow_send_finish", poll_start)
        ]

        self.assertIn("esp_now_unregister_recv_cb", begin)
        self.assertIn("esp_now_unregister_send_cb", begin)
        self.assertNotIn("state->completed", begin)
        self.assertNotRegex(begin + worker, r"waits\s*\+\+\s*<")
        self.assertIn("callbacks_active", worker)
        self.assertLess(worker.index("callbacks_active"), worker.index("esp_now_deinit"))
        self.assertNotIn("espnow_restore_native_session", worker)
        self.assertIn("ESPNOW_LIFECYCLE_FAILED", worker)
        self.assertLess(
            worker.index("&state->completed"),
            worker.index("&state->recovery_pending"),
        )
        self.assertIn("esp32_mquickjs_future_submit_worker", schedule)
        self.assertIn("recovery_pending", poll)
        self.assertIn("ESPNOW_RECOVERY_PENDING", source)

    def test_session_close_disposes_native_event_queue_without_waiting_for_gc(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        close_capture = source[
            source.index("static bool espnow_session_close_capture") :
            source.index("static bool espnow_control_start")
        ]
        destroy = source[
            source.index("static void espnow_control_destroy") :
            source.index("static esp32_mquickjs_resource_key_t espnow_control_resource_key")
        ]

        self.assertIn("state->event_queue_ref", close_capture)
        self.assertIn("state->event_queue_rooted = true", close_capture)
        self.assertIn("esp32_mquickjs_event_queue_dispose", destroy)
        self.assertIn("JS_DeleteGCRef", destroy)

    def test_explicit_close_retires_gc_owned_native_handle_refs(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        destroy = source[
            source.index("static void espnow_control_destroy") :
            source.index("static esp32_mquickjs_resource_key_t espnow_control_resource_key")
        ]
        session_finalizer = source[
            source.index("void js_espnow_session_finalizer") :
            source.index("JSValue js_espnow_session_receive")
        ]

        self.assertIn("s_espnow_closed_session_ref", source)
        self.assertIn("s_espnow_closed_peer_ref", source)
        self.assertIn("espnow_retire_handle", destroy)
        self.assertIn("JS_SetOpaque", source)
        self.assertIn("ref != &s_espnow_closed_session_ref", session_finalizer)

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

    def test_power_save_can_be_disabled_and_native_defaults_are_restored(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        types = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        parser = source[
            source.index("static bool espnow_parse_power_save") :
            source.index("static bool espnow_parse_open_options")
        ]
        close_native = source[
            source.index("static void espnow_close_native") :
            source.index("static bool espnow_allocate_receive_pool")
        ]
        control = source[
            source.index("case ESPNOW_OPERATION_SET_POWER_SAVE:") :
            source.index("case ESPNOW_OPERATION_CLOSE_SESSION:")
        ]

        self.assertIn('"enabled"', parser)
        self.assertIn("ESP_WIFI_CONNECTIONLESS_INTERVAL_DEFAULT_MODE", source)
        self.assertIn("UINT16_MAX", close_native)
        self.assertIn("state->power_save_enabled", control)
        self.assertIn("session->power_save_enabled =", control)
        self.assertIn("enabled: false", types)

    def test_default_open_preserves_native_connectionless_defaults(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        open_initialize = source[
            source.index("static void espnow_open_initialize") :
            source.index("static bool espnow_open_start")
        ]
        restore = source[
            source.index("static esp_err_t espnow_restore_native_session") :
            source.index("static esp_err_t espnow_begin_timeout_recovery")
        ]

        self.assertIn(
            "state->err == ESP_OK && session->power_save_enabled",
            open_initialize,
        )
        self.assertNotIn(
            "session->power_save_enabled ? session->wake_window_ms",
            open_initialize,
        )
        self.assertIn("if (!session->power_save_enabled)", restore)
        self.assertNotIn(
            "session->power_save_enabled ? session->wake_window_ms", restore
        )

    def test_ai_wireless_doc_lists_the_complete_resource_surface(self):
        wireless = (ROOT / "docs/ai/wireless.md").read_text(encoding="utf-8")

        for token in (
            "session.stats()",
            "session.peer(address)",
            "session.peers()",
            "session.setPowerSave(options)",
            "RSSI",
        ):
            self.assertIn(token, wireless)

    def test_peer_rate_capability_is_not_advertised_without_a_public_api(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")
        types = (ROOT / "types/esp32qjs-c-api.d.ts").read_text(
            encoding="utf-8"
        )

        capabilities = source[
            source.index("JSValue js_espnow_capabilities") :
            source.index("JSValue js_espnow_open")
        ]
        self.assertIn('"peerRateConfig",\n                                         JS_FALSE', capabilities)
        self.assertIn("readonly peerRateConfig: false", types)

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

    def test_send_timeout_requires_explicit_recover_before_reopening_lane(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")

        timeout_start = source.index("static void espnow_send_recovery_worker")
        timeout_end = source.index("static bool espnow_schedule_send_recovery")
        timeout_worker = source[timeout_start:timeout_end]
        recovery_start = source.index("static esp_err_t espnow_restore_native_session")
        recovery_end = source.index("static bool espnow_recover_start", recovery_start)
        recovery = source[recovery_start:recovery_end]
        for token in (
            "esp_now_init",
            "esp_now_register_recv_cb",
            "esp_now_register_send_cb",
            "esp_now_set_pmk",
            "esp_now_add_peer",
        ):
            self.assertIn(token, recovery)
        self.assertNotIn("espnow_restore_native_session", timeout_worker)
        self.assertIn("espnow_restore_native_session(session)", recovery)
        self.assertIn("espnow_note_native_deinit", recovery)
        self.assertIn("s_espnow_reopen_not_before_us", recovery)
        self.assertIn("esp32_mquickjs_wireless_tx_retry_recovery", source)
        self.assertIn("esp32_mquickjs_wireless_tx_finish_recovery", recovery)
        self.assertIn('"recover"', source)

    def test_every_espnow_control_operation_has_a_future_driver(self):
        source = (
            MQUICKJS / "src/modules/espnow/esp32_mquickjs_espnow.c"
        ).read_text(encoding="utf-8")

        for driver in (
            "s_espnow_open_driver",
            "s_espnow_peer_add_driver",
            "s_espnow_peer_update_driver",
            "s_espnow_peer_remove_driver",
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
