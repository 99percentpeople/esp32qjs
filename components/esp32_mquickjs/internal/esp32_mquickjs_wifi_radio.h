#pragma once

#include "sdkconfig.h"

#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp32_mquickjs_wifi_promiscuous_broker.h"
#include "esp32_mquickjs_wifi_interval.h"
#include "esp32_mquickjs_wifi_mesh_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Recovery routing is internal; the identity always names the original native
 * operation, never a lease granted to the coordinator. TWT selects an entire
 * generation with identity=0 and explicit public close-all consent. */
typedef enum {
    ESP32_MQUICKJS_WIFI_RECOVERY_ACTION,
    ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX,
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    ESP32_MQUICKJS_WIFI_RECOVERY_TWT,
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    ESP32_MQUICKJS_WIFI_RECOVERY_FTM,
#endif
} esp32_mquickjs_wifi_recovery_kind_t;
typedef struct {
    uint32_t generation, identity;
    esp32_mquickjs_wifi_recovery_kind_t kind;
} esp32_mquickjs_wifi_recovery_request_t;

#include "esp_phy.h"
#include "esp32_mquickjs_wifi_raw_tx_broker.h"
#include "esp32_mquickjs_wifi_tx_rate.h"
#include "esp32_mquickjs_wifi_policy.h"
#include "esp32_mquickjs_wifi_he_statistics.h"
#include "esp32_mquickjs_wifi_twt_controls.h"
#include "esp32_mquickjs_wifi_ftm_offset.h"
#endif

#define ESP32_MQUICKJS_WIFI_MAX_WAKE_LOCKS 16U
#define ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS 4U
#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100U
#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000U
#define ESP32_MQUICKJS_WIFI_AP_DTIM_MAX 10U

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Read-only regulatory snapshot. Never initializes or starts the driver. */
esp_err_t esp32_mquickjs_wifi_radio_get_country(wifi_country_t *country);
#endif


typedef enum {
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA = 0,
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI,
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MONITOR,
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_NAN,
#endif
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX,
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE,
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION,
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT,
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_FTM,
#endif
#endif
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MESH,
#endif
    ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT,
} esp32_mquickjs_wifi_radio_client_t;

typedef struct {
    uint32_t generation;
    uint32_t identity;
    esp32_mquickjs_wifi_radio_client_t client;
    bool acquired;
} esp32_mquickjs_wifi_radio_lease_t;

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Native Raw TX admission. channel=0 follows the current channel while idle;
 * numeric channels hold the existing shared fixed-channel lease. Each submitted
 * operation pins its observed channel until retire, including after timeout.
 * AP requires an already running AP; no implicit AP configuration is created.
 * rate=NULL keeps shared behavior. A temporary rate requires a fully stopped,
 * initialized, owner-free Station with a known same-generation predecessor;
 * blocks new Radio/wake owners until release_and_stop_idle stops and restores.
 * An acquired output lease on error remains caller-owned for cleanup. */
esp_err_t esp32_mquickjs_wifi_radio_raw_tx_acquire(
    esp32_mquickjs_wifi_raw_tx_interface_t interface, uint8_t channel,
    const wifi_tx_rate_config_t *rate,
    esp32_mquickjs_wifi_radio_lease_t *lease, uint8_t *actual_channel);
/* bytes is a native, stable span for the whole call. Association policy comes
 * from SDK readback, never a JS assertion. token remains owned even on SDK error
 * if its identity is nonzero. The lease cannot be released before retirement. */
esp_err_t esp32_mquickjs_wifi_radio_raw_tx_submit(
    const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_raw_tx_interface_t interface, bool driver_sequence,
    const uint8_t *bytes, size_t length, esp32_mquickjs_wifi_raw_tx_token_t *token,
    esp32_mquickjs_wifi_raw_tx_validation_t *validation, uint8_t *actual_channel);
bool esp32_mquickjs_wifi_radio_raw_tx_retire(
    const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_raw_tx_token_t *token);
#endif

typedef struct {
    uint32_t generation;
    uint32_t radio_lease_identity;
    uint32_t identity;
    esp32_mquickjs_wifi_radio_client_t client;
    bool acquired;
    bool framework_enabled;
} esp32_mquickjs_wifi_radio_promiscuous_lease_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED = 0,
    ESP32_MQUICKJS_WIFI_RADIO_INITIALIZING,
    ESP32_MQUICKJS_WIFI_RADIO_STOPPED,
    ESP32_MQUICKJS_WIFI_RADIO_STARTING,
    ESP32_MQUICKJS_WIFI_RADIO_STARTED,
    ESP32_MQUICKJS_WIFI_RADIO_STOPPING,
    ESP32_MQUICKJS_WIFI_RADIO_FAULTED,
    ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING,
} esp32_mquickjs_wifi_radio_driver_state_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NONE = 0,
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN,
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_CONNECT,
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_ACTION,
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_ROC,
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_AP_DEAUTH,
#endif
#if CONFIG_ESP_WIFI_DPP_SUPPORT
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP,
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN,
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SMARTCONFIG,
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_WPS,
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_TWT_PROBE,
#endif
#if CONFIG_ESP_WIFI_RRM_SUPPORT
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_RRM,
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    ESP32_MQUICKJS_WIFI_RADIO_OPERATION_FTM,
#endif
#endif
} esp32_mquickjs_wifi_radio_operation_kind_t;

typedef struct {
    uint32_t generation;
    uint32_t lease_identity;
    uint32_t identity;
    esp32_mquickjs_wifi_radio_operation_kind_t kind;
} esp32_mquickjs_wifi_radio_operation_t;

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI || CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
/* Exact ESP-NOW Radio owner borrows the initialized framework interval baseline.
 * Failed configure returns/retains the interval token; release remains legal for
 * cleanup even when the interval value is uncertain. Other owners cannot enter
 * while borrowed, and generic Radio release cannot drop its restoration duty. */
