#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#include <stdint.h>
#include <stdatomic.h>

#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_wifi_action_lane.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define ESP32_MQUICKJS_WIFI_MAX_SCAN_RECORDS 32U
#define ESP32_MQUICKJS_WIFI_MAX_WATCHERS 4U
#define ESP32_MQUICKJS_WIFI_MAX_WATCH_CAPACITY 64U
#define ESP32_MQUICKJS_WIFI_WATCH_INGRESS_CAPACITY 32U
#define ESP32_MQUICKJS_WIFI_WATCH_NEIGHBOR_SLOTS 2U
#define ESP32_MQUICKJS_WIFI_WATCH_MAX_NEIGHBORS 64U
#define ESP32_MQUICKJS_WIFI_WATCH_MAX_REPORT_BYTES 4096U

JSValue js_wifi_watch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
#if CONFIG_ESP_WIFI_RRM_SUPPORT || CONFIG_ESP_WIFI_WNM_SUPPORT || CONFIG_ESP_WIFI_11R_SUPPORT
JSValue js_wifi_roaming_watch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
#endif
JSValue esp32_mquickjs_wifi_watch_status(JSContext *ctx);
void esp32_mquickjs_wifi_watch_reset_counters(void);
void esp32_mquickjs_wifi_reset_connection_counters(void);
JSValue esp32_mquickjs_wifi_ap_status(JSContext *ctx);
void esp32_mquickjs_wifi_watch_capture(int32_t id, const void *data, uint32_t generation);
void esp32_mquickjs_deinit_wifi_watch_runtime(void);
bool esp32_mquickjs_wifi_watch_controlled(int32_t id);
bool esp32_mquickjs_wifi_watch_control_ready(int32_t id, uint32_t generation);
JSValue esp32_mquickjs_wifi_ssid_text(JSContext *ctx, const uint8_t *ssid, size_t length);
bool esp32_mquickjs_wifi_set_ssid_properties(JSContext *ctx, JSValue *object,
    const uint8_t *ssid, size_t length);
/* Pure capture/validation shared by connect and the upcoming configuration transaction. */
bool esp32_mquickjs_wifi_parse_connect_config(JSContext *ctx, int argc,
    JSGCRef *argv, wifi_config_t *config, uint32_t *timeout_ms);
bool esp32_mquickjs_wifi_parse_station_config(JSContext *ctx, int argc,
    JSGCRef *argv, wifi_config_t *config, uint32_t *timeout_ms);
bool esp32_mquickjs_wifi_parse_station_config_for_operation(JSContext *ctx, int argc,
    JSGCRef *argv, wifi_config_t *config, uint32_t *timeout_ms, const char *operation);
/* Native input capture for configure and public driver transactions.
 * Owns no driver/helper resources; caller must wipe successful output. */
bool esp32_mquickjs_wifi_parse_driver_config(JSContext *ctx, JSValue options,
    wifi_interface_t interface, wifi_config_t *config);
bool esp32_mquickjs_wifi_parse_driver_config_for_operation(JSContext *ctx, JSValue options,
    wifi_interface_t interface, wifi_config_t *config, const char *operation);
