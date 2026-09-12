/* Radio-local SDK mutation boundary. Include after SDK declarations and the
 * mutation-mutex-owned invalidation helper. Function-like self references
 * intentionally resolve to the original SDK call after macro expansion.
 *
 * Invalidate on attempt, including failed writes and rollback, except reviewed
 * writes that do not affect any STOP snapshot payload. Read-only SDK queries and
 * STOP are excluded; STOP captures and commits its own observation.
 * Other units cannot consume this private header. New Radio setters belong in
 * this inventory before a stopped observation can be trusted for restoration.
 */
#ifndef ESP32_MQUICKJS_WIFI_RADIO_MUTATION_H
#define ESP32_MQUICKJS_WIFI_RADIO_MUTATION_H

#define WIFI_RADIO_MUTATION(call) (wifi_radio_invalidate_stop_snapshot_locked(), (call))
/* Fixed SDK fff9895c82 C3/S3/C5: storage changes only the future-write policy;
 * RSSI changes only a one-shot observer threshold, excluded from restart replay.
 * Event mask changes only observation filtering, captured afresh by restart.
 * FTM responder offset writes only the T1 correction scalar via synchronous
 * IPC; its separate accepted-value record is captured and replayed pre-start.
 * These writes do not change saved power/band/channel/inactive-time observations. Driver
 * faults, unknown storage, generation and owner gates still apply separately.
 * Unreviewed target implementations retain conservative invalidation. */
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C5
#define WIFI_RADIO_STOP_NEUTRAL(call) (call)
#else
#define WIFI_RADIO_STOP_NEUTRAL(call) WIFI_RADIO_MUTATION(call)
#endif
#define esp_wifi_ftm_initiate_session(...) WIFI_RADIO_MUTATION(esp_wifi_ftm_initiate_session(__VA_ARGS__))
/* Statistics affect only their native allocation/counter state. Their separate
 * STOP/restart capture does not consume power/band/channel observations. */
#define esp_wifi_enable_rx_statistics(...) WIFI_RADIO_STOP_NEUTRAL(esp_wifi_enable_rx_statistics(__VA_ARGS__))
#define esp_wifi_enable_tx_statistics(...) WIFI_RADIO_STOP_NEUTRAL(esp_wifi_enable_tx_statistics(__VA_ARGS__))
#define esp_wifi_enable_bsscolor_collision_detection(...) WIFI_RADIO_MUTATION(esp_wifi_enable_bsscolor_collision_detection(__VA_ARGS__))
#define esp_wifi_ftm_end_session(...) WIFI_RADIO_MUTATION(esp_wifi_ftm_end_session(__VA_ARGS__))
#define esp_wifi_ftm_resp_set_offset(...) WIFI_RADIO_STOP_NEUTRAL(esp_wifi_ftm_resp_set_offset(__VA_ARGS__))
#define esp_wifi_action_tx_req(...) WIFI_RADIO_MUTATION(esp_wifi_action_tx_req(__VA_ARGS__))
#define esp_wifi_remain_on_channel(...) WIFI_RADIO_MUTATION(esp_wifi_remain_on_channel(__VA_ARGS__))
#define esp_wifi_coex_pwr_configure(...) WIFI_RADIO_MUTATION(esp_wifi_coex_pwr_configure(__VA_ARGS__))
#define esp_wifi_config_11b_rate(...) WIFI_RADIO_MUTATION(esp_wifi_config_11b_rate(__VA_ARGS__))
#define esp_wifi_config_80211_tx(...) WIFI_RADIO_MUTATION(esp_wifi_config_80211_tx(__VA_ARGS__))
#define esp_wifi_connectionless_module_set_wake_interval(...) WIFI_RADIO_MUTATION(esp_wifi_connectionless_module_set_wake_interval(__VA_ARGS__))
#define esp_wifi_deinit(...) WIFI_RADIO_MUTATION(esp_wifi_deinit(__VA_ARGS__))
#define esp_wifi_disable_pmf_config(...) WIFI_RADIO_MUTATION(esp_wifi_disable_pmf_config(__VA_ARGS__))
#define esp_wifi_init(...) WIFI_RADIO_MUTATION(esp_wifi_init(__VA_ARGS__))
#define esp_wifi_restore(...) WIFI_RADIO_MUTATION(esp_wifi_restore(__VA_ARGS__))
#define esp_wifi_set_band(...) WIFI_RADIO_MUTATION(esp_wifi_set_band(__VA_ARGS__))
#define esp_wifi_set_band_mode(...) WIFI_RADIO_MUTATION(esp_wifi_set_band_mode(__VA_ARGS__))
#define esp_wifi_set_bandwidth(...) WIFI_RADIO_MUTATION(esp_wifi_set_bandwidth(__VA_ARGS__))
#define esp_wifi_set_bandwidths(...) WIFI_RADIO_MUTATION(esp_wifi_set_bandwidths(__VA_ARGS__))
#define esp_wifi_set_channel(...) WIFI_RADIO_MUTATION(esp_wifi_set_channel(__VA_ARGS__))
#define esp_wifi_set_config(...) WIFI_RADIO_MUTATION(esp_wifi_set_config(__VA_ARGS__))
#define esp_wifi_set_country(...) WIFI_RADIO_MUTATION(esp_wifi_set_country(__VA_ARGS__))
#define esp_wifi_set_country_code(...) WIFI_RADIO_MUTATION(esp_wifi_set_country_code(__VA_ARGS__))
#define esp_wifi_set_dynamic_cs(...) WIFI_RADIO_MUTATION(esp_wifi_set_dynamic_cs(__VA_ARGS__))
#define esp_wifi_set_event_mask(...) WIFI_RADIO_STOP_NEUTRAL(esp_wifi_set_event_mask(__VA_ARGS__))
#define esp_wifi_set_inactive_time(...) WIFI_RADIO_MUTATION(esp_wifi_set_inactive_time(__VA_ARGS__))
#define esp_wifi_set_mac(...) WIFI_RADIO_MUTATION(esp_wifi_set_mac(__VA_ARGS__))
#define esp_wifi_set_max_tx_power(...) WIFI_RADIO_MUTATION(esp_wifi_set_max_tx_power(__VA_ARGS__))
#define esp_wifi_set_mode(...) WIFI_RADIO_MUTATION(esp_wifi_set_mode(__VA_ARGS__))
#define esp_wifi_set_protocol(...) WIFI_RADIO_MUTATION(esp_wifi_set_protocol(__VA_ARGS__))
#define esp_wifi_set_protocols(...) WIFI_RADIO_MUTATION(esp_wifi_set_protocols(__VA_ARGS__))
#define esp_wifi_set_ps(...) WIFI_RADIO_MUTATION(esp_wifi_set_ps(__VA_ARGS__))
#define esp_wifi_set_rssi_threshold(...) WIFI_RADIO_STOP_NEUTRAL(esp_wifi_set_rssi_threshold(__VA_ARGS__))
#define esp_wifi_set_storage(...) WIFI_RADIO_STOP_NEUTRAL(esp_wifi_set_storage(__VA_ARGS__))
#define esp_wifi_set_vendor_ie(...) WIFI_RADIO_MUTATION(esp_wifi_set_vendor_ie(__VA_ARGS__))
#define esp_wifi_start(...) WIFI_RADIO_MUTATION(esp_wifi_start(__VA_ARGS__))

#endif