esp_err_t esp32_mquickjs_wifi_radio_interval_configure(
    const esp32_mquickjs_wifi_radio_lease_t *lease, uint16_t milliseconds,
    esp32_mquickjs_wifi_interval_token_t *token);
esp_err_t esp32_mquickjs_wifi_radio_interval_release(
    const esp32_mquickjs_wifi_radio_lease_t *lease, esp32_mquickjs_wifi_interval_token_t *token);
void esp32_mquickjs_wifi_radio_interval_status(esp32_mquickjs_wifi_interval_state_t *status);
#endif

/* One bounded reservation for native STA scan/connect. Begin before any
 * disconnect/config/scan side effect. Retain through native completion and
 * cleanup; end only from the runtime task, outside the Wi-Fi state lock.
 * Caller supplies a zero token; stale copies cannot end another operation. */
esp_err_t esp32_mquickjs_wifi_radio_begin_operation(
    esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_radio_operation_kind_t kind,
    esp32_mquickjs_wifi_radio_operation_t *out_operation);
void esp32_mquickjs_wifi_radio_end_operation(
    esp32_mquickjs_wifi_radio_operation_t *operation);

/* No credentials: records only the latest native configuration transaction. */
typedef struct {
    const char *stage;
    esp_err_t error;
    bool mutation_attempted;
    bool rollback_attempted;
    bool rollback_complete;
    const char *rollback_stage;
    esp_err_t rollback_error;
    bool persistent_mutation_possible;
} esp32_mquickjs_wifi_radio_config_result_t;

/* Applied after native START completion, before any owner is published.
 * This is not a promise to limit RF power before esp_wifi_start returns. */
typedef struct {
    bool tx_power_set;
    int8_t tx_power_quarter_dbm;
} esp32_mquickjs_wifi_radio_start_controls_t;
esp_err_t esp32_mquickjs_wifi_radio_validate_start_controls(bool start,
    const esp32_mquickjs_wifi_radio_start_controls_t *controls);

typedef struct {
    uint32_t generation;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    wifi_mode_t requested_mode;
    uint32_t event_identity;
    uint8_t event_phase, event_expected, event_seen, event_stopped, event_live;
    bool event_fence_pending;
    esp32_mquickjs_wifi_radio_config_result_t configuration;
    esp32_mquickjs_wifi_radio_config_result_t activation;
    uint32_t active_operations;
    uint32_t wake_locks;
    esp_err_t wake_lock_error;
    bool lifecycle_active;
    uint32_t fixed_channel_owners;
    uint32_t conflicted_channel_owners;
    const char *cleanup_stage;
    esp_err_t cleanup_error;
    bool driver_owned;
    bool restart_required;
    const char *fault_stage;
    esp_err_t fault_error;
    bool initialized;
    bool starting;
    bool started;
    wifi_mode_t mode;
    wifi_storage_t storage;
    uint8_t primary_channel;
    wifi_second_chan_t secondary_channel;
    uint32_t channel_generation;
    esp_err_t channel_observation_error;
    bool max_tx_power_available;
    int8_t max_tx_power_quarter_dbm;
    bool power_save_available;
    wifi_ps_type_t power_save;
    bool fixed_channel_claimed;
    bool promiscuous_claimed;
    uint32_t promiscuous_owners;
    bool promiscuous_identity_exhausted;
    esp32_mquickjs_wifi_radio_client_t promiscuous_client;
    uint32_t clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
} esp32_mquickjs_wifi_radio_status_t;

esp_err_t esp32_mquickjs_wifi_radio_acquire(
    esp32_mquickjs_wifi_radio_client_t client,
    wifi_mode_t required_mode,
    esp32_mquickjs_wifi_radio_lease_t *out_lease);

typedef struct { uint32_t generation, identity; } esp32_mquickjs_wifi_radio_lifecycle_t;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Common runtime/helper route. Each dispatch revalidates the exact lifecycle
 * in its native operation implementation; a stale token cannot change kind. */
esp_err_t esp32_mquickjs_wifi_radio_begin_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_recovery_request_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode);
bool esp32_mquickjs_wifi_radio_recovery_active(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_checkpoint_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
esp_err_t esp32_mquickjs_wifi_radio_stop_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_check_stopped_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_shutdown_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_finish_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
/* Before disconnect/STOP, revoke native operation authority that depends on
 * the live association. Idempotent; failure keeps driver and owners intact. */
esp_err_t esp32_mquickjs_wifi_radio_prepare_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token);

#endif

/* Reserve the cold AP helper before the shared configuration coordinator
 * validates, configures and starts the interface. */
esp_err_t esp32_mquickjs_wifi_radio_reserve_ap(esp32_mquickjs_wifi_radio_lease_t *lease);
/* Pure validation, also used before JS construction acquires any Radio owner. */
esp_err_t esp32_mquickjs_wifi_radio_validate_ap_config(const wifi_config_t *config);

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
typedef struct { uint32_t generation, identity; } esp32_mquickjs_wifi_wake_token_t;
esp_err_t esp32_mquickjs_wifi_radio_wake_acquire(esp32_mquickjs_wifi_wake_token_t *token);
esp_err_t esp32_mquickjs_wifi_radio_wake_release(esp32_mquickjs_wifi_wake_token_t *token);
bool esp32_mquickjs_wifi_radio_wake_active(const esp32_mquickjs_wifi_wake_token_t *token);
esp_err_t esp32_mquickjs_wifi_radio_wake_release_all(void);
typedef struct {
    const char *stage;
    bool driver_accepted;
} esp32_mquickjs_wifi_radio_mutation_t;
esp_err_t esp32_mquickjs_wifi_radio_set_country_code(
    const esp32_mquickjs_wifi_radio_lease_t *lease, const char code[3], bool ieee80211d,
    wifi_country_t *actual, esp32_mquickjs_wifi_radio_mutation_t *mutation);