JSValue js_wifi_driver_restore(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_capabilities(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_interface_config(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_interface_config(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
/* Complete bounded outer capture; still internal until the public binding and
 * result/error contract are wired. Omitted lifecycle
 * selections remain explicit presence bits, never inferred by this parser.
 * Caller allocates this storage and securely wipes it after use. */
typedef struct {
    bool mode_set, storage_set, start_set;
    wifi_mode_t mode;
    wifi_storage_t storage;
    bool start, allow_disconnect;
    bool station_set, access_point_set;
    wifi_config_t station, access_point;
    esp32_mquickjs_wifi_radio_config_controls_t controls;
    esp32_mquickjs_wifi_radio_start_controls_t start_controls;
} esp32_mquickjs_wifi_configuration_t;
bool esp32_mquickjs_wifi_capture_stop_ap_timeout(JSContext *ctx, JSValue value, uint32_t *timeout_ms);
bool esp32_mquickjs_wifi_capture_stop(JSContext *ctx, JSValue options, uint32_t *timeout_ms);
bool esp32_mquickjs_wifi_capture_restart(JSContext *ctx, JSValue options, uint32_t *timeout_ms, bool *allow_ap_restart);
JSValue js_wifi_driver_restart(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
bool esp32_mquickjs_wifi_capture_start(JSContext *ctx, JSValue options,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection);
esp_err_t esp32_mquickjs_wifi_ap_begin_start_configuration(
    esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, bool *already_started);
bool esp32_mquickjs_wifi_capture_configuration(JSContext *ctx, JSValue options,
    esp32_mquickjs_wifi_configuration_t *configuration);
/* Per-call progress, without credentials or pointers into caller storage. */
typedef struct {
    const char *stage;
    bool admitted, stop_attempted, configuration_attempted, resume_attempted;
} esp32_mquickjs_wifi_configuration_execution_t;
JSValue esp32_mquickjs_wifi_throw_configuration_error(JSContext *ctx, esp_err_t error,
    const char *option, const esp32_mquickjs_wifi_configuration_execution_t *execution);
JSValue js_wifi_configure(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
/* Internal captured-input executor. Caller wipes the entire capture on every
 * exit; resolved selections are returned only after the full transaction. */
esp_err_t esp32_mquickjs_wifi_apply_configuration(
    esp32_mquickjs_wifi_configuration_t *configuration,
    esp32_mquickjs_wifi_configuration_execution_t *execution);
esp_err_t esp32_mquickjs_wifi_activate_ap(wifi_config_t *config);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
bool esp32_mquickjs_wifi_parse_start_ap_config(JSContext *ctx, JSValue input,
    wifi_config_t *config, bool *allow_disconnect);
bool esp32_mquickjs_wifi_parse_ap_config(JSContext *ctx, JSValue options,
    wifi_config_t *config, bool *allow_disconnect);
bool esp32_mquickjs_wifi_parse_ap_config_for_operation(JSContext *ctx, JSValue options,
    wifi_config_t *config, const char *operation);
#endif

typedef struct {
    uint32_t generation;
    uint32_t status;
} esp32_mquickjs_wifi_scan_event_t;

typedef struct {
    uint32_t generation;
    uint32_t kind;
    int32_t reason;
    uint64_t completed_us;
    esp32_mquickjs_wifi_link_snapshot_t link;
} esp32_mquickjs_wifi_connect_event_t;

typedef struct {
    const char *cleanup_stage;
    esp_err_t cleanup_error;
    bool cleanup_timer_stopped;
    bool cleanup_radio_released;
    bool runtime_cleanup_pending;
    bool initialized;
    bool started;
    bool connected;
    bool connect_start_active;
    bool scan_start_active;
    esp32_mquickjs_wifi_radio_operation_t radio_operation;
    bool connect_in_progress;
    bool connect_draining;
    bool disconnect_active;
    bool disconnect_submitted;
    bool disconnect_seen;
    bool disconnect_fence_posted;
    bool disconnect_fence_seen;
    bool disconnect_epoch_exhausted;
    uint32_t disconnect_epoch;
    esp_err_t disconnect_cleanup_error;
    bool scan_in_progress;
    bool scan_draining;
    bool scan_results_pending;
    bool scan_stop_submitted;
    bool scan_stop_active;
    esp_err_t scan_cleanup_error;
    wifi_scan_config_t native_scan_config;
    /* Driver may retain filters beyond the public Future's lifetime. */
    uint8_t native_scan_ssid[33];
    uint8_t native_scan_bssid[6];
    bool scan_future_registered;
    bool connect_future_registered;
    uint32_t connection_future_operation;
    EventGroupHandle_t event_group;
    QueueHandle_t scan_queue;
    QueueHandle_t connect_queue;
    QueueHandle_t driver_event_queue;
    SemaphoreHandle_t lock;
    esp_netif_t *sta_netif;
    esp_err_t sta_detach_error;
    esp_event_handler_instance_t wifi_start_event_instance;
    esp_event_handler_instance_t wifi_connected_event_instance;
    esp_event_handler_instance_t wifi_disconnect_event_instance;
    esp_event_handler_instance_t wifi_scan_event_instance;
    esp_event_handler_instance_t ip_event_instance;
    esp_event_handler_instance_t control_event_instance;
    uint32_t scan_generation;
    uint32_t connect_generation;
    esp32_mquickjs_future_token_t scan_future_token;
    esp32_mquickjs_future_token_t connect_future_token;
    esp_timer_handle_t connect_timeout_timer;
    esp32_mquickjs_wifi_radio_lease_t radio_lease;
    esp32_mquickjs_wifi_status_t status;
    _Atomic uint32_t callbacks_active;
    _Atomic uint32_t dropped_driver_events;
} esp32_mquickjs_wifi_state_t;

enum {
    ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS = 1,
    ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_FAILURE = 2,
    ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT = 3,
    ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED = 4,
};

enum {
    ESP32_MQUICKJS_WIFI_OPERATION_NONE = 0,
    ESP32_MQUICKJS_WIFI_OPERATION_CONNECT = 1,
    ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT = 2,
};

bool esp32_mquickjs_init_wifi_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_init_wifi_future_runtime(JSContext *ctx,
                                             esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_wifi_runtime(JSContext *ctx);

JSValue js_wifi_connect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_disconnect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_set_power_save(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_set_tx_power(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
/* Runtime-task coordinator hooks. Caller has drained operations/events and
 * owns an exact stopped lifecycle with all leases released. Neither helper
 * changes driver mode/storage nor retires the caller's lifecycle token. */
esp_err_t esp32_mquickjs_wifi_prepare_for_configuration(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_retire_for_configuration(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
/* Runtime-task handoff: exact three-owner admission, then release AP's lease.
 * A successful handoff makes ordinary AP cleanup unavailable until the caller
 * retires its netif with this same token. No native stop/configuration here. */
esp_err_t esp32_mquickjs_wifi_ap_netif_cleanup_error(void);
/* Runtime-task APSTA close and explicit whole-cleanup handoff. */
esp_err_t esp32_mquickjs_wifi_reopen_ap_shared(wifi_config_t *config, bool *handled);
esp_err_t esp32_mquickjs_wifi_ap_reopen(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station, wifi_config_t *config,
    esp32_mquickjs_wifi_radio_lifecycle_t *token);
bool esp32_mquickjs_wifi_ap_stop_pending(void);
esp_err_t esp32_mquickjs_wifi_stop_ap_shared(bool *handled);
esp_err_t esp32_mquickjs_wifi_ap_begin_partial_stop(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_ap_retire_partial_stop(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_ap_finish_partial_stop(esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_ap_adopt_partial_stop(const esp32_mquickjs_wifi_radio_lifecycle_t *token);

/* Internal typed-native executor. Caller has fully captured/validated configs;
 * pending operations are rejected. allow_disconnect authorizes this Station
 * disconnect and any running AP stop, never shutdown of another feature. */
esp_err_t esp32_mquickjs_wifi_configure_interfaces(wifi_mode_t mode,
    wifi_storage_t storage, bool start, wifi_config_t *station,
    esp32_mquickjs_wifi_config_accept_fn accept_station, wifi_config_t *access_point,
    esp32_mquickjs_wifi_config_accept_fn accept_access_point,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    const esp32_mquickjs_wifi_radio_start_controls_t *start_controls, bool allow_disconnect);
/* After Future prepare has requested cancellation, advance an admitted physical
 * recovery even if its original native owner or another Future is still pending.
 * Does not start recovery, restore configuration or release the original owner. */
bool esp32_mquickjs_prepare_wifi_recovery_runtime_destroy(void);
bool esp32_mquickjs_wifi_configuration_pending(void);
/* Internal whole-driver restart executor. Caller explicitly authorizes
 * retirement/recreation of Wi-Fi's own helpers and AP. Connected Station,
 * pending native operations, foreign owners and existing cleanup are rejected.
 * Failure after admission retains its exact token for central stop/runtime
 * cleanup; the public strict variant can explicitly retry a complete checkpoint,
 * while an incomplete source or unproven initialization remains cleanup-only.
 * Uses the caller's current native wait scope. The public Candidate binding
 * uses the strict variant below; unproven fault recovery remains restricted. */
esp_err_t esp32_mquickjs_wifi_restart_interfaces(wifi_mode_t mode,
    esp32_mquickjs_wifi_configuration_execution_t *execution);
/* Strict zero-owner variant: resolves a clean uninitialized or healthy stopped
 * source under its lifecycle reservation. Initialized off is restored to off;
 * allow_ap_restart permits temporary AP activation needed for saved AP policy.
 * No caller-selected final mode, forced release or disconnect. Same execution,
 * cleanup and wait-scope contract as the internal executor above. */
esp_err_t esp32_mquickjs_wifi_restart_stopped_interfaces(
    bool allow_ap_restart, esp32_mquickjs_wifi_configuration_execution_t *execution);
/* Runtime-task recovery state, owned by the caller and initially zeroed.
 * Begin authorizes Wi-Fi helper/AP retirement; allow_disconnect additionally
 * authorizes the established Station disconnect. Pending scan/connect Futures
 * are rejected before admission. The copied operation identity selects recovery
 * admission only; this coordinator never retires the original owner token.
 * Step returns ESP_OK with complete=false while original native ownership is
 * draining. The scheduler must continue polling that original Future/worker.
 * A failure/cancel retains central cleanup's exact reservation; dispose frees
 * only caller-owned validation storage and never releases a Radio owner.
 * These internal hooks have no JS/runtime pointer and are not public bindings. */
typedef struct {
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    wifi_mode_t mode;
    wifi_config_t *access_point;
    esp32_mquickjs_wifi_configuration_execution_t execution;
    unsigned phase;
} esp32_mquickjs_wifi_recovery_t;
esp_err_t esp32_mquickjs_wifi_recovery_begin(
    esp32_mquickjs_wifi_recovery_t *state,
    const esp32_mquickjs_wifi_recovery_request_t *operation, bool allow_disconnect);
esp_err_t esp32_mquickjs_wifi_recovery_step(
    esp32_mquickjs_wifi_recovery_t *state, bool *complete);
void esp32_mquickjs_wifi_recovery_dispose(esp32_mquickjs_wifi_recovery_t *state);
esp_err_t esp32_mquickjs_wifi_ap_begin_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_recovery_request_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode);
/* Runtime-task AP cleanup admission before atomic Radio claim; success binds
 * the AP retirement obligation to that same token without releasing a lease. */
esp_err_t esp32_mquickjs_wifi_ap_begin_stopped_restart(
    esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_radio_restart_selection_t *selection);
/* stopAP may retire only an AP-only pending transaction, not a mixed one. */
esp_err_t esp32_mquickjs_wifi_cleanup_ap_configuration(void);
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
struct esp32_mquickjs_wifi_eap_profile;
esp_err_t esp32_mquickjs_wifi_ap_begin_enterprise_restart(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    struct esp32_mquickjs_wifi_eap_profile *profile, bool allow_ap_restart,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode);
esp_err_t esp32_mquickjs_wifi_ap_release_enterprise_stop(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_ap_begin_enterprise_stop(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lifecycle_t *token);
#endif
esp_err_t esp32_mquickjs_wifi_ap_begin_configuration(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_ap_begin_selected_configuration(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    const esp32_mquickjs_wifi_radio_start_controls_t *start_controls,
    esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_ap_prepare_for_configuration(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
/* Recovery-only retirement preserves the outstanding native Action/ROC/FTM owner;
 * it does not authorize creating helpers or publishing new leases. */
esp_err_t esp32_mquickjs_wifi_ap_retire_for_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_retire_for_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_ap_retire_for_configuration(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_configuration_slot(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
bool esp32_mquickjs_init_wifi_ap_runtime(JSContext *ctx);
void esp32_mquickjs_deinit_wifi_ap_runtime(void);
JSValue js_wifi_start_ap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_stop_ap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_ap_clients(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_deauth_client(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_get_mac(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_set_mac(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_configure_tx_rate(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_tx_rate_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_bss_color_collision_reporting(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_dynamic_carrier_sense(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_configure_11b_rate(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_coexistence_power_management(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
esp_err_t esp32_mquickjs_wifi_apply_policy(esp32_mquickjs_wifi_policy_control_t control,
    wifi_interface_t interface, bool requested, bool *accepted,
    esp32_mquickjs_wifi_radio_config_result_t *result);
JSValue js_wifi_driver_get_event_mask(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue esp32_mquickjs_wifi_interval_to_js(JSContext *ctx);
JSValue esp32_mquickjs_wifi_policies_to_js(JSContext *ctx);
JSValue esp32_mquickjs_wifi_rssi_request_to_js(JSContext *ctx);
JSValue js_wifi_driver_set_connectionless_wake_interval(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
esp_err_t esp32_mquickjs_wifi_apply_interval(uint16_t milliseconds,
    esp32_mquickjs_wifi_radio_config_result_t *result);
JSValue js_wifi_driver_set_event_mask(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_mode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_country(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_channel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_home_channel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue esp32_mquickjs_wifi_country_to_js(JSContext *ctx, const wifi_country_t *country);
bool esp32_mquickjs_wifi_capture_country_details(JSContext *ctx, JSValue input, wifi_country_t *country);
JSValue js_wifi_driver_set_country_details(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_band(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_band_mode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_power_save(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_tx_power(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_rssi(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_aid(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_negotiated_phy(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_tsf_time(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_inactive_time(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_inactive_time(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_rssi_threshold(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_storage(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_mode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_disable_pmf(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_antenna(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_antenna_gpio(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_antenna(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_antenna_gpio(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_band(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_band_mode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
esp_err_t esp32_mquickjs_wifi_apply_band(bool band_mode, int32_t requested,
    int32_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result);
esp_err_t esp32_mquickjs_wifi_apply_scan_parameters(const wifi_scan_default_params_t *requested,
    wifi_scan_default_params_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result);
esp_err_t esp32_mquickjs_wifi_apply_he_statistics(bool rx, uint8_t selection, bool enabled,
    esp32_mquickjs_wifi_he_statistics_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result);
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
esp_err_t esp32_mquickjs_wifi_apply_twt_control(esp32_mquickjs_wifi_twt_control_kind_t kind,
    esp32_mquickjs_wifi_twt_control_t *value, uint32_t *generation,
    esp32_mquickjs_wifi_radio_config_result_t *result);
#endif
JSValue js_wifi_driver_get_statistics_config(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_configure_rx_statistics(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_tx_statistics(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_scan_parameters(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_scan_parameters(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
esp_err_t esp32_mquickjs_wifi_apply_connection_control(
    esp32_mquickjs_wifi_connection_control_t control, wifi_interface_t interface,
    int32_t requested, int32_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result);
JSValue js_wifi_driver_get_protocol(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_protocols(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_bandwidth(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_get_bandwidths(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_protocol(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_protocols(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_bandwidth(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_driver_set_bandwidths(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
/* Shared strict protocol array capture. Caller supplies the public error on false
 * only if no original getter/allocation exception is pending. */
bool esp32_mquickjs_wifi_capture_protocol(JSContext *ctx, JSValue input, uint16_t *output);
bool esp32_mquickjs_wifi_tx_rate_capture(JSContext *ctx, JSValue input, wifi_tx_rate_config_t *config);
JSValue js_wifi_set_country(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_set_channel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
/* Runtime-task lookup only. Exact identity is revalidated under the Radio mutex. */
const esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_control_lease(void);
bool esp32_mquickjs_init_wifi_wake_runtime(JSContext *ctx);
void esp32_mquickjs_deinit_wifi_wake_runtime(void);
JSValue js_wifi_acquire_wake_lock(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_wake_lock_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_wake_lock_acquired(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_wake_lock_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_wifi_wake_lock_finalizer(JSContext *ctx, void *opaque);
JSValue js_wifi_start(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_stop(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_capabilities(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue esp32_mquickjs_wifi_make_connect_result(JSContext *ctx,
    const esp32_mquickjs_wifi_link_snapshot_t *link, double elapsed_ms);
JSValue js_wifi_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

esp_err_t esp32_mquickjs_wifi_get_status(esp32_mquickjs_wifi_status_t *status);
esp_err_t esp32_mquickjs_wifi_ensure_started(void);
/* Runtime task only; no Wi-Fi status lock held across this callback barrier. */
esp_err_t esp32_mquickjs_wifi_prepare_connect_timer(void);
esp_err_t esp32_mquickjs_wifi_prepare_connect(wifi_config_t *config);
esp_err_t esp32_mquickjs_wifi_start_connect(const wifi_config_t *config,
                                            uint32_t timeout_ms);
esp_err_t esp32_mquickjs_wifi_start_disconnect(bool *out_pending);
esp_err_t esp32_mquickjs_wifi_cancel_connect(uint32_t generation);

/* Runtime-task operations. Native SCAN_DONE is the terminal barrier; cancelling
 * a public Future does not relinquish this boot-lived scan storage. */
esp_err_t esp32_mquickjs_wifi_start_scan(const wifi_scan_config_t *config,
                                       uint32_t generation);
esp_err_t esp32_mquickjs_wifi_cancel_scan(uint32_t generation);
esp_err_t esp32_mquickjs_wifi_drain_scan(void);
void esp32_mquickjs_wifi_scan_results_consumed(void);

esp32_mquickjs_wifi_state_t *esp32_mquickjs_wifi_state(void);
void esp32_mquickjs_wifi_lock(void);
void esp32_mquickjs_wifi_unlock(void);
/* Requires the Station helper lock. Includes an exact native provisioning owner
 * as well as a public Future; never use the native owner as a wake token. */
bool esp32_mquickjs_wifi_connection_reserved_locked(void);
void esp32_mquickjs_wifi_set_scanning_locked(bool scanning);
void esp32_mquickjs_wifi_clear_scan_future(void);
void esp32_mquickjs_wifi_clear_connect_future(void);
const char *esp32_mquickjs_wifi_reason_to_string(int32_t reason);
JSValue esp32_mquickjs_wifi_make_status_object(JSContext *ctx);
JSValue esp32_mquickjs_wifi_make_scan_results_array(JSContext *ctx, uint16_t max_records);
/* Converts a caller-owned immutable record; performs no SDK read or consume. */
JSValue esp32_mquickjs_wifi_scan_record_to_js(JSContext *ctx, const wifi_ap_record_t *record);
JSValue esp32_mquickjs_wifi_throw_operation_error(
    JSContext *ctx,
    const char *code,
    const char *operation,
    esp_err_t err,
    int32_t disconnect_reason,
    uint32_t scan_status);
JSValue esp32_mquickjs_wifi_throw_connect_error(JSContext *ctx, esp_err_t err);
JSValue esp32_mquickjs_wifi_throw_scan_error(JSContext *ctx, esp_err_t err);
int esp32_mquickjs_wifi_value_to_timeout_ms(JSContext *ctx,
                                            JSValue value,
                                            uint32_t default_timeout_ms,
                                            uint32_t *out_timeout_ms);

#endif