/* Complete explicit regulatory details on an initialized/stopped/zero-owner
 * driver, including AP/APSTA/off. Country persistence is independent of storage.
 * Readback/rollback describe running configuration, not a durable NVS commit. */
esp_err_t esp32_mquickjs_wifi_radio_set_country_details(const wifi_country_t *country,
    wifi_country_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result);
esp_err_t esp32_mquickjs_wifi_radio_change_channel(
    const esp32_mquickjs_wifi_radio_lease_t *lease, uint8_t primary, wifi_second_chan_t secondary,
    uint8_t *actual_primary, wifi_second_chan_t *actual_secondary, uint32_t *generation,
    esp32_mquickjs_wifi_radio_mutation_t *mutation);
/* Pure mapping of public 5 GHz channel numbers to SDK bitmap positions. */
uint32_t esp32_mquickjs_wifi_radio_5ghz_channel_bit(uint8_t channel);
/* Validate explicit scan channels against the current band/country snapshot,
 * while holding an exact native SCAN operation reservation. */
esp_err_t esp32_mquickjs_wifi_radio_validate_scan_channels(
    const esp32_mquickjs_wifi_radio_operation_t *operation,
    const wifi_scan_config_t *config);
/* Reads never initialize/start Radio. Mutex excludes driver teardown. */
typedef struct {
    bool rssi_valid, phy_valid;
    int rssi;
    wifi_phy_mode_t phy;
} esp32_mquickjs_wifi_link_sample_t;
/* Advisory current readings; AP identity checked around the SDK queries. */
esp_err_t esp32_mquickjs_wifi_radio_sample_station_link(
    const esp32_mquickjs_wifi_radio_lease_t *lease, const uint8_t bssid[6], uint8_t channel,
    esp32_mquickjs_wifi_link_sample_t *sample);
esp_err_t esp32_mquickjs_wifi_radio_get_mac(wifi_interface_t interface, uint8_t mac[6]);
typedef enum {
    ESP32_MQUICKJS_WIFI_PHY_PROTOCOL,
    ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS,
    ESP32_MQUICKJS_WIFI_PHY_BANDWIDTH,
    ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS,
} esp32_mquickjs_wifi_phy_query_t;
typedef struct {
    wifi_protocols_t protocols;
    wifi_bandwidths_t bandwidths;
    uint8_t bands; /* plural: bit 0 = 2.4 GHz, bit 1 = 5 GHz; singular uses bit 0 */
} esp32_mquickjs_wifi_phy_readback_t;
/* Live SDK reads under the mutation mutex, no implicit init or owner acquisition.
 * Stable initialized driver only; rejects lifecycle/operation/fault transitions.
 * Singular preserves SDK AUTO-band rejection. Plural omits inactive bands.
 * Output is cleared on every error, including partial SDK writes to output. */
esp_err_t esp32_mquickjs_wifi_radio_read_phy(wifi_interface_t interface,
    esp32_mquickjs_wifi_phy_query_t query, esp32_mquickjs_wifi_phy_readback_t *output,
    const char **stage);
/* Initialized/stopped/zero-owner transaction. Same query selects field/shape.
 * Singular input uses the first slot and resolves its actual band under lock.
 * Snapshot both protocol/width, validate, write/read back, then rollback on error.
 * Failure publishes no output; result and status.radio.configuration retain diagnostics. */
esp_err_t esp32_mquickjs_wifi_radio_write_phy(wifi_interface_t interface,
    esp32_mquickjs_wifi_phy_query_t query, const esp32_mquickjs_wifi_phy_readback_t *requested,
    esp32_mquickjs_wifi_phy_readback_t *output, esp32_mquickjs_wifi_radio_config_result_t *result);
typedef enum {
    ESP32_MQUICKJS_WIFI_DRIVER_BAND,
    ESP32_MQUICKJS_WIFI_DRIVER_BAND_MODE,
    ESP32_MQUICKJS_WIFI_DRIVER_POWER_SAVE,
    ESP32_MQUICKJS_WIFI_DRIVER_TX_POWER,
    ESP32_MQUICKJS_WIFI_DRIVER_RSSI,
    ESP32_MQUICKJS_WIFI_DRIVER_AID,
    ESP32_MQUICKJS_WIFI_DRIVER_NEGOTIATED_PHY,
    ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME,
    ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME,
} esp32_mquickjs_wifi_driver_query_t;
/* One live SDK observation, no association identity or cross-call atomicity.
 * Caller supplies STA for global queries. Native TX power remains quarter-dBm.
 * TSF zero preserves SDK unavailable semantics; reject non-exact JS integers.
 * Mutation mutex excludes lifecycle/driver writes; no initialization/owner. */
esp_err_t esp32_mquickjs_wifi_radio_read_driver(esp32_mquickjs_wifi_driver_query_t query,
    wifi_interface_t interface, int64_t *output, const char **stage);
typedef enum {
    ESP32_MQUICKJS_WIFI_DRIVER_STATE_MODE,
    ESP32_MQUICKJS_WIFI_DRIVER_STATE_COUNTRY,
    ESP32_MQUICKJS_WIFI_DRIVER_STATE_CHANNEL,
    ESP32_MQUICKJS_WIFI_DRIVER_STATE_HOME_CHANNEL,
} esp32_mquickjs_wifi_driver_state_query_t;
typedef struct {
    wifi_mode_t mode;
    wifi_country_t country;
    uint8_t channel;
    wifi_second_chan_t secondary;
    /* Only current-channel observations have a framework revision. Home is zero. */
    uint32_t channel_generation;
} esp32_mquickjs_wifi_driver_state_readback_t;
/* Stable initialized driver, no implicit owner/init. Home requires started.
 * Reject unknown SDK encodings and clear all partial output on error. */
esp_err_t esp32_mquickjs_wifi_radio_read_state(esp32_mquickjs_wifi_driver_state_query_t query,
    esp32_mquickjs_wifi_driver_state_readback_t *output, const char **stage);
/* SDK log dump under the same lifecycle/mutation exclusion as driver reads.
 * No init, Radio lease, counter reset, or interpretation of console output. */
esp_err_t esp32_mquickjs_wifi_radio_dump_stats(uint32_t mask, const char **stage);
/* Driver-wide event broker control. Only the SDK probe-request bit is writable;
 * never suppress lifecycle/Future completion events. Read exposes the raw mask.
 * Stable initialized driver; no owner/init. Failed writes restore only a known
 * safe previous mask, with readback. No replay of ALL/unknown previous bits. */
esp_err_t esp32_mquickjs_wifi_radio_event_mask(bool write, uint32_t requested,
    uint32_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result);
/* Explicit future-write storage policy, initialized/stopped/zero owners.
 * No SDK getter: return acceptance. A failed write invalidates knowledge and
 * only a subsequent explicit accepted replacement repairs this specific fault. */
esp_err_t esp32_mquickjs_wifi_radio_set_storage(wifi_storage_t storage,
    esp32_mquickjs_wifi_radio_config_result_t *result);
/* Initialized, stopped, zero owners. Verified mode write with known-mode
 * rollback; neither enables RF nor creates interface helpers. */
/* Explicit default reset: stopped/zero owners, no rollback or implicit recovery. */
esp_err_t esp32_mquickjs_wifi_radio_restore(esp32_mquickjs_wifi_radio_config_result_t *result);
esp_err_t esp32_mquickjs_wifi_radio_set_mode(wifi_mode_t mode,
    esp32_mquickjs_wifi_radio_config_result_t *result);
/* Explicit security mutation after configuration, fully stopped/zero owners.
 * Reject mandatory-PMF security; verify both cleared flags and unchanged other
 * fields. Failure after mutation faults; no guessed runtime/NVS rollback. */
esp_err_t esp32_mquickjs_wifi_radio_disable_pmf(wifi_interface_t interface,
    esp32_mquickjs_wifi_radio_config_result_t *result);
/* Pure config validation shared by capture and stopped mutations; no SDK calls. */
bool esp32_mquickjs_wifi_radio_pmf_disable_allowed(wifi_interface_t interface, const wifi_config_t *config);
typedef union {
    esp_phy_ant_config_t config;
    esp_phy_ant_gpio_config_t gpio;
} esp32_mquickjs_wifi_antenna_snapshot_t;
/* Snapshot of SDK-stored shared PHY configuration, not RF measurements.
 * Initialized/stable Radio; no mutation or owner acquisition. Clears output
 * on any error. The two observations are not an atomic combined snapshot. */
esp_err_t esp32_mquickjs_wifi_radio_read_antenna(bool gpio,
    esp32_mquickjs_wifi_antenna_snapshot_t *output, const char **stage);
/* Runtime task; initialized, fully stopped, zero owners; shared PHY guard. */
esp_err_t esp32_mquickjs_wifi_radio_write_antenna(bool gpio,
    const esp32_mquickjs_wifi_antenna_snapshot_t *requested,
    esp32_mquickjs_wifi_radio_config_result_t *result);
typedef enum {
    ESP32_MQUICKJS_WIFI_POLICY_DYNAMIC_CS,
    ESP32_MQUICKJS_WIFI_POLICY_11B_RATE,
    ESP32_MQUICKJS_WIFI_POLICY_COEX_POWER,
    ESP32_MQUICKJS_WIFI_POLICY_BSS_COLOR,
} esp32_mquickjs_wifi_policy_control_t;
/* No public SDK getters: return acceptance, never invented readback/defaults.
 * Dynamic CS requires started exact framework owners. 11b requires enabled
 * interface, fully stopped zero-owner driver. Coex permits either state, with
 * the same started-owner or stopped-exclusive admission. Global iface is MAX.
 * SDK failure faults Radio without guessed rollback or automatic replay. */
esp_err_t esp32_mquickjs_wifi_radio_write_interval(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point, uint16_t milliseconds,
    esp32_mquickjs_wifi_radio_config_result_t *result);
/* Native retained credential snapshot bytes, excluding fixed control state. */
uint32_t esp32_mquickjs_wifi_radio_restart_snapshot_bytes(void);
void esp32_mquickjs_wifi_radio_policy_status(esp32_mquickjs_wifi_policy_state_t *status);
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
/* Stopped AP/APSTA, zero owner, no lifecycle/operation. A failed write leaves
 * the offset uncertain; explicit replacement is permitted without guessing a
 * previous value. Output record and result describe this exact attempt. */
esp_err_t esp32_mquickjs_wifi_radio_write_ftm_offset(int32_t centimeters,
    esp32_mquickjs_wifi_ftm_offset_state_t *record, esp32_mquickjs_wifi_radio_config_result_t *result);
void esp32_mquickjs_wifi_radio_ftm_offset_status(esp32_mquickjs_wifi_ftm_offset_state_t *record);
#endif
esp_err_t esp32_mquickjs_wifi_radio_write_policy(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_policy_control_t control, wifi_interface_t interface,
    bool requested, bool *accepted, esp32_mquickjs_wifi_radio_config_result_t *result);
typedef enum {
    ESP32_MQUICKJS_WIFI_CONNECTION_INACTIVE_TIME,
    ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,
} esp32_mquickjs_wifi_connection_control_t;
/* Historical explicit request, never SDK threshold/armed-state readback.
 * Revision is boot-scoped and never reused across physical driver generations.
 * A low-RSSI event has no cookie and must not consume or rearm this record. */
typedef struct {
    uint32_t generation, revision;
    int32_t requested_dbm;
    esp_err_t error;
} esp32_mquickjs_wifi_rssi_request_t;
/* Returns whether this request belongs to the currently owned physical driver
 * generation, not whether the notification is armed. No SDK call or mutation. */
bool esp32_mquickjs_wifi_radio_rssi_request_status(esp32_mquickjs_wifi_rssi_request_t *status);
/* Runtime coordinator supplies exact framework owners. Inactive time excludes
 * other feature owners and may disconnect clients even if value rollback works.
 * RSSI is observer-only, permits unrelated RF owners, and is never auto-rearmed.
 * Its successful result is SDK acceptance, not threshold readback/armed state. */
esp_err_t esp32_mquickjs_wifi_radio_connection_control(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_connection_control_t control, wifi_interface_t interface,
    int32_t requested, int32_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result);
/* Started Station-only, exact framework owners, no unrelated/fixed-channel use.
 * Actual change requires unassociated STA. setBand requires AUTO band mode.
 * SDK chooses home channel; verify mode/band/channel. Failed mutation faults
 * Radio, without an unproved rollback of hidden inactive-band configuration. */
esp_err_t esp32_mquickjs_wifi_radio_change_band(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    bool band_mode, int32_t requested, int32_t *actual,
    esp32_mquickjs_wifi_radio_config_result_t *result);
/* Records are framework writes, never driver readback. Snapshot remains available
 * during faults. Setter requires initialized/stopped/owner-free admission and may
 * repair only a fault created by this same rate boundary. */
esp_err_t esp32_mquickjs_wifi_radio_tx_rate_status(wifi_interface_t interface,
    esp32_mquickjs_wifi_tx_rate_record_t *record, uint32_t *generation,
    esp32_mquickjs_wifi_tx_rate_lease_t *temporary);
esp_err_t esp32_mquickjs_wifi_radio_configure_tx_rate(wifi_interface_t interface,
    const wifi_tx_rate_config_t *config, esp32_mquickjs_wifi_tx_rate_write_t *write,
    esp32_mquickjs_wifi_tx_rate_record_t *record, uint32_t *generation,
    esp32_mquickjs_wifi_tx_rate_lease_t *temporary);
esp_err_t esp32_mquickjs_wifi_radio_set_mac(wifi_interface_t interface, const uint8_t requested[6],
    uint8_t actual[6], esp32_mquickjs_wifi_radio_mutation_t *mutation);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
/* Public, non-secret observations only. On error only started is meaningful;
 * it reflects received AP_START/AP_STOP events, not association/IP readiness. */
typedef struct {
    bool started, hidden;
    uint8_t ssid[32], ssid_len, channel, max_connections, client_count, mac[6];
    wifi_auth_mode_t authmode;
} esp32_mquickjs_wifi_ap_snapshot_t;
esp_err_t esp32_mquickjs_wifi_radio_sample_ap(
    const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_ap_snapshot_t *snapshot, const char **stage);

typedef struct {
    wifi_sta_list_t stations;
    uint16_t aid[ESP_WIFI_MAX_CONN_NUM]; /* zero means no longer associated */
} esp32_mquickjs_wifi_ap_clients_t;
/* AIDs are later observations, not identities or an atomic association set. */
esp_err_t esp32_mquickjs_wifi_radio_ap_clients(
    const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_ap_clients_t *snapshot);
/* Current MAC lookup and targeted deauth in one Wi-Fi-task command. A true
 * result is SDK request acceptance, not confirmation of peer departure. */
esp_err_t esp32_mquickjs_wifi_radio_ap_deauth(const esp32_mquickjs_wifi_radio_lease_t *lease,
    const uint8_t address[6], bool *requested, bool *handoff_unknown, const char **stage);
/* Receipts only: never retries a dispatched operation. */
bool esp32_mquickjs_wifi_radio_ap_deauth_poll(bool *pending);
#endif
#endif

esp_err_t esp32_mquickjs_wifi_radio_ensure_started(
    esp32_mquickjs_wifi_radio_lease_t *lease);

esp_err_t esp32_mquickjs_wifi_radio_get_status(
    esp32_mquickjs_wifi_radio_status_t *out_status);

typedef struct {
    uint8_t primary;
    wifi_second_chan_t secondary;
    uint32_t generation;
    bool fixed;
    bool conflicted;
} esp32_mquickjs_wifi_radio_channel_status_t;

/* Callback-safe snapshot: exact lease validation, no SDK/Radio mutation mutex.
 * A fixed conflict remains latched until that owner releases its constraint. */
esp_err_t esp32_mquickjs_wifi_radio_lease_channel_status(
    const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_radio_channel_status_t *status);

esp_err_t esp32_mquickjs_wifi_radio_get_channel(
    uint8_t *primary,
    wifi_second_chan_t *secondary,
    uint32_t *channel_generation);

esp_err_t esp32_mquickjs_wifi_radio_set_channel(
    esp32_mquickjs_wifi_radio_lease_t *lease,
    uint8_t primary,
    wifi_second_chan_t secondary);

void esp32_mquickjs_wifi_radio_release_channel(
    esp32_mquickjs_wifi_radio_lease_t *lease);

/* An error can still return acquired=true when driver rollback is pending.
 * Preserve this token, channel and Radio lease until release succeeds. Each
 * acquisition has its own non-reused identity, independent of the Radio lease. */
esp_err_t esp32_mquickjs_wifi_radio_acquire_promiscuous(
    esp32_mquickjs_wifi_radio_lease_t *radio_lease,
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *out_lease);

/* Atomically reserve an RX subscriber, submit the shared SDK demand and then
 * activate it under the Radio mutation mutex. A failure with acquired=true
 * retains subscriber/context storage for release/retry. Release first stops
 * admission and returns with acquired=true while entered dispatches drain;
 * it never waits while holding the Radio mutex. Only free control storage
 * after acquired becomes false. Sink must obey the broker callback contract. */
esp_err_t esp32_mquickjs_wifi_radio_subscribe_promiscuous(
    esp32_mquickjs_wifi_radio_lease_t *radio_lease,
    esp32_mquickjs_wifi_promiscuous_subscriber_t *subscriber,
    const esp32_mquickjs_wifi_rx_filter_t *filter,
    esp32_mquickjs_wifi_promiscuous_sink_t sink, void *context,
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *out_lease);

void esp32_mquickjs_wifi_radio_release_promiscuous(
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *lease);

void esp32_mquickjs_wifi_radio_release(
    esp32_mquickjs_wifi_radio_lease_t *lease);

const char *esp32_mquickjs_wifi_radio_driver_state_name(
    esp32_mquickjs_wifi_radio_driver_state_t state);

/* Internal lifecycle boundary. Call only after all feature/STA leases have
 * exited. Stop retains initialized driver storage; shutdown also deinitializes.
 * Neither operation implicitly closes a child or resets NVS. */
esp_err_t esp32_mquickjs_wifi_radio_stop(void);
/* Runtime owner retirement: release only this exact lease, then stop if the
 * registry is empty. Shared owners keep the driver running. A failed final
 * stop is retryable with the now-empty token; active operations retain it. */
esp_err_t esp32_mquickjs_wifi_radio_release_and_stop_idle(
    esp32_mquickjs_wifi_radio_lease_t *lease);
esp_err_t esp32_mquickjs_wifi_radio_shutdown(void);

/* Cross-step lifecycle reservation. Only the exact application, idle STA and
 * AP helper leases supplied by the runtime may exist. Begin has no driver effects.
 * After begin, release these leases and clean helper callbacks outside the Radio
 * mutex, then finish. Failed finish retains admission exclusion for explicit
 * retry. Tokens are boot-scoped and never recycled. */
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Pure capture validates all requested fields before this native transaction.
 * A validator checks accepted SDK config without JS, allocation or driver calls.
 * On success, mutable station/AP inputs receive the accepted SDK readback.
 * Caller owns stop/start and an exact lifecycle token; this commits only while
 * stopped with no owners, and never releases that token (including rollback). */
typedef bool (*esp32_mquickjs_wifi_config_accept_fn)(
    const wifi_config_t *requested, const wifi_config_t *actual);
/* Internal stopped-driver transaction input, not a public JS contract. Band
 * masks use bit 0 for 2.4 GHz and bit 1 for 5 GHz; index 0/1 is STA/AP.
 * Protocols are complete bitmaps, not the SDK's maximum-protocol shorthand.
 * Country max_tx_power is read-only and must be zero in a details request.
 * No band-mode/channel/TX-power mutation here: those require a started driver. */
typedef struct {
    bool country_set;
    bool country_by_code;
    wifi_country_t country;
    uint8_t protocol_bands[2];
    uint8_t bandwidth_bands[2];
    wifi_protocols_t protocols[2];
    wifi_bandwidths_t bandwidths[2];
    bool power_save_set;
    wifi_ps_type_t power_save;
} esp32_mquickjs_wifi_radio_config_controls_t;
/* Selection is resolved atomically with admission. Omitted values preserve a
 * healthy configured driver; cold mode follows provided interfaces (else STA),
 * storage defaults to RAM and start to true. No credentials are retained here. */
typedef struct {
    bool mode_set, storage_set, start_set;
    wifi_mode_t mode;
    wifi_storage_t storage;
    bool start, allow_disconnect;
    bool station_set, access_point_set;
    bool start_only; /* Internal start adapter, never accepted by configure capture. */
    /* Explicit startAP reconfiguration: preserve the existing Station mode
     * and storage, add AP, and authorize the common stopped transaction. */
    bool activate_ap;
} esp32_mquickjs_wifi_radio_configuration_selection_t;
/* Finish only the matching pre-start Vendor IE handoff before physical cleanup. */
esp_err_t esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_begin_start_lifecycle(
    esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, bool *already_started);
esp_err_t esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_config_t *config);
esp_err_t esp32_mquickjs_wifi_radio_begin_configuration_lifecycle(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    const esp32_mquickjs_wifi_radio_start_controls_t *start_controls,
    esp32_mquickjs_wifi_radio_lifecycle_t *token);
bool esp32_mquickjs_wifi_radio_accept_station_config(
    const wifi_config_t *requested, const wifi_config_t *actual);
/* Pure validation, also called before the helper coordinator stops anything. */
esp_err_t esp32_mquickjs_wifi_radio_validate_config_controls(wifi_mode_t mode,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls);
bool esp32_mquickjs_wifi_radio_accept_ap_config(
    const wifi_config_t *requested, const wifi_config_t *actual);
/* Stable initialized driver; no implicit lifecycle or owner acquisition.
 * Default reads erase credentials before returning. Explicit secret reads are
 * build-gated. A failed read erases the entire caller-provided output. */
esp_err_t esp32_mquickjs_wifi_radio_read_interface_config(wifi_interface_t interface,
    bool include_secrets, wifi_config_t *config, const char **stage);
/* Full replacement on a stopped, zero-owner, enabled interface. Keeps mode and
 * storage selection, verifies security/readback and uses the shared rollback.
 * Success replaces config with actual readback; caller must erase on every exit. */
esp_err_t esp32_mquickjs_wifi_radio_write_interface_config(wifi_interface_t interface,
    wifi_config_t *config, esp32_mquickjs_wifi_radio_config_result_t *result);
/* Exact stopped token, zero owners. Initialize only if necessary, retaining
 * the token and original native fault on failure. */
esp_err_t esp32_mquickjs_wifi_radio_initialize_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_configure_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token,
    wifi_mode_t mode, wifi_storage_t storage,
    wifi_config_t *station, esp32_mquickjs_wifi_config_accept_fn accept_station,
    wifi_config_t *access_point, esp32_mquickjs_wifi_config_accept_fn accept_access_point,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    esp32_mquickjs_wifi_radio_config_result_t *result);
#endif

esp_err_t esp32_mquickjs_wifi_radio_begin_lifecycle(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_lifecycle_t *token);
/* Internal APSTA removal: retain Station/Application/AP leases until AP_STOP
 * and mode readback, retire AP netif, then finish releases only AP. An uncertain
 * failed setter is observed, never replayed. Whole cleanup is still possible
 * through quiesce/finish_lifecycle after every owner explicitly exits. */
/* Reopen only an already matching stored AP config, retaining live Station.
 * Prepare AP netif after begin, then finish exactly once; abort transfers the
 * same reservation to AP removal after any setup/activation failure. */
esp_err_t esp32_mquickjs_wifi_radio_begin_ap_reopen(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station, const wifi_config_t *requested,
    esp32_mquickjs_wifi_radio_lease_t *access_point, esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_check_ap_reopen(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_finish_ap_reopen(esp32_mquickjs_wifi_radio_lifecycle_t *token, uint8_t *primary);
esp_err_t esp32_mquickjs_wifi_radio_abort_ap_reopen(const esp32_mquickjs_wifi_radio_lifecycle_t *token);

esp_err_t esp32_mquickjs_wifi_radio_begin_ap_stop(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_check_ap_stopped_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
/* Abandon only the partial intent, retaining the exact lifecycle reservation.
 * The caller has explicitly authorized whole cleanup and drained Station. */
esp_err_t esp32_mquickjs_wifi_radio_adopt_ap_stop(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_finish_ap_stop(
    esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_radio_lease_t *access_point);
/* Stop after lease release, retaining exclusion until helper netif/storage exit. */
esp_err_t esp32_mquickjs_wifi_radio_quiesce_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_finish_lifecycle(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, bool shutdown);

/* Read-only exact-token admission for runtime helper/netif work performed
 * outside the Radio mutex. Requires no owners, operations or started driver.
 * cleanup also permits a stopped diagnostic fault, but never uncertain init. */
esp_err_t esp32_mquickjs_wifi_radio_check_stopped_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, bool cleanup);

/* Handoff after stopped configuration: callbacks/netifs must already be ready.
 * All output slots must be empty and distinct. Start publishes their exact
 * leases only after start/mode and optional TX-power readback; failure keeps the token and publishes
 * no leases. For start=false supply no outputs: retain config, end exclusion.
 * This does not initialize/deinitialize or change the configured mode/storage. */
esp_err_t esp32_mquickjs_wifi_radio_resume_lifecycle(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode, bool start,
    esp32_mquickjs_wifi_radio_lease_t *application,
    esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_radio_start_controls_t *controls);

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Historical observations from immediately before a submitted whole STOP.
 * Not a stopped-state getter or proof that later configuration is unchanged.
 * stop_identity==0 means no observation attempt. SDK enum values fit uint8_t
 * on the supported targets; error==ESP_OK and step==COMPLETE validate payload. */
typedef enum {
    ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_NONE,
    ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_ADMISSION,
    ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_POWER,
    ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_BAND,
    ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_CHANNEL,
    ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_HOME,
    ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_STA_INACTIVE,
    ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_AP_INACTIVE,
    ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_VERIFY,
    ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_COMPLETE,
} esp32_mquickjs_wifi_stop_snapshot_step_t;
typedef struct {
    uint32_t generation, stop_identity;
    esp_err_t error;
    /* Seconds for interfaces enabled in mode; zero for an unobserved interface. */
    uint16_t inactive_time[2];
    uint8_t mode, band_mode, band, primary, secondary;
    int8_t tx_power;
    uint8_t step;
    /* Full STOP completed and no later snapshot-invalidating Radio SDK mutation was attempted.
     * This permits capture without a source START, not by itself restart admission. */
    bool unchanged;
} esp32_mquickjs_wifi_radio_stop_snapshot_t;
void esp32_mquickjs_wifi_radio_stop_snapshot(esp32_mquickjs_wifi_radio_stop_snapshot_t *snapshot);

typedef struct {
    wifi_mode_t mode;
    bool cold, restore_off;
    /* Input: permit temporary AP activation if an off source has AP policy. */
    bool allow_ap_restart;
} esp32_mquickjs_wifi_radio_restart_selection_t;

/* Internal restart admission. Requires a fresh zero token, either a clean
 * uninitialized driver or healthy stopped driver, no lease/operation/wake owner or borrowed-setting
 * restore obligation. Resolves work mode and claims lifecycle atomically, without SDK calls.
 * Missing or invalidated STOP history requires source START during checkpoint;
 * the caller must prepare helpers for that source mode before checkpoint.
 * Cold admission selects Station/RAM and creates helpers after driver init.
 * Initialized off selects temporary Station (APSTA if AP policy requires it
 * and allow_ap_restart is true), and must finish stopped/off. It has real
 * predecessor configuration and must never use cold defaults.
 * On state/owner failure output fields are zero and token remains zero; the
 * allow_ap_restart input is preserved. Invalid argument
 * rejection leaves outputs untouched. Caller owns cleanup after admission. */
esp_err_t esp32_mquickjs_wifi_radio_begin_stopped_restart(
    esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_radio_restart_selection_t *selection);
/* Explicit retry of the same retained complete restart checkpoint. Admission
 * under the Radio mutex: no new token, recapture, fault clearing or driver
 * mutation. Success resets only the per-call configuration diagnostic.
 * Rejects incomplete sources, reboot-required/unproven init,
 * foreign owners and another feature's recovery. Caller must quiesce and retire
 * helpers before rebuild; the original checkpoint remains the sole source. */
esp_err_t esp32_mquickjs_wifi_radio_admit_restart_retry(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_radio_restart_selection_t *selection);
/* Verify the rebuilt off checkpoint through a temporary START, then STOP,
 * fence, set/read back off and restore storage. Retains the exact lifecycle
 * and checkpoint until the caller retires helpers and finish_lifecycle(false).
 * Never publishes leases or retries a failed physical reconstruction. */
esp_err_t esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);

/* Runtime restart phases, all retaining the same lifecycle identity. No phase
 * publishes leases. Driver calls hold the mutation mutex, not a critical section.
 *
 * 1. Exit all native operations/leases before checkpoint. Capture can use
 *    temporary Station cycles; old helpers remain attached until this returns
 *    success with the driver stopped and its event boundary drained.
 * 2. Retire old Station/AP helpers outside the Radio mutex, then rebuild.
 *    Rebuild requires proven STOP, performs shutdown/init, and returns stopped.
 * 3. Attach new Station/AP helpers before replay. Station is needed even for
 *    AP-only restoration because dual-band preparation can temporarily start it.
 * 4. Replay returns stopped, retaining the reservation and frozen secrets.
 *    Retire any temporary helper, then resume_lifecycle with all real outputs.
 *
 * The runtime owns step ordering and helper admission. A helper failure must
 * not advance to the next phase. Errors retain token/checkpoint; before another
 * physical rebuild, quiesce and retire any newly attached helpers. This is not
 * a public restart API or an automatic retry/recovery policy. */
esp_err_t esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
esp_err_t esp32_mquickjs_wifi_radio_rebuild_restart_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
esp_err_t esp32_mquickjs_wifi_radio_replay_restart_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
#endif

/* Radio-only internal restart, for callers with no runtime helpers/netifs.
 * Runtime integration must use the phases above to retire/attach helpers.
 * Keeps admission closed through shutdown, registration,
 * init/start and replay of known boolean policy records. Snapshot precedes
 * physical deinit; post-start policy acceptance precedes owner publication.
 * Failure retains token/frozen intent/native cleanup, without a published owner.
 * Station/AP config is captured before deinit and read back before handoff;
 * RAM policy covers replay, START and final acceptance, then the saved storage
 * policy is restored before publishing owners. A failed commit stays unknown;
 * this is not the public restart API or complete driver configuration restoration. */
esp_err_t esp32_mquickjs_wifi_radio_restart_lifecycle(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode,
    esp32_mquickjs_wifi_radio_lease_t *application);

/* Persistent global settings: exact STA helper plus its one APPLICATION lease,
 * no other live feature lease.
 * Validation, driver write/readback and release use the same mutation mutex.
 * A readback error does not undo an already accepted driver write. */
esp_err_t esp32_mquickjs_wifi_radio_set_power_save(
    esp32_mquickjs_wifi_radio_lease_t *lease,
    wifi_ps_type_t requested, wifi_ps_type_t *actual);
esp_err_t esp32_mquickjs_wifi_radio_set_tx_power(
    esp32_mquickjs_wifi_radio_lease_t *lease,
    int8_t requested, int8_t *actual);

const void *
esp32_mquickjs_wifi_radio_channel_key(void);

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
/* Exact CSI owner; serialize native observation with lease release/mutation. */
esp_err_t esp32_mquickjs_wifi_radio_read_csi_config(
    const esp32_mquickjs_wifi_radio_lease_t *lease, wifi_csi_config_t *config,
    uint32_t *generation);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
esp_err_t esp32_mquickjs_wifi_radio_read_he_statistics(esp32_mquickjs_wifi_he_statistics_t *actual,
    const char **stage);
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
esp_err_t esp32_mquickjs_wifi_radio_twt_control(
    const esp32_mquickjs_wifi_radio_lease_t *application, const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_twt_control_kind_t kind, esp32_mquickjs_wifi_twt_control_t *value,
    uint32_t *generation, esp32_mquickjs_wifi_radio_config_result_t *result);
#endif
esp_err_t esp32_mquickjs_wifi_radio_write_he_statistics(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    bool rx, uint8_t selection, bool enabled, esp32_mquickjs_wifi_he_statistics_t *actual,
    esp32_mquickjs_wifi_radio_config_result_t *result);
esp_err_t esp32_mquickjs_wifi_radio_read_scan_parameters(wifi_scan_default_params_t *actual,
    const char **stage);
esp_err_t esp32_mquickjs_wifi_radio_write_scan_parameters(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const wifi_scan_default_params_t *requested, wifi_scan_default_params_t *actual,
    esp32_mquickjs_wifi_radio_config_result_t *result);
#endif

#endif
