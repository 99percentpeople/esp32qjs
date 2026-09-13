#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_wifi_promiscuous_driver.h"
#include "esp32_mquickjs_wifi_wait.h"

/* The shared Radio also builds for ESP-NOW without the Wi-Fi JS module. */
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_roaming.h"
#include "esp32_mquickjs_wifi_rrm_radio.h"
#include "esp32_mquickjs_wifi_eap_radio.h"
#include "esp32_mquickjs_wifi_smartconfig_radio.h"
#include "esp32_mquickjs_wifi_wps_radio.h"
#include "esp32_mquickjs_wifi_wps_ap_radio.h"
#include "esp32_mquickjs_wifi_dpp_radio.h"
#include "esp32_mquickjs_wifi_nan_radio.h"
#include "esp32_mquickjs_wifi_mesh_radio.h"
#include "esp32_mquickjs_wifi_ap_prestart.h"
#include "esp32_mquickjs_wifi_antenna.h"
#include "esp32_mquickjs_wifi_raw_tx_ap.h"
#include "esp32_mquickjs_wifi_raw_tx_recovery.h"
#include "esp32_mquickjs_wifi_ftm_recovery.h"
#include "esp32_mquickjs_wifi_action_radio.h"
#include "esp32_mquickjs_wifi_action_sdk.h"
#include "esp32_mquickjs_wifi_vendor_ie_watch.h"
#include "esp32_mquickjs_wifi_vendor_ie.h"
#include "esp32_mquickjs_wifi_wapi_radio.h"
#include "esp32_mquickjs_wifi_twt_probe_result.h"
#include "esp32_mquickjs_wifi_twt_setup_result.h"
#include "esp32_mquickjs_wifi_twt_broadcast_timer.h"
#include "esp32_mquickjs_wifi_twt_radio.h"
#include "esp32_mquickjs_wifi_twt_agreement_radio.h"
#endif

#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO

#include "esp32_mquickjs_nvs_flash_boot.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp32_mquickjs_core.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define WIFI_RADIO_MAX_LEASES 16U
#define WIFI_RADIO_START_EVENT_TIMEOUT_MS 5000U
#define WIFI_RADIO_STOP_EVENT_TIMEOUT_MS 1000U
ESP_EVENT_DEFINE_BASE(ESP32QJS_WIFI_RADIO_CONTROL_EVENT);
enum { RADIO_EVENTS_IDLE, RADIO_EVENTS_START, RADIO_EVENTS_STOP, RADIO_EVENTS_AP_STOP, RADIO_EVENTS_AP_START, RADIO_EVENTS_RESTART, RADIO_EVENTS_STA_START };
enum { AP_STOP_IDLE, AP_STOP_READY, AP_STOP_ATTEMPTED, AP_STOP_SUBMITTED, AP_STOP_EVENTS_DONE, AP_STOP_QUIESCED };

typedef struct {
    uint32_t identity;
    uint32_t promiscuous_identity;
    esp32_mquickjs_wifi_promiscuous_token_t rx_token;
    bool promiscuous_closing;
    esp32_mquickjs_wifi_radio_client_t client;
    wifi_mode_t required_mode;
    uint8_t primary_channel;
    uint8_t secondary_channel;
    bool fixed_channel;
    bool channel_conflict;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    uint32_t raw_tx_identity;
    bool raw_tx_channel_pinned;
#endif
} wifi_radio_live_lease_t;

typedef struct {
    portMUX_TYPE lock;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    bool driver_owned;
    bool storage_configured;
    wifi_storage_t storage;
    esp32_mquickjs_wifi_radio_config_result_t configuration;
    esp32_mquickjs_wifi_radio_config_result_t activation;
    bool started;
    bool stop_required;
    bool stop_submitted;
    uint8_t ap_stop_phase;
    bool ap_reopen_pending, ap_reopen_attempted, ap_reopen_config_pending;
    bool ap_reopen_quiesced, ap_reopen_restore_complete;
    uint32_t ap_transition_application, ap_transition_station, ap_transition_access_point;
    esp_err_t ap_stop_error;
    uint8_t event_phase, event_expected, event_seen, event_live;
    bool event_fence_posted, event_fence_seen;
    uint32_t event_identity, event_revision;
    bool restart_required;
    wifi_mode_t effective_mode;
    const char *cleanup_stage;
    esp_err_t cleanup_error;
    const char *fault_stage;
    esp_err_t fault_error;
    uint32_t generation;
    uint32_t next_lease_identity;
    uint32_t next_promiscuous_identity;
    uint32_t next_operation_identity;
    uint32_t next_lifecycle_identity;
    uint32_t wake_locks;
    esp_err_t wake_lock_error;
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    esp32_mquickjs_wifi_radio_operation_t operation;
    uint32_t clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
    uint8_t primary_channel;
    wifi_second_chan_t secondary_channel;
    uint32_t channel_generation;
    uint64_t channel_observation_revision;
    esp_err_t channel_observation_error;
    bool promiscuous_claimed;
    esp32_mquickjs_wifi_radio_client_t promiscuous_client;
    uint32_t promiscuous_lease_identity;
} wifi_radio_state_t;

static const char *TAG = "esp32qjs_wifi_radio";
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
typedef struct wifi_radio_mesh wifi_radio_mesh_t;
static wifi_radio_mesh_t *s_mesh_radio;
static bool wifi_radio_mesh_worker_locked(void);
static bool wifi_radio_mesh_stop_owner_locked(const esp32_mquickjs_wifi_radio_lease_t *lease);
static bool wifi_radio_mesh_release_owner_locked(const esp32_mquickjs_wifi_radio_lease_t *lease);
#define WIFI_RADIO_MESH_PENDING (s_mesh_radio != NULL)
#else
#define WIFI_RADIO_MESH_PENDING false
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
typedef struct wifi_radio_nan wifi_radio_nan_t;
static wifi_radio_nan_t *s_nan_radio;
#define WIFI_RADIO_NAN_PENDING (s_nan_radio != NULL)
#else
#define WIFI_RADIO_NAN_PENDING false
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
typedef struct {
    esp32_mquickjs_wifi_dpp_worker_t *worker;
    wifi_config_t saved, scratch;
    wifi_storage_t storage;
    uint32_t owners[3];
    wifi_second_chan_t home_secondary;
    uint8_t home_primary;
    bool listen_attempted, channel_restored;
    bool selection_attempted, storage_attempted, station_mutated;
    bool config_restored, storage_restored, connection_borrowed, connection_used, connection_ready;
    bool restore_requires_restart, restore_stopped, restore_started, restore_native_retired;
    bool restore_start_attempted, restore_start_accepted, restore_policies_done, config_restore_written;
    bool restore_recovery_pending, restore_recovery_reset;
    uint32_t restore_recovery_attempts;
    bool allow_ap_restart;
    wifi_mode_t restore_mode;
    wifi_ps_type_t restore_power_save;
    uint16_t restore_inactive[2];
    int8_t restore_tx_power;
    esp_err_t restore_start_error;
    esp_err_t error;
    const char *stage;
} wifi_radio_dpp_t;
static wifi_radio_dpp_t *s_dpp_radio;
static bool wifi_radio_dpp_stop_owner_locked(uint32_t identity);
#define WIFI_RADIO_DPP_PENDING (s_dpp_radio != NULL)
#else
#define WIFI_RADIO_DPP_PENDING false
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
/* Operation mutex only. Single lazy binding; decoder owns all native secrets. */
typedef struct {
    esp32_mquickjs_wifi_smartconfig_decoder_t *decoder;
    uint32_t owners[3];
    wifi_second_chan_t home_secondary;
    uint8_t home_primary;
    bool channel_restored, connection_borrowed, connection_used;
} wifi_radio_smartconfig_t;
static wifi_radio_smartconfig_t *s_sc_radio;
/* Numeric boot-scoped marker; callbacks never dereference the lazy binding. */
static uint32_t s_sc_fence_posted, s_sc_fence_seen;
#define WIFI_RADIO_SMARTCONFIG_FENCE_EVENT 7
#define WIFI_RADIO_SMARTCONFIG_PENDING (s_sc_radio != NULL)
/* Lazy WPS binding owns the saved Station secret and scratch readback. Only
 * the operation mutex accesses it; boot event callbacks use numeric tokens. */
typedef struct {
    esp32_mquickjs_wifi_wps_worker_t *worker;
    wifi_config_t saved, scratch;
    uint32_t owners[3];
    wifi_storage_t storage;
    wifi_second_chan_t home_secondary;
    uint8_t home_primary;
    bool storage_attempted, start_attempted;
    bool config_restored, channel_restored, storage_restored;
    esp_err_t error;
    const char *stage;
} wifi_radio_wps_t;
static wifi_radio_wps_t *s_wps_radio;
static uint32_t s_wps_fence_posted, s_wps_fence_seen;
#define WIFI_RADIO_WPS_FENCE_EVENT 8
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
typedef struct {
    esp32_mquickjs_wifi_wps_ap_worker_t *worker;
    uint32_t owners[3];
    esp_err_t error;
    const char *stage;
} wifi_radio_wps_ap_t;
static wifi_radio_wps_ap_t *s_wps_ap_radio;
#define WIFI_RADIO_WPS_PENDING (s_wps_radio != NULL || s_wps_ap_radio != NULL)
#else
#define WIFI_RADIO_WPS_PENDING (s_wps_radio != NULL)
#endif
#else
#define WIFI_RADIO_SMARTCONFIG_PENDING false
#define WIFI_RADIO_WPS_PENDING false
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
/* Operation mutex only. Credentials remain in the installer's native owner. */
static struct {
    uint64_t identity;
    uint32_t generation, owners[3];
    bool ready;
} s_eap_radio;
#define WIFI_RADIO_EAP_PENDING (s_eap_radio.identity != 0)
#else
#define WIFI_RADIO_EAP_PENDING false
#endif
static const char s_wifi_radio_channel_lane_key;
static wifi_radio_state_t s_radio = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
    .generation = 1,
    .next_lease_identity = 1,
    .next_promiscuous_identity = 1,
    .next_operation_identity = 1,
    .next_lifecycle_identity = 1,
};
/* Driver-owned default-loop listener. Shutdown unregisters it and waits for
 * entered callbacks before deinit; the statically allocated mutex is boot-lived. */
static esp_event_handler_instance_t s_channel_event_instance;
/* Boot-owned marker handler contains no driver/runtime/JS pointer. */
static esp_event_handler_instance_t s_lifecycle_event_instance;
typedef struct { uint32_t identity, revision; } wifi_radio_event_fence_t;
static atomic_uint s_channel_callbacks;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Radio mutation mutex serializes SDK/owner changes; snapshot critical section
 * serializes the bounded ledger with the default event-loop callback. */
static struct {
    esp32_mquickjs_wifi_action_lane_t lane;
    esp32_mquickjs_wifi_radio_lease_t lease;
    wifi_second_chan_t secondary;
    uint32_t posted_revision;
    bool fence_posted;
    /* Last admitted recovery identity; authority also requires the current
     * lifecycle. Boot-nonreused identities make the retired record inert. */
    esp32_mquickjs_wifi_radio_lifecycle_t recovery;
} s_action = {.lane = {.next_identity = 1}};
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
/* Shared TWT identity domain, including future Agreement owners. Never reset
 * on runtime or driver restart. Marker storage is boot-owned and never moves. */
static esp32_mquickjs_wifi_twt_identity_t s_twt_identity = {.next_identity = 1};
static struct {
    struct {
        esp32_mquickjs_wifi_twt_token_t token;
        uint32_t native_identity;
        esp_err_t submit_error, cleanup_error;
        const char *cleanup_stage;
        bool dispatching, cleanup_pending;
    } state;
    esp32_mquickjs_wifi_radio_lease_t lease;
    esp32_mquickjs_wifi_twt_probe_retire_t retirement;
    uint32_t operation_identity;
} s_twt_probe;
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
/* Lazy boot-retained storage: native fence callbacks must never reference a
 * Future or movable JS object. Mutation mutex serializes owner changes. */
typedef struct {
    esp32_mquickjs_wifi_twt_token_t token;
    wifi_itwt_setup_config_t requested;
    esp32_mquickjs_wifi_twt_setup_dispatch_t dispatch;
    esp32_mquickjs_wifi_radio_lease_t lease;
    esp32_mquickjs_wifi_twt_setup_retire_t retirement;
    esp_err_t submit_error, cleanup_error;
    const char *cleanup_stage;
    bool dispatching, closing, result_released, native_retired;
} wifi_radio_twt_individual_t;
static wifi_radio_twt_individual_t *s_twt_individual;
typedef struct {
    esp32_mquickjs_wifi_twt_token_t token;
    wifi_btwt_setup_config_t requested;
    esp32_mquickjs_wifi_btwt_dispatch_t dispatch;
    esp32_mquickjs_wifi_radio_lease_t lease;
    esp32_mquickjs_wifi_btwt_retire_t retirement;
    esp_err_t submit_error, teardown_error, cleanup_error;
    const char *cleanup_stage;
    bool dispatching, closing, working, native_retired, teardown_attempted;
} wifi_radio_twt_broadcast_t;
static wifi_radio_twt_broadcast_t **s_twt_broadcast;
static struct {
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    bool prepared, stopped;
} s_twt_recovery;
static bool wifi_radio_twt_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static bool wifi_radio_twt_managed_lease_locked(uint32_t identity);
static bool wifi_radio_twt_recovery_owners_locked(void);
static bool wifi_radio_twt_recovery_active(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static esp_err_t wifi_radio_twt_recovery_begin(const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station, const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_recovery_request_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode);
static esp_err_t wifi_radio_twt_recovery_checkpoint(const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
static esp_err_t wifi_radio_twt_recovery_prepare(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static esp_err_t wifi_radio_twt_recovery_phase(const esp32_mquickjs_wifi_radio_lifecycle_t *token, bool shutdown);
static esp_err_t wifi_radio_twt_recovery_stopped(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static esp_err_t wifi_radio_twt_recovery_finish(const esp32_mquickjs_wifi_radio_lifecycle_t *token);

static bool wifi_radio_twt_broadcast_lease_retained(const esp32_mquickjs_wifi_radio_lease_t *lease)
{
    if (lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT || s_twt_broadcast == NULL) return false;
    for (unsigned i = 1; i < 32; ++i) {
        const wifi_radio_twt_broadcast_t *owner = s_twt_broadcast[i];
        if (owner != NULL && owner->lease.identity == lease->identity &&
            owner->lease.generation == lease->generation && !owner->native_retired) return true;
    }
    return false;
}

static bool wifi_radio_twt_individual_lease_retained(const esp32_mquickjs_wifi_radio_lease_t *lease)
{
    if (lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT || s_twt_individual == NULL) return false;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL; ++i) {
        const wifi_radio_twt_individual_t *owner = &s_twt_individual[i];
        if (owner->token.identity != 0U && owner->lease.identity == lease->identity &&
            owner->lease.generation == lease->generation && !owner->native_retired) return true;
    }
    return false;
}
#endif
typedef struct {
    esp32_mquickjs_wifi_action_token_t token;
    uint32_t revision;
} wifi_radio_action_fence_t;
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
static struct {
    esp32_mquickjs_wifi_ftm_state_t state;
    esp32_mquickjs_wifi_radio_lease_t lease;
    wifi_ftm_initiator_cfg_t config;
    uint32_t posted_revision;
    bool fence_posted;
    esp32_mquickjs_wifi_radio_lifecycle_t recovery;
    bool recovery_stopped, recovery_timer_started, recovery_timer_ready, recovery_sdk_ready;
} s_ftm;
static bool wifi_radio_ftm_exact_locked(const esp32_mquickjs_wifi_ftm_token_t *token);
static bool wifi_radio_ftm_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static uint32_t wifi_radio_ftm_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static esp_err_t wifi_radio_ftm_recovery_timer_locked(void);
/* One lazy boot-owned TASK timer; never stores a runtime/session pointer. A
 * single pending marker is consumed before another can be armed. */
static struct {
    esp_timer_handle_t handle;
    esp32_mquickjs_wifi_ftm_token_t token;
    uint32_t revision;
    atomic_bool completed;
    bool pending;
} s_ftm_timer_fence;
typedef struct {
    esp32_mquickjs_wifi_ftm_token_t token;
    uint32_t revision;
} wifi_radio_ftm_fence_t;
static void wifi_radio_ftm_event(int32_t id, const void *data);
#endif
static struct {
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    esp32_mquickjs_wifi_raw_tx_token_t operation;
    bool stopped, sdk_fenced;
} s_raw_tx_recovery;
static bool wifi_radio_raw_tx_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static bool wifi_radio_raw_tx_recovery_owner_locked(const esp32_mquickjs_wifi_radio_lease_t *lease);
static uint32_t wifi_radio_raw_tx_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static void wifi_radio_action_event(int32_t id, const void *data);
static bool wifi_radio_action_exact_locked(const esp32_mquickjs_wifi_action_token_t *token);
static bool wifi_radio_action_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static uint32_t wifi_radio_action_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static esp32_mquickjs_wifi_wake_token_t s_wake_tokens[ESP32_MQUICKJS_WIFI_MAX_WAKE_LOCKS];
static uint32_t s_next_wake_identity = 1U;
/* Mutation-mutex owned; kept out of the interrupt-readable Radio snapshot. */
static esp32_mquickjs_wifi_tx_rate_state_t s_tx_rates = {.next_identity = 1U};
static esp32_mquickjs_wifi_policy_state_t s_policies;
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
static esp32_mquickjs_wifi_ftm_offset_state_t s_ftm_offset;
#endif
static esp32_mquickjs_wifi_rssi_request_t s_rssi_request;
/* Last observed values, not a getter for a disabled interface. All accesses
 * hold the Radio mutation mutex; physical deinit ends this generation. */
static struct {
    uint32_t generation;
    uint16_t values[2], pending_values[2];
    uint8_t known, failed, pending, attempted, persistent;
    esp_err_t error;
} s_inactive_history;
#include "esp32_mquickjs_wifi_scan_parameters.h"
/* Mutex-owned RAM observation/intent. Hidden Station values are restored at
 * activation, never reported as a live SDK read. Physical deinit resets it. */
static struct {
    wifi_scan_default_params_t value;
    uint32_t generation;
    esp_err_t error;
    bool known, pending, attempted;
} s_scan_parameters;
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
/* Current allocation ownership and a separate STOP replay intent. The latter
 * is never exposed as an actual read. Every SDK command holds the Radio mutex. */
static struct {
    esp32_mquickjs_wifi_he_statistics_t saved;
    uint8_t completed;
    bool managed, pending;
} s_he_statistics;
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
static struct {
    wifi_twt_config_t value;
    bool known, pending;
} s_twt_policy;
#endif
static esp_err_t wifi_radio_scan_parameters_observe_locked(wifi_scan_default_params_t *actual)
{
    wifi_scan_default_params_t value = {0};
    esp_err_t error = esp_wifi_get_scan_parameters(&value);
    if (error == ESP_OK && !wifi_scan_parameters_observed_valid(&value)) error = ESP_ERR_INVALID_RESPONSE;
    s_scan_parameters.generation = s_radio.generation;
    s_scan_parameters.error = error;
    s_scan_parameters.known = error == ESP_OK;
    if (error == ESP_OK) {
        s_scan_parameters.value = value;
        if (actual != NULL) *actual = value;
    }
    return error;
}
static void wifi_radio_inactive_history_record(unsigned index, uint16_t value, esp_err_t error)
{
    if (s_inactive_history.generation != s_radio.generation) {
        memset(&s_inactive_history, 0, sizeof(s_inactive_history));
        s_inactive_history.generation = s_radio.generation;
    }
    uint8_t bit = (uint8_t)(1U << index);
    if (error == ESP_OK && value < (index == 0 ? 3 : 10)) error = ESP_ERR_INVALID_RESPONSE;
    if (error == ESP_OK) {
        s_inactive_history.values[index] = value;
        s_inactive_history.known |= bit;
        s_inactive_history.failed &= (uint8_t)~bit;
        if (s_inactive_history.failed == 0U) s_inactive_history.error = ESP_OK;
    } else {
        s_inactive_history.values[index] = 0;
        s_inactive_history.known &= (uint8_t)~bit;
        s_inactive_history.failed |= bit;
        s_inactive_history.error = error;
    }
}
/* One boot-owned historical observation, no secrets or dynamic allocation.
 * Held under the mutation mutex, separate from the interrupt-readable state. */
static struct {
    esp32_mquickjs_wifi_radio_lease_t leases[2];
    esp32_mquickjs_wifi_radio_lifecycle_t start_owner;
    wifi_mode_t start_mode;
    wifi_storage_t start_storage;
    esp32_mquickjs_wifi_vendor_ie_slot_t slots[ESP32_MQUICKJS_WIFI_VENDOR_IE_SLOTS];
} s_vendor_ie;
static bool wifi_radio_vendor_ie_start_matches(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static esp_err_t wifi_radio_vendor_ie_begin_start_locked(
    const esp32_mquickjs_wifi_radio_lease_t *application, const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    esp32_mquickjs_wifi_radio_lifecycle_t *token);
static unsigned wifi_radio_vendor_ie_start_count(void);
static esp_err_t wifi_radio_vendor_ie_start_attach(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static void wifi_radio_vendor_ie_start_park(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static void wifi_radio_vendor_ie_start_commit(const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static esp32_mquickjs_wifi_radio_stop_snapshot_t s_stop_snapshot;
static void wifi_radio_invalidate_stop_snapshot_locked(void)
{
    s_stop_snapshot.unchanged = false;
}
/* Local SDK call boundary, after SDK declarations. This does not change SDK
 * symbols or calls made by other translation units. */
#include "esp32_mquickjs_wifi_radio_mutation.h"
typedef struct {
    wifi_config_t saved[2], scratch;
    wifi_country_t country;
    wifi_protocols_t protocols[2];
    wifi_bandwidths_t bandwidths[2];
    uint8_t mac[2][6];
    uint16_t inactive_time[2];
    wifi_scan_default_params_t scan_parameters;
    bool scan_parameters_known, restore_off;
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
    esp32_mquickjs_wifi_he_statistics_t he_statistics;
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    wifi_twt_config_t twt_policy;
#endif
    wifi_ps_type_t power_save;
    uint32_t event_mask;
    wifi_storage_t storage;
    uint8_t mask;
    int8_t tx_power;
    esp32_mquickjs_wifi_interval_snapshot_t interval;
    esp32_mquickjs_wifi_tx_rate_snapshot_t rates;
    wifi_band_mode_t band_mode;
    wifi_band_t band;
    wifi_second_chan_t secondary;
    uint8_t primary, visible_bands, capture_phase, inactive_mask, inactive_saved_mask;
    const char *capture_stage;
    esp_err_t capture_error;
} wifi_radio_restart_configs_t;
static struct {
    esp32_mquickjs_wifi_radio_lifecycle_t owner;
    wifi_radio_restart_configs_t *snapshot;
    uint32_t source_generation, replay_generation, interval_revision, rate_identity[2];
    wifi_mode_t mode;
    uint8_t phase, start_phase, rate_completed;
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
    uint8_t he_completed;
#endif
    bool captured;
} s_config_restart;
static esp_err_t wifi_radio_validate_protocol(uint16_t value, bool ghz5);
static esp_err_t wifi_radio_read_phy(wifi_interface_t iface, wifi_protocols_t *protocols, wifi_bandwidths_t *bandwidths);
static esp_err_t wifi_radio_write_phy(wifi_interface_t iface, const wifi_protocols_t *protocols, const wifi_bandwidths_t *bandwidths);
static bool wifi_radio_phy_equal(uint8_t bands, const wifi_protocols_t *a, const wifi_bandwidths_t *ab,
    const wifi_protocols_t *b, const wifi_bandwidths_t *bb);
static bool wifi_radio_country_valid(const wifi_country_t *country);
static esp_err_t wifi_radio_band_snapshot(wifi_band_mode_t *mode, wifi_band_t *band);
static bool wifi_radio_country_equal(const wifi_country_t *a, const wifi_country_t *b, bool include_power);
static esp_err_t wifi_radio_restart_configs_capture_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
static esp_err_t wifi_radio_restart_configs_replay_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static esp_err_t wifi_radio_restart_configs_pre_start_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
static esp_err_t wifi_radio_restart_configs_post_start_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static esp_err_t wifi_radio_restart_configs_verify_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
static esp_err_t wifi_radio_restart_configs_commit_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
static esp_err_t wifi_radio_restart_configs_commit_storage_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
static esp_err_t wifi_radio_restart_configs_start_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode,
    esp32_mquickjs_wifi_radio_lease_t *lease);
static bool wifi_radio_restart_configs_ready_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
static void wifi_radio_restart_configs_discard_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);

/* Frozen non-secret intent belongs to one exact lifecycle reservation. */
static struct {
    esp32_mquickjs_wifi_radio_lifecycle_t owner;
    esp32_mquickjs_wifi_policy_snapshot_t snapshot;
    uint32_t replay_generation;
    wifi_mode_t mode;
    uint8_t completed;
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    esp32_mquickjs_wifi_ftm_offset_snapshot_t ftm_offset;
    bool ftm_offset_completed;
#endif
} s_policy_restart;
static esp_err_t wifi_radio_policy_restart_prepare_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
static esp_err_t wifi_radio_policy_restart_replay_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, bool after_start);

static const char s_tx_rate_fault[] = "tx-rate-uncertain";
static const char s_tx_rate_restore_fault[] = "tx-rate-restore";
static esp32_mquickjs_wifi_tx_rate_lease_t s_tx_rate_lease;

static esp_err_t wifi_radio_write_tx_rate(void *opaque, wifi_interface_t interface,
    const wifi_tx_rate_config_t *config);
static esp_err_t wifi_radio_restore_tx_rate_locked(const esp32_mquickjs_wifi_radio_lease_t *lease);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI || CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
/* Only the mutation mutex accesses interval knowledge; no IRQ reader. */
static esp32_mquickjs_wifi_interval_state_t s_interval;
static esp_err_t wifi_radio_interval_writer(void *opaque, uint16_t milliseconds)
{
    (void)opaque;
    return esp_wifi_connectionless_module_set_wake_interval(milliseconds);
}
#endif
static esp_err_t wifi_radio_refresh_channel(uint8_t *primary, wifi_second_chan_t *secondary,
                                           uint32_t *generation);
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
static void wifi_radio_smartconfig_observe_fence(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    taskENTER_CRITICAL(&s_radio.lock);
    if (token->identity && token->identity == s_sc_fence_posted &&
        token->identity == s_radio.operation.identity && token->generation == s_radio.operation.generation &&
        token->lease_identity == s_radio.operation.lease_identity &&
        token->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SMARTCONFIG && token->kind == s_radio.operation.kind)
        s_sc_fence_seen = token->identity;
    taskEXIT_CRITICAL(&s_radio.lock);
}
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
static void wifi_radio_wps_observe_fence(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    taskENTER_CRITICAL(&s_radio.lock);
    if (token->identity && token->identity == s_wps_fence_posted &&
        token->identity == s_radio.operation.identity && token->generation == s_radio.operation.generation &&
        token->lease_identity == s_radio.operation.lease_identity &&
        token->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_WPS && token->kind == s_radio.operation.kind)
        s_wps_fence_seen = token->identity;
    taskEXIT_CRITICAL(&s_radio.lock);
}
#endif
static void wifi_radio_lifecycle_fence(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != ESP32QJS_WIFI_RADIO_CONTROL_EVENT || data == NULL) return;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (id == WIFI_RADIO_WPS_FENCE_EVENT) {
        wifi_radio_wps_observe_fence(data);
        return;
    }
    if (id == WIFI_RADIO_SMARTCONFIG_FENCE_EVENT) {
        wifi_radio_smartconfig_observe_fence(data);
        return;
    }
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (id == ESP32_MQUICKJS_WIFI_BTWT_FENCE_EVENT) {
        esp32_mquickjs_wifi_btwt_setup_observe_fence(data);
        return;
    }
    if (id == ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_EVENT) {
        esp32_mquickjs_wifi_twt_setup_result_observe_fence(data);
        return;
    }
    if (id == ESP32_MQUICKJS_WIFI_TWT_PROBE_FENCE_EVENT) {
        esp32_mquickjs_wifi_twt_probe_result_observe_fence(data);
        return;
    }
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (id == 3) {
        const wifi_radio_ftm_fence_t *fence = data;
        taskENTER_CRITICAL(&s_radio.lock);
        esp32_mquickjs_wifi_ftm_state_t *state = &s_ftm.state;
        if (s_ftm.fence_posted && s_ftm.posted_revision == fence->revision &&
            state->token.identity != 0U && state->token.identity == fence->token.identity &&
            state->token.generation == fence->token.generation && state->revision == fence->revision &&
            state->sdk_fenced && state->report_consumed && !state->ambiguous) {
            state->event_fenced = true;
            s_ftm.fence_posted = false;
        }
        taskEXIT_CRITICAL(&s_radio.lock);
        return;
    }
#endif
    if (id == 2) {
        const wifi_radio_action_fence_t *action = data;
        taskENTER_CRITICAL(&s_radio.lock);
        if (s_action.fence_posted && s_action.posted_revision == action->revision &&
            esp32_mquickjs_wifi_action_event_fenced(&s_action.lane, &action->token, action->revision))
            s_action.fence_posted = false;
        taskEXIT_CRITICAL(&s_radio.lock);
        return;
    }
#endif
    if (id != 1) return;
    const wifi_radio_event_fence_t *fence = data;
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_radio.event_phase != RADIO_EVENTS_IDLE && s_radio.event_fence_posted &&
        fence->identity == s_radio.event_identity && fence->revision == s_radio.event_revision)
        s_radio.event_fence_seen = true;
    taskEXIT_CRITICAL(&s_radio.lock);
}

static void wifi_radio_observe_lifecycle_event(int32_t id)
{
    uint8_t mode = 0;
    bool start = false;
    if (id == WIFI_EVENT_STA_START) { mode = WIFI_MODE_STA; start = true; }
    else if (id == WIFI_EVENT_STA_STOP) mode = WIFI_MODE_STA;
    else if (id == WIFI_EVENT_AP_START) { mode = WIFI_MODE_AP; start = true; }
    else if (id == WIFI_EVENT_AP_STOP) mode = WIFI_MODE_AP;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    else if (id == WIFI_EVENT_NAN_SYNC_STARTED) { mode = WIFI_MODE_NAN; start = true; }
    else if (id == WIFI_EVENT_NAN_SYNC_STOPPED) mode = WIFI_MODE_NAN;
#endif
    if (mode == 0) return;
    taskENTER_CRITICAL(&s_radio.lock);
    if (start) s_radio.event_live |= mode;
    else s_radio.event_live &= (uint8_t)~mode;
    if (s_radio.event_phase != RADIO_EVENTS_IDLE) {
        if (s_radio.event_revision != UINT32_MAX) s_radio.event_revision++;
        /* A new native event invalidates an earlier queued marker. Matching
         * revision is required, so that marker cannot acknowledge its suffix. */
        s_radio.event_fence_posted = false;
        s_radio.event_fence_seen = false;
        if (s_radio.event_phase == RADIO_EVENTS_RESTART) {
            /* High bits remember STOP; START counts only after that STOP. A
             * second STOP invalidates the earlier START for this interface. */
            if (!start) {
                s_radio.event_seen |= (uint8_t)(mode << 4);
                s_radio.event_seen &= (uint8_t)~mode;
            } else if (s_radio.event_seen & (mode << 4)) s_radio.event_seen |= mode;
        } else if ((start && (s_radio.event_phase == RADIO_EVENTS_START || s_radio.event_phase == RADIO_EVENTS_AP_START ||
            s_radio.event_phase == RADIO_EVENTS_STA_START)) ||
            (!start && (s_radio.event_phase == RADIO_EVENTS_STOP || s_radio.event_phase == RADIO_EVENTS_AP_STOP))) s_radio.event_seen |= mode;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
}

/* Mutation mutex held, no SDK call in the snapshot critical section. */
static esp_err_t wifi_radio_begin_events(unsigned phase, wifi_mode_t expected)
{
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_radio.event_identity == UINT32_MAX) {
        s_radio.restart_required = true;
        taskEXIT_CRITICAL(&s_radio.lock);
        return ESP_ERR_NO_MEM;
    }
    s_radio.event_identity++;
    s_radio.event_phase = phase;
    s_radio.event_expected = expected;
    s_radio.event_seen = 0;
    s_radio.event_revision = 0;
    s_radio.event_fence_posted = false;
    s_radio.event_fence_seen = false;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

static esp_err_t wifi_radio_wait_events_inner(esp32_mquickjs_runtime_t *runtime)
{
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = esp32_mquickjs_wifi_wait_remaining(pdMS_TO_TICKS((s_radio.event_phase == RADIO_EVENTS_STOP || s_radio.event_phase == RADIO_EVENTS_AP_STOP)
        ? WIFI_RADIO_STOP_EVENT_TIMEOUT_MS : WIFI_RADIO_START_EVENT_TIMEOUT_MS));
    for (;;) {
        if (!esp32_mquickjs_cooperate(runtime)) return ESP_ERR_INVALID_STATE;
        wifi_radio_event_fence_t fence = {0};
        bool post = false;
        taskENTER_CRITICAL(&s_radio.lock);
        if (s_radio.event_phase == RADIO_EVENTS_IDLE || s_radio.event_revision == UINT32_MAX) {
            taskEXIT_CRITICAL(&s_radio.lock);
            return ESP_ERR_INVALID_STATE;
        }
        bool ready = (s_radio.event_seen & s_radio.event_expected) == s_radio.event_expected;
        if (s_radio.event_phase == RADIO_EVENTS_RESTART)
            ready = ready && ((s_radio.event_seen >> 4) & s_radio.event_expected) == s_radio.event_expected &&
                s_radio.event_live == s_radio.event_expected;
        else if (s_radio.event_phase == RADIO_EVENTS_START)
            ready = ready && (s_radio.event_live & s_radio.event_expected) == s_radio.event_expected;
        else if (s_radio.event_phase == RADIO_EVENTS_AP_START || s_radio.event_phase == RADIO_EVENTS_STA_START)
            ready = ready && s_radio.event_live == WIFI_MODE_APSTA;
        else if (s_radio.event_phase == RADIO_EVENTS_AP_STOP)
            ready = ready && s_radio.event_live == WIFI_MODE_STA;
        else ready = ready && s_radio.event_live == 0;
        if (ready && s_radio.event_fence_seen) {
            s_radio.event_phase = RADIO_EVENTS_IDLE;
            taskEXIT_CRITICAL(&s_radio.lock);
            return ESP_OK;
        }
        if (ready && !s_radio.event_fence_posted) {
            fence = (wifi_radio_event_fence_t){s_radio.event_identity, s_radio.event_revision};
            s_radio.event_fence_posted = true;
            post = true;
        }
        taskEXIT_CRITICAL(&s_radio.lock);
        if (post) {
            esp_err_t err = esp_event_post(ESP32QJS_WIFI_RADIO_CONTROL_EVENT, 1,
                &fence, sizeof(fence), 0);
            if (err != ESP_OK) {
                taskENTER_CRITICAL(&s_radio.lock);
                if (fence.identity == s_radio.event_identity && fence.revision == s_radio.event_revision)
                    s_radio.event_fence_posted = false;
                taskEXIT_CRITICAL(&s_radio.lock);
                return err;
            }
        }
        if ((TickType_t)(xTaskGetTickCount() - started) >= timeout)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
}

static esp_err_t wifi_radio_wait_events(void)
{
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
    if (wifi_radio_mesh_worker_locked()) return wifi_radio_wait_events_inner(NULL);
#endif
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    esp32_mquickjs_native_wait_t wait;
    esp32_mquickjs_native_wait_begin(runtime, &wait);
    esp_err_t err = wifi_radio_wait_events_inner(runtime);
    esp32_mquickjs_native_wait_end(runtime, &wait);
    return err;
}

static void wifi_radio_channel_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base != WIFI_EVENT) return;
    atomic_fetch_add_explicit(&s_channel_callbacks, 1, memory_order_acq_rel);
    wifi_radio_observe_lifecycle_event(id);
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    wifi_radio_action_event(id, data);
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    wifi_radio_ftm_event(id, data);
#endif
#endif
    if (id != WIFI_EVENT_HOME_CHANNEL_CHANGE && id != WIFI_EVENT_STA_CONNECTED &&
        id != WIFI_EVENT_AP_START) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    bool owned = s_radio.driver_owned;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (!owned) goto done;
    uint8_t primary;
    wifi_second_chan_t secondary;
    uint32_t generation;
    /* Read current SDK state rather than replaying a delayed event payload.
     * Never wait for the Radio mutation mutex from the event-loop task. */
    (void)wifi_radio_refresh_channel(&primary, &secondary, &generation);
done:
    atomic_fetch_sub_explicit(&s_channel_callbacks, 1, memory_order_release);
}

/* Boot-lived task mutex covers validation, driver effects and release.
 * SDK calls never run under the short snapshot critical section. */
static StaticSemaphore_t s_operation_mutex_storage;
static SemaphoreHandle_t s_operation_mutex;
static _Atomic int s_operation_mutex_state;

static void wifi_radio_operation_lock(void)
{
    int expected = 0;
    if (atomic_compare_exchange_strong(&s_operation_mutex_state, &expected, 1)) {
        s_operation_mutex = xSemaphoreCreateMutexStatic(&s_operation_mutex_storage);
        atomic_store_explicit(&s_operation_mutex_state, 2, memory_order_release);
    } else {
        while (atomic_load_explicit(&s_operation_mutex_state,
                                    memory_order_acquire) != 2) vTaskDelay(1);
    }
    (void)xSemaphoreTake(s_operation_mutex, portMAX_DELAY);
}

static void wifi_radio_operation_unlock(void)
{
    (void)xSemaphoreGive(s_operation_mutex);
}

const char *esp32_mquickjs_wifi_radio_driver_state_name(
    esp32_mquickjs_wifi_radio_driver_state_t state)
{
    switch (state) {
    case ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED: return "uninitialized";
    case ESP32_MQUICKJS_WIFI_RADIO_INITIALIZING: return "initializing";
    case ESP32_MQUICKJS_WIFI_RADIO_STOPPED: return "stopped";
    case ESP32_MQUICKJS_WIFI_RADIO_STARTING: return "starting";
    case ESP32_MQUICKJS_WIFI_RADIO_STARTED: return "started";
    case ESP32_MQUICKJS_WIFI_RADIO_STOPPING: return "stopping";
    case ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING: return "cleanup-pending";
    case ESP32_MQUICKJS_WIFI_RADIO_FAULTED:
    default: return "faulted";
    }
}

static void wifi_radio_set_state(esp32_mquickjs_wifi_radio_driver_state_t state)
{
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.driver_state = state;
    taskEXIT_CRITICAL(&s_radio.lock);
}

static esp_err_t wifi_radio_record_fault(const char *stage, esp_err_t err)
{
    if (err != ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        if (s_radio.fault_stage == NULL) {
            s_radio.fault_stage = stage;
            s_radio.fault_error = err;
        }
        s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_FAULTED;
        taskEXIT_CRITICAL(&s_radio.lock);
    }
    return err;
}

static esp_err_t wifi_radio_cleanup_fault(const char *stage, esp_err_t err)
{
    if (err == ESP_OK) return ESP_OK;
    wifi_radio_record_fault(stage, err);
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.cleanup_stage = stage;
    s_radio.cleanup_error = err;
    s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;
    taskEXIT_CRITICAL(&s_radio.lock);
    return err;
}

/* Called with the mutation mutex. Successful deinit is the only reset of
 * driver ownership; a failed public init cannot prove SDK cleanup completed. */
static esp_err_t wifi_radio_initialize(void)
{
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    esp_err_t antenna_error = esp32_mquickjs_wifi_antenna_fault();
    if (antenna_error != ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.restart_required = true;
        taskEXIT_CRITICAL(&s_radio.lock);
        return wifi_radio_record_fault("antenna-device-restart-required", antenna_error);
    }
#endif
    if (s_radio.storage_configured) return ESP_OK;
    wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_INITIALIZING);
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (s_channel_event_instance == NULL) {
        esp_err_t loop_err = esp_event_loop_create_default();
        if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE)
            return wifi_radio_record_fault("event-loop", loop_err);
        loop_err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
            wifi_radio_channel_event, NULL, &s_channel_event_instance);
        if (loop_err != ESP_OK) return wifi_radio_record_fault("channel-handler", loop_err);
    }
    if (s_lifecycle_event_instance == NULL) {
        esp_err_t event_err = esp_event_handler_instance_register(
            ESP32QJS_WIFI_RADIO_CONTROL_EVENT, ESP_EVENT_ANY_ID, wifi_radio_lifecycle_fence,
            NULL, &s_lifecycle_event_instance);
        if (event_err != ESP_OK) return wifi_radio_record_fault("lifecycle-handler", event_err);
    }
    esp_err_t err = wifi_radio_record_fault(
        "nvs", esp32_mquickjs_nvs_flash_ensure_initialized());
    if (err == ESP_OK) {
        err = wifi_radio_record_fault("init", esp_wifi_init(&config));
        if (err == ESP_OK) {
            taskENTER_CRITICAL(&s_radio.lock);
            s_radio.driver_owned = true;
            taskEXIT_CRITICAL(&s_radio.lock);
        }
    }
    if (err != ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.restart_required = true;
        taskEXIT_CRITICAL(&s_radio.lock);
        return err;
    }
    err = wifi_radio_record_fault("storage", esp_wifi_set_storage(WIFI_STORAGE_RAM));
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI || CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
    if (err == ESP_OK) {
        esp32_mquickjs_wifi_interval_result_t result;
        /* Establish framework policy with an actual accepted write. The default
         * constant alone is not knowledge of the driver's previous state. */
        err = wifi_radio_record_fault("interval-baseline", esp32_mquickjs_wifi_interval_write(
            &s_interval, s_radio.generation, ESP_WIFI_CONNECTIONLESS_INTERVAL_DEFAULT_MODE,
            wifi_radio_interval_writer, NULL, &result));
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (err == ESP_OK) err = wifi_radio_record_fault("vendor-ie-register",
        esp32_mquickjs_wifi_vendor_ie_broker_register(s_radio.generation));
#endif
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.storage_configured = true;
        s_radio.storage = WIFI_STORAGE_RAM;
        s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        taskEXIT_CRITICAL(&s_radio.lock);
    }
    return err;
}

static wifi_mode_t wifi_radio_requested_mode(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        if (s_radio.leases[i].identity != 0U)
            mode = (wifi_mode_t)(mode | s_radio.leases[i].required_mode);
    }
    return mode;
}

static bool wifi_radio_lease_valid(
    const esp32_mquickjs_wifi_radio_lease_t *lease)
{
    bool valid = false;

    if (lease == NULL || !lease->acquired || lease->identity == 0U ||
        (unsigned)lease->client >= ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT) {
        return false;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    if (lease->generation == s_radio.generation) {
        for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
            if (s_radio.leases[i].identity == lease->identity &&
                s_radio.leases[i].client == lease->client) {
                valid = true;
                break;
            }
        }
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    return valid;
}

#if CONFIG_SOC_WIFI_SUPPORT_5G || CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
uint32_t esp32_mquickjs_wifi_radio_5ghz_channel_bit(uint8_t channel)
{
    static const uint8_t channels[] = {
        36U, 40U, 44U, 48U, 52U, 56U, 60U, 64U,
        100U, 104U, 108U, 112U, 116U, 120U, 124U, 128U,
        132U, 136U, 140U, 144U, 149U, 153U, 157U, 161U,
        165U, 169U, 173U, 177U,
    };
    uint32_t index;

    for (index = 0U; index < sizeof(channels); ++index) {
        if (channels[index] == channel) return 1UL << (index + 1U);
    }
    return 0U;
}
#endif

static esp_err_t wifi_radio_validate_regulatory_channel(uint8_t channel)
{
    wifi_country_t country = {0};
    esp_err_t err = esp_wifi_get_country(&country);

    if (err != ESP_OK) return err;
    if (channel <= 14U) {
        uint16_t end = (uint16_t)country.schan + country.nchan;

        return country.nchan > 0U && channel >= country.schan && channel < end
            ? ESP_OK : ESP_ERR_NOT_ALLOWED;
    }
#if CONFIG_SOC_WIFI_SUPPORT_5G
    {
        uint32_t bit = esp32_mquickjs_wifi_radio_5ghz_channel_bit(channel);

        if (bit == 0U) return ESP_ERR_INVALID_ARG;
        if (country.policy == WIFI_COUNTRY_POLICY_MANUAL &&
            country.wifi_5g_channel_mask != 0U &&
            (country.wifi_5g_channel_mask & bit) == 0U) {
            return ESP_ERR_NOT_ALLOWED;
        }
        return ESP_OK;
    }
#else
    return ESP_ERR_INVALID_ARG;
#endif
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI || CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
static bool wifi_radio_interval_owner(const esp32_mquickjs_wifi_radio_lease_t *lease)
{
    return lease != NULL && lease->client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW &&
        wifi_radio_lease_valid(lease) && s_radio.driver_owned && s_radio.storage_configured &&
        s_radio.lifecycle.identity == 0U && s_radio.operation.identity == 0U;
}

esp_err_t esp32_mquickjs_wifi_radio_interval_configure(
    const esp32_mquickjs_wifi_radio_lease_t *lease, uint16_t milliseconds,
    esp32_mquickjs_wifi_interval_token_t *token)
{
    if (token == NULL) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_interval_owner(lease) || !s_radio.started ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.fault_stage != NULL ||
        s_radio.cleanup_stage != NULL || s_radio.restart_required || s_radio.wake_locks != 0U ||
        s_radio.promiscuous_claimed) goto done;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (s_tx_rate_lease.identity != 0U) goto done;
#endif
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (owner->identity == 0U || owner->identity == lease->identity) continue;
        if (owner->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION &&
            owner->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA &&
            owner->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP) goto done;
        if (owner->fixed_channel || owner->channel_conflict) goto done;
    }
    esp32_mquickjs_wifi_interval_result_t result;
    if (token->identity == 0U)
        err = esp32_mquickjs_wifi_interval_acquire(&s_interval, lease->generation, lease->identity,
            milliseconds, wifi_radio_interval_writer, NULL, token, &result);
    else if (token->generation == lease->generation && token->owner_identity == lease->identity)
        err = esp32_mquickjs_wifi_interval_update(&s_interval, token, milliseconds,
            wifi_radio_interval_writer, NULL, &result);
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_interval_release(
    const esp32_mquickjs_wifi_radio_lease_t *lease, esp32_mquickjs_wifi_interval_token_t *token)
{
    if (token == NULL) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (token->identity == 0U && token->owner_identity == 0U && token->generation == 0U) {
        err = ESP_OK;
        goto done;
    }
    if (!wifi_radio_interval_owner(lease) || token->generation != lease->generation ||
        token->owner_identity != lease->identity) goto done;
    esp32_mquickjs_wifi_interval_result_t result;
    /* Restore the captured value under the same Radio exclusion. SDK failure
     * retains the token; unrelated Radio faults are not cleared by this step. */
    err = esp32_mquickjs_wifi_interval_release(&s_interval, token, wifi_radio_interval_writer, NULL, &result);
done:
    wifi_radio_operation_unlock();
    return err;
}

void esp32_mquickjs_wifi_radio_interval_status(esp32_mquickjs_wifi_interval_state_t *status)
{
    if (status == NULL) return;
    wifi_radio_operation_lock();
    *status = s_interval;
    wifi_radio_operation_unlock();
}
#endif

static esp_err_t wifi_radio_acquire_locked(
    esp32_mquickjs_wifi_radio_client_t client,
    wifi_mode_t required_mode,
    esp32_mquickjs_wifi_radio_lease_t *out_lease)
{
    if (out_lease == NULL ||
        (unsigned)client >= ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT ||
        (required_mode != WIFI_MODE_STA && required_mode != WIFI_MODE_AP &&
         required_mode != WIFI_MODE_APSTA)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_lease, 0, sizeof(*out_lease));
    if (WIFI_RADIO_NAN_PENDING || WIFI_RADIO_MESH_PENDING) return ESP_ERR_INVALID_STATE;
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
    if (client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MESH) return ESP_ERR_INVALID_ARG;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    /* NAN has a separate exclusive admission; a generic caller cannot create
     * a NAN owner with STA/AP requirements. */
    if (client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_NAN) return ESP_ERR_INVALID_ARG;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (WIFI_RADIO_SMARTCONFIG_PENDING || WIFI_RADIO_WPS_PENDING) return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
    if (WIFI_RADIO_DPP_PENDING) return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (WIFI_RADIO_EAP_PENDING) return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI || CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
    if (s_interval.owner.identity != 0U || s_interval.uncertain) return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (s_tx_rate_lease.identity != 0U) return ESP_ERR_INVALID_STATE;
#endif
    /* Raw TX and Vendor IE may join an already enabled interface without AP
     * reconfiguration. Other AP-sharing paths use their lifecycle handoff. */
    if (s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP] != 0U
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        &&
        !(((client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX && s_radio.started) ||
            client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE ||
            (client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION && s_radio.started)
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
            || (client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT && s_radio.started)
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
            || (client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_FTM && s_radio.started)
#endif
            ) &&
          (s_radio.effective_mode & required_mode) == required_mode)
#endif
        ) return ESP_ERR_INVALID_STATE;
    if (s_radio.fault_stage != NULL) return s_radio.fault_error;
    taskENTER_CRITICAL(&s_radio.lock);
    size_t slot;
    for (slot = 0; slot < WIFI_RADIO_MAX_LEASES; ++slot) {
        if (s_radio.leases[slot].identity == 0U) break;
    }
    if (slot == WIFI_RADIO_MAX_LEASES || s_radio.next_lease_identity == 0U) {
        taskEXIT_CRITICAL(&s_radio.lock);
        return ESP_ERR_NO_MEM;
    }
    s_radio.clients[client]++;
    out_lease->generation = s_radio.generation;
    out_lease->identity = s_radio.next_lease_identity++;
    s_radio.leases[slot] = (wifi_radio_live_lease_t){
        .identity = out_lease->identity,
        .client = client,
        .required_mode = required_mode,
    };
    out_lease->client = client;
    out_lease->acquired = true;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

/* Caller owns Radio mutation and has admitted either a valid lease or the
 * exclusive restart preparation. Both use the same native event/fence path. */
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* A disabled interface has no legal inactive-time setter. A committed restart
 * transfers its desired value here; apply it at the next real activation before
 * owner publication. Observations cannot overwrite this separate intent. */
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
static esp_err_t wifi_radio_retire_he_statistics_locked(void)
{
    if (!s_he_statistics.managed) return ESP_OK;
    esp32_mquickjs_wifi_he_statistics_t actual;
    esp_err_t error = esp32_mquickjs_wifi_he_statistics_snapshot(&actual, true);
    if (error != ESP_OK) return wifi_radio_cleanup_fault("he-statistics-retire", error);
    /* A previous failed restore owns the original complete intent, not this
     * partially restored prefix. STOP retry must not replace that intent. */
    if (!s_he_statistics.pending) s_he_statistics.saved = actual;
    s_he_statistics.pending = true;
    s_he_statistics.managed = false;
    s_he_statistics.completed = 0;
    return ESP_OK;
}

static esp_err_t wifi_radio_restore_he_statistics_locked(void)
{
    if (!s_he_statistics.pending) return ESP_OK;
    if (!s_radio.started || s_radio.fault_stage || s_radio.cleanup_stage || s_radio.restart_required)
        return ESP_ERR_INVALID_STATE;
    s_he_statistics.managed = true; /* Retain cleanup even on partial failure. */
    esp_err_t error = esp32_mquickjs_wifi_he_statistics_restore(&s_he_statistics.saved, &s_he_statistics.completed);
    if (error != ESP_OK) return wifi_radio_record_fault("he-statistics-restore", error);
    s_he_statistics.pending = false;
    return ESP_OK;
}
#endif

static esp_err_t wifi_radio_restore_scan_parameters_locked(wifi_mode_t mode)
{
    if (!s_scan_parameters.pending || !(mode & WIFI_MODE_STA) || s_scan_parameters.generation != s_radio.generation ||
        (!s_scan_parameters.known && s_scan_parameters.error == ESP_OK)) return ESP_OK;
    if (s_radio.fault_stage || s_radio.cleanup_stage || s_radio.restart_required) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_radio_config_result_t result = {.stage = "scan-parameters-restore-readback"};
    wifi_scan_default_params_t wanted = s_scan_parameters.value, actual = {0};
    esp_err_t error = s_scan_parameters.error;
    if (error != ESP_OK) { result.stage = "scan-parameters-restore-source"; goto failed; }
    error = esp_wifi_get_scan_parameters(&actual);
    if (error == ESP_OK && !wifi_scan_parameters_equal(&wanted, &actual) && !s_scan_parameters.attempted) {
        result.stage = "scan-parameters-restore-write";
        result.mutation_attempted = s_scan_parameters.attempted = true;
        error = wifi_scan_parameters_set_native(&wanted);
        if (error == ESP_OK) {
            result.stage = "scan-parameters-restore-readback";
            error = esp_wifi_get_scan_parameters(&actual);
        }
    }
    if (error == ESP_OK && !wifi_scan_parameters_equal(&wanted, &actual)) error = ESP_ERR_INVALID_RESPONSE;
    if (error == ESP_OK) { s_scan_parameters.pending = s_scan_parameters.attempted = false; return ESP_OK; }
failed:
    result.mutation_attempted |= s_scan_parameters.attempted;
    result.error = error;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.configuration = result; taskEXIT_CRITICAL(&s_radio.lock);
    return wifi_radio_record_fault(result.stage, error);
}

static esp_err_t wifi_radio_restore_inactive_locked(wifi_mode_t mode)
{
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (s_twt_policy.pending) {
        if (!s_radio.started || s_radio.fault_stage || s_radio.cleanup_stage || s_radio.restart_required)
            return ESP_ERR_INVALID_STATE;
        esp32_mquickjs_wifi_twt_control_t value = {.config = s_twt_policy.value};
        esp_err_t error = esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG, &value);
        if (error != ESP_OK) return wifi_radio_record_fault("twt-policy-restore", error);
        s_twt_policy.pending = false;
    }
#endif
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
    esp_err_t he_error = wifi_radio_restore_he_statistics_locked();
    if (he_error != ESP_OK) return he_error;
#endif
    esp_err_t scan_error = wifi_radio_restore_scan_parameters_locked(mode);
    if (scan_error != ESP_OK) return scan_error;
    if (s_inactive_history.pending == 0U) return ESP_OK;
    /* Preserve the original operation's diagnostics while faulted. */
    if (s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required)
        return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_radio_config_result_t result = {.stage = "inactive-restore-admission"};
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_inactive_history.generation != s_radio.generation || !s_radio.driver_owned ||
        !s_radio.storage_configured || !s_radio.started || s_radio.fault_stage != NULL ||
        (s_radio.storage != WIFI_STORAGE_RAM && s_radio.storage != WIFI_STORAGE_FLASH) ||
        s_radio.cleanup_stage != NULL || s_radio.restart_required) goto done;
    uint8_t mask = s_inactive_history.pending & (uint8_t)mode;
    if (mask == 0U) return ESP_OK;
    result.mutation_attempted = (s_inactive_history.attempted & mask) != 0U;
    result.persistent_mutation_possible = (s_inactive_history.persistent & mask) != 0U;
    wifi_mode_t actual_mode;
    result.stage = "inactive-restore-mode";
    err = esp_wifi_get_mode(&actual_mode);
    if (err == ESP_OK && actual_mode != mode) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) goto done;
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t bit = (uint8_t)(1U << i);
        if (!(mask & bit)) continue;
        wifi_interface_t interface = i == 0 ? WIFI_IF_STA : WIFI_IF_AP;
        uint16_t wanted = s_inactive_history.pending_values[i], actual = 0;
        if (wanted < (i == 0 ? 3 : 10)) { err = ESP_ERR_INVALID_STATE; goto done; }
        bool attempted = (s_inactive_history.attempted & bit) != 0U;
        result.mutation_attempted |= attempted;
        result.persistent_mutation_possible |= (s_inactive_history.persistent & bit) != 0U;
        result.stage = i == 0 ? "station-inactive-restore-readback" : "ap-inactive-restore-readback";
        err = esp_wifi_get_inactive_time(interface, &actual);
        if (err == ESP_OK && actual != wanted && !attempted) {
            result.stage = i == 0 ? "station-inactive-restore-write" : "ap-inactive-restore-write";
            s_inactive_history.attempted |= bit;
            if (s_radio.storage == WIFI_STORAGE_FLASH) s_inactive_history.persistent |= bit;
            result.mutation_attempted = true;
            result.persistent_mutation_possible |= s_radio.storage == WIFI_STORAGE_FLASH;
            err = esp_wifi_set_inactive_time(interface, wanted);
            if (err == ESP_OK) {
                result.stage = i == 0 ? "station-inactive-restore-readback" : "ap-inactive-restore-readback";
                err = esp_wifi_get_inactive_time(interface, &actual);
            }
        }
        if (err == ESP_OK && actual != wanted) err = ESP_ERR_INVALID_RESPONSE;
        wifi_radio_inactive_history_record(i, actual, err);
        if (err != ESP_OK) goto done;
        s_inactive_history.pending &= (uint8_t)~bit;
        s_inactive_history.attempted &= (uint8_t)~bit;
        s_inactive_history.persistent &= (uint8_t)~bit;
        s_inactive_history.pending_values[i] = 0;
    }
    result.stage = "inactive-restore-complete";
done:
    result.error = err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = result;
    taskEXIT_CRITICAL(&s_radio.lock);
    return wifi_radio_record_fault(result.stage, err);
}
#endif

static esp_err_t wifi_radio_start_stopped_locked(wifi_mode_t mode, bool restore_settings)
{
    esp_err_t err;
    err = wifi_radio_begin_events(RADIO_EVENTS_START, mode);
    if (err != ESP_OK) return wifi_radio_record_fault("start-events", err);
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.stop_required = true;
    s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_STARTING;
    taskEXIT_CRITICAL(&s_radio.lock);
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    s_twt_policy.pending = s_twt_policy.known;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if ((mode & WIFI_MODE_STA) && s_scan_parameters.generation == s_radio.generation &&
        (s_scan_parameters.known || s_scan_parameters.error != ESP_OK)) s_scan_parameters.pending = true;
#endif
    err = wifi_radio_record_fault("start", esp_wifi_start());
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.started = true;
        taskEXIT_CRITICAL(&s_radio.lock);
        err = wifi_radio_record_fault("start-events", wifi_radio_wait_events());
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        if (err == ESP_OK && restore_settings) err = wifi_radio_restore_inactive_locked(mode);
#else
        (void)restore_settings;
#endif
        if (err == ESP_OK) wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STARTED);
    }
    return err;
}

static esp_err_t wifi_radio_ensure_started_locked(
    esp32_mquickjs_wifi_radio_lease_t *lease)
{
    wifi_mode_t current_mode;
    esp_err_t err;
    if (!wifi_radio_lease_valid(lease)) return ESP_ERR_INVALID_STATE;
    if (s_radio.fault_stage != NULL) return s_radio.fault_error;
    ESP_RETURN_ON_ERROR(wifi_radio_initialize(), TAG, "Wi-Fi radio initialization failed");
    wifi_mode_t required_mode = wifi_radio_requested_mode();
    ESP_RETURN_ON_ERROR(
        wifi_radio_record_fault("get-mode", esp_wifi_get_mode(&current_mode)),
        TAG, "read Wi-Fi mode failed");
    if (current_mode != WIFI_MODE_NULL && current_mode != WIFI_MODE_STA &&
        current_mode != WIFI_MODE_AP && current_mode != WIFI_MODE_APSTA)
        return ESP_ERR_INVALID_STATE;
    /* Keep an effective superset while running: dropping an AP lease must not
     * tear down the surviving STA. Reconcile exactly at the next stopped start. */
    wifi_mode_t merged_mode = s_radio.started
        ? (wifi_mode_t)(current_mode | required_mode) : required_mode;
    if (merged_mode != current_mode) {
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
            if (s_radio.leases[i].raw_tx_identity != 0U) return ESP_ERR_INVALID_STATE;
#endif
        if (s_radio.operation.identity != 0U) return ESP_ERR_INVALID_STATE;
        if (s_radio.started) {
            err = wifi_radio_begin_events(RADIO_EVENTS_START,
                (wifi_mode_t)(merged_mode & ~current_mode));
            if (err != ESP_OK) return wifi_radio_record_fault("start-events", err);
            wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STARTING);
        }
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        if (s_radio.started && !(current_mode & WIFI_MODE_STA) && (merged_mode & WIFI_MODE_STA) &&
            s_scan_parameters.generation == s_radio.generation &&
            (s_scan_parameters.known || s_scan_parameters.error != ESP_OK)) s_scan_parameters.pending = true;
#endif
        ESP_RETURN_ON_ERROR(
            wifi_radio_record_fault("mode", esp_wifi_set_mode(merged_mode)),
            TAG, "set Wi-Fi mode failed");
    }
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.effective_mode = merged_mode;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (s_radio.started) {
        if (merged_mode != current_mode) {
            err = wifi_radio_record_fault("start-events", wifi_radio_wait_events());
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
            if (err == ESP_OK) err = wifi_radio_restore_inactive_locked(merged_mode);
#endif
            if (err == ESP_OK) wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STARTED);
            return err;
        }
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        return wifi_radio_restore_inactive_locked(merged_mode);
#else
        return ESP_OK;
#endif
    }
    return wifi_radio_start_stopped_locked(merged_mode, true);
}

static esp_err_t wifi_radio_refresh_channel(
    uint8_t *primary, wifi_second_chan_t *secondary, uint32_t *channel_generation)
{
    if (primary == NULL || secondary == NULL || channel_generation == NULL)
        return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL(&s_radio.lock);
    uint32_t driver_generation = s_radio.generation;
    uint64_t revision = s_radio.channel_observation_revision;
    taskEXIT_CRITICAL(&s_radio.lock);
    uint8_t actual_primary = 0;
    wifi_second_chan_t actual_secondary = WIFI_SECOND_CHAN_NONE;
    esp_err_t err = esp_wifi_get_channel(&actual_primary, &actual_secondary);
    if (err == ESP_OK && actual_primary == 0U) err = ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_radio.lock);
    if (driver_generation != s_radio.generation) {
        taskEXIT_CRITICAL(&s_radio.lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (revision == s_radio.channel_observation_revision) {
        s_radio.channel_observation_revision++;
        s_radio.channel_observation_error = err;
        if (err == ESP_OK) {
            if (s_radio.channel_generation == 0 || s_radio.primary_channel != actual_primary ||
                s_radio.secondary_channel != actual_secondary) {
                s_radio.primary_channel = actual_primary;
                s_radio.secondary_channel = actual_secondary;
                if (++s_radio.channel_generation == 0U) s_radio.channel_generation = 1U;
            }
            for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
                wifi_radio_live_lease_t *live = &s_radio.leases[i];
                if (live->identity != 0U && live->fixed_channel &&
                    (live->primary_channel != actual_primary ||
                     live->secondary_channel != (uint8_t)actual_secondary))
                    live->channel_conflict = true;
            }
        }
    }
    *primary = s_radio.primary_channel;
    *secondary = s_radio.secondary_channel;
    *channel_generation = s_radio.channel_generation;
    err = s_radio.channel_observation_error;
    taskEXIT_CRITICAL(&s_radio.lock);
    return err;
}

static esp_err_t wifi_radio_get_channel_locked(
    uint8_t *primary, wifi_second_chan_t *secondary, uint32_t *generation)
{
    return wifi_radio_refresh_channel(primary, secondary, generation);
}

esp_err_t esp32_mquickjs_wifi_radio_lease_channel_status(
    const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_radio_channel_status_t *status)
{
    if (lease == NULL || status == NULL) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    esp_err_t err = ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_radio.lock);
    if (lease->acquired && lease->generation == s_radio.generation && s_radio.started) {
        for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
            wifi_radio_live_lease_t *live = &s_radio.leases[i];
            if (live->identity != 0U && live->identity == lease->identity && live->client == lease->client) {
                status->primary = s_radio.primary_channel;
                status->secondary = s_radio.secondary_channel;
                status->generation = s_radio.channel_generation;
                status->fixed = live->fixed_channel;
                status->conflicted = live->channel_conflict;
                err = s_radio.fault_stage != NULL ? s_radio.fault_error : s_radio.channel_observation_error;
                if (status->primary == 0U && err == ESP_OK) err = ESP_ERR_INVALID_STATE;
                break;
            }
        }
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    return err;
}

static void wifi_radio_snapshot(esp32_mquickjs_wifi_radio_status_t *out_status)
{
    taskENTER_CRITICAL(&s_radio.lock);
    out_status->driver_state = s_radio.driver_state;
    out_status->initialized = s_radio.storage_configured;
    out_status->driver_owned = s_radio.driver_owned;
    out_status->started = s_radio.started;
    out_status->starting = s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_STARTING;
    out_status->restart_required = s_radio.restart_required;
    out_status->fault_stage = s_radio.fault_stage;
    out_status->fault_error = s_radio.fault_error;
    out_status->cleanup_stage = s_radio.cleanup_stage;
    out_status->cleanup_error = s_radio.cleanup_error;
    out_status->generation = s_radio.generation;
    out_status->mode = s_radio.effective_mode;
    out_status->storage = s_radio.storage;
    out_status->requested_mode = wifi_radio_requested_mode();
    out_status->configuration = s_radio.configuration;
    out_status->activation = s_radio.activation;
    out_status->event_phase = s_radio.event_phase;
    out_status->event_identity = s_radio.event_identity;
    out_status->event_expected = s_radio.event_expected;
    out_status->event_seen = s_radio.event_seen & 7U;
    out_status->event_stopped = s_radio.event_phase == RADIO_EVENTS_RESTART ? (s_radio.event_seen >> 4) & 7U :
        (s_radio.event_phase == RADIO_EVENTS_STOP || s_radio.event_phase == RADIO_EVENTS_AP_STOP) ? s_radio.event_seen : 0U;
    out_status->event_live = s_radio.event_live;
    out_status->event_fence_pending = s_radio.event_phase != RADIO_EVENTS_IDLE &&
        !s_radio.event_fence_seen;
    memcpy(out_status->clients, s_radio.clients, sizeof(out_status->clients));
    out_status->primary_channel = s_radio.primary_channel;
    out_status->secondary_channel = s_radio.secondary_channel;
    out_status->channel_generation = s_radio.channel_generation;
    out_status->channel_observation_error = s_radio.channel_observation_error;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        if (s_radio.leases[i].identity != 0U && s_radio.leases[i].fixed_channel) {
            out_status->fixed_channel_owners++;
            if (s_radio.leases[i].channel_conflict) out_status->conflicted_channel_owners++;
        }
    }
    out_status->lifecycle_active = s_radio.lifecycle.identity != 0U;
    out_status->active_operations = s_radio.operation.identity != 0U ? 1U : 0U;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].raw_tx_identity != 0U) ++out_status->active_operations;
#endif
    out_status->wake_locks = s_radio.wake_locks;
    out_status->wake_lock_error = s_radio.wake_lock_error;
    out_status->fixed_channel_claimed = out_status->fixed_channel_owners > 0U;
    out_status->promiscuous_claimed = s_radio.promiscuous_claimed;
    for (unsigned i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].promiscuous_identity != 0) ++out_status->promiscuous_owners;
    out_status->promiscuous_identity_exhausted = s_radio.next_promiscuous_identity == 0;
    out_status->promiscuous_client = s_radio.promiscuous_client;
    taskEXIT_CRITICAL(&s_radio.lock);
}

static esp_err_t wifi_radio_get_status_locked(
    esp32_mquickjs_wifi_radio_status_t *out_status)
{
    if (out_status == NULL) return ESP_ERR_INVALID_ARG;
    memset(out_status, 0, sizeof(*out_status));
    if (s_radio.started && s_radio.fault_stage == NULL) {
        uint8_t primary;
        wifi_second_chan_t secondary;
        uint32_t generation;
        (void)wifi_radio_get_channel_locked(&primary, &secondary, &generation);
        if (esp_wifi_get_max_tx_power(&out_status->max_tx_power_quarter_dbm) == ESP_OK)
            out_status->max_tx_power_available = true;
        if (esp_wifi_get_ps(&out_status->power_save) == ESP_OK)
            out_status->power_save_available = true;
    }
    wifi_radio_snapshot(out_status);
    return ESP_OK;
}

static esp_err_t wifi_radio_set_channel_locked(
    esp32_mquickjs_wifi_radio_lease_t *lease,
    uint8_t primary,
    wifi_second_chan_t secondary)
{
    if (s_radio.lifecycle.identity != 0U) return ESP_ERR_INVALID_STATE;
    wifi_ap_record_t ap = {0};
    wifi_config_t ap_config = {0};
    wifi_mode_t mode = WIFI_MODE_NULL;
    wifi_radio_live_lease_t *owner = NULL;
    uint8_t actual_primary;
    wifi_second_chan_t actual_secondary;
    uint32_t actual_generation;
    esp_err_t err;

    if (!wifi_radio_lease_valid(lease) || primary == 0 ||
        (secondary != WIFI_SECOND_CHAN_NONE &&
         secondary != WIFI_SECOND_CHAN_ABOVE &&
         secondary != WIFI_SECOND_CHAN_BELOW)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_radio.fault_stage != NULL) return s_radio.fault_error;
    if (s_radio.operation.identity != 0U) return ESP_ERR_INVALID_STATE;
    err = wifi_radio_validate_regulatory_channel(primary);
    if (err != ESP_OK) return err;
    if (!s_radio.started) return ESP_ERR_INVALID_STATE;
    bool conflict = false;
    taskENTER_CRITICAL(&s_radio.lock);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        wifi_radio_live_lease_t *live = &s_radio.leases[i];
        if (live->identity == lease->identity) owner = live;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        if (live->raw_tx_identity != 0U) conflict = true;
#endif
        if (live->identity != 0U && live->identity != lease->identity &&
            live->fixed_channel && (live->primary_channel != primary ||
                live->secondary_channel != (uint8_t)secondary || live->channel_conflict))
            conflict = true;
    }
    conflict = conflict || owner == NULL || owner->channel_conflict;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (conflict) return ESP_ERR_INVALID_STATE;
    err = wifi_radio_get_channel_locked(&actual_primary, &actual_secondary, &actual_generation);
    if (err != ESP_OK) return err;
    taskENTER_CRITICAL(&s_radio.lock);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U && s_radio.leases[i].channel_conflict)
            conflict = true;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (conflict) return ESP_ERR_INVALID_STATE;
    err = esp_wifi_sta_get_ap_info(&ap);
    if (err == ESP_OK && (ap.primary != primary || actual_secondary != secondary)) {
        err = ESP_ERR_INVALID_STATE;
        return err;
    }
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
        return err;
    }
    err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) return err;
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        err = esp_wifi_get_config(WIFI_IF_AP, &ap_config);
        if (err != ESP_OK) return err;
        if ((ap_config.ap.channel != 0U && ap_config.ap.channel != primary) ||
            actual_primary != primary || actual_secondary != secondary) {
            err = ESP_ERR_INVALID_STATE;
            return err;
        }
    }
    taskENTER_CRITICAL(&s_radio.lock);
    uint64_t revision = s_radio.channel_observation_revision;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (actual_primary != primary || actual_secondary != secondary) {
        err = esp_wifi_set_channel(primary, secondary);
        if (err != ESP_OK) return err;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    if (revision != s_radio.channel_observation_revision &&
        (s_radio.primary_channel != primary || s_radio.secondary_channel != secondary ||
         s_radio.channel_observation_error != ESP_OK)) {
        taskEXIT_CRITICAL(&s_radio.lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (owner->channel_conflict) {
        taskEXIT_CRITICAL(&s_radio.lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_radio.channel_observation_revision++;
    s_radio.channel_observation_error = ESP_OK;
    owner->fixed_channel = true;
    owner->primary_channel = primary;
    owner->secondary_channel = (uint8_t)secondary;
    if (s_radio.primary_channel != primary || s_radio.secondary_channel != secondary) {
        s_radio.primary_channel = primary;
        s_radio.secondary_channel = secondary;
        if (++s_radio.channel_generation == 0U) s_radio.channel_generation = 1U;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

static void wifi_radio_release_channel_locked(
    esp32_mquickjs_wifi_radio_lease_t *lease)
{
    if (!wifi_radio_lease_valid(lease)) return;
    taskENTER_CRITICAL(&s_radio.lock);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        if (s_radio.leases[i].identity == lease->identity) {
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
            if (s_radio.leases[i].raw_tx_identity != 0U) break;
#endif
            s_radio.leases[i].fixed_channel = false;
            s_radio.leases[i].channel_conflict = false;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_radio.lock);
}

static wifi_radio_live_lease_t *wifi_radio_promiscuous_owner(uint32_t radio_identity)
{
    for (unsigned i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity == radio_identity && radio_identity != 0) return &s_radio.leases[i];
    return NULL;
}

static void wifi_radio_promiscuous_summary(void)
{
    s_radio.promiscuous_claimed = false;
    s_radio.promiscuous_lease_identity = 0;
    for (unsigned i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        if (s_radio.leases[i].promiscuous_identity == 0) continue;
        s_radio.promiscuous_claimed = true;
        s_radio.promiscuous_client = s_radio.leases[i].client;
        s_radio.promiscuous_lease_identity = s_radio.leases[i].identity;
        break;
    }
}

static void wifi_radio_promiscuous_clear_fault(void)
{
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_radio.cleanup_stage != NULL && strncmp(s_radio.cleanup_stage, "promiscuous-", 12) == 0) {
        s_radio.cleanup_stage = NULL;
        s_radio.cleanup_error = ESP_OK;
        if (s_radio.fault_stage != NULL && strncmp(s_radio.fault_stage, "promiscuous-", 12) == 0) {
            s_radio.fault_stage = NULL;
            s_radio.fault_error = ESP_OK;
            s_radio.driver_state = s_radio.started ? ESP32_MQUICKJS_WIFI_RADIO_STARTED : ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        }
    }
    taskEXIT_CRITICAL(&s_radio.lock);
}

static esp_err_t wifi_radio_promiscuous_demand(esp32_mquickjs_wifi_promiscuous_demand_t *demand)
{
    bool enabled = false, preserve_baseline = false;
    unsigned receivers = 0;
    esp32_mquickjs_wifi_promiscuous_token_t tokens[ESP32_MQUICKJS_WIFI_PROMISCUOUS_MAX_SUBSCRIBERS];
    for (unsigned i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (owner->promiscuous_identity == 0 || owner->promiscuous_closing) continue;
        enabled = true;
        if (owner->rx_token.identity != 0) {
            if (receivers == ESP32_MQUICKJS_WIFI_PROMISCUOUS_MAX_SUBSCRIBERS) return ESP_ERR_INVALID_STATE;
            tokens[receivers++] = owner->rx_token;
        }
        else preserve_baseline = true;
    }
    esp32_mquickjs_wifi_promiscuous_snapshot_t requirements;
    /* No unowned registry reservation may obtain SDK mutation rights. Control
     * transactions and the registry's lifecycle are serialized by this mutex. */
    if (!esp32_mquickjs_wifi_promiscuous_owned_requirements(tokens, receivers, &requirements))
        return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_promiscuous_driver_demand(&requirements, enabled, preserve_baseline, demand);
}

static esp_err_t wifi_radio_promiscuous_registry_error(esp32_mquickjs_wifi_promiscuous_result_t result)
{
    if (result == ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK) return ESP_OK;
    if (result == ESP32_MQUICKJS_WIFI_PROMISCUOUS_CAPACITY) return ESP_ERR_NO_MEM;
    if (result == ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_ARGUMENT) return ESP_ERR_INVALID_ARG;
    return ESP_ERR_INVALID_STATE;
}

static void wifi_radio_release_promiscuous_locked(esp32_mquickjs_wifi_radio_promiscuous_lease_t *lease);

static esp_err_t wifi_radio_acquire_promiscuous_locked(
    esp32_mquickjs_wifi_radio_lease_t *radio_lease,
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *out_lease,
    esp32_mquickjs_wifi_promiscuous_subscriber_t *subscriber,
    const esp32_mquickjs_wifi_rx_filter_t *filter,
    esp32_mquickjs_wifi_promiscuous_sink_t sink, void *context)
{
    if (!wifi_radio_lease_valid(radio_lease) || out_lease == NULL) return ESP_ERR_INVALID_ARG;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (WIFI_RADIO_SMARTCONFIG_PENDING || WIFI_RADIO_WPS_PENDING) return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
    if (WIFI_RADIO_DPP_PENDING) return ESP_ERR_INVALID_STATE;
#endif
    memset(out_lease, 0, sizeof(*out_lease));
    if ((subscriber == NULL && (filter != NULL || sink != NULL || context != NULL)) ||
        (subscriber != NULL && (!esp32_mquickjs_wifi_rx_filter_valid(filter) || sink == NULL)))
        return ESP_ERR_INVALID_ARG;
    if (s_radio.lifecycle.identity != 0U || !s_radio.started) return ESP_ERR_INVALID_STATE;
    if (s_radio.fault_stage != NULL) return s_radio.fault_error;
    wifi_radio_live_lease_t *owner = wifi_radio_promiscuous_owner(radio_lease->identity);
    if (owner == NULL || owner->promiscuous_identity != 0) return ESP_ERR_INVALID_STATE;
    if (s_radio.next_promiscuous_identity == 0) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_promiscuous_token_t rx_token = {0};
    if (subscriber != NULL) {
        esp_err_t err = wifi_radio_promiscuous_registry_error(esp32_mquickjs_wifi_promiscuous_reserve(
            subscriber, filter, sink, context, &rx_token));
        if (err != ESP_OK) return err;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    owner->rx_token = rx_token;
    owner->promiscuous_closing = false;
    owner->promiscuous_identity = s_radio.next_promiscuous_identity;
    s_radio.next_promiscuous_identity = s_radio.next_promiscuous_identity == UINT32_MAX ? 0 : s_radio.next_promiscuous_identity + 1U;
    wifi_radio_promiscuous_summary();
    taskEXIT_CRITICAL(&s_radio.lock);
    *out_lease = (esp32_mquickjs_wifi_radio_promiscuous_lease_t){
        .generation = radio_lease->generation, .radio_lease_identity = radio_lease->identity,
        .identity = owner->promiscuous_identity, .client = radio_lease->client,
        .acquired = true, .framework_enabled = true,
    };
    esp32_mquickjs_wifi_promiscuous_demand_t demand;
    esp_err_t err = wifi_radio_promiscuous_demand(&demand);
    esp32_mquickjs_wifi_promiscuous_driver_status_t status = {0};
    if (err == ESP_OK) {
        err = esp32_mquickjs_wifi_promiscuous_driver_apply(&demand);
        esp32_mquickjs_wifi_promiscuous_driver_status(&status);
    }
    if (err == ESP_OK && rx_token.identity != 0) {
        err = wifi_radio_promiscuous_registry_error(esp32_mquickjs_wifi_promiscuous_activate(&owner->rx_token));
        if (err != ESP_OK) {
            (void)wifi_radio_cleanup_fault("promiscuous-activate", err);
            wifi_radio_release_promiscuous_locked(out_lease);
        }
        return err;
    }
    if (err != ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        owner->promiscuous_closing = true;
        taskEXIT_CRITICAL(&s_radio.lock);
        if (rx_token.identity != 0 && esp32_mquickjs_wifi_promiscuous_begin_close(&owner->rx_token) !=
            ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK) {
            (void)wifi_radio_cleanup_fault("promiscuous-registry", ESP_ERR_INVALID_STATE);
            return err;
        }
        if (status.cleanup_pending) {
            (void)wifi_radio_record_fault(status.error_stage, status.error);
            (void)wifi_radio_cleanup_fault(status.cleanup_stage, status.cleanup_error);
            /* A failed open still owns its reserved control and Radio claim
             * until driver rollback and registry drain both finish. */
        } else {
            if (rx_token.identity != 0 && esp32_mquickjs_wifi_promiscuous_finish_close(&owner->rx_token) !=
                ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK) {
                (void)wifi_radio_cleanup_fault("promiscuous-registry", ESP_ERR_INVALID_STATE);
                return err;
            }
            taskENTER_CRITICAL(&s_radio.lock);
            owner->promiscuous_identity = 0;
            owner->promiscuous_closing = false;
            wifi_radio_promiscuous_summary();
            taskEXIT_CRITICAL(&s_radio.lock);
            memset(out_lease, 0, sizeof(*out_lease));
        }
    }
    return err;
}

static void wifi_radio_release_promiscuous_locked(
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *lease)
{
    if (lease == NULL || !lease->acquired) return;
    wifi_radio_live_lease_t *owner = wifi_radio_promiscuous_owner(lease->radio_lease_identity);
    if (lease->generation != s_radio.generation || owner == NULL ||
        owner->client != lease->client || lease->identity == 0 || owner->promiscuous_identity != lease->identity) {
        memset(lease, 0, sizeof(*lease));
        return;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    owner->promiscuous_closing = true;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (owner->rx_token.identity != 0 && esp32_mquickjs_wifi_promiscuous_begin_close(&owner->rx_token) !=
        ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK) {
        (void)wifi_radio_cleanup_fault("promiscuous-registry", ESP_ERR_INVALID_STATE);
        return;
    }
    esp_err_t err = esp32_mquickjs_wifi_promiscuous_driver_recover();
    if (err == ESP_OK) {
        esp32_mquickjs_wifi_promiscuous_demand_t demand;
        err = wifi_radio_promiscuous_demand(&demand);
        if (err != ESP_OK) {
            (void)wifi_radio_cleanup_fault("promiscuous-registry", err);
            return;
        }
        err = esp32_mquickjs_wifi_promiscuous_driver_apply(&demand);
    }
    if (err != ESP_OK) {
        esp32_mquickjs_wifi_promiscuous_driver_status_t status;
        esp32_mquickjs_wifi_promiscuous_driver_status(&status);
        if (status.error != ESP_OK && status.error_stage != NULL)
            (void)wifi_radio_record_fault(status.error_stage, status.error);
        (void)wifi_radio_cleanup_fault(status.cleanup_pending ? status.cleanup_stage :
            (status.error_stage != NULL ? status.error_stage : "promiscuous-stop"),
            status.cleanup_pending ? status.cleanup_error : err);
        return;
    }
    if (owner->rx_token.identity != 0) {
        esp32_mquickjs_wifi_promiscuous_result_t result = esp32_mquickjs_wifi_promiscuous_finish_close(&owner->rx_token);
        if (result == ESP32_MQUICKJS_WIFI_PROMISCUOUS_DRAINING) return;
        if (result != ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK) {
            (void)wifi_radio_cleanup_fault("promiscuous-registry", ESP_ERR_INVALID_STATE);
            return;
        }
    }
    taskENTER_CRITICAL(&s_radio.lock);
    owner->promiscuous_identity = 0;
    owner->promiscuous_closing = false;
    wifi_radio_promiscuous_summary();
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_promiscuous_clear_fault();
    memset(lease, 0, sizeof(*lease));
}

static void wifi_radio_release_locked(
    esp32_mquickjs_wifi_radio_lease_t *lease)
{
    if (!wifi_radio_lease_valid(lease)) {
        return;
    }
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
    if (lease->client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MESH &&
        !wifi_radio_mesh_release_owner_locked(lease)) return;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
    if (s_dpp_radio) for (unsigned i = 0; i < 3; ++i)
        if (s_dpp_radio->owners[i] == lease->identity) return;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (s_sc_radio) for (unsigned i = 0; i < 3; ++i)
        if (s_sc_radio->owners[i] == lease->identity) return;
    if (s_wps_radio) for (unsigned i = 0; i < 3; ++i)
        if (s_wps_radio->owners[i] == lease->identity) return;
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
    if (s_wps_ap_radio) for (unsigned i = 0; i < 3; ++i)
        if (s_wps_ap_radio->owners[i] == lease->identity) return;
#endif
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (WIFI_RADIO_EAP_PENDING && s_eap_radio.generation == lease->generation)
        for (unsigned i = 0; i < 3; ++i) if (s_eap_radio.owners[i] == lease->identity) return;
#endif
    if (s_radio.operation.identity != 0U &&
        s_radio.operation.lease_identity == lease->identity) return;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (wifi_radio_twt_individual_lease_retained(lease) || wifi_radio_twt_broadcast_lease_retained(lease)) return;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI || CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
    if (s_interval.owner.owner_identity == lease->identity && s_interval.owner.generation == lease->generation) return;
#endif
    wifi_radio_live_lease_t *owner = wifi_radio_promiscuous_owner(lease->identity);
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (owner != NULL && owner->raw_tx_identity != 0U) return;
    if (s_tx_rate_lease.identity == lease->identity &&
        s_tx_rate_lease.generation == lease->generation) return;
#endif
    if (owner != NULL && owner->promiscuous_identity != 0) {
        esp32_mquickjs_wifi_radio_promiscuous_lease_t promiscuous = {
            .generation = lease->generation,
            .identity = owner->promiscuous_identity,
            .radio_lease_identity = lease->identity,
            .client = lease->client,
            .acquired = true,
            .framework_enabled = true,
        };
        wifi_radio_release_promiscuous_locked(&promiscuous);
        if (promiscuous.acquired) return;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        if (s_radio.leases[i].identity == lease->identity) {
            memset(&s_radio.leases[i], 0, sizeof(s_radio.leases[i]));
            s_radio.clients[lease->client]--;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    memset(lease, 0, sizeof(*lease));
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Observation failure never prevents STOP. Capture once per STOP event
 * identity, before its first SDK submission; a retry must not replace the
 * predecessor with failed/stopped SDK output. This is historical evidence,
 * not yet admission for replay across later stopped-state configuration. */
static void wifi_radio_capture_stop_snapshot_locked(void)
{
    esp32_mquickjs_wifi_radio_stop_snapshot_t snapshot = {
        .generation = s_radio.generation, .stop_identity = s_radio.event_identity,
        .mode = (uint8_t)s_radio.effective_mode,
        .error = ESP_ERR_INVALID_STATE, .step = ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_ADMISSION,
    };
    if (!s_radio.started || !s_radio.driver_owned || !s_radio.storage_configured ||
        s_radio.fault_stage != NULL || s_radio.restart_required ||
        (s_radio.effective_mode != WIFI_MODE_STA && s_radio.effective_mode != WIFI_MODE_AP &&
         s_radio.effective_mode != WIFI_MODE_APSTA)) goto done;
    if (s_radio.effective_mode & WIFI_MODE_STA)
        (void)wifi_radio_scan_parameters_observe_locked(NULL);
    int8_t power;
    wifi_band_mode_t band_mode, verified_mode;
    wifi_band_t band, verified_band;
    uint8_t primary, home;
    uint16_t inactive_time[2] = {0};
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE, home_secondary = WIFI_SECOND_CHAN_NONE;
    snapshot.step = ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_POWER;
    snapshot.error = esp_wifi_get_max_tx_power(&power);
    if (snapshot.error == ESP_OK && (power < 8 || power > 80)) snapshot.error = ESP_ERR_INVALID_RESPONSE;
    if (snapshot.error != ESP_OK) goto done;
    snapshot.step = ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_BAND;
    snapshot.error = wifi_radio_band_snapshot(&band_mode, &band);
    if (snapshot.error != ESP_OK) goto done;
    snapshot.step = ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_CHANNEL;
    snapshot.error = esp_wifi_get_channel(&primary, &secondary);
    if (snapshot.error != ESP_OK) goto done;
    snapshot.step = ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_HOME;
    snapshot.error = esp_wifi_get_home_channel(&home, &home_secondary);
    if (snapshot.error != ESP_OK) goto done;
    for (unsigned i = 0; i < 2; ++i) {
        if (!(snapshot.mode & (1U << i))) continue;
        snapshot.step = i == 0 ? ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_STA_INACTIVE
                              : ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_AP_INACTIVE;
        snapshot.error = esp_wifi_get_inactive_time(i == 0 ? WIFI_IF_STA : WIFI_IF_AP, &inactive_time[i]);
        if (snapshot.error == ESP_OK && inactive_time[i] < (i == 0 ? 3 : 10))
            snapshot.error = ESP_ERR_INVALID_RESPONSE;
        wifi_radio_inactive_history_record(i, inactive_time[i], snapshot.error);
        if (snapshot.error != ESP_OK) goto done;
    }
    snapshot.step = ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_VERIFY;
    snapshot.error = wifi_radio_band_snapshot(&verified_mode, &verified_band);
    if (snapshot.error != ESP_OK) goto done;
    if (verified_mode != band_mode || verified_band != band || primary != home || secondary != home_secondary ||
        (secondary != WIFI_SECOND_CHAN_NONE && secondary != WIFI_SECOND_CHAN_ABOVE && secondary != WIFI_SECOND_CHAN_BELOW) ||
        (band == WIFI_BAND_2G ? primary < 1 || primary > 14 : esp32_mquickjs_wifi_radio_5ghz_channel_bit(primary) == 0U)) {
        snapshot.error = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    snapshot.tx_power = power;
    snapshot.band_mode = (uint8_t)band_mode;
    snapshot.band = (uint8_t)band;
    snapshot.primary = primary;
    snapshot.secondary = (uint8_t)secondary;
    memcpy(snapshot.inactive_time, inactive_time, sizeof(inactive_time));
    snapshot.step = ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_COMPLETE;
    /* Candidate until STOP/fence completes. The getter also checks live state;
     * a later mutation clears this bit even while STOP cleanup is pending. */
    snapshot.unchanged = true;
done:
    /* Partial SDK outputs remain local. Only a complete observation supplies
     * payload; generation/event/error survive even on failed capture. */
    s_stop_snapshot = snapshot;
}

static bool wifi_radio_stop_snapshot_unchanged_locked(void)
{
    return s_stop_snapshot.unchanged && s_stop_snapshot.stop_identity != 0U &&
        s_stop_snapshot.stop_identity == s_radio.event_identity &&
        s_stop_snapshot.generation == s_radio.generation &&
        s_stop_snapshot.error == ESP_OK && s_stop_snapshot.step == ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_COMPLETE &&
        s_radio.driver_owned && s_radio.storage_configured && !s_radio.started && !s_radio.stop_required &&
        s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
        s_radio.fault_stage == NULL && s_radio.cleanup_stage == NULL && !s_radio.restart_required;
}

/* STOP removed the power/current/home getters. Use only the same physical
 * generation's unmodified predecessor, while verifying the SDK band policy
 * that remains readable after STOP. No driver mutation or guessed default. */
static esp_err_t wifi_radio_restart_stopped_observations_locked(
    wifi_radio_restart_configs_t *snapshot, bool verify)
{
    if (!wifi_radio_stop_snapshot_unchanged_locked() ||
        s_stop_snapshot.mode != (uint8_t)s_radio.effective_mode) return ESP_ERR_INVALID_STATE;
    wifi_band_mode_t mode;
    esp_err_t err = esp_wifi_get_band_mode(&mode);
    if (err != ESP_OK) return err;
    if (mode != (wifi_band_mode_t)s_stop_snapshot.band_mode) return ESP_ERR_INVALID_RESPONSE;
    if (verify)
        return snapshot->tx_power == s_stop_snapshot.tx_power && snapshot->band_mode == mode &&
            snapshot->band == (wifi_band_t)s_stop_snapshot.band && snapshot->primary == s_stop_snapshot.primary &&
            snapshot->secondary == (wifi_second_chan_t)s_stop_snapshot.secondary &&
            snapshot->inactive_mask == s_stop_snapshot.mode &&
            (!(snapshot->inactive_mask & WIFI_MODE_STA) || snapshot->inactive_time[0] == s_stop_snapshot.inactive_time[0]) &&
            (!(snapshot->inactive_mask & WIFI_MODE_AP) || snapshot->inactive_time[1] == s_stop_snapshot.inactive_time[1])
            ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    snapshot->tx_power = s_stop_snapshot.tx_power;
    snapshot->band_mode = mode;
    snapshot->band = (wifi_band_t)s_stop_snapshot.band;
    snapshot->primary = s_stop_snapshot.primary;
    snapshot->secondary = (wifi_second_chan_t)s_stop_snapshot.secondary;
    snapshot->inactive_mask = s_stop_snapshot.mode;
    memcpy(snapshot->inactive_time, s_stop_snapshot.inactive_time, sizeof(snapshot->inactive_time));
    snapshot->visible_bands = mode == WIFI_BAND_MODE_2G_ONLY ? 1U : mode == WIFI_BAND_MODE_5G_ONLY ? 2U : 3U;
    return ESP_OK;
}
#endif

/* An explicitly admitted recovery can retain its exact native owner across
 * physical STOP. The other exception is an exact temporary-rate owner restoring
 * pre-start settings. NULL requires zero owners; neither path drops a token to
 * satisfy admission or permits unrelated owners across SDK STOP. */
static esp_err_t wifi_radio_stop_owners_locked(const esp32_mquickjs_wifi_radio_lease_t *allowed,
    bool twt_recovery, uint32_t dpp_identity)
{
    bool dpp_restore = false;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
    if (dpp_identity) {
        dpp_restore = wifi_radio_dpp_stop_owner_locked(dpp_identity);
        if (!dpp_restore || allowed || twt_recovery) return ESP_ERR_INVALID_STATE;
    }
#else
    if (dpp_identity) return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (WIFI_RADIO_EAP_PENDING) return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (twt_recovery && (allowed != NULL || !wifi_radio_twt_recovery_exact_locked(&s_radio.lifecycle) ||
        !s_twt_recovery.prepared || !wifi_radio_twt_recovery_owners_locked())) return ESP_ERR_INVALID_STATE;
#else
    if (twt_recovery) return ESP_ERR_INVALID_STATE;
#endif
    uint32_t identity = 0;
    if (allowed != NULL) {
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        if (!wifi_radio_lease_valid(allowed)) return ESP_ERR_INVALID_STATE;
        if (allowed->client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX) {
            if (!wifi_radio_raw_tx_recovery_owner_locked(allowed) &&
                (s_tx_rate_lease.identity != allowed->identity || s_tx_rate_lease.generation != allowed->generation ||
                 s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U)) return ESP_ERR_INVALID_STATE;
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
        } else if (allowed->client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MESH) {
            if (!wifi_radio_mesh_stop_owner_locked(allowed)) return ESP_ERR_INVALID_STATE;
#endif
        } else if (allowed->client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION) {
            esp32_mquickjs_wifi_action_token_t operation = {s_action.lane.generation, s_action.lane.identity};
            if (!wifi_radio_action_recovery_exact_locked(&s_radio.lifecycle) ||
                allowed->identity != s_action.lease.identity || !wifi_radio_action_exact_locked(&operation) ||
                s_action.lane.dispatching || s_action.lane.cancel_busy) return ESP_ERR_INVALID_STATE;
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
        } else if (allowed->client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_FTM) {
            if (!wifi_radio_ftm_recovery_exact_locked(&s_radio.lifecycle) ||
                allowed->identity != s_ftm.lease.identity || !wifi_radio_ftm_exact_locked(&s_ftm.state.token) ||
                !s_ftm.state.submitted || s_ftm.state.dispatching) return ESP_ERR_INVALID_STATE;
#endif
        } else return ESP_ERR_INVALID_STATE;
        wifi_radio_live_lease_t *owner = wifi_radio_promiscuous_owner(allowed->identity);
        if (owner == NULL || (owner->raw_tx_identity != 0U && !wifi_radio_raw_tx_recovery_owner_locked(allowed)))
            return ESP_ERR_INVALID_STATE;
        identity = allowed->identity;
#else
        return ESP_ERR_INVALID_STATE;
#endif
    }
    if (s_radio.wake_locks != 0) return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (!twt_recovery && !dpp_restore && s_radio.leases[i].identity != 0U && s_radio.leases[i].identity != identity) return ESP_ERR_INVALID_STATE;
    if (s_radio.promiscuous_claimed) return ESP_ERR_INVALID_STATE;
    if (s_radio.restart_required) return s_radio.fault_error;
    if (!s_radio.stop_required) {
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
        if (twt_recovery && !s_twt_recovery.stopped) return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        if (wifi_radio_raw_tx_recovery_exact_locked(&s_radio.lifecycle) && !s_raw_tx_recovery.stopped)
            return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
        if (wifi_radio_ftm_recovery_exact_locked(&s_radio.lifecycle) && !s_ftm.recovery_stopped)
            return ESP_ERR_INVALID_STATE;
#endif
        return ESP_OK;
    }
    wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STOPPING);
    esp_err_t err;
    if (s_radio.event_phase != RADIO_EVENTS_STOP) {
        taskENTER_CRITICAL(&s_radio.lock);
        /* An interface may already have delivered STOP during a failed AP
         * transition. Wait only for still-live interfaces, then require the
         * complete live mask to be empty at the queue fence. A configured mode
         * can include an already retired interface and would wait for a second
         * STOP that the SDK will not emit. */
        wifi_mode_t expected = s_radio.event_live;
        taskEXIT_CRITICAL(&s_radio.lock);
        err = wifi_radio_begin_events(RADIO_EVENTS_STOP, expected);
        if (err != ESP_OK) return wifi_radio_cleanup_fault("stop-events", err);
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        wifi_radio_capture_stop_snapshot_locked();
#endif
    }
    if (!s_radio.stop_submitted) {
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
        err = wifi_radio_retire_he_statistics_locked();
        if (err != ESP_OK) return err;
#endif
        err = esp_wifi_stop();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
            return wifi_radio_cleanup_fault("stop", err);
        s_radio.stop_submitted = true;
    }
    /* An accepted stop is never repeated merely because its event/fence is
     * delayed or the event queue is full. Retry only this retained suffix.
     * DPP restoration runs on a worker: it must not enter the active JS
     * runtime's native-wait scope or call its cooperation hook off-task. */
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
    err = dpp_restore ? wifi_radio_wait_events_inner(NULL) : wifi_radio_wait_events();
#else
    err = wifi_radio_wait_events();
#endif
    if (err != ESP_OK) return wifi_radio_cleanup_fault("stop-events", err);
#if ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE
    if (s_radio.ap_reopen_config_pending) {
        err = esp32_mquickjs_wifi_ap_prestart_release(s_radio.lifecycle.generation, s_radio.lifecycle.identity);
        if (err != ESP_OK) return wifi_radio_cleanup_fault("ap-prestart-release", err);
        s_radio.ap_reopen_config_pending = false;
    }
#endif
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.started = false;
    s_radio.stop_required = false;
    s_radio.stop_submitted = false;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (wifi_radio_raw_tx_recovery_exact_locked(&s_radio.lifecycle)) s_raw_tx_recovery.stopped = true;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (wifi_radio_ftm_recovery_exact_locked(&s_radio.lifecycle)) s_ftm.recovery_stopped = true;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (twt_recovery) s_twt_recovery.stopped = true;
#endif
    s_radio.ap_stop_phase = AP_STOP_IDLE;
    s_radio.ap_reopen_pending = s_radio.ap_reopen_attempted = false;
    s_radio.ap_reopen_quiesced = s_radio.ap_reopen_restore_complete = false;
    s_radio.ap_transition_application = s_radio.ap_transition_station = s_radio.ap_transition_access_point = 0;
    s_radio.ap_stop_error = ESP_OK;
    if (s_radio.cleanup_stage != NULL &&
        (strcmp(s_radio.cleanup_stage, "stop") == 0 || strcmp(s_radio.cleanup_stage, "stop-events") == 0 ||
         strcmp(s_radio.cleanup_stage, "he-statistics-retire") == 0)) {
        s_radio.cleanup_stage = NULL;
        s_radio.cleanup_error = ESP_OK;
        if (s_radio.fault_stage != NULL &&
            (strcmp(s_radio.fault_stage, "stop") == 0 || strcmp(s_radio.fault_stage, "stop-events") == 0 ||
             strcmp(s_radio.fault_stage, "he-statistics-retire") == 0)) {
            s_radio.fault_stage = NULL;
            s_radio.fault_error = ESP_OK;
        }
    }
    /* A failed init/start needs shutdown; stop alone does not rebuild storage. */
    s_radio.driver_state = s_radio.fault_stage == NULL
        ? ESP32_MQUICKJS_WIFI_RADIO_STOPPED : ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

static esp_err_t wifi_radio_stop_lease_locked(const esp32_mquickjs_wifi_radio_lease_t *allowed, bool twt_recovery)
{
    return wifi_radio_stop_owners_locked(allowed, twt_recovery, 0);
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
void esp32_mquickjs_wifi_radio_stop_snapshot(esp32_mquickjs_wifi_radio_stop_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    wifi_radio_operation_lock();
    *snapshot = s_stop_snapshot;
    snapshot->unchanged = wifi_radio_stop_snapshot_unchanged_locked();
    wifi_radio_operation_unlock();
}
#endif

static esp_err_t wifi_radio_stop_locked(void)
{
    return wifi_radio_stop_lease_locked(NULL, false);
}

esp_err_t esp32_mquickjs_wifi_radio_release_and_stop_idle(
    esp32_mquickjs_wifi_radio_lease_t *lease)
{
    if (lease == NULL) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_OK;
    if (lease->acquired) {
        if (!wifi_radio_lease_valid(lease)) { err = ESP_ERR_INVALID_STATE; goto done; }
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        if (s_tx_rate_lease.identity == lease->identity && s_tx_rate_lease.generation == lease->generation) {
            err = wifi_radio_restore_tx_rate_locked(lease);
            if (err != ESP_OK) goto done;
        }
#endif
        wifi_radio_release_locked(lease);
        if (lease->acquired) {
            err = s_radio.fault_stage != NULL ? s_radio.fault_error : ESP_ERR_INVALID_STATE;
            goto done;
        }
    }
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    if (s_radio.lifecycle.identity == 0U) err = wifi_radio_stop_locked();
done:
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_shutdown_lease_locked(const esp32_mquickjs_wifi_radio_lease_t *allowed, bool twt_recovery)
{
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_WAPI_PSK
    esp32_mquickjs_wifi_wapi_status_t wapi;
    esp32_mquickjs_wifi_wapi_sdk_status(&wapi);
    if (wapi.uncertain) return wifi_radio_cleanup_fault("wapi-deinit", wapi.cleanup_error ?
        wapi.cleanup_error : wapi.error ? wapi.error : ESP_ERR_INVALID_STATE);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    /* A parked Vendor IE still owns driver copies through its exact token. */
    if (s_vendor_ie.start_owner.identity != 0U) return ESP_ERR_INVALID_STATE;
#endif
    esp_err_t err = wifi_radio_stop_lease_locked(allowed, twt_recovery);
    if (err != ESP_OK) return err;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    /* TWT retirement still uses the initialized native ioctl queue. Keep both
     * driver and callback routes until the original owners have drained. */
    if (twt_recovery && (s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT] != 0U ||
        s_radio.operation.identity != 0U || esp32_mquickjs_wifi_twt_information_pending()))
        return wifi_radio_cleanup_fault("twt-native-drain", ESP_ERR_TIMEOUT);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (wifi_radio_ftm_recovery_exact_locked(&s_radio.lifecycle) && s_radio.driver_owned) {
        err = wifi_radio_ftm_recovery_timer_locked();
        if (err != ESP_OK) return wifi_radio_cleanup_fault(s_ftm.recovery_timer_ready ? "ftm-sdk-drain" : "ftm-timer-drain", err);
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    err = esp32_mquickjs_wifi_vendor_ie_broker_unregister(s_radio.generation);
    if (err != ESP_OK) {
        esp32_mquickjs_wifi_vendor_ie_broker_status_t vendor;
        esp32_mquickjs_wifi_vendor_ie_broker_status(&vendor);
        return wifi_radio_cleanup_fault(vendor.unregister_written ? "vendor-ie-drain" : "vendor-ie-unregister", err);
    }
    esp32_mquickjs_wifi_raw_tx_broker_status_t raw_tx;
    esp32_mquickjs_wifi_raw_tx_broker_status(&raw_tx);
    if (raw_tx.generation != 0U) {
        if (raw_tx.generation != s_radio.generation)
            return wifi_radio_cleanup_fault("raw-tx-generation", ESP_ERR_INVALID_STATE);
        bool recovery = wifi_radio_raw_tx_recovery_exact_locked(&s_radio.lifecycle);
        err = recovery ? esp32_mquickjs_wifi_raw_tx_broker_quiesce(raw_tx.generation, &s_raw_tx_recovery.operation)
            : esp32_mquickjs_wifi_raw_tx_broker_unregister(raw_tx.generation);
        if (err != ESP_OK) return wifi_radio_cleanup_fault("raw-tx-unregister", err);
        if (recovery && !s_raw_tx_recovery.sdk_fenced) {
            /* Unregister only clears a function pointer. Drain SDK task work
             * that may already have loaded it before physical deinit. */
            err = esp32_mquickjs_wifi_action_sdk_fence();
            if (err != ESP_OK) return wifi_radio_cleanup_fault("raw-tx-sdk-drain", err);
            s_raw_tx_recovery.sdk_fenced = true;
        }
    }
#endif
    if (s_channel_event_instance != NULL) {
        err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_channel_event_instance);
        if (err != ESP_OK) return wifi_radio_cleanup_fault("channel-unregister", err);
        s_channel_event_instance = NULL;
    }
    while (atomic_load_explicit(&s_channel_callbacks, memory_order_acquire) != 0U) {
        if (esp32_mquickjs_wifi_wait_remaining(portMAX_DELAY) == 0)
            return wifi_radio_cleanup_fault("channel-drain", ESP_ERR_TIMEOUT);
        vTaskDelay(1);
    }
    if (s_radio.driver_owned) {
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
        /* Covers initialized-but-stopped cleanup too; never destroy the native
         * task while it still owns statistics allocations. */
        err = wifi_radio_retire_he_statistics_locked();
        if (err != ESP_OK) return err;
#endif
        wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING);
        err = esp_wifi_deinit();
        if (err != ESP_OK) return wifi_radio_cleanup_fault("deinit", err);
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_WAPI_PSK
        err = esp32_mquickjs_wifi_wapi_sdk_cleanup_error();
        if (err != ESP_OK) {
            /* Physical deinit returned; do not repeat it because the SDK
             * discarded a WAPI cleanup error. Keep the fault diagnosable. */
            s_radio.driver_owned = false;
            return wifi_radio_cleanup_fault("wapi-deinit", err);
        }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI || CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
        /* Normal shutdown requires zero Radio owners, hence no interval token.
         * Only successful physical deinit invalidates accepted SDK knowledge. */
        (void)esp32_mquickjs_wifi_interval_invalidate(&s_interval, s_radio.generation);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        /* Unregistration and callback retirement preceded physical deinit.
         * Only this successful physical boundary grants termination. Leave the
         * operation token and generation intact until its native owner retires. */
        if (wifi_radio_action_recovery_exact_locked(&s_radio.lifecycle) && s_action.lane.identity != 0U) {
            esp32_mquickjs_wifi_action_token_t operation = {s_action.lane.generation, s_action.lane.identity};
            taskENTER_CRITICAL(&s_radio.lock);
            (void)esp32_mquickjs_wifi_action_terminated(&s_action.lane, &operation);
            taskEXIT_CRITICAL(&s_radio.lock);
        }
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
        if (wifi_radio_ftm_recovery_exact_locked(&s_radio.lifecycle) && s_ftm.lease.acquired) {
            /* STOP freed SDK report/context, TASK timers and channel callbacks
             * drained before this deinit. Keep original attribution errors;
             * physical retirement does not fabricate a ranging completion. */
            taskENTER_CRITICAL(&s_radio.lock);
            s_ftm.state.physical_termination = true;
            s_ftm.state.report_consumed = s_ftm.state.report_discarded = true;
            s_ftm.state.copied_entries = 0;
            s_ftm.state.sdk_fenced = s_ftm.state.event_fenced = true;
            taskEXIT_CRITICAL(&s_radio.lock);
        }
#endif
        (void)esp32_mquickjs_wifi_vendor_ie_broker_reset(s_radio.generation);
        if (wifi_radio_raw_tx_recovery_exact_locked(&s_radio.lifecycle) &&
            s_tx_rate_lease.identity == s_raw_tx_recovery.operation.radio_lease_identity &&
            s_tx_rate_lease.generation == s_raw_tx_recovery.operation.generation)
            memset(&s_tx_rate_lease, 0, sizeof(s_tx_rate_lease));
        esp32_mquickjs_wifi_tx_rate_invalidate(&s_tx_rates);
        esp32_mquickjs_wifi_policy_invalidate(&s_policies);
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        esp32_mquickjs_wifi_ftm_offset_invalidate(&s_ftm_offset);
#endif
        memset(&s_inactive_history, 0, sizeof(s_inactive_history));
        memset(&s_scan_parameters, 0, sizeof(s_scan_parameters));
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
        memset(&s_he_statistics, 0, sizeof(s_he_statistics));
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
        memset(&s_twt_policy, 0, sizeof(s_twt_policy));
#endif
#endif
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.driver_owned = false;
        s_radio.storage_configured = false;
        s_radio.effective_mode = WIFI_MODE_NULL;
        taskEXIT_CRITICAL(&s_radio.lock);
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    /* Successful physical deinit is recorded above before this suffix. A
     * callback still draining must not cause a second deinit on retry. */
    if (raw_tx.generation != 0U &&
        !esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(raw_tx.generation))
        return wifi_radio_cleanup_fault("raw-tx-drain", ESP_ERR_TIMEOUT);
    if (wifi_radio_raw_tx_recovery_exact_locked(&s_radio.lifecycle) &&
        wifi_radio_promiscuous_owner(s_raw_tx_recovery.operation.radio_lease_identity) != NULL)
        return wifi_radio_cleanup_fault("raw-tx-retire", ESP_ERR_TIMEOUT);
    if (wifi_radio_action_recovery_exact_locked(&s_radio.lifecycle) && s_action.lease.acquired)
        return wifi_radio_cleanup_fault("action-retire", ESP_ERR_TIMEOUT);
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (wifi_radio_ftm_recovery_exact_locked(&s_radio.lifecycle) && s_ftm.lease.acquired)
        return wifi_radio_cleanup_fault("ftm-retire", ESP_ERR_TIMEOUT);
#endif
#endif
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED) {
        /* Identity is boot-scoped. Never recycle it, even after driver reset. */
        if (s_radio.generation == UINT32_MAX) {
            s_radio.restart_required = true;
            s_radio.fault_stage = "generation-exhausted";
            s_radio.fault_error = ESP_ERR_INVALID_STATE;
            s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_FAULTED;
            taskEXIT_CRITICAL(&s_radio.lock);
            return ESP_ERR_INVALID_STATE;
        }
        s_radio.generation++;
    }
    s_radio.channel_observation_revision++;
    s_radio.channel_observation_error = ESP_OK;
    s_radio.primary_channel = 0U;
    s_radio.secondary_channel = WIFI_SECOND_CHAN_NONE;
    s_radio.fault_stage = NULL;
    s_radio.fault_error = ESP_OK;
    s_radio.cleanup_stage = NULL;
    s_radio.cleanup_error = ESP_OK;
    s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

static esp_err_t wifi_radio_shutdown_locked(void)
{
    return wifi_radio_shutdown_lease_locked(NULL, false);
}

esp_err_t esp32_mquickjs_wifi_radio_stop(void)
{
    wifi_radio_operation_lock();
    esp_err_t err = s_radio.lifecycle.identity != 0U ? ESP_ERR_INVALID_STATE : wifi_radio_stop_locked();
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_shutdown(void)
{
    wifi_radio_operation_lock();
    esp_err_t err = s_radio.lifecycle.identity != 0U ? ESP_ERR_INVALID_STATE : wifi_radio_shutdown_locked();
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_begin_lifecycle_with_dependents_locked(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_radio_lease_t *dependent_station,
    const esp32_mquickjs_wifi_radio_lease_t *dependent_ap,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, uint64_t eap_identity)
{
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (WIFI_RADIO_EAP_PENDING) {
        if (!eap_identity || eap_identity != s_eap_radio.identity ||
            s_eap_radio.generation != s_radio.generation || dependent_station || dependent_ap)
            return ESP_ERR_INVALID_STATE;
        const esp32_mquickjs_wifi_radio_lease_t *pinned[3] = {application, station, access_point};
        for (unsigned i = 0; i < 3; ++i)
            if ((pinned[i] && pinned[i]->acquired ? pinned[i]->identity : 0) != s_eap_radio.owners[i])
                return ESP_ERR_INVALID_STATE;
    } else if (eap_identity) return ESP_ERR_INVALID_STATE;
#else
    if (eap_identity) return ESP_ERR_NOT_SUPPORTED;
#endif
    if (token == NULL || token->identity != 0U) return ESP_ERR_INVALID_ARG;
    if (s_radio.wake_locks != 0 || s_radio.lifecycle.identity != 0U ||
        s_radio.operation.identity != 0U || s_radio.promiscuous_claimed) return ESP_ERR_INVALID_STATE;
    const esp32_mquickjs_wifi_radio_lease_t *owners[5] = {application, station, access_point, dependent_station, dependent_ap};
    const esp32_mquickjs_wifi_radio_client_t clients[5] = {
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE, ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE,
#else
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT, ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT,
#endif
    };
    uint32_t identities[5] = {0};
    for (size_t i = 0; i < 5; ++i) {
        if (owners[i] == NULL || !owners[i]->acquired) continue;
        if (!wifi_radio_lease_valid(owners[i]) || owners[i]->client != clients[i])
            return ESP_ERR_INVALID_STATE;
        identities[i] = owners[i]->identity;
    }
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U && s_radio.leases[i].identity != identities[0] &&
            s_radio.leases[i].identity != identities[1] && s_radio.leases[i].identity != identities[2] && s_radio.leases[i].identity != identities[3] &&
            s_radio.leases[i].identity != identities[4])
            return ESP_ERR_INVALID_STATE;
    if (s_radio.next_lifecycle_identity == 0U) return ESP_ERR_NO_MEM;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.lifecycle = (esp32_mquickjs_wifi_radio_lifecycle_t){s_radio.generation, s_radio.next_lifecycle_identity++};
    *token = s_radio.lifecycle;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

static esp_err_t wifi_radio_begin_lifecycle_locked(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return wifi_radio_begin_lifecycle_with_dependents_locked(application, station, access_point, NULL, NULL, token, 0);
}

esp_err_t esp32_mquickjs_wifi_radio_begin_lifecycle(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_begin_lifecycle_locked(application, station, access_point, token);
    wifi_radio_operation_unlock();
    return err;
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Resolve the saved mode and reserve exclusive ownership under one mutex.
 * A prior status read cannot authorize restart: an intervening owner or writer
 * must be observed before admission. This does not capture or mutate the SDK. */
esp_err_t esp32_mquickjs_wifi_radio_begin_stopped_restart(
    esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_radio_restart_selection_t *selection)
{
    if (token == NULL || token->identity != 0U || token->generation != 0U || selection == NULL)
        return ESP_ERR_INVALID_ARG;
    bool allow_ap_restart = selection->allow_ap_restart;
    *selection = (esp32_mquickjs_wifi_radio_restart_selection_t){.allow_ap_restart = allow_ap_restart};
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    bool cold = s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED &&
        !s_radio.driver_owned && !s_radio.storage_configured && s_radio.effective_mode == WIFI_MODE_NULL;
    if ((!cold && (!s_radio.driver_owned || !s_radio.storage_configured ||
            s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
            (s_radio.storage != WIFI_STORAGE_RAM && s_radio.storage != WIFI_STORAGE_FLASH))) || s_radio.started ||
        s_radio.stop_required || s_radio.stop_submitted || s_radio.restart_required ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL ||
        s_radio.event_phase != RADIO_EVENTS_IDLE || s_radio.event_live != 0U ||
        s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending ||
        s_interval.owner.identity != 0U || s_interval.restore_pending ||
        s_config_restart.snapshot != NULL || s_policy_restart.owner.identity != 0U ||
        s_vendor_ie.start_owner.identity != 0U ||
        WIFI_RADIO_SMARTCONFIG_PENDING || WIFI_RADIO_WPS_PENDING || WIFI_RADIO_DPP_PENDING ||
        WIFI_RADIO_EAP_PENDING || WIFI_RADIO_NAN_PENDING || WIFI_RADIO_MESH_PENDING)
        goto done;
    wifi_mode_t saved_mode = s_radio.effective_mode;
    if (saved_mode != WIFI_MODE_NULL && saved_mode != WIFI_MODE_STA && saved_mode != WIFI_MODE_AP && saved_mode != WIFI_MODE_APSTA)
        goto done;
    bool restore_off = !cold && saved_mode == WIFI_MODE_NULL;
    if (cold || restore_off) saved_mode = WIFI_MODE_STA;
    if (restore_off) {
        bool needs_ap = s_policies.records[ESP32_MQUICKJS_WIFI_POLICY_SLOT_AP_11B].configured;
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        needs_ap |= s_ftm_offset.configured;
#endif
        if (needs_ap) {
            if (!allow_ap_restart) goto done;
            saved_mode = WIFI_MODE_APSTA;
        }
    }
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (saved_mode & WIFI_MODE_AP) { err = ESP_ERR_NOT_SUPPORTED; goto done; }
#endif
    /* NULL owner inputs require every registry slot, including Wi-Fi's own
     * Application/STA/AP leases, to be vacant. Never release an owner's lease. */
    err = wifi_radio_begin_lifecycle_locked(NULL, NULL, NULL, token);
    if (err == ESP_OK) {
        selection->mode = saved_mode;
        selection->cold = cold;
        selection->restore_off = restore_off;
    }
done:
    wifi_radio_operation_unlock();
    return err;
}

/* Resolve start defaults and admission together. A matching running driver is
 * a no-op; changing running mode/storage never implicitly stops its owners. */
esp_err_t esp32_mquickjs_wifi_radio_begin_start_lifecycle(
    esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, bool *already_started)
{
    if (selection == NULL || token == NULL || token->identity != 0U || already_started == NULL)
        return ESP_ERR_INVALID_ARG;
    *already_started = false;
    esp32_mquickjs_wifi_radio_configuration_selection_t resolved = *selection;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_radio.lifecycle.identity != 0U || s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL ||
        s_radio.restart_required || (s_radio.stop_required && !s_radio.started) ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)) goto done;
    if (!resolved.mode_set) {
        resolved.mode = s_radio.storage_configured ? s_radio.effective_mode : WIFI_MODE_NULL;
        if (resolved.mode == WIFI_MODE_NULL) resolved.mode = WIFI_MODE_STA;
    }
    if (!resolved.storage_set)
        resolved.storage = s_radio.storage_configured ? s_radio.storage : WIFI_STORAGE_RAM;
    resolved.start = true;
    resolved.start_set = true;
    if ((resolved.mode != WIFI_MODE_STA && resolved.mode != WIFI_MODE_AP && resolved.mode != WIFI_MODE_APSTA) ||
        (resolved.storage != WIFI_STORAGE_RAM && resolved.storage != WIFI_STORAGE_FLASH) ||
        resolved.station_set || resolved.access_point_set || resolved.allow_disconnect || resolved.activate_ap) {
        err = ESP_ERR_INVALID_ARG;
        goto done;
    }
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (resolved.mode & WIFI_MODE_AP) { err = ESP_ERR_NOT_SUPPORTED; goto done; }
#endif
    if (s_radio.started) {
        if (resolved.mode != s_radio.effective_mode || resolved.storage != s_radio.storage)
            goto done;
        const esp32_mquickjs_wifi_radio_lease_t *owners[] = {application, station, access_point};
        const esp32_mquickjs_wifi_radio_client_t roles[] = {
            ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION, ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,
            ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,
        };
        for (size_t i = 0; i < 3; ++i)
            if (owners[i] != NULL && owners[i]->acquired &&
                (!wifi_radio_lease_valid(owners[i]) || owners[i]->client != roles[i])) goto done;
        if ((resolved.mode & WIFI_MODE_AP) &&
            (access_point == NULL || !access_point->acquired)) goto done;
        wifi_mode_t actual;
        err = esp_wifi_get_mode(&actual);
        if (err == ESP_OK && actual != resolved.mode) err = ESP_ERR_INVALID_STATE;
        if (err != ESP_OK) goto done;
        /* Anchor a Station-only driver started by another feature before
         * releasing the mutex. Otherwise its last owner could shut it down
         * between this resolution and helper allocation, changing storage. */
        if (resolved.mode == WIFI_MODE_STA && application != NULL && !application->acquired) {
            if (application->identity != 0U || application->generation != 0U ||
                s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION] != 0U) {
                err = ESP_ERR_INVALID_STATE;
                goto done;
            }
            err = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
                                           WIFI_MODE_STA, application);
            if (err != ESP_OK) goto done;
        }
        *already_started = true;
    } else {
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        err = wifi_radio_vendor_ie_begin_start_locked(application, station, access_point, &resolved, token);
#else
        err = wifi_radio_begin_lifecycle_locked(application, station, access_point, token);
#endif
        if (err != ESP_OK) goto done;
    }
    *selection = resolved;
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_begin_configuration_lifecycle(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    const esp32_mquickjs_wifi_radio_start_controls_t *start_controls,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (selection == NULL || token == NULL || token->identity != 0U)
        return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_radio_configuration_selection_t resolved = *selection;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    /* Fault recovery belongs to explicit cleanup, never implicit defaults. */
    if (s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        (s_radio.stop_required && !s_radio.started) ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)) goto done;
    if (!resolved.mode_set) {
        resolved.mode = s_radio.storage_configured ? s_radio.effective_mode : WIFI_MODE_NULL;
        if (resolved.mode == WIFI_MODE_NULL) {
            resolved.mode = (resolved.station_set ? WIFI_MODE_STA : 0) |
                (resolved.access_point_set ? WIFI_MODE_AP : 0);
            if (resolved.mode == WIFI_MODE_NULL) resolved.mode = WIFI_MODE_STA;
        }
    }
    if (!resolved.storage_set)
        resolved.storage = s_radio.storage_configured ? s_radio.storage : WIFI_STORAGE_RAM;
    if (!resolved.start_set) resolved.start = s_radio.storage_configured ? s_radio.started : true;
    err = ESP_ERR_INVALID_ARG;
    if (resolved.activate_ap) {
        /* Resolve under the same mutex as exact-owner admission. A status
         * sample before this lock must not decide which interface to remove.
         * This adapter never writes the saved Station configuration. */
        if (resolved.mode_set || resolved.storage_set || resolved.station_set ||
            !resolved.access_point_set || !resolved.allow_disconnect ||
            !resolved.start_set || !resolved.start || resolved.start_only ||
            controls != NULL || start_controls != NULL ||
            (resolved.mode != WIFI_MODE_STA && resolved.mode != WIFI_MODE_AP && resolved.mode != WIFI_MODE_APSTA))
            goto done;
        resolved.mode |= WIFI_MODE_AP;
    }
    if ((resolved.mode != WIFI_MODE_STA && resolved.mode != WIFI_MODE_AP && resolved.mode != WIFI_MODE_APSTA) ||
        (resolved.storage != WIFI_STORAGE_RAM && resolved.storage != WIFI_STORAGE_FLASH) ||
        (resolved.station_set && !(resolved.mode & WIFI_MODE_STA)) ||
        (resolved.access_point_set && !(resolved.mode & WIFI_MODE_AP)) ||
        (resolved.start && (resolved.mode & WIFI_MODE_AP) && !resolved.access_point_set)) goto done;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (resolved.mode & WIFI_MODE_AP) { err = ESP_ERR_NOT_SUPPORTED; goto done; }
#endif
    err = esp32_mquickjs_wifi_radio_validate_config_controls(resolved.mode, controls);
    if (err != ESP_OK) goto done;
    err = esp32_mquickjs_wifi_radio_validate_start_controls(resolved.start, start_controls);
    if (err != ESP_OK) goto done;
    /* A zero-client snapshot cannot exclude association before STOP. Require
     * explicit permission whenever the old running mode contains AP. The Radio
     * mutex serializes mode, admission and token publication, not peer traffic. */
    if (!resolved.allow_disconnect && s_radio.started && (s_radio.effective_mode & WIFI_MODE_AP)) {
        err = ESP_ERR_INVALID_STATE;
        goto done;
    }
    err = wifi_radio_begin_lifecycle_locked(application, station, access_point, token);
    if (err == ESP_OK) *selection = resolved;
done:
    wifi_radio_operation_unlock();
    return err;
}
#endif

/* Remove AP while retaining the same live Station/Application identities.
 * All callbacks/netifs stay attached until the AP_STOP event and marker finish. */
esp_err_t esp32_mquickjs_wifi_radio_begin_ap_stop(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (token == NULL || token->identity != 0U || station == NULL || access_point == NULL ||
        !station->acquired || !access_point->acquired) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.started || !s_radio.driver_owned || !s_radio.storage_configured ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED ||
        s_radio.effective_mode != WIFI_MODE_APSTA || s_radio.fault_stage != NULL ||
        s_radio.cleanup_stage != NULL || s_radio.restart_required || s_radio.ap_stop_phase != AP_STOP_IDLE)
        goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        if ((s_radio.leases[i].identity == station->identity &&
             s_radio.leases[i].required_mode != WIFI_MODE_STA) ||
            (s_radio.leases[i].identity == access_point->identity &&
             s_radio.leases[i].required_mode != WIFI_MODE_AP)) goto done;
    }
    err = wifi_radio_begin_lifecycle_locked(application, station, access_point, token);
    if (err != ESP_OK) goto done;
    s_radio.ap_transition_application = application != NULL && application->acquired ? application->identity : 0;
    s_radio.ap_transition_station = station->identity;
    s_radio.ap_transition_access_point = access_point->identity;
    s_radio.ap_stop_error = ESP_OK;
    s_radio.ap_stop_phase = AP_STOP_READY;
done:
    wifi_radio_operation_unlock();
    return err;
}

static bool wifi_radio_ap_transition_valid(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (token == NULL || token->identity == 0U || token->identity != s_radio.lifecycle.identity ||
        token->generation != s_radio.lifecycle.generation)
        return false;
    const uint32_t identities[] = {s_radio.ap_transition_application, s_radio.ap_transition_station, s_radio.ap_transition_access_point};
    const esp32_mquickjs_wifi_radio_client_t clients[] = {
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION, ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,
    };
    for (size_t i = 0; i < 3; ++i) {
        if (i == 0 && identities[i] == 0U) continue;
        esp32_mquickjs_wifi_radio_lease_t owner = {token->generation, identities[i], clients[i], true};
        if (!wifi_radio_lease_valid(&owner)) return false;
    }
    return true;
}

static bool wifi_radio_ap_stop_valid(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return s_radio.ap_stop_phase != AP_STOP_IDLE && wifi_radio_ap_transition_valid(token);
}

static bool wifi_radio_interfaces_live(wifi_mode_t mode)
{
    taskENTER_CRITICAL(&s_radio.lock);
    bool live = s_radio.event_live == mode;
    taskEXIT_CRITICAL(&s_radio.lock);
    return live;
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
static bool wifi_radio_ap_prestart_config_matches(const wifi_config_t *requested, const wifi_config_t *actual)
{
    return esp32_mquickjs_wifi_radio_accept_ap_config(requested, actual) &&
        (requested->ap.channel == 0 || requested->ap.channel == actual->ap.channel) &&
        requested->ap.ssid_hidden == actual->ap.ssid_hidden &&
        requested->ap.max_connection == actual->ap.max_connection &&
        requested->ap.beacon_interval == actual->ap.beacon_interval &&
        requested->ap.csa_count == actual->ap.csa_count && requested->ap.dtim_period == actual->ap.dtim_period &&
        requested->ap.ftm_responder == actual->ap.ftm_responder;
}

static bool wifi_radio_ap_reopen_config_matches(const wifi_config_t *requested, const wifi_config_t *actual)
{
    return wifi_radio_ap_prestart_config_matches(requested, actual) && requested->ap.channel == actual->ap.channel;
}
#endif

/* The SDK public setter rejects inactive AP. Different configurations use the
 * guarded native mode-change window before AP allocation and AP start.
 * Matching configurations retain their existing mutation-free capture path. */
esp_err_t esp32_mquickjs_wifi_radio_begin_ap_reopen(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const wifi_config_t *requested,
    esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
#if !CONFIG_ESP32_MQUICKJS_FEATURE_WIFI || !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    (void)application; (void)station; (void)requested; (void)access_point; (void)token;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (requested == NULL || token == NULL || token->identity != 0U ||
        station == NULL || !station->acquired || access_point == NULL ||
        access_point->acquired || access_point->identity != 0U || access_point->generation != 0U)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = esp32_mquickjs_wifi_radio_validate_ap_config(requested);
    if (err != ESP_OK) return err;
    wifi_radio_operation_lock();
    wifi_config_t *stored = NULL;
    err = ESP_ERR_INVALID_STATE;
    if (!s_radio.started || !s_radio.driver_owned || !s_radio.storage_configured ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED ||
        s_radio.effective_mode != WIFI_MODE_STA || !wifi_radio_interfaces_live(WIFI_MODE_STA) ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        s_radio.ap_stop_phase != AP_STOP_IDLE || s_radio.ap_reopen_pending) goto done;
    if (!wifi_radio_lease_valid(station) || station->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA)
        goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity == station->identity && s_radio.leases[i].required_mode != WIFI_MODE_STA)
            goto done;
    /* Claim the existing owners first; all following failures before AP owner
     * publication have no driver mutations and can release this reservation. */
    err = wifi_radio_begin_lifecycle_locked(application, station, NULL, token);
    if (err != ESP_OK) goto done;
    if (s_radio.next_lease_identity == 0U) { err = ESP_ERR_NO_MEM; goto release_token; }
    stored = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*stored), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (stored == NULL) { err = ESP_ERR_NO_MEM; goto release_token; }
    wifi_mode_t mode;
    err = esp_wifi_get_mode(&mode);
    if (err == ESP_OK && mode != WIFI_MODE_STA) err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && requested->ap.channel != 0)
        err = wifi_radio_validate_regulatory_channel(requested->ap.channel);
    if (err == ESP_OK) err = esp_wifi_get_config(WIFI_IF_AP, stored);
    if (err == ESP_OK && !wifi_radio_ap_reopen_config_matches(requested, stored)) {
#if ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE
        err = esp32_mquickjs_wifi_ap_prestart_prepare(token->generation, token->identity, requested, stored,
            wifi_radio_ap_prestart_config_matches);
        if (err == ESP_OK) s_radio.ap_reopen_config_pending = true;
#else
        err = ESP_ERR_NOT_SUPPORTED;
#endif
    }
    if (err != ESP_OK) goto release_token;
    err = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP, WIFI_MODE_AP, access_point);
    if (err != ESP_OK) goto release_token;
    s_radio.ap_transition_application = application != NULL && application->acquired ? application->identity : 0;
    s_radio.ap_transition_station = station->identity;
    s_radio.ap_transition_access_point = access_point->identity;
    s_radio.ap_reopen_pending = true;
    s_radio.ap_reopen_attempted = false;
    s_radio.ap_reopen_quiesced = s_radio.ap_reopen_restore_complete = false;
    goto done;
release_token:
#if ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE
    if (s_radio.ap_reopen_config_pending) {
        esp_err_t released = esp32_mquickjs_wifi_ap_prestart_release(token->generation, token->identity);
        if (released) { err = wifi_radio_cleanup_fault("ap-prestart-release", released); goto done; }
        s_radio.ap_reopen_config_pending = false;
    }
#endif
    taskENTER_CRITICAL(&s_radio.lock);
    memset(&s_radio.lifecycle, 0, sizeof(s_radio.lifecycle));
    memset(token, 0, sizeof(*token));
    taskEXIT_CRITICAL(&s_radio.lock);
done:
    if (stored != NULL) {
        esp32_mquickjs_wireless_secure_zero(stored, sizeof(*stored));
        esp32_mquickjs_memory_payload_free(stored);
    }
    wifi_radio_operation_unlock();
    return err;
#endif
}

esp_err_t esp32_mquickjs_wifi_radio_check_ap_reopen(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    bool valid = s_radio.ap_reopen_pending && !s_radio.ap_reopen_attempted &&
        wifi_radio_ap_transition_valid(token) && s_radio.started &&
        s_radio.effective_mode == WIFI_MODE_STA && wifi_radio_interfaces_live(WIFI_MODE_STA);
    wifi_radio_operation_unlock();
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

/* AP netif/handlers must already be attached. A failed activation is never
 * replayed; the caller must transfer this exact token to AP removal/cleanup. */
esp_err_t esp32_mquickjs_wifi_radio_finish_ap_reopen(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, uint8_t *primary)
{
    if (primary == NULL) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    const char *stage = "ap-reopen-state";
    if (!s_radio.ap_reopen_pending || s_radio.ap_reopen_attempted ||
        !wifi_radio_ap_transition_valid(token)) goto done;
    if (!s_radio.started || s_radio.effective_mode != WIFI_MODE_STA || !wifi_radio_interfaces_live(WIFI_MODE_STA))
        goto failed;
    stage = "ap-reopen-events";
    err = wifi_radio_begin_events(RADIO_EVENTS_AP_START, WIFI_MODE_AP);
    if (err != ESP_OK) goto failed;
    s_radio.ap_reopen_attempted = true;
    stage = "ap-reopen-mode";
#if ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE
    if (s_radio.ap_reopen_config_pending) {
        esp32_mquickjs_wifi_ap_prestart_result_t prepared;
        err = esp32_mquickjs_wifi_ap_prestart_activate(token->generation, token->identity, &prepared);
        s_radio.ap_reopen_quiesced = prepared.returned && prepared.ap_quiesced;
        s_radio.ap_reopen_restore_complete = s_radio.ap_reopen_quiesced &&
            (!prepared.mutated || (prepared.rollback_complete && s_radio.storage == WIFI_STORAGE_RAM));
        s_radio.configuration = (esp32_mquickjs_wifi_radio_config_result_t){
            .error = err, .stage = prepared.stage, .mutation_attempted = prepared.mutated,
            .rollback_attempted = prepared.rollback_attempted, .rollback_complete = prepared.rollback_complete,
            .rollback_error = prepared.rollback_error,
            .rollback_stage = prepared.rollback_attempted ? "ap-prestart-rollback" : NULL,
            .persistent_mutation_possible = prepared.mutated && s_radio.storage == WIFI_STORAGE_FLASH};
        if (prepared.stage) stage = prepared.stage;
        esp_err_t released = esp32_mquickjs_wifi_ap_prestart_release(token->generation, token->identity);
        if (!released) s_radio.ap_reopen_config_pending = false;
        else {
            s_radio.ap_reopen_restore_complete = false;
            if (!err) { err = released; stage = "ap-prestart-release"; }
        }
    } else
#endif
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) goto failed;
    stage = "ap-reopen-events";
    err = wifi_radio_wait_events();
    if (err != ESP_OK) goto failed;
    stage = "ap-reopen-mode-readback";
    wifi_mode_t mode;
    err = esp_wifi_get_mode(&mode);
    if (err == ESP_OK && mode != WIFI_MODE_APSTA) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) goto failed;
    stage = "ap-reopen-channel";
    uint8_t channel;
    wifi_second_chan_t secondary;
    uint32_t generation;
    err = wifi_radio_refresh_channel(&channel, &secondary, &generation);
    if (err != ESP_OK) goto failed;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    stage = "ap-reopen-inactive-restore";
    err = wifi_radio_restore_inactive_locked(mode);
    if (err != ESP_OK) goto failed;
#endif
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_radio.event_live != WIFI_MODE_APSTA) {
        taskEXIT_CRITICAL(&s_radio.lock);
        err = ESP_ERR_INVALID_STATE;
        goto failed;
    }
    *primary = channel;
    s_radio.effective_mode = WIFI_MODE_APSTA;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U && s_radio.leases[i].identity == s_radio.ap_transition_application)
            s_radio.leases[i].required_mode = WIFI_MODE_APSTA;
    s_radio.ap_reopen_pending = s_radio.ap_reopen_attempted = false;
    s_radio.ap_reopen_quiesced = s_radio.ap_reopen_restore_complete = false;
    s_radio.ap_transition_application = s_radio.ap_transition_station = s_radio.ap_transition_access_point = 0;
    memset(&s_radio.lifecycle, 0, sizeof(s_radio.lifecycle));
    memset(token, 0, sizeof(*token));
    taskEXIT_CRITICAL(&s_radio.lock);
    err = ESP_OK;
    goto done;
failed:
    err = wifi_radio_cleanup_fault(stage, err);
done:
    wifi_radio_operation_unlock();
    return err;
}

/* A rejected pre-create transaction can retire its AP netif after native AP
 * absence and an event fence, without waiting for an AP_STOP that cannot occur.
 * Once native AP start was allowed, normal AP_STOP must prove termination. */
esp_err_t esp32_mquickjs_wifi_radio_abort_ap_reopen(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_radio.ap_reopen_pending && wifi_radio_ap_transition_valid(token)) {
#if ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE
        if (s_radio.ap_reopen_config_pending) {
            err = esp32_mquickjs_wifi_ap_prestart_release(token->generation, token->identity);
            if (err) { wifi_radio_operation_unlock(); return err; }
            s_radio.ap_reopen_config_pending = false;
        }
#endif
        if (s_radio.ap_reopen_quiesced) {
            err = wifi_radio_begin_events(RADIO_EVENTS_AP_STOP, WIFI_MODE_NULL);
            if (err != ESP_OK) { wifi_radio_operation_unlock(); return err; }
            s_radio.ap_stop_phase = AP_STOP_SUBMITTED;
        } else s_radio.ap_stop_phase = s_radio.ap_reopen_attempted ? AP_STOP_READY : AP_STOP_QUIESCED;
        s_radio.ap_stop_error = ESP_OK;
        s_radio.ap_reopen_pending = s_radio.ap_reopen_attempted = false;
        err = ESP_OK;
    }
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    const char *stage = "ap-stop-state";
    if (!wifi_radio_ap_stop_valid(token)) goto done;
    if (s_radio.ap_stop_phase == AP_STOP_QUIESCED) { err = ESP_OK; goto done; }
    if (!s_radio.started || s_radio.restart_required) goto failed;
    wifi_mode_t mode;
    if (s_radio.ap_stop_phase == AP_STOP_READY) {
        stage = "ap-stop-mode-snapshot";
        err = esp_wifi_get_mode(&mode);
        if (err != ESP_OK) goto failed;
        if (mode != WIFI_MODE_APSTA) { err = ESP_ERR_INVALID_STATE; goto failed; }
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        /* Once AP is disabled its getter is unavailable. A read failure must
         * not prevent closing the interface; keep it as unavailable history. */
        uint16_t inactive = 0;
        esp_err_t observed = esp_wifi_get_inactive_time(WIFI_IF_AP, &inactive);
        wifi_radio_inactive_history_record(1, inactive, observed);
#endif
        stage = "ap-stop-events";
        err = wifi_radio_begin_events(RADIO_EVENTS_AP_STOP, WIFI_MODE_AP);
        if (err != ESP_OK) goto failed;
        stage = "ap-stop-mode";
        s_radio.ap_stop_phase = AP_STOP_ATTEMPTED;
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        s_radio.ap_stop_error = err;
        if (err != ESP_OK) goto failed;
        s_radio.ap_stop_phase = AP_STOP_SUBMITTED;
    }
    if (s_radio.ap_stop_phase == AP_STOP_ATTEMPTED) {
        /* A failed setter may already have changed mode. A retry observes it;
         * never replay an uncertain mutation. If APSTA remains, explicit whole
         * cleanup after the Station exits is required. */
        stage = "ap-stop-mode-readback";
        err = esp_wifi_get_mode(&mode);
        if (err != ESP_OK) goto failed;
        if (mode != WIFI_MODE_STA) {
            err = s_radio.ap_stop_error != ESP_OK ? s_radio.ap_stop_error : ESP_ERR_INVALID_RESPONSE;
            goto failed;
        }
        s_radio.ap_stop_phase = AP_STOP_SUBMITTED;
    }
    if (s_radio.ap_stop_phase == AP_STOP_SUBMITTED) {
        stage = "ap-stop-events";
        err = wifi_radio_wait_events();
        if (err != ESP_OK) goto failed;
        s_radio.ap_stop_phase = AP_STOP_EVENTS_DONE;
    }
    stage = "ap-stop-mode-readback";
    err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) goto failed;
    if (mode != WIFI_MODE_STA) { err = ESP_ERR_INVALID_RESPONSE; goto failed; }
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.effective_mode = WIFI_MODE_STA;
    s_radio.ap_stop_phase = AP_STOP_QUIESCED;
    taskEXIT_CRITICAL(&s_radio.lock);
    err = ESP_OK;
    goto done;
failed:
    err = wifi_radio_cleanup_fault(stage, err);
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_adopt_ap_stop(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (wifi_radio_ap_stop_valid(token)) {
        s_radio.ap_stop_phase = AP_STOP_IDLE;
        s_radio.ap_reopen_pending = s_radio.ap_reopen_attempted = false;
        s_radio.ap_reopen_quiesced = s_radio.ap_reopen_restore_complete = false;
        s_radio.ap_transition_application = s_radio.ap_transition_station = s_radio.ap_transition_access_point = 0;
        s_radio.ap_stop_error = ESP_OK;
        err = ESP_OK;
    }
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_check_ap_stopped_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    bool valid = wifi_radio_ap_stop_valid(token);
    taskENTER_CRITICAL(&s_radio.lock);
    valid = valid && s_radio.ap_stop_phase == AP_STOP_QUIESCED && s_radio.started &&
        s_radio.effective_mode == WIFI_MODE_STA && s_radio.event_live == WIFI_MODE_STA;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

/* Caller has retired only the AP netif. Do not stop the remaining driver or
 * touch Station helper/IP state; reduce Application's APSTA requirement to STA. */
esp_err_t esp32_mquickjs_wifi_radio_finish_ap_stop(
    esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_radio_lease_t *access_point)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_ap_stop_valid(token) || access_point == NULL ||
        !wifi_radio_lease_valid(access_point) || access_point->identity != s_radio.ap_transition_access_point ||
        access_point->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP ||
        s_radio.ap_stop_phase != AP_STOP_QUIESCED || !s_radio.started) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    bool station_live = s_radio.event_live == WIFI_MODE_STA && s_radio.effective_mode == WIFI_MODE_STA;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (!station_live) goto done;
    wifi_radio_release_locked(access_point);
    if (access_point->acquired) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U && s_radio.leases[i].identity == s_radio.ap_transition_application)
            s_radio.leases[i].required_mode = WIFI_MODE_STA;
    if (s_radio.fault_stage != NULL && (strncmp(s_radio.fault_stage, "ap-stop-", 8) == 0 ||
        (s_radio.ap_reopen_restore_complete && strncmp(s_radio.fault_stage, "ap-prestart-", 12) == 0))) {
        s_radio.fault_stage = NULL;
        s_radio.fault_error = ESP_OK;
    }
    if (s_radio.cleanup_stage != NULL && (strncmp(s_radio.cleanup_stage, "ap-stop-", 8) == 0 ||
        (s_radio.ap_reopen_restore_complete && strncmp(s_radio.cleanup_stage, "ap-prestart-", 12) == 0))) {
        s_radio.cleanup_stage = NULL;
        s_radio.cleanup_error = ESP_OK;
    }
    s_radio.driver_state = s_radio.fault_stage == NULL ? ESP32_MQUICKJS_WIFI_RADIO_STARTED : ESP32_MQUICKJS_WIFI_RADIO_FAULTED;
    s_radio.ap_stop_phase = AP_STOP_IDLE;
    s_radio.ap_reopen_pending = s_radio.ap_reopen_attempted = false;
    s_radio.ap_reopen_quiesced = s_radio.ap_reopen_restore_complete = false;
    s_radio.ap_transition_application = s_radio.ap_transition_station = s_radio.ap_transition_access_point = 0;
    s_radio.ap_stop_error = ESP_OK;
    memset(&s_radio.lifecycle, 0, sizeof(s_radio.lifecycle));
    memset(token, 0, sizeof(*token));
    taskEXIT_CRITICAL(&s_radio.lock);
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_quiesce_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (token == NULL || token->identity == 0U) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (token->identity == s_radio.lifecycle.identity && token->generation == s_radio.lifecycle.generation)
        err = wifi_radio_stop_locked();
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_finish_lifecycle(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, bool shutdown)
{
    if (token == NULL || token->identity == 0U) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (WIFI_RADIO_MESH_PENDING) goto done;
    if (token->identity != s_radio.lifecycle.identity || token->generation != s_radio.lifecycle.generation) goto done;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (s_vendor_ie.start_owner.identity != 0U) goto done;
#endif
    err = shutdown ? wifi_radio_shutdown_locked() : wifi_radio_stop_locked();
    if (err == ESP_OK) {
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        wifi_radio_restart_configs_discard_locked(token);
        if (s_policy_restart.owner.identity == token->identity &&
            s_policy_restart.owner.generation == token->generation)
            memset(&s_policy_restart, 0, sizeof(s_policy_restart));
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
        if (wifi_radio_twt_recovery_exact_locked(token)) memset(&s_twt_recovery, 0, sizeof(s_twt_recovery));
#endif
        taskENTER_CRITICAL(&s_radio.lock);
        memset(&s_radio.lifecycle, 0, sizeof(s_radio.lifecycle));
        taskEXIT_CRITICAL(&s_radio.lock);
        memset(token, 0, sizeof(*token));
    }
done:
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_check_stopped_lifecycle_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, bool cleanup)
{
    if (token == NULL || token->identity == 0U) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (token->identity != s_radio.lifecycle.identity || token->generation != s_radio.lifecycle.generation ||
        s_radio.started || s_radio.stop_required || s_radio.operation.identity != 0U ||
        s_radio.wake_locks != 0 || s_radio.promiscuous_claimed ||
        s_radio.restart_required || (!cleanup && (s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL)) ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         !(cleanup && (s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_FAULTED ||
                       s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING)))) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    err = ESP_OK;
done:
    return err;
}


esp_err_t esp32_mquickjs_wifi_radio_check_stopped_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, bool cleanup)
{
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_check_stopped_lifecycle_locked(token, cleanup);
    wifi_radio_operation_unlock();
    return err;
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* SDK get_config returns a dormant SAE default even for a non-SAE AP. This
 * caller-owned snapshot must retain its exact bytes; validate active policy
 * with the existing input validator, then put the inert SDK field back. */
static esp_err_t wifi_radio_validate_saved_ap_config(wifi_config_t *config)
{
    wifi_sae_pwe_method_t saved_pwe = config->ap.sae_pwe_h2e;
    bool sae = config->ap.authmode == WIFI_AUTH_WPA3_PSK ||
        config->ap.authmode == WIFI_AUTH_WPA2_WPA3_PSK || config->ap.wpa3_compatible_mode;
    if (!sae && (saved_pwe == WPA3_SAE_PWE_HUNT_AND_PECK ||
        saved_pwe == WPA3_SAE_PWE_HASH_TO_ELEMENT || saved_pwe == WPA3_SAE_PWE_BOTH))
        config->ap.sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED;
    esp_err_t error = esp32_mquickjs_wifi_radio_validate_ap_config(config);
    config->ap.sae_pwe_h2e = saved_pwe;
    return error;
}

/* Caller owns secure temporary output; never expose these credentials to JS. */
esp_err_t esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(*config));
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_check_stopped_lifecycle_locked(token, false);
    if (err == ESP_OK && (!s_radio.driver_owned || !s_radio.storage_configured)) err = ESP_ERR_INVALID_STATE;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (err == ESP_OK) err = esp_wifi_get_config(WIFI_IF_AP, config);
    if (err == ESP_OK) err = wifi_radio_validate_saved_ap_config(config);
    if (err == ESP_OK && config->ap.channel != 0) err = wifi_radio_validate_regulatory_channel(config->ap.channel);
#else
    if (err == ESP_OK) err = ESP_ERR_NOT_SUPPORTED;
#endif
    if (err != ESP_OK) esp32_mquickjs_wireless_secure_zero(config, sizeof(*config));
    wifi_radio_operation_unlock();
    return err;
}
#endif

esp_err_t esp32_mquickjs_wifi_radio_validate_start_controls(bool start,
    const esp32_mquickjs_wifi_radio_start_controls_t *controls)
{
    if (controls == NULL || !controls->tx_power_set) return ESP_OK;
    return start && controls->tx_power_quarter_dbm >= 8 && controls->tx_power_quarter_dbm <= 80
        ? ESP_OK : ESP_ERR_INVALID_ARG;
}

/* Native START/events/mode have completed, the exact lifecycle and staged
 * leases still exclude all other mutation. No JS roots or input are retained. */
static esp_err_t wifi_radio_apply_start_controls(
    const esp32_mquickjs_wifi_radio_start_controls_t *controls)
{
    if (controls == NULL || !controls->tx_power_set) return ESP_OK;
    esp32_mquickjs_wifi_radio_config_result_t result = {.stage = "tx-power-snapshot"};
    int8_t before, actual;
    esp_err_t err = esp_wifi_get_max_tx_power(&before);
    if (err == ESP_OK && (before < 8 || before > 80)) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) goto record;
    result.stage = "tx-power-config";
    result.mutation_attempted = true;
    err = esp_wifi_set_max_tx_power(controls->tx_power_quarter_dbm);
    if (err != ESP_OK) goto rollback;
    result.stage = "tx-power-readback";
    err = esp_wifi_get_max_tx_power(&actual);
    /* The SDK quantizes/clamps power. Accept a valid lower ceiling, never a
     * value above the request. Actual power is available in radio status. */
    if (err == ESP_OK && (actual < 8 || actual > controls->tx_power_quarter_dbm))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) goto rollback;
    result.stage = "complete";
    goto record;
rollback:
    result.rollback_attempted = true;
    result.rollback_stage = "rollback-tx-power";
    result.rollback_error = esp_wifi_set_max_tx_power(before);
    if (result.rollback_error != ESP_OK) goto record;
    result.rollback_stage = "rollback-tx-power-readback";
    result.rollback_error = esp_wifi_get_max_tx_power(&actual);
    if (result.rollback_error == ESP_OK && actual != before)
        result.rollback_error = ESP_ERR_INVALID_RESPONSE;
    if (result.rollback_error == ESP_OK) {
        result.rollback_complete = true;
        result.rollback_stage = NULL;
    }
record:
    result.error = err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.activation = result;
    taskEXIT_CRITICAL(&s_radio.lock);
    /* Startup has happened even if power was restored. Explicit cleanup must
     * stop/drain it; a successful scalar rollback is not lifecycle recovery. */
    if (err != ESP_OK) {
        (void)wifi_radio_record_fault(result.stage, err);
        if (result.rollback_attempted && !result.rollback_complete)
            (void)wifi_radio_cleanup_fault("activation-rollback", result.rollback_error);
    }
    return err;
}

struct esp32_mquickjs_wifi_eap_profile;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
static esp_err_t wifi_radio_eap_restart_install_locked(esp32_mquickjs_wifi_eap_profile_t *profile,
    const esp32_mquickjs_wifi_radio_lease_t owners[3]);
#endif

static esp_err_t wifi_radio_resume_lifecycle_locked(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode, bool start,
    esp32_mquickjs_wifi_radio_lease_t *application,
    esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_radio_start_controls_t *controls, bool keep_lifecycle,
    struct esp32_mquickjs_wifi_eap_profile *enterprise_profile)
{
    if (enterprise_profile && (!start || keep_lifecycle || controls ||
        !(mode & WIFI_MODE_STA) || !application || !station)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP32_MQUICKJS_FEATURE_WIFI || !CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (enterprise_profile) return ESP_ERR_NOT_SUPPORTED;
#endif
    if (token == NULL || token->identity == 0U ||
        (mode != WIFI_MODE_STA && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA))
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (mode & WIFI_MODE_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    esp_err_t validated = esp32_mquickjs_wifi_radio_validate_start_controls(start, controls);
    if (validated != ESP_OK) return validated;
    esp32_mquickjs_wifi_radio_lease_t *outputs[3] = {application, station, access_point};
    const esp32_mquickjs_wifi_radio_client_t clients[3] = {
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,
    };
    const wifi_mode_t modes[3] = {mode, WIFI_MODE_STA, WIFI_MODE_AP};
    size_t owner_count = 0;
    wifi_mode_t required_mode = WIFI_MODE_NULL;
    for (size_t i = 0; i < 3; ++i) {
        if (outputs[i] == NULL) continue;
        if (!start || outputs[i]->acquired || outputs[i]->identity != 0U ||
            outputs[i]->generation != 0U || (modes[i] & mode) != modes[i]) return ESP_ERR_INVALID_ARG;
        for (size_t j = 0; j < i; ++j)
            if (outputs[i] == outputs[j]) return ESP_ERR_INVALID_ARG;
        owner_count++;
        required_mode = (wifi_mode_t)(required_mode | modes[i]);
    }
    if (start && (owner_count == 0 || required_mode != mode)) return ESP_ERR_INVALID_ARG;
    if (token->identity != s_radio.lifecycle.identity || token->generation != s_radio.lifecycle.generation ||
        s_radio.operation.identity != 0U || s_radio.wake_locks != 0 || s_radio.promiscuous_claimed ||
        !s_radio.driver_owned || !s_radio.storage_configured || s_radio.started || s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL) return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) return ESP_ERR_INVALID_STATE;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (!wifi_radio_restart_configs_ready_locked(token, mode)) return ESP_ERR_INVALID_STATE;
    if (keep_lifecycle && (!start || s_config_restart.snapshot == NULL ||
        !s_config_restart.snapshot->restore_off || controls != NULL)) return ESP_ERR_INVALID_STATE;
    if (!keep_lifecycle && s_config_restart.snapshot != NULL && s_config_restart.snapshot->restore_off)
        return ESP_ERR_INVALID_STATE;
    /* A restoration and a new power request are different operations. */
    if (s_config_restart.captured && controls != NULL && controls->tx_power_set) return ESP_ERR_INVALID_ARG;
#endif
    size_t dependent_count = 0;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (s_vendor_ie.start_owner.identity != 0U) {
        if (!start || !wifi_radio_vendor_ie_start_matches(token) || mode != s_vendor_ie.start_mode ||
            s_radio.storage != s_vendor_ie.start_storage || controls != NULL) return ESP_ERR_INVALID_STATE;
        dependent_count = wifi_radio_vendor_ie_start_count();
    }
#endif
    size_t reservation_count = owner_count + dependent_count;
    if (reservation_count > WIFI_RADIO_MAX_LEASES) return ESP_ERR_NO_MEM;
    /* Reserve identity capacity before even attempting start. UINT32_MAX is a
     * valid final identity; zero is the exhausted sentinel, never recycled. */
    if (reservation_count != 0 && (s_radio.next_lease_identity == 0U ||
        reservation_count - 1 > UINT32_MAX - s_radio.next_lease_identity)) return ESP_ERR_NO_MEM;
    wifi_mode_t actual_mode;
    esp_err_t err = esp_wifi_get_mode(&actual_mode);
    if (err == ESP_OK && actual_mode != mode) err = ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) return err;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (start) {
        /* Final replay may include temporary START/STOP cycles. Check the
         * resulting credentials before an AP can advertise or accept clients,
         * and before consuming any staged owner identity. */
        err = wifi_radio_restart_configs_pre_start_locked(token, mode);
        if (err != ESP_OK) return err;
    }
#endif
    esp32_mquickjs_wifi_radio_lease_t staged[3] = {0};
    esp32_mquickjs_wifi_radio_lease_t *start_lease = NULL;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    err = wifi_radio_vendor_ie_start_attach(token);
    if (err != ESP_OK) goto fail;
#endif
    for (size_t i = 0; i < 3; ++i) {
        if (outputs[i] == NULL) continue;
        err = wifi_radio_acquire_locked(clients[i], modes[i], &staged[i]);
        if (err != ESP_OK) goto fail;
        if (start_lease == NULL) start_lease = &staged[i];
    }
    if (start) {
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
        err = wifi_radio_restart_configs_start_locked(token, mode, start_lease);
#else
        err = wifi_radio_ensure_started_locked(start_lease);
#endif
        if (err != ESP_OK) goto fail;
        err = esp_wifi_get_mode(&actual_mode);
        if (err == ESP_OK && actual_mode != mode) err = ESP_ERR_INVALID_RESPONSE;
        if (err != ESP_OK) {
            (void)wifi_radio_record_fault("resume-mode-readback", err);
            goto fail;
        }
    }
    if (start) {
        err = wifi_radio_apply_start_controls(controls);
        if (err != ESP_OK) goto fail;
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (s_policy_restart.owner.identity != 0U) {
        if (!start || s_policy_restart.mode != mode) { err = ESP_ERR_INVALID_STATE; goto fail; }
        err = wifi_radio_policy_restart_replay_locked(token, true);
        if (err != ESP_OK) goto fail;
        if (s_policy_restart.completed != s_policy_restart.snapshot.mask) { err = ESP_ERR_INVALID_STATE; goto fail; }
    }
    err = wifi_radio_restart_configs_post_start_locked(token);
    if (err != ESP_OK) goto fail;
    err = wifi_radio_restart_configs_commit_locked(token, mode);
    if (err != ESP_OK) goto fail;
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (enterprise_profile) {
        err = wifi_radio_eap_restart_install_locked(enterprise_profile, staged);
        if (err != ESP_OK) goto fail;
    }
#endif
    if (!keep_lifecycle) {
        wifi_radio_restart_configs_discard_locked(token);
        memset(&s_policy_restart, 0, sizeof(s_policy_restart));
    }
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    wifi_radio_vendor_ie_start_commit(token);
#endif
    /* Only now hand the leases back to the runtime. Other operations remain
     * excluded until every output is installed and the token is retired. */
    for (size_t i = 0; i < 3; ++i)
        if (outputs[i] != NULL) *outputs[i] = staged[i];
    if (keep_lifecycle) return ESP_OK;
    taskENTER_CRITICAL(&s_radio.lock);
    memset(&s_radio.lifecycle, 0, sizeof(s_radio.lifecycle));
    taskEXIT_CRITICAL(&s_radio.lock);
    memset(token, 0, sizeof(*token));
    return ESP_OK;
fail:
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    wifi_radio_vendor_ie_start_park(token);
#endif
    for (size_t i = 3; i > 0; --i) wifi_radio_release_locked(&staged[i - 1]);
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_resume_lifecycle(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode, bool start,
    esp32_mquickjs_wifi_radio_lease_t *application,
    esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_radio_start_controls_t *controls)
{
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_resume_lifecycle_locked(token, mode, start, application, station, access_point, controls, false, NULL);
    wifi_radio_operation_unlock();
    return err;
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
esp_err_t esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    wifi_radio_operation_lock();
    /* The one temporary native lease is never published. The reservation and
     * frozen source outlive both START verification and the final STOP fence. */
    esp32_mquickjs_wifi_radio_lease_t temporary = {0};
    esp_err_t err = wifi_radio_resume_lifecycle_locked(token, mode, true,
        &temporary, NULL, NULL, NULL, true, NULL);
    wifi_radio_release_locked(&temporary);
    if (err != ESP_OK) goto done;
    err = wifi_radio_stop_locked();
    if (err != ESP_OK) goto done;
    err = wifi_radio_check_stopped_lifecycle_locked(token, false);
    if (err != ESP_OK) goto done;
    err = esp_wifi_set_mode(WIFI_MODE_NULL);
    if (err != ESP_OK) { err = wifi_radio_record_fault("restart-off-mode", err); goto done; }
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.effective_mode = WIFI_MODE_NULL;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_mode_t actual;
    err = esp_wifi_get_mode(&actual);
    if (err == ESP_OK && actual != WIFI_MODE_NULL) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) { err = wifi_radio_record_fault("restart-off-mode-readback", err); goto done; }
    err = wifi_radio_restart_configs_commit_storage_locked(token);
done:
    wifi_radio_operation_unlock();
    return err;
}

/* These phases retain the lifecycle reservation while the runtime retires or
 * attaches netifs outside the Radio mutex. They do not own helper resources. */
static bool wifi_radio_restart_checkpoint_matches_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    return token != NULL && token->identity != 0U &&
        token->identity == s_radio.lifecycle.identity && token->generation == s_radio.lifecycle.generation &&
        s_config_restart.captured && s_config_restart.owner.identity == token->identity &&
        s_config_restart.owner.generation == token->generation && s_config_restart.mode == mode &&
        s_policy_restart.owner.identity == token->identity &&
        s_policy_restart.owner.generation == token->generation && s_policy_restart.mode == mode;
}

esp_err_t esp32_mquickjs_wifi_radio_admit_restart_retry(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_radio_restart_selection_t *selection)
{
    if (token == NULL || token->identity == 0U || selection == NULL) return ESP_ERR_INVALID_ARG;
    bool allow_ap_restart = selection->allow_ap_restart;
    *selection = (esp32_mquickjs_wifi_radio_restart_selection_t){.allow_ap_restart = allow_ap_restart};
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    wifi_mode_t mode = s_config_restart.mode;
    if (!wifi_radio_restart_checkpoint_matches_locked(token, mode) ||
        (mode != WIFI_MODE_STA && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) ||
        s_config_restart.source_generation == 0U ||
        (s_config_restart.snapshot != NULL && s_config_restart.snapshot->capture_error != ESP_OK) ||
        s_radio.restart_required || s_radio.operation.identity != 0U || s_radio.wake_locks != 0U ||
        s_radio.promiscuous_claimed || s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending ||
        s_interval.owner.identity != 0U || s_interval.restore_pending || s_vendor_ie.start_owner.identity != 0U ||
        WIFI_RADIO_SMARTCONFIG_PENDING || WIFI_RADIO_WPS_PENDING || WIFI_RADIO_DPP_PENDING ||
        WIFI_RADIO_EAP_PENDING || WIFI_RADIO_NAN_PENDING || WIFI_RADIO_MESH_PENDING ||
        wifi_radio_raw_tx_recovery_exact_locked(token) || wifi_radio_action_recovery_exact_locked(token)) goto done;
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (wifi_radio_ftm_recovery_exact_locked(token)) goto done;
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (wifi_radio_twt_recovery_exact_locked(token)) goto done;
#endif
    if (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED &&
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED &&
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_FAULTED &&
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING) goto done;
    if ((!s_radio.driver_owned && (s_radio.started || s_radio.stop_required || s_radio.storage_configured)) ||
        (s_radio.driver_owned && s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED)) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    bool restore_off = s_config_restart.snapshot != NULL && s_config_restart.snapshot->restore_off;
    if (restore_off && (mode & WIFI_MODE_AP) && !allow_ap_restart) goto done;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (mode & WIFI_MODE_AP) { err = ESP_ERR_NOT_SUPPORTED; goto done; }
#endif
    size_t owners = restore_off || mode == WIFI_MODE_AP ? 1U : mode == WIFI_MODE_STA ? 2U : 3U;
    if (s_radio.next_lease_identity == 0U || owners - 1U > UINT32_MAX - s_radio.next_lease_identity) {
        err = ESP_ERR_NO_MEM;
        goto done;
    }
    /* Faults stay visible through STOP/helper retirement. Only successful
     * physical shutdown can clear them. No uncertain current state is read as
     * a replacement for the predecessor captured by this exact lifecycle. */
    selection->mode = mode;
    selection->cold = s_config_restart.snapshot == NULL;
    selection->restore_off = restore_off;
    /* No configuration step of this new call has run yet. A STOP/helper/init
     * error must not expose the previous attempt's configuration result. */
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = (esp32_mquickjs_wifi_radio_config_result_t){0};
    taskEXIT_CRITICAL(&s_radio.lock);
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_checkpoint_restart_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    if (s_vendor_ie.start_owner.identity != 0U) return ESP_ERR_INVALID_STATE;
    if (token == NULL || token->identity == 0U ||
        (mode != WIFI_MODE_STA && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (mode & WIFI_MODE_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    /* A rebuilt generation must keep its frozen predecessor, never treat the
     * new driver as another source checkpoint. Cleanup/rebuild is explicit. */
    if ((s_config_restart.captured || s_config_restart.snapshot != NULL) &&
        s_config_restart.source_generation != s_radio.generation) return ESP_ERR_INVALID_STATE;
    esp_err_t err = wifi_radio_policy_restart_prepare_locked(token, mode);
    if (err != ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.configuration = (esp32_mquickjs_wifi_radio_config_result_t){
            .stage = "restart-policy-snapshot", .error = err};
        taskEXIT_CRITICAL(&s_radio.lock);
        return err;
    }
    err = wifi_radio_restart_configs_capture_locked(token, mode);
    if (err != ESP_OK) return err;
    /* Capture may itself need START/STOP cycles to read an inactive band.
     * Only a completed final STOP permits old helper/netif retirement. */
    err = wifi_radio_stop_locked();
    if (err != ESP_OK) return err;
    return wifi_radio_check_stopped_lifecycle_locked(token, false);
}

esp_err_t esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    wifi_radio_operation_lock();
    /* Public execution details must never reuse a previous operation's config
     * result when this checkpoint rejects before reaching the capture helper. */
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "restart-checkpoint-admission"};
    taskEXIT_CRITICAL(&s_radio.lock);
    esp_err_t err = wifi_radio_checkpoint_restart_locked(token, mode);
    if (s_radio.configuration.stage != NULL && strcmp(s_radio.configuration.stage, "restart-checkpoint-admission") == 0) {
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.configuration.error = err;
        taskEXIT_CRITICAL(&s_radio.lock);
    }
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_rebuild_restart_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    if (!wifi_radio_restart_checkpoint_matches_locked(token, mode)) return ESP_ERR_INVALID_STATE;
    /* Caller must have retired old helpers. Never hide STOP here: runtime
     * callbacks/netifs must survive until STOP and their own drain complete. */
    esp_err_t err = wifi_radio_check_stopped_lifecycle_locked(token, true);
    if (err != ESP_OK) return err;
    err = wifi_radio_shutdown_locked();
    if (err != ESP_OK) return err;
    err = wifi_radio_initialize();
    if (err != ESP_OK) return err;
    return wifi_radio_check_stopped_lifecycle_locked(token, false);
}

esp_err_t esp32_mquickjs_wifi_radio_rebuild_restart_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_rebuild_restart_locked(token, mode);
    wifi_radio_operation_unlock();
    return err;
}

#if CONFIG_ESP_WIFI_WAPI_PSK
esp_err_t esp32_mquickjs_wifi_radio_wapi_prepare(bool enabled, bool *rebuild)
{
    if (!rebuild) return ESP_ERR_INVALID_ARG;
    *rebuild = false;
    wifi_radio_operation_lock();
    esp32_mquickjs_wifi_wapi_status_t status;
    esp32_mquickjs_wifi_wapi_sdk_status(&status);
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (status.busy || status.uncertain || s_radio.lifecycle.identity || s_radio.fault_stage ||
        s_radio.cleanup_stage || s_radio.restart_required) goto done;
    if (status.requested_enabled == enabled &&
        (!status.supplicant_active || status.enabled == enabled)) { error = ESP_OK; goto done; }
    if (s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED && !s_radio.driver_owned &&
        !status.supplicant_active && !s_radio.operation.identity && !s_radio.wake_locks && !s_radio.promiscuous_claimed) {
        for (unsigned i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
            if (s_radio.leases[i].identity) goto done;
        error = esp32_mquickjs_wifi_wapi_sdk_policy(enabled);
    } else if (wifi_radio_stop_snapshot_unchanged_locked()) {
        /* The runtime obtains the exact zero-owner lifecycle before changing
         * policy; this observation does not authorize a later mutation. */
        *rebuild = true; error = ESP_OK;
    }
done:
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wapi_select(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode, bool enabled)
{
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_restart_checkpoint_matches_locked(token, mode) ?
        wifi_radio_check_stopped_lifecycle_locked(token, true) : ESP_ERR_INVALID_STATE;
    if (!error) error = esp32_mquickjs_wifi_wapi_sdk_policy(enabled);
    wifi_radio_operation_unlock();
    return error;
}
#endif

static esp_err_t wifi_radio_replay_restart_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    if (!wifi_radio_restart_checkpoint_matches_locked(token, mode)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = wifi_radio_check_stopped_lifecycle_locked(token, false);
    if (err != ESP_OK) return err;
    if (!s_radio.driver_owned || !s_radio.storage_configured ||
        (s_config_restart.snapshot != NULL && s_config_restart.source_generation == s_radio.generation))
        return ESP_ERR_INVALID_STATE;
    /* New helper handlers/netifs must already be attached: replay can START
     * a temporary Station even when the final requested mode is AP-only. */
    err = wifi_radio_record_fault("mode", esp_wifi_set_mode(mode));
    if (err != ESP_OK) return err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.effective_mode = mode;
    taskEXIT_CRITICAL(&s_radio.lock);
    err = wifi_radio_restart_configs_replay_locked(token);
    if (err != ESP_OK) return err;
    return wifi_radio_policy_restart_replay_locked(token, false);
}

esp_err_t esp32_mquickjs_wifi_radio_replay_restart_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_replay_restart_locked(token, mode);
    wifi_radio_operation_unlock();
    return err;
}
#endif

esp_err_t esp32_mquickjs_wifi_radio_restart_lifecycle(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode,
    esp32_mquickjs_wifi_radio_lease_t *application)
{
    if (token == NULL || token->identity == 0U || application == NULL || application->acquired ||
        application->identity != 0U || application->generation != 0U ||
        (mode != WIFI_MODE_STA && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (mode & WIFI_MODE_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (token->identity != s_radio.lifecycle.identity || token->generation != s_radio.lifecycle.generation) goto done;
    if (s_radio.next_lease_identity == 0U) { err = ESP_ERR_NO_MEM; goto done; }
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    err = wifi_radio_checkpoint_restart_locked(token, mode);
    if (err != ESP_OK) goto done;
    err = wifi_radio_rebuild_restart_locked(token, mode);
    if (err != ESP_OK) goto done;
    err = wifi_radio_replay_restart_locked(token, mode);
    if (err != ESP_OK) goto done;
#else
    err = wifi_radio_shutdown_locked();
    if (err != ESP_OK) goto done;
    err = wifi_radio_initialize();
    if (err != ESP_OK) goto done;
    err = wifi_radio_record_fault("mode", esp_wifi_set_mode(mode));
    if (err != ESP_OK) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.effective_mode = mode;
    taskEXIT_CRITICAL(&s_radio.lock);
#endif
    err = wifi_radio_resume_lifecycle_locked(token, mode, true, application, NULL, NULL, NULL, false, NULL);
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_begin_operation(
    esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_radio_operation_kind_t kind,
    esp32_mquickjs_wifi_radio_operation_t *out_operation)
{
    if (out_operation == NULL || out_operation->identity != 0U ||
        (kind != ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN &&
         kind != ESP32_MQUICKJS_WIFI_RADIO_OPERATION_CONNECT)) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (WIFI_RADIO_EAP_PENDING && !s_eap_radio.ready) goto done;
#endif
    if (s_radio.lifecycle.identity != 0U) goto done;
    if (!wifi_radio_lease_valid(lease) ||
        lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA) goto done;
    if (s_radio.fault_stage != NULL) { err = s_radio.fault_error; goto done; }
    if (!s_radio.started || s_radio.operation.identity != 0U) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        if (s_radio.leases[i].identity != 0U && s_radio.leases[i].fixed_channel) goto done;
    }
    if (s_radio.next_operation_identity == 0U) { err = ESP_ERR_NO_MEM; goto done; }
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.operation = (esp32_mquickjs_wifi_radio_operation_t){
        .generation = lease->generation,
        .lease_identity = lease->identity,
        .identity = s_radio.next_operation_identity++,
        .kind = kind,
    };
    *out_operation = s_radio.operation;
    taskEXIT_CRITICAL(&s_radio.lock);
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

void esp32_mquickjs_wifi_radio_end_operation(
    esp32_mquickjs_wifi_radio_operation_t *operation)
{
    if (operation == NULL || operation->identity == 0U) return;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (operation->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_ACTION ||
        operation->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_ROC
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        || operation->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_AP_DEAUTH
#endif
#if CONFIG_ESP_WIFI_DPP_SUPPORT
        || operation->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
        || operation->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
        || operation->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SMARTCONFIG
        || operation->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_WPS
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
        || operation->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_TWT_PROBE
#endif
#if CONFIG_ESP_WIFI_RRM_SUPPORT
        || operation->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_RRM
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
        || operation->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_FTM
#endif
        ) return;
#endif
    wifi_radio_operation_lock();
    taskENTER_CRITICAL(&s_radio.lock);
    if (operation->identity == s_radio.operation.identity &&
        operation->generation == s_radio.operation.generation &&
        operation->lease_identity == s_radio.operation.lease_identity &&
        operation->kind == s_radio.operation.kind)
        memset(&s_radio.operation, 0, sizeof(s_radio.operation));
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    memset(operation, 0, sizeof(*operation));
}

static esp_err_t wifi_radio_configuration_owner_locked(
    const esp32_mquickjs_wifi_radio_lease_t *lease)
{
    if (s_radio.lifecycle.identity != 0U) return ESP_ERR_INVALID_STATE;
    if (!wifi_radio_lease_valid(lease)) return ESP_ERR_INVALID_STATE;
    if (s_radio.fault_stage != NULL) return s_radio.fault_error;
    if (!s_radio.started || s_radio.promiscuous_claimed || s_radio.operation.identity != 0U)
        return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        if (s_radio.leases[i].identity != 0U &&
            s_radio.leases[i].identity != lease->identity &&
            !(lease->client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA &&
              s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION] == 1U &&
              s_radio.leases[i].client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION))
            return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_set_power_save(
    esp32_mquickjs_wifi_radio_lease_t *lease,
    wifi_ps_type_t requested, wifi_ps_type_t *actual)
{
    if (actual == NULL || (requested != WIFI_PS_NONE &&
        requested != WIFI_PS_MIN_MODEM && requested != WIFI_PS_MAX_MODEM))
        return ESP_ERR_INVALID_ARG;
    wifi_ps_type_t value;
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_configuration_owner_locked(lease);
    if (err == ESP_OK) err = esp_wifi_set_ps(requested);
    if (err == ESP_OK) err = esp_wifi_get_ps(&value);
    if (err == ESP_OK) *actual = value;
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_set_tx_power(
    esp32_mquickjs_wifi_radio_lease_t *lease,
    int8_t requested, int8_t *actual)
{
    if (actual == NULL || requested < 8 || requested > 80)
        return ESP_ERR_INVALID_ARG;
    int8_t value;
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_configuration_owner_locked(lease);
    if (err == ESP_OK) err = esp_wifi_set_max_tx_power(requested);
    if (err == ESP_OK) err = esp_wifi_get_max_tx_power(&value);
    if (err == ESP_OK) *actual = value;
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_reserve_ap(esp32_mquickjs_wifi_radio_lease_t *lease)
{
    if (lease == NULL || lease->acquired) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_radio.lifecycle.identity != 0U || s_radio.started || s_radio.operation.identity != 0U ||
        s_radio.promiscuous_claimed) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    err = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP, WIFI_MODE_AP, lease);
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_validate_ap_config(const wifi_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    return ESP_ERR_NOT_SUPPORTED;
#else
    const wifi_ap_config_t *ap = &config->ap;
    if (ap->ssid_len == 0 || ap->ssid_len > sizeof(ap->ssid) ||
        (ap->channel > 14 && esp32_mquickjs_wifi_radio_5ghz_channel_bit(ap->channel) == 0) ||
        ap->max_connection < 1 || ap->max_connection > ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS ||
        ap->beacon_interval < ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU ||
        ap->beacon_interval > ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU ||
        ap->beacon_interval % ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU != 0 ||
        ap->dtim_period < 1 || ap->dtim_period > ESP32_MQUICKJS_WIFI_AP_DTIM_MAX || ap->csa_count == 0 ||
        (ap->gtk_rekey_interval != 0 && ap->gtk_rekey_interval < 60)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
    if (ap->channel > 14) return ESP_ERR_NOT_SUPPORTED;
#endif
    bool sae = ap->authmode == WIFI_AUTH_WPA3_PSK || ap->authmode == WIFI_AUTH_WPA2_WPA3_PSK ||
        ap->wpa3_compatible_mode;
    bool owe = ap->authmode == WIFI_AUTH_OWE;
    bool open = ap->authmode == WIFI_AUTH_OPEN;
    if (!open && !owe && !sae && ap->authmode != WIFI_AUTH_WPA_PSK &&
        ap->authmode != WIFI_AUTH_WPA2_PSK && ap->authmode != WIFI_AUTH_WPA_WPA2_PSK) return ESP_ERR_INVALID_ARG;
    size_t password_length = strnlen((const char *)ap->password, sizeof(ap->password));
    if (open || owe) {
        if (password_length != 0) return ESP_ERR_INVALID_ARG;
    } else if (password_length < (ap->authmode == WIFI_AUTH_WPA3_PSK && !ap->wpa3_compatible_mode ? 1U : 8U) || password_length > 63)
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_ENABLE_WPA3_SAE || !CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
    if (sae) return ESP_ERR_NOT_SUPPORTED;
#endif
#if !CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_SOFTAP
    if (owe) return ESP_ERR_NOT_SUPPORTED;
#endif
    bool tkip = ap->pairwise_cipher == WIFI_CIPHER_TYPE_TKIP || ap->pairwise_cipher == WIFI_CIPHER_TYPE_TKIP_CCMP;
    if (!tkip && ap->pairwise_cipher != WIFI_CIPHER_TYPE_CCMP &&
        ap->pairwise_cipher != WIFI_CIPHER_TYPE_GCMP && ap->pairwise_cipher != WIFI_CIPHER_TYPE_GCMP256)
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_GCMP_SUPPORT
    if (ap->pairwise_cipher == WIFI_CIPHER_TYPE_GCMP || ap->pairwise_cipher == WIFI_CIPHER_TYPE_GCMP256)
        return ESP_ERR_NOT_SUPPORTED;
#endif
    if ((ap->authmode == WIFI_AUTH_WPA_PSK && !tkip) ||
        (!ap->pmf_cfg.capable && (ap->pmf_cfg.required || sae || owe)) ||
        ((sae || owe || ap->pmf_cfg.required) && tkip) ||
        ((ap->authmode == WIFI_AUTH_WPA3_PSK || owe) && !ap->pmf_cfg.required) ||
        (open && (ap->pmf_cfg.required || ap->gtk_rekey_interval != 0)) ||
        (ap->transition_disable && !sae)) return ESP_ERR_INVALID_ARG;
    if (ap->sae_pwe_h2e != WPA3_SAE_PWE_UNSPECIFIED) {
        if (!sae || (ap->sae_pwe_h2e != WPA3_SAE_PWE_HUNT_AND_PECK &&
            ap->sae_pwe_h2e != WPA3_SAE_PWE_HASH_TO_ELEMENT && ap->sae_pwe_h2e != WPA3_SAE_PWE_BOTH))
            return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_ENABLE_SAE_H2E
        if (ap->sae_pwe_h2e != WPA3_SAE_PWE_HUNT_AND_PECK) return ESP_ERR_NOT_SUPPORTED;
#endif
    }
#if !CONFIG_ESP_WIFI_FTM_ENABLE || !CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT
    if (ap->ftm_responder) return ESP_ERR_NOT_SUPPORTED;
#endif
    if (ap->sae_ext && (ap->wpa3_compatible_mode || ap->authmode != WIFI_AUTH_WPA3_PSK ||
        ap->pairwise_cipher != WIFI_CIPHER_TYPE_GCMP256 ||
        (ap->sae_pwe_h2e != WPA3_SAE_PWE_HASH_TO_ELEMENT && ap->sae_pwe_h2e != WPA3_SAE_PWE_BOTH)))
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_SOC_WIFI_GCMP_SUPPORT || !CONFIG_ESP_WIFI_GCMP_SUPPORT || !CONFIG_ESP_WIFI_ENABLE_SAE_H2E
    if (ap->sae_ext) return ESP_ERR_NOT_SUPPORTED;
#endif
    /* Compatible mode explicitly authorizes the SDK's WPA2/CCMP base profile
     * override, with SAE supplied through RSN override for compatible peers. */
    if (ap->wpa3_compatible_mode &&
        ((ap->authmode != WIFI_AUTH_WPA2_PSK && ap->authmode != WIFI_AUTH_WPA3_PSK &&
          ap->authmode != WIFI_AUTH_WPA2_WPA3_PSK) || ap->pairwise_cipher != WIFI_CIPHER_TYPE_CCMP))
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
    if (ap->wpa3_compatible_mode) return ESP_ERR_NOT_SUPPORTED;
#endif
    if ((ap->bss_max_idle_cfg.period != 0 && ap->bss_max_idle_cfg.period < 10) ||
        (ap->bss_max_idle_cfg.protected_keep_alive && (ap->bss_max_idle_cfg.period == 0 || open)))
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT
    if (ap->bss_max_idle_cfg.period != 0 || ap->bss_max_idle_cfg.protected_keep_alive)
        return ESP_ERR_NOT_SUPPORTED;
#endif
    return ESP_OK;
#endif
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Compare public semantic fields from the pinned SDK, excluding reserved bits
 * and compiler padding. These are rollback snapshots, not JS config inputs. */
static bool wifi_radio_config_equal(wifi_interface_t interface,
    const wifi_config_t *a, const wifi_config_t *b)
{
#define EQ(field) (a->field == b->field)
#define BYTES(field) (memcmp(a->field, b->field, sizeof(a->field)) == 0)
    if (interface == WIFI_IF_STA) {
        return BYTES(sta.ssid) && BYTES(sta.password) && BYTES(sta.bssid) &&
            BYTES(sta.sae_h2e_identifier) && EQ(sta.scan_method) && EQ(sta.bssid_set) &&
            EQ(sta.channel) && EQ(sta.listen_interval) && EQ(sta.sort_method) &&
            EQ(sta.threshold.rssi) && EQ(sta.threshold.authmode) && EQ(sta.threshold.rssi_5g_adjustment) &&
            EQ(sta.pmf_cfg.capable) && EQ(sta.pmf_cfg.required) &&
            EQ(sta.rm_enabled) && EQ(sta.btm_enabled) && EQ(sta.mbo_enabled) &&
            EQ(sta.ft_enabled) && EQ(sta.owe_enabled) && EQ(sta.transition_disable) &&
            EQ(sta.disable_wpa3_compatible_mode) && EQ(sta.sae_pwe_h2e) &&
            EQ(sta.sae_pk_mode) && EQ(sta.failure_retry_cnt) && EQ(sta.he_dcm_set) &&
            EQ(sta.he_dcm_max_constellation_tx) && EQ(sta.he_dcm_max_constellation_rx) &&
            EQ(sta.he_mcs9_enabled) && EQ(sta.he_su_beamformee_disabled) &&
            EQ(sta.he_trig_su_bmforming_feedback_disabled) &&
            EQ(sta.he_trig_mu_bmforming_partial_feedback_disabled) &&
            EQ(sta.he_trig_cqi_feedback_disabled) && EQ(sta.vht_su_beamformee_disabled) &&
            EQ(sta.vht_mu_beamformee_disabled) && EQ(sta.vht_mcs8_enabled);
    }
    return BYTES(ap.ssid) && BYTES(ap.password) && EQ(ap.ssid_len) && EQ(ap.channel) &&
        EQ(ap.authmode) && EQ(ap.ssid_hidden) && EQ(ap.max_connection) &&
        EQ(ap.beacon_interval) && EQ(ap.csa_count) && EQ(ap.dtim_period) &&
        EQ(ap.pairwise_cipher) && EQ(ap.ftm_responder) && EQ(ap.pmf_cfg.capable) &&
        EQ(ap.pmf_cfg.required) && EQ(ap.sae_pwe_h2e) && EQ(ap.transition_disable) &&
        EQ(ap.sae_ext) && EQ(ap.wpa3_compatible_mode) && EQ(ap.bss_max_idle_cfg.period) &&
        EQ(ap.bss_max_idle_cfg.protected_keep_alive) && EQ(ap.gtk_rekey_interval);
#undef BYTES
#undef EQ
}

bool esp32_mquickjs_wifi_radio_accept_station_config(const wifi_config_t *requested,
    const wifi_config_t *actual)
{
    return requested != NULL && actual != NULL && wifi_radio_config_equal(WIFI_IF_STA, requested, actual);
}

enum { RESTART_CONFIG_RAM,
    RESTART_CONFIG_DISABLE,
    RESTART_CONFIG_DISABLE_READ,
    RESTART_CONFIG_COUNTRY,
    RESTART_CONFIG_COUNTRY_READ,
    RESTART_CONFIG_EVENT_MASK,
    RESTART_CONFIG_EVENT_MASK_READ,
    RESTART_CONFIG_MAC_MODE,
    RESTART_CONFIG_MAC_MODE_READ,
    RESTART_CONFIG_MAC_TEMP,
    RESTART_CONFIG_MAC_AP,
    RESTART_CONFIG_MAC_AP_READ,
    RESTART_CONFIG_MAC_STA,
    RESTART_CONFIG_MAC_STA_READ,
    RESTART_CONFIG_ENABLE,
    RESTART_CONFIG_STA_WRITE,
    RESTART_CONFIG_STA_READ,
    RESTART_CONFIG_AP_WRITE,
    RESTART_CONFIG_AP_READ,
    RESTART_CONFIG_PHY_MODE,
    RESTART_CONFIG_STA_PHY_WRITE,
    RESTART_CONFIG_STA_PHY_READ,
    RESTART_CONFIG_AP_PHY_WRITE,
    RESTART_CONFIG_AP_PHY_READ,
    RESTART_CONFIG_BAND_RESTORE,
    RESTART_CONFIG_PS,
    RESTART_CONFIG_PS_READ,
    RESTART_CONFIG_MODE,
    RESTART_CONFIG_MODE_READ,
    RESTART_CONFIG_RATES,
    RESTART_CONFIG_INTERVAL,
    RESTART_CONFIG_DONE };

static bool wifi_radio_restart_configs_owner(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return token != NULL && token->identity != 0U && token->identity == s_radio.lifecycle.identity &&
        token->generation == s_radio.lifecycle.generation;
}

static esp_err_t wifi_radio_restart_configs_record(const char *stage, esp_err_t err, bool mutated)
{
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = (esp32_mquickjs_wifi_radio_config_result_t){
        .stage = stage, .error = err, .mutation_attempted = mutated};
    taskEXIT_CRITICAL(&s_radio.lock);
    if (err != ESP_OK && mutated) (void)wifi_radio_record_fault(stage, err);
    return err;
}

static void wifi_radio_restart_configs_discard_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (token == NULL || s_config_restart.owner.identity != token->identity ||
        s_config_restart.owner.generation != token->generation) return;
    if (s_config_restart.snapshot != NULL) {
        esp32_mquickjs_wireless_secure_zero(s_config_restart.snapshot, sizeof(*s_config_restart.snapshot));
        esp32_mquickjs_memory_payload_free(s_config_restart.snapshot);
    }
    memset(&s_config_restart, 0, sizeof(s_config_restart));
}

/* Full PHY reads require AUTO on C5. Capture preparation retains the visible
 * predecessor before exposing the hidden band; never substitute defaults. */
static esp_err_t wifi_radio_restart_phy_mode(void)
{
    wifi_band_mode_t mode;
    esp_err_t err = esp_wifi_get_band_mode(&mode);
    if (err != ESP_OK) return err;
    if (mode != WIFI_BAND_MODE_2G_ONLY && mode != WIFI_BAND_MODE_5G_ONLY && mode != WIFI_BAND_MODE_AUTO)
        return ESP_ERR_INVALID_RESPONSE;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    return mode == WIFI_BAND_MODE_AUTO ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
#else
    return mode == WIFI_BAND_MODE_2G_ONLY ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
#endif
}

static esp_err_t wifi_radio_restart_band_mode_read(wifi_band_mode_t wanted)
{
    wifi_band_mode_t actual;
    esp_err_t err = esp_wifi_get_band_mode(&actual);
    return err == ESP_OK && actual != wanted ? ESP_ERR_INVALID_RESPONSE : err;
}

/* New driver only, no published owner. Both AUTO preparation and restoring
 * the saved single-band mode require START: use Station, fence the SDK cycle,
 * and finish STOP before enabling the final interface mode. */
static esp_err_t wifi_radio_restart_band_select_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t restore_mode,
    wifi_band_mode_t wanted, const char **stage)
{
    if (!wifi_radio_restart_configs_owner(token) || !s_config_restart.captured ||
        s_config_restart.owner.identity != token->identity || s_config_restart.owner.generation != token->generation ||
        s_config_restart.source_generation == s_radio.generation || !s_radio.driver_owned || !s_radio.storage_configured ||
        s_radio.started || s_radio.stop_required || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
        s_radio.operation.identity != 0U || s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL ||
        (restore_mode != WIFI_MODE_STA && restore_mode != WIFI_MODE_AP && restore_mode != WIFI_MODE_APSTA))
        return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) return ESP_ERR_INVALID_STATE;
    if (wanted != WIFI_BAND_MODE_2G_ONLY && wanted != WIFI_BAND_MODE_5G_ONLY && wanted != WIFI_BAND_MODE_AUTO)
        return ESP_ERR_INVALID_ARG;
    *stage = "restart-band-select-snapshot";
    wifi_band_mode_t current;
    esp_err_t err = esp_wifi_get_band_mode(&current);
    if (err != ESP_OK) return err;
    if (current != WIFI_BAND_MODE_2G_ONLY && current != WIFI_BAND_MODE_5G_ONLY && current != WIFI_BAND_MODE_AUTO)
        return ESP_ERR_INVALID_RESPONSE;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    if (current == wanted) return ESP_OK;
    *stage = "restart-band-select-station";
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = WIFI_MODE_STA; taskEXIT_CRITICAL(&s_radio.lock);
    wifi_mode_t actual;
    *stage = "restart-band-select-station-readback";
    err = esp_wifi_get_mode(&actual);
    if (err == ESP_OK && actual != WIFI_MODE_STA) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) return err;
    *stage = "restart-band-select-start";
    err = wifi_radio_start_stopped_locked(WIFI_MODE_STA, true);
    if (err != ESP_OK) return err;
    /* In this unassociated Station path the fixed SDK mode change restarts
     * the native interface. Observe that cycle separately from our final STOP. */
    *stage = "restart-band-select-cycle-begin";
    err = wifi_radio_begin_events(RADIO_EVENTS_RESTART, WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STARTING);
    *stage = "restart-band-select-write";
    err = esp_wifi_set_band_mode(wanted);
    if (err != ESP_OK) return err;
    *stage = "restart-band-select-readback";
    err = wifi_radio_restart_band_mode_read(wanted);
    if (err != ESP_OK) return err;
    *stage = "restart-band-select-cycle-events";
    err = wifi_radio_wait_events();
    if (err != ESP_OK) return err;
    wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STARTED);
    *stage = "restart-band-select-stop";
    err = wifi_radio_stop_locked();
    if (err != ESP_OK) return err;
    *stage = "restart-band-select-stopped-readback";
    err = wifi_radio_restart_band_mode_read(wanted);
    if (err != ESP_OK) return err;
    *stage = "restart-band-select-mode";
    err = esp_wifi_set_mode(restore_mode);
    if (err != ESP_OK) return err;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = restore_mode; taskEXIT_CRITICAL(&s_radio.lock);
    *stage = "restart-band-select-mode-readback";
    err = esp_wifi_get_mode(&actual);
    if (err == ESP_OK && actual != restore_mode) err = ESP_ERR_INVALID_RESPONSE;
#else
    if (wanted != WIFI_BAND_MODE_2G_ONLY || current != WIFI_BAND_MODE_2G_ONLY) return ESP_ERR_INVALID_RESPONSE;
#endif
    return err;
}

static esp_err_t wifi_radio_restart_band_prepare_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t restore_mode, const char **stage)
{
#if CONFIG_SOC_WIFI_SUPPORT_5G
    const wifi_band_mode_t wanted = WIFI_BAND_MODE_AUTO;
#else
    const wifi_band_mode_t wanted = WIFI_BAND_MODE_2G_ONLY;
#endif
    return wifi_radio_restart_band_select_locked(token, restore_mode, wanted, stage);
}

static esp_err_t wifi_radio_restart_phy_subset_io(wifi_radio_restart_configs_t *snapshot,
    unsigned index, uint8_t bands, bool verify, uint8_t preserve)
{
    wifi_protocols_t protocols;
    wifi_bandwidths_t bandwidths;
    esp_err_t err = wifi_radio_read_phy(index == 0 ? WIFI_IF_STA : WIFI_IF_AP, &protocols, &bandwidths);
    if (err != ESP_OK) return err;
    for (unsigned band = 1; band <= bands; band <<= 1) {
        if (!(bands & band)) continue;
        uint16_t protocol = band == 1 ? protocols.ghz_2g : protocols.ghz_5g;
        wifi_bandwidth_t width = band == 1 ? bandwidths.ghz_2g : bandwidths.ghz_5g;
        if (wifi_radio_validate_protocol(protocol, band == 2) != ESP_OK ||
            (width != WIFI_BW20 && width != WIFI_BW40) ||
            (width == WIFI_BW40 && (!(protocol & WIFI_PROTOCOL_11N) || (protocol & (WIFI_PROTOCOL_11AC | WIFI_PROTOCOL_11AX)))))
            return ESP_ERR_INVALID_RESPONSE;
    }
    if (verify)
        return wifi_radio_phy_equal(bands, &snapshot->protocols[index], &snapshot->bandwidths[index],
            &protocols, &bandwidths) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    if (preserve != 0U && !wifi_radio_phy_equal(preserve, &snapshot->protocols[index],
        &snapshot->bandwidths[index], &protocols, &bandwidths)) return ESP_ERR_INVALID_RESPONSE;
    snapshot->protocols[index] = protocols;
    snapshot->bandwidths[index] = bandwidths;
    return ESP_OK;
}

/* Capture saved policy without a temporary START/AUTO cycle while the native
 * Action/ROC operation remains owned. The pinned C5 SDK adapter supplies the
 * otherwise hidden band; single-band targets use their ordinary getter. */
static esp_err_t wifi_radio_restart_recovery_phy_read(wifi_radio_restart_configs_t *snapshot, unsigned index)
{
    esp32_mquickjs_wifi_saved_phy_t saved = {0};
#if CONFIG_SOC_WIFI_SUPPORT_5G
    const uint8_t bands = 3U;
    esp_err_t err = esp32_mquickjs_wifi_action_sdk_saved_phy(index == 0 ? WIFI_IF_STA : WIFI_IF_AP, &saved);
#else
    const uint8_t bands = 1U;
    esp_err_t err = wifi_radio_read_phy(index == 0 ? WIFI_IF_STA : WIFI_IF_AP, &saved.protocols, &saved.bandwidths);
#endif
    if (err != ESP_OK) return err;
    for (unsigned band = 1; band <= bands; band <<= 1) {
        uint16_t protocol = band == 1 ? saved.protocols.ghz_2g : saved.protocols.ghz_5g;
        wifi_bandwidth_t width = band == 1 ? saved.bandwidths.ghz_2g : saved.bandwidths.ghz_5g;
        if (wifi_radio_validate_protocol(protocol, band == 2) != ESP_OK ||
            (width != WIFI_BW20 && width != WIFI_BW40) ||
            (width == WIFI_BW40 && (!(protocol & WIFI_PROTOCOL_11N) || (protocol & (WIFI_PROTOCOL_11AC | WIFI_PROTOCOL_11AX)))))
            return ESP_ERR_INVALID_RESPONSE;
    }
    snapshot->protocols[index] = saved.protocols;
    snapshot->bandwidths[index] = saved.bandwidths;
    return ESP_OK;
}

static esp_err_t wifi_radio_restart_phy_io(wifi_radio_restart_configs_t *snapshot, unsigned index, bool verify)
{
#if CONFIG_SOC_WIFI_SUPPORT_5G
    const uint8_t bands = 3U;
#else
    const uint8_t bands = 1U;
#endif
    return wifi_radio_restart_phy_subset_io(snapshot, index, bands, verify, 0U);
}

static esp_err_t wifi_radio_restart_phy_read(wifi_radio_restart_configs_t *snapshot, bool verify, const char **stage)
{
    *stage = verify ? "restart-phy-band-final-readback" : "restart-phy-band-snapshot";
    esp_err_t err = verify ? wifi_radio_restart_band_mode_read(snapshot->band_mode) : wifi_radio_restart_phy_mode();
    if (err != ESP_OK) return err;
    for (unsigned i = 0; i < 2; ++i) {
        if (!(snapshot->mask & (1U << i))) continue;
        *stage = i == 0 ? (verify ? "restart-station-phy-final-readback" : "restart-station-phy-snapshot")
                        : (verify ? "restart-ap-phy-final-readback" : "restart-ap-phy-snapshot");
        /* Both bands were read back in AUTO before selecting the saved mode.
         * Final single-band getters can only prove the visible band's fields. */
        err = verify ? wifi_radio_restart_phy_subset_io(snapshot, i, snapshot->visible_bands, true, 0U)
                     : wifi_radio_restart_phy_io(snapshot, i, false);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

/* Inactive-time getters are START-only and reject disabled interfaces. Capture
 * only the original active mode; never manufacture values for hidden slots.
 * The caller freezes this before any temporary Station/band preparation. */
static esp_err_t wifi_radio_restart_inactive_read(wifi_radio_restart_configs_t *snapshot,
    uint8_t mask, bool verify, const char **stage)
{
    *stage = "restart-inactive-admission";
    if (!s_radio.started ||
        (mask & ~((uint8_t)s_radio.effective_mode)) != 0U ||
        (mask & ~snapshot->mask) != 0U) return ESP_ERR_INVALID_STATE;
    uint16_t observed[2] = {0};
    for (unsigned i = 0; i < 2; ++i) {
        if (!(mask & (1U << i))) continue;
        *stage = i == 0 ? (verify ? "restart-station-inactive-final-readback" : "restart-station-inactive-snapshot")
                        : (verify ? "restart-ap-inactive-final-readback" : "restart-ap-inactive-snapshot");
        esp_err_t err = esp_wifi_get_inactive_time(i == 0 ? WIFI_IF_STA : WIFI_IF_AP, &observed[i]);
        if (err == ESP_OK && (observed[i] < (i == 0 ? 3 : 10) ||
            (verify && observed[i] != snapshot->inactive_time[i]))) err = ESP_ERR_INVALID_RESPONSE;
        wifi_radio_inactive_history_record(i, observed[i], err);
        if (err != ESP_OK) return err;
    }
    if (!verify) memcpy(snapshot->inactive_time, observed, sizeof(observed));
    return ESP_OK;
}

static esp_err_t wifi_radio_restart_globals_read(wifi_radio_restart_configs_t *snapshot,
    bool verify, const char **stage)
{
    wifi_country_t country = {0};
    *stage = verify ? "restart-country-final-readback" : "restart-country-snapshot";
    esp_err_t err = esp_wifi_get_country(&country);
    if (err == ESP_OK && (!wifi_radio_country_valid(&country) ||
        (verify && !wifi_radio_country_equal(&snapshot->country, &country, false)))) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) return err;
    if (!verify) snapshot->country = country;
    wifi_ps_type_t power_save;
    *stage = verify ? "restart-power-save-final-readback" : "restart-power-save-snapshot";
    err = esp_wifi_get_ps(&power_save);
    if (err == ESP_OK && (power_save != WIFI_PS_NONE && power_save != WIFI_PS_MIN_MODEM && power_save != WIFI_PS_MAX_MODEM))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK && verify && power_save != snapshot->power_save) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) return err;
    if (!verify) snapshot->power_save = power_save;
    uint32_t event_mask;
    *stage = verify ? "restart-event-mask-final-readback" : "restart-event-mask-snapshot";
    err = esp_wifi_get_event_mask(&event_mask);
    if (err == ESP_OK && ((event_mask & ~((uint32_t)WIFI_EVENT_MASK_AP_PROBEREQRECVED)) != 0U ||
        (verify && event_mask != snapshot->event_mask))) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) return err;
    if (!verify) snapshot->event_mask = event_mask;
    for (unsigned i = 0; i < 2; ++i) {
        if (!(snapshot->mask & (1U << i))) continue;
        uint8_t mac[6];
        *stage = i == 0 ? (verify ? "restart-station-mac-final-readback" : "restart-station-mac-snapshot")
                        : (verify ? "restart-ap-mac-final-readback" : "restart-ap-mac-snapshot");
        err = esp_wifi_get_mac(i == 0 ? WIFI_IF_STA : WIFI_IF_AP, mac);
        if (err == ESP_OK && ((mac[0] & 1U) || memcmp(mac, "\0\0\0\0\0\0", 6) == 0 ||
            (verify && memcmp(mac, snapshot->mac[i], 6) != 0))) err = ESP_ERR_INVALID_RESPONSE;
        if (err != ESP_OK) return err;
        if (!verify) memcpy(snapshot->mac[i], mac, 6);
    }
    if ((snapshot->mask & WIFI_MODE_AP) && memcmp(snapshot->mac[0], snapshot->mac[1], 6) == 0)
        return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

/* Both interfaces are selected while the physical driver remains stopped.
 * An unused local address frees the STA address
 * before restoring AP, so a valid swapped STA/AP pair survives factory reset.
 * The temporary address is never used by a started interface. */
static esp_err_t wifi_radio_restart_mac_temporary(const wifi_radio_restart_configs_t *snapshot,
    const char **stage)
{
    uint8_t current[3][6], temporary[6], actual[6];
    unsigned count = 2;
    *stage = "restart-mac-temporary-snapshot";
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, current[0]);
    if (err == ESP_OK) err = esp_wifi_get_mac(WIFI_IF_AP, current[1]);
    if (err != ESP_OK) return err;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
    err = esp_wifi_get_mac(WIFI_IF_NAN, current[2]);
    if (err != ESP_OK) return err;
    count = 3;
    if (memcmp(current[2], snapshot->mac[0], 6) == 0 || memcmp(current[2], snapshot->mac[1], 6) == 0)
        return ESP_ERR_INVALID_STATE;
#endif
    memcpy(temporary, snapshot->mac[0], 6);
    temporary[0] = (temporary[0] | 2U) & 0xfeU;
    bool found = false;
    for (unsigned suffix = 0; suffix < 256; ++suffix) {
        temporary[5] = (uint8_t)(snapshot->mac[0][5] + suffix);
        bool conflict = false;
        for (unsigned i = 0; i < count; ++i) conflict |= memcmp(temporary, current[i], 6) == 0;
        for (unsigned i = 0; i < 2; ++i) conflict |= memcmp(temporary, snapshot->mac[i], 6) == 0;
        if (!conflict) { found = true; break; }
    }
    if (!found) return ESP_ERR_INVALID_STATE;
    *stage = "restart-mac-temporary-write";
    err = esp_wifi_set_mac(WIFI_IF_STA, temporary);
    if (err != ESP_OK) return err;
    *stage = "restart-mac-temporary-readback";
    err = esp_wifi_get_mac(WIFI_IF_STA, actual);
    return err == ESP_OK && memcmp(actual, temporary, 6) != 0 ? ESP_ERR_INVALID_RESPONSE : err;
}

/* The current and home channel must describe the same idle radio. This does
 * not invent an observation of the inactive band's hidden home channel.
 * The pinned C5 SDK writes only the low byte of each secondary-channel enum;
 * initialize the complete outputs before native access, as in channel refresh. */
static esp_err_t wifi_radio_restart_channel_read(wifi_radio_restart_configs_t *snapshot, bool verify)
{
    wifi_band_mode_t mode;
    wifi_band_t band;
    uint8_t primary, home;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE, home_secondary = WIFI_SECOND_CHAN_NONE;
    esp_err_t err = wifi_radio_band_snapshot(&mode, &band);
    if (err == ESP_OK) err = esp_wifi_get_channel(&primary, &secondary);
    if (err == ESP_OK) err = esp_wifi_get_home_channel(&home, &home_secondary);
    if (err != ESP_OK) return err;
    if ((secondary != WIFI_SECOND_CHAN_NONE && secondary != WIFI_SECOND_CHAN_ABOVE && secondary != WIFI_SECOND_CHAN_BELOW) ||
        (band == WIFI_BAND_2G ? primary < 1 || primary > 14 : esp32_mquickjs_wifi_radio_5ghz_channel_bit(primary) == 0U))
        return ESP_ERR_INVALID_RESPONSE;
    if (primary != home || secondary != home_secondary) return ESP_ERR_INVALID_STATE;
    if (verify)
        return snapshot->band_mode == mode && snapshot->band == band && snapshot->primary == primary &&
            snapshot->secondary == secondary ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    snapshot->band_mode = mode;
    snapshot->band = band;
    snapshot->primary = primary;
    snapshot->secondary = secondary;
    snapshot->visible_bands = mode == WIFI_BAND_MODE_2G_ONLY ? 1U : mode == WIFI_BAND_MODE_5G_ONLY ? 2U : 3U;
    return ESP_OK;
}

/* Recovery returns to the SDK's home channel, not the current off-channel
 * residency. The runtime must quiesce managed scan/connect/AP transitions before
 * capture; repeat this observation before freezing the complete checkpoint. */
static esp_err_t wifi_radio_restart_recovery_home_read(wifi_radio_restart_configs_t *snapshot, bool verify)
{
    wifi_band_mode_t mode;
    uint8_t home;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    esp_err_t err = esp_wifi_get_band_mode(&mode);
    if (err == ESP_OK) err = esp_wifi_get_home_channel(&home, &secondary);
    if (err != ESP_OK) return err;
    if ((mode != WIFI_BAND_MODE_2G_ONLY && mode != WIFI_BAND_MODE_5G_ONLY && mode != WIFI_BAND_MODE_AUTO) ||
        (secondary != WIFI_SECOND_CHAN_NONE && secondary != WIFI_SECOND_CHAN_ABOVE && secondary != WIFI_SECOND_CHAN_BELOW) ||
        home == 0U || (home > 14 && esp32_mquickjs_wifi_radio_5ghz_channel_bit(home) == 0U) ||
        (mode == WIFI_BAND_MODE_2G_ONLY && home > 14) || (mode == WIFI_BAND_MODE_5G_ONLY && home <= 14))
        return ESP_ERR_INVALID_RESPONSE;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
    if (mode != WIFI_BAND_MODE_2G_ONLY || home > 14) return ESP_ERR_INVALID_RESPONSE;
#endif
    wifi_band_t band = home > 14 ? WIFI_BAND_5G : WIFI_BAND_2G;
    if (verify) return snapshot->band_mode == mode && snapshot->band == band &&
        snapshot->primary == home && snapshot->secondary == secondary ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    snapshot->band_mode = mode;
    snapshot->band = band;
    snapshot->primary = home;
    snapshot->secondary = secondary;
    snapshot->visible_bands = mode == WIFI_BAND_MODE_2G_ONLY ? 1U : mode == WIFI_BAND_MODE_5G_ONLY ? 2U : 3U;
    return ESP_OK;
}

enum { RESTART_CAPTURE_RAM, RESTART_CAPTURE_STOP, RESTART_CAPTURE_MODE, RESTART_CAPTURE_MODE_READ,
    RESTART_CAPTURE_START, RESTART_CAPTURE_START_EVENTS, RESTART_CAPTURE_AUTO, RESTART_CAPTURE_AUTO_READ,
    RESTART_CAPTURE_AUTO_EVENTS, RESTART_CAPTURE_STA_PHY, RESTART_CAPTURE_AP_PHY, RESTART_CAPTURE_DONE,
    RESTART_CAPTURE_FAILED };

/* Old physical generation, exact lifecycle owner, no published leases. Once
 * preparation begins, retain the original secret snapshot until replay or
 * explicit successful cleanup. Retry only known read/event/STOP suffixes;
 * an uncertain configuration/START write requires explicit cleanup. */
static esp_err_t wifi_radio_restart_capture_continue_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    wifi_radio_restart_configs_t *snapshot = s_config_restart.snapshot;
    if (!wifi_radio_restart_configs_owner(token) || snapshot == NULL || s_config_restart.captured ||
        s_config_restart.owner.identity != token->identity || s_config_restart.owner.generation != token->generation ||
        s_config_restart.mode != mode || s_config_restart.source_generation != s_radio.generation ||
        !s_radio.driver_owned || !s_radio.storage_configured || s_radio.restart_required ||
        s_radio.operation.identity != 0U || s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending) return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) return ESP_ERR_INVALID_STATE;
    if (s_config_restart.snapshot->capture_phase == RESTART_CAPTURE_FAILED)
        return wifi_radio_restart_configs_record(s_config_restart.snapshot->capture_stage, s_config_restart.snapshot->capture_error, true);
#if CONFIG_SOC_WIFI_SUPPORT_5G
    const char *stage = "restart-capture-phase";
    esp_err_t err = ESP_ERR_INVALID_STATE;
    while (s_config_restart.snapshot->capture_phase < RESTART_CAPTURE_DONE) {
        bool uncertain_write = false;
        switch (s_config_restart.snapshot->capture_phase) {
        case RESTART_CAPTURE_RAM:
            stage = "restart-capture-ram";
            uncertain_write = true;
            err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
            if (err == ESP_OK) {
                taskENTER_CRITICAL(&s_radio.lock); s_radio.storage = WIFI_STORAGE_RAM; taskEXIT_CRITICAL(&s_radio.lock);
            }
            break;
        case RESTART_CAPTURE_STOP:
            stage = "restart-capture-stop";
            err = wifi_radio_stop_locked();
            break;
        case RESTART_CAPTURE_MODE:
            stage = "restart-capture-station";
            uncertain_write = true;
            err = esp_wifi_set_mode(WIFI_MODE_STA);
            if (err == ESP_OK) {
                taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = WIFI_MODE_STA; taskEXIT_CRITICAL(&s_radio.lock);
            }
            break;
        case RESTART_CAPTURE_MODE_READ: {
            wifi_mode_t actual;
            stage = "restart-capture-station-readback";
            err = esp_wifi_get_mode(&actual);
            if (err == ESP_OK && actual != WIFI_MODE_STA) err = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        case RESTART_CAPTURE_START:
            stage = "restart-capture-start";
            /* This temporary, unleased START only exposes hidden PHY state.
             * The exact checkpoint already owns the settings. Keep their
             * pending flags and the original fault until physical rebuild;
             * normal activation gates must not block known capture suffixes. */
            err = wifi_radio_start_stopped_locked(WIFI_MODE_STA, false);
            if (s_radio.started) {
                /* Accepted native START: even on timeout, never submit again. */
                s_config_restart.snapshot->capture_phase = RESTART_CAPTURE_START_EVENTS;
                if (err == ESP_OK) { ++s_config_restart.snapshot->capture_phase; continue; }
            } else {
                uncertain_write = s_radio.stop_required;
            }
            break;
        case RESTART_CAPTURE_START_EVENTS:
            stage = "restart-capture-start-events";
            err = wifi_radio_wait_events();
            break;
        case RESTART_CAPTURE_AUTO:
            stage = "restart-capture-cycle-begin";
            err = wifi_radio_begin_events(RADIO_EVENTS_RESTART, WIFI_MODE_STA);
            if (err != ESP_OK) break;
            wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STARTING);
            stage = "restart-capture-auto";
            uncertain_write = true;
            err = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
            break;
        case RESTART_CAPTURE_AUTO_READ:
            stage = "restart-capture-auto-readback";
            err = wifi_radio_restart_phy_mode();
            break;
        case RESTART_CAPTURE_AUTO_EVENTS:
            stage = "restart-capture-cycle-events";
            err = wifi_radio_wait_events();
            if (err == ESP_OK) wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STARTED);
            break;
        case RESTART_CAPTURE_STA_PHY:
        case RESTART_CAPTURE_AP_PHY: {
            unsigned index = s_config_restart.snapshot->capture_phase - RESTART_CAPTURE_STA_PHY;
            stage = index == 0 ? "restart-capture-station-phy" : "restart-capture-ap-phy";
            err = !(snapshot->mask & (1U << index)) ? ESP_OK :
                wifi_radio_restart_phy_subset_io(snapshot, index, 3U, false, snapshot->visible_bands);
            break;
        }
        default:
            return ESP_ERR_INVALID_STATE;
        }
        if (err != ESP_OK) {
            if (uncertain_write) {
                s_config_restart.snapshot->capture_phase = RESTART_CAPTURE_FAILED;
                s_config_restart.snapshot->capture_stage = stage;
                s_config_restart.snapshot->capture_error = err;
            }
            return wifi_radio_restart_configs_record(stage, err, true);
        }
        ++s_config_restart.snapshot->capture_phase;
    }
    s_config_restart.captured = true;
    return ESP_OK;
#else
    return ESP_ERR_INVALID_STATE;
#endif
}

static esp_err_t wifi_radio_restart_configs_capture_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    const char *stage = "restart-config-admission";
    esp_err_t err = ESP_ERR_INVALID_STATE;
    bool source_mutated = false;
    bool raw_recovery = wifi_radio_raw_tx_recovery_exact_locked(token);
    if (!wifi_radio_restart_configs_owner(token) ||
        (s_tx_rate_lease.identity != 0U && !raw_recovery) || s_tx_rate_lease.restore_pending) goto failed;
    if (s_config_restart.captured) {
        if (s_config_restart.owner.identity != token->identity ||
            s_config_restart.owner.generation != token->generation || s_config_restart.mode != mode)
            goto failed;
        /* Every new physical attempt consumes baseline plus replay revisions.
         * Keep frozen intent on failure; never recapture uncertain driver state. */
        if (s_config_restart.snapshot != NULL && s_interval.revision > UINT32_MAX - 2U)
            return wifi_radio_restart_configs_record("restart-interval-capacity", err, false);
        if (s_config_restart.snapshot != NULL &&
            !esp32_mquickjs_wifi_tx_rate_replay_capacity(&s_tx_rates, &s_config_restart.snapshot->rates))
            return wifi_radio_restart_configs_record("restart-tx-rate-capacity", err, false);
        return ESP_OK;
    }
    if (s_config_restart.snapshot != NULL)
        return wifi_radio_restart_capture_continue_locked(token, mode);
    bool recovery = wifi_radio_action_recovery_exact_locked(token);
    uint32_t recovery_owner = recovery ? wifi_radio_action_recovery_capture_owner_locked(token) : 0U;
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (wifi_radio_ftm_recovery_exact_locked(token)) {
        recovery = true;
        recovery_owner = wifi_radio_ftm_recovery_capture_owner_locked(token);
    }
#endif
    if (raw_recovery) {
        recovery = true;
        recovery_owner = wifi_radio_raw_tx_recovery_capture_owner_locked(token);
        if (recovery_owner == 0U) goto failed;
    }
    bool twt_recovery = false;
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    twt_recovery = wifi_radio_twt_recovery_exact_locked(token);
    if (twt_recovery) {
        if (!wifi_radio_twt_recovery_owners_locked()) goto failed;
        recovery = true;
    }
#endif
    if ((s_radio.operation.identity != 0U && recovery_owner == 0U && !twt_recovery) || s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required) goto failed;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (!twt_recovery && s_radio.leases[i].identity != 0U && s_radio.leases[i].identity != recovery_owner) goto failed;
    /* Cold initialization has no previous SDK config, and allocates no secret
     * snapshot. An uncertain init still fails the existing shutdown boundary. */
    if (!s_radio.driver_owned) {
        s_config_restart.owner = *token;
        s_config_restart.mode = mode;
        s_config_restart.source_generation = s_radio.generation;
        s_config_restart.captured = true;
        return ESP_OK;
    }
    if (!s_radio.storage_configured || (s_radio.storage != WIFI_STORAGE_RAM && s_radio.storage != WIFI_STORAGE_FLASH))
        goto failed;
    bool stopped_source = !s_radio.started && wifi_radio_stop_snapshot_unchanged_locked() &&
        s_stop_snapshot.mode == (uint8_t)s_radio.effective_mode;
    bool start_source = !s_radio.started && !stopped_source;
    if (start_source && (s_radio.stop_required || s_radio.stop_submitted ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
        s_radio.event_phase != RADIO_EVENTS_IDLE || s_radio.event_live != 0U || recovery)) {
        stage = "restart-source-admission";
        goto failed;
    }
    bool restore_off = s_radio.effective_mode == WIFI_MODE_NULL;
    if (restore_off && (!start_source || (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA))) {
        stage = "restart-inactive-mode-admission";
        goto failed;
    }
    stage = "restart-config-allocate";
    wifi_radio_restart_configs_t *snapshot = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*snapshot), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (snapshot == NULL) { err = ESP_ERR_NO_MEM; goto failed; }
    snapshot->storage = s_radio.storage;
    snapshot->restore_off = restore_off;
    snapshot->mask = WIFI_MODE_STA;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    snapshot->mask |= WIFI_MODE_AP;
#endif
    stage = "restart-tx-rate-snapshot";
    err = raw_recovery ? esp32_mquickjs_wifi_tx_rate_recovery_capture(&s_tx_rates,
        s_radio.generation, recovery_owner, &s_tx_rate_lease, &snapshot->rates)
        : esp32_mquickjs_wifi_tx_rate_restart_capture(&s_tx_rates, s_radio.generation, &snapshot->rates);
    if (err != ESP_OK) goto discard;
    stage = "restart-interval-snapshot";
    err = esp32_mquickjs_wifi_interval_capture(&s_interval, s_radio.generation, &snapshot->interval);
    if (err != ESP_OK) goto discard;
    wifi_mode_t actual;
    stage = "restart-config-mode-snapshot";
    err = esp_wifi_get_mode(&actual);
    if (err == ESP_OK && actual != s_radio.effective_mode) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) goto discard;
    for (unsigned i = 0; i < 2; ++i) {
        if (!(snapshot->mask & (1U << i))) continue;
        stage = i == 0 ? "restart-station-snapshot" : "restart-ap-snapshot";
        /* The getter can read inactive interfaces too; no mode mutation is
         * needed for capture. Setter restoration enables them while stopped. */
        err = esp_wifi_get_config(i == 0 ? WIFI_IF_STA : WIFI_IF_AP, &snapshot->saved[i]);
        if (err != ESP_OK) goto discard;
    }
    err = wifi_radio_restart_globals_read(snapshot, false, &stage);
    if (err != ESP_OK) goto discard;
    if (start_source) {
        /* A healthy configured driver may never have run, or a stopped write
         * may have invalidated its old RF observations. The explicit restart
         * owns the source START too; no owner is published or STA connected.
         * Allocate and validate all readable intent before touching the SDK.
         * As with replay, use RAM so a temporary AP START cannot commit NVS. */
        if (s_radio.storage != WIFI_STORAGE_RAM) {
            stage = "restart-source-ram";
            source_mutated = true;
            taskENTER_CRITICAL(&s_radio.lock); s_radio.storage_configured = false; taskEXIT_CRITICAL(&s_radio.lock);
            err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
            if (err != ESP_OK) goto discard;
            taskENTER_CRITICAL(&s_radio.lock);
            s_radio.storage = WIFI_STORAGE_RAM;
            s_radio.storage_configured = true;
            taskEXIT_CRITICAL(&s_radio.lock);
        }
        if (restore_off) {
            stage = "restart-source-off-mode";
            source_mutated = true;
            err = esp_wifi_set_mode(mode);
            if (err != ESP_OK) goto discard;
            taskENTER_CRITICAL(&s_radio.lock);
            s_radio.effective_mode = mode;
            taskEXIT_CRITICAL(&s_radio.lock);
            stage = "restart-source-off-mode-readback";
            err = esp_wifi_get_mode(&actual);
            if (err == ESP_OK && actual != mode) err = ESP_ERR_INVALID_RESPONSE;
            if (err != ESP_OK) goto discard;
        }
        stage = "restart-source-start";
        err = wifi_radio_start_stopped_locked(actual, true);
        source_mutated |= s_radio.stop_required;
        if (err != ESP_OK) goto discard;
        /* START may normalize SDK configuration. Capture its actual result
         * consistently with the runtime-only power/channel/inactive values,
         * retaining the original storage selection for final commit. */
        stage = "restart-source-mode-readback";
        wifi_mode_t started_mode;
        err = esp_wifi_get_mode(&started_mode);
        if (err == ESP_OK && started_mode != actual) err = ESP_ERR_INVALID_RESPONSE;
        if (err != ESP_OK) goto discard;
        for (unsigned i = 0; i < 2; ++i) {
            if (!(snapshot->mask & (1U << i))) continue;
            stage = i == 0 ? "restart-source-station-readback" : "restart-source-ap-readback";
            err = esp_wifi_get_config(i == 0 ? WIFI_IF_STA : WIFI_IF_AP, &snapshot->saved[i]);
            if (err != ESP_OK) goto discard;
        }
        err = wifi_radio_restart_globals_read(snapshot, false, &stage);
        if (err != ESP_OK) goto discard;
    }
    if (stopped_source) {
        stage = "restart-stop-observations";
        err = wifi_radio_restart_stopped_observations_locked(snapshot, false);
    } else {
        snapshot->inactive_mask = (uint8_t)actual;
        err = wifi_radio_restart_inactive_read(snapshot, snapshot->inactive_mask, false, &stage);
        if (err != ESP_OK) goto discard;
        stage = "restart-tx-power-snapshot";
        err = esp_wifi_get_max_tx_power(&snapshot->tx_power);
        if (err == ESP_OK && (snapshot->tx_power < 8 || snapshot->tx_power > 80)) err = ESP_ERR_INVALID_RESPONSE;
        if (err != ESP_OK) goto discard;
        stage = "restart-band-channel-snapshot";
        err = recovery ? wifi_radio_restart_recovery_home_read(snapshot, false) : wifi_radio_restart_channel_read(snapshot, false);
    }
    if (err != ESP_OK) goto discard;
    for (unsigned i = 0; i < 2; ++i) {
        if (!(snapshot->mask & (1U << i))) continue;
        stage = i == 0 ? "restart-station-visible-phy" : "restart-ap-visible-phy";
        err = recovery ? wifi_radio_restart_recovery_phy_read(snapshot, i)
            : wifi_radio_restart_phy_subset_io(snapshot, i, snapshot->visible_bands, false, 0U);
        if (err != ESP_OK) goto discard;
    }
    stage = "restart-band-channel-snapshot-check";
    err = stopped_source ? wifi_radio_restart_stopped_observations_locked(snapshot, true)
        : recovery ? wifi_radio_restart_recovery_home_read(snapshot, true) : wifi_radio_restart_channel_read(snapshot, true);
    if (err != ESP_OK) goto discard;
    if (!stopped_source) {
        err = wifi_radio_restart_inactive_read(snapshot, snapshot->inactive_mask, true, &stage);
        if (err != ESP_OK) goto discard;
    }
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
    stage = "restart-he-statistics-snapshot";
    if (s_he_statistics.pending) snapshot->he_statistics = s_he_statistics.saved;
    else {
        err = esp32_mquickjs_wifi_he_statistics_snapshot(&snapshot->he_statistics, false);
        if (err != ESP_OK) goto discard;
        s_he_statistics.managed = true;
    }
#endif
    stage = "restart-scan-parameters-snapshot";
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    esp32_mquickjs_wifi_twt_control_t twt_control = {0};
    stage = "restart-twt-policy-snapshot";
    err = esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_READ_CONFIG, &twt_control);
    if (err != ESP_OK) goto discard;
    snapshot->twt_policy = twt_control.config;
    stage = "restart-scan-parameters-snapshot";
#endif
    if (!stopped_source && (actual & WIFI_MODE_STA)) {
        err = wifi_radio_scan_parameters_observe_locked(&snapshot->scan_parameters);
        if (err != ESP_OK) goto discard;
        snapshot->scan_parameters_known = true;
    } else if (s_scan_parameters.generation == s_radio.generation) {
        if (s_scan_parameters.error != ESP_OK) { err = s_scan_parameters.error; goto discard; }
        snapshot->scan_parameters_known = s_scan_parameters.known;
        snapshot->scan_parameters = s_scan_parameters.value;
    }
    snapshot->inactive_saved_mask = snapshot->inactive_mask;
    if (s_inactive_history.generation == s_radio.generation) {
        /* An unavailable hidden observation is not permission to substitute an
         * SDK default. Active observations above may repair their own slots. */
        if (s_inactive_history.failed & snapshot->mask & ~snapshot->inactive_mask & ~s_inactive_history.pending) {
            stage = "restart-hidden-inactive-history";
            err = s_inactive_history.error != ESP_OK ? s_inactive_history.error : ESP_ERR_INVALID_STATE;
            goto discard;
        }
        for (unsigned i = 0; i < 2; ++i) {
            uint8_t bit = (uint8_t)(1U << i);
            if (!(snapshot->mask & bit) || (snapshot->inactive_mask & bit) ||
                !((s_inactive_history.known | s_inactive_history.pending) & bit)) continue;
            snapshot->inactive_time[i] = (s_inactive_history.pending & bit)
                ? s_inactive_history.pending_values[i] : s_inactive_history.values[i];
            snapshot->inactive_saved_mask |= bit;
        }
    }
#if CONFIG_SOC_WIFI_SUPPORT_5G
    if (!stopped_source && snapshot->visible_bands != 3U && (!s_radio.started || !s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)) {
        err = ESP_ERR_INVALID_STATE;
        goto discard;
    }
#endif
    s_config_restart.owner = *token;
    s_config_restart.snapshot = snapshot;
    s_config_restart.source_generation = s_radio.generation;
    s_config_restart.mode = mode;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    if (!recovery && snapshot->visible_bands != 3U)
        return wifi_radio_restart_capture_continue_locked(token, mode);
#endif
    s_config_restart.captured = true;
    return ESP_OK;
discard:
    if (source_mutated) {
        /* No complete checkpoint exists. Keep the partial source and exact
         * failure until physical cleanup; never replay it or repeat START. */
        snapshot->capture_phase = RESTART_CAPTURE_FAILED;
        snapshot->capture_stage = stage;
        snapshot->capture_error = err;
        s_config_restart.owner = *token;
        s_config_restart.snapshot = snapshot;
        s_config_restart.source_generation = s_radio.generation;
        s_config_restart.mode = mode;
    } else {
        esp32_mquickjs_wireless_secure_zero(snapshot, sizeof(*snapshot));
        esp32_mquickjs_memory_payload_free(snapshot);
    }
failed:
    return wifi_radio_restart_configs_record(stage, err, source_mutated);
}

bool esp32_mquickjs_wifi_radio_pmf_disable_allowed(wifi_interface_t interface, const wifi_config_t *config)
{
    if (config == NULL || (interface != WIFI_IF_STA && interface != WIFI_IF_AP)) return false;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return false;
#endif
    wifi_auth_mode_t auth = interface == WIFI_IF_STA ? config->sta.threshold.authmode : config->ap.authmode;
    /* Explicitly reviewed non-WPA3 families only. Never turn a new/unknown
     * security encoding into permission to remove management protection. */
    switch (auth) {
    case WIFI_AUTH_OPEN:
    case WIFI_AUTH_WEP:
    case WIFI_AUTH_WPA_PSK:
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
    case WIFI_AUTH_ENTERPRISE:
    case WIFI_AUTH_WPA_ENTERPRISE:
        break;
    default:
        return false;
    }
    return interface == WIFI_IF_STA ? config->sta.disable_wpa3_compatible_mode :
        !config->ap.wpa3_compatible_mode;
}

/* Only for an SDK-observed predecessor, never new caller input. set_config
 * may ignore capable=false; a disabled predecessor needs the dedicated API
 * after config restoration and before START, followed by full caller readback. */
static esp_err_t wifi_radio_restore_disabled_pmf(wifi_interface_t interface, const wifi_config_t *config)
{
    const wifi_pmf_config_t *pmf = interface == WIFI_IF_STA ? &config->sta.pmf_cfg : &config->ap.pmf_cfg;
    if (pmf->capable || pmf->required) return ESP_OK;
    wifi_config_t observed = {0};
    esp_err_t err = esp_wifi_get_config(interface, &observed);
    const wifi_pmf_config_t *actual = interface == WIFI_IF_STA ? &observed.sta.pmf_cfg : &observed.ap.pmf_cfg;
    bool disabled = !actual->capable && !actual->required;
    esp32_mquickjs_wireless_secure_zero(&observed, sizeof(observed));
    if (err != ESP_OK || disabled) return err;
    if (!esp32_mquickjs_wifi_radio_pmf_disable_allowed(interface, config)) return ESP_ERR_NOT_SUPPORTED;
    return esp_wifi_disable_pmf_config(interface);
}

static esp_err_t wifi_radio_restart_config_io(wifi_radio_restart_configs_t *snapshot,
    unsigned index, bool write)
{
    wifi_interface_t interface = index == 0 ? WIFI_IF_STA : WIFI_IF_AP;
    memset(&snapshot->scratch, 0, sizeof(snapshot->scratch));
    esp_err_t err = ESP_OK;
    if (write) {
        bool apply = true;
        if (interface == WIFI_IF_AP) {
            /* The SDK's untouched OPEN AP default reports PMF capable=true,
             * but set_config normalizes it to false. Preserve an already exact
             * predecessor without rewriting it; read failures never authorize
             * a write, and the separate readback phase still checks all fields. */
            err = esp_wifi_get_config(interface, &snapshot->scratch);
            apply = err == ESP_OK &&
                !wifi_radio_config_equal(interface, &snapshot->saved[index], &snapshot->scratch);
        }
        if (apply) {
            /* SDK's mutable input cannot alter the frozen predecessor. */
            snapshot->scratch = snapshot->saved[index];
            err = esp_wifi_set_config(interface, &snapshot->scratch);
            if (err == ESP_OK) err = wifi_radio_restore_disabled_pmf(interface, &snapshot->saved[index]);
        }
    } else {
        err = esp_wifi_get_config(interface, &snapshot->scratch);
        if (err == ESP_OK && !wifi_radio_config_equal(interface, &snapshot->saved[index], &snapshot->scratch)) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    esp32_mquickjs_wireless_secure_zero(&snapshot->scratch, sizeof(snapshot->scratch));
    return err;
}

static esp_err_t wifi_radio_restart_configs_replay_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (!wifi_radio_restart_configs_owner(token) || !s_config_restart.captured ||
        s_config_restart.owner.identity != token->identity || s_config_restart.owner.generation != token->generation ||
        !s_radio.driver_owned || !s_radio.storage_configured || s_radio.started || s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED) return ESP_ERR_INVALID_STATE;
    if (s_config_restart.replay_generation != s_radio.generation) {
        s_config_restart.replay_generation = s_radio.generation;
        s_config_restart.phase = RESTART_CONFIG_RAM;
        s_config_restart.start_phase = 0;
        s_config_restart.interval_revision = 0;
        s_config_restart.rate_completed = 0;
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
        s_config_restart.he_completed = 0;
#endif
        memset(s_config_restart.rate_identity, 0, sizeof(s_config_restart.rate_identity));
    }
    wifi_radio_restart_configs_t *snapshot = s_config_restart.snapshot;
    if (snapshot == NULL) { s_config_restart.phase = RESTART_CONFIG_DONE; return ESP_OK; }
    if (s_config_restart.source_generation == s_radio.generation) return ESP_ERR_INVALID_STATE;
    static const char *const stages[] = {
        "restart-config-ram",
        "restart-config-disable",
        "restart-config-disable-readback",
        "restart-country-write",
        "restart-country-readback",
        "restart-event-mask-write",
        "restart-event-mask-readback",
        "restart-mac-mode",
        "restart-mac-mode-readback",
        "restart-mac-temporary",
        "restart-ap-mac-write",
        "restart-ap-mac-readback",
        "restart-station-mac-write",
        "restart-station-mac-readback",
        "restart-config-enable",
        "restart-station-write",
        "restart-station-readback",
        "restart-ap-write",
        "restart-ap-readback",
        "restart-phy-band-replay",
        "restart-station-phy-write",
        "restart-station-phy-readback",
        "restart-ap-phy-write",
        "restart-ap-phy-readback",
        "restart-band-restore",
        "restart-power-save-write",
        "restart-power-save-readback",
        "restart-config-mode",
        "restart-config-mode-readback",
        "restart-tx-rate-replay",
        "restart-interval-replay"
    };
    while (s_config_restart.phase < RESTART_CONFIG_DONE) {
        unsigned phase = s_config_restart.phase;
        esp_err_t err = ESP_OK;
        wifi_mode_t actual;
        const char *stage = stages[phase];
        wifi_country_t country;
        uint8_t mac[6];
        uint32_t event_mask;
        wifi_ps_type_t power_save;
        switch (phase) {
        case RESTART_CONFIG_RAM:
            err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
            if (err == ESP_OK) {
                taskENTER_CRITICAL(&s_radio.lock); s_radio.storage = WIFI_STORAGE_RAM; taskEXIT_CRITICAL(&s_radio.lock);
            }
            break;
        case RESTART_CONFIG_DISABLE:
            err = esp_wifi_set_mode(WIFI_MODE_NULL);
            if (err == ESP_OK) {
                taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = WIFI_MODE_NULL; taskEXIT_CRITICAL(&s_radio.lock);
            }
            break;
        case RESTART_CONFIG_DISABLE_READ:
            err = esp_wifi_get_mode(&actual);
            if (err == ESP_OK && actual != WIFI_MODE_NULL) err = ESP_ERR_INVALID_RESPONSE;
            break;
        case RESTART_CONFIG_COUNTRY:
            country = snapshot->country;
            /* Country's maximum-power member is observation-only in the SDK.
             * Explicit TX-power restoration is a separate post-start control. */
            err = esp_wifi_set_country(&country);
            break;
        case RESTART_CONFIG_COUNTRY_READ:
            memset(&country, 0, sizeof(country));
            err = esp_wifi_get_country(&country);
            if (err == ESP_OK && !wifi_radio_country_equal(&snapshot->country, &country, false)) err = ESP_ERR_INVALID_RESPONSE;
            break;
        case RESTART_CONFIG_EVENT_MASK:
            err = esp_wifi_set_event_mask(snapshot->event_mask);
            break;
        case RESTART_CONFIG_EVENT_MASK_READ:
            err = esp_wifi_get_event_mask(&event_mask);
            if (err == ESP_OK && event_mask != snapshot->event_mask) err = ESP_ERR_INVALID_RESPONSE;
            break;
        case RESTART_CONFIG_MAC_MODE:
            /* The SDK rejects MAC writes for interfaces absent from mode.
             * Selecting the saved interface mask does not set its START flag;
             * MAC replacement therefore emits no Station/AP activation. */
            err = esp_wifi_set_mode((wifi_mode_t)snapshot->mask);
            if (err == ESP_OK) {
                taskENTER_CRITICAL(&s_radio.lock);
                s_radio.effective_mode = (wifi_mode_t)snapshot->mask;
                taskEXIT_CRITICAL(&s_radio.lock);
            }
            break;
        case RESTART_CONFIG_MAC_MODE_READ:
            err = esp_wifi_get_mode(&actual);
            if (err == ESP_OK && actual != (wifi_mode_t)snapshot->mask) err = ESP_ERR_INVALID_RESPONSE;
            break;
        case RESTART_CONFIG_MAC_TEMP:
            if (snapshot->mask & WIFI_MODE_AP) err = wifi_radio_restart_mac_temporary(snapshot, &stage);
            break;
        case RESTART_CONFIG_MAC_AP:
            if (snapshot->mask & WIFI_MODE_AP) {
                memcpy(mac, snapshot->mac[1], 6);
                err = esp_wifi_set_mac(WIFI_IF_AP, mac);
            }
            break;
        case RESTART_CONFIG_MAC_AP_READ:
            if (snapshot->mask & WIFI_MODE_AP) {
                err = esp_wifi_get_mac(WIFI_IF_AP, mac);
                if (err == ESP_OK && memcmp(mac, snapshot->mac[1], 6) != 0) err = ESP_ERR_INVALID_RESPONSE;
            }
            break;
        case RESTART_CONFIG_MAC_STA:
            memcpy(mac, snapshot->mac[0], 6);
            err = esp_wifi_set_mac(WIFI_IF_STA, mac);
            break;
        case RESTART_CONFIG_MAC_STA_READ:
            err = esp_wifi_get_mac(WIFI_IF_STA, mac);
            if (err == ESP_OK && memcmp(mac, snapshot->mac[0], 6) != 0) err = ESP_ERR_INVALID_RESPONSE;
            break;
        case RESTART_CONFIG_PHY_MODE:
            err = wifi_radio_restart_band_prepare_locked(token, (wifi_mode_t)snapshot->mask, &stage);
            break;
        case RESTART_CONFIG_STA_PHY_WRITE:
            err = wifi_radio_write_phy(WIFI_IF_STA, &snapshot->protocols[0], &snapshot->bandwidths[0]);
            break;
        case RESTART_CONFIG_STA_PHY_READ:
            err = wifi_radio_restart_phy_io(snapshot, 0, true);
            break;
        case RESTART_CONFIG_AP_PHY_WRITE:
            if (snapshot->mask & WIFI_MODE_AP)
                err = wifi_radio_write_phy(WIFI_IF_AP, &snapshot->protocols[1], &snapshot->bandwidths[1]);
            break;
        case RESTART_CONFIG_AP_PHY_READ:
            if (snapshot->mask & WIFI_MODE_AP) err = wifi_radio_restart_phy_io(snapshot, 1, true);
            break;
        case RESTART_CONFIG_BAND_RESTORE:
            err = wifi_radio_restart_band_select_locked(token, (wifi_mode_t)snapshot->mask, snapshot->band_mode, &stage);
            break;
        case RESTART_CONFIG_PS:
            err = esp_wifi_set_ps(snapshot->power_save);
            break;
        case RESTART_CONFIG_PS_READ:
            err = esp_wifi_get_ps(&power_save);
            if (err == ESP_OK && power_save != snapshot->power_save) err = ESP_ERR_INVALID_RESPONSE;
            break;
        case RESTART_CONFIG_ENABLE:
            err = esp_wifi_set_mode((wifi_mode_t)snapshot->mask);
            if (err == ESP_OK) {
                taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = (wifi_mode_t)snapshot->mask; taskEXIT_CRITICAL(&s_radio.lock);
            }
            break;
        case RESTART_CONFIG_STA_WRITE:
        case RESTART_CONFIG_STA_READ:
            err = wifi_radio_restart_config_io(snapshot, 0, phase == RESTART_CONFIG_STA_WRITE);
            break;
        case RESTART_CONFIG_AP_WRITE:
        case RESTART_CONFIG_AP_READ:
            if (snapshot->mask & WIFI_MODE_AP)
                err = wifi_radio_restart_config_io(snapshot, 1, phase == RESTART_CONFIG_AP_WRITE);
            break;
        case RESTART_CONFIG_MODE:
            err = esp_wifi_set_mode(s_config_restart.mode);
            if (err == ESP_OK) {
                taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = s_config_restart.mode; taskEXIT_CRITICAL(&s_radio.lock);
            }
            break;
        case RESTART_CONFIG_MODE_READ:
            err = esp_wifi_get_mode(&actual);
            if (err == ESP_OK && actual != s_config_restart.mode) err = ESP_ERR_INVALID_RESPONSE;
            break;
        case RESTART_CONFIG_RATES: {
            esp32_mquickjs_wifi_tx_rate_write_t output;
            err = esp32_mquickjs_wifi_tx_rate_replay(&s_tx_rates, s_radio.generation, &snapshot->rates,
                &s_config_restart.rate_completed, wifi_radio_write_tx_rate, NULL, &output);
            for (unsigned i = 0; i < 2; ++i)
                if (s_config_restart.rate_completed & (1U << i)) s_config_restart.rate_identity[i] = s_tx_rates.records[i].write_identity;
            break;
        }
        case RESTART_CONFIG_INTERVAL: {
            esp32_mquickjs_wifi_interval_result_t result;
            err = esp32_mquickjs_wifi_interval_replay(&s_interval, s_radio.generation, &snapshot->interval,
                wifi_radio_interval_writer, NULL, &result);
            if (err == ESP_OK) s_config_restart.interval_revision = result.revision;
            break;
        }
        }
        if (err != ESP_OK) return wifi_radio_restart_configs_record(stage, err, true);
        ++s_config_restart.phase;
    }
    return ESP_OK;
}

static bool wifi_radio_restart_configs_ready_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    if (!s_config_restart.captured) return s_config_restart.snapshot == NULL;
    return wifi_radio_restart_configs_owner(token) && s_config_restart.owner.identity == token->identity &&
        s_config_restart.owner.generation == token->generation && s_config_restart.mode == mode &&
        s_config_restart.replay_generation == s_radio.generation && s_config_restart.phase == RESTART_CONFIG_DONE;
}

static esp_err_t wifi_radio_restart_configs_pre_start_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    if (!s_config_restart.captured) return s_config_restart.snapshot == NULL ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (!wifi_radio_restart_configs_ready_locked(token, mode) ||
        !s_radio.driver_owned || !s_radio.storage_configured || s_radio.started || s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED || s_radio.fault_stage != NULL ||
        s_radio.cleanup_stage != NULL || s_radio.operation.identity != 0U || s_radio.wake_locks != 0U ||
        s_radio.promiscuous_claimed || s_radio.restart_required) return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) return ESP_ERR_INVALID_STATE;
    wifi_radio_restart_configs_t *snapshot = s_config_restart.snapshot;
    if (snapshot == NULL) return ESP_OK;
    /* AP START can save its chosen channel through the SDK's NVS path. Keep
     * that implicit write, and all explicit restoration, in RAM until final
     * acceptance. Restoring FLASH before START can persist intermediate state. */
    if (s_radio.storage != WIFI_STORAGE_RAM)
        return wifi_radio_restart_configs_record("restart-storage-admission", ESP_ERR_INVALID_STATE, true);
    for (unsigned i = 0; i < 2; ++i) {
        if (!(snapshot->mask & (1U << i))) continue;
        esp_err_t err = wifi_radio_restart_config_io(snapshot, i, false);
        if (err != ESP_OK) return wifi_radio_restart_configs_record(
            i == 0 ? "restart-station-pre-start-readback" : "restart-ap-pre-start-readback", err, true);
    }
    /* AP preferred/automatic channel may differ from the saved home channel.
     * The activation path starts AP-only and uses SDK-supported CSA before
     * enabling STA, retaining the original AP configuration throughout. */
    if ((mode & WIFI_MODE_AP) && !(snapshot->mask & WIFI_MODE_AP))
        return wifi_radio_restart_configs_record("restart-ap-channel-admission", ESP_ERR_INVALID_STATE, true);
    return ESP_OK;
}

/* Poll observations only while an accepted AP CSA settles. Preserve SDK
 * failures independently of an ordinary not-yet-matching channel snapshot. */
static esp_err_t wifi_radio_restart_ap_channel_matches(
    const wifi_radio_restart_configs_t *snapshot, bool *matches)
{
    *matches = false;
    wifi_band_mode_t mode;
    wifi_band_t band;
    esp_err_t err = wifi_radio_band_snapshot(&mode, &band);
    if (err != ESP_OK) return err;
    uint8_t primary, home;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE, home_secondary = WIFI_SECOND_CHAN_NONE;
    err = esp_wifi_get_channel(&primary, &secondary);
    if (err == ESP_OK) err = esp_wifi_get_home_channel(&home, &home_secondary);
    if (err != ESP_OK) return err;
    *matches = mode == snapshot->band_mode && band == snapshot->band &&
        primary == snapshot->primary && home == snapshot->primary &&
        secondary == snapshot->secondary && home_secondary == snapshot->secondary;
    return ESP_OK;
}

static esp_err_t wifi_radio_restart_ap_channel_wait_inner(
    esp32_mquickjs_runtime_t *runtime, const wifi_radio_restart_configs_t *snapshot)
{
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = esp32_mquickjs_wifi_wait_remaining(pdMS_TO_TICKS(WIFI_RADIO_START_EVENT_TIMEOUT_MS));
    for (;;) {
        if (!esp32_mquickjs_cooperate(runtime)) return ESP_ERR_INVALID_STATE;
        bool matches;
        esp_err_t err = wifi_radio_restart_ap_channel_matches(snapshot, &matches);
        if (err != ESP_OK || matches) return err;
        if ((TickType_t)(xTaskGetTickCount() - started) >= timeout) return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
}

static esp_err_t wifi_radio_restart_ap_channel_wait(const wifi_radio_restart_configs_t *snapshot)
{
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
    if (wifi_radio_mesh_worker_locked()) return wifi_radio_restart_ap_channel_wait_inner(NULL, snapshot);
#endif
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    esp32_mquickjs_native_wait_t wait;
    esp32_mquickjs_native_wait_begin(runtime, &wait);
    esp_err_t err = wifi_radio_restart_ap_channel_wait_inner(runtime, snapshot);
    esp32_mquickjs_native_wait_end(runtime, &wait);
    return err;
}

/* All output leases are staged under the exact lifecycle token. AP-only mode
 * permits SDK CSA even if a native client arrives before publication. APSTA
 * forbids that mutation with clients: add STA only after AP channel acceptance.
 * Never update live AP config, which the pinned SDK implements as AP STOP/START.
 * Any failed write/event/readback leaves the physical attempt for cleanup. */
static esp_err_t wifi_radio_restart_configs_start_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode,
    esp32_mquickjs_wifi_radio_lease_t *lease)
{
    wifi_radio_restart_configs_t *snapshot = s_config_restart.snapshot;
    if (!s_config_restart.captured || snapshot == NULL || !(mode & WIFI_MODE_AP))
        return wifi_radio_ensure_started_locked(lease);
    if (!wifi_radio_restart_configs_ready_locked(token, mode) || !wifi_radio_lease_valid(lease) ||
        !s_radio.driver_owned || !s_radio.storage_configured || s_radio.storage != WIFI_STORAGE_RAM ||
        s_radio.started || s_radio.stop_required || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
        s_radio.operation.identity != 0U || s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required)
        return ESP_ERR_INVALID_STATE;
    const char *stage = "restart-ap-mode";
    esp_err_t err = ESP_OK;
    if (mode == WIFI_MODE_APSTA) {
        err = esp_wifi_set_mode(WIFI_MODE_AP);
        if (err != ESP_OK) goto failed;
        taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = WIFI_MODE_AP; taskEXIT_CRITICAL(&s_radio.lock);
    }
    wifi_mode_t actual;
    stage = "restart-ap-mode-readback";
    err = esp_wifi_get_mode(&actual);
    if (err == ESP_OK && actual != WIFI_MODE_AP) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) goto failed;
    stage = "restart-ap-start";
    err = wifi_radio_start_stopped_locked(WIFI_MODE_AP, true);
    if (err != ESP_OK) goto failed;
    stage = "restart-ap-channel-write";
    err = esp_wifi_set_channel(snapshot->primary, snapshot->secondary);
    if (err != ESP_OK) goto failed;
    stage = "restart-ap-channel-wait";
    err = wifi_radio_restart_ap_channel_wait(snapshot);
    if (err != ESP_OK) goto failed;
    if (mode == WIFI_MODE_APSTA) {
        stage = "restart-station-start-events";
        err = wifi_radio_begin_events(RADIO_EVENTS_STA_START, WIFI_MODE_STA);
        if (err != ESP_OK) goto failed;
        wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STARTING);
        stage = "restart-station-start-mode";
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) goto failed;
        taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = WIFI_MODE_APSTA; taskEXIT_CRITICAL(&s_radio.lock);
        stage = "restart-station-start-events";
        err = wifi_radio_wait_events();
        if (err != ESP_OK) goto failed;
        wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STARTED);
    }
    stage = "restart-activation-mode-readback";
    err = esp_wifi_get_mode(&actual);
    if (err == ESP_OK && actual != mode) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) goto failed;
    stage = "restart-activation-channel-readback";
    err = wifi_radio_restart_channel_read(snapshot, true);
    if (err != ESP_OK) goto failed;
    return ESP_OK;
failed:
    return wifi_radio_restart_configs_record(stage, err, true);
}

enum { RESTART_START_CHANNEL, RESTART_START_CHANNEL_READ, RESTART_START_POWER,
    RESTART_START_POWER_READ, RESTART_START_STA_INACTIVE, RESTART_START_STA_INACTIVE_READ,
    RESTART_START_AP_INACTIVE, RESTART_START_AP_INACTIVE_READ,
    RESTART_START_SCAN_PARAMETERS, RESTART_START_SCAN_PARAMETERS_READ, RESTART_START_HE_STATISTICS,
    RESTART_START_TWT_POLICY, RESTART_START_DONE };

/* START and policy replay have finished, but staged leases are unpublished.
 * Restoring a prior SDK observation requires equality, not the lower ceiling
 * accepted by an explicit new setTxPower request. */
static esp_err_t wifi_radio_restart_configs_post_start_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (!s_config_restart.captured) return s_config_restart.snapshot == NULL ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (!wifi_radio_restart_configs_ready_locked(token, s_config_restart.mode)) return ESP_ERR_INVALID_STATE;
    if (s_config_restart.snapshot == NULL) return ESP_OK;
    if (!s_radio.started || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED ||
        !s_radio.storage_configured || s_radio.storage != WIFI_STORAGE_RAM ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL) return ESP_ERR_INVALID_STATE;
    if (s_config_restart.start_phase == RESTART_START_CHANNEL) {
        /* set_channel is START-only and does not survive STOP. The restored
         * Station is not scanning/connecting and its owners remain staged.
         * AP activation already restored its channel in AP-only mode and,
         * for APSTA, fenced STA startup. This stage only verifies AP/APSTA. */
        if (s_config_restart.mode == WIFI_MODE_STA) {
            esp_err_t err = esp_wifi_set_channel(s_config_restart.snapshot->primary, s_config_restart.snapshot->secondary);
            if (err != ESP_OK) return wifi_radio_restart_configs_record("restart-channel-write", err, true);
        }
        s_config_restart.start_phase = RESTART_START_CHANNEL_READ;
    }
    if (s_config_restart.start_phase == RESTART_START_CHANNEL_READ) {
        esp_err_t err = wifi_radio_restart_channel_read(s_config_restart.snapshot, true);
        if (err != ESP_OK) return wifi_radio_restart_configs_record("restart-channel-readback", err, true);
        s_config_restart.start_phase = RESTART_START_POWER;
    }
    if (s_config_restart.start_phase == RESTART_START_POWER) {
        esp_err_t err = esp_wifi_set_max_tx_power(s_config_restart.snapshot->tx_power);
        if (err != ESP_OK) return wifi_radio_restart_configs_record("restart-tx-power-write", err, true);
        s_config_restart.start_phase = RESTART_START_POWER_READ;
    }
    if (s_config_restart.start_phase == RESTART_START_POWER_READ) {
        int8_t actual;
        esp_err_t err = esp_wifi_get_max_tx_power(&actual);
        if (err == ESP_OK && actual != s_config_restart.snapshot->tx_power) err = ESP_ERR_INVALID_RESPONSE;
        if (err != ESP_OK) return wifi_radio_restart_configs_record("restart-tx-power-readback", err, true);
        s_config_restart.start_phase = RESTART_START_STA_INACTIVE;
    }
    while (s_config_restart.start_phase < RESTART_START_SCAN_PARAMETERS) {
        unsigned phase = s_config_restart.start_phase - RESTART_START_STA_INACTIVE;
        unsigned index = phase / 2U;
        bool readback = (phase & 1U) != 0U;
        if (s_config_restart.snapshot->inactive_saved_mask & (uint8_t)s_config_restart.mode & (1U << index)) {
            wifi_interface_t interface = index == 0 ? WIFI_IF_STA : WIFI_IF_AP;
            uint16_t wanted = s_config_restart.snapshot->inactive_time[index], actual;
            const char *stage = index == 0
                ? (readback ? "restart-station-inactive-readback" : "restart-station-inactive-write")
                : (readback ? "restart-ap-inactive-readback" : "restart-ap-inactive-write");
            /* Storage stays RAM until all final acceptance checks pass. A
             * failed write retains its phase and the original frozen value. */
            esp_err_t err = readback ? esp_wifi_get_inactive_time(interface, &actual)
                                     : esp_wifi_set_inactive_time(interface, wanted);
            if (err == ESP_OK && readback && actual != wanted) err = ESP_ERR_INVALID_RESPONSE;
            if (readback || err != ESP_OK) wifi_radio_inactive_history_record(index, wanted, err);
            if (err != ESP_OK) return wifi_radio_restart_configs_record(stage, err, true);
        }
        ++s_config_restart.start_phase;
    }
    if (s_config_restart.start_phase == RESTART_START_SCAN_PARAMETERS) {
        if (s_config_restart.snapshot->scan_parameters_known && (s_config_restart.mode & WIFI_MODE_STA)) {
            esp_err_t error = wifi_scan_parameters_set_native(&s_config_restart.snapshot->scan_parameters);
            if (error != ESP_OK) return wifi_radio_restart_configs_record("restart-scan-parameters-write", error, true);
        }
        s_config_restart.start_phase = RESTART_START_SCAN_PARAMETERS_READ;
    }
    if (s_config_restart.start_phase == RESTART_START_SCAN_PARAMETERS_READ) {
        if (s_config_restart.snapshot->scan_parameters_known && (s_config_restart.mode & WIFI_MODE_STA)) {
            wifi_scan_default_params_t actual;
            esp_err_t error = wifi_radio_scan_parameters_observe_locked(&actual);
            if (error == ESP_OK && !wifi_scan_parameters_equal(&actual, &s_config_restart.snapshot->scan_parameters))
                error = ESP_ERR_INVALID_RESPONSE;
            if (error != ESP_OK) return wifi_radio_restart_configs_record("restart-scan-parameters-readback", error, true);
        }
        s_config_restart.start_phase = RESTART_START_HE_STATISTICS;
    }
    if (s_config_restart.start_phase == RESTART_START_HE_STATISTICS) {
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
        s_he_statistics.managed = true;
        esp_err_t error = esp32_mquickjs_wifi_he_statistics_restore(
            &s_config_restart.snapshot->he_statistics, &s_config_restart.he_completed);
        if (error != ESP_OK) return wifi_radio_restart_configs_record("restart-he-statistics", error, true);
#endif
        s_config_restart.start_phase = RESTART_START_TWT_POLICY;
    }
    if (s_config_restart.start_phase == RESTART_START_TWT_POLICY) {
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
        esp32_mquickjs_wifi_twt_control_t value = {.config = s_config_restart.snapshot->twt_policy};
        esp_err_t error = esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG, &value);
        if (error != ESP_OK) return wifi_radio_restart_configs_record("restart-twt-policy", error, true);
        s_twt_policy.value = value.config;
        s_twt_policy.known = true;
        s_twt_policy.pending = false;
#endif
        s_config_restart.start_phase = RESTART_START_DONE;
    }
    return ESP_OK;
}

static esp_err_t wifi_radio_restart_configs_verify_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    if (!wifi_radio_restart_configs_ready_locked(token, mode)) return ESP_ERR_INVALID_STATE;
    if (s_config_restart.snapshot == NULL) return ESP_OK;
    if (s_config_restart.start_phase != RESTART_START_DONE || !s_radio.started) return ESP_ERR_INVALID_STATE;
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    esp32_mquickjs_wifi_twt_control_t twt_value = {0};
    esp_err_t twt_error = esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_READ_CONFIG, &twt_value);
    if (twt_error == ESP_OK &&
        (twt_value.config.post_wakeup_event != s_config_restart.snapshot->twt_policy.post_wakeup_event ||
         twt_value.config.twt_enable_keep_alive != s_config_restart.snapshot->twt_policy.twt_enable_keep_alive))
        twt_error = ESP_ERR_INVALID_RESPONSE;
    if (twt_error != ESP_OK) return wifi_radio_restart_configs_record("restart-twt-policy-readback", twt_error, true);
#endif
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
    esp32_mquickjs_wifi_he_statistics_t he_actual;
    esp_err_t he_error = esp32_mquickjs_wifi_he_statistics_snapshot(&he_actual, false);
    if (he_error == ESP_OK && !wifi_he_statistics_equal(&he_actual, &s_config_restart.snapshot->he_statistics))
        he_error = ESP_ERR_INVALID_RESPONSE;
    if (he_error != ESP_OK) return wifi_radio_restart_configs_record("restart-he-statistics-readback", he_error, true);
#endif
    const esp32_mquickjs_wifi_tx_rate_snapshot_t *rates = &s_config_restart.snapshot->rates;
    if (s_config_restart.rate_completed != rates->mask || s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending)
        return wifi_radio_restart_configs_record("restart-tx-rate-acceptance", ESP_ERR_INVALID_STATE, true);
    for (unsigned i = 0; i < 2; ++i) {
        if (!(rates->mask & (1U << i))) continue;
        const esp32_mquickjs_wifi_tx_rate_record_t *record = &s_tx_rates.records[i];
        const wifi_tx_rate_config_t *wanted = &rates->configs[i];
        if (!record->known || record->uncertain || record->generation != s_radio.generation ||
            record->write_identity != s_config_restart.rate_identity[i] || record->config.phymode != wanted->phymode ||
            record->config.rate != wanted->rate || record->config.ersu != wanted->ersu || record->config.dcm != wanted->dcm)
            return wifi_radio_restart_configs_record("restart-tx-rate-acceptance", ESP_ERR_INVALID_STATE, true);
    }
    /* There is no SDK interval getter. Verify the exclusive writer's accepted
     * revision/value, not an invented native readback or RF timing guarantee. */
    if (!s_interval.known || s_interval.uncertain || s_interval.restore_pending || s_interval.owner.identity != 0U ||
        s_interval.generation != s_radio.generation || s_interval.revision != s_config_restart.interval_revision ||
        s_interval.value != s_config_restart.snapshot->interval.value)
        return wifi_radio_restart_configs_record("restart-interval-acceptance", ESP_ERR_INVALID_STATE, true);
    if (s_config_restart.snapshot->scan_parameters_known && (mode & WIFI_MODE_STA)) {
        wifi_scan_default_params_t actual;
        esp_err_t error = wifi_radio_scan_parameters_observe_locked(&actual);
        if (error == ESP_OK && !wifi_scan_parameters_equal(&actual, &s_config_restart.snapshot->scan_parameters))
            error = ESP_ERR_INVALID_RESPONSE;
        if (error != ESP_OK) return wifi_radio_restart_configs_record("restart-scan-parameters-final-readback", error, true);
    }
    int8_t actual_power;
    esp_err_t power_error = esp_wifi_get_max_tx_power(&actual_power);
    if (power_error == ESP_OK && actual_power != s_config_restart.snapshot->tx_power) power_error = ESP_ERR_INVALID_RESPONSE;
    if (power_error != ESP_OK) return wifi_radio_restart_configs_record("restart-tx-power-final-readback", power_error, true);
    const char *stage = NULL;
    esp_err_t global_error = wifi_radio_restart_inactive_read(s_config_restart.snapshot,
        s_config_restart.snapshot->inactive_saved_mask & (uint8_t)mode, true, &stage);
    if (global_error != ESP_OK) return wifi_radio_restart_configs_record(stage, global_error, true);
    global_error = wifi_radio_restart_phy_read(s_config_restart.snapshot, true, &stage);
    if (global_error != ESP_OK) return wifi_radio_restart_configs_record(stage, global_error, true);
    /* Full AUTO PHY replay alone is insufficient. The final mode, current
     * band and current/home channel must still match the frozen predecessor. */
    global_error = wifi_radio_restart_channel_read(s_config_restart.snapshot, true);
    if (global_error != ESP_OK)
        return wifi_radio_restart_configs_record("restart-band-channel-final-readback", global_error, true);
    global_error = wifi_radio_restart_globals_read(s_config_restart.snapshot, true, &stage);
    if (global_error != ESP_OK) return wifi_radio_restart_configs_record(stage, global_error, true);
    for (unsigned i = 0; i < 2; ++i) {
        if (!(s_config_restart.snapshot->mask & (1U << i))) continue;
        esp_err_t err = wifi_radio_restart_config_io(s_config_restart.snapshot, i, false);
        if (err != ESP_OK) return wifi_radio_restart_configs_record(
            i == 0 ? "restart-station-final-readback" : "restart-ap-final-readback", err, true);
    }
    return ESP_OK;
}

/* Called by the real resume path before owner publication. Verification and
 * SDK START happen while storage is RAM. Only then restore the caller's policy
 * for subsequent writes. set_storage has no getter: record SDK acceptance,
 * and keep failure unknown instead of claiming rollback or retrying it here. */
static esp_err_t wifi_radio_restart_configs_commit_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    wifi_radio_restart_configs_t *snapshot = s_config_restart.snapshot;
    if (s_config_restart.captured && snapshot != NULL &&
        (!s_radio.driver_owned || !s_radio.storage_configured || s_radio.storage != WIFI_STORAGE_RAM ||
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED ||
         s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = wifi_radio_restart_configs_verify_locked(token, mode);
    if (err != ESP_OK || snapshot == NULL) return err;
    /* Transfer hidden intent before the frozen allocation can be released.
     * It remains unobserved in this new driver until that interface is enabled. */
    if (snapshot->scan_parameters_known && !(mode & WIFI_MODE_STA)) {
        s_scan_parameters.value = snapshot->scan_parameters;
        s_scan_parameters.generation = s_radio.generation;
        s_scan_parameters.known = s_scan_parameters.pending = true;
        s_scan_parameters.error = ESP_OK;
        s_scan_parameters.attempted = false;
    }
    uint8_t hidden = snapshot->inactive_saved_mask & ~((uint8_t)mode);
    if (s_inactive_history.generation != s_radio.generation) {
        memset(&s_inactive_history, 0, sizeof(s_inactive_history));
        s_inactive_history.generation = s_radio.generation;
    }
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t bit = (uint8_t)(1U << i);
        if (!(hidden & bit)) continue;
        s_inactive_history.pending_values[i] = snapshot->inactive_time[i];
        s_inactive_history.pending |= bit;
        s_inactive_history.attempted &= (uint8_t)~bit;
        s_inactive_history.persistent &= (uint8_t)~bit;
        s_inactive_history.known &= (uint8_t)~bit;
    }
    /* Off restoration keeps RAM through final STOP and mode=NULL. The source
     * remains frozen until helper retirement and lifecycle completion. */
    if (snapshot->restore_off) return ESP_OK;
    return wifi_radio_restart_configs_commit_storage_locked(token);
}

static esp_err_t wifi_radio_restart_configs_commit_storage_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_restart_configs_t *snapshot = s_config_restart.snapshot;
    if (!wifi_radio_restart_configs_owner(token) || !s_config_restart.captured || snapshot == NULL ||
        s_config_restart.owner.identity != token->identity || s_config_restart.owner.generation != token->generation ||
        !s_radio.driver_owned || !s_radio.storage_configured || s_radio.storage != WIFI_STORAGE_RAM ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL) return ESP_ERR_INVALID_STATE;
    if (snapshot->restore_off && (s_radio.started || s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED || s_radio.effective_mode != WIFI_MODE_NULL))
        return ESP_ERR_INVALID_STATE;
    if (snapshot->storage == WIFI_STORAGE_RAM) return ESP_OK;
    if (snapshot->storage != WIFI_STORAGE_FLASH) return ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.storage_configured = false;
    taskEXIT_CRITICAL(&s_radio.lock);
    esp_err_t err = esp_wifi_set_storage(snapshot->storage);
    if (err != ESP_OK) return wifi_radio_restart_configs_record("restart-storage-commit", err, true);
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.storage = snapshot->storage;
    s_radio.storage_configured = true;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

uint32_t esp32_mquickjs_wifi_radio_restart_snapshot_bytes(void)
{
    wifi_radio_operation_lock();
    uint32_t bytes = s_config_restart.snapshot == NULL ? 0U : sizeof(*s_config_restart.snapshot);
    wifi_radio_operation_unlock();
    return bytes;
}

/* These helpers run inside the Radio mutation lane. PHY writes require STOP;
 * country-only transactions also support an idle, unassociated Station. */
typedef struct {
    wifi_country_t country;
    wifi_protocols_t protocols[2], desired_protocols[2];
    wifi_bandwidths_t bandwidths[2], desired_bandwidths[2];
    wifi_ps_type_t power_save;
    uint8_t active_bands;
    bool country_touched, phy_touched[2], power_save_touched;
} wifi_radio_controls_snapshot_t;

static bool wifi_radio_country_valid(const wifi_country_t *c)
{
    return !(!((c->cc[0] >= 'A' && c->cc[0] <= 'Z' && c->cc[1] >= 'A' && c->cc[1] <= 'Z') ||
              (c->cc[0] == '0' && c->cc[1] == '1')) ||
            (c->cc[2] != '\0' && c->cc[2] != ' ' && c->cc[2] != 'I' && c->cc[2] != 'O' && c->cc[2] != 'X') ||
            (c->policy != WIFI_COUNTRY_POLICY_AUTO && c->policy != WIFI_COUNTRY_POLICY_MANUAL) ||
            c->schan < 1 || c->nchan < 1 || (unsigned)c->schan + c->nchan > 15);
}

static bool wifi_radio_country_equal(const wifi_country_t *a, const wifi_country_t *b,
    bool include_power)
{
    /* SDK normalizes an unspecified environment octet to a space. */
    char a_env = a->cc[2] == '\0' ? ' ' : a->cc[2];
    char b_env = b->cc[2] == '\0' ? ' ' : b->cc[2];
    return a->cc[0] == b->cc[0] && a->cc[1] == b->cc[1] && a_env == b_env &&
        a->schan == b->schan && a->nchan == b->nchan && a->policy == b->policy &&
        (!include_power || a->max_tx_power == b->max_tx_power)
#if CONFIG_SOC_WIFI_SUPPORT_5G
        && a->wifi_5g_channel_mask == b->wifi_5g_channel_mask
#endif
        ;
}

static esp_err_t wifi_radio_validate_protocol(uint16_t value, bool ghz5)
{
    uint16_t base = ghz5 ? WIFI_PROTOCOL_11A : WIFI_PROTOCOL_11B;
    uint16_t allowed = base | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX |
        (ghz5 ? WIFI_PROTOCOL_11AC : WIFI_PROTOCOL_11G | WIFI_PROTOCOL_LR);
    if (value == 0 || (value & ~allowed) != 0) return ESP_ERR_INVALID_ARG;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
    if (ghz5) return ESP_ERR_NOT_SUPPORTED;
#endif
#if !CONFIG_SOC_WIFI_HE_SUPPORT
    if (value & WIFI_PROTOCOL_11AX) return ESP_ERR_NOT_SUPPORTED;
#endif
    /* Preserve LR as a bitmap; never expand it into an unrelated PHY. */
    uint16_t regular = value & ~WIFI_PROTOCOL_LR;
    if (regular == 0) return ESP_OK;
    if (!(regular & base)) return ESP_ERR_INVALID_ARG;
    if (!ghz5 && (regular & WIFI_PROTOCOL_11N) && !(regular & WIFI_PROTOCOL_11G))
        return ESP_ERR_INVALID_ARG;
    if ((regular & (WIFI_PROTOCOL_11AC | WIFI_PROTOCOL_11AX)) && !(regular & WIFI_PROTOCOL_11N))
        return ESP_ERR_INVALID_ARG;
    if (ghz5 && (regular & WIFI_PROTOCOL_11AX) && !(regular & WIFI_PROTOCOL_11AC))
        return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_validate_config_controls(wifi_mode_t mode,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls)
{
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA)
        return ESP_ERR_INVALID_ARG;
    if (controls == NULL) return ESP_OK;
    if (controls->country_set) {
        const wifi_country_t *c = &controls->country;
        if (!((c->cc[0] >= 'A' && c->cc[0] <= 'Z' && c->cc[1] >= 'A' && c->cc[1] <= 'Z') ||
              (c->cc[0] == '0' && c->cc[1] == '1')) ||
            (c->policy != WIFI_COUNTRY_POLICY_AUTO && c->policy != WIFI_COUNTRY_POLICY_MANUAL))
            return ESP_ERR_INVALID_ARG;
        if (controls->country_by_code) {
            if (c->cc[2] != '\0') return ESP_ERR_INVALID_ARG;
        } else {
            if ((c->cc[2] != '\0' && c->cc[2] != ' ' && c->cc[2] != 'O' &&
                 c->cc[2] != 'I' && c->cc[2] != 'X') || c->schan < 1 ||
                c->nchan < 1 || (unsigned)c->schan + c->nchan > 15 || c->max_tx_power != 0)
                return ESP_ERR_INVALID_ARG;
#if CONFIG_SOC_WIFI_SUPPORT_5G
            uint32_t known = 0;
            for (unsigned ch = 15; ch <= 177; ++ch)
                known |= esp32_mquickjs_wifi_radio_5ghz_channel_bit((uint8_t)ch);
            if ((c->wifi_5g_channel_mask & ~known) != 0 ||
                (c->wifi_5g_channel_mask && c->policy != WIFI_COUNTRY_POLICY_MANUAL))
                return ESP_ERR_INVALID_ARG;
#endif
        }
    }
    if (controls->power_save_set && (!(mode & WIFI_MODE_STA) ||
        (controls->power_save != WIFI_PS_NONE && controls->power_save != WIFI_PS_MIN_MODEM &&
         controls->power_save != WIFI_PS_MAX_MODEM))) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < 2; ++i) {
        unsigned bands = controls->protocol_bands[i] | controls->bandwidth_bands[i];
        if ((bands & ~3U) || (bands && !(mode & (i == 0 ? WIFI_MODE_STA : WIFI_MODE_AP))))
            return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if (i == 1 && bands) return ESP_ERR_NOT_SUPPORTED;
#endif
#if !CONFIG_SOC_WIFI_SUPPORT_5G
        if (bands & 2U) return ESP_ERR_NOT_SUPPORTED;
#endif
        for (unsigned band = 1; band <= 2; band <<= 1) {
            if (controls->protocol_bands[i] & band) {
                esp_err_t err = wifi_radio_validate_protocol(band == 1 ?
                    controls->protocols[i].ghz_2g : controls->protocols[i].ghz_5g, band == 2);
                if (err != ESP_OK) return err;
            }
            if (controls->bandwidth_bands[i] & band) {
                wifi_bandwidth_t bw = band == 1 ? controls->bandwidths[i].ghz_2g : controls->bandwidths[i].ghz_5g;
                if (bw != WIFI_BW20 && bw != WIFI_BW40) return ESP_ERR_INVALID_ARG;
                if ((controls->protocol_bands[i] & band) && bw == WIFI_BW40) {
                    uint16_t p = band == 1 ? controls->protocols[i].ghz_2g : controls->protocols[i].ghz_5g;
                    if (!(p & WIFI_PROTOCOL_11N) || (p & (WIFI_PROTOCOL_11AC | WIFI_PROTOCOL_11AX)))
                        return ESP_ERR_INVALID_ARG;
                }
            }
        }
    }
    return ESP_OK;
}

static esp_err_t wifi_radio_read_phy(wifi_interface_t iface, wifi_protocols_t *protocols,
    wifi_bandwidths_t *bandwidths)
{
    memset(protocols, 0, sizeof(*protocols));
    memset(bandwidths, 0, sizeof(*bandwidths));
#if CONFIG_SOC_WIFI_SUPPORT_5G
    esp_err_t err = esp_wifi_get_protocols(iface, protocols);
    return err == ESP_OK ? esp_wifi_get_bandwidths(iface, bandwidths) : err;
#else
    uint8_t protocol = 0;
    esp_err_t err = esp_wifi_get_protocol(iface, &protocol);
    protocols->ghz_2g = protocol;
    return err == ESP_OK ? esp_wifi_get_bandwidth(iface, &bandwidths->ghz_2g) : err;
#endif
}

esp_err_t esp32_mquickjs_wifi_radio_read_phy(wifi_interface_t interface,
    esp32_mquickjs_wifi_phy_query_t query, esp32_mquickjs_wifi_phy_readback_t *output,
    const char **stage)
{
    if (output != NULL) memset(output, 0, sizeof(*output));
    if (stage != NULL) *stage = "admission";
    if (output == NULL || stage == NULL ||
        (interface != WIFI_IF_STA && interface != WIFI_IF_AP) ||
        (query != ESP32_MQUICKJS_WIFI_PHY_PROTOCOL && query != ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS &&
         query != ESP32_MQUICKJS_WIFI_PHY_BANDWIDTH && query != ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS))
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    bool plural = query == ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS || query == ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS;
    bool protocol = query == ESP32_MQUICKJS_WIFI_PHY_PROTOCOL || query == ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS;
    esp32_mquickjs_wifi_phy_readback_t value = {.bands = 1U};
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED) ||
        !(s_radio.effective_mode & (interface == WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP))) goto done;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    if (plural) {
        wifi_band_mode_t mode;
        *stage = "band-mode";
        err = esp_wifi_get_band_mode(&mode);
        if (err != ESP_OK) goto done;
        if (mode == WIFI_BAND_MODE_2G_ONLY) value.bands = 1U;
        else if (mode == WIFI_BAND_MODE_5G_ONLY) value.bands = 2U;
        else if (mode == WIFI_BAND_MODE_AUTO) value.bands = 3U;
        else { err = ESP_ERR_INVALID_RESPONSE; goto done; }
        *stage = protocol ? "protocols" : "bandwidths";
        err = protocol ? esp_wifi_get_protocols(interface, &value.protocols)
                       : esp_wifi_get_bandwidths(interface, &value.bandwidths);
    } else
#else
    (void)plural;
#endif
    {
        *stage = protocol ? "protocol" : "bandwidth";
        if (protocol) {
            uint8_t bitmap = 0;
            err = esp_wifi_get_protocol(interface, &bitmap);
            value.protocols.ghz_2g = bitmap;
        } else err = esp_wifi_get_bandwidth(interface, &value.bandwidths.ghz_2g);
    }
    if (err != ESP_OK) goto done;
    *stage = "decode";
    for (unsigned band = 1; band <= 2; band <<= 1) {
        if (!(value.bands & band)) continue;
        uint16_t bits = band == 1 ? value.protocols.ghz_2g : value.protocols.ghz_5g;
        wifi_bandwidth_t width = band == 1 ? value.bandwidths.ghz_2g : value.bandwidths.ghz_5g;
        if ((protocol && (bits & ~(WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N |
                                  WIFI_PROTOCOL_11A | WIFI_PROTOCOL_11AC | WIFI_PROTOCOL_11AX | WIFI_PROTOCOL_LR))) ||
            (!protocol && width != WIFI_BW20 && width != WIFI_BW40)) {
            err = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
    }
    /* Ignore SDK storage for unrequested bands; never expose zero-fill as BW20. */
    if (!(value.bands & 1U)) { value.protocols.ghz_2g = 0; value.bandwidths.ghz_2g = 0; }
    if (!(value.bands & 2U)) { value.protocols.ghz_5g = 0; value.bandwidths.ghz_5g = 0; }
    *output = value;
    *stage = NULL;
done:
    wifi_radio_operation_unlock();
    return err;
}

static bool wifi_radio_phy_equal(uint8_t bands, const wifi_protocols_t *a,
    const wifi_bandwidths_t *ab, const wifi_protocols_t *b, const wifi_bandwidths_t *bb)
{
    return (!(bands & 1) || (a->ghz_2g == b->ghz_2g && ab->ghz_2g == bb->ghz_2g)) &&
        (!(bands & 2) || (a->ghz_5g == b->ghz_5g && ab->ghz_5g == bb->ghz_5g));
}

static esp_err_t wifi_radio_write_phy(wifi_interface_t iface,
    const wifi_protocols_t *protocols, const wifi_bandwidths_t *bandwidths)
{
    /* A previous HT40 configuration cannot be carried through an AX/AC
     * protocol write. Establish HT20 first, then restore the requested width. */
#if CONFIG_SOC_WIFI_SUPPORT_5G
    wifi_bandwidths_t narrow = {.ghz_2g = WIFI_BW20, .ghz_5g = WIFI_BW20};
    wifi_protocols_t protocol_copy = *protocols;
    wifi_bandwidths_t bandwidth_copy = *bandwidths;
    esp_err_t err = esp_wifi_set_bandwidths(iface, &narrow);
    if (err == ESP_OK) err = esp_wifi_set_protocols(iface, &protocol_copy);
    if (err == ESP_OK) err = esp_wifi_set_bandwidths(iface, &bandwidth_copy);
#else
    esp_err_t err = esp_wifi_set_bandwidth(iface, WIFI_BW20);
    if (err == ESP_OK) err = esp_wifi_set_protocol(iface, (uint8_t)protocols->ghz_2g);
    if (err == ESP_OK) err = esp_wifi_set_bandwidth(iface, bandwidths->ghz_2g);
#endif
    return err;
}

static esp_err_t wifi_radio_snapshot_controls(
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    wifi_radio_controls_snapshot_t *snapshot, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (controls == NULL) return ESP_OK;
    esp_err_t err;
    if (controls->country_set) {
        result->stage = "country-snapshot";
        err = esp_wifi_get_country(&snapshot->country);
        if (err != ESP_OK) return err;
    }
    bool phy = controls->protocol_bands[0] || controls->protocol_bands[1] ||
        controls->bandwidth_bands[0] || controls->bandwidth_bands[1];
    snapshot->active_bands = 1;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    if (phy) {
        wifi_band_mode_t band;
        result->stage = "band-mode-snapshot";
        err = esp_wifi_get_band_mode(&band);
        if (err != ESP_OK) return err;
        if (band == WIFI_BAND_MODE_2G_ONLY) snapshot->active_bands = 1;
        else if (band == WIFI_BAND_MODE_5G_ONLY) snapshot->active_bands = 2;
        else if (band == WIFI_BAND_MODE_AUTO) snapshot->active_bands = 3;
        else return ESP_ERR_INVALID_RESPONSE;
    }
#else
    (void)phy;
#endif
    for (size_t i = 0; i < 2; ++i) {
        unsigned bands = controls->protocol_bands[i] | controls->bandwidth_bands[i];
        if (!bands) continue;
        result->stage = i == 0 ? "station-phy-snapshot" : "ap-phy-snapshot";
        /* The SDK silently ignores inactive bands. Refuse before any write. */
        if (bands & ~snapshot->active_bands) return ESP_ERR_INVALID_STATE;
        err = wifi_radio_read_phy(i == 0 ? WIFI_IF_STA : WIFI_IF_AP,
            &snapshot->protocols[i], &snapshot->bandwidths[i]);
        if (err != ESP_OK) return err;
        snapshot->desired_protocols[i] = snapshot->protocols[i];
        snapshot->desired_bandwidths[i] = snapshot->bandwidths[i];
        for (unsigned band = 1; band <= 2; band <<= 1) {
            uint16_t *p = band == 1 ? &snapshot->desired_protocols[i].ghz_2g : &snapshot->desired_protocols[i].ghz_5g;
            wifi_bandwidth_t *bw = band == 1 ? &snapshot->desired_bandwidths[i].ghz_2g : &snapshot->desired_bandwidths[i].ghz_5g;
            if (controls->protocol_bands[i] & band) *p = band == 1 ? controls->protocols[i].ghz_2g : controls->protocols[i].ghz_5g;
            if (controls->bandwidth_bands[i] & band) *bw = band == 1 ? controls->bandwidths[i].ghz_2g : controls->bandwidths[i].ghz_5g;
            if (!(snapshot->active_bands & band)) continue;
            if ((*bw != WIFI_BW20 && *bw != WIFI_BW40) ||
                (*bw == WIFI_BW40 && (!(*p & WIFI_PROTOCOL_11N) || (*p & (WIFI_PROTOCOL_11AC | WIFI_PROTOCOL_11AX)))))
                return ESP_ERR_INVALID_ARG;
        }
    }
    if (controls->power_save_set) {
        result->stage = "power-save-snapshot";
        err = esp_wifi_get_ps(&snapshot->power_save);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static esp_err_t wifi_radio_apply_country(
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    wifi_radio_controls_snapshot_t *snapshot, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (controls == NULL || !controls->country_set) return ESP_OK;
    result->stage = "country-config";
    snapshot->country_touched = true;
    /* Both country setters persist independently of WIFI_STORAGE_RAM. */
    result->persistent_mutation_possible = true;
    esp_err_t err = controls->country_by_code ?
        esp_wifi_set_country_code(controls->country.cc, controls->country.policy == WIFI_COUNTRY_POLICY_AUTO) :
        esp_wifi_set_country(&controls->country);
    if (err != ESP_OK) return err;
    result->stage = "country-config-readback";
    wifi_country_t actual = {0};
    err = esp_wifi_get_country(&actual);
    if (err != ESP_OK) return err;
    if (controls->country_by_code) {
        if (actual.cc[0] != controls->country.cc[0] || actual.cc[1] != controls->country.cc[1] ||
            actual.policy != controls->country.policy) return ESP_ERR_INVALID_RESPONSE;
    } else if (!wifi_radio_country_equal(&controls->country, &actual, false)) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

static esp_err_t wifi_radio_apply_controls(
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    wifi_radio_controls_snapshot_t *snapshot, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (controls == NULL) return ESP_OK;
    for (size_t i = 0; i < 2; ++i) {
        if (!(controls->protocol_bands[i] | controls->bandwidth_bands[i])) continue;
        wifi_interface_t iface = i == 0 ? WIFI_IF_STA : WIFI_IF_AP;
        result->stage = i == 0 ? "station-phy-config" : "ap-phy-config";
        snapshot->phy_touched[i] = true;
        esp_err_t err = wifi_radio_write_phy(iface, &snapshot->desired_protocols[i], &snapshot->desired_bandwidths[i]);
        if (err != ESP_OK) return err;
        result->stage = i == 0 ? "station-phy-readback" : "ap-phy-readback";
        wifi_protocols_t actual;
        wifi_bandwidths_t bandwidths;
        err = wifi_radio_read_phy(iface, &actual, &bandwidths);
        if (err != ESP_OK) return err;
        if (!wifi_radio_phy_equal(snapshot->active_bands, &snapshot->desired_protocols[i],
            &snapshot->desired_bandwidths[i], &actual, &bandwidths)) return ESP_ERR_INVALID_RESPONSE;
    }
    if (controls->power_save_set) {
        result->stage = "power-save-config";
        snapshot->power_save_touched = true;
        esp_err_t err = esp_wifi_set_ps(controls->power_save);
        if (err != ESP_OK) return err;
        result->stage = "power-save-readback";
        wifi_ps_type_t actual;
        err = esp_wifi_get_ps(&actual);
        if (err != ESP_OK) return err;
        if (actual != controls->power_save) return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t wifi_radio_restore_controls(wifi_radio_controls_snapshot_t *snapshot,
    esp32_mquickjs_wifi_radio_config_result_t *result, bool country_only)
{
    esp_err_t err;
    if (country_only) {
        if (!snapshot->country_touched) return ESP_OK;
        result->rollback_stage = "rollback-country";
        err = esp_wifi_set_country(&snapshot->country);
        if (err != ESP_OK) return err;
        result->rollback_stage = "rollback-country-readback";
        wifi_country_t actual = {0};
        err = esp_wifi_get_country(&actual);
        if (err != ESP_OK) return err;
        /* max_tx_power is read-only. If country/PHY changes it, a stopped
         * transaction cannot prove restoration; retain the fault. */
        return wifi_radio_country_equal(&snapshot->country, &actual, true) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    }
    for (int i = 1; i >= 0; --i) {
        if (!snapshot->phy_touched[i]) continue;
        wifi_interface_t iface = i == 0 ? WIFI_IF_STA : WIFI_IF_AP;
        result->rollback_stage = i == 0 ? "rollback-station-phy" : "rollback-ap-phy";
        err = wifi_radio_write_phy(iface, &snapshot->protocols[i], &snapshot->bandwidths[i]);
        if (err != ESP_OK) return err;
        result->rollback_stage = i == 0 ? "rollback-station-phy-readback" : "rollback-ap-phy-readback";
        wifi_protocols_t actual;
        wifi_bandwidths_t bandwidths;
        err = wifi_radio_read_phy(iface, &actual, &bandwidths);
        if (err != ESP_OK) return err;
        if (!wifi_radio_phy_equal(snapshot->active_bands, &snapshot->protocols[i], &snapshot->bandwidths[i],
            &actual, &bandwidths)) return ESP_ERR_INVALID_RESPONSE;
    }
    if (snapshot->power_save_touched) {
        result->rollback_stage = "rollback-power-save";
        err = esp_wifi_set_ps(snapshot->power_save);
        if (err != ESP_OK) return err;
        result->rollback_stage = "rollback-power-save-readback";
        wifi_ps_type_t actual;
        err = esp_wifi_get_ps(&actual);
        if (err != ESP_OK) return err;
        if (actual != snapshot->power_save) return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_write_phy(wifi_interface_t interface,
    esp32_mquickjs_wifi_phy_query_t query, const esp32_mquickjs_wifi_phy_readback_t *requested,
    esp32_mquickjs_wifi_phy_readback_t *output, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "phy-admission", .error = ESP_ERR_INVALID_ARG};
    if (requested == NULL || output == NULL || requested == output) return ESP_ERR_INVALID_ARG;
    memset(output, 0, sizeof(*output));
    if ((interface != WIFI_IF_STA && interface != WIFI_IF_AP) ||
        (query != ESP32_MQUICKJS_WIFI_PHY_PROTOCOL && query != ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS &&
         query != ESP32_MQUICKJS_WIFI_PHY_BANDWIDTH && query != ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS))
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) { result->error = ESP_ERR_NOT_SUPPORTED; return result->error; }
#endif
    bool plural = query == ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS || query == ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS;
    bool protocol = query == ESP32_MQUICKJS_WIFI_PHY_PROTOCOL || query == ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS;
    if (requested->bands == 0U || (requested->bands & ~3U) || (!plural && requested->bands != 1U))
        return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_radio_config_controls_t controls = {0};
    wifi_radio_controls_snapshot_t snapshot = {0};
    unsigned index = interface == WIFI_IF_STA ? 0 : 1;
    uint8_t bands = requested->bands;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.started || s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
        s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        s_radio.wake_locks != 0U || s_radio.promiscuous_claimed || s_tx_rate_lease.identity != 0U ||
        !(s_radio.effective_mode & (interface == WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP))) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    controls.protocols[index] = requested->protocols;
    controls.bandwidths[index] = requested->bandwidths;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    if (!plural) {
        wifi_band_mode_t mode;
        result->stage = "band-mode-snapshot";
        err = esp_wifi_get_band_mode(&mode);
        if (err != ESP_OK) goto done;
        if (mode == WIFI_BAND_MODE_AUTO) { err = ESP_ERR_NOT_SUPPORTED; goto done; }
        if (mode == WIFI_BAND_MODE_5G_ONLY) {
            bands = 2U;
            controls.protocols[index].ghz_5g = requested->protocols.ghz_2g;
            controls.bandwidths[index].ghz_5g = requested->bandwidths.ghz_2g;
        } else if (mode != WIFI_BAND_MODE_2G_ONLY) { err = ESP_ERR_INVALID_RESPONSE; goto done; }
    }
#endif
    if (protocol) controls.protocol_bands[index] = bands;
    else controls.bandwidth_bands[index] = bands;
    result->stage = "phy-validate";
    err = esp32_mquickjs_wifi_radio_validate_config_controls(s_radio.effective_mode, &controls);
    if (err != ESP_OK) goto done;
    err = wifi_radio_snapshot_controls(&controls, &snapshot, result);
    if (err != ESP_OK) goto done;
    /* Rollback must be able to reproduce every active band, including a band
     * omitted by the request. Do not replay unknown/normalized protocol bits. */
    result->stage = "phy-predecessor";
    for (unsigned band = 1; band <= 2; band <<= 1) {
        if (!(snapshot.active_bands & band)) continue;
        uint16_t previous = band == 1 ? snapshot.protocols[index].ghz_2g : snapshot.protocols[index].ghz_5g;
        wifi_bandwidth_t width = band == 1 ? snapshot.bandwidths[index].ghz_2g : snapshot.bandwidths[index].ghz_5g;
        err = wifi_radio_validate_protocol(previous, band == 2);
        if (err != ESP_OK) goto done;
        if ((width != WIFI_BW20 && width != WIFI_BW40) ||
            (width == WIFI_BW40 && (!(previous & WIFI_PROTOCOL_11N) || (previous & (WIFI_PROTOCOL_11AC | WIFI_PROTOCOL_11AX))))) {
            err = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
    }
    result->mutation_attempted = true;
    result->persistent_mutation_possible = s_radio.storage == WIFI_STORAGE_FLASH;
    err = wifi_radio_apply_controls(&controls, &snapshot, result);
    if (err != ESP_OK) {
        result->rollback_attempted = true;
        result->rollback_error = wifi_radio_restore_controls(&snapshot, result, false);
        result->rollback_complete = result->rollback_error == ESP_OK;
        if (result->rollback_complete) result->rollback_stage = NULL;
        if (!result->rollback_complete || result->persistent_mutation_possible)
            (void)wifi_radio_record_fault(result->stage, err);
        if (!result->rollback_complete)
            (void)wifi_radio_cleanup_fault("phy-rollback", result->rollback_error);
        goto done;
    }
    output->bands = plural ? snapshot.active_bands : 1U;
    output->protocols = snapshot.desired_protocols[index];
    output->bandwidths = snapshot.desired_bandwidths[index];
    if (!plural && bands == 2U) {
        output->protocols.ghz_2g = output->protocols.ghz_5g;
        output->bandwidths.ghz_2g = output->bandwidths.ghz_5g;
    }
    if (!(output->bands & 1U)) { output->protocols.ghz_2g = 0; output->bandwidths.ghz_2g = 0; }
    if (!(output->bands & 2U)) { output->protocols.ghz_5g = 0; output->bandwidths.ghz_5g = 0; }
    result->stage = "complete";
done:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = *result;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

/* Caller has already validated the input and reserved the stopped driver.
 * All temporary config/credential storage is allocated before the first SDK
 * mutation and securely wiped on every exit. No callbacks acquire this mutex. */
static esp_err_t wifi_radio_configure_locked(wifi_mode_t mode, wifi_storage_t storage,
    wifi_config_t *station, esp32_mquickjs_wifi_config_accept_fn accept_station,
    wifi_config_t *access_point, esp32_mquickjs_wifi_config_accept_fn accept_access_point,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "config-admission"};
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if ((mode != WIFI_MODE_STA && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) ||
        (storage != WIFI_STORAGE_RAM && storage != WIFI_STORAGE_FLASH) ||
        (station != NULL && (!(mode & WIFI_MODE_STA) || accept_station == NULL)) ||
        (access_point != NULL && (!(mode & WIFI_MODE_AP) || accept_access_point == NULL))) goto record;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if ((mode & WIFI_MODE_AP) || access_point != NULL) { err = ESP_ERR_NOT_SUPPORTED; goto record; }
#endif
    err = esp32_mquickjs_wifi_radio_validate_config_controls(mode, controls);
    if (err != ESP_OK) goto record;
    if (station != NULL && !station->sta.pmf_cfg.capable &&
        (station->sta.pmf_cfg.required || !esp32_mquickjs_wifi_radio_pmf_disable_allowed(WIFI_IF_STA, station))) {
        result->stage = "station-pmf-security";
        err = ESP_ERR_INVALID_ARG;
        goto record;
    }
    if (access_point != NULL && !access_point->ap.pmf_cfg.capable &&
        (access_point->ap.pmf_cfg.required || !esp32_mquickjs_wifi_radio_pmf_disable_allowed(WIFI_IF_AP, access_point))) {
        result->stage = "ap-pmf-security";
        err = ESP_ERR_INVALID_ARG;
        goto record;
    }
    err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned || !s_radio.storage_configured || s_radio.started ||
        s_radio.stop_required || s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED) goto record;
    if (access_point != NULL && access_point->ap.channel != 0 &&
        (controls == NULL || !controls->country_set)) {
        result->stage = "ap-regulatory";
        err = wifi_radio_validate_regulatory_channel(access_point->ap.channel);
        if (err != ESP_OK) goto record;
    }
    struct {
        wifi_config_t before[2];
        wifi_config_t actual[2];
        wifi_radio_controls_snapshot_t controls;
    } *snapshots = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*snapshots), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (snapshots == NULL) { result->stage = "config-allocate"; err = ESP_ERR_NO_MEM; goto record; }
    wifi_config_t *requested[2] = {station, access_point};
    esp32_mquickjs_wifi_config_accept_fn accept[2] = {accept_station, accept_access_point};
    const wifi_interface_t interfaces[2] = {WIFI_IF_STA, WIFI_IF_AP};
    const char *snapshot_stages[2] = {"station-snapshot", "ap-snapshot"};
    const char *write_stages[2] = {"station-config", "ap-config"};
    const char *read_stages[2] = {"station-config-readback", "ap-config-readback"};
    bool touched[2] = {false, false};
    wifi_storage_t old_storage = s_radio.storage;
    wifi_mode_t old_mode, actual_mode;
    result->stage = "mode-snapshot";
    err = esp_wifi_get_mode(&old_mode);
    if (err != ESP_OK) goto free_snapshots;
    if (old_mode != WIFI_MODE_NULL && old_mode != WIFI_MODE_STA &&
        old_mode != WIFI_MODE_AP && old_mode != WIFI_MODE_APSTA) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto free_snapshots;
    }
    for (size_t i = 0; i < 2; ++i) {
        if (requested[i] == NULL) continue;
        result->stage = snapshot_stages[i];
        err = esp_wifi_get_config(interfaces[i], &snapshots->before[i]);
        if (err != ESP_OK) goto free_snapshots;
    }
    err = wifi_radio_snapshot_controls(controls, &snapshots->controls, result);
    if (err != ESP_OK) goto free_snapshots;
    result->mutation_attempted = true;
    result->stage = "config-storage";
    err = esp_wifi_set_storage(storage);
    if (err != ESP_OK) goto rollback;
    /* A native failure can follow a partial persistent write. RAM readback
     * cannot establish what the previous NVS image contained. */
    result->persistent_mutation_possible = storage == WIFI_STORAGE_FLASH;
    err = wifi_radio_apply_country(controls, &snapshots->controls, result);
    if (err != ESP_OK) goto rollback;
    if (access_point != NULL && access_point->ap.channel != 0 && controls != NULL && controls->country_set) {
        result->stage = "ap-regulatory";
        err = wifi_radio_validate_regulatory_channel(access_point->ap.channel);
        if (err != ESP_OK) goto rollback;
    }
    result->stage = "config-mode";
    err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) goto rollback;
    result->stage = "config-mode-readback";
    err = esp_wifi_get_mode(&actual_mode);
    if (err == ESP_OK && actual_mode != mode) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) goto rollback;
    for (size_t i = 0; i < 2; ++i) {
        if (requested[i] == NULL) continue;
        result->stage = write_stages[i];
        snapshots->actual[i] = *requested[i];
        touched[i] = true;
        err = esp_wifi_set_config(interfaces[i], &snapshots->actual[i]);
        if (err != ESP_OK) goto rollback;
        if (!(interfaces[i] == WIFI_IF_AP ? requested[i]->ap.pmf_cfg.capable : requested[i]->sta.pmf_cfg.capable)) {
            /* Captured input uses capable=false only for an explicit
             * disable. The dedicated SDK call must precede readback/START. */
            result->stage = interfaces[i] == WIFI_IF_AP ? "ap-pmf-config" : "station-pmf-config";
            err = esp_wifi_disable_pmf_config(interfaces[i]);
            if (err != ESP_OK) goto rollback;
        }
        result->stage = read_stages[i];
        memset(&snapshots->actual[i], 0, sizeof(snapshots->actual[i]));
        err = esp_wifi_get_config(interfaces[i], &snapshots->actual[i]);
        if (err == ESP_OK && !accept[i](requested[i], &snapshots->actual[i]))
            err = ESP_ERR_INVALID_RESPONSE;
        if (err != ESP_OK) goto rollback;
    }
    err = wifi_radio_apply_controls(controls, &snapshots->controls, result);
    if (err != ESP_OK) goto rollback;
    /* Later interface-global settings must not silently weaken a previously
     * accepted interface config. Publish only the final native readback. */
    if (controls != NULL) {
        for (size_t i = 0; i < 2; ++i) {
            if (requested[i] == NULL) continue;
            result->stage = read_stages[i];
            memset(&snapshots->actual[i], 0, sizeof(snapshots->actual[i]));
            err = esp_wifi_get_config(interfaces[i], &snapshots->actual[i]);
            if (err == ESP_OK && !accept[i](requested[i], &snapshots->actual[i]))
                err = ESP_ERR_INVALID_RESPONSE;
            if (err != ESP_OK) goto rollback;
        }
    }
    for (size_t i = 0; i < 2; ++i)
        if (requested[i] != NULL) *requested[i] = snapshots->actual[i];
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.effective_mode = mode;
    s_radio.storage = storage;
    taskEXIT_CRITICAL(&s_radio.lock);
    result->stage = "complete";
    goto free_snapshots;

rollback:
    result->rollback_attempted = true;
    result->rollback_stage = "rollback-storage-ram";
    result->rollback_error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result->rollback_error != ESP_OK) goto rollback_failed;
    result->rollback_error = wifi_radio_restore_controls(&snapshots->controls, result, true);
    if (result->rollback_error != ESP_OK) goto rollback_failed;
    /* Enable every interface that might need restoration, even if it was
     * disabled in the old mode. Stay stopped throughout this sequence. */
    result->rollback_stage = "rollback-mode-enable";
    result->rollback_error = esp_wifi_set_mode((wifi_mode_t)(old_mode | mode));
    if (result->rollback_error != ESP_OK) goto rollback_failed;
    for (size_t i = 0; i < 2; ++i) {
        if (!touched[i]) continue;
        result->rollback_stage = i == 0 ? "rollback-station" : "rollback-ap";
        snapshots->actual[i] = snapshots->before[i];
        result->rollback_error = esp_wifi_set_config(interfaces[i], &snapshots->actual[i]);
        if (result->rollback_error != ESP_OK) goto rollback_failed;
        result->rollback_stage = i == 0 ? "rollback-station-pmf" : "rollback-ap-pmf";
        result->rollback_error = wifi_radio_restore_disabled_pmf(interfaces[i], &snapshots->before[i]);
        if (result->rollback_error != ESP_OK) goto rollback_failed;
        result->rollback_stage = i == 0 ? "rollback-station-readback" : "rollback-ap-readback";
        memset(&snapshots->actual[i], 0, sizeof(snapshots->actual[i]));
        result->rollback_error = esp_wifi_get_config(interfaces[i], &snapshots->actual[i]);
        if (result->rollback_error == ESP_OK &&
            !wifi_radio_config_equal(interfaces[i], &snapshots->before[i], &snapshots->actual[i]))
            result->rollback_error = ESP_ERR_INVALID_RESPONSE;
        if (result->rollback_error != ESP_OK) goto rollback_failed;
    }
    result->rollback_error = wifi_radio_restore_controls(&snapshots->controls, result, false);
    if (result->rollback_error != ESP_OK) goto rollback_failed;
    result->rollback_stage = "rollback-mode";
    result->rollback_error = esp_wifi_set_mode(old_mode);
    if (result->rollback_error != ESP_OK) goto rollback_failed;
    result->rollback_stage = "rollback-mode-readback";
    result->rollback_error = esp_wifi_get_mode(&actual_mode);
    if (result->rollback_error == ESP_OK && actual_mode != old_mode)
        result->rollback_error = ESP_ERR_INVALID_RESPONSE;
    if (result->rollback_error != ESP_OK) goto rollback_failed;
    result->rollback_stage = "rollback-storage";
    result->rollback_error = esp_wifi_set_storage(old_storage);
    if (result->rollback_error != ESP_OK) goto rollback_failed;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.effective_mode = old_mode;
    s_radio.storage = old_storage;
    taskEXIT_CRITICAL(&s_radio.lock);
    result->rollback_complete = true; /* Runtime config only, not NVS history. */
    result->rollback_stage = NULL;
    if (result->persistent_mutation_possible)
        (void)wifi_radio_record_fault(result->stage, err);
    goto free_snapshots;
rollback_failed:
    /* Temporary credentials are not retained to retry a partly known config.
     * Keep the lifecycle reservation faulted; explicit shutdown is required. */
    (void)wifi_radio_record_fault(result->stage, err);
    (void)wifi_radio_cleanup_fault("configuration-rollback", result->rollback_error);
free_snapshots:
    esp32_mquickjs_wireless_secure_zero(snapshots, sizeof(*snapshots));
    esp32_mquickjs_memory_payload_free(snapshots);
record:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = *result;
    taskEXIT_CRITICAL(&s_radio.lock);
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_read_interface_config(wifi_interface_t interface,
    bool include_secrets, wifi_config_t *config, const char **stage)
{
    if (config != NULL) esp32_mquickjs_wireless_secure_zero(config, sizeof(*config));
    if (stage != NULL) *stage = "admission";
    if (config == NULL || stage == NULL || (interface != WIFI_IF_STA && interface != WIFI_IF_AP))
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
#if !CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK
    if (include_secrets) { *stage = "secret-readback"; return ESP_ERR_NOT_ALLOWED; }
#endif
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)) goto done;
    *stage = "interface-config";
    err = esp_wifi_get_config(interface, config);
    if (err != ESP_OK) goto done;
    if (!include_secrets) {
        if (interface == WIFI_IF_STA) {
            esp32_mquickjs_wireless_secure_zero(config->sta.password, sizeof(config->sta.password));
            esp32_mquickjs_wireless_secure_zero(config->sta.sae_h2e_identifier, sizeof(config->sta.sae_h2e_identifier));
        } else esp32_mquickjs_wireless_secure_zero(config->ap.password, sizeof(config->ap.password));
    }
    *stage = NULL;
done:
    if (err != ESP_OK) esp32_mquickjs_wireless_secure_zero(config, sizeof(*config));
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_write_interface_config(wifi_interface_t interface,
    wifi_config_t *config, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "config-admission", .error = ESP_ERR_INVALID_ARG};
    if (config == NULL || (interface != WIFI_IF_STA && interface != WIFI_IF_AP)) return result->error;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) { result->error = ESP_ERR_NOT_SUPPORTED; return result->error; }
#endif
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.started || s_radio.stop_required || s_radio.restart_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED || s_radio.lifecycle.identity != 0U ||
        s_radio.operation.identity != 0U || s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL ||
        s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending ||
        !(s_radio.effective_mode & (interface == WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP))) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    err = wifi_radio_configure_locked(s_radio.effective_mode, s_radio.storage,
        interface == WIFI_IF_STA ? config : NULL, esp32_mquickjs_wifi_radio_accept_station_config,
        interface == WIFI_IF_AP ? config : NULL, esp32_mquickjs_wifi_radio_accept_ap_config, NULL, result);
done:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.configuration = *result; taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_initialize_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (token == NULL || token->identity == 0U) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (token->identity != s_radio.lifecycle.identity || token->generation != s_radio.lifecycle.generation ||
        s_radio.started || s_radio.stop_required || s_radio.operation.identity != 0U ||
        s_radio.wake_locks != 0 || s_radio.promiscuous_claimed || s_radio.restart_required ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED)) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    err = wifi_radio_initialize();
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_configure_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token,
    wifi_mode_t mode, wifi_storage_t storage,
    wifi_config_t *station, esp32_mquickjs_wifi_config_accept_fn accept_station,
    wifi_config_t *access_point, esp32_mquickjs_wifi_config_accept_fn accept_access_point,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "config-admission", .error = ESP_ERR_INVALID_STATE};
    if (token == NULL || token->identity == 0U) {
        result->error = ESP_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (token->identity != s_radio.lifecycle.identity || token->generation != s_radio.lifecycle.generation ||
        s_radio.operation.identity != 0 || s_radio.wake_locks != 0 || s_radio.promiscuous_claimed) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    if (s_vendor_ie.start_owner.identity != 0U) {
        if (!wifi_radio_vendor_ie_start_matches(token) || mode != s_vendor_ie.start_mode ||
            storage != s_vendor_ie.start_storage || station != NULL || access_point != NULL ||
            accept_station != NULL || accept_access_point != NULL || controls != NULL ||
            !s_radio.driver_owned || !s_radio.storage_configured || s_radio.started || s_radio.stop_required ||
            s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED || s_radio.restart_required ||
            s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL) goto done;
        wifi_mode_t observed = WIFI_MODE_NULL;
        result->stage = "vendor-ie-start-mode";
        err = esp_wifi_get_mode(&observed);
        if (err == ESP_OK && observed != mode) err = ESP_ERR_INVALID_RESPONSE;
        result->error = err;
        if (err == ESP_OK) result->stage = "complete";
        taskENTER_CRITICAL(&s_radio.lock); s_radio.configuration = *result; taskEXIT_CRITICAL(&s_radio.lock);
        goto done;
    }
#endif
    err = wifi_radio_configure_locked(mode, storage, station, accept_station,
        access_point, accept_access_point, controls, result);
done:
    wifi_radio_operation_unlock();
    return err;
}

bool esp32_mquickjs_wifi_radio_accept_ap_config(const wifi_config_t *requested, const wifi_config_t *actual)
{
    size_t password_length = strnlen((const char *)requested->ap.password, sizeof(requested->ap.password));
    return actual->ap.ssid_len == requested->ap.ssid_len &&
        memcmp(actual->ap.ssid, requested->ap.ssid, requested->ap.ssid_len) == 0 &&
        strnlen((const char *)actual->ap.password, sizeof(actual->ap.password)) == password_length &&
        memcmp(actual->ap.password, requested->ap.password, password_length) == 0 &&
        actual->ap.authmode == (requested->ap.wpa3_compatible_mode ? WIFI_AUTH_WPA2_PSK : requested->ap.authmode) &&
        actual->ap.sae_ext == requested->ap.sae_ext &&
        actual->ap.wpa3_compatible_mode == requested->ap.wpa3_compatible_mode &&
        actual->ap.bss_max_idle_cfg.period == requested->ap.bss_max_idle_cfg.period &&
        actual->ap.bss_max_idle_cfg.protected_keep_alive == requested->ap.bss_max_idle_cfg.protected_keep_alive &&
        (requested->ap.pmf_cfg.capable ?
            ((actual->ap.pmf_cfg.capable || requested->ap.authmode == WIFI_AUTH_OPEN || requested->ap.authmode == WIFI_AUTH_WPA_PSK) &&
             (actual->ap.pmf_cfg.required || !requested->ap.pmf_cfg.required)) :
            (!actual->ap.pmf_cfg.capable && !actual->ap.pmf_cfg.required)) &&
        (actual->ap.transition_disable || !requested->ap.transition_disable) &&
        (requested->ap.sae_pwe_h2e == WPA3_SAE_PWE_UNSPECIFIED || actual->ap.sae_pwe_h2e == requested->ap.sae_pwe_h2e) &&
        (requested->ap.gtk_rekey_interval == 0 || actual->ap.gtk_rekey_interval == requested->ap.gtk_rekey_interval) &&
        (requested->ap.authmode == WIFI_AUTH_OPEN || actual->ap.pairwise_cipher == requested->ap.pairwise_cipher);
}
#endif


#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
static int wifi_radio_wake_slot(const esp32_mquickjs_wifi_wake_token_t *token)
{
    if (token == NULL || token->identity == 0) return -1;
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MAX_WAKE_LOCKS; ++i)
        if (s_wake_tokens[i].identity == token->identity &&
            s_wake_tokens[i].generation == token->generation) return (int)i;
    return -1;
}

esp_err_t esp32_mquickjs_wifi_radio_wake_acquire(esp32_mquickjs_wifi_wake_token_t *token)
{
    if (token == NULL || token->identity != 0) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (WIFI_RADIO_SMARTCONFIG_PENDING || WIFI_RADIO_WPS_PENDING) goto done;
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
    if (WIFI_RADIO_DPP_PENDING) goto done;
#endif
    if (s_tx_rate_lease.identity != 0U || !s_radio.started || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED ||
        s_radio.lifecycle.identity != 0 || s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL) goto done;
    if (s_interval.owner.identity != 0U || s_interval.uncertain) goto done;
    size_t slot;
    for (slot = 0; slot < ESP32_MQUICKJS_WIFI_MAX_WAKE_LOCKS; ++slot)
        if (s_wake_tokens[slot].identity == 0) break;
    if (slot == ESP32_MQUICKJS_WIFI_MAX_WAKE_LOCKS || s_next_wake_identity == 0) { err = ESP_ERR_NO_MEM; goto done; }
    uint32_t identity = s_next_wake_identity++;
    err = esp_wifi_force_wakeup_acquire();
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.wake_lock_error = err;
    if (err == ESP_OK) {
        *token = (esp32_mquickjs_wifi_wake_token_t){.generation = s_radio.generation, .identity = identity};
        s_wake_tokens[slot] = *token;
        s_radio.wake_locks++;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
done:
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_wake_release_locked(esp32_mquickjs_wifi_wake_token_t *token)
{
    int slot = wifi_radio_wake_slot(token);
    if (slot < 0) { memset(token, 0, sizeof(*token)); return ESP_OK; }
    esp_err_t err = esp_wifi_force_wakeup_release();
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.wake_lock_error = err;
    if (err == ESP_OK) {
        memset(&s_wake_tokens[slot], 0, sizeof(s_wake_tokens[slot]));
        s_radio.wake_locks--;
        memset(token, 0, sizeof(*token));
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_wake_release(esp32_mquickjs_wifi_wake_token_t *token)
{
    if (token == NULL) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_wake_release_locked(token);
    wifi_radio_operation_unlock();
    return err;
}

bool esp32_mquickjs_wifi_radio_wake_active(const esp32_mquickjs_wifi_wake_token_t *token)
{
    wifi_radio_operation_lock();
    bool active = wifi_radio_wake_slot(token) >= 0;
    wifi_radio_operation_unlock();
    return active;
}

esp_err_t esp32_mquickjs_wifi_radio_wake_release_all(void)
{
    wifi_radio_operation_lock();
    esp_err_t result = ESP_OK;
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MAX_WAKE_LOCKS; ++i) {
        esp32_mquickjs_wifi_wake_token_t token = s_wake_tokens[i];
        if (token.identity == 0) continue;
        esp_err_t err = wifi_radio_wake_release_locked(&token);
        if (result == ESP_OK && err != ESP_OK) result = err;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.wake_lock_error = result;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return result;
}

esp_err_t esp32_mquickjs_wifi_radio_set_country_details(const wifi_country_t *country,
    wifi_country_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "country-admission", .error = ESP_ERR_INVALID_ARG};
    if (country == NULL || actual == NULL || country == actual) return ESP_ERR_INVALID_ARG;
    memset(actual, 0, sizeof(*actual));
    esp32_mquickjs_wifi_radio_config_controls_t controls = {.country_set = true, .country = *country};
    esp_err_t err = esp32_mquickjs_wifi_radio_validate_config_controls(WIFI_MODE_STA, &controls);
    if (err != ESP_OK) { result->error = err; return err; }
    wifi_radio_operation_lock();
    err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.started || s_radio.stop_required || s_radio.restart_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED || s_radio.fault_stage != NULL ||
        s_radio.cleanup_stage != NULL || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    wifi_radio_controls_snapshot_t before = {0};
    err = wifi_radio_snapshot_controls(&controls, &before, result);
    if (err != ESP_OK) goto done;
    if (!wifi_radio_country_valid(&before.country)) { err = ESP_ERR_INVALID_RESPONSE; goto done; }
    if (wifi_radio_country_equal(country, &before.country, false)) {
        *actual = before.country; result->stage = "complete"; goto done;
    }
    result->mutation_attempted = true;
    err = wifi_radio_apply_country(&controls, &before, result);
    if (err == ESP_OK) {
        result->stage = "country-result-readback";
        wifi_country_t observed = {0};
        err = esp_wifi_get_country(&observed);
        if (err == ESP_OK && (!wifi_radio_country_valid(&observed) ||
            !wifi_radio_country_equal(country, &observed, false))) err = ESP_ERR_INVALID_RESPONSE;
        if (err == ESP_OK) { *actual = observed; result->stage = "complete"; goto done; }
    }
    result->rollback_attempted = true;
    result->rollback_error = wifi_radio_restore_controls(&before, result, true);
    result->rollback_complete = result->rollback_error == ESP_OK;
    if (result->rollback_complete) result->rollback_stage = NULL;
    else {
        (void)wifi_radio_record_fault(result->stage, err);
        (void)wifi_radio_cleanup_fault("country-rollback", result->rollback_error);
    }
done:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = *result;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_set_country_code(
    const esp32_mquickjs_wifi_radio_lease_t *lease, const char code[3], bool ieee80211d,
    wifi_country_t *actual, esp32_mquickjs_wifi_radio_mutation_t *mutation)
{
    if (mutation == NULL) return ESP_ERR_INVALID_ARG;
    *mutation = (esp32_mquickjs_wifi_radio_mutation_t){.stage = "admission"};
    if (code == NULL || actual == NULL || code[2] != '\0' ||
        !((code[0] >= 'A' && code[0] <= 'Z' && code[1] >= 'A' && code[1] <= 'Z') ||
          (code[0] == '0' && code[1] == '1'))) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp32_mquickjs_wifi_radio_config_result_t result = {.stage = "admission"};
    esp_err_t err = wifi_radio_configuration_owner_locked(lease);
    if (err != ESP_OK) goto done;
    if (lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA ||
        s_radio.effective_mode != WIFI_MODE_STA || !s_radio.driver_owned || !s_radio.storage_configured ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.restart_required ||
        s_radio.cleanup_stage != NULL || s_radio.wake_locks != 0U ||
        s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending) {
        err = ESP_ERR_INVALID_STATE; goto done;
    }
    wifi_ap_record_t ap;
    err = esp_wifi_sta_get_ap_info(&ap);
    if (err == ESP_OK) { err = ESP_ERR_INVALID_STATE; goto done; }
    if (err != ESP_ERR_WIFI_NOT_CONNECT) goto done;
    wifi_radio_controls_snapshot_t before = {0};
    result.stage = "country-snapshot";
    err = esp_wifi_get_country(&before.country);
    if (err != ESP_OK) goto done;
    if (!wifi_radio_country_valid(&before.country)) { err = ESP_ERR_INVALID_RESPONSE; goto done; }
    result.stage = "country-set";
    result.mutation_attempted = before.country_touched = true;
    /* The SDK can change RAM/NVS before returning an error, independently of
     * the selected Station/AP storage policy. Never auto-repeat that write. */
    result.persistent_mutation_possible = true;
    err = esp_wifi_set_country_code(code, ieee80211d);
    if (err != ESP_OK) goto rollback;
    mutation->driver_accepted = true;
    result.stage = "country-readback";
    wifi_country_t country = {0};
    err = esp_wifi_get_country(&country);
    if (err == ESP_OK && (!wifi_radio_country_valid(&country) ||
        country.cc[0] != code[0] || country.cc[1] != code[1] ||
        country.policy != (ieee80211d ? WIFI_COUNTRY_POLICY_AUTO : WIFI_COUNTRY_POLICY_MANUAL)))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) { *actual = country; result.stage = "complete"; goto done; }
rollback:
    result.rollback_attempted = true;
    result.rollback_error = wifi_radio_restore_controls(&before, &result, true);
    result.rollback_complete = result.rollback_error == ESP_OK;
    if (result.rollback_complete) result.rollback_stage = NULL;
    else {
        (void)wifi_radio_record_fault(result.stage, err);
        (void)wifi_radio_cleanup_fault("country-rollback", result.rollback_error);
    }
done:
    result.error = err;
    mutation->stage = err == ESP_OK ? NULL : result.stage;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = result;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_change_channel(
    const esp32_mquickjs_wifi_radio_lease_t *lease, uint8_t primary, wifi_second_chan_t secondary,
    uint8_t *actual_primary, wifi_second_chan_t *actual_secondary, uint32_t *generation,
    esp32_mquickjs_wifi_radio_mutation_t *mutation)
{
    if (mutation == NULL) return ESP_ERR_INVALID_ARG;
    *mutation = (esp32_mquickjs_wifi_radio_mutation_t){.stage = "admission"};
    if (primary == 0 || actual_primary == NULL || actual_secondary == NULL || generation == NULL ||
        (secondary != WIFI_SECOND_CHAN_NONE && secondary != WIFI_SECOND_CHAN_ABOVE && secondary != WIFI_SECOND_CHAN_BELOW) ||
        (primary > 14 && (secondary != WIFI_SECOND_CHAN_NONE ||
            esp32_mquickjs_wifi_radio_5ghz_channel_bit(primary) == 0))) return ESP_ERR_INVALID_ARG;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
    if (primary > 14) return ESP_ERR_NOT_SUPPORTED;
#endif
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_configuration_owner_locked(lease);
    if (err != ESP_OK) goto done;
    if ((lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA &&
         lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP) ||
        (s_radio.effective_mode != WIFI_MODE_STA && s_radio.effective_mode != WIFI_MODE_AP)) {
        err = ESP_ERR_INVALID_STATE;
        goto done;
    }
    mutation->stage = "channel-validate";
    err = wifi_radio_validate_regulatory_channel(primary);
    if (err != ESP_OK) goto done;
    if (primary <= 14 && secondary != WIFI_SECOND_CHAN_NONE) {
        int extension = (int)primary + (secondary == WIFI_SECOND_CHAN_ABOVE ? 4 : -4);
        if (extension < 1 || extension > 14) { err = ESP_ERR_INVALID_ARG; goto done; }
        err = wifi_radio_validate_regulatory_channel((uint8_t)extension);
        if (err != ESP_OK) goto done;
    }
    uint8_t current_primary;
    wifi_second_chan_t current_secondary;
    uint32_t current_generation;
    mutation->stage = "channel-readback";
    err = wifi_radio_get_channel_locked(&current_primary, &current_secondary, &current_generation);
    if (err != ESP_OK) goto done;
    if (s_radio.effective_mode == WIFI_MODE_STA) {
        wifi_ap_record_t ap;
        err = esp_wifi_sta_get_ap_info(&ap);
        if (err == ESP_OK) {
            /* A no-op read is allowed on an established link; no channel write. */
            if (current_primary != primary || (primary <= 14 && current_secondary != secondary)) {
                mutation->stage = "admission";
                err = ESP_ERR_INVALID_STATE;
                goto done;
            }
            *actual_primary = current_primary;
            *actual_secondary = current_secondary;
            *generation = current_generation;
            mutation->stage = NULL;
            goto done;
        }
        if (err != ESP_ERR_WIFI_NOT_CONNECT) goto done;
    }
    mutation->stage = "channel-set";
    err = esp_wifi_set_channel(primary, secondary);
    if (err != ESP_OK) goto done;
    mutation->driver_accepted = true;
    mutation->stage = "channel-readback";
    err = wifi_radio_get_channel_locked(actual_primary, actual_secondary, generation);
    if (err == ESP_OK) mutation->stage = NULL;
    /* No fixed-channel lease is created: a later scan/connect may change home.
     * An AP channel switch may finish asynchronously after this readback. */
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_validate_scan_channels(
    const esp32_mquickjs_wifi_radio_operation_t *operation,
    const wifi_scan_config_t *config)
{
    if (operation == NULL || config == NULL) return ESP_ERR_INVALID_ARG;
    uint32_t ghz2 = config->channel_bitmap.ghz_2_channels;
    uint32_t ghz5 = config->channel_bitmap.ghz_5_channels;
    bool all_channels = ghz2 == 0 && ghz5 == 0 && config->channel == 0;
    if (config->channel != 0) {
        if (ghz2 != 0 || ghz5 != 0) return ESP_ERR_INVALID_ARG;
        if (config->channel <= 14) ghz2 = 1UL << config->channel;
        else {
            ghz5 = esp32_mquickjs_wifi_radio_5ghz_channel_bit(config->channel);
            if (ghz5 == 0) return ESP_ERR_INVALID_ARG;
        }
    } else if ((ghz2 & ~0x7fffUL) != 0 || (ghz5 & ~0x1fffffffUL) != 0 ||
               ((ghz2 & 1U) && ghz2 != 1U) || ((ghz5 & 1U) && ghz5 != 1U)) {
        return ESP_ERR_INVALID_ARG;
    }
    ghz2 &= ~1UL;
    ghz5 &= ~1UL;
    if (!all_channels && ghz2 == 0 && ghz5 == 0) return ESP_ERR_INVALID_ARG;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
    if (ghz5 != 0) return ESP_ERR_NOT_SUPPORTED;
#endif
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (operation->identity == 0 || operation->identity != s_radio.operation.identity ||
        operation->generation != s_radio.operation.generation ||
        operation->lease_identity != s_radio.operation.lease_identity ||
        operation->kind != ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN ||
        s_radio.operation.kind != operation->kind || !s_radio.started ||
        s_radio.lifecycle.identity != 0 || s_radio.fault_stage != NULL) goto done;
    if (all_channels) { err = ESP_OK; goto done; }
    wifi_country_t country = {0};
    err = esp_wifi_get_country(&country);
    if (err != ESP_OK) goto done;
    uint32_t allowed2 = 0;
    uint16_t end = (uint16_t)country.schan + country.nchan;
    for (uint16_t channel = country.schan; channel < end && channel <= 14; ++channel)
        if (channel != 0) allowed2 |= 1UL << channel;
    if ((ghz2 & ~allowed2) != 0) { err = ESP_ERR_NOT_ALLOWED; goto done; }
#if CONFIG_SOC_WIFI_SUPPORT_5G
    wifi_band_mode_t band;
    err = esp_wifi_get_band_mode(&band);
    if (err != ESP_OK) goto done;
    if ((ghz2 && band == WIFI_BAND_MODE_5G_ONLY) ||
        (ghz5 && band == WIFI_BAND_MODE_2G_ONLY)) { err = ESP_ERR_NOT_ALLOWED; goto done; }
    if (band != WIFI_BAND_MODE_2G_ONLY && band != WIFI_BAND_MODE_5G_ONLY &&
        band != WIFI_BAND_MODE_AUTO) { err = ESP_ERR_NOT_SUPPORTED; goto done; }
    if (ghz5 != 0) {
        /* Zero/auto means an implicit SDK regulatory table, not "all allowed".
         * Public SDK getters do not expose that table. Never silently trim a
         * requested list or invent a country-to-channel table here. */
        if (country.policy != WIFI_COUNTRY_POLICY_MANUAL || country.wifi_5g_channel_mask == 0) {
            err = ESP_ERR_NOT_SUPPORTED;
            goto done;
        }
        if ((ghz5 & ~country.wifi_5g_channel_mask) != 0) { err = ESP_ERR_NOT_ALLOWED; goto done; }
    }
#endif
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

/* Advisory samples: the SDK has no atomic multi-get link snapshot. Guard
 * both ends against a different AP/channel; never use these getters as the
 * identity of a completed connect operation. */
esp_err_t esp32_mquickjs_wifi_radio_sample_station_link(
    const esp32_mquickjs_wifi_radio_lease_t *lease,
    const uint8_t bssid[6], uint8_t channel,
    esp32_mquickjs_wifi_link_sample_t *sample)
{
    if (sample == NULL) return ESP_ERR_INVALID_ARG;
    memset(sample, 0, sizeof(*sample));
    if (bssid == NULL || channel == 0) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    wifi_ap_record_t ap;
    if (!wifi_radio_lease_valid(lease) ||
        lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA ||
        !s_radio.started || (s_radio.effective_mode != WIFI_MODE_STA && s_radio.effective_mode != WIFI_MODE_APSTA) ||
        s_radio.lifecycle.identity != 0U || s_radio.fault_stage != NULL ||
        s_radio.cleanup_stage != NULL) goto done;
    err = esp_wifi_sta_get_ap_info(&ap);
    if (err != ESP_OK) goto done;
    if (ap.primary != channel || memcmp(ap.bssid, bssid, 6) != 0) {
        err = ESP_ERR_INVALID_STATE;
        goto done;
    }
    sample->rssi_valid = esp_wifi_sta_get_rssi(&sample->rssi) == ESP_OK;
    sample->phy_valid = esp_wifi_sta_get_negotiated_phymode(&sample->phy) == ESP_OK;
    err = esp_wifi_sta_get_ap_info(&ap);
    if (err == ESP_OK && (ap.primary != channel || memcmp(ap.bssid, bssid, 6) != 0))
        err = ESP_ERR_INVALID_STATE;
done:
    wifi_radio_operation_unlock();
    if (err != ESP_OK) memset(sample, 0, sizeof(*sample));
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_get_country(wifi_country_t *country)
{
    if (country == NULL) return ESP_ERR_INVALID_ARG;
    memset(country, 0, sizeof(*country));
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_radio.driver_owned && s_radio.lifecycle.identity == 0U &&
        s_radio.fault_stage == NULL && s_radio.cleanup_stage == NULL &&
        (s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
         s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_STARTED)) {
        err = esp_wifi_get_country(country);
    }
    wifi_radio_operation_unlock();
    if (err != ESP_OK) memset(country, 0, sizeof(*country));
    return err;
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
esp_err_t esp32_mquickjs_wifi_radio_read_csi_config(
    const esp32_mquickjs_wifi_radio_lease_t *lease, wifi_csi_config_t *config,
    uint32_t *generation)
{
    if (!config || !generation) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(*config));
    *generation = 0;
    wifi_csi_config_t observed = {0};
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_lease_valid(lease) || lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI ||
        !s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.lifecycle.identity ||
        s_radio.operation.identity || s_radio.fault_stage || s_radio.cleanup_stage || s_radio.restart_required)
        goto done;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    error = esp_wifi_get_csi_config(&observed);
#else
    /* The fixed legacy target libraries declare this API but do not define it. */
    error = ESP_ERR_NOT_SUPPORTED;
#endif
    if (error == ESP_OK) {
        *config = observed;
        *generation = s_radio.generation;
    }
done:
    wifi_radio_operation_unlock();
    return error;
}
#endif

static esp_err_t wifi_radio_write_tx_rate(void *opaque, wifi_interface_t interface,
    const wifi_tx_rate_config_t *config)
{
    (void)opaque;
    wifi_tx_rate_config_t value = *config;
    return esp_wifi_config_80211_tx(interface, &value);
}

/* Own the exact Radio lease until stop and restore are both complete. A failed
 * restore is retried against the captured predecessor, never against a guessed
 * default; generic release is blocked while this record exists. */
static esp_err_t wifi_radio_restore_tx_rate_locked(const esp32_mquickjs_wifi_radio_lease_t *lease)
{
    if (!wifi_radio_lease_valid(lease) || s_tx_rate_lease.identity != lease->identity ||
        s_tx_rate_lease.generation != lease->generation || !s_radio.driver_owned ||
        !s_radio.storage_configured) return ESP_ERR_INVALID_STATE;
    s_tx_rate_lease.restore_pending = true;
    esp_err_t err = wifi_radio_stop_lease_locked(lease, false);
    if (err != ESP_OK) { s_tx_rate_lease.restore_error = err; return err; }
    err = esp32_mquickjs_wifi_tx_rate_restore(&s_tx_rates, lease->generation, lease->identity,
        &s_tx_rate_lease, wifi_radio_write_tx_rate, NULL);
    if (err != ESP_OK) return wifi_radio_cleanup_fault(s_tx_rate_restore_fault, err);
    /* Successful restore settled the rate obligation. Other lifecycle faults
     * remain diagnostic and require their own cleanup. */
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_radio.cleanup_stage == s_tx_rate_restore_fault) {
        s_radio.cleanup_stage = NULL; s_radio.cleanup_error = ESP_OK;
    }
    if (s_radio.fault_stage == s_tx_rate_restore_fault) {
        s_radio.fault_stage = NULL; s_radio.fault_error = ESP_OK;
    }
    s_radio.driver_state = s_radio.fault_stage == NULL && s_radio.cleanup_stage == NULL
        ? ESP32_MQUICKJS_WIFI_RADIO_STOPPED : ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_tx_rate_status(wifi_interface_t interface,
    esp32_mquickjs_wifi_tx_rate_record_t *record, uint32_t *generation,
    esp32_mquickjs_wifi_tx_rate_lease_t *temporary)
{
    if (record == NULL || generation == NULL ||
        (interface != WIFI_IF_STA && interface != WIFI_IF_AP)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    wifi_radio_operation_lock();
    *record = s_tx_rates.records[interface == WIFI_IF_STA ? 0 : 1];
    *generation = s_radio.generation;
    if (temporary != NULL) *temporary = s_tx_rate_lease.identity != 0U && s_tx_rate_lease.interface == interface
        ? s_tx_rate_lease : (esp32_mquickjs_wifi_tx_rate_lease_t){0};
    wifi_radio_operation_unlock();
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_configure_tx_rate(wifi_interface_t interface,
    const wifi_tx_rate_config_t *config, esp32_mquickjs_wifi_tx_rate_write_t *write,
    esp32_mquickjs_wifi_tx_rate_record_t *record, uint32_t *generation,
    esp32_mquickjs_wifi_tx_rate_lease_t *temporary)
{
    if (temporary != NULL) *temporary = (esp32_mquickjs_wifi_tx_rate_lease_t){0};
    if (write == NULL || record == NULL || generation == NULL) return ESP_ERR_INVALID_ARG;
    *write = (esp32_mquickjs_wifi_tx_rate_write_t){0};
    *record = (esp32_mquickjs_wifi_tx_rate_record_t){0};
    *generation = 0;
    if ((interface != WIFI_IF_STA && interface != WIFI_IF_AP) ||
        !esp32_mquickjs_wifi_tx_rate_valid(config)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    bool repairing = s_radio.fault_stage == s_tx_rate_fault &&
        s_radio.cleanup_stage == s_tx_rate_fault &&
        s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;
    if (!s_radio.driver_owned || !s_radio.storage_configured || s_radio.started ||
        s_radio.stop_required || s_radio.restart_required || s_radio.wake_locks != 0 ||
        s_radio.lifecycle.identity != 0 || s_radio.operation.identity != 0 ||
        s_radio.promiscuous_claimed || (!repairing &&
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
         s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL))) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0) goto done;
    err = esp32_mquickjs_wifi_tx_rate_apply(&s_tx_rates, s_radio.generation,
        interface, config, wifi_radio_write_tx_rate, NULL, write);
    if (write->attempted && esp32_mquickjs_wifi_tx_rate_uncertain(&s_tx_rates)) {
        /* Preserve the original failed write in the record; a failed restore
         * separately explains why new owners cannot safely enter. */
        if (write->uncertain) wifi_radio_cleanup_fault(s_tx_rate_fault,
            write->rollback_error != ESP_OK ? write->rollback_error : write->error);
    } else if (write->accepted && repairing) {
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.fault_stage = s_radio.cleanup_stage = NULL;
        s_radio.fault_error = s_radio.cleanup_error = ESP_OK;
        s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        taskEXIT_CRITICAL(&s_radio.lock);
    }
done:
    *record = s_tx_rates.records[interface == WIFI_IF_STA ? 0 : 1];
    *generation = s_radio.generation;
    if (temporary != NULL) *temporary = s_tx_rate_lease.identity != 0U && s_tx_rate_lease.interface == interface
        ? s_tx_rate_lease : (esp32_mquickjs_wifi_tx_rate_lease_t){0};
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_connection_owner_locked(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    wifi_interface_t interface, bool observer)
{
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (WIFI_RADIO_EAP_PENDING && !s_eap_radio.ready && !observer) return ESP_ERR_INVALID_STATE;
#endif
    if (!s_radio.driver_owned) return ESP_ERR_WIFI_NOT_INIT;
    if (!s_radio.storage_configured || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED && s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED) ||
        !(s_radio.effective_mode & (interface == WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP))) return ESP_ERR_INVALID_STATE;
    if (!observer && !s_radio.started) return ESP_ERR_WIFI_NOT_STARTED;
    if (!observer && (s_radio.wake_locks != 0U || s_radio.promiscuous_claimed || s_tx_rate_lease.identity != 0U))
        return ESP_ERR_INVALID_STATE;
    const esp32_mquickjs_wifi_radio_lease_t *owners[3] = {application, station, access_point};
    const esp32_mquickjs_wifi_radio_client_t roles[3] = {ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA, ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP};
    uint32_t identities[3] = {0};
    for (size_t i = 0; i < 3; ++i) {
        if (owners[i] == NULL || !owners[i]->acquired) continue;
        if (owners[i]->client != roles[i] || !wifi_radio_lease_valid(owners[i])) return ESP_ERR_INVALID_STATE;
        identities[i] = owners[i]->identity;
    }
    /* A running interface belongs to its helper. Stopped RSSI configuration is
     * also legal without helpers; no default owner is manufactured. */
    if (s_radio.started && identities[interface == WIFI_IF_STA ? 1 : 2] == 0U) return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (owner->identity == 0U || owner->identity == identities[0] ||
            owner->identity == identities[1] || owner->identity == identities[2]) continue;
        if (!observer || owner->client == roles[0] || owner->client == roles[1] || owner->client == roles[2])
            return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

#if CONFIG_ESP_WIFI_DPP_SUPPORT
#include "esp32_mquickjs_wifi_dpp_radio.inc"
#endif

#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
#include "esp32_mquickjs_wifi_nan_radio.inc"
#endif
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
#include "esp32_mquickjs_wifi_mesh_radio.inc"
#endif

#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp32_mquickjs_wifi_wps_radio.inc"
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
#include "esp32_mquickjs_wifi_wps_ap_radio.inc"
#endif

static bool wifi_radio_smartconfig_exact_locked(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    return token && token->identity && s_sc_radio &&
        token->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SMARTCONFIG &&
        token->identity == s_radio.operation.identity && token->generation == s_radio.generation &&
        token->generation == s_radio.operation.generation && token->lease_identity == s_radio.operation.lease_identity &&
        token->kind == s_radio.operation.kind && s_radio.driver_owned;
}

static esp_err_t wifi_radio_smartconfig_snapshot_locked(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_radio_status_t *status)
{
    *status = (esp32_mquickjs_wifi_smartconfig_radio_status_t){.error = ESP_ERR_INVALID_STATE,
        .stage = "smartconfig-radio-identity"};
    if (!wifi_radio_smartconfig_exact_locked(token)) return ESP_ERR_INVALID_STATE;
    status->operation = *token;
    status->home_primary = s_sc_radio->home_primary;
    status->home_secondary = s_sc_radio->home_secondary;
    status->channel_restored = s_sc_radio->channel_restored;
    taskENTER_CRITICAL(&s_radio.lock);
    status->event_fenced = s_sc_fence_seen == token->identity;
    taskEXIT_CRITICAL(&s_radio.lock);
    esp_err_t error = esp32_mquickjs_wifi_smartconfig_decoder_status(s_sc_radio->decoder, &status->decoder);
    if (error == ESP_OK && status->decoder.token.identity)
        error = esp32_mquickjs_wifi_smartconfig_events_status(&status->decoder.token, &status->events);
    status->error = error;
    status->stage = status->decoder.stage;
    return error;
}

static esp_err_t wifi_radio_smartconfig_restore_locked(void)
{
    if (s_sc_radio->channel_restored) return ESP_OK;
    esp32_mquickjs_wifi_smartconfig_decoder_status_t decoder;
    esp_err_t error = esp32_mquickjs_wifi_smartconfig_decoder_status(s_sc_radio->decoder, &decoder);
    if (error != ESP_OK) return error;
    if (decoder.handoff_unknown || !decoder.capture_stopped) return ESP_ERR_INVALID_STATE;
    /* Rejected foreign decoder preflight performed no RF mutation. In that
     * case even a seemingly helpful set_channel would affect someone else. */
    if (!decoder.mutation_attempted) { s_sc_radio->channel_restored = true; return ESP_OK; }
    if (!s_radio.started || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)
        return ESP_ERR_INVALID_STATE;
    wifi_ap_record_t ap = {0};
    error = esp_wifi_sta_get_ap_info(&ap);
    esp32_mquickjs_wireless_secure_zero(&ap, sizeof(ap));
    if (error != ESP_ERR_WIFI_NOT_CONNECT) return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    uint8_t primary;
    wifi_second_chan_t secondary;
    uint32_t generation;
    error = wifi_radio_get_channel_locked(&primary, &secondary, &generation);
    if (error != ESP_OK) return error;
    if (primary != s_sc_radio->home_primary || secondary != s_sc_radio->home_secondary) {
        error = wifi_radio_validate_regulatory_channel(s_sc_radio->home_primary);
        if (error == ESP_OK) error = esp_wifi_set_channel(s_sc_radio->home_primary, s_sc_radio->home_secondary);
        if (error == ESP_OK) error = wifi_radio_get_channel_locked(&primary, &secondary, &generation);
        if (error != ESP_OK) return error;
        if (primary != s_sc_radio->home_primary || secondary != s_sc_radio->home_secondary)
            return ESP_ERR_INVALID_RESPONSE;
    }
    s_sc_radio->channel_restored = true;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_smartconfig_begin(
    const esp32_mquickjs_wifi_radio_lease_t owners[3], bool allow_ap_channel_change,
    const esp32_mquickjs_wifi_smartconfig_decoder_options_t *options,
    esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_radio_status_t *status)
{
    if (!owners || !options || !token || !status || token->identity || token->generation || token->lease_identity || token->kind)
        return ESP_ERR_INVALID_ARG;
    *status = (esp32_mquickjs_wifi_smartconfig_radio_status_t){.stage = "smartconfig-radio-admission"};
    esp_err_t input_error = esp32_mquickjs_wifi_smartconfig_decoder_validate_options(options);
    if (input_error != ESP_OK) {
        status->error = input_error; status->stage = "smartconfig-options";
        return input_error;
    }
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (s_sc_radio || WIFI_RADIO_EAP_PENDING) goto done;
    error = wifi_radio_connection_owner_locked(&owners[0], &owners[1], &owners[2], WIFI_IF_STA, false);
    if (error != ESP_OK) goto done;
    if ((s_radio.effective_mode & WIFI_MODE_AP) && (!allow_ap_channel_change || !owners[2].acquired)) {
        error = ESP_ERR_INVALID_STATE; goto done;
    }
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity && s_radio.leases[i].fixed_channel) { error = ESP_ERR_INVALID_STATE; goto done; }
    wifi_ap_record_t ap = {0};
    error = esp_wifi_sta_get_ap_info(&ap);
    esp32_mquickjs_wireless_secure_zero(&ap, sizeof(ap));
    if (error != ESP_ERR_WIFI_NOT_CONNECT) { if (error == ESP_OK) error = ESP_ERR_INVALID_STATE; goto done; }
    uint8_t primary;
    wifi_second_chan_t secondary;
    uint32_t generation;
    error = wifi_radio_get_channel_locked(&primary, &secondary, &generation);
    if (error != ESP_OK) goto done;
    if (primary < 1 || primary > 14 || (secondary != WIFI_SECOND_CHAN_NONE &&
        secondary != WIFI_SECOND_CHAN_ABOVE && secondary != WIFI_SECOND_CHAN_BELOW)) {
        error = ESP_ERR_INVALID_STATE; goto done;
    }
    if (!s_radio.next_operation_identity) { error = ESP_ERR_NO_MEM; goto done; }
    wifi_radio_smartconfig_t *binding = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*binding), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!binding) { error = ESP_ERR_NO_MEM; goto done; }
    error = esp32_mquickjs_wifi_smartconfig_decoder_create(s_radio.generation, options, &binding->decoder);
    if (error != ESP_OK) { esp32_mquickjs_memory_payload_free(binding); goto done; }
    for (unsigned i = 0; i < 3; ++i) binding->owners[i] = owners[i].acquired ? owners[i].identity : 0;
    binding->home_primary = primary; binding->home_secondary = secondary;
    s_sc_radio = binding;
    taskENTER_CRITICAL(&s_radio.lock);
    s_sc_fence_posted = s_sc_fence_seen = 0;
    s_radio.operation = (esp32_mquickjs_wifi_radio_operation_t){.generation = s_radio.generation,
        .identity = s_radio.next_operation_identity++, .lease_identity = owners[1].identity,
        .kind = ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SMARTCONFIG};
    *token = s_radio.operation;
    taskEXIT_CRITICAL(&s_radio.lock);
    error = esp32_mquickjs_wifi_smartconfig_decoder_start(binding->decoder);
    (void)wifi_radio_smartconfig_snapshot_locked(token, status);
done:
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_smartconfig_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_smartconfig_snapshot_locked(token, status);
    wifi_radio_operation_unlock();
    return error;
}

static esp_err_t wifi_radio_smartconfig_fence_locked(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    taskENTER_CRITICAL(&s_radio.lock);
    bool ready = s_sc_fence_seen == token->identity;
    bool post = !ready && s_sc_fence_posted != token->identity;
    if (post) s_sc_fence_posted = token->identity;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (post) {
        esp_err_t error = esp_event_post(ESP32QJS_WIFI_RADIO_CONTROL_EVENT, WIFI_RADIO_SMARTCONFIG_FENCE_EVENT,
            token, sizeof(*token), 0);
        if (error != ESP_OK) {
            taskENTER_CRITICAL(&s_radio.lock);
            s_sc_fence_posted = 0;
            taskEXIT_CRITICAL(&s_radio.lock);
            return error;
        }
    }
    return ready ? ESP_OK : ESP_ERR_NOT_FINISHED;
}

esp_err_t esp32_mquickjs_wifi_radio_smartconfig_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    const char *stage = NULL;
    if (wifi_radio_smartconfig_exact_locked(token)) {
        error = esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(s_sc_radio->decoder);
        if (error == ESP_OK) { stage = "smartconfig-channel-restore"; error = wifi_radio_smartconfig_restore_locked(); }
        if (error == ESP_OK) { stage = "smartconfig-event-fence"; error = wifi_radio_smartconfig_fence_locked(token); }
    }
    (void)wifi_radio_smartconfig_snapshot_locked(token, status);
    status->error = error;
    if (error != ESP_OK && stage) status->stage = stage;
    wifi_radio_operation_unlock();
    return error != ESP_OK && stage && strcmp(stage, "smartconfig-event-fence") == 0 ? ESP_ERR_NOT_FINISHED : error;
}

esp_err_t esp32_mquickjs_wifi_radio_smartconfig_connection_begin(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_radio.lock);
    bool fenced = token && token->identity && s_sc_fence_seen == token->identity;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (wifi_radio_smartconfig_exact_locked(token) && s_sc_radio->channel_restored &&
        fenced && !s_sc_radio->connection_used && s_radio.started &&
        s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_STARTED && !s_radio.fault_stage) {
        esp32_mquickjs_wifi_smartconfig_decoder_status_t decoder;
        error = esp32_mquickjs_wifi_smartconfig_decoder_status(s_sc_radio->decoder, &decoder);
        if (error == ESP_OK && (!decoder.capture_stopped || decoder.closing || decoder.handoff_unknown))
            error = ESP_ERR_INVALID_STATE;
        if (error == ESP_OK) {
            wifi_ap_record_t ap = {0};
            error = esp_wifi_sta_get_ap_info(&ap);
            esp32_mquickjs_wireless_secure_zero(&ap, sizeof(ap));
            error = error == ESP_ERR_WIFI_NOT_CONNECT ? ESP_OK : error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
        }
        if (error == ESP_OK) s_sc_radio->connection_borrowed = s_sc_radio->connection_used = true;
    }
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_smartconfig_connection_end(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_smartconfig_exact_locked(token) && s_sc_radio->connection_borrowed) {
        s_sc_radio->connection_borrowed = false;
        error = ESP_OK;
    }
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_smartconfig_ack(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_smartconfig_exact_locked(token) && s_sc_radio->channel_restored)
        error = esp32_mquickjs_wifi_smartconfig_decoder_ack(s_sc_radio->decoder);
    (void)wifi_radio_smartconfig_snapshot_locked(token, status);
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_smartconfig_credentials(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_credentials_t *credentials, bool commit)
{
    if ((!commit && !credentials) || (commit && credentials)) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_smartconfig_decoder_status_t decoder;
    if (wifi_radio_smartconfig_exact_locked(token) && s_sc_radio->channel_restored) {
        error = esp32_mquickjs_wifi_smartconfig_decoder_status(s_sc_radio->decoder, &decoder);
        if (error == ESP_OK && (decoder.handoff_unknown || !decoder.capture_stopped || decoder.closing))
            error = ESP_ERR_INVALID_STATE;
        if (error == ESP_OK) error = esp32_mquickjs_wifi_smartconfig_decoder_credentials(
            s_sc_radio->decoder, credentials, commit);
    }
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_smartconfig_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_smartconfig_radio_status_t *status)
{
    if (!status || !token) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    const char *stage = NULL;
    if (wifi_radio_smartconfig_exact_locked(token)) {
        if (s_sc_radio->connection_borrowed) {
            (void)wifi_radio_smartconfig_snapshot_locked(token, status);
            status->error = ESP_ERR_NOT_FINISHED;
            status->stage = "smartconfig-connection-drain";
            wifi_radio_operation_unlock();
            return ESP_ERR_NOT_FINISHED;
        }
        error = esp32_mquickjs_wifi_smartconfig_decoder_close(s_sc_radio->decoder);
        if (error == ESP_OK) { stage = "smartconfig-channel-restore"; error = wifi_radio_smartconfig_restore_locked(); }
        if (error == ESP_OK) { stage = "smartconfig-event-fence"; error = wifi_radio_smartconfig_fence_locked(token); }
        (void)wifi_radio_smartconfig_snapshot_locked(token, status);
        if (error == ESP_OK) {
            stage = "smartconfig-decoder-release";
            error = esp32_mquickjs_wifi_smartconfig_decoder_release(&s_sc_radio->decoder);
            if (error == ESP_OK) {
                esp32_mquickjs_memory_payload_free(s_sc_radio); s_sc_radio = NULL;
                taskENTER_CRITICAL(&s_radio.lock);
                memset(&s_radio.operation, 0, sizeof(s_radio.operation));
                memset(token, 0, sizeof(*token));
                taskEXIT_CRITICAL(&s_radio.lock);
            }
        }
    } else (void)wifi_radio_smartconfig_snapshot_locked(token, status);
    status->error = error;
    if (error != ESP_OK && stage) status->stage = stage;
    wifi_radio_operation_unlock();
    return error;
}
#endif

#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
static bool wifi_radio_eap_exact_locked(uint64_t identity)
{
    if (!identity || identity != s_eap_radio.identity || s_eap_radio.generation != s_radio.generation)
        return false;
    const esp32_mquickjs_wifi_radio_client_t roles[3] = {ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA, ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP};
    for (unsigned i = 0; i < 3; ++i) {
        if (!s_eap_radio.owners[i]) continue;
        esp32_mquickjs_wifi_radio_lease_t lease = {.identity = s_eap_radio.owners[i],
            .generation = s_eap_radio.generation, .client = roles[i], .acquired = true};
        if (!wifi_radio_lease_valid(&lease)) return false;
    }
    return true;
}

static esp_err_t wifi_radio_eap_disconnected_locked(void)
{
    wifi_ap_record_t ap;
    esp_err_t error = esp_wifi_sta_get_ap_info(&ap);
    esp32_mquickjs_wireless_secure_zero(&ap, sizeof(ap));
    return error == ESP_ERR_WIFI_NOT_CONNECT ? ESP_OK : error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
}

esp_err_t esp32_mquickjs_wifi_radio_eap_install(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_eap_profile_t *profile,
    esp32_mquickjs_wifi_eap_install_result_t *result)
{
    if (!result || !profile) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_eap_install_result_t){.stage = "radio-admission", .error = ESP_ERR_INVALID_STATE};
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (WIFI_RADIO_EAP_PENDING) goto done;
    error = wifi_radio_connection_owner_locked(application, station, access_point, WIFI_IF_STA, false);
    if (error != ESP_OK) goto done;
    error = wifi_radio_eap_disconnected_locked();
    if (error != ESP_OK) goto done;
    error = esp32_mquickjs_wifi_eap_install(profile, result);
    if (result->retained && result->identity) {
        s_eap_radio.identity = result->identity;
        s_eap_radio.generation = s_radio.generation;
        const esp32_mquickjs_wifi_radio_lease_t *owners[3] = {application, station, access_point};
        for (unsigned i = 0; i < 3; ++i)
            s_eap_radio.owners[i] = owners[i] && owners[i]->acquired ? owners[i]->identity : 0;
        s_eap_radio.ready = error == ESP_OK && result->enabled;
    }
done:
    result->error = error;
    wifi_radio_operation_unlock();
    return error;
}

/* Stop admission is one lock interval: exact EAP pins plus the ordinary live
 * registry, wake/operation/promiscuous checks. It never clears credentials before
 * ownership has transferred to a lifecycle that survives cleanup failures. */
esp_err_t esp32_mquickjs_wifi_radio_eap_begin_stop(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (!identity || !token || token->identity || token->generation) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_eap_exact_locked(identity) ?
        wifi_radio_begin_lifecycle_with_dependents_locked(application, station, access_point, NULL, NULL, token, identity) : ESP_ERR_INVALID_STATE;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_eap_begin_restart(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_eap_profile_t *profile, bool allow_ap_restart,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode)
{
    if (!profile || !mode || !token || token->identity || token->generation) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_eap_exact_locked(identity) || !s_eap_radio.ready ||
        s_radio.generation == UINT32_MAX || s_radio.event_phase != RADIO_EVENTS_IDLE ||
        s_tx_rate_lease.identity || s_tx_rate_lease.restore_pending || s_interval.owner.identity || s_interval.restore_pending ||
        s_vendor_ie.start_owner.identity || s_config_restart.snapshot || s_policy_restart.owner.identity)
        goto done;
    error = wifi_radio_connection_owner_locked(application, station, access_point, WIFI_IF_STA, false);
    if (error != ESP_OK) goto done;
    wifi_mode_t actual;
    error = esp_wifi_get_mode(&actual);
    if (error != ESP_OK) goto done;
    if ((actual != WIFI_MODE_STA && actual != WIFI_MODE_APSTA) || actual != s_radio.effective_mode ||
        (actual == WIFI_MODE_APSTA && !allow_ap_restart)) { error = ESP_ERR_INVALID_STATE; goto done; }
    /* This source is running. START observations must match its interfaces;
     * requiring zero would only admit a stopped/inconsistent source. */
    taskENTER_CRITICAL(&s_radio.lock);
    bool live = s_radio.event_live == actual;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (!live) { error = ESP_ERR_INVALID_STATE; goto done; }
    unsigned owners = actual == WIFI_MODE_APSTA ? 3U : 2U;
    if (!s_radio.next_lease_identity || owners - 1 > UINT32_MAX - s_radio.next_lease_identity) {
        error = ESP_ERR_NO_MEM;
        goto done;
    }
    error = wifi_radio_eap_disconnected_locked();
    if (error != ESP_OK) goto done;
    esp32_mquickjs_wifi_eap_install_result_t source;
    error = esp32_mquickjs_wifi_eap_install_restart_source(identity, profile, &source);
    if (error != ESP_OK || !source.enabled || !source.retained) {
        if (error == ESP_OK) error = ESP_ERR_INVALID_STATE;
        goto done;
    }
    error = wifi_radio_begin_lifecycle_with_dependents_locked(application, station, access_point, NULL, NULL, token, identity);
    if (error == ESP_OK) *mode = actual;
done:
    wifi_radio_operation_unlock();
    return error;
}

static esp_err_t wifi_radio_eap_restart_install_locked(esp32_mquickjs_wifi_eap_profile_t *profile,
    const esp32_mquickjs_wifi_radio_lease_t owners[3])
{
    if (WIFI_RADIO_EAP_PENDING) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_eap_install_result_t result;
    esp_err_t error = wifi_radio_eap_disconnected_locked();
    if (error != ESP_OK) return error;
    error = esp32_mquickjs_wifi_eap_install(profile, &result);
    if (result.retained && result.identity) {
        s_eap_radio.identity = result.identity;
        s_eap_radio.generation = s_radio.generation;
        s_eap_radio.ready = error == ESP_OK && result.enabled;
        /* Failed installation is pinned by the still-live lifecycle, not by
         * unpublished staged leases. Clear must precede STOP/deinit on retry. */
        for (unsigned i = 0; i < 3; ++i)
            s_eap_radio.owners[i] = s_eap_radio.ready ? owners[i].identity : 0;
    }
    if (error == ESP_OK && (!result.retained || !result.identity || !result.enabled)) error = ESP_ERR_INVALID_RESPONSE;
    if (error != ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.configuration = (esp32_mquickjs_wifi_radio_config_result_t){
            .stage = "restart-enterprise-install", .error = error, .mutation_attempted = true};
        taskEXIT_CRITICAL(&s_radio.lock);
        (void)wifi_radio_record_fault("restart-enterprise-install", error);
    }
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_eap_resume_restart(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode,
    esp32_mquickjs_wifi_radio_lease_t *application,
    esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_eap_profile_t *profile)
{
    if (!profile) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!WIFI_RADIO_EAP_PENDING && wifi_radio_restart_checkpoint_matches_locked(token, mode))
        error = wifi_radio_resume_lifecycle_locked(token, mode, true, application, station, access_point, NULL, false, profile);
    wifi_radio_operation_unlock();
    return error;
}

static esp_err_t wifi_radio_eap_clear_locked(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_eap_install_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_eap_install_result_t){.stage = "radio-clear", .error = ESP_ERR_INVALID_STATE};
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (token) {
        if (!token->identity || token->identity != s_radio.lifecycle.identity ||
            token->generation != s_radio.lifecycle.generation)
            goto done;
        /* A restart retains the original lifecycle across physical driver
         * generations; the binding below must still name the current driver. */
        /* This lifecycle excludes installation of a successor binding. A prior
         * successful clear is a completed prefix, not a reason to call SDK again. */
        if (!s_eap_radio.identity) { error = ESP_OK; goto done; }
    } else if (s_radio.lifecycle.identity) goto done;
    if (!wifi_radio_eap_exact_locked(identity) || s_radio.operation.identity) goto done;
    error = wifi_radio_eap_disconnected_locked();
    if (error != ESP_OK) goto done;
    s_eap_radio.ready = false;
    error = esp32_mquickjs_wifi_eap_install_clear(identity, result);
    if (error == ESP_OK && result->entered && !result->retained && !result->identity &&
        result->sdk.entered && !result->sdk.resources) memset(&s_eap_radio, 0, sizeof(s_eap_radio));
done:
    if (identity && identity == s_eap_radio.identity) {
        result->identity = identity;
        result->retained = true;
        if (error == ESP_OK) error = ESP_ERR_INVALID_STATE;
    }
    result->error = error;
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_eap_clear(uint64_t identity,
    esp32_mquickjs_wifi_eap_install_result_t *result)
{
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_eap_clear_locked(identity, NULL, result);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_eap_clear_lifecycle(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_eap_install_result_t *result)
{
    if (!identity || !token) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_eap_clear_locked(identity, token, result);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_eap_status(uint64_t identity,
    esp32_mquickjs_wifi_eap_install_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_eap_install_result_t){.stage = "radio-status", .error = ESP_ERR_INVALID_STATE};
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_eap_exact_locked(identity)
        ? esp32_mquickjs_wifi_eap_install_status(identity, result) : ESP_ERR_INVALID_STATE;
    if (identity && identity == s_eap_radio.identity) {
        result->identity = identity;
        result->retained = true;
    }
    result->error = error;
    wifi_radio_operation_unlock();
    return error;
}

uint64_t esp32_mquickjs_wifi_radio_eap_identity(void)
{
    wifi_radio_operation_lock();
    uint64_t identity = s_eap_radio.identity;
    wifi_radio_operation_unlock();
    return identity;
}

bool esp32_mquickjs_wifi_radio_eap_ready(void)
{
    wifi_radio_operation_lock();
    bool ready = s_eap_radio.identity && s_eap_radio.ready;
    wifi_radio_operation_unlock();
    return ready;
}
#endif

#if CONFIG_ESP_WIFI_RRM_SUPPORT
static bool wifi_radio_rrm_exact_locked(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    return token != NULL && token->identity != 0U &&
        token->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_RRM &&
        token->identity == s_radio.operation.identity && token->generation == s_radio.operation.generation &&
        token->lease_identity == s_radio.operation.lease_identity && token->kind == s_radio.operation.kind &&
        token->generation == s_radio.generation && s_radio.driver_owned;
}

esp_err_t esp32_mquickjs_wifi_radio_rrm_reserve(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_radio_operation_t *token)
{
    if (token == NULL || token->identity || token->generation || token->lease_identity || token->kind)
        return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_connection_owner_locked(application, station, access_point, WIFI_IF_STA, false);
    if (err != ESP_OK) goto done;
    if (s_radio.next_operation_identity == 0U) { err = ESP_ERR_NO_MEM; goto done; }
    wifi_config_t config;
    err = esp_wifi_get_config(WIFI_IF_STA, &config);
    bool enabled = err == ESP_OK && config.sta.rm_enabled;
    esp32_mquickjs_wireless_secure_zero(&config, sizeof(config));
    if (err != ESP_OK) goto done;
    if (!enabled) { err = ESP_ERR_INVALID_STATE; goto done; }
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.operation = (esp32_mquickjs_wifi_radio_operation_t){
        .generation = s_radio.generation, .lease_identity = station->identity,
        .identity = s_radio.next_operation_identity++, .kind = ESP32_MQUICKJS_WIFI_RADIO_OPERATION_RRM,
    };
    *token = s_radio.operation;
    taskEXIT_CRITICAL(&s_radio.lock);
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_rrm_command(
    const esp32_mquickjs_wifi_radio_operation_t *token, esp32_mquickjs_wifi_rrm_command_t command,
    esp32_mquickjs_wifi_rrm_callback_t callback, esp32_mquickjs_wifi_rrm_sdk_result_t *result)
{
    if (result == NULL || callback == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_rrm_sdk_result_t){.ownership = -1};
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (wifi_radio_rrm_exact_locked(token))
        err = esp32_mquickjs_wifi_rrm_sdk_command(command, callback, (void *)(uintptr_t)token->identity, result);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_rrm_retire(
    esp32_mquickjs_wifi_radio_operation_t *token, esp32_mquickjs_wifi_rrm_callback_t callback,
    esp32_mquickjs_wifi_rrm_sdk_result_t *result)
{
    if (result == NULL || callback == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_rrm_sdk_result_t){.ownership = -1};
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_rrm_exact_locked(token)) goto done;
    err = esp32_mquickjs_wifi_rrm_sdk_command(ESP32_MQUICKJS_WIFI_RRM_QUERY,
        callback, (void *)(uintptr_t)token->identity, result);
    if (err != ESP_OK) goto done;
    if (result->ownership != 0) { err = ESP_ERR_INVALID_STATE; goto done; }
    taskENTER_CRITICAL(&s_radio.lock);
    memset(&s_radio.operation, 0, sizeof(s_radio.operation));
    memset(token, 0, sizeof(*token));
    taskEXIT_CRITICAL(&s_radio.lock);
done:
    wifi_radio_operation_unlock();
    return err;
}
#endif

#if CONFIG_ESP_WIFI_RRM_SUPPORT || CONFIG_ESP_WIFI_WNM_SUPPORT
esp_err_t esp32_mquickjs_wifi_radio_roaming(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_btm_query_t *query, esp32_mquickjs_wifi_roaming_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_roaming_result_t){.stage = "radio-admission"};
    wifi_radio_operation_lock();
    esp_err_t err = ESP_OK;
    result->radio_generation = s_radio.driver_owned ? s_radio.generation : 0;
    if (query != NULL) {
        err = wifi_radio_connection_owner_locked(application, station, access_point, WIFI_IF_STA, false);
        if (err != ESP_OK) goto done;
        if ((s_radio.effective_mode & WIFI_MODE_AP) && !query->allow_ap_channel_change) {
            result->stage = "ap-channel-permission"; err = ESP_ERR_INVALID_STATE; goto done;
        }
        taskENTER_CRITICAL(&s_radio.lock);
        bool pinned = false;
        for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
            const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
            if (owner->identity && (owner->fixed_channel || owner->channel_conflict || owner->raw_tx_identity)) pinned = true;
        }
        taskEXIT_CRITICAL(&s_radio.lock);
        if (pinned) { result->stage = "fixed-channel-owner"; err = ESP_ERR_INVALID_STATE; goto done; }
        /* A query may lead to later autonomous channel movement. Invalidate any
         * saved STOP observation before dispatch, including failed attempts. */
        wifi_radio_invalidate_stop_snapshot_locked();
    } else {
        if (s_radio.lifecycle.identity || s_radio.operation.identity || s_radio.fault_stage ||
            s_radio.cleanup_stage || s_radio.restart_required) { err = ESP_ERR_INVALID_STATE; goto done; }
        if (!s_radio.driver_owned || !s_radio.started || !(s_radio.effective_mode & WIFI_MODE_STA)) goto done;
        if (!s_radio.storage_configured || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED) {
            err = ESP_ERR_INVALID_STATE; goto done;
        }
    }
    err = esp32_mquickjs_wifi_roaming_sdk(query, result);
done:
    result->error = err;
    wifi_radio_operation_unlock();
    return err;
}
#endif

esp_err_t esp32_mquickjs_wifi_radio_write_interval(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point, uint16_t milliseconds,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "interval-admission", .error = ESP_ERR_INVALID_STATE};
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        s_interval.owner.identity != 0U) goto done;
    if (s_radio.started) {
        wifi_interface_t interface = (s_radio.effective_mode & WIFI_MODE_STA) ? WIFI_IF_STA : WIFI_IF_AP;
        err = wifi_radio_connection_owner_locked(application, station, access_point, interface, false);
        if (err != ESP_OK) goto done;
    } else {
        if (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED || s_radio.stop_required ||
            s_radio.wake_locks != 0U || s_radio.promiscuous_claimed || s_tx_rate_lease.identity != 0U) goto done;
        for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
            if (s_radio.leases[i].identity != 0U) goto done;
    }
    result->stage = "interval-write";
    esp32_mquickjs_wifi_interval_result_t write;
    err = esp32_mquickjs_wifi_interval_write(&s_interval, s_radio.generation,
        milliseconds, wifi_radio_interval_writer, NULL, &write);
    result->mutation_attempted = write.attempted;
    if (err == ESP_OK) result->stage = "complete";
    /* A failed write quarantines interval knowledge. Explicit replacement or
     * physical deinit can clear it; never invent readback or replay zero. */
done:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = *result;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
static esp_err_t wifi_radio_ftm_offset_writer(void *opaque, int16_t centimeters)
{
    (void)opaque;
    return esp_wifi_ftm_resp_set_offset(centimeters);
}

void esp32_mquickjs_wifi_radio_ftm_offset_status(esp32_mquickjs_wifi_ftm_offset_state_t *record)
{
    if (record == NULL) return;
    wifi_radio_operation_lock();
    *record = s_ftm_offset;
    wifi_radio_operation_unlock();
}

esp_err_t esp32_mquickjs_wifi_radio_write_ftm_offset(int32_t centimeters,
    esp32_mquickjs_wifi_ftm_offset_state_t *record, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (record != NULL) *record = (esp32_mquickjs_wifi_ftm_offset_state_t){0};
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "ftm-offset-admission", .error = ESP_ERR_INVALID_ARG};
    if (record == NULL || centimeters < INT16_MIN || centimeters > INT16_MAX) return result->error;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
        s_radio.started || s_radio.stop_required || s_radio.lifecycle.identity != 0U ||
        s_radio.operation.identity != 0U || s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL ||
        s_radio.restart_required || s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_tx_rate_lease.identity != 0U || !(s_radio.effective_mode & WIFI_MODE_AP)) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    wifi_mode_t mode;
    result->stage = "ftm-offset-mode";
    err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) goto done;
    if (mode != s_radio.effective_mode) { err = ESP_ERR_INVALID_STATE; goto done; }
    result->stage = "ftm-offset-write";
    err = esp32_mquickjs_wifi_ftm_offset_apply(&s_ftm_offset, s_radio.generation,
        (int16_t)centimeters, wifi_radio_ftm_offset_writer, NULL, &result->mutation_attempted);
    if (err == ESP_OK) result->stage = "complete";
    else if (!result->mutation_attempted) result->stage = "ftm-offset-revision";
done:
    *record = s_ftm_offset;
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = *result;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}
#endif

static esp_err_t wifi_radio_policy_writer(void *opaque,
    esp32_mquickjs_wifi_policy_slot_t slot, bool requested)
{
    (void)opaque;
    switch (slot) {
    case ESP32_MQUICKJS_WIFI_POLICY_SLOT_DYNAMIC_CS:
        return esp_wifi_set_dynamic_cs(requested);
    case ESP32_MQUICKJS_WIFI_POLICY_SLOT_STA_11B:
        return esp_wifi_config_11b_rate(WIFI_IF_STA, requested);
    case ESP32_MQUICKJS_WIFI_POLICY_SLOT_AP_11B:
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        return esp_wifi_config_11b_rate(WIFI_IF_AP, requested);
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    case ESP32_MQUICKJS_WIFI_POLICY_SLOT_COEX_POWER:
#if CONFIG_ESP_COEX_POWER_MANAGEMENT
        return esp_wifi_coex_pwr_configure(requested);
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT
    case ESP32_MQUICKJS_WIFI_POLICY_SLOT_BSS_COLOR:
        return esp_wifi_enable_bsscolor_collision_detection(WIFI_IF_STA, requested);
#endif
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t wifi_radio_policy_restart_prepare_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    if (token == NULL || token->identity == 0U || token->identity != s_radio.lifecycle.identity ||
        token->generation != s_radio.lifecycle.generation) return ESP_ERR_INVALID_STATE;
    if (s_policy_restart.owner.identity != 0U)
        return s_policy_restart.owner.identity == token->identity &&
            s_policy_restart.owner.generation == token->generation && s_policy_restart.mode == mode
            ? ESP_OK : ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_policy_snapshot_t snapshot;
    esp_err_t err = esp32_mquickjs_wifi_policy_capture(&s_policies, s_radio.generation, &snapshot);
    if (err != ESP_OK) return err;
    if (((snapshot.mask & (1U << ESP32_MQUICKJS_WIFI_POLICY_SLOT_STA_11B)) && !(mode & WIFI_MODE_STA)) ||
        ((snapshot.mask & (1U << ESP32_MQUICKJS_WIFI_POLICY_SLOT_AP_11B)) && !(mode & WIFI_MODE_AP)))
        return ESP_ERR_INVALID_STATE;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    if ((snapshot.mask & (1U << ESP32_MQUICKJS_WIFI_POLICY_SLOT_BSS_COLOR)) && !(mode & WIFI_MODE_STA))
        return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    esp32_mquickjs_wifi_ftm_offset_snapshot_t offset;
    err = esp32_mquickjs_wifi_ftm_offset_capture(&s_ftm_offset, s_radio.generation, &offset);
    if (err != ESP_OK) return err;
    if (offset.configured && !(mode & WIFI_MODE_AP)) return ESP_ERR_INVALID_STATE;
    s_policy_restart.ftm_offset = offset;
#endif
    s_policy_restart.owner = *token;
    s_policy_restart.snapshot = snapshot;
    s_policy_restart.mode = mode;
    return ESP_OK;
}

static esp_err_t wifi_radio_policy_restart_replay_locked(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, bool after_start)
{
    if (token == NULL || token->identity == 0U || token->identity != s_radio.lifecycle.identity ||
        token->generation != s_radio.lifecycle.generation || s_policy_restart.owner.identity != token->identity ||
        s_policy_restart.owner.generation != token->generation || !s_radio.driver_owned || !s_radio.storage_configured ||
        s_radio.effective_mode != s_policy_restart.mode || s_radio.started != after_start ||
        s_radio.driver_state != (after_start ? ESP32_MQUICKJS_WIFI_RADIO_STARTED : ESP32_MQUICKJS_WIFI_RADIO_STOPPED))
        return ESP_ERR_INVALID_STATE;
    if (s_policy_restart.replay_generation != s_radio.generation) {
        /* A fresh physical generation must replay every setting. Never skip a
         * prior generation's writes after cleanup/retry rebuilt the driver. */
        s_policy_restart.replay_generation = s_radio.generation;
        s_policy_restart.completed = 0;
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        s_policy_restart.ftm_offset_completed = false;
#endif
    }
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    /* Never defer an offset write until an AP can already accept FTM requests. */
    if (after_start && !s_policy_restart.ftm_offset_completed) return ESP_ERR_INVALID_STATE;
    if (!after_start) {
        esp_err_t offset_error = esp32_mquickjs_wifi_ftm_offset_replay(&s_ftm_offset,
            s_radio.generation, &s_policy_restart.ftm_offset, &s_policy_restart.ftm_offset_completed,
            wifi_radio_ftm_offset_writer, NULL);
        if (offset_error != ESP_OK) return wifi_radio_record_fault("restart-ftm-offset", offset_error);
    }
#endif
    esp32_mquickjs_wifi_policy_write_t write;
    esp_err_t err = esp32_mquickjs_wifi_policy_replay(&s_policies, s_radio.generation,
        &s_policy_restart.snapshot, after_start, &s_policy_restart.completed,
        wifi_radio_policy_writer, NULL, &write);
    if (err != ESP_OK) (void)wifi_radio_record_fault(
        after_start ? "restart-policy-post-start" : "restart-policy-pre-start", err);
    return err;
}

void esp32_mquickjs_wifi_radio_policy_status(esp32_mquickjs_wifi_policy_state_t *status)
{
    if (status == NULL) return;
    wifi_radio_operation_lock();
    *status = s_policies;
    wifi_radio_operation_unlock();
}

esp_err_t esp32_mquickjs_wifi_radio_write_policy(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_policy_control_t control, wifi_interface_t interface,
    bool requested, bool *accepted, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (accepted != NULL) *accepted = false;
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "policy-admission", .error = ESP_ERR_INVALID_ARG};
    bool by_interface = control == ESP32_MQUICKJS_WIFI_POLICY_11B_RATE;
    if (accepted == NULL || (unsigned)control > ESP32_MQUICKJS_WIFI_POLICY_BSS_COLOR ||
        (by_interface ? interface != WIFI_IF_STA && interface != WIFI_IF_AP : interface != WIFI_IF_MAX))
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (by_interface && interface == WIFI_IF_AP) { result->error = ESP_ERR_NOT_SUPPORTED; return result->error; }
#endif
#if !CONFIG_ESP_COEX_POWER_MANAGEMENT
    if (control == ESP32_MQUICKJS_WIFI_POLICY_COEX_POWER) { result->error = ESP_ERR_NOT_SUPPORTED; return result->error; }
#endif
#if !CONFIG_SOC_WIFI_HE_SUPPORT
    if (control == ESP32_MQUICKJS_WIFI_POLICY_BSS_COLOR) { result->error = ESP_ERR_NOT_SUPPORTED; return result->error; }
#endif
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)) goto done;
    if (by_interface && !(s_radio.effective_mode & (interface == WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP))) goto done;
    if (control == ESP32_MQUICKJS_WIFI_POLICY_BSS_COLOR && !(s_radio.effective_mode & WIFI_MODE_STA)) goto done;
    if (s_radio.started) {
        if (by_interface) goto done; /* SDK 11b rate is pre-start only. */
        wifi_interface_t owner_interface = (s_radio.effective_mode & WIFI_MODE_STA) ? WIFI_IF_STA : WIFI_IF_AP;
        err = wifi_radio_connection_owner_locked(application, station, access_point, owner_interface, false);
        if (err != ESP_OK) goto done;
    } else {
        if (control == ESP32_MQUICKJS_WIFI_POLICY_DYNAMIC_CS || control == ESP32_MQUICKJS_WIFI_POLICY_BSS_COLOR) { err = ESP_ERR_WIFI_NOT_STARTED; goto done; }
        if (s_radio.stop_required || s_radio.wake_locks != 0U || s_radio.promiscuous_claimed || s_tx_rate_lease.identity != 0U) goto done;
        for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
            if (s_radio.leases[i].identity != 0U) goto done;
    }
    esp32_mquickjs_wifi_policy_slot_t slot;
    switch (control) {
    case ESP32_MQUICKJS_WIFI_POLICY_DYNAMIC_CS:
        slot = ESP32_MQUICKJS_WIFI_POLICY_SLOT_DYNAMIC_CS;
        result->stage = "dynamic-cs-write";
        break;
    case ESP32_MQUICKJS_WIFI_POLICY_11B_RATE:
        slot = interface == WIFI_IF_STA ? ESP32_MQUICKJS_WIFI_POLICY_SLOT_STA_11B : ESP32_MQUICKJS_WIFI_POLICY_SLOT_AP_11B;
        result->stage = "11b-rate-write";
        break;
    case ESP32_MQUICKJS_WIFI_POLICY_COEX_POWER:
        slot = ESP32_MQUICKJS_WIFI_POLICY_SLOT_COEX_POWER;
        result->stage = "coexistence-power-write";
        break;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    case ESP32_MQUICKJS_WIFI_POLICY_BSS_COLOR:
        slot = ESP32_MQUICKJS_WIFI_POLICY_SLOT_BSS_COLOR;
        result->stage = "bss-color-reporting-write";
        break;
#endif
    default:
        err = ESP_ERR_INVALID_ARG;
        goto done;
    }
    esp32_mquickjs_wifi_policy_write_t write;
    err = esp32_mquickjs_wifi_policy_apply(&s_policies, s_radio.generation, slot,
        requested, wifi_radio_policy_writer, NULL, &write);
    result->mutation_attempted = write.attempted;
    if (err == ESP_OK) { *accepted = requested; result->stage = "complete"; }
    else if (write.attempted) {
        /* The retained accepted value is history after a failed SDK call, not
         * proof of the driver's current policy. No guessed rollback or replay. */
        (void)wifi_radio_record_fault(result->stage, err);
    } else {
        result->stage = "policy-record-admission";
    }

done:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = *result;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_band_snapshot(wifi_band_mode_t *mode, wifi_band_t *band)
{
    esp_err_t err = esp_wifi_get_band_mode(mode);
    if (err != ESP_OK) return err;
    if (*mode != WIFI_BAND_MODE_2G_ONLY && *mode != WIFI_BAND_MODE_5G_ONLY && *mode != WIFI_BAND_MODE_AUTO)
        return ESP_ERR_INVALID_RESPONSE;
    err = esp_wifi_get_band(band);
    if (err != ESP_OK) return err;
    if ((*band != WIFI_BAND_2G && *band != WIFI_BAND_5G) ||
        (*mode == WIFI_BAND_MODE_2G_ONLY && *band != WIFI_BAND_2G) ||
        (*mode == WIFI_BAND_MODE_5G_ONLY && *band != WIFI_BAND_5G)) return ESP_ERR_INVALID_RESPONSE;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
    if (*mode != WIFI_BAND_MODE_2G_ONLY || *band != WIFI_BAND_2G) return ESP_ERR_INVALID_RESPONSE;
#endif
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_change_band(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    bool band_mode, int32_t requested, int32_t *actual,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (actual != NULL) *actual = 0;
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "band-admission", .error = ESP_ERR_INVALID_ARG};
    if (actual == NULL || (band_mode ? (requested != WIFI_BAND_MODE_2G_ONLY && requested != WIFI_BAND_MODE_5G_ONLY && requested != WIFI_BAND_MODE_AUTO)
        : (requested != WIFI_BAND_2G && requested != WIFI_BAND_5G))) return ESP_ERR_INVALID_ARG;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
    if (band_mode ? requested != WIFI_BAND_MODE_2G_ONLY : requested != WIFI_BAND_2G) {
        result->error = ESP_ERR_NOT_SUPPORTED;
        return result->error;
    }
#endif
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_connection_owner_locked(application, station, access_point, WIFI_IF_STA, false);
    if (err != ESP_OK) goto done;
    /* Checking an AP client list cannot prevent a new association between the
     * check and the SDK mutation. Caller must explicitly close AP first. */
    if (s_radio.effective_mode != WIFI_MODE_STA) { err = ESP_ERR_INVALID_STATE; goto done; }
    bool pinned = false;
    taskENTER_CRITICAL(&s_radio.lock);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (owner->identity != 0U && (owner->fixed_channel || owner->channel_conflict || owner->raw_tx_identity != 0U))
            pinned = true;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (pinned) { err = ESP_ERR_INVALID_STATE; goto done; }
    wifi_band_mode_t before_mode;
    wifi_band_t before_band;
    result->stage = "band-snapshot";
    err = wifi_radio_band_snapshot(&before_mode, &before_band);
    if (err != ESP_OK) goto done;
    if (!band_mode && before_mode != WIFI_BAND_MODE_AUTO) { err = ESP_ERR_NOT_SUPPORTED; goto done; }
    /* No mutation is needed, even on an associated Station. */
    if ((band_mode ? (int32_t)before_mode : (int32_t)before_band) == requested) {
        *actual = requested;
        result->stage = "complete";
        goto done;
    }
    result->stage = "band-station-link";
    wifi_ap_record_t ap;
    err = esp_wifi_sta_get_ap_info(&ap);
    if (err == ESP_OK) { err = ESP_ERR_INVALID_STATE; goto done; }
    if (err != ESP_ERR_WIFI_NOT_CONNECT) goto done;
    result->stage = band_mode ? "band-mode-write" : "band-write";
    result->mutation_attempted = true;
    result->persistent_mutation_possible = s_radio.storage == WIFI_STORAGE_FLASH;
    err = band_mode ? esp_wifi_set_band_mode((wifi_band_mode_t)requested) : esp_wifi_set_band((wifi_band_t)requested);
    if (err != ESP_OK) goto uncertain;
    result->stage = "band-readback";
    wifi_band_mode_t mode;
    wifi_band_t band;
    err = wifi_radio_band_snapshot(&mode, &band);
    if (err == ESP_OK && (band_mode ? (int32_t)mode != requested : ((int32_t)band != requested || mode != before_mode)))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) goto uncertain;
    result->stage = "band-channel-readback";
    uint8_t primary;
    wifi_second_chan_t secondary;
    uint32_t generation;
    err = wifi_radio_get_channel_locked(&primary, &secondary, &generation);
    if (err != ESP_OK) goto uncertain;
    if ((secondary != WIFI_SECOND_CHAN_NONE && secondary != WIFI_SECOND_CHAN_ABOVE && secondary != WIFI_SECOND_CHAN_BELOW) ||
        (band == WIFI_BAND_2G ? primary < 1 || primary > 14 : esp32_mquickjs_wifi_radio_5ghz_channel_bit(primary) == 0U)) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto uncertain;
    }
    result->stage = "band-channel-regulatory";
    err = wifi_radio_validate_regulatory_channel(primary);
    if (err != ESP_OK) goto uncertain;
    *actual = band_mode ? (int32_t)mode : (int32_t)band;
    result->stage = "complete";
    goto done;
uncertain:
    /* Public getters cannot snapshot all hidden inactive-band state. A failed
     * call may have changed RF/home channel already; do not replay or invent a
     * rollback. Retain the original error and explicit physical-recovery need. */
    (void)wifi_radio_record_fault(result->stage, err);
done:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = *result;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

bool esp32_mquickjs_wifi_radio_rssi_request_status(esp32_mquickjs_wifi_rssi_request_t *status)
{
    if (status == NULL) return false;
    wifi_radio_operation_lock();
    *status = s_rssi_request;
    bool active = status->revision != 0U && s_radio.driver_owned && status->generation == s_radio.generation;
    wifi_radio_operation_unlock();
    return active;
}

#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
static esp_err_t wifi_radio_he_statistics_admission_locked(void)
{
    if (!s_radio.driver_owned) return ESP_ERR_WIFI_NOT_INIT;
    if (!s_radio.started) return ESP_ERR_WIFI_NOT_STARTED;
    if (!s_radio.storage_configured || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED ||
        s_radio.lifecycle.identity || s_radio.operation.identity || s_radio.fault_stage || s_radio.cleanup_stage ||
        s_radio.restart_required || s_he_statistics.pending ||
        (s_radio.effective_mode != WIFI_MODE_STA && s_radio.effective_mode != WIFI_MODE_AP &&
         s_radio.effective_mode != WIFI_MODE_APSTA)) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
#endif

esp_err_t esp32_mquickjs_wifi_radio_read_he_statistics(esp32_mquickjs_wifi_he_statistics_t *actual,
    const char **stage)
{
    if (!actual || !stage) return ESP_ERR_INVALID_ARG;
    *actual = (esp32_mquickjs_wifi_he_statistics_t){0};
    *stage = "he-statistics-admission";
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_he_statistics_admission_locked();
    if (error == ESP_OK) {
        *stage = "he-statistics-read";
        error = esp32_mquickjs_wifi_he_statistics_snapshot(actual, false);
        if (error == ESP_OK) { s_he_statistics.managed = true; *stage = NULL; }
    }
    wifi_radio_operation_unlock();
    return error;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t esp32_mquickjs_wifi_radio_write_he_statistics(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    bool rx, uint8_t selection, bool enabled, esp32_mquickjs_wifi_he_statistics_t *actual,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "he-statistics-admission", .error = ESP_ERR_INVALID_ARG};
    if (!actual || selection > 3 || (rx && enabled)) return ESP_ERR_INVALID_ARG;
    *actual = (esp32_mquickjs_wifi_he_statistics_t){0};
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_he_statistics_admission_locked();
    if (error != ESP_OK) goto done;
    wifi_interface_t interface = (s_radio.effective_mode & WIFI_MODE_STA) ? WIFI_IF_STA : WIFI_IF_AP;
    error = wifi_radio_connection_owner_locked(application, station, access_point, interface, false);
    if (error != ESP_OK) goto done;
    result->stage = "he-statistics-snapshot";
    esp32_mquickjs_wifi_he_statistics_t wanted;
    error = esp32_mquickjs_wifi_he_statistics_snapshot(&wanted, false);
    if (error != ESP_OK) goto done;
    if (rx) { wanted.ordinary = (selection & 1U) != 0; wanted.multi_user = (selection & 2U) != 0; }
    else if (enabled) wanted.tx_mask |= 1U << selection;
    else wanted.tx_mask &= ~(1U << selection);
    result->stage = rx ? "rx-statistics-write" : "tx-statistics-write";
    result->mutation_attempted = true;
    s_he_statistics.managed = true;
    error = rx ? esp_wifi_enable_rx_statistics(wanted.ordinary, wanted.multi_user) :
        esp_wifi_enable_tx_statistics((esp_wifi_aci_t)selection, enabled);
    esp_err_t observed = esp32_mquickjs_wifi_he_statistics_snapshot(actual, false);
    if (error == ESP_OK && observed == ESP_OK && !wifi_he_statistics_equal(actual, &wanted))
        observed = ESP_ERR_INVALID_RESPONSE;
    if (observed != ESP_OK) {
        /* Preserve a native submission error even if observation also fails.
         * A valid failed-write observation needs no speculative counter rollback. */
        if (error == ESP_OK) error = observed;
        result->stage = "he-statistics-readback";
        (void)wifi_radio_record_fault(result->stage, error);
    } else if (error == ESP_OK) result->stage = "complete";
done:
    result->error = error;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = *result;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return error;
#else
    (void)application; (void)station; (void)access_point;
    result->error = ESP_ERR_NOT_SUPPORTED;
    return result->error;
#endif
}

esp_err_t esp32_mquickjs_wifi_radio_read_scan_parameters(wifi_scan_default_params_t *actual,
    const char **stage)
{
    if (actual == NULL || stage == NULL) return ESP_ERR_INVALID_ARG;
    memset(actual, 0, sizeof(*actual));
    *stage = "scan-parameters-admission";
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED ||
        !(s_radio.effective_mode & WIFI_MODE_STA) || s_radio.lifecycle.identity || s_radio.operation.identity ||
        s_radio.fault_stage || s_radio.cleanup_stage || s_radio.restart_required) goto done;
    *stage = "scan-parameters-read";
    error = wifi_radio_scan_parameters_observe_locked(actual);
    if (error == ESP_OK) *stage = NULL;
done:
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_write_scan_parameters(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const wifi_scan_default_params_t *requested, wifi_scan_default_params_t *actual,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (actual != NULL) memset(actual, 0, sizeof(*actual));
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "scan-parameters-admission", .error = ESP_ERR_INVALID_ARG};
    if (actual == NULL || !wifi_scan_parameters_valid(requested)) return ESP_ERR_INVALID_ARG;
    wifi_scan_default_params_t wanted = wifi_scan_parameters_effective(requested), previous, observed;
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_connection_owner_locked(application, station, access_point, WIFI_IF_STA, false);
    if (error != ESP_OK) goto done;
    result->stage = "scan-parameters-snapshot";
    error = wifi_radio_scan_parameters_observe_locked(&previous);
    if (error != ESP_OK) goto done;
    result->stage = "scan-parameters-write";
    result->mutation_attempted = true;
    wifi_radio_invalidate_stop_snapshot_locked();
    error = wifi_scan_parameters_set_native(requested);
    if (error == ESP_OK) {
        result->stage = "scan-parameters-readback";
        error = wifi_radio_scan_parameters_observe_locked(&observed);
        if (error == ESP_OK && !wifi_scan_parameters_equal(&observed, &wanted)) error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) {
        s_scan_parameters.attempted = false;
        *actual = observed;
        result->stage = "complete";
        goto done;
    }
    /* OOM/busy/precondition rejection may leave the original value intact.
     * Read it first, so a second rejected setter cannot manufacture a fault.
     * Preserve the first operation's error even if cleanup succeeds. */
    result->rollback_stage = "scan-parameters-rollback-observation";
    result->rollback_error = wifi_radio_scan_parameters_observe_locked(&observed);
    if (result->rollback_error != ESP_OK || !wifi_scan_parameters_equal(&observed, &previous)) {
        result->rollback_attempted = true;
        result->rollback_stage = "scan-parameters-rollback-write";
        result->rollback_error = wifi_scan_parameters_set_native(&previous);
        if (result->rollback_error == ESP_OK) {
            result->rollback_stage = "scan-parameters-rollback-readback";
            result->rollback_error = wifi_radio_scan_parameters_observe_locked(&observed);
            if (result->rollback_error == ESP_OK && !wifi_scan_parameters_equal(&observed, &previous))
                result->rollback_error = ESP_ERR_INVALID_RESPONSE;
        }
    }
    result->rollback_complete = result->rollback_error == ESP_OK;
    if (result->rollback_complete) {
        s_scan_parameters.attempted = false;
        result->rollback_stage = NULL;
    } else {
        (void)wifi_radio_record_fault(result->stage, error);
        (void)wifi_radio_cleanup_fault("scan-parameters-rollback", result->rollback_error);
    }
done:
    result->error = error;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.configuration = *result; taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_connection_control(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_connection_control_t control, wifi_interface_t interface,
    int32_t requested, int32_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (actual != NULL) *actual = 0;
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "control-admission", .error = ESP_ERR_INVALID_ARG};
    bool observer = control == ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD;
    if (actual == NULL || (control != ESP32_MQUICKJS_WIFI_CONNECTION_INACTIVE_TIME && !observer) ||
        (interface != WIFI_IF_STA && interface != WIFI_IF_AP) ||
        (observer && (interface != WIFI_IF_STA || requested < -100 || requested > 10)) ||
        (!observer && (requested < (interface == WIFI_IF_STA ? 3 : 10) || requested > UINT16_MAX)))
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) { result->error = ESP_ERR_NOT_SUPPORTED; return result->error; }
#endif
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_connection_owner_locked(application, station, access_point, interface, observer);
    if (err != ESP_OK) goto done;
    if (observer) {
        if (s_rssi_request.revision == UINT32_MAX) {
            result->stage = "rssi-threshold-identity";
            err = ESP_ERR_NO_MEM;
            goto done;
        }
        result->stage = "rssi-threshold-write";
        result->mutation_attempted = true;
        s_rssi_request.generation = s_radio.generation;
        ++s_rssi_request.revision;
        s_rssi_request.requested_dbm = requested;
        err = esp_wifi_set_rssi_threshold(requested);
        s_rssi_request.error = err;
        /* No public getter or callback cookie. Do not replay, invent a rollback,
         * or equate write acceptance with a currently armed event subscription.
         * Keep this historical attempt across deinit; restart never replays it.
         * Only a new explicit setter requests another notification. */
        if (err == ESP_OK) { *actual = requested; result->stage = "complete"; }
        goto done;
    }
    uint16_t previous, observed;
    result->stage = "inactive-time-snapshot";
    err = esp_wifi_get_inactive_time(interface, &previous);
    if (err == ESP_OK && previous < (interface == WIFI_IF_STA ? 3 : 10)) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) { wifi_radio_inactive_history_record(interface == WIFI_IF_STA ? 0 : 1, 0, err); goto done; }
    result->mutation_attempted = true;
    result->stage = "inactive-time-write";
    /* The pinned SDK routes this write through wifi_nvs_set when storage is
     * FLASH, despite esp_wifi.h describing the setting as non-persistent. */
    result->persistent_mutation_possible = s_radio.storage == WIFI_STORAGE_FLASH;
    err = esp_wifi_set_inactive_time(interface, (uint16_t)requested);
    if (err == ESP_OK) {
        result->stage = "inactive-time-readback";
        err = esp_wifi_get_inactive_time(interface, &observed);
        if (err == ESP_OK && observed != requested) err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        wifi_radio_inactive_history_record(interface == WIFI_IF_STA ? 0 : 1, observed, ESP_OK);
        uint8_t bit = interface == WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP;
        s_inactive_history.pending &= (uint8_t)~bit;
        s_inactive_history.attempted &= (uint8_t)~bit;
        s_inactive_history.persistent &= (uint8_t)~bit;
        s_inactive_history.pending_values[interface == WIFI_IF_STA ? 0 : 1] = 0;
        *actual = observed; result->stage = "complete"; goto done;
    }
    result->rollback_attempted = true;
    result->rollback_stage = "rollback-inactive-time";
    result->rollback_error = esp_wifi_set_inactive_time(interface, previous);
    if (result->rollback_error == ESP_OK) {
        result->rollback_stage = "rollback-inactive-time-readback";
        result->rollback_error = esp_wifi_get_inactive_time(interface, &observed);
        if (result->rollback_error == ESP_OK && observed != previous) result->rollback_error = ESP_ERR_INVALID_RESPONSE;
    }
    result->rollback_complete = result->rollback_error == ESP_OK;
    wifi_radio_inactive_history_record(interface == WIFI_IF_STA ? 0 : 1, previous, result->rollback_error);
    if (result->rollback_complete) result->rollback_stage = NULL;
    else {
        (void)wifi_radio_record_fault(result->stage, err);
        (void)wifi_radio_cleanup_fault("inactive-time-rollback", result->rollback_error);
    }
    /* Readback proves the running threshold only, not NVS restoration or the
     * reversal of a disconnect/deauth while a shorter threshold was active. */
done:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = *result;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_write_antenna(bool gpio,
    const esp32_mquickjs_wifi_antenna_snapshot_t *requested,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "antenna-admission", .error = ESP_ERR_INVALID_ARG};
    if (requested == NULL) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.started || s_radio.stop_required || s_radio.restart_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED || s_radio.fault_stage != NULL ||
        s_radio.cleanup_stage != NULL || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    err = esp32_mquickjs_wifi_antenna_write(gpio, requested, result);
    if (esp32_mquickjs_wifi_antenna_fault() != ESP_OK) {
        (void)wifi_radio_record_fault("antenna-device-restart-required", err);
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.restart_required = true;
        taskEXIT_CRITICAL(&s_radio.lock);
    }
done:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.configuration = *result;
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_read_antenna(bool gpio,
    esp32_mquickjs_wifi_antenna_snapshot_t *output, const char **stage)
{
    if (output != NULL) memset(output, 0, sizeof(*output));
    if (stage != NULL) *stage = "admission";
    if (output == NULL || stage == NULL) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_antenna_snapshot_t snapshot = {0};
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)) goto done;
    *stage = gpio ? "antenna-gpio" : "antenna";
    err = gpio ? esp_phy_get_ant_gpio(&snapshot.gpio) : esp_phy_get_ant(&snapshot.config);
    if (err != ESP_OK) goto done;
    if (!gpio) {
        const esp_phy_ant_config_t *config = &snapshot.config;
        if ((config->rx_ant_mode != ESP_PHY_ANT_MODE_ANT0 && config->rx_ant_mode != ESP_PHY_ANT_MODE_ANT1 &&
             config->rx_ant_mode != ESP_PHY_ANT_MODE_AUTO) ||
            (config->tx_ant_mode != ESP_PHY_ANT_MODE_ANT0 && config->tx_ant_mode != ESP_PHY_ANT_MODE_ANT1 &&
             config->tx_ant_mode != ESP_PHY_ANT_MODE_AUTO) ||
            (config->rx_ant_default != ESP_PHY_ANT_ANT0 && config->rx_ant_default != ESP_PHY_ANT_ANT1) ||
            (config->tx_ant_mode == ESP_PHY_ANT_MODE_AUTO && config->rx_ant_mode != ESP_PHY_ANT_MODE_AUTO)) {
            *stage = "decode";
            err = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
    }
    /* GPIO fields and enabled_ant selectors retain their full SDK bit widths.
     * Stored GPIO numbers are not proof of available pins or physical wiring. */
    *output = snapshot;
done:
    wifi_radio_operation_unlock();
    return err;
}

typedef enum {
    WIFI_RADIO_RESTORE_DEFAULTS,
    WIFI_RADIO_RESTORE_MODE,
    WIFI_RADIO_RESTORE_STORAGE,
} wifi_radio_restore_phase_t;

static bool wifi_radio_restore_phase_locked(wifi_radio_restore_phase_t *phase)
{
    *phase = WIFI_RADIO_RESTORE_DEFAULTS;
    const char *fault = s_radio.fault_stage;
    const char *cleanup = s_radio.cleanup_stage;
    if (fault == NULL && cleanup == NULL)
        return s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    if (fault == NULL || (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_FAULTED &&
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING)) return false;
    /* The SDK defaults contract covers mode/protocol/bandwidth. It does not
     * establish retirement of unrelated native owners or reset every policy.
     * Composite transactions retain their lifecycle/cleanup reservation. */
    bool mode = strcmp(fault, "mode-snapshot") == 0 || strcmp(fault, "mode-write") == 0 ||
        strcmp(fault, "mode-readback") == 0;
    bool phy = strcmp(fault, "station-phy-config") == 0 || strcmp(fault, "ap-phy-config") == 0 ||
        strcmp(fault, "station-phy-readback") == 0 || strcmp(fault, "ap-phy-readback") == 0;
    bool restore = strcmp(fault, "restore-defaults") == 0 || strcmp(fault, "restore-mode-readback") == 0 ||
        strcmp(fault, "restore-storage") == 0;
    if (!mode && !phy && !restore) return false;
    if (cleanup == NULL) return !restore;
    if (strcmp(cleanup, "restore-defaults") == 0) return true;
    if (strcmp(cleanup, "restore-mode-readback") == 0) {
        *phase = WIFI_RADIO_RESTORE_MODE;
        return true;
    }
    if (strcmp(cleanup, "restore-storage") == 0) {
        *phase = WIFI_RADIO_RESTORE_STORAGE;
        return true;
    }
    return (mode && strcmp(cleanup, "mode-rollback") == 0) ||
        (phy && strcmp(cleanup, "phy-rollback") == 0);
}

esp_err_t esp32_mquickjs_wifi_radio_restore(esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "restore-admission", .error = ESP_ERR_INVALID_STATE};
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    wifi_radio_restore_phase_t phase;
    wifi_mode_t observed = WIFI_MODE_NULL;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    /* esp_wifi_restore contains an implicit STOP. Require completed STOP and
     * event retirement before calling it, including owners that live outside
     * the lease registry. No child Session is closed on the caller's behalf. */
    if (!wifi_radio_restore_phase_locked(&phase) ||
        (!s_radio.storage_configured && phase != WIFI_RADIO_RESTORE_STORAGE) ||
        (s_radio.storage != WIFI_STORAGE_RAM && s_radio.storage != WIFI_STORAGE_FLASH) ||
        s_radio.started || s_radio.stop_required || s_radio.stop_submitted ||
        s_radio.restart_required || s_radio.lifecycle.identity != 0U ||
        s_radio.operation.identity != 0U || s_radio.event_phase != RADIO_EVENTS_IDLE || s_radio.event_live != 0U ||
        s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending ||
        s_interval.owner.identity != 0U || s_interval.restore_pending ||
        s_vendor_ie.start_owner.identity != 0U || s_config_restart.snapshot != NULL || s_policy_restart.owner.identity != 0U ||
        WIFI_RADIO_SMARTCONFIG_PENDING || WIFI_RADIO_WPS_PENDING || WIFI_RADIO_DPP_PENDING ||
        WIFI_RADIO_EAP_PENDING || WIFI_RADIO_NAN_PENDING || WIFI_RADIO_MESH_PENDING) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    /* Only a failed suffix is retried. cleanup_stage is native progress, unlike
     * the per-call configuration result which rejected calls may overwrite. */
    if (phase == WIFI_RADIO_RESTORE_STORAGE) goto restore_storage;
    if (phase == WIFI_RADIO_RESTORE_MODE) goto restore_mode;
    result->stage = "restore-defaults";
    result->mutation_attempted = true;
    /* Restore writes defaults through the SDK NVS loader independently of the
     * selected future-write storage. It cannot restore the prior NVS contents. */
    result->persistent_mutation_possible = true;
    err = esp_wifi_restore();
    /* Even a failed default load may have changed part of the configuration.
     * Forget observations, keep identity/revision space and outstanding owners.
     * Never manufacture new policy/interval/rate defaults from SDK success. */
    (void)esp32_mquickjs_wifi_interval_invalidate(&s_interval, s_radio.generation);
    esp32_mquickjs_wifi_policy_invalidate(&s_policies);
    esp32_mquickjs_wifi_tx_rate_invalidate(&s_tx_rates);
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    esp32_mquickjs_wifi_ftm_offset_invalidate(&s_ftm_offset);
#endif
    memset(&s_inactive_history, 0, sizeof(s_inactive_history));
    memset(&s_scan_parameters, 0, sizeof(s_scan_parameters));
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
    memset(&s_he_statistics, 0, sizeof(s_he_statistics));
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    memset(&s_twt_policy, 0, sizeof(s_twt_policy));
#endif
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.primary_channel = 0;
    s_radio.secondary_channel = WIFI_SECOND_CHAN_NONE;
    s_radio.channel_observation_error = ESP_ERR_INVALID_STATE;
    if (++s_radio.channel_generation == 0U) s_radio.channel_generation = 1U;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (err != ESP_OK) goto fault;
restore_mode:
    result->stage = "restore-mode-readback";
    err = esp_wifi_get_mode(&observed);
    if (err == ESP_OK && observed != WIFI_MODE_NULL && observed != WIFI_MODE_STA &&
        observed != WIFI_MODE_AP && observed != WIFI_MODE_APSTA) err = ESP_ERR_INVALID_RESPONSE;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (err == ESP_OK && (observed == WIFI_MODE_AP || observed == WIFI_MODE_APSTA)) err = ESP_ERR_NOT_SUPPORTED;
#endif
    if (err != ESP_OK) goto fault;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = observed; taskEXIT_CRITICAL(&s_radio.lock);
    /* The SDK has no storage getter. Re-establish the caller's selected future
     * write policy explicitly, without rewriting credentials or starting RF. */
restore_storage:
    result->stage = "restore-storage";
    result->mutation_attempted = true;
    err = esp_wifi_set_storage(s_radio.storage);
    if (err != ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock); s_radio.storage_configured = false; taskEXIT_CRITICAL(&s_radio.lock);
        goto fault;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.storage_configured = true;
    s_radio.fault_stage = NULL;
    s_radio.fault_error = ESP_OK;
    s_radio.cleanup_stage = NULL;
    s_radio.cleanup_error = ESP_OK;
    s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    taskEXIT_CRITICAL(&s_radio.lock);
    result->stage = "restore-sdk-accepted";
    goto done;
fault:
    (void)wifi_radio_cleanup_fault(result->stage, err);
done:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.configuration = *result; taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_set_mode(wifi_mode_t mode,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "mode-admission", .error = ESP_ERR_INVALID_ARG};
    if (mode != WIFI_MODE_NULL && mode != WIFI_MODE_STA && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA)
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        result->error = ESP_ERR_NOT_SUPPORTED;
        return result->error;
    }
#endif
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || (s_radio.storage != WIFI_STORAGE_RAM && s_radio.storage != WIFI_STORAGE_FLASH) ||
        s_radio.started || s_radio.stop_required || s_radio.restart_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED || s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL ||
        s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U || s_radio.wake_locks != 0U ||
        s_radio.promiscuous_claimed || s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    wifi_mode_t previous, observed;
    result->stage = "mode-snapshot";
    err = esp_wifi_get_mode(&previous);
    if (err != ESP_OK) goto done;
    if ((previous != WIFI_MODE_NULL && previous != WIFI_MODE_STA && previous != WIFI_MODE_AP && previous != WIFI_MODE_APSTA) ||
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        previous == WIFI_MODE_AP || previous == WIFI_MODE_APSTA ||
#endif
        previous != s_radio.effective_mode) {
        err = wifi_radio_record_fault(result->stage, ESP_ERR_INVALID_RESPONSE);
        goto done;
    }
    if (previous == mode) { result->stage = "complete"; goto done; }
    result->stage = "mode-write";
    result->mutation_attempted = true;
    result->persistent_mutation_possible = s_radio.storage == WIFI_STORAGE_FLASH;
    err = esp_wifi_set_mode(mode);
    if (err == ESP_OK) {
        result->stage = "mode-readback";
        err = esp_wifi_get_mode(&observed);
        if (err == ESP_OK && observed != mode) err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = mode; taskEXIT_CRITICAL(&s_radio.lock);
        result->stage = "complete";
        goto done;
    }
    /* A failed write can already have changed mode. Restore only the verified
     * predecessor; even successful runtime rollback cannot prove NVS rollback. */
    result->rollback_attempted = true;
    result->rollback_stage = "rollback-mode";
    result->rollback_error = esp_wifi_set_mode(previous);
    if (result->rollback_error == ESP_OK) {
        result->rollback_stage = "rollback-mode-readback";
        result->rollback_error = esp_wifi_get_mode(&observed);
        if (result->rollback_error == ESP_OK && observed != previous)
            result->rollback_error = ESP_ERR_INVALID_RESPONSE;
    }
    result->rollback_complete = result->rollback_error == ESP_OK;
    if (result->rollback_complete) result->rollback_stage = NULL;
    if (!result->rollback_complete || result->persistent_mutation_possible)
        (void)wifi_radio_record_fault(result->stage, err);
    if (!result->rollback_complete) (void)wifi_radio_cleanup_fault("mode-rollback", result->rollback_error);
done:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.configuration = *result; taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_set_storage(wifi_storage_t storage,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    static const char storage_fault[] = "storage-write";
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "storage-admission", .error = ESP_ERR_INVALID_ARG};
    if (storage != WIFI_STORAGE_RAM && storage != WIFI_STORAGE_FLASH) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    bool repairing = s_radio.fault_stage == storage_fault && s_radio.cleanup_stage == NULL &&
        s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_FAULTED;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (s_radio.started || s_radio.stop_required || s_radio.restart_required ||
        s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U || s_radio.wake_locks != 0U ||
        s_radio.promiscuous_claimed || s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending ||
        (!repairing && (!s_radio.storage_configured || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
            s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL))) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    result->stage = storage_fault;
    result->mutation_attempted = true;
    err = esp_wifi_set_storage(storage);
    taskENTER_CRITICAL(&s_radio.lock);
    if (err == ESP_OK) {
        s_radio.storage = storage;
        s_radio.storage_configured = true;
        if (repairing) {
            s_radio.fault_stage = NULL;
            s_radio.fault_error = ESP_OK;
            s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        }
    } else {
        /* No getter exists. The previous enum is no longer known to describe
         * the SDK; block other mutations until explicit replacement/cleanup. */
        s_radio.storage_configured = false;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (err != ESP_OK) (void)wifi_radio_record_fault(storage_fault, err);
    else result->stage = "complete";
done:
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.configuration = *result; taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_disable_pmf(wifi_interface_t interface,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "pmf-admission", .error = ESP_ERR_INVALID_ARG};
    if (interface != WIFI_IF_STA && interface != WIFI_IF_AP) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) { result->error = ESP_ERR_NOT_SUPPORTED; return result->error; }
#endif
    wifi_config_t expected = {0}, actual = {0};
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || (s_radio.storage != WIFI_STORAGE_RAM && s_radio.storage != WIFI_STORAGE_FLASH) ||
        s_radio.started || s_radio.stop_required || s_radio.restart_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED || s_radio.fault_stage != NULL ||
        s_radio.cleanup_stage != NULL || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    result->stage = "pmf-snapshot";
    err = esp_wifi_get_config(interface, &expected);
    if (err != ESP_OK) goto done;
    result->stage = "pmf-security";
    if (!esp32_mquickjs_wifi_radio_pmf_disable_allowed(interface, &expected)) { err = ESP_ERR_NOT_SUPPORTED; goto done; }
    wifi_pmf_config_t *pmf = interface == WIFI_IF_STA ? &expected.sta.pmf_cfg : &expected.ap.pmf_cfg;
    if (!pmf->capable && !pmf->required) { result->stage = "complete"; goto done; }
    pmf->capable = false;
    pmf->required = false;
    result->stage = "pmf-write";
    result->mutation_attempted = true;
    result->persistent_mutation_possible = s_radio.storage == WIFI_STORAGE_FLASH;
    err = esp_wifi_disable_pmf_config(interface);
    if (err == ESP_OK) {
        result->stage = "pmf-readback";
        err = esp_wifi_get_config(interface, &actual);
        if (err == ESP_OK && !wifi_radio_config_equal(interface, &expected, &actual)) err = ESP_ERR_INVALID_RESPONSE;
    }
    /* SDK may have cleared one flag or persisted a prefix even on error. Do
     * not guess a rollback or automatically repeat a security mutation. */
    if (err != ESP_OK) (void)wifi_radio_record_fault(result->stage, err);
    else result->stage = "complete";
done:
    esp32_mquickjs_wireless_secure_zero(&actual, sizeof(actual));
    esp32_mquickjs_wireless_secure_zero(&expected, sizeof(expected));
    result->error = err;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.configuration = *result; taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_operation_unlock();
    return err;
}

/* Radio mutex owns both slot history and exact persistent interface leases.
 * Driver copies the IE synchronously; no JS/storage pointer survives set(). */


static wifi_interface_t wifi_radio_vendor_ie_interface(wifi_vendor_ie_type_t frame)
{
    return frame == WIFI_VND_IE_TYPE_PROBE_REQ || frame == WIFI_VND_IE_TYPE_ASSOC_REQ ? WIFI_IF_STA : WIFI_IF_AP;
}

static void wifi_radio_vendor_ie_release_empty(wifi_interface_t interface)
{
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_VENDOR_IE_SLOTS; ++i) {
        if (wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)(i / 2U)) == interface &&
            (s_vendor_ie.slots[i].length != 0U || s_vendor_ie.slots[i].pending)) return;
    }
    wifi_radio_release_locked(&s_vendor_ie.leases[interface]);
}

static esp_err_t wifi_radio_vendor_ie_remove(unsigned index, const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    esp32_mquickjs_wifi_vendor_ie_slot_t *slot = &s_vendor_ie.slots[index];
    if (slot->length == 0U && !slot->pending) return ESP_OK;
    wifi_vendor_ie_type_t frame = (wifi_vendor_ie_type_t)(index / 2U);
    if (!wifi_radio_lease_valid(&s_vendor_ie.leases[wifi_radio_vendor_ie_interface(frame)]) &&
        !wifi_radio_vendor_ie_start_matches(token))
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_wifi_set_vendor_ie(false, frame, (wifi_vendor_ie_id_t)(index % 2U), NULL);
    if (err == ESP_OK) memset(slot, 0, sizeof(*slot));
    else { slot->pending = true; slot->error = err; }
    return err;
}

/* The start token owns parked copies until resume restores exact registry
 * owners, or cleanup removes the copies. No identity is recycled. */
static bool wifi_radio_vendor_ie_start_matches(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return token != NULL && token->identity != 0U &&
        token->identity == s_vendor_ie.start_owner.identity && token->generation == s_vendor_ie.start_owner.generation &&
        token->identity == s_radio.lifecycle.identity && token->generation == s_radio.lifecycle.generation &&
        token->generation == s_radio.generation;
}

static unsigned wifi_radio_vendor_ie_start_count(void)
{
    unsigned mask = 0;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_VENDOR_IE_SLOTS; ++i)
        if (s_vendor_ie.slots[i].length != 0U || s_vendor_ie.slots[i].pending)
            mask |= 1U << wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)(i / 2U));
    return (mask & 1U) + ((mask >> 1U) & 1U);
}

static void wifi_radio_vendor_ie_start_park(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (!wifi_radio_vendor_ie_start_matches(token)) return;
    for (unsigned i = 0; i < 2; ++i) wifi_radio_release_locked(&s_vendor_ie.leases[i]);
}

static esp_err_t wifi_radio_vendor_ie_begin_start_locked(
    const esp32_mquickjs_wifi_radio_lease_t *application, const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (s_vendor_ie.start_owner.identity != 0U) return ESP_ERR_INVALID_STATE;
    if (wifi_radio_vendor_ie_start_count() == 0U)
        return wifi_radio_begin_lifecycle_locked(application, station, access_point, token);
    if (!s_radio.driver_owned || !s_radio.storage_configured || s_radio.started || s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
        selection->mode != s_radio.effective_mode || selection->storage != s_radio.storage ||
        s_interval.owner.identity != 0U || s_interval.uncertain || s_tx_rate_lease.identity != 0U)
        return ESP_ERR_INVALID_STATE;
    unsigned mask = 0;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_VENDOR_IE_SLOTS; ++i) {
        if (s_vendor_ie.slots[i].pending) return ESP_ERR_INVALID_STATE;
        if (s_vendor_ie.slots[i].length != 0U)
            mask |= 1U << wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)(i / 2U));
    }
    for (unsigned i = 0; i < 2; ++i) {
        const esp32_mquickjs_wifi_radio_lease_t *lease = &s_vendor_ie.leases[i];
        if (!!(mask & (1U << i)) != lease->acquired) return ESP_ERR_INVALID_STATE;
        if (!(mask & (1U << i))) continue;
        if (!wifi_radio_lease_valid(lease) || lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE)
            return ESP_ERR_INVALID_STATE;
        wifi_radio_live_lease_t *live = wifi_radio_promiscuous_owner(lease->identity);
        wifi_mode_t required = i == WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP;
        if (live == NULL || live->required_mode != required || (selection->mode & required) != required ||
            live->promiscuous_identity != 0U || live->raw_tx_identity != 0U || live->fixed_channel ||
            live->channel_conflict) return ESP_ERR_INVALID_STATE;
    }
    unsigned needed = wifi_radio_vendor_ie_start_count() + ((selection->mode & WIFI_MODE_STA) ? 2U : 0U) +
        ((selection->mode & WIFI_MODE_AP) ? 1U : 0U);
    if (needed > WIFI_RADIO_MAX_LEASES || s_radio.next_lease_identity == 0U ||
        needed - 1U > UINT32_MAX - s_radio.next_lease_identity) return ESP_ERR_NO_MEM;
    esp_err_t err = wifi_radio_begin_lifecycle_with_dependents_locked(application, station, access_point,
        &s_vendor_ie.leases[0], &s_vendor_ie.leases[1], token, 0);
    if (err != ESP_OK) return err;
    s_vendor_ie.start_owner = *token;
    s_vendor_ie.start_mode = selection->mode;
    s_vendor_ie.start_storage = selection->storage;
    wifi_radio_vendor_ie_start_park(token);
    return ESP_OK;
}

static esp_err_t wifi_radio_vendor_ie_start_attach(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (s_vendor_ie.start_owner.identity == 0U) return ESP_OK;
    if (!wifi_radio_vendor_ie_start_matches(token)) return ESP_ERR_INVALID_STATE;
    unsigned mask = 0;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_VENDOR_IE_SLOTS; ++i) {
        if (s_vendor_ie.slots[i].pending) return ESP_ERR_INVALID_STATE;
        if (s_vendor_ie.slots[i].length != 0U)
            mask |= 1U << wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)(i / 2U));
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (!(mask & (1U << i))) continue;
        if (s_vendor_ie.leases[i].acquired) return ESP_ERR_INVALID_STATE;
        esp_err_t err = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE,
            i == WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP, &s_vendor_ie.leases[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static void wifi_radio_vendor_ie_start_commit(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (!wifi_radio_vendor_ie_start_matches(token)) return;
    memset(&s_vendor_ie.start_owner, 0, sizeof(s_vendor_ie.start_owner));
    s_vendor_ie.start_mode = WIFI_MODE_NULL;
    s_vendor_ie.start_storage = WIFI_STORAGE_RAM;
}

esp_err_t esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_OK;
    if (s_vendor_ie.start_owner.identity == 0U) goto done;
    if (!wifi_radio_vendor_ie_start_matches(token) || !s_radio.driver_owned) {
        err = ESP_ERR_INVALID_STATE; goto done;
    }
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_VENDOR_IE_SLOTS; ++i) {
        esp_err_t removal = wifi_radio_vendor_ie_remove(i, token);
        if (err == ESP_OK && removal != ESP_OK) err = removal;
    }
    wifi_radio_vendor_ie_release_empty(WIFI_IF_STA);
    wifi_radio_vendor_ie_release_empty(WIFI_IF_AP);
    if (err == ESP_OK) wifi_radio_vendor_ie_start_commit(token);
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_vendor_ie_set(wifi_interface_t interface,
    wifi_vendor_ie_type_t frame, unsigned index, bool enabled, const uint8_t *bytes, size_t length)
{
    if ((interface != WIFI_IF_STA && interface != WIFI_IF_AP) || (unsigned)frame > WIFI_VND_IE_TYPE_ASSOC_RESP ||
        index > 1U || wifi_radio_vendor_ie_interface(frame) != interface ||
        (enabled && (bytes == NULL || length < 6U || length > ESP32_MQUICKJS_WIFI_VENDOR_IE_MAX_BYTES ||
                     bytes[0] != WIFI_VENDOR_IE_ELEMENT_ID || (size_t)bytes[1] + 2U != length)) ||
        (!enabled && (bytes != NULL || length != 0U))) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (s_radio.lifecycle.identity != 0U || s_vendor_ie.start_owner.identity != 0U) goto done;
    unsigned slot_index = (unsigned)frame * 2U + index;
    esp32_mquickjs_wifi_vendor_ie_slot_t *slot = &s_vendor_ie.slots[slot_index];
    if (!enabled) {
        err = wifi_radio_vendor_ie_remove(slot_index, NULL);
        wifi_radio_vendor_ie_release_empty(interface);
        goto done;
    }
    if (!s_radio.storage_configured || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED) ||
        (!s_radio.started && s_radio.stop_required)) goto done;
    wifi_mode_t required = interface == WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP;
    if ((s_radio.effective_mode & required) != required || slot->length != 0U || slot->pending) goto done;
    esp32_mquickjs_wifi_radio_lease_t *lease = &s_vendor_ie.leases[interface];
    if (!lease->acquired) {
        err = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE, required, lease);
        if (err != ESP_OK) goto done;
    } else if (!wifi_radio_lease_valid(lease)) goto done;
    /* Reserve the bounded cleanup obligation before a possibly partial write.
     * A failed call cannot free this owner or be silently resubmitted. */
    slot->pending = true;
    err = esp_wifi_set_vendor_ie(true, frame, (wifi_vendor_ie_id_t)index, bytes);
    slot->error = err;
    if (err == ESP_OK) { slot->length = (uint16_t)length; slot->pending = false; }
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_vendor_ie_clear(int interface)
{
    if (interface != -1 && interface != WIFI_IF_STA && interface != WIFI_IF_AP) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_OK;
    bool any = false;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_VENDOR_IE_SLOTS; ++i)
        if ((interface == -1 || wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)(i / 2U)) == interface) &&
            (s_vendor_ie.slots[i].length != 0U || s_vendor_ie.slots[i].pending)) any = true;
    if (!any) goto done;
    /* Explicit removal is allowed in a faulted driver; never clear its fault.
     * There is no callback or pending send identity to wait on for these copies. */
    if (!s_radio.driver_owned || s_radio.lifecycle.identity != 0U) { err = ESP_ERR_INVALID_STATE; goto done; }
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_VENDOR_IE_SLOTS; ++i) {
        wifi_interface_t selected = wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)(i / 2U));
        if (interface != -1 && selected != interface) continue;
        esp_err_t removal = wifi_radio_vendor_ie_remove(i, NULL);
        if (err == ESP_OK && removal != ESP_OK) err = removal;
    }
    if (interface == -1 || interface == WIFI_IF_STA) wifi_radio_vendor_ie_release_empty(WIFI_IF_STA);
    if (interface == -1 || interface == WIFI_IF_AP) wifi_radio_vendor_ie_release_empty(WIFI_IF_AP);
done:
    wifi_radio_operation_unlock();
    return err;
}

void esp32_mquickjs_wifi_radio_vendor_ie_status(esp32_mquickjs_wifi_vendor_ie_status_t *status)
{
    if (status == NULL) return;
    wifi_radio_operation_lock();
    *status = (esp32_mquickjs_wifi_vendor_ie_status_t){.generation = s_radio.generation,
        .start_pending = s_vendor_ie.start_owner.identity != 0U};
    memcpy(status->slots, s_vendor_ie.slots, sizeof(status->slots));
    for (unsigned i = 0; i < 2U; ++i) if (s_vendor_ie.leases[i].acquired) ++status->owners;
    wifi_radio_operation_unlock();
}

esp_err_t esp32_mquickjs_wifi_radio_event_mask(bool write, uint32_t requested,
    uint32_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (actual != NULL) *actual = 0;
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "admission", .error = ESP_ERR_INVALID_ARG};
    const uint32_t allowed = WIFI_EVENT_MASK_AP_PROBEREQRECVED;
    if (actual == NULL || (write && (requested & ~allowed) != 0U)) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)) goto done;
    uint32_t previous = 0, observed = 0;
    result->stage = "event-mask-read";
    err = esp_wifi_get_event_mask(&previous);
    if (err != ESP_OK) goto done;
    if (!write || previous == requested) {
        *actual = previous;
        result->stage = "complete";
        goto done;
    }
    result->stage = "event-mask-write";
    result->mutation_attempted = true;
    err = esp_wifi_set_event_mask(requested);
    if (err == ESP_OK) {
        result->stage = "event-mask-readback";
        err = esp_wifi_get_event_mask(&observed);
        if (err == ESP_OK && observed != requested) err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        *actual = observed;
        result->stage = "complete";
        goto done;
    }
    if ((previous & ~allowed) == 0U) {
        result->rollback_attempted = true;
        result->rollback_stage = "rollback-event-mask";
        result->rollback_error = esp_wifi_set_event_mask(previous);
        if (result->rollback_error == ESP_OK) {
            result->rollback_stage = "rollback-event-mask-readback";
            result->rollback_error = esp_wifi_get_event_mask(&observed);
            if (result->rollback_error == ESP_OK && observed != previous)
                result->rollback_error = ESP_ERR_INVALID_RESPONSE;
        }
        result->rollback_complete = result->rollback_error == ESP_OK;
        if (result->rollback_complete) result->rollback_stage = NULL;
    }
    /* A failed setter may already have taken effect. Never restore an unsafe
     * externally supplied mask just to make a transaction look reversible. */
    if (!result->rollback_complete) {
        (void)wifi_radio_record_fault(result->stage, err);
        if (result->rollback_attempted)
            (void)wifi_radio_cleanup_fault("event-mask-rollback", result->rollback_error);
    }
done:
    result->error = err;
    if (write) {
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.configuration = *result;
        taskEXIT_CRITICAL(&s_radio.lock);
    }
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_dump_stats(uint32_t mask, const char **stage)
{
    if (stage == NULL) return ESP_ERR_INVALID_ARG;
    *stage = "mask";
    const uint32_t bits = WIFI_STATIS_BUFFER | WIFI_STATIS_RXTX | WIFI_STATIS_HW |
        WIFI_STATIS_DIAG | WIFI_STATIS_PS;
    if (mask != (uint32_t)WIFI_STATIS_ALL && (mask & ~bits) != 0U) return ESP_ERR_INVALID_ARG;
    *stage = "admission";
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)) goto done;
    *stage = "driver-dump";
    err = esp_wifi_statis_dump(mask);
    if (err == ESP_OK) *stage = NULL;
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_read_state(esp32_mquickjs_wifi_driver_state_query_t query,
    esp32_mquickjs_wifi_driver_state_readback_t *output, const char **stage)
{
    if (output != NULL) memset(output, 0, sizeof(*output));
    if (stage != NULL) *stage = "admission";
    if (output == NULL || stage == NULL || (unsigned)query > ESP32_MQUICKJS_WIFI_DRIVER_STATE_HOME_CHANNEL)
        return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_driver_state_readback_t value = {0};
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)) goto done;
    switch (query) {
    case ESP32_MQUICKJS_WIFI_DRIVER_STATE_MODE:
        *stage = "mode";
        err = esp_wifi_get_mode(&value.mode);
        if (err != ESP_OK) goto done;
        if (value.mode != WIFI_MODE_NULL && value.mode != WIFI_MODE_STA &&
            value.mode != WIFI_MODE_AP && value.mode != WIFI_MODE_APSTA) goto invalid;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if (value.mode == WIFI_MODE_AP || value.mode == WIFI_MODE_APSTA) goto invalid;
#endif
        break;
    case ESP32_MQUICKJS_WIFI_DRIVER_STATE_COUNTRY: {
        *stage = "country";
        err = esp_wifi_get_country(&value.country);
        if (err != ESP_OK) goto done;
        const wifi_country_t *c = &value.country;
        if (!wifi_radio_country_valid(c)) goto invalid;
        /* Raw 5 GHz mask is reported without treating zero/automatic as an
         * authoritative channel allowlist. No regulatory reconfiguration. */
        break;
    }
    case ESP32_MQUICKJS_WIFI_DRIVER_STATE_CHANNEL:
    case ESP32_MQUICKJS_WIFI_DRIVER_STATE_HOME_CHANNEL:
        if (query == ESP32_MQUICKJS_WIFI_DRIVER_STATE_HOME_CHANNEL) {
            if (!s_radio.started) { err = ESP_ERR_WIFI_NOT_STARTED; goto done; }
            *stage = "home-channel";
            /* Home channel has no callback revision. Never write it into the
             * current-channel cache or attach that cache's generation to it. */
            err = esp_wifi_get_home_channel(&value.channel, &value.secondary);
        } else {
            *stage = "channel";
            err = wifi_radio_get_channel_locked(&value.channel, &value.secondary, &value.channel_generation);
        }
        if (err != ESP_OK) goto done;
        if (value.channel < 1 || (value.channel > 14 &&
#if CONFIG_SOC_WIFI_SUPPORT_5G
            esp32_mquickjs_wifi_radio_5ghz_channel_bit(value.channel) == 0U
#else
            true
#endif
            ) || (value.secondary != WIFI_SECOND_CHAN_NONE && value.secondary != WIFI_SECOND_CHAN_ABOVE &&
                  value.secondary != WIFI_SECOND_CHAN_BELOW)) goto invalid;
        break;
    }
    *output = value;
    *stage = NULL;
    goto done;
invalid:
    *stage = "decode";
    err = ESP_ERR_INVALID_RESPONSE;
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_read_driver(esp32_mquickjs_wifi_driver_query_t query,
    wifi_interface_t interface, int64_t *output, const char **stage)
{
    if (output != NULL) *output = 0;
    if (stage != NULL) *stage = "admission";
    if (output == NULL || stage == NULL ||
        (unsigned)query > ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME ||
        (interface != WIFI_IF_STA && interface != WIFI_IF_AP)) return ESP_ERR_INVALID_ARG;
    bool station = query == ESP32_MQUICKJS_WIFI_DRIVER_RSSI || query == ESP32_MQUICKJS_WIFI_DRIVER_AID ||
                   query == ESP32_MQUICKJS_WIFI_DRIVER_NEGOTIATED_PHY;
    bool by_interface = query == ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME || query == ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME;
    if ((!by_interface || station) && interface != WIFI_IF_STA) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    bool started = query == ESP32_MQUICKJS_WIFI_DRIVER_TX_POWER || query == ESP32_MQUICKJS_WIFI_DRIVER_RSSI ||
                   query == ESP32_MQUICKJS_WIFI_DRIVER_NEGOTIATED_PHY || by_interface;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    int64_t value = 0;
    if (!s_radio.driver_owned) { err = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)) goto done;
    if ((station || by_interface) && !(s_radio.effective_mode & (interface == WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP)))
        goto done;
    if (started && !s_radio.started) { err = ESP_ERR_WIFI_NOT_STARTED; goto done; }
    switch (query) {
    case ESP32_MQUICKJS_WIFI_DRIVER_BAND: {
        wifi_band_t band;
        *stage = "band";
        err = esp_wifi_get_band(&band);
        if (err != ESP_OK) goto done;
        value = band;
        if (band != WIFI_BAND_2G && band != WIFI_BAND_5G) goto invalid;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
        if (band != WIFI_BAND_2G) goto invalid;
#endif
        break;
    }
    case ESP32_MQUICKJS_WIFI_DRIVER_BAND_MODE: {
        wifi_band_mode_t mode;
        *stage = "band-mode";
        err = esp_wifi_get_band_mode(&mode);
        if (err != ESP_OK) goto done;
        value = mode;
        if (mode != WIFI_BAND_MODE_2G_ONLY && mode != WIFI_BAND_MODE_5G_ONLY && mode != WIFI_BAND_MODE_AUTO) goto invalid;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
        if (mode != WIFI_BAND_MODE_2G_ONLY) goto invalid;
#endif
        break;
    }
    case ESP32_MQUICKJS_WIFI_DRIVER_POWER_SAVE: {
        wifi_ps_type_t mode;
        *stage = "power-save";
        err = esp_wifi_get_ps(&mode);
        if (err != ESP_OK) goto done;
        value = mode;
        if (mode != WIFI_PS_NONE && mode != WIFI_PS_MIN_MODEM && mode != WIFI_PS_MAX_MODEM) goto invalid;
        break;
    }
    case ESP32_MQUICKJS_WIFI_DRIVER_TX_POWER: {
        int8_t power;
        *stage = "tx-power";
        err = esp_wifi_get_max_tx_power(&power);
        if (err != ESP_OK) goto done;
        value = power;
        break;
    }
    case ESP32_MQUICKJS_WIFI_DRIVER_RSSI: {
        int rssi;
        *stage = "rssi";
        err = esp_wifi_sta_get_rssi(&rssi);
        if (err != ESP_OK) goto done;
        value = rssi;
        break;
    }
    case ESP32_MQUICKJS_WIFI_DRIVER_AID: {
        uint16_t aid;
        *stage = "aid";
        err = esp_wifi_sta_get_aid(&aid);
        if (err != ESP_OK) goto done;
        value = aid; /* SDK reports zero when not associated. */
        break;
    }
    case ESP32_MQUICKJS_WIFI_DRIVER_NEGOTIATED_PHY: {
        wifi_phy_mode_t phy;
        *stage = "negotiated-phy";
        err = esp_wifi_sta_get_negotiated_phymode(&phy);
        if (err != ESP_OK) goto done;
        if (esp32_mquickjs_wifi_tx_phy_name(phy) == NULL) goto invalid;
        value = phy;
        break;
    }
    case ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME:
        *stage = "tsf-time";
        value = esp_wifi_get_tsf_time(interface);
        if (value < 0 || value > INT64_C(9007199254740991)) goto invalid;
        err = ESP_OK;
        break;
    case ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME: {
        uint16_t seconds;
        *stage = "inactive-time";
        err = esp_wifi_get_inactive_time(interface, &seconds);
        if (err != ESP_OK) goto done;
        value = seconds;
        break;
    }
    default:
        err = ESP_ERR_INVALID_ARG;
        goto done;
    }
    *output = value;
    *stage = NULL;
    goto done;
invalid:
    *stage = "decode";
    err = ESP_ERR_INVALID_RESPONSE;
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_get_mac(wifi_interface_t interface, uint8_t mac[6])
{
    if (mac == NULL || (interface != WIFI_IF_STA && interface != WIFI_IF_AP))
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    uint8_t value[6];
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_radio.driver_owned && s_radio.lifecycle.identity == 0U &&
        s_radio.fault_stage == NULL && s_radio.cleanup_stage == NULL) {
        err = esp_wifi_get_mac(interface, value);
        if (err == ESP_OK) memcpy(mac, value, sizeof(value));
    }
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_set_mac(wifi_interface_t interface, const uint8_t requested[6],
    uint8_t actual[6], esp32_mquickjs_wifi_radio_mutation_t *mutation)
{
    if (mutation == NULL) return ESP_ERR_INVALID_ARG;
    *mutation = (esp32_mquickjs_wifi_radio_mutation_t){.stage = "admission"};
    if (requested == NULL || actual == NULL ||
        (interface != WIFI_IF_STA && interface != WIFI_IF_AP) ||
        (requested[0] & 1U) != 0 || memcmp(requested, "\0\0\0\0\0\0", 6) == 0)
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned || !s_radio.storage_configured || s_radio.started || s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
        s_radio.lifecycle.identity != 0 || s_radio.operation.identity != 0 ||
        s_radio.promiscuous_claimed || s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0) goto done;
    /* The SDK requires disabled interfaces and distinct interface addresses.
     * A fully stopped, owner-free driver excludes both activity and re-start. */
    static const wifi_interface_t interfaces[] = {
        WIFI_IF_STA,
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        WIFI_IF_AP,
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
        WIFI_IF_NAN,
#endif
    };
    mutation->stage = "mac-collision-check";
    for (size_t i = 0; i < sizeof(interfaces) / sizeof(*interfaces); ++i) {
        if (interfaces[i] == interface) continue;
        uint8_t other[6];
        err = esp_wifi_get_mac(interfaces[i], other);
        if (err != ESP_OK) goto done;
        if (memcmp(other, requested, 6) == 0) { err = ESP_ERR_INVALID_ARG; goto done; }
    }
    mutation->stage = "mac-set";
    err = esp_wifi_set_mac(interface, requested);
    if (err != ESP_OK) goto done;
    mutation->driver_accepted = true;
    mutation->stage = "mac-readback";
    uint8_t value[6];
    err = esp_wifi_get_mac(interface, value);
    if (err == ESP_OK) {
        if (memcmp(value, requested, 6) != 0) err = ESP_ERR_INVALID_RESPONSE;
        else { memcpy(actual, value, 6); mutation->stage = NULL; }
    }
done:
    wifi_radio_operation_unlock();
    return err;
}

#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
esp_err_t esp32_mquickjs_wifi_radio_sample_ap(
    const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_ap_snapshot_t *snapshot, const char **stage)
{
    if (snapshot == NULL || stage == NULL) return ESP_ERR_INVALID_ARG;
    memset(snapshot, 0, sizeof(*snapshot));
    *stage = "admission";
    /* Keep the SDK config, including its password, off the runtime task stack.
     * Nothing from this temporary workspace escapes except whitelisted fields. */
    struct ap_query { wifi_config_t config; wifi_sta_list_t stations; } *query = NULL;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_lease_valid(lease) ||
        (lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP &&
         !(lease->client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX &&
           s_tx_rate_lease.interface == WIFI_IF_AP && s_tx_rate_lease.identity == lease->identity &&
           s_tx_rate_lease.generation == lease->generation)) ||
        !s_radio.started || s_radio.lifecycle.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL) goto done;
    wifi_mode_t mode;
    *stage = "mode";
    err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) goto done;
    if ((mode & WIFI_MODE_AP) == 0) { err = ESP_ERR_INVALID_STATE; goto done; }
    *stage = "allocate";
    query = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*query), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (query == NULL) { err = ESP_ERR_NO_MEM; goto done; }
    *stage = "config";
    err = esp_wifi_get_config(WIFI_IF_AP, &query->config);
    if (err != ESP_OK) goto done;
    size_t length = query->config.ap.ssid_len;
    if (length == 0) length = strnlen((const char *)query->config.ap.ssid, 32);
    if (length > sizeof(snapshot->ssid)) { err = ESP_ERR_INVALID_SIZE; goto done; }
    memcpy(snapshot->ssid, query->config.ap.ssid, length);
    snapshot->ssid_len = length;
    snapshot->hidden = query->config.ap.ssid_hidden;
    snapshot->authmode = query->config.ap.authmode;
    snapshot->max_connections = query->config.ap.max_connection;
    *stage = "mac";
    err = esp_wifi_get_mac(WIFI_IF_AP, snapshot->mac);
    if (err != ESP_OK) goto done;
    *stage = "clients";
    err = esp_wifi_ap_get_sta_list(&query->stations);
    if (err != ESP_OK) goto done;
    if (query->stations.num < 0 || query->stations.num > ESP_WIFI_MAX_CONN_NUM) {
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    snapshot->client_count = query->stations.num;
    *stage = "channel";
    wifi_second_chan_t secondary;
    err = esp_wifi_get_channel(&snapshot->channel, &secondary);
    if (err == ESP_OK) *stage = NULL;
done:
    if (query != NULL) {
        esp32_mquickjs_wireless_secure_zero(query, sizeof(*query));
        esp32_mquickjs_memory_payload_free(query);
    }
    taskENTER_CRITICAL(&s_radio.lock);
    bool started = (s_radio.event_live & WIFI_MODE_AP) != 0;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (err == ESP_OK && !started) { err = ESP_ERR_INVALID_STATE; *stage = "state"; }
    if (err != ESP_OK) memset(snapshot, 0, sizeof(*snapshot));
    snapshot->started = started;
    wifi_radio_operation_unlock();
    return err;
}

#include "esp32_mquickjs_wifi_ap_deauth.inc"

esp_err_t esp32_mquickjs_wifi_radio_ap_clients(
    const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_ap_clients_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    memset(snapshot, 0, sizeof(*snapshot));
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_lease_valid(lease) ||
        lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP ||
        !s_radio.started || s_radio.lifecycle.identity != 0U ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL) goto done;
    err = esp_wifi_ap_get_sta_list(&snapshot->stations);
    if (err != ESP_OK) goto done;
    if (snapshot->stations.num < 0 || snapshot->stations.num > ESP_WIFI_MAX_CONN_NUM) {
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    for (int i = 0; i < snapshot->stations.num; ++i) {
        err = esp_wifi_ap_get_sta_aid(snapshot->stations.sta[i].mac, &snapshot->aid[i]);
        if (err == ESP_ERR_NOT_FOUND) {
            snapshot->aid[i] = 0;
            err = ESP_OK;
        }
        if (err != ESP_OK) goto done;
    }
done:
    wifi_radio_operation_unlock();
    if (err != ESP_OK) memset(snapshot, 0, sizeof(*snapshot));
    return err;
}
#endif
#endif

esp_err_t esp32_mquickjs_wifi_radio_get_channel(
    uint8_t *primary, wifi_second_chan_t *secondary, uint32_t *generation)
{
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_get_channel_locked(primary, secondary, generation);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_get_status(
    esp32_mquickjs_wifi_radio_status_t *status)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    wifi_radio_snapshot(status);
    if (status->lifecycle_active || status->driver_state == ESP32_MQUICKJS_WIFI_RADIO_INITIALIZING ||
        status->driver_state == ESP32_MQUICKJS_WIFI_RADIO_STARTING ||
        status->driver_state == ESP32_MQUICKJS_WIFI_RADIO_STOPPING ||
        status->driver_state == ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING)
        return ESP_OK;
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_get_status_locked(status);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_acquire(
    esp32_mquickjs_wifi_radio_client_t client, wifi_mode_t required_mode, esp32_mquickjs_wifi_radio_lease_t *out_lease)
{
    wifi_radio_operation_lock();
    esp_err_t err = client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP ? ESP_ERR_INVALID_ARG :
        s_radio.lifecycle.identity != 0U ? ESP_ERR_INVALID_STATE :
        wifi_radio_acquire_locked(client, required_mode, out_lease);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_ensure_started(
    esp32_mquickjs_wifi_radio_lease_t *lease)
{
    wifi_radio_operation_lock();
    esp_err_t err = s_radio.lifecycle.identity != 0U ? ESP_ERR_INVALID_STATE :
        wifi_radio_ensure_started_locked(lease);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_set_channel(
    esp32_mquickjs_wifi_radio_lease_t *lease, uint8_t primary, wifi_second_chan_t secondary)
{
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_set_channel_locked(lease, primary, secondary);
    wifi_radio_operation_unlock();
    return err;
}

void esp32_mquickjs_wifi_radio_release_channel(
    esp32_mquickjs_wifi_radio_lease_t *lease)
{
    wifi_radio_operation_lock();
    wifi_radio_release_channel_locked(lease);
    wifi_radio_operation_unlock();
}

esp_err_t esp32_mquickjs_wifi_radio_acquire_promiscuous(
    esp32_mquickjs_wifi_radio_lease_t *radio_lease, esp32_mquickjs_wifi_radio_promiscuous_lease_t *out_lease)
{
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_acquire_promiscuous_locked(radio_lease, out_lease, NULL, NULL, NULL, NULL);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_subscribe_promiscuous(
    esp32_mquickjs_wifi_radio_lease_t *radio_lease,
    esp32_mquickjs_wifi_promiscuous_subscriber_t *subscriber,
    const esp32_mquickjs_wifi_rx_filter_t *filter,
    esp32_mquickjs_wifi_promiscuous_sink_t sink, void *context,
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *out_lease)
{
    if (subscriber == NULL) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_acquire_promiscuous_locked(radio_lease, out_lease, subscriber, filter, sink, context);
    wifi_radio_operation_unlock();
    return err;
}

void esp32_mquickjs_wifi_radio_release_promiscuous(
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *lease)
{
    wifi_radio_operation_lock();
    wifi_radio_release_promiscuous_locked(lease);
    wifi_radio_operation_unlock();
}

void esp32_mquickjs_wifi_radio_release(
    esp32_mquickjs_wifi_radio_lease_t *lease)
{
    wifi_radio_operation_lock();
    wifi_radio_release_locked(lease);
    wifi_radio_operation_unlock();
}

const void *
esp32_mquickjs_wifi_radio_channel_key(void)
{
    return &s_wifi_radio_channel_lane_key;
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
esp_err_t esp32_mquickjs_wifi_radio_begin_raw_tx_ap_rate(
    const wifi_tx_rate_config_t *rate, esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode)
{
    if (token == NULL || token->identity != 0U || token->generation != 0U || mode == NULL ||
        !esp32_mquickjs_wifi_tx_rate_valid(rate)) return ESP_ERR_INVALID_ARG;
    *mode = WIFI_MODE_NULL;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    return ESP_ERR_NOT_SUPPORTED;
#else
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned || !s_radio.storage_configured || s_radio.started || s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
        (s_radio.effective_mode != WIFI_MODE_AP && s_radio.effective_mode != WIFI_MODE_APSTA) ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        s_interval.owner.identity != 0U || s_interval.uncertain || s_interval.restore_pending ||
        s_vendor_ie.start_owner.identity != 0U || s_config_restart.snapshot != NULL ||
        s_policy_restart.owner.identity != 0U) goto done;
    err = esp32_mquickjs_wifi_tx_rate_borrow_admission(&s_tx_rates, s_radio.generation,
        WIFI_IF_AP, rate, &s_tx_rate_lease);
    if (err == ESP_OK) err = wifi_radio_begin_lifecycle_locked(NULL, NULL, NULL, token);
    if (err == ESP_OK) *mode = s_radio.effective_mode;
done:
    wifi_radio_operation_unlock();
    return err;
#endif
}

esp_err_t esp32_mquickjs_wifi_radio_start_raw_tx_ap_rate(
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t requested_mode, const wifi_config_t *expected,
    const wifi_tx_rate_config_t *rate, esp32_mquickjs_wifi_radio_lease_t *lease,
    uint8_t *actual_channel)
{
    if (token == NULL || token->identity == 0U || expected == NULL || lease == NULL ||
        (requested_mode != WIFI_MODE_AP && requested_mode != WIFI_MODE_APSTA) ||
        lease->acquired || lease->identity != 0U || lease->generation != 0U || actual_channel == NULL ||
        !esp32_mquickjs_wifi_tx_rate_valid(rate)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    return ESP_ERR_NOT_SUPPORTED;
#else
    /* Reserve temporary credential storage before taking any native owner. */
    wifi_config_t *actual = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*actual), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (actual == NULL) return ESP_ERR_NO_MEM;
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_check_stopped_lifecycle_locked(token, false);
    if (err != ESP_OK) goto done;
    if (!s_radio.driver_owned || !s_radio.storage_configured || s_radio.effective_mode != requested_mode ||
        s_vendor_ie.start_owner.identity != 0U || s_config_restart.snapshot != NULL ||
        s_policy_restart.owner.identity != 0U) { err = ESP_ERR_INVALID_STATE; goto done; }
    err = esp32_mquickjs_wifi_tx_rate_borrow_admission(&s_tx_rates, s_radio.generation,
        WIFI_IF_AP, rate, &s_tx_rate_lease);
    if (err != ESP_OK) goto done;
    *actual = *expected;
    err = wifi_radio_validate_saved_ap_config(actual);
    if (err != ESP_OK) goto done;
    if (expected->ap.channel != 0U) {
        err = wifi_radio_validate_regulatory_channel(expected->ap.channel);
        if (err != ESP_OK) goto done;
    }
    wifi_mode_t mode;
    err = esp_wifi_get_mode(&mode);
    if (err == ESP_OK && mode != requested_mode) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) goto done;
    err = esp_wifi_get_config(WIFI_IF_AP, actual);
    if (err == ESP_OK && !wifi_radio_config_equal(WIFI_IF_AP, expected, actual)) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) goto done;
    err = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX, requested_mode, lease);
    if (err != ESP_OK) goto done;
    err = esp32_mquickjs_wifi_tx_rate_borrow(&s_tx_rates, lease->generation, lease->identity,
        WIFI_IF_AP, rate, &s_tx_rate_lease, wifi_radio_write_tx_rate, NULL);
    if (err != ESP_OK) {
        if (s_tx_rate_lease.restore_pending)
            wifi_radio_cleanup_fault(s_tx_rate_restore_fault, s_tx_rate_lease.restore_error);
        goto handoff;
    }
    err = wifi_radio_ensure_started_locked(lease);
    if (err != ESP_OK) goto handoff;
    err = esp_wifi_get_mode(&mode);
    if (err == ESP_OK && mode != requested_mode) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) { wifi_radio_record_fault("raw-tx-ap-mode", err); goto handoff; }
    uint8_t primary;
    wifi_second_chan_t secondary;
    uint32_t revision;
    err = wifi_radio_get_channel_locked(&primary, &secondary, &revision);
    if (err == ESP_OK && (primary == 0U || (expected->ap.channel != 0U && primary != expected->ap.channel)))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) *actual_channel = primary;
    else wifi_radio_record_fault("raw-tx-ap-channel", err);
handoff:
    /* A live temporary-rate record excludes every new Radio/wake owner, and
     * retains the real lease until STOP/restore. This remains true after failed
     * write/START. If rollback removed the record, retain lifecycle instead. */
    if (s_tx_rate_lease.identity == lease->identity && s_tx_rate_lease.generation == lease->generation) {
        taskENTER_CRITICAL(&s_radio.lock);
        memset(&s_radio.lifecycle, 0, sizeof(s_radio.lifecycle));
        taskEXIT_CRITICAL(&s_radio.lock);
        memset(token, 0, sizeof(*token));
    }
done:
    wifi_radio_operation_unlock();
    esp32_mquickjs_wireless_secure_zero(actual, sizeof(*actual));
    esp32_mquickjs_memory_payload_free(actual);
    return err;
#endif
}

esp_err_t esp32_mquickjs_wifi_radio_quiesce_raw_tx_ap_rate(
    esp32_mquickjs_wifi_radio_lease_t *lease, esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (lease == NULL || token == NULL || (token->identity == 0U && token->generation != 0U)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    return ESP_ERR_NOT_SUPPORTED;
#else
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    bool reserved = token->identity != 0U;
    if (reserved ? token->identity != s_radio.lifecycle.identity || token->generation != s_radio.lifecycle.generation
                 : s_radio.lifecycle.identity != 0U) goto done;
    if (s_radio.wake_locks != 0U || s_radio.operation.identity != 0U || s_radio.promiscuous_claimed) goto done;
    if (!reserved && s_radio.next_lifecycle_identity == 0U) { err = ESP_ERR_NO_MEM; goto done; }
    if (lease->acquired) {
        if (!wifi_radio_lease_valid(lease) || lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX) goto done;
        wifi_radio_live_lease_t *owner = wifi_radio_promiscuous_owner(lease->identity);
        if (owner == NULL || (owner->required_mode != WIFI_MODE_AP && owner->required_mode != WIFI_MODE_APSTA) ||
            owner->raw_tx_identity != 0U) goto done;
        for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
            if (s_radio.leases[i].identity != 0U && s_radio.leases[i].identity != lease->identity) goto done;
        if (s_tx_rate_lease.identity != 0U) {
            if (reserved || s_tx_rate_lease.interface != WIFI_IF_AP ||
                s_tx_rate_lease.identity != lease->identity || s_tx_rate_lease.generation != lease->generation) goto done;
            err = wifi_radio_restore_tx_rate_locked(lease);
            if (err != ESP_OK) goto done;
        } else if (!reserved || s_radio.started || s_radio.stop_required) goto done;
        wifi_radio_release_locked(lease);
        if (lease->acquired) { err = ESP_ERR_INVALID_STATE; goto done; }
    } else if (!reserved || lease->identity != 0U || lease->generation != 0U) goto done;
    /* The mutex spans final owner release and new reservation. AP helper
     * retirement therefore never observes an unprotected driver generation. */
    if (!reserved) {
        err = wifi_radio_begin_lifecycle_locked(NULL, NULL, NULL, token);
        if (err != ESP_OK) goto done;
    }
    err = wifi_radio_stop_locked();
done:
    wifi_radio_operation_unlock();
    return err;
#endif
}
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
esp_err_t esp32_mquickjs_wifi_radio_raw_tx_acquire(
    esp32_mquickjs_wifi_raw_tx_interface_t interface, uint8_t channel,
    const wifi_tx_rate_config_t *rate,
    esp32_mquickjs_wifi_radio_lease_t *lease, uint8_t *actual_channel)
{
    if (lease == NULL || lease->acquired || lease->identity != 0U || lease->generation != 0U ||
        actual_channel == NULL || (interface != ESP32_MQUICKJS_WIFI_RAW_TX_STATION &&
        interface != ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT) ||
        (rate != NULL && !esp32_mquickjs_wifi_tx_rate_valid(rate))) return ESP_ERR_INVALID_ARG;
    wifi_mode_t required = interface == ESP32_MQUICKJS_WIFI_RAW_TX_STATION ? WIFI_MODE_STA : WIFI_MODE_AP;
    wifi_interface_t rate_interface = required == WIFI_MODE_STA ? WIFI_IF_STA : WIFI_IF_AP;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    uint8_t actual;
    wifi_second_chan_t secondary;
    uint32_t revision;
    if (s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U) goto done;
    if (rate != NULL) {
        /* Pre-start only. AP still requires an already running AP below, so it
         * cannot borrow a rate through this startup path. No implicit AP reset. */
        if (required != WIFI_MODE_STA) { err = ESP_ERR_NOT_SUPPORTED; goto done; }
        if (!s_radio.driver_owned || !s_radio.storage_configured || s_radio.started || s_radio.stop_required ||
            s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED || s_radio.restart_required ||
            s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.wake_locks != 0U ||
            s_radio.promiscuous_claimed) goto done;
        err = esp32_mquickjs_wifi_tx_rate_borrow_admission(&s_tx_rates, s_radio.generation,
            rate_interface, rate, &s_tx_rate_lease);
        if (err != ESP_OK) goto done;
        err = ESP_ERR_INVALID_STATE;
        for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
            if (s_radio.leases[i].identity != 0U) goto done;
    }
    /* AP startup is owned by the AP helper/configuration transaction. */
    if (required == WIFI_MODE_AP && (!s_radio.started || !(s_radio.effective_mode & WIFI_MODE_AP))) goto done;
    if (channel != 0U) {
        if (channel > 14U) {
#if CONFIG_SOC_WIFI_SUPPORT_5G
            if (esp32_mquickjs_wifi_radio_5ghz_channel_bit(channel) == 0U) { err = ESP_ERR_INVALID_ARG; goto done; }
#else
            err = ESP_ERR_INVALID_ARG; goto done;
#endif
        }
        if (s_radio.storage_configured) {
            err = wifi_radio_validate_regulatory_channel(channel);
            if (err != ESP_OK) goto done;
        }
    }
    err = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX, required, lease);
    if (err != ESP_OK) goto done;
    if (rate != NULL) {
        err = esp32_mquickjs_wifi_tx_rate_borrow(&s_tx_rates, lease->generation, lease->identity,
            rate_interface, rate, &s_tx_rate_lease, wifi_radio_write_tx_rate, NULL);
        if (err != ESP_OK) {
            if (s_tx_rate_lease.restore_pending)
                wifi_radio_cleanup_fault(s_tx_rate_restore_fault, s_tx_rate_lease.restore_error);
            goto done;
        }
    }
    err = wifi_radio_ensure_started_locked(lease);
    if (err != ESP_OK) goto done;
    err = wifi_radio_get_channel_locked(&actual, &secondary, &revision);
    if (err != ESP_OK) goto done;
    if (channel != 0U) {
        err = wifi_radio_set_channel_locked(lease, channel, secondary);
        if (err != ESP_OK) goto done;
        actual = channel;
    }
    *actual_channel = actual;
done:
    /* Retain even a partially acquired lease on failure. The native caller
     * owns release/stop cleanup and must not lose a failed-start diagnostic. */
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_raw_tx_policy(
    wifi_mode_t mode, const uint8_t *bytes,
    esp32_mquickjs_wifi_raw_tx_validation_policy_t *policy)
{
    esp_err_t err;
    uint8_t self[6];
    if (mode & WIFI_MODE_STA) {
        wifi_ap_record_t ap = {0};
        err = esp_wifi_sta_get_ap_info(&ap);
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) return err;
        if (err == ESP_OK) {
            policy->connection_active = true;
            if (policy->interface == ESP32_MQUICKJS_WIFI_RAW_TX_STATION) {
                err = esp_wifi_get_mac(WIFI_IF_STA, self);
                if (err != ESP_OK) return err;
                policy->associated_path = memcmp(bytes + 4, ap.bssid, 6) == 0 && memcmp(bytes + 10, self, 6) == 0;
            }
        }
    }
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (mode & WIFI_MODE_AP) {
        wifi_sta_list_t clients = {0};
        err = esp_wifi_ap_get_sta_list(&clients);
        if (err != ESP_OK) return err;
        if ((unsigned)clients.num > sizeof(clients.sta) / sizeof(clients.sta[0])) return ESP_ERR_INVALID_STATE;
        if (clients.num != 0) {
            policy->connection_active = true;
            if (policy->interface == ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT) {
                err = esp_wifi_get_mac(WIFI_IF_AP, self);
                if (err != ESP_OK) return err;
                if (memcmp(bytes + 10, self, 6) == 0) {
                    for (unsigned i = 0; i < (unsigned)clients.num; ++i)
                        if (memcmp(bytes + 4, clients.sta[i].mac, 6) == 0) policy->associated_path = true;
                }
            }
        }
    }
#else
    if (mode & WIFI_MODE_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    return ESP_OK;
}

static void wifi_radio_raw_tx_unpin(wifi_radio_live_lease_t *owner)
{
    taskENTER_CRITICAL(&s_radio.lock);
    owner->raw_tx_identity = 0;
    if (owner->raw_tx_channel_pinned) {
        owner->fixed_channel = false;
        owner->channel_conflict = false;
        owner->raw_tx_channel_pinned = false;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
}

esp_err_t esp32_mquickjs_wifi_radio_raw_tx_submit(
    const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_raw_tx_interface_t interface, bool driver_sequence,
    const uint8_t *bytes, size_t length, esp32_mquickjs_wifi_raw_tx_token_t *token,
    esp32_mquickjs_wifi_raw_tx_validation_t *validation, uint8_t *actual_channel)
{
    if (token == NULL || token->identity != 0U || token->generation != 0U || token->radio_lease_identity != 0U ||
        validation == NULL || actual_channel == NULL) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_raw_tx_validation_policy_t policy = {.interface = interface, .driver_sequence = driver_sequence};
    esp32_mquickjs_wifi_raw_tx_validated_frame_t frame;
    *validation = esp32_mquickjs_wifi_raw_tx_validate(bytes, length, &policy, &frame);
    if (*validation != ESP32_MQUICKJS_WIFI_RAW_TX_VALID) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    wifi_mode_t mode;
    uint8_t actual;
    wifi_second_chan_t secondary;
    uint32_t revision;
    if (!wifi_radio_lease_valid(lease) || lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX ||
        !s_radio.started || s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U) goto done;
    if (s_radio.fault_stage != NULL) { err = s_radio.fault_error; goto done; }
    wifi_radio_live_lease_t *owner = wifi_radio_promiscuous_owner(lease->identity);
    wifi_mode_t required = interface == ESP32_MQUICKJS_WIFI_RAW_TX_STATION ? WIFI_MODE_STA : WIFI_MODE_AP;
    if (owner == NULL || owner->required_mode != required) goto done;
    esp32_mquickjs_wifi_raw_tx_broker_status_t status;
    esp32_mquickjs_wifi_raw_tx_broker_status(&status);
    if (status.in_flight >= status.max_in_flight || status.registration_uncertain ||
        status.unregister_written || status.identity_exhausted) goto done;
    err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) goto done;
    if ((mode & required) != required || mode != s_radio.effective_mode) { err = ESP_ERR_INVALID_STATE; goto done; }
    err = wifi_radio_raw_tx_policy(mode, bytes, &policy);
    if (err != ESP_OK) goto done;
    *validation = esp32_mquickjs_wifi_raw_tx_validate(bytes, length, &policy, &frame);
    if (*validation != ESP32_MQUICKJS_WIFI_RAW_TX_VALID) { err = ESP_ERR_INVALID_ARG; goto done; }
    err = wifi_radio_get_channel_locked(&actual, &secondary, &revision);
    if (err != ESP_OK) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    bool conflict = owner->channel_conflict || (owner->fixed_channel &&
        (owner->primary_channel != actual || owner->secondary_channel != (uint8_t)secondary));
    taskEXIT_CRITICAL(&s_radio.lock);
    if (conflict) { err = ESP_ERR_INVALID_STATE; goto done; }
    err = esp32_mquickjs_wifi_raw_tx_broker_register(lease->generation);
    if (err != ESP_OK) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    if (owner->channel_conflict || s_radio.channel_observation_error != ESP_OK ||
        s_radio.primary_channel != actual || s_radio.secondary_channel != secondary) {
        taskEXIT_CRITICAL(&s_radio.lock);
        err = ESP_ERR_INVALID_STATE;
        goto done;
    }
    if (owner->raw_tx_identity == 0U) owner->raw_tx_channel_pinned = !owner->fixed_channel;
    owner->fixed_channel = true;
    owner->primary_channel = actual;
    owner->secondary_channel = (uint8_t)secondary;
    taskEXIT_CRITICAL(&s_radio.lock);
    err = esp32_mquickjs_wifi_raw_tx_broker_submit(lease->generation, lease->identity,
        bytes, length, &policy, token, validation);
    if (token->identity != 0U) {
        taskENTER_CRITICAL(&s_radio.lock);
        if (owner->raw_tx_identity == 0U) owner->raw_tx_identity = token->identity;
        taskEXIT_CRITICAL(&s_radio.lock);
        *actual_channel = actual;
    } else if (owner->raw_tx_identity == 0U) wifi_radio_raw_tx_unpin(owner);
done:
    wifi_radio_operation_unlock();
    return err;
}

bool esp32_mquickjs_wifi_radio_raw_tx_retire(
    const esp32_mquickjs_wifi_radio_lease_t *lease, esp32_mquickjs_wifi_raw_tx_token_t *token)
{
    if (token == NULL) return false;
    wifi_radio_operation_lock();
    bool retired = false;
    if (!wifi_radio_lease_valid(lease) || lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX ||
        token->generation != lease->generation || token->radio_lease_identity != lease->identity) goto done;
    wifi_radio_live_lease_t *owner = wifi_radio_promiscuous_owner(lease->identity);
    if (owner == NULL || owner->raw_tx_identity == 0U) goto done;
    if (wifi_radio_raw_tx_recovery_exact_locked(&s_radio.lifecycle) &&
        token->identity == s_raw_tx_recovery.operation.identity &&
        token->generation == s_raw_tx_recovery.operation.generation &&
        token->radio_lease_identity == s_raw_tx_recovery.operation.radio_lease_identity) {
        esp32_mquickjs_wifi_raw_tx_broker_status_t native;
        (void)esp32_mquickjs_wifi_raw_tx_broker_result(token, &native);
        if (!native.native_terminated) goto done;
    }
    retired = esp32_mquickjs_wifi_raw_tx_broker_retire(token);
    if (retired) {
        uint32_t remaining = esp32_mquickjs_wifi_raw_tx_broker_owner_identity(lease->identity);
        if (remaining == 0U) wifi_radio_raw_tx_unpin(owner);
        else {
            taskENTER_CRITICAL(&s_radio.lock);
            owner->raw_tx_identity = remaining;
            taskEXIT_CRITICAL(&s_radio.lock);
        }
    }
done:
    wifi_radio_operation_unlock();
    return retired;
}
static bool wifi_radio_raw_tx_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return token != NULL && token->identity != 0U && token->identity == s_raw_tx_recovery.lifecycle.identity &&
        token->generation == s_raw_tx_recovery.lifecycle.generation && token->identity == s_radio.lifecycle.identity &&
        token->generation == s_radio.lifecycle.generation;
}

static bool wifi_radio_raw_tx_recovery_owner_locked(const esp32_mquickjs_wifi_radio_lease_t *lease)
{
    if (!wifi_radio_raw_tx_recovery_exact_locked(&s_radio.lifecycle) || !wifi_radio_lease_valid(lease) ||
        lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX ||
        lease->identity != s_raw_tx_recovery.operation.radio_lease_identity ||
        lease->generation != s_raw_tx_recovery.operation.generation) return false;
    wifi_radio_live_lease_t *owner = wifi_radio_promiscuous_owner(lease->identity);
    if (owner == NULL) return false;
    /* Physical termination covers every record for this exact lease/generation.
     * Its representative identity may advance as a Session retires its window. */
    if (!s_radio.driver_owned) return s_raw_tx_recovery.stopped && s_raw_tx_recovery.sdk_fenced;
    if (owner->raw_tx_identity != s_raw_tx_recovery.operation.identity) return false;
    esp32_mquickjs_wifi_raw_tx_broker_status_t native;
    esp32_mquickjs_wifi_raw_tx_broker_status(&native);
    return owner->raw_tx_identity == s_raw_tx_recovery.operation.identity && native.operation_active &&
        native.submit_returned && !native.control_busy && !native.native_terminated &&
        native.token.generation == lease->generation && native.token.radio_lease_identity == lease->identity &&
        native.token.identity == s_raw_tx_recovery.operation.identity;
}

static uint32_t wifi_radio_raw_tx_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (!wifi_radio_raw_tx_recovery_exact_locked(token)) return 0U;
    const esp32_mquickjs_wifi_radio_lease_t owner = {.generation = s_raw_tx_recovery.operation.generation,
        .identity = s_raw_tx_recovery.operation.radio_lease_identity,
        .client = ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX, .acquired = true};
    return wifi_radio_raw_tx_recovery_owner_locked(&owner) ? owner.identity : 0U;
}

esp_err_t esp32_mquickjs_wifi_radio_checkpoint_raw_tx_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    const char *stage = "raw-tx-recovery-checkpoint-admission";
    if (wifi_radio_raw_tx_recovery_capture_owner_locked(token) == 0U ||
        !s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started || !s_radio.stop_required ||
        s_radio.stop_submitted || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        mode != s_radio.effective_mode) goto done;
    stage = "raw-tx-recovery-policy-snapshot";
    err = wifi_radio_policy_restart_prepare_locked(token, mode);
    if (err != ESP_OK) goto done;
    err = wifi_radio_restart_configs_capture_locked(token, mode);
    if (err == ESP_OK) (void)wifi_radio_restart_configs_record("raw-tx-recovery-checkpoint-complete", ESP_OK, false);
    wifi_radio_operation_unlock();
    return err;
done:
    (void)wifi_radio_restart_configs_record(stage, err, false);
    wifi_radio_operation_unlock();
    return err;
}

bool esp32_mquickjs_wifi_radio_raw_tx_recovery_active(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    bool active = wifi_radio_raw_tx_recovery_exact_locked(token);
    wifi_radio_operation_unlock();
    return active;
}

esp_err_t esp32_mquickjs_wifi_radio_begin_raw_tx_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_raw_tx_token_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode)
{
    if (operation == NULL || operation->generation == 0U || operation->identity == 0U ||
        operation->radio_lease_identity == 0U || token == NULL || token->identity != 0U ||
        token->generation != 0U || mode == NULL) return ESP_ERR_INVALID_ARG;
    *mode = WIFI_MODE_NULL;
#if !CONFIG_IDF_TARGET_ESP32C3 && !CONFIG_IDF_TARGET_ESP32S3 && !CONFIG_IDF_TARGET_ESP32C5
    return ESP_ERR_NOT_SUPPORTED;
#endif
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_raw_tx_broker_status_t native;
    esp32_mquickjs_wifi_raw_tx_broker_status(&native);
    if (native.token.identity != operation->identity || native.token.generation != operation->generation ||
        native.token.radio_lease_identity != operation->radio_lease_identity || native.generation != s_radio.generation ||
        !native.operation_active || !native.submit_returned || native.control_busy || native.native_terminated ||
        s_radio.lifecycle.identity != 0U || s_radio.operation.identity != 0U || !s_radio.driver_owned ||
        !s_radio.storage_configured || !s_radio.started || !s_radio.stop_required || s_radio.stop_submitted ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.restart_required ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.wake_locks != 0U ||
        s_radio.promiscuous_claimed || s_interval.owner.identity != 0U || s_interval.restore_pending ||
        s_vendor_ie.start_owner.identity != 0U || s_config_restart.snapshot != NULL ||
        s_policy_restart.owner.identity != 0U) goto done;
    wifi_radio_live_lease_t *original = wifi_radio_promiscuous_owner(operation->radio_lease_identity);
    if (original == NULL || original->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX ||
        original->raw_tx_identity != operation->identity || operation->generation != s_radio.generation) goto done;
    if (s_tx_rate_lease.restore_pending) goto done;
    if (s_tx_rate_lease.identity != 0U) {
        unsigned index = s_tx_rate_lease.interface == WIFI_IF_STA ? 0U : 1U;
        const esp32_mquickjs_wifi_tx_rate_record_t *rate = &s_tx_rates.records[index];
        if (s_tx_rate_lease.identity != original->identity || s_tx_rate_lease.generation != operation->generation ||
            !rate->known || rate->uncertain || rate->generation != operation->generation ||
            rate->write_identity != s_tx_rate_lease.write_identity ||
            !esp32_mquickjs_wifi_tx_rate_valid(&s_tx_rate_lease.previous)) goto done;
    }
    if (s_radio.effective_mode != WIFI_MODE_STA && s_radio.effective_mode != WIFI_MODE_AP &&
        s_radio.effective_mode != WIFI_MODE_APSTA) goto done;
    const esp32_mquickjs_wifi_radio_lease_t *owners[] = {application, station, access_point};
    const esp32_mquickjs_wifi_radio_client_t clients[] = {ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA, ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP};
    uint32_t identities[3] = {0};
    for (size_t i = 0; i < 3; ++i) {
        if (owners[i] == NULL || !owners[i]->acquired) continue;
        if (!wifi_radio_lease_valid(owners[i]) || owners[i]->client != clients[i]) goto done;
        identities[i] = owners[i]->identity;
    }
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (owner->identity == 0U) continue;
        if (owner->promiscuous_identity != 0U ||
            (owner->raw_tx_identity != 0U && owner->identity != original->identity) ||
            (owner->identity != original->identity && owner->identity != identities[0] &&
             owner->identity != identities[1] && owner->identity != identities[2])) goto done;
    }
    if (s_radio.next_lifecycle_identity == 0U) { err = ESP_ERR_NO_MEM; goto done; }
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.lifecycle = (esp32_mquickjs_wifi_radio_lifecycle_t){s_radio.generation, s_radio.next_lifecycle_identity++};
    s_raw_tx_recovery.lifecycle = s_radio.lifecycle;
    s_raw_tx_recovery.operation = *operation;
    s_raw_tx_recovery.stopped = s_raw_tx_recovery.sdk_fenced = false;
    *token = s_radio.lifecycle;
    *mode = s_radio.effective_mode;
    taskEXIT_CRITICAL(&s_radio.lock);
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_raw_tx_recovery_phase(const esp32_mquickjs_wifi_radio_lifecycle_t *token, bool shutdown)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (wifi_radio_raw_tx_recovery_exact_locked(token)) {
        /* Identity-only validation reference. It grants no release authority;
         * only the original Future/Session owns the mutable lease storage. */
        const esp32_mquickjs_wifi_radio_lease_t owner = {.generation = s_raw_tx_recovery.operation.generation,
            .identity = s_raw_tx_recovery.operation.radio_lease_identity,
            .client = ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX, .acquired = true};
        const esp32_mquickjs_wifi_radio_lease_t *allowed = wifi_radio_lease_valid(&owner) ? &owner : NULL;
        err = shutdown ? wifi_radio_shutdown_lease_locked(allowed, false) : wifi_radio_stop_lease_locked(allowed, false);
    }
    wifi_radio_operation_unlock();
    return err;
}
esp_err_t esp32_mquickjs_wifi_radio_stop_raw_tx_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return wifi_radio_raw_tx_recovery_phase(token, false);
}
esp_err_t esp32_mquickjs_wifi_radio_shutdown_raw_tx_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return wifi_radio_raw_tx_recovery_phase(token, true);
}
esp_err_t esp32_mquickjs_wifi_radio_check_stopped_raw_tx_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_raw_tx_recovery_exact_locked(token) || !s_raw_tx_recovery.stopped || s_radio.started ||
        s_radio.stop_required || s_radio.stop_submitted || s_radio.event_phase != RADIO_EVENTS_IDLE ||
        s_radio.wake_locks != 0U || s_radio.promiscuous_claimed || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED)) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U && s_radio.leases[i].identity != s_raw_tx_recovery.operation.radio_lease_identity)
            goto done;
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}
esp_err_t esp32_mquickjs_wifi_radio_finish_raw_tx_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_raw_tx_recovery_exact_locked(token) || !s_raw_tx_recovery.stopped || !s_raw_tx_recovery.sdk_fenced ||
        s_radio.driver_owned || s_radio.started || s_radio.stop_required || s_radio.stop_submitted ||
        s_radio.event_phase != RADIO_EVENTS_IDLE || s_radio.operation.identity != 0U || s_radio.restart_required ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_tx_rate_lease.identity != 0U) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    esp32_mquickjs_wifi_raw_tx_broker_status_t native;
    esp32_mquickjs_wifi_raw_tx_broker_status(&native);
    if (native.operation_active || native.native_terminated || native.token.identity != 0U) goto done;
    memset(&s_raw_tx_recovery, 0, sizeof(s_raw_tx_recovery));
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

/* All SDK functions below execute under the Radio mutation mutex. */
static void wifi_radio_action_event(int32_t id, const void *data)
{
    if (data == NULL) return;
    taskENTER_CRITICAL(&s_radio.lock);
    bool recorded = false;
    if (id == WIFI_EVENT_ACTION_TX_STATUS) {
        const wifi_event_action_tx_status_t *event = data;
        recorded = esp32_mquickjs_wifi_action_observe(&s_action.lane, ESP32_MQUICKJS_WIFI_ACTION_SEND,
            event->context, (uint8_t)event->ifx, event->channel, event->op_id, (unsigned)event->status);
    } else if (id == WIFI_EVENT_ROC_DONE) {
        const wifi_event_roc_done_t *event = data;
        recorded = esp32_mquickjs_wifi_action_observe(&s_action.lane, ESP32_MQUICKJS_WIFI_ACTION_ROC,
            event->context, 0, event->channel, event->op_id, (unsigned)event->status);
    }
    if (recorded) s_action.fence_posted = false;
    taskEXIT_CRITICAL(&s_radio.lock);
}

static bool wifi_radio_action_exact_locked(const esp32_mquickjs_wifi_action_token_t *token)
{
    return token != NULL && token->identity != 0U && token->identity == s_action.lane.identity &&
        token->generation == s_action.lane.generation && token->generation == s_radio.generation &&
        wifi_radio_lease_valid(&s_action.lease) &&
        s_action.lease.client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION &&
        s_radio.operation.lease_identity == s_action.lease.identity &&
        s_radio.operation.generation == token->generation && s_radio.operation.identity != 0U &&
        s_radio.operation.kind == (s_action.lane.kind == ESP32_MQUICKJS_WIFI_ACTION_SEND ?
            ESP32_MQUICKJS_WIFI_RADIO_OPERATION_ACTION : ESP32_MQUICKJS_WIFI_RADIO_OPERATION_ROC);
}

esp_err_t esp32_mquickjs_wifi_radio_begin_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_recovery_request_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode)
{
    if (operation == NULL) return ESP_ERR_INVALID_ARG;
    switch (operation->kind) {
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    case ESP32_MQUICKJS_WIFI_RECOVERY_TWT:
        return wifi_radio_twt_recovery_begin(application, station, access_point, operation, token, mode);
#endif
    case ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX: {
        esp32_mquickjs_wifi_raw_tx_broker_status_t native;
        esp32_mquickjs_wifi_raw_tx_broker_status(&native);
        if (native.token.identity != operation->identity || native.token.generation != operation->generation)
            return ESP_ERR_INVALID_STATE;
        /* The caller names the broker operation, not a release authority. The
         * native begin revalidates this complete token under the Radio mutex. */
        return esp32_mquickjs_wifi_radio_begin_raw_tx_recovery(application, station, access_point, &native.token, token, mode);
    }
    case ESP32_MQUICKJS_WIFI_RECOVERY_ACTION: {
        const esp32_mquickjs_wifi_action_token_t identity = {operation->generation, operation->identity};
        return esp32_mquickjs_wifi_radio_begin_action_recovery(application, station, access_point, &identity, token, mode);
    }
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    case ESP32_MQUICKJS_WIFI_RECOVERY_FTM: {
        const esp32_mquickjs_wifi_ftm_token_t identity = {operation->generation, operation->identity};
        return esp32_mquickjs_wifi_radio_begin_ftm_recovery(application, station, access_point, &identity, token, mode);
    }
#endif
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t esp32_mquickjs_wifi_radio_prepare_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (wifi_radio_twt_recovery_active(token)) return wifi_radio_twt_recovery_prepare(token);
#endif
    return esp32_mquickjs_wifi_radio_recovery_active(token) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool esp32_mquickjs_wifi_radio_recovery_active(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (wifi_radio_twt_recovery_active(token)) return true;
#endif
    if (esp32_mquickjs_wifi_radio_raw_tx_recovery_active(token)) return true;
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (esp32_mquickjs_wifi_radio_ftm_recovery_active(token)) return true;
#endif
    return esp32_mquickjs_wifi_radio_action_recovery_active(token);
}

esp_err_t esp32_mquickjs_wifi_radio_checkpoint_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (wifi_radio_twt_recovery_active(token)) return wifi_radio_twt_recovery_checkpoint(token, mode);
#endif
    if (esp32_mquickjs_wifi_radio_raw_tx_recovery_active(token))
        return esp32_mquickjs_wifi_radio_checkpoint_raw_tx_recovery(token, mode);
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (esp32_mquickjs_wifi_radio_ftm_recovery_active(token))
        return esp32_mquickjs_wifi_radio_checkpoint_ftm_recovery(token, mode);
#endif
    return esp32_mquickjs_wifi_radio_checkpoint_action_recovery(token, mode);
}

esp_err_t esp32_mquickjs_wifi_radio_stop_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (wifi_radio_twt_recovery_active(token)) return wifi_radio_twt_recovery_phase(token, false);
#endif
    if (esp32_mquickjs_wifi_radio_raw_tx_recovery_active(token))
        return esp32_mquickjs_wifi_radio_stop_raw_tx_recovery(token);
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (esp32_mquickjs_wifi_radio_ftm_recovery_active(token))
        return esp32_mquickjs_wifi_radio_stop_ftm_recovery(token);
#endif
    return esp32_mquickjs_wifi_radio_stop_action_recovery(token);
}

esp_err_t esp32_mquickjs_wifi_radio_check_stopped_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (wifi_radio_twt_recovery_active(token)) return wifi_radio_twt_recovery_stopped(token);
#endif
    if (esp32_mquickjs_wifi_radio_raw_tx_recovery_active(token))
        return esp32_mquickjs_wifi_radio_check_stopped_raw_tx_recovery(token);
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (esp32_mquickjs_wifi_radio_ftm_recovery_active(token))
        return esp32_mquickjs_wifi_radio_check_stopped_ftm_recovery(token);
#endif
    return esp32_mquickjs_wifi_radio_check_stopped_action_recovery(token);
}

esp_err_t esp32_mquickjs_wifi_radio_shutdown_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (wifi_radio_twt_recovery_active(token)) return wifi_radio_twt_recovery_phase(token, true);
#endif
    if (esp32_mquickjs_wifi_radio_raw_tx_recovery_active(token))
        return esp32_mquickjs_wifi_radio_shutdown_raw_tx_recovery(token);
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (esp32_mquickjs_wifi_radio_ftm_recovery_active(token))
        return esp32_mquickjs_wifi_radio_shutdown_ftm_recovery(token);
#endif
    return esp32_mquickjs_wifi_radio_shutdown_action_recovery(token);
}

esp_err_t esp32_mquickjs_wifi_radio_finish_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (wifi_radio_twt_recovery_active(token)) return wifi_radio_twt_recovery_finish(token);
#endif
    if (esp32_mquickjs_wifi_radio_raw_tx_recovery_active(token))
        return esp32_mquickjs_wifi_radio_finish_raw_tx_recovery(token);
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (esp32_mquickjs_wifi_radio_ftm_recovery_active(token))
        return esp32_mquickjs_wifi_radio_finish_ftm_recovery(token);
#endif
    return esp32_mquickjs_wifi_radio_finish_action_recovery(token);
}

static bool wifi_radio_action_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return token != NULL && token->identity != 0U &&
        token->identity == s_action.recovery.identity && token->generation == s_action.recovery.generation &&
        token->identity == s_radio.lifecycle.identity && token->generation == s_radio.lifecycle.generation;
}

bool esp32_mquickjs_wifi_radio_action_recovery_active(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    bool active = wifi_radio_action_recovery_exact_locked(token);
    wifi_radio_operation_unlock();
    return active;
}

esp_err_t esp32_mquickjs_wifi_radio_check_stopped_action_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_action_recovery_exact_locked(token) || s_radio.started || s_radio.stop_required ||
        s_radio.stop_submitted || s_radio.event_phase != RADIO_EVENTS_IDLE || s_radio.wake_locks != 0U ||
        s_radio.promiscuous_claimed || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED)) goto done;
    uint32_t allowed = 0;
    if (s_action.lease.acquired || s_radio.operation.identity != 0U) {
        esp32_mquickjs_wifi_action_token_t operation = {s_action.lane.generation, s_action.lane.identity};
        if (!wifi_radio_action_exact_locked(&operation) || !s_action.lane.submitted ||
            s_action.lane.dispatching || s_action.lane.cancel_busy) goto done;
        allowed = s_action.lease.identity;
    }
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U && s_radio.leases[i].identity != allowed) goto done;
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

static uint32_t wifi_radio_action_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    esp32_mquickjs_wifi_action_token_t operation = {s_action.lane.generation, s_action.lane.identity};
    return wifi_radio_action_recovery_exact_locked(token) && wifi_radio_action_exact_locked(&operation) &&
        s_action.lane.submitted && !s_action.lane.dispatching && !s_action.lane.cancel_busy &&
        !s_action.lane.physical_termination ? s_action.lease.identity : 0U;
}

esp_err_t esp32_mquickjs_wifi_radio_checkpoint_action_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    const char *stage = "recovery-checkpoint-admission";
    if (!wifi_radio_action_recovery_exact_locked(token) || token->generation != s_radio.generation ||
        !s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started || !s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.fault_stage != NULL ||
        s_radio.cleanup_stage != NULL || mode != s_radio.effective_mode) goto done;
    stage = "recovery-policy-snapshot";
    err = wifi_radio_policy_restart_prepare_locked(token, mode);
    if (err != ESP_OK) goto done;
    /* Capture owns its exact diagnostic and wipes failed secret allocations.
     * No STOP/START or owner publication occurs in this checkpoint phase. */
    err = wifi_radio_restart_configs_capture_locked(token, mode);
    if (err == ESP_OK) (void)wifi_radio_restart_configs_record("recovery-checkpoint-complete", ESP_OK, false);
    wifi_radio_operation_unlock();
    return err;
done:
    (void)wifi_radio_restart_configs_record(stage, err, false);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_begin_action_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_action_token_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode)
{
    if (token == NULL || token->identity != 0U || token->generation != 0U || mode == NULL ||
        operation == NULL || operation->identity == 0U || operation->generation == 0U) return ESP_ERR_INVALID_ARG;
    *mode = WIFI_MODE_NULL;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_action_exact_locked(operation) || !s_action.lane.submitted ||
        s_action.lane.dispatching || s_action.lane.cancel_busy || s_action.lane.physical_termination ||
        s_radio.lifecycle.identity != 0U ||
        !s_radio.driver_owned || s_radio.restart_required || s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending ||
        s_interval.owner.identity != 0U || s_interval.restore_pending || s_vendor_ie.start_owner.identity != 0U ||
        s_config_restart.snapshot != NULL || s_policy_restart.owner.identity != 0U) goto done;
    if (s_radio.effective_mode != WIFI_MODE_STA && s_radio.effective_mode != WIFI_MODE_AP &&
        s_radio.effective_mode != WIFI_MODE_APSTA) goto done;
    const esp32_mquickjs_wifi_radio_lease_t *owners[3] = {application, station, access_point};
    const esp32_mquickjs_wifi_radio_client_t clients[3] = {
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,
    };
    uint32_t identities[3] = {0};
    for (size_t i = 0; i < 3; ++i) {
        if (owners[i] == NULL || !owners[i]->acquired) continue;
        if (!wifi_radio_lease_valid(owners[i]) || owners[i]->client != clients[i]) goto done;
        identities[i] = owners[i]->identity;
    }
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (owner->identity == 0U) continue;
        if (owner->promiscuous_identity != 0U || owner->raw_tx_identity != 0U ||
            (owner->identity != s_action.lease.identity && owner->identity != identities[0] &&
             owner->identity != identities[1] && owner->identity != identities[2])) goto done;
    }
    if (s_radio.next_lifecycle_identity == 0U) { err = ESP_ERR_NO_MEM; goto done; }
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.lifecycle = (esp32_mquickjs_wifi_radio_lifecycle_t){s_radio.generation, s_radio.next_lifecycle_identity++};
    s_action.recovery = s_radio.lifecycle;
    *token = s_radio.lifecycle;
    *mode = s_radio.effective_mode;
    taskEXIT_CRITICAL(&s_radio.lock);
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

/* Caller retires managed Wi-Fi leases and helper resources between these two
 * phases. The Action/ROC owner is never handed to the coordinator for release. */
static esp_err_t wifi_radio_action_recovery_phase(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, bool shutdown)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (wifi_radio_action_recovery_exact_locked(token)) {
        const esp32_mquickjs_wifi_radio_lease_t *allowed = s_action.lease.acquired ? &s_action.lease : NULL;
        err = shutdown ? wifi_radio_shutdown_lease_locked(allowed, false) : wifi_radio_stop_lease_locked(allowed, false);
    }
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_stop_action_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return wifi_radio_action_recovery_phase(token, false);
}

esp_err_t esp32_mquickjs_wifi_radio_shutdown_action_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return wifi_radio_action_recovery_phase(token, true);
}

esp_err_t esp32_mquickjs_wifi_radio_finish_action_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_action_recovery_exact_locked(token) || s_radio.driver_owned ||
        s_radio.started || s_radio.stop_required || s_radio.stop_submitted ||
        s_radio.event_phase != RADIO_EVENTS_IDLE || s_radio.operation.identity != 0U ||
        s_action.lease.acquired || s_radio.wake_locks != 0U || s_radio.promiscuous_claimed ||
        s_radio.restart_required || s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    /* Preserve s_radio.lifecycle and both frozen configuration records. */
    memset(&s_action.recovery, 0, sizeof(s_action.recovery));
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

static bool wifi_radio_action_parameters(wifi_interface_t interface, uint8_t channel,
    wifi_second_chan_t secondary, uint32_t duration)
{
    return (interface == WIFI_IF_STA || interface == WIFI_IF_AP) && channel != 0U &&
        (secondary == WIFI_SECOND_CHAN_NONE || secondary == WIFI_SECOND_CHAN_ABOVE || secondary == WIFI_SECOND_CHAN_BELOW) &&
        duration >= 1U && duration <= 60000U;
}

static esp_err_t wifi_radio_action_admit_locked(esp32_mquickjs_wifi_action_kind_t kind,
    wifi_interface_t interface, uint8_t channel, wifi_second_chan_t secondary,
    bool allow_broadcast, esp32_mquickjs_wifi_action_token_t *token)
{
    if (!s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.lifecycle.identity != 0U ||
        s_radio.operation.identity != 0U || s_radio.restart_required || s_radio.cleanup_stage != NULL ||
        s_action.lane.identity != 0U) return ESP_ERR_INVALID_STATE;
    if (s_radio.fault_stage != NULL) return s_radio.fault_error;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    if (s_radio.next_operation_identity == 0U) return ESP_ERR_NO_MEM;
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) return err;
    wifi_mode_t required = interface == WIFI_IF_STA ? WIFI_MODE_STA : WIFI_MODE_AP;
    if (mode != s_radio.effective_mode || (mode & required) != required ||
        (allow_broadcast && mode == WIFI_MODE_APSTA)) return ESP_ERR_INVALID_STATE;
    err = wifi_radio_validate_regulatory_channel(channel);
    if (err != ESP_OK) return err;
    if (secondary != WIFI_SECOND_CHAN_NONE) {
        int adjacent = channel + (secondary == WIFI_SECOND_CHAN_ABOVE ? 4 : -4);
        if (adjacent < 1 || adjacent > 177 || channel == 14 || adjacent == 14 ||
            ((channel <= 14) != (adjacent <= 14))) return ESP_ERR_INVALID_ARG;
        err = wifi_radio_validate_regulatory_channel((uint8_t)adjacent);
        if (err != ESP_OK) return err;
    }
    uint8_t current;
    wifi_second_chan_t current_secondary;
    uint32_t generation;
    err = wifi_radio_get_channel_locked(&current, &current_secondary, &generation);
    if (err != ESP_OK) return err;
    bool off_channel = current != channel || current_secondary != secondary;
    if (off_channel) {
        /* Default v1 never moves an active AP or associated Station away from
         * home. Disconnected STA may use native off-channel scheduling only
         * when no other RF session depends on the current channel. */
        if (mode & WIFI_MODE_AP) return ESP_ERR_INVALID_STATE;
        wifi_ap_record_t ap;
        err = esp_wifi_sta_get_ap_info(&ap);
        if (err == ESP_OK) return ESP_ERR_INVALID_STATE;
        if (err != ESP_ERR_WIFI_NOT_CONNECT) return err;
    }
    bool conflict = false;
    taskENTER_CRITICAL(&s_radio.lock);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (owner->identity == 0U) continue;
        if (owner->raw_tx_identity != 0U || owner->channel_conflict ||
            (owner->fixed_channel && (owner->primary_channel != channel || owner->secondary_channel != (uint8_t)secondary)))
            conflict = true;
        if (off_channel && owner->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION &&
            owner->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA &&
            owner->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE) conflict = true;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (conflict) return ESP_ERR_INVALID_STATE;
    err = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION, required, &s_action.lease);
    if (err != ESP_OK) return err;
    taskENTER_CRITICAL(&s_radio.lock);
    err = esp32_mquickjs_wifi_action_reserve(&s_action.lane, s_radio.generation, kind,
        (uint32_t)(uintptr_t)esp32_mquickjs_wifi_action_receive, (uint8_t)interface, channel, token);
    if (err == ESP_OK) {
        s_action.secondary = secondary;
        s_action.fence_posted = false;
        s_action.posted_revision = 0;
        s_radio.operation = (esp32_mquickjs_wifi_radio_operation_t){
            .generation = s_radio.generation, .lease_identity = s_action.lease.identity,
            .identity = s_radio.next_operation_identity++,
            .kind = kind == ESP32_MQUICKJS_WIFI_ACTION_SEND ? ESP32_MQUICKJS_WIFI_RADIO_OPERATION_ACTION : ESP32_MQUICKJS_WIFI_RADIO_OPERATION_ROC,
        };
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (err != ESP_OK) wifi_radio_release_locked(&s_action.lease);
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_action_send(wifi_action_tx_req_t *request,
    size_t request_bytes, esp32_mquickjs_wifi_action_token_t *token)
{
    if (request == NULL || request_bytes < sizeof(*request) || token == NULL || token->identity != 0U || token->generation != 0U ||
        !wifi_radio_action_parameters(request->ifx, request->channel, request->sec_channel, request->wait_time_ms) ||
        request->type != WIFI_OFFCHAN_TX_REQ || request->rx_cb != esp32_mquickjs_wifi_action_receive || request->op_id != 0U ||
        request->data_len == 0U || request->data_len > 1476U || request_bytes - sizeof(*request) != request->data_len)
        return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_action_admit_locked(ESP32_MQUICKJS_WIFI_ACTION_SEND,
        request->ifx, request->channel, request->sec_channel, false, token);
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        bool begun = esp32_mquickjs_wifi_action_begin_submit(&s_action.lane, token);
        taskEXIT_CRITICAL(&s_radio.lock);
        if (!begun) err = ESP_ERR_INVALID_STATE;
        else {
            err = esp_wifi_action_tx_req(request);
            taskENTER_CRITICAL(&s_radio.lock);
            (void)esp32_mquickjs_wifi_action_submitted(&s_action.lane, token, err, request->op_id);
            taskEXIT_CRITICAL(&s_radio.lock);
        }
    }
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_action_roc(wifi_roc_req_t *request,
    esp32_mquickjs_wifi_action_token_t *token)
{
    if (request == NULL || token == NULL || token->identity != 0U || token->generation != 0U ||
        !wifi_radio_action_parameters(request->ifx, request->channel, request->sec_channel, request->wait_time_ms) ||
        request->type != WIFI_ROC_REQ || request->rx_cb != esp32_mquickjs_wifi_action_receive || request->done_cb != NULL || request->op_id != 0U)
        return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_action_admit_locked(ESP32_MQUICKJS_WIFI_ACTION_ROC,
        request->ifx, request->channel, request->sec_channel, request->allow_broadcast, token);
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        bool begun = esp32_mquickjs_wifi_action_begin_submit(&s_action.lane, token);
        taskEXIT_CRITICAL(&s_radio.lock);
        if (!begun) err = ESP_ERR_INVALID_STATE;
        else {
            err = esp_wifi_remain_on_channel(request);
            taskENTER_CRITICAL(&s_radio.lock);
            (void)esp32_mquickjs_wifi_action_submitted(&s_action.lane, token, err, request->op_id);
            taskEXIT_CRITICAL(&s_radio.lock);
        }
    }
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_action_cancel(const esp32_mquickjs_wifi_action_token_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_action_exact_locked(token)) goto done;
    esp32_mquickjs_wifi_action_lane_t state;
    taskENTER_CRITICAL(&s_radio.lock);
    (void)esp32_mquickjs_wifi_action_request_cancel(&s_action.lane, token);
    bool submit = esp32_mquickjs_wifi_action_begin_cancel(&s_action.lane, token);
    state = s_action.lane;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (!submit) { err = state.terminal || state.sdk_quiescent || state.physical_termination || state.cancel_written || !state.submitted ? ESP_OK : ESP_ERR_INVALID_STATE; goto done; }
    if (state.kind == ESP32_MQUICKJS_WIFI_ACTION_SEND) {
        wifi_action_tx_req_t request = {.ifx = (wifi_interface_t)state.interface, .type = WIFI_OFFCHAN_TX_CANCEL,
            .channel = state.channel, .sec_channel = s_action.secondary, .op_id = state.operation_id,
            .rx_cb = esp32_mquickjs_wifi_action_receive};
        err = esp_wifi_action_tx_req(&request);
    } else {
        wifi_roc_req_t request = {.ifx = (wifi_interface_t)state.interface, .type = WIFI_ROC_CANCEL,
            .channel = state.channel, .sec_channel = s_action.secondary, .op_id = state.operation_id,
            .rx_cb = esp32_mquickjs_wifi_action_receive};
        err = esp_wifi_remain_on_channel(&request);
    }
    taskENTER_CRITICAL(&s_radio.lock);
    (void)esp32_mquickjs_wifi_action_cancelled(&s_action.lane, token, err);
    taskEXIT_CRITICAL(&s_radio.lock);
done:
    wifi_radio_operation_unlock();
    return err;
}

void esp32_mquickjs_wifi_radio_action_snapshot(esp32_mquickjs_wifi_action_lane_t *out)
{
    if (out == NULL) return;
    taskENTER_CRITICAL(&s_radio.lock);
    *out = s_action.lane;
    taskEXIT_CRITICAL(&s_radio.lock);
}

bool esp32_mquickjs_wifi_radio_action_status(const esp32_mquickjs_wifi_action_token_t *token,
    esp32_mquickjs_wifi_action_lane_t *out)
{
    if (out == NULL) return false;
    taskENTER_CRITICAL(&s_radio.lock);
    bool ok = token != NULL && token->identity != 0U && s_action.lane.identity == token->identity &&
        s_action.lane.generation == token->generation;
    if (ok) *out = s_action.lane;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ok;
}

esp_err_t esp32_mquickjs_wifi_radio_action_retire(esp32_mquickjs_wifi_action_token_t *token,
    esp32_mquickjs_wifi_action_lane_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_action_exact_locked(token)) goto done;
    uint32_t revision = 0;
    taskENTER_CRITICAL(&s_radio.lock);
    bool ready = esp32_mquickjs_wifi_action_fence_revision(&s_action.lane, token, &revision);
    *out = s_action.lane;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (!ready && out->submitted && !out->physical_termination) {
        /* Missing or ambiguous event status is not a guessed terminal. The
         * reviewed SDK task can independently attest complete native retirement;
         * retain owner until the following exact default-loop marker drains. */
        revision = out->revision;
        err = esp32_mquickjs_wifi_action_sdk_quiescent();
        if (err != ESP_OK) goto done;
        taskENTER_CRITICAL(&s_radio.lock);
        ready = esp32_mquickjs_wifi_action_quiescent(&s_action.lane, token, revision);
        *out = s_action.lane;
        taskEXIT_CRITICAL(&s_radio.lock);
        if (!ready) { err = ESP_ERR_TIMEOUT; goto done; }
    }
    if (out->submitted && !out->physical_termination && !out->sdk_fenced) {
        err = esp32_mquickjs_wifi_action_sdk_fence();
        if (err != ESP_OK) goto done;
        taskENTER_CRITICAL(&s_radio.lock);
        ready = esp32_mquickjs_wifi_action_sdk_fenced(&s_action.lane, token, revision);
        taskEXIT_CRITICAL(&s_radio.lock);
        if (!ready) { err = ESP_ERR_TIMEOUT; goto done; }
    }
    taskENTER_CRITICAL(&s_radio.lock);
    *out = s_action.lane;
    bool post = out->submitted && !out->physical_termination && out->sdk_fenced && !out->event_fenced && !s_action.fence_posted;
    wifi_radio_action_fence_t fence = {.token = *token, .revision = out->revision};
    if (post) { s_action.fence_posted = true; s_action.posted_revision = fence.revision; }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (post) {
        err = esp_event_post(ESP32QJS_WIFI_RADIO_CONTROL_EVENT, 2, &fence, sizeof(fence), 0);
        if (err != ESP_OK) {
            taskENTER_CRITICAL(&s_radio.lock);
            if (s_action.posted_revision == fence.revision) s_action.fence_posted = false;
            taskEXIT_CRITICAL(&s_radio.lock);
            goto done;
        }
    }
    taskENTER_CRITICAL(&s_radio.lock);
    *out = s_action.lane;
    bool released = esp32_mquickjs_wifi_action_release(&s_action.lane, token);
    if (released) {
        memset(&s_radio.operation, 0, sizeof(s_radio.operation));
        s_action.fence_posted = false;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (released) { wifi_radio_release_locked(&s_action.lease); err = ESP_OK; }
    else err = ESP_ERR_TIMEOUT;
done:
    wifi_radio_operation_unlock();
    return err;
}

#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
esp_err_t esp32_mquickjs_wifi_radio_twt_control(
    const esp32_mquickjs_wifi_radio_lease_t *application, const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    esp32_mquickjs_wifi_twt_control_kind_t kind, esp32_mquickjs_wifi_twt_control_t *value,
    uint32_t *generation, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "twt-control-admission", .error = ESP_ERR_INVALID_ARG};
    if (!value || !generation || (unsigned)kind > ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET ||
        (kind == ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET && value->offset_us > 102400)) return ESP_ERR_INVALID_ARG;
    *generation = 0;
    bool write = kind == ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG || kind == ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET;
    bool associated = kind == ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET || kind == ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned) { error = ESP_ERR_WIFI_NOT_INIT; goto done; }
    if (!s_radio.storage_configured || s_radio.lifecycle.identity || s_radio.operation.identity ||
        s_radio.restart_required || s_radio.fault_stage || s_radio.cleanup_stage || s_twt_policy.pending ||
        !(s_radio.effective_mode & WIFI_MODE_STA) ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED)) goto done;
    if (associated && !s_radio.started) { error = ESP_ERR_WIFI_NOT_STARTED; goto done; }
    if (write) {
        /* Shared TWT policy may affect existing managed Agreements. Validate
         * helper identities, then admit only their leases and exact TWT owners. */
        error = wifi_radio_connection_owner_locked(application, station, access_point, WIFI_IF_STA, true);
        if (error != ESP_OK) goto done;
        if (s_radio.wake_locks || s_radio.promiscuous_claimed || s_tx_rate_lease.identity) {
            error = ESP_ERR_INVALID_STATE; goto done;
        }
        for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
            const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
            if (!owner->identity || (application && application->acquired && owner->identity == application->identity) ||
                (station && station->acquired && owner->identity == station->identity) ||
                (access_point && access_point->acquired && owner->identity == access_point->identity)) continue;
            esp32_mquickjs_wifi_radio_lease_t lease = {s_radio.generation, owner->identity, owner->client, true};
            if (!wifi_radio_lease_valid(&lease) ||
                (!wifi_radio_twt_individual_lease_retained(&lease) && !wifi_radio_twt_broadcast_lease_retained(&lease))) {
                error = ESP_ERR_INVALID_STATE; goto done;
            }
        }
    }
    result->stage = kind == ESP32_MQUICKJS_WIFI_TWT_READ_CONFIG ? "twt-policy-read" :
        kind == ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG ? "twt-policy-write" :
        kind == ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS ? "twt-flows-read" : "twt-offset-write";
    result->mutation_attempted = write;
    error = esp32_mquickjs_wifi_twt_sdk_control(kind, value);
    if (error == ESP_OK) {
        *generation = s_radio.generation;
        if (kind == ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG) {
            s_twt_policy.value = value->config; s_twt_policy.known = true;
        }
        result->stage = "complete";
    } else if (write && error != ESP_ERR_WIFI_NOT_ASSOC && error != ESP_ERR_INVALID_ARG) {
        /* No blind rollback of shared policy or an association's scalar. */
        (void)wifi_radio_record_fault(result->stage, error);
    }
done:
    result->error = error;
    if (write) {
        taskENTER_CRITICAL(&s_radio.lock); s_radio.configuration = *result; taskEXIT_CRITICAL(&s_radio.lock);
    }
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_twt_broadcast_snapshot(
    esp32_mquickjs_wifi_twt_broadcast_snapshot_t *output, uint32_t *generation)
{
    if (output == NULL || generation == NULL) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.lifecycle.identity != 0U ||
        s_radio.restart_required || s_radio.cleanup_stage != NULL) goto done;
    if (s_radio.fault_stage != NULL) { error = s_radio.fault_error; goto done; }
    wifi_mode_t mode;
    error = esp_wifi_get_mode(&mode);
    if (error != ESP_OK) goto done;
    if (mode != s_radio.effective_mode || !(mode & WIFI_MODE_STA)) { error = ESP_ERR_INVALID_STATE; goto done; }
    error = esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(output);
    if (error == ESP_OK) *generation = s_radio.generation;
done:
    wifi_radio_operation_unlock();
    return error;
}
static wifi_radio_twt_individual_t *wifi_radio_twt_individual_find(const esp32_mquickjs_wifi_twt_token_t *token)
{
    if (token == NULL || token->identity == 0U || token->generation == 0U || s_twt_individual == NULL) return NULL;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL; ++i) {
        wifi_radio_twt_individual_t *owner = &s_twt_individual[i];
        if (owner->token.identity == token->identity && owner->token.generation == token->generation) return owner;
    }
    return NULL;
}
esp_err_t esp32_mquickjs_wifi_radio_twt_individual_submit(
    const esp32_mquickjs_wifi_itwt_options_t *options, esp32_mquickjs_wifi_twt_token_t *token)
{
    if (token == NULL || token->identity != 0U || token->generation != 0U ||
        !esp32_mquickjs_wifi_itwt_options_valid(options)) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.lifecycle.identity != 0U ||
        s_radio.operation.identity != 0U || s_radio.restart_required || s_radio.cleanup_stage != NULL) goto done;
    if (s_radio.fault_stage != NULL) { error = s_radio.fault_error; goto done; }
    if (s_twt_identity.next_identity == 0U) { error = ESP_ERR_NO_MEM; goto done; }
    wifi_mode_t mode;
    error = esp_wifi_get_mode(&mode);
    if (error != ESP_OK) goto done;
    if (mode != s_radio.effective_mode || !(mode & WIFI_MODE_STA)) { error = ESP_ERR_INVALID_STATE; goto done; }
    wifi_ap_record_t ap;
    error = esp_wifi_sta_get_ap_info(&ap);
    if (error != ESP_OK) goto done;
    uint8_t channel;
    wifi_second_chan_t secondary;
    uint32_t generation;
    error = wifi_radio_get_channel_locked(&channel, &secondary, &generation);
    if (error != ESP_OK) goto done;
    bool conflict = false;
    taskENTER_CRITICAL(&s_radio.lock);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *live = &s_radio.leases[i];
        if (live->identity != 0U && (live->raw_tx_identity != 0U || live->channel_conflict ||
            (live->fixed_channel && (live->primary_channel != channel || live->secondary_channel != (uint8_t)secondary))))
            conflict = true;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (conflict) { error = ESP_ERR_INVALID_STATE; goto done; }
    /* Foreign native calls consume the same request-ID prefix. Resynchronise
     * before reserving; the native result ledger still rechecks on submit. */
    esp32_mquickjs_wifi_twt_setup_results_snapshot_t results;
    esp32_mquickjs_wifi_twt_setup_results_snapshot(&results);
    if (results.fault != ESP_OK) { error = results.fault; goto done; }
    uint32_t next = (uint32_t)(results.last_request_id + 1);
    if (next < s_twt_identity.next_connection_id) next = s_twt_identity.next_connection_id;
    if (next > 32767U) { error = ESP_ERR_NO_MEM; goto done; }
    uint32_t request_id = options->connection_id_set ? options->config.twt_id : next;
    if (request_id < next) { error = ESP_ERR_INVALID_STATE; goto done; }
    if (s_twt_individual == NULL) {
        wifi_radio_twt_individual_t *fresh = esp32_mquickjs_memory_wireless_calloc("wifi.radio", ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
        if (fresh == NULL) { error = ESP_ERR_NO_MEM; goto done; }
        taskENTER_CRITICAL(&s_radio.lock);
        s_twt_individual = fresh;
        taskEXIT_CRITICAL(&s_radio.lock);
    }
    wifi_radio_twt_individual_t *owner = NULL;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL; ++i)
        if (s_twt_individual[i].token.identity == 0U) { owner = &s_twt_individual[i]; break; }
    if (owner == NULL) { error = ESP_ERR_NO_MEM; goto done; }
    error = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT, WIFI_MODE_STA, &owner->lease);
    if (error != ESP_OK) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    uint64_t identity = s_twt_identity.next_identity;
    s_twt_identity.next_identity = identity == UINT64_MAX ? 0U : identity + 1U;
    s_twt_identity.next_connection_id = request_id + 1U;
    owner->token = (esp32_mquickjs_wifi_twt_token_t){identity, s_radio.generation};
    owner->requested = options->config;
    owner->requested.twt_id = (uint16_t)request_id;
    owner->dispatching = true;
    wifi_radio_live_lease_t *live = wifi_radio_promiscuous_owner(owner->lease.identity);
    live->fixed_channel = true;
    live->primary_channel = channel;
    live->secondary_channel = (uint8_t)secondary;
    *token = owner->token;
    taskEXIT_CRITICAL(&s_radio.lock);
    esp32_mquickjs_wifi_twt_setup_dispatch_t dispatch = {0};
    error = esp32_mquickjs_wifi_twt_sdk_individual_submit(&owner->requested, &dispatch);
    taskENTER_CRITICAL(&s_radio.lock);
    owner->dispatch = dispatch;
    owner->submit_error = error;
    owner->dispatching = false;
    taskEXIT_CRITICAL(&s_radio.lock);
done:
    wifi_radio_operation_unlock();
    return error;
}
bool esp32_mquickjs_wifi_radio_twt_individual_status(const esp32_mquickjs_wifi_twt_token_t *token,
    esp32_mquickjs_wifi_twt_individual_radio_state_t *output)
{
    if (output == NULL) return false;
    taskENTER_CRITICAL(&s_radio.lock);
    wifi_radio_twt_individual_t *owner = wifi_radio_twt_individual_find(token);
    esp32_mquickjs_wifi_twt_individual_radio_state_t state = {0};
    if (owner != NULL) {
        state.token = owner->token;
        state.requested = owner->requested;
        state.dispatch = owner->dispatch;
        state.submit_error = owner->submit_error;
        state.cleanup_error = owner->cleanup_error;
        state.cleanup_stage = owner->cleanup_stage;
        state.dispatching = owner->dispatching;
        state.closing = owner->closing;
        state.result_released = owner->result_released;
        state.native_retired = owner->native_retired;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (owner == NULL) return false;
    if (state.dispatch.identity != 0U && !state.result_released && !state.native_retired &&
        esp32_mquickjs_wifi_twt_setup_result_read(state.dispatch.identity, &state.native) != ESP_OK) return false;
    taskENTER_CRITICAL(&s_radio.lock);
    bool exact = wifi_radio_twt_individual_find(token) == owner &&
        owner->dispatch.identity == state.dispatch.identity && owner->native_retired == state.native_retired &&
        owner->result_released == state.result_released && owner->closing == state.closing && owner->dispatching == state.dispatching;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (exact) *output = state;
    return exact;
}
unsigned esp32_mquickjs_wifi_radio_twt_individual_tokens(esp32_mquickjs_wifi_twt_token_t *tokens,
    unsigned capacity, bool closing_only)
{
    if (tokens == NULL || capacity == 0U) return 0;
    unsigned count = 0;
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_twt_individual != NULL)
        for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL && count < capacity; ++i) {
            const wifi_radio_twt_individual_t *owner = &s_twt_individual[i];
            if (owner->token.identity != 0U && (!closing_only || owner->closing)) tokens[count++] = owner->token;
        }
    taskEXIT_CRITICAL(&s_radio.lock);
    return count;
}
bool esp32_mquickjs_wifi_radio_twt_individual_request_close(const esp32_mquickjs_wifi_twt_token_t *token)
{
    taskENTER_CRITICAL(&s_radio.lock);
    wifi_radio_twt_individual_t *owner = wifi_radio_twt_individual_find(token);
    if (owner != NULL) owner->closing = true;
    uint32_t identity = owner != NULL ? owner->dispatch.identity : 0;
    taskEXIT_CRITICAL(&s_radio.lock);
    esp32_mquickjs_wifi_twt_setup_result_request_close(identity);
    return owner != NULL;
}
static esp_err_t wifi_radio_twt_individual_retire(wifi_radio_twt_individual_t *owner)
{
    if (owner->dispatch.identity == 0U) return ESP_OK; /* No native request was accepted. */
    /* Also covers a close requested while submit was still assigning identity. */
    esp32_mquickjs_wifi_twt_setup_result_request_close(owner->dispatch.identity);
    owner->retirement.stage = "individual-information-retire";
    esp_err_t information_error = esp32_mquickjs_wifi_twt_sdk_information_reap(owner->dispatch.identity);
    if (information_error != ESP_OK) return information_error;
    if (owner->retirement.result_released)
        return esp32_mquickjs_wifi_twt_setup_retire_poll(&owner->retirement, &owner->token, owner->dispatch.identity);
    esp32_mquickjs_wifi_twt_setup_result_t result;
    owner->retirement.stage = "individual-result-read";
    esp_err_t error = esp32_mquickjs_wifi_twt_setup_result_read(owner->dispatch.identity, &result);
    if (error != ESP_OK) return error;
    if (result.flags & (ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING | ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING))
        return ESP_ERR_NOT_FINISHED;
    if (!(result.flags & (ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED | ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED))) {
        owner->retirement.stage = "individual-native-state";
        esp32_mquickjs_wifi_twt_sdk_snapshot_t native;
        error = esp32_mquickjs_wifi_twt_sdk_snapshot(&native);
        if (error != ESP_OK) return error;
        unsigned flow = 8U;
        for (unsigned i = 0; i < 8U; ++i) {
            if (native.individual_ids[i] != result.request_id) continue;
            if (flow != 8U) return ESP_ERR_INVALID_STATE;
            flow = i;
        }
        if (flow != 8U) {
            owner->retirement.stage = "individual-teardown-submit";
            error = esp32_mquickjs_wifi_twt_sdk_setup_teardown(owner->dispatch.identity, (uint8_t)flow);
            if (error != ESP_OK) return error;
        }
    }
    /* Reads current flags again: an early completion or error can precede
     * submission return. The existing coordinator owns cancellation, native
     * and event fences, result release, and a failed TX-release suffix. */
    return esp32_mquickjs_wifi_twt_setup_retire_poll(&owner->retirement, &owner->token, owner->dispatch.identity);
}
esp_err_t esp32_mquickjs_wifi_radio_twt_individual_close(esp32_mquickjs_wifi_twt_token_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    wifi_radio_twt_individual_t *owner = wifi_radio_twt_individual_find(token);
    if (owner == NULL || token->generation != s_radio.generation || owner->dispatching ||
        !wifi_radio_lease_valid(&owner->lease)) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    owner->closing = true;
    taskEXIT_CRITICAL(&s_radio.lock);
    error = owner->native_retired ? ESP_OK : wifi_radio_twt_individual_retire(owner);
    taskENTER_CRITICAL(&s_radio.lock);
    owner->cleanup_error = error;
    owner->cleanup_stage = owner->retirement.stage;
    owner->result_released = owner->retirement.result_released;
    if (error == ESP_OK) owner->native_retired = true;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (error != ESP_OK) goto done;
    wifi_radio_release_locked(&owner->lease);
    taskENTER_CRITICAL(&s_radio.lock);
    if (owner->lease.acquired) {
        error = owner->cleanup_error = ESP_ERR_INVALID_STATE;
        owner->cleanup_stage = "individual-lease-release";
    } else {
        memset(owner, 0, sizeof(*owner));
        *token = (esp32_mquickjs_wifi_twt_token_t){0};
    }
    taskEXIT_CRITICAL(&s_radio.lock);
done:
    wifi_radio_operation_unlock();
    return error;
}

static esp_err_t wifi_radio_twt_individual_information(const esp32_mquickjs_wifi_twt_token_t *token,
    uint32_t duration_ms, bool resume, uint32_t *identity)
{
    if (identity == NULL || duration_ms > ESP32_MQUICKJS_WIFI_TWT_SUSPEND_MAX_MS) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    taskENTER_CRITICAL(&s_radio.lock);
    wifi_radio_twt_individual_t *owner = wifi_radio_twt_individual_find(token);
    bool valid = owner != NULL && token->generation == s_radio.generation && !owner->dispatching &&
        !owner->closing && !owner->native_retired && !owner->result_released &&
        owner->submit_error == ESP_OK && owner->dispatch.identity != 0U && wifi_radio_lease_valid(&owner->lease);
    uint32_t setup_identity = valid ? owner->dispatch.identity : 0;
    taskEXIT_CRITICAL(&s_radio.lock);
    esp_err_t error = valid ? esp32_mquickjs_wifi_twt_sdk_information_submit(setup_identity, duration_ms, resume, identity)
        : ESP_ERR_INVALID_STATE;
    wifi_radio_operation_unlock();
    return error;
}
esp_err_t esp32_mquickjs_wifi_radio_twt_individual_suspend(const esp32_mquickjs_wifi_twt_token_t *token,
    uint32_t duration_ms, uint32_t *identity)
{ return wifi_radio_twt_individual_information(token, duration_ms, false, identity); }
esp_err_t esp32_mquickjs_wifi_radio_twt_individual_resume(const esp32_mquickjs_wifi_twt_token_t *token, uint32_t *identity)
{ return wifi_radio_twt_individual_information(token, 0, true, identity); }


static wifi_radio_twt_broadcast_t *wifi_radio_twt_broadcast_find(const esp32_mquickjs_wifi_twt_token_t *token)
{
    if (token == NULL || !token->identity || !token->generation || s_twt_broadcast == NULL) return NULL;
    for (unsigned i = 1; i < 32; ++i) {
        wifi_radio_twt_broadcast_t *owner = s_twt_broadcast[i];
        if (owner != NULL && owner->token.identity == token->identity && owner->token.generation == token->generation) return owner;
    }
    return NULL;
}
esp_err_t esp32_mquickjs_wifi_radio_twt_broadcast_submit(const esp32_mquickjs_wifi_btwt_options_t *options,
    esp32_mquickjs_wifi_twt_token_t *token)
{
    if (token == NULL || token->identity || token->generation || !esp32_mquickjs_wifi_btwt_options_valid(options)) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.lifecycle.identity != 0U ||
        s_radio.operation.identity != 0U || s_radio.restart_required || s_radio.cleanup_stage != NULL) goto done;
    if (s_radio.fault_stage != NULL) { error = s_radio.fault_error; goto done; }
    if (s_twt_identity.next_identity == 0U) { error = ESP_ERR_NO_MEM; goto done; }
    wifi_mode_t mode;
    error = esp_wifi_get_mode(&mode);
    if (error != ESP_OK) goto done;
    if (mode != s_radio.effective_mode || !(mode & WIFI_MODE_STA)) { error = ESP_ERR_INVALID_STATE; goto done; }
    wifi_ap_record_t ap;
    error = esp_wifi_sta_get_ap_info(&ap);
    if (error != ESP_OK) goto done;
    uint8_t channel;
    wifi_second_chan_t secondary;
    uint32_t generation;
    error = wifi_radio_get_channel_locked(&channel, &secondary, &generation);
    if (error != ESP_OK) goto done;
    bool conflict = false;
    taskENTER_CRITICAL(&s_radio.lock);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *live = &s_radio.leases[i];
        if (live->identity != 0U && (live->raw_tx_identity != 0U || live->channel_conflict ||
            (live->fixed_channel && (live->primary_channel != channel || live->secondary_channel != (uint8_t)secondary))))
            conflict = true;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (conflict) { error = ESP_ERR_INVALID_STATE; goto done; }
    unsigned slot = options->config.btwt_id;
    if (s_twt_broadcast != NULL && s_twt_broadcast[slot] != NULL) { error = ESP_ERR_INVALID_STATE; goto done; }
    if (!esp32_mquickjs_wifi_twt_tx_broadcast_remaining(slot)) { error = ESP_ERR_NO_MEM; goto done; }
    if (s_twt_broadcast == NULL) {
        wifi_radio_twt_broadcast_t **fresh = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 32, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
        if (fresh == NULL) { error = ESP_ERR_NO_MEM; goto done; }
        taskENTER_CRITICAL(&s_radio.lock);s_twt_broadcast = fresh;taskEXIT_CRITICAL(&s_radio.lock);
    }
    wifi_radio_twt_broadcast_t *owner = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*owner), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (owner == NULL) { error = ESP_ERR_NO_MEM; goto done; }
    error = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT, WIFI_MODE_STA, &owner->lease);
    if (error != ESP_OK) { esp32_mquickjs_memory_payload_free(owner); goto done; }
    taskENTER_CRITICAL(&s_radio.lock);
    uint64_t identity = s_twt_identity.next_identity;
    s_twt_identity.next_identity = identity == UINT64_MAX ? 0 : identity + 1;
    owner->token = (esp32_mquickjs_wifi_twt_token_t){identity, s_radio.generation};
    owner->requested = options->config;
    owner->dispatching = true;
    s_twt_broadcast[slot] = owner;
    wifi_radio_live_lease_t *live = wifi_radio_promiscuous_owner(owner->lease.identity);
    live->fixed_channel = true;live->primary_channel = channel;live->secondary_channel = (uint8_t)secondary;
    *token = owner->token;
    taskEXIT_CRITICAL(&s_radio.lock);
    esp32_mquickjs_wifi_btwt_dispatch_t dispatch = {0};
    error = esp32_mquickjs_wifi_twt_sdk_broadcast_submit(&owner->requested, &dispatch);
    taskENTER_CRITICAL(&s_radio.lock);
    owner->dispatch = dispatch;owner->submit_error = error;owner->dispatching = false;
    taskEXIT_CRITICAL(&s_radio.lock);
done:
    wifi_radio_operation_unlock();return error;
}
bool esp32_mquickjs_wifi_radio_twt_broadcast_status(const esp32_mquickjs_wifi_twt_token_t *token,
    esp32_mquickjs_wifi_twt_broadcast_radio_state_t *output)
{
    if (output == NULL) return false;
    esp32_mquickjs_wifi_twt_broadcast_radio_state_t state = {0};
    taskENTER_CRITICAL(&s_radio.lock);
    wifi_radio_twt_broadcast_t *owner = wifi_radio_twt_broadcast_find(token);
    bool available = owner != NULL && !owner->working;
    if (available) {
        state.token = owner->token;state.requested = owner->requested;state.dispatch = owner->dispatch;
        state.teardown = owner->retirement.teardown;
        state.submit_error = owner->submit_error;state.teardown_error = owner->teardown_error;
        state.cleanup_error = owner->cleanup_error;state.cleanup_stage = owner->cleanup_stage;
        state.dispatching = owner->dispatching;state.closing = owner->closing;
        state.result_released = owner->retirement.released;state.native_retired = owner->native_retired;
        state.teardown_attempted = owner->teardown_attempted;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (!available) return false;
    if (state.dispatch.identity && !state.result_released &&
        !esp32_mquickjs_wifi_btwt_setup_result(state.requested.btwt_id, state.dispatch.identity, &state.native)) return false;
    state.dialog_attempts_remaining = esp32_mquickjs_wifi_twt_tx_broadcast_remaining(state.requested.btwt_id);
    taskENTER_CRITICAL(&s_radio.lock);
    bool exact = wifi_radio_twt_broadcast_find(token) == owner && !owner->working &&
        owner->dispatching == state.dispatching && owner->dispatch.identity == state.dispatch.identity &&
        owner->retirement.released == state.result_released && owner->native_retired == state.native_retired && owner->closing == state.closing;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (exact) *output = state;
    return exact;
}
unsigned esp32_mquickjs_wifi_radio_twt_broadcast_tokens(esp32_mquickjs_wifi_twt_token_t *tokens,unsigned capacity,bool closing_only)
{
    if (tokens == NULL || !capacity) return 0;
    unsigned count = 0;
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_twt_broadcast != NULL)
        for (unsigned i = 1; i < 32 && count < capacity; ++i) {
            const wifi_radio_twt_broadcast_t *owner = s_twt_broadcast[i];
            if (owner != NULL && (!closing_only || owner->closing)) tokens[count++] = owner->token;
        }
    taskEXIT_CRITICAL(&s_radio.lock);return count;
}
bool esp32_mquickjs_wifi_radio_twt_broadcast_request_close(const esp32_mquickjs_wifi_twt_token_t *token)
{
    taskENTER_CRITICAL(&s_radio.lock);
    wifi_radio_twt_broadcast_t *owner = wifi_radio_twt_broadcast_find(token);
    if (owner != NULL) owner->closing = true;
    taskEXIT_CRITICAL(&s_radio.lock);return owner != NULL;
}
static esp_err_t wifi_radio_twt_broadcast_retire(wifi_radio_twt_broadcast_t *owner)
{
    unsigned slot = owner->requested.btwt_id;
    if (owner->dispatch.handoff_error || (owner->dispatch.native_entered && !owner->dispatch.native_completed)) {
        owner->retirement.stage = "broadcast-dispatch-unconfirmed";
        return owner->dispatch.handoff_error ? owner->dispatch.handoff_error : ESP_ERR_INVALID_STATE;
    }
    if (!owner->dispatch.identity) return ESP_OK;
    if (owner->retirement.released)
        return esp32_mquickjs_wifi_btwt_retire_poll(&owner->retirement,&owner->token,slot,owner->dispatch.identity);
    esp32_mquickjs_wifi_btwt_timer_result_t result;
    owner->retirement.stage = "broadcast-result-read";
    if (!esp32_mquickjs_wifi_btwt_setup_result(slot,owner->dispatch.identity,&result)) return ESP_ERR_INVALID_STATE;
    if (result.tx_busy || result.publishing) return ESP_ERR_NOT_FINISHED;
    if (!owner->teardown_attempted && !result.native_closed) {
        owner->retirement.stage = "broadcast-native-state";
        esp32_mquickjs_wifi_twt_sdk_snapshot_t native;
        esp_err_t error = esp32_mquickjs_wifi_twt_sdk_snapshot(&native);
        if (error != ESP_OK) return error;
        if (native.broadcast_id_bitmap & (UINT32_C(1) << slot)) {
            owner->retirement.stage = "broadcast-teardown-submit";
            error = esp32_mquickjs_wifi_twt_sdk_broadcast_teardown(slot,owner->dispatch.identity);
            esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t teardown;
            esp32_mquickjs_wifi_twt_teardown_tx_snapshot(&teardown);
            if (teardown.broadcast && teardown.identity == owner->dispatch.identity && teardown.flow == slot) {
                owner->teardown_attempted = true;owner->retirement.teardown = teardown;
            }
            owner->teardown_error = error;
            if (error != ESP_OK) return error;
        }
    }
    return esp32_mquickjs_wifi_btwt_retire_poll(&owner->retirement,&owner->token,slot,owner->dispatch.identity);
}
void esp32_mquickjs_wifi_radio_twt_close_agreements(esp32_mquickjs_wifi_twt_close_group_t *group)
{
    if (group == NULL) return;
    *group = (esp32_mquickjs_wifi_twt_close_group_t){0};
    uint32_t identities[ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL] = {0};
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_twt_individual != NULL)
        for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL; ++i) {
            wifi_radio_twt_individual_t *owner = &s_twt_individual[i];
            if (!owner->token.identity) continue;
            owner->closing = true;
            identities[group->individual_count] = owner->dispatch.identity;
            group->individual[group->individual_count++] = owner->token;
        }
    if (s_twt_broadcast != NULL)
        for (unsigned i = 1; i < 32; ++i) {
            wifi_radio_twt_broadcast_t *owner = s_twt_broadcast[i];
            if (owner == NULL || !owner->token.identity) continue;
            owner->closing = true;
            group->broadcast[group->broadcast_count++] = owner->token;
        }
    taskEXIT_CRITICAL(&s_radio.lock);
    for (unsigned i = 0; i < group->individual_count; ++i)
        esp32_mquickjs_wifi_twt_setup_result_request_close(identities[i]);
}
unsigned esp32_mquickjs_wifi_radio_twt_close_pending(const esp32_mquickjs_wifi_twt_close_group_t *group)
{
    if (group == NULL) return 0;
    unsigned pending = 0;
    taskENTER_CRITICAL(&s_radio.lock);
    for (unsigned i = 0; i < group->individual_count; ++i)
        if (wifi_radio_twt_individual_find(&group->individual[i]) != NULL) ++pending;
    for (unsigned i = 0; i < group->broadcast_count; ++i)
        if (wifi_radio_twt_broadcast_find(&group->broadcast[i]) != NULL) ++pending;
    taskEXIT_CRITICAL(&s_radio.lock);
    return pending;
}
esp_err_t esp32_mquickjs_wifi_radio_twt_broadcast_close(esp32_mquickjs_wifi_twt_token_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    wifi_radio_twt_broadcast_t *owner = wifi_radio_twt_broadcast_find(token);
    if (owner == NULL || token->generation != s_radio.generation || owner->dispatching || owner->working ||
        !wifi_radio_lease_valid(&owner->lease)) goto done;
    taskENTER_CRITICAL(&s_radio.lock);owner->closing = true;owner->working = true;taskEXIT_CRITICAL(&s_radio.lock);
    error = owner->native_retired ? ESP_OK : wifi_radio_twt_broadcast_retire(owner);
    taskENTER_CRITICAL(&s_radio.lock);
    owner->cleanup_error = error;owner->cleanup_stage = owner->retirement.stage;
    if (error == ESP_OK) owner->native_retired = true;
    owner->working = false;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (error != ESP_OK) goto done;
    wifi_radio_release_locked(&owner->lease);
    taskENTER_CRITICAL(&s_radio.lock);
    if (owner->lease.acquired) {
        error = owner->cleanup_error = ESP_ERR_INVALID_STATE;owner->cleanup_stage = "broadcast-lease-release";
    } else {
        s_twt_broadcast[owner->requested.btwt_id] = NULL;
        *token = (esp32_mquickjs_wifi_twt_token_t){0};
    }
    bool freed = !owner->lease.acquired;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (freed) esp32_mquickjs_memory_payload_free(owner);
done:
    wifi_radio_operation_unlock();return error;
}

static bool wifi_radio_twt_probe_exact_locked(const esp32_mquickjs_wifi_twt_token_t *token)
{
    return token != NULL && token->identity != 0U && token->identity == s_twt_probe.state.token.identity &&
        token->generation == s_twt_probe.state.token.generation && token->generation == s_radio.generation &&
        wifi_radio_lease_valid(&s_twt_probe.lease) && s_twt_probe.lease.client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT &&
        s_radio.operation.identity == s_twt_probe.operation_identity && s_radio.operation.generation == token->generation &&
        s_radio.operation.lease_identity == s_twt_probe.lease.identity &&
        s_radio.operation.kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_TWT_PROBE;
}
esp_err_t esp32_mquickjs_wifi_radio_twt_probe_submit(uint32_t response_ms, esp32_mquickjs_wifi_twt_token_t *token)
{
    if (token == NULL || token->identity != 0U || token->generation != 0U || response_ms == 0U ||
        response_ms > ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.lifecycle.identity != 0U ||
        s_radio.operation.identity != 0U || s_radio.restart_required || s_radio.cleanup_stage != NULL ||
        s_twt_probe.state.token.identity != 0U) goto done;
    if (s_radio.fault_stage != NULL) { error = s_radio.fault_error; goto done; }
    if (s_radio.next_operation_identity == 0U || s_twt_identity.next_identity == 0U) { error = ESP_ERR_NO_MEM; goto done; }
    wifi_mode_t mode;
    error = esp_wifi_get_mode(&mode);
    if (error != ESP_OK) goto done;
    if (mode != s_radio.effective_mode || !(mode & WIFI_MODE_STA)) { error = ESP_ERR_INVALID_STATE; goto done; }
    wifi_ap_record_t ap;
    error = esp_wifi_sta_get_ap_info(&ap);
    if (error != ESP_OK) goto done;
    uint8_t channel;
    wifi_second_chan_t secondary;
    uint32_t generation;
    error = wifi_radio_get_channel_locked(&channel, &secondary, &generation);
    if (error != ESP_OK) goto done;
    bool conflict = false;
    taskENTER_CRITICAL(&s_radio.lock);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (owner->identity != 0U && (owner->raw_tx_identity != 0U || owner->channel_conflict ||
            (owner->fixed_channel && (owner->primary_channel != channel || owner->secondary_channel != (uint8_t)secondary))))
            conflict = true;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (conflict) { error = ESP_ERR_INVALID_STATE; goto done; }
    error = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT, WIFI_MODE_STA, &s_twt_probe.lease);
    if (error != ESP_OK) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    uint64_t identity = s_twt_identity.next_identity;
    s_twt_identity.next_identity = identity == UINT64_MAX ? 0U : identity + 1U;
    s_twt_probe.state.token = (esp32_mquickjs_wifi_twt_token_t){identity, s_radio.generation};
    s_twt_probe.state.dispatching = true;
    s_twt_probe.operation_identity = s_radio.next_operation_identity++;
    s_radio.operation = (esp32_mquickjs_wifi_radio_operation_t){.generation = s_radio.generation,
        .lease_identity = s_twt_probe.lease.identity, .identity = s_twt_probe.operation_identity,
        .kind = ESP32_MQUICKJS_WIFI_RADIO_OPERATION_TWT_PROBE};
    wifi_radio_live_lease_t *owner = wifi_radio_promiscuous_owner(s_twt_probe.lease.identity);
    owner->fixed_channel = true;
    owner->primary_channel = channel;
    owner->secondary_channel = (uint8_t)secondary;
    *token = s_twt_probe.state.token;
    taskEXIT_CRITICAL(&s_radio.lock);
    uint32_t native_identity = 0;
    error = esp32_mquickjs_wifi_twt_sdk_probe_submit(response_ms, &native_identity);
    taskENTER_CRITICAL(&s_radio.lock);
    s_twt_probe.state.native_identity = native_identity;
    s_twt_probe.state.submit_error = error;
    s_twt_probe.state.dispatching = false;
    taskEXIT_CRITICAL(&s_radio.lock);
done:
    wifi_radio_operation_unlock();
    return error;
}
bool esp32_mquickjs_wifi_radio_twt_probe_status(const esp32_mquickjs_wifi_twt_token_t *token,
    esp32_mquickjs_wifi_twt_probe_radio_state_t *output)
{
    if (output == NULL) return false;
    taskENTER_CRITICAL(&s_radio.lock);
    esp32_mquickjs_wifi_twt_probe_radio_state_t result = {
        .token = s_twt_probe.state.token, .native_identity = s_twt_probe.state.native_identity,
        .submit_error = s_twt_probe.state.submit_error, .cleanup_error = s_twt_probe.state.cleanup_error,
        .cleanup_stage = s_twt_probe.state.cleanup_stage, .dispatching = s_twt_probe.state.dispatching,
        .cleanup_pending = s_twt_probe.state.cleanup_pending};
    bool exact = token == NULL || (token->identity != 0U && token->identity == result.token.identity &&
        token->generation == result.token.generation && token->generation == s_radio.generation);
    taskEXIT_CRITICAL(&s_radio.lock);
    if (!exact) return false;
    if (result.native_identity != 0U) {
        esp32_mquickjs_wifi_twt_probe_result_snapshot(&result.native);
        if (result.native.identity != result.native_identity) return false;
        esp32_mquickjs_wifi_twt_probe_timer_snapshot_t timer;
        esp32_mquickjs_wifi_twt_probe_wake_snapshot_t wake;
        esp32_mquickjs_wifi_twt_probe_timer_snapshot(&timer);
        esp32_mquickjs_wifi_twt_probe_wake_snapshot(&wake);
        result.native_error = result.native.control_error != ESP_OK ? result.native.control_error :
            timer.fault != ESP_OK ? timer.fault : wake.fault != ESP_OK ? wake.fault : result.native.fault;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    exact = result.token.identity == s_twt_probe.state.token.identity &&
        result.token.generation == s_twt_probe.state.token.generation && result.native_identity == s_twt_probe.state.native_identity;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (exact) *output = result;
    return exact;
}
void esp32_mquickjs_wifi_radio_twt_probe_snapshot(esp32_mquickjs_wifi_twt_probe_radio_state_t *output)
{
    if (output == NULL) return;
    if (!esp32_mquickjs_wifi_radio_twt_probe_status(NULL, output)) *output = (esp32_mquickjs_wifi_twt_probe_radio_state_t){0};
}
static bool wifi_radio_twt_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return token != NULL && token->identity != 0U && token->identity == s_twt_recovery.lifecycle.identity &&
        token->generation == s_twt_recovery.lifecycle.generation && token->identity == s_radio.lifecycle.identity &&
        token->generation == s_radio.lifecycle.generation;
}
static bool wifi_radio_twt_managed_lease_locked(uint32_t identity)
{
    const esp32_mquickjs_wifi_radio_lease_t *lease = NULL;
    if (s_twt_probe.state.token.identity && s_twt_probe.lease.identity == identity && !s_twt_probe.state.dispatching)
        lease = &s_twt_probe.lease;
    if (s_twt_individual != NULL)
        for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL; ++i) {
            const wifi_radio_twt_individual_t *owner = &s_twt_individual[i];
            if (owner->token.identity && owner->lease.identity == identity && !owner->dispatching)
                lease = &owner->lease;
        }
    if (s_twt_broadcast != NULL)
        for (unsigned i = 1; i < 32; ++i) {
            const wifi_radio_twt_broadcast_t *owner = s_twt_broadcast[i];
            if (owner != NULL && owner->token.identity && owner->lease.identity == identity && !owner->dispatching)
                lease = &owner->lease;
        }
    return lease != NULL && lease->client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT && wifi_radio_lease_valid(lease);
}
static bool wifi_radio_twt_recovery_owners_locked(void)
{
    if (!wifi_radio_twt_recovery_exact_locked(&s_radio.lifecycle)) return false;
    if (s_radio.operation.identity != 0U &&
        (!wifi_radio_twt_probe_exact_locked(&s_twt_probe.state.token) || s_twt_probe.state.dispatching)) return false;
    for (unsigned i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (!owner->identity) continue;
        if (owner->promiscuous_identity || owner->raw_tx_identity || !wifi_radio_twt_managed_lease_locked(owner->identity))
            return false;
    }
    return true;
}
static bool wifi_radio_twt_recovery_active(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    bool active = wifi_radio_twt_recovery_exact_locked(token);
    wifi_radio_operation_unlock();
    return active;
}
static esp_err_t wifi_radio_twt_recovery_begin(const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station, const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_recovery_request_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode)
{
    if (token == NULL || token->identity || token->generation || mode == NULL || operation == NULL ||
        operation->kind != ESP32_MQUICKJS_WIFI_RECOVERY_TWT || operation->identity || !operation->generation)
        return ESP_ERR_INVALID_ARG;
    *mode = WIFI_MODE_NULL;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (operation->generation != s_radio.generation || s_radio.lifecycle.identity ||
        !s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started || !s_radio.stop_required ||
        s_radio.stop_submitted || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL || s_radio.restart_required ||
        s_radio.wake_locks || s_radio.promiscuous_claimed || WIFI_RADIO_EAP_PENDING ||
        s_tx_rate_lease.identity || s_tx_rate_lease.restore_pending || s_interval.owner.identity ||
        s_interval.restore_pending || s_vendor_ie.start_owner.identity || s_config_restart.snapshot != NULL ||
        s_policy_restart.owner.identity || !(s_radio.effective_mode & WIFI_MODE_STA)) goto done;
    if (s_radio.operation.identity &&
        (!wifi_radio_twt_probe_exact_locked(&s_twt_probe.state.token) || s_twt_probe.state.dispatching)) goto done;
    const esp32_mquickjs_wifi_radio_lease_t *helpers[3] = {application, station, access_point};
    const esp32_mquickjs_wifi_radio_client_t clients[3] = {ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA, ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP};
    uint32_t identities[3] = {0};
    for (unsigned i = 0; i < 3; ++i) {
        if (helpers[i] == NULL || !helpers[i]->acquired) continue;
        if (!wifi_radio_lease_valid(helpers[i]) || helpers[i]->client != clients[i]) goto done;
        identities[i] = helpers[i]->identity;
    }
    unsigned managed = 0;
    for (unsigned i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (!owner->identity) continue;
        if (owner->promiscuous_identity || owner->raw_tx_identity) goto done;
        if (wifi_radio_twt_managed_lease_locked(owner->identity)) { ++managed; continue; }
        if (owner->identity != identities[0] && owner->identity != identities[1] && owner->identity != identities[2]) goto done;
    }
    if (!managed) goto done;
    if (!s_radio.next_lifecycle_identity) { error = ESP_ERR_NO_MEM; goto done; }
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.lifecycle = (esp32_mquickjs_wifi_radio_lifecycle_t){s_radio.generation, s_radio.next_lifecycle_identity++};
    s_twt_recovery.lifecycle = s_radio.lifecycle;
    s_twt_recovery.prepared = s_twt_recovery.stopped = false;
    if (s_twt_probe.state.token.identity) s_twt_probe.state.cleanup_pending = true;
    if (s_twt_individual != NULL)
        for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL; ++i)
            if (s_twt_individual[i].token.identity) s_twt_individual[i].closing = true;
    if (s_twt_broadcast != NULL)
        for (unsigned i = 1; i < 32; ++i)
            if (s_twt_broadcast[i] != NULL) s_twt_broadcast[i]->closing = true;
    *token = s_radio.lifecycle;
    *mode = s_radio.effective_mode;
    taskEXIT_CRITICAL(&s_radio.lock);
    /* No SDK call and no nested locks. Operation mutex excludes submission;
     * later admission is blocked by lifecycle, so this entire set is fixed. */
    if (s_twt_individual != NULL)
        for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL; ++i)
            if (s_twt_individual[i].token.identity)
                esp32_mquickjs_wifi_twt_setup_result_request_close(s_twt_individual[i].dispatch.identity);
    error = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return error;
}
static esp_err_t wifi_radio_twt_recovery_prepare(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_twt_recovery_exact_locked(token)) goto done;
    if (s_twt_recovery.prepared) { error = ESP_OK; goto done; }
    /* Probe PM ownership must be revoked while its current association still
     * exists. This acknowledges cancellation only, not TX/timer/event drain. */
    error = s_twt_probe.state.native_identity
        ? esp32_mquickjs_wifi_twt_sdk_probe_cancel(s_twt_probe.state.native_identity) : ESP_OK;
    if (error == ESP_OK) s_twt_recovery.prepared = true;
    else {
        taskENTER_CRITICAL(&s_radio.lock);
        s_twt_probe.state.cleanup_error = error;
        s_twt_probe.state.cleanup_stage = "probe-recovery-cancel";
        taskEXIT_CRITICAL(&s_radio.lock);
    }
done:
    wifi_radio_operation_unlock();
    return error;
}
static esp_err_t wifi_radio_twt_recovery_checkpoint(const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    const char *stage = "twt-recovery-checkpoint-admission";
    if (!wifi_radio_twt_recovery_exact_locked(token) || !s_twt_recovery.prepared ||
        token->generation != s_radio.generation || !s_radio.driver_owned || !s_radio.storage_configured ||
        !s_radio.started || !s_radio.stop_required || s_radio.stop_submitted ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.fault_stage != NULL ||
        s_radio.cleanup_stage != NULL || s_radio.restart_required || mode != s_radio.effective_mode) goto done;
    stage = "twt-recovery-policy-snapshot";
    error = wifi_radio_policy_restart_prepare_locked(token, mode);
    if (error != ESP_OK) goto done;
    error = wifi_radio_restart_configs_capture_locked(token, mode);
    if (error == ESP_OK) (void)wifi_radio_restart_configs_record("twt-recovery-checkpoint-complete", ESP_OK, false);
    wifi_radio_operation_unlock();
    return error;
done:
    (void)wifi_radio_restart_configs_record(stage, error, false);
    wifi_radio_operation_unlock();
    return error;
}
static esp_err_t wifi_radio_twt_recovery_phase(const esp32_mquickjs_wifi_radio_lifecycle_t *token, bool shutdown)
{
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_twt_recovery_exact_locked(token))
        error = shutdown ? wifi_radio_shutdown_lease_locked(NULL, true) : wifi_radio_stop_lease_locked(NULL, true);
    wifi_radio_operation_unlock();
    return error;
}
static esp_err_t wifi_radio_twt_recovery_stopped(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    bool valid = wifi_radio_twt_recovery_exact_locked(token) && s_twt_recovery.stopped &&
        !s_radio.started && !s_radio.stop_required && !s_radio.stop_submitted &&
        s_radio.event_phase == RADIO_EVENTS_IDLE && !s_radio.wake_locks && !s_radio.promiscuous_claimed &&
        !s_radio.restart_required && wifi_radio_twt_recovery_owners_locked() &&
        (s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_STOPPED ||
         s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING ||
         s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED);
    wifi_radio_operation_unlock();
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}
static esp_err_t wifi_radio_twt_recovery_finish(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_twt_recovery_exact_locked(token) || !s_twt_recovery.stopped || s_radio.driver_owned ||
        s_radio.started || s_radio.stop_required || s_radio.stop_submitted || s_radio.event_phase != RADIO_EVENTS_IDLE ||
        s_radio.operation.identity || s_radio.wake_locks || s_radio.promiscuous_claimed || s_radio.restart_required ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED) goto done;
    for (unsigned i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) if (s_radio.leases[i].identity) goto done;
    memset(&s_twt_recovery, 0, sizeof(s_twt_recovery));
    error = ESP_OK; /* Frozen configuration and lifecycle transfer to replay. */
done:
    wifi_radio_operation_unlock();
    return error;
}
bool esp32_mquickjs_wifi_radio_twt_probe_request_close(const esp32_mquickjs_wifi_twt_token_t *token)
{
    taskENTER_CRITICAL(&s_radio.lock);
    bool exact = token != NULL && token->identity != 0U && token->identity == s_twt_probe.state.token.identity &&
        token->generation == s_twt_probe.state.token.generation;
    if (exact) s_twt_probe.state.cleanup_pending = true;
    taskEXIT_CRITICAL(&s_radio.lock);
    return exact;
}
bool esp32_mquickjs_wifi_radio_twt_probe_cleanup_token(esp32_mquickjs_wifi_twt_token_t *token)
{
    if (token == NULL) return false;
    taskENTER_CRITICAL(&s_radio.lock);
    bool pending = s_twt_probe.state.token.identity != 0U && s_twt_probe.state.cleanup_pending;
    if (pending) *token = s_twt_probe.state.token;
    taskEXIT_CRITICAL(&s_radio.lock);
    return pending;
}

esp_err_t esp32_mquickjs_wifi_radio_twt_probe_retire(esp32_mquickjs_wifi_twt_token_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_twt_probe_exact_locked(token) || s_twt_probe.state.dispatching) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    s_twt_probe.state.cleanup_pending = true;
    taskEXIT_CRITICAL(&s_radio.lock);
    /* No accepted native identity means the reviewed wrapper never entered
     * a probe; only the Radio reservation needs release. */
    error = s_twt_probe.state.native_identity == 0U ? ESP_OK :
        esp32_mquickjs_wifi_twt_probe_retire_poll(&s_twt_probe.retirement, token, s_twt_probe.state.native_identity);
    taskENTER_CRITICAL(&s_radio.lock);
    s_twt_probe.state.cleanup_error = error;
    s_twt_probe.state.cleanup_stage = s_twt_probe.retirement.stage;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (error != ESP_OK) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    esp32_mquickjs_wifi_radio_operation_t operation = s_radio.operation;
    memset(&s_radio.operation, 0, sizeof(s_radio.operation));
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_release_locked(&s_twt_probe.lease);
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_twt_probe.lease.acquired) {
        s_radio.operation = operation;
        error = s_twt_probe.state.cleanup_error = ESP_ERR_INVALID_STATE;
        s_twt_probe.state.cleanup_stage = "probe-lease-release";
    } else {
        memset(&s_twt_probe, 0, sizeof(s_twt_probe));
        *token = (esp32_mquickjs_wifi_twt_token_t){0};
    }
    taskEXIT_CRITICAL(&s_radio.lock);
done:
    wifi_radio_operation_unlock();
    return error;
}
#endif

#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
/* No SDK or observation-queue call from this event handler. The SDK has no
 * cookie: any second/foreign report invalidates attribution and drain evidence.
 * Never reuse this lane until the preceding report and native work are retired. */
static void wifi_radio_ftm_event(int32_t id, const void *data)
{
    if (id != WIFI_EVENT_FTM_REPORT || data == NULL) return;
    const wifi_event_ftm_report_t *report = data;
    taskENTER_CRITICAL(&s_radio.lock);
    esp32_mquickjs_wifi_ftm_state_t *state = &s_ftm.state;
    if (state->token.identity != 0U && !state->physical_termination && (state->dispatching || state->submitted)) {
        state->sdk_fenced = state->event_fenced = false;
        s_ftm.fence_posted = false;
        if (state->revision == UINT32_MAX) state->ambiguous = true;
        else ++state->revision;
        if (state->terminal || memcmp(report->peer_mac, s_ftm.config.resp_mac, 6) != 0 ||
            (unsigned)report->status > FTM_STATUS_USER_TERM) state->ambiguous = true;
        else {
            state->terminal = true;
            memset(&state->report, 0, sizeof(state->report));
            memcpy(state->report.peer_mac, report->peer_mac, 6);
            state->report.status = report->status;
            if (report->status == FTM_STATUS_SUCCESS) {
                state->report.rtt_raw = report->rtt_raw;
                state->report.rtt_est = report->rtt_est;
                state->report.dist_est = report->dist_est;
                state->report.ftm_report_num_entries = report->ftm_report_num_entries;
            }
        }
    }
    taskEXIT_CRITICAL(&s_radio.lock);
}

static bool wifi_radio_ftm_exact_locked(const esp32_mquickjs_wifi_ftm_token_t *token)
{
    return token != NULL && token->identity != 0U &&
        token->identity == s_ftm.state.token.identity && token->generation == s_ftm.state.token.generation &&
        token->generation == s_radio.generation && wifi_radio_lease_valid(&s_ftm.lease) &&
        s_ftm.lease.client == ESP32_MQUICKJS_WIFI_RADIO_CLIENT_FTM &&
        s_radio.operation.kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_FTM &&
        s_radio.operation.identity == token->identity && s_radio.operation.generation == token->generation &&
        s_radio.operation.lease_identity == s_ftm.lease.identity;
}

static bool wifi_radio_ftm_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return token != NULL && token->identity != 0U && token->identity == s_ftm.recovery.identity &&
        token->generation == s_ftm.recovery.generation && token->identity == s_radio.lifecycle.identity &&
        token->generation == s_radio.lifecycle.generation;
}

static uint32_t wifi_radio_ftm_recovery_capture_owner_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return wifi_radio_ftm_recovery_exact_locked(token) && wifi_radio_ftm_exact_locked(&s_ftm.state.token) &&
        s_ftm.state.submitted && !s_ftm.state.dispatching && !s_ftm.state.physical_termination
        ? s_ftm.lease.identity : 0U;
}

bool esp32_mquickjs_wifi_radio_ftm_recovery_active(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    bool active = wifi_radio_ftm_recovery_exact_locked(token);
    wifi_radio_operation_unlock();
    return active;
}

esp_err_t esp32_mquickjs_wifi_radio_begin_ftm_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_ftm_token_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode)
{
    if (token == NULL || token->identity != 0U || token->generation != 0U || mode == NULL ||
        operation == NULL || operation->identity == 0U || operation->generation == 0U) return ESP_ERR_INVALID_ARG;
    *mode = WIFI_MODE_NULL;
#if !(CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C5)
    /* STOP/FTM cleanup control flow has not been qualified for another SDK target. */
    return ESP_ERR_NOT_SUPPORTED;
#endif
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_ftm_exact_locked(operation) || !s_ftm.state.submitted || s_ftm.state.dispatching ||
        s_ftm.state.physical_termination || s_radio.lifecycle.identity != 0U ||
        !s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started || !s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.fault_stage != NULL ||
        s_radio.cleanup_stage != NULL || s_radio.restart_required || s_radio.wake_locks != 0U ||
        s_radio.promiscuous_claimed || s_tx_rate_lease.identity != 0U || s_tx_rate_lease.restore_pending ||
        s_interval.owner.identity != 0U || s_interval.restore_pending || s_vendor_ie.start_owner.identity != 0U ||
        s_config_restart.snapshot != NULL || s_policy_restart.owner.identity != 0U) goto done;
    if (s_radio.effective_mode != WIFI_MODE_STA && s_radio.effective_mode != WIFI_MODE_APSTA) goto done;
    const esp32_mquickjs_wifi_radio_lease_t *owners[3] = {application, station, access_point};
    const esp32_mquickjs_wifi_radio_client_t clients[3] = {
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,
    };
    uint32_t identities[3] = {0};
    for (size_t i = 0; i < 3; ++i) {
        if (owners[i] == NULL || !owners[i]->acquired) continue;
        if (!wifi_radio_lease_valid(owners[i]) || owners[i]->client != clients[i]) goto done;
        identities[i] = owners[i]->identity;
    }
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (owner->identity == 0U) continue;
        if (owner->promiscuous_identity != 0U || owner->raw_tx_identity != 0U ||
            (owner->identity != s_ftm.lease.identity && owner->identity != identities[0] &&
             owner->identity != identities[1] && owner->identity != identities[2])) goto done;
    }
    if (s_radio.next_lifecycle_identity == 0U) { err = ESP_ERR_NO_MEM; goto done; }
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.lifecycle = (esp32_mquickjs_wifi_radio_lifecycle_t){s_radio.generation, s_radio.next_lifecycle_identity++};
    s_ftm.recovery = s_radio.lifecycle;
    s_ftm.recovery_stopped = s_ftm.recovery_timer_started = s_ftm.recovery_timer_ready = s_ftm.recovery_sdk_ready = false;
    *token = s_radio.lifecycle;
    *mode = s_radio.effective_mode;
    taskEXIT_CRITICAL(&s_radio.lock);
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_checkpoint_ftm_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    const char *stage = "ftm-recovery-checkpoint-admission";
    if (!wifi_radio_ftm_recovery_exact_locked(token) || token->generation != s_radio.generation ||
        !s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started || !s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.fault_stage != NULL ||
        s_radio.cleanup_stage != NULL || mode != s_radio.effective_mode) goto done;
    stage = "ftm-recovery-policy-snapshot";
    err = wifi_radio_policy_restart_prepare_locked(token, mode);
    if (err != ESP_OK) goto done;
    err = wifi_radio_restart_configs_capture_locked(token, mode);
    if (err == ESP_OK) (void)wifi_radio_restart_configs_record("ftm-recovery-checkpoint-complete", ESP_OK, false);
    wifi_radio_operation_unlock();
    return err;
done:
    (void)wifi_radio_restart_configs_record(stage, err, false);
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_check_stopped_ftm_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_ftm_recovery_exact_locked(token) || !s_ftm.recovery_stopped || s_radio.started ||
        s_radio.stop_required || s_radio.stop_submitted || s_radio.event_phase != RADIO_EVENTS_IDLE ||
        s_radio.wake_locks != 0U || s_radio.promiscuous_claimed || s_radio.restart_required ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED)) goto done;
    uint32_t allowed = 0;
    if (s_ftm.lease.acquired || s_radio.operation.identity != 0U) {
        if (!wifi_radio_ftm_exact_locked(&s_ftm.state.token) || !s_ftm.state.submitted || s_ftm.state.dispatching) goto done;
        allowed = s_ftm.lease.identity;
    }
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U && s_radio.leases[i].identity != allowed) goto done;
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_ftm_recovery_phase(const esp32_mquickjs_wifi_radio_lifecycle_t *token, bool shutdown)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (wifi_radio_ftm_recovery_exact_locked(token)) {
        const esp32_mquickjs_wifi_radio_lease_t *allowed = s_ftm.lease.acquired ? &s_ftm.lease : NULL;
        err = shutdown ? wifi_radio_shutdown_lease_locked(allowed, false) : wifi_radio_stop_lease_locked(allowed, false);
    }
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_stop_ftm_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return wifi_radio_ftm_recovery_phase(token, false);
}

esp_err_t esp32_mquickjs_wifi_radio_shutdown_ftm_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return wifi_radio_ftm_recovery_phase(token, true);
}

esp_err_t esp32_mquickjs_wifi_radio_finish_ftm_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_ftm_recovery_exact_locked(token) || !s_ftm.recovery_stopped || !s_ftm.recovery_sdk_ready ||
        s_radio.driver_owned || s_radio.started || s_radio.stop_required || s_radio.stop_submitted ||
        s_radio.event_phase != RADIO_EVENTS_IDLE || s_radio.operation.identity != 0U || s_ftm.lease.acquired ||
        s_radio.wake_locks != 0U || s_radio.promiscuous_claimed || s_radio.restart_required ||
        s_radio.fault_stage != NULL || s_radio.cleanup_stage != NULL ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED) goto done;
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity != 0U) goto done;
    /* Retain central lifecycle/checkpoints until ordinary restore or cleanup. */
    memset(&s_ftm.recovery, 0, sizeof(s_ftm.recovery));
    s_ftm.recovery_stopped = s_ftm.recovery_timer_started = s_ftm.recovery_timer_ready = s_ftm.recovery_sdk_ready = false;
    err = ESP_OK;
done:
    wifi_radio_operation_unlock();
    return err;
}

static esp_err_t wifi_radio_ftm_admit_locked(const wifi_ftm_initiator_cfg_t *config,
    esp32_mquickjs_wifi_ftm_token_t *token)
{
    if (!s_radio.driver_owned || !s_radio.storage_configured || !s_radio.started ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED || s_radio.lifecycle.identity != 0U ||
        s_radio.operation.identity != 0U || s_radio.restart_required || s_radio.cleanup_stage != NULL ||
        s_ftm.state.token.identity != 0U) return ESP_ERR_INVALID_STATE;
    if (s_radio.fault_stage != NULL) return s_radio.fault_error;
    if (s_radio.next_operation_identity == 0U) return ESP_ERR_NO_MEM;
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) return err;
    if (mode != s_radio.effective_mode || !(mode & WIFI_MODE_STA)) return ESP_ERR_INVALID_STATE;
    err = wifi_radio_validate_regulatory_channel(config->channel);
    if (err != ESP_OK) return err;
    uint8_t current;
    wifi_second_chan_t secondary;
    uint32_t generation;
    err = wifi_radio_get_channel_locked(&current, &secondary, &generation);
    if (err != ESP_OK) return err;
    bool off_channel = current != config->channel;
    if (off_channel) {
        if (mode & WIFI_MODE_AP) return ESP_ERR_INVALID_STATE;
        wifi_ap_record_t ap;
        err = esp_wifi_sta_get_ap_info(&ap);
        if (err == ESP_OK) return ESP_ERR_INVALID_STATE;
        if (err != ESP_ERR_WIFI_NOT_CONNECT) return err;
        secondary = WIFI_SECOND_CHAN_NONE;
    }
    bool conflict = false;
    taskENTER_CRITICAL(&s_radio.lock);
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *owner = &s_radio.leases[i];
        if (owner->identity == 0U) continue;
        if (owner->raw_tx_identity != 0U || owner->channel_conflict ||
            (owner->fixed_channel && (owner->primary_channel != config->channel ||
                owner->secondary_channel != (uint8_t)secondary))) conflict = true;
        if (off_channel && owner->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION &&
            owner->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA &&
            owner->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE) conflict = true;
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (conflict) return ESP_ERR_INVALID_STATE;
    err = wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_FTM, WIFI_MODE_STA, &s_ftm.lease);
    if (err != ESP_OK) return err;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.operation = (esp32_mquickjs_wifi_radio_operation_t){
        .generation = s_radio.generation, .identity = s_radio.next_operation_identity++,
        .lease_identity = s_ftm.lease.identity, .kind = ESP32_MQUICKJS_WIFI_RADIO_OPERATION_FTM,
    };
    *token = (esp32_mquickjs_wifi_ftm_token_t){s_radio.generation, s_radio.operation.identity};
    s_ftm.state = (esp32_mquickjs_wifi_ftm_state_t){.token = *token};
    s_ftm.config = *config;
    s_ftm.fence_posted = false;
    s_ftm.posted_revision = 0;
    wifi_radio_live_lease_t *owner = wifi_radio_promiscuous_owner(s_ftm.lease.identity);
    owner->fixed_channel = true;
    owner->primary_channel = config->channel;
    owner->secondary_channel = (uint8_t)secondary;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ESP_OK;
}

bool esp32_mquickjs_wifi_ftm_config_valid(const wifi_ftm_initiator_cfg_t *config)
{
    return config != NULL && config->channel != 0U && !(config->resp_mac[0] & 1U) &&
        memcmp(config->resp_mac, "\0\0\0\0\0\0", 6) != 0 && config->burst_period <= 100U && config->burst_period != 1U &&
        (config->frm_count == 0U || config->frm_count == 16U || config->frm_count == 24U ||
         config->frm_count == 32U || config->frm_count == 64U);
}

esp_err_t esp32_mquickjs_wifi_radio_ftm_start(const wifi_ftm_initiator_cfg_t *config,
    esp32_mquickjs_wifi_ftm_token_t *token)
{
    if (!esp32_mquickjs_wifi_ftm_config_valid(config) || token == NULL ||
        token->identity != 0U || token->generation != 0U) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = wifi_radio_ftm_admit_locked(config, token);
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_radio.lock);
        s_ftm.state.dispatching = true;
        taskEXIT_CRITICAL(&s_radio.lock);
        /* Keep the request stable even beyond the synchronous SDK wrapper.
         * An early event may arrive while this call is still returning. */
        err = esp_wifi_ftm_initiate_session(&s_ftm.config);
        taskENTER_CRITICAL(&s_radio.lock);
        s_ftm.state.dispatching = false;
        s_ftm.state.submitted = true;
        s_ftm.state.submit_error = err;
        taskEXIT_CRITICAL(&s_radio.lock);
    }
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_ftm_end(const esp32_mquickjs_wifi_ftm_token_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_ftm_exact_locked(token)) goto done;
    if (wifi_radio_ftm_recovery_exact_locked(&s_radio.lifecycle)) {
        err = s_ftm.state.physical_termination ? ESP_OK : ESP_ERR_TIMEOUT;
        goto done;
    }
    taskENTER_CRITICAL(&s_radio.lock);
    s_ftm.state.end_requested = true;
    bool terminal = s_ftm.state.terminal;
    bool submit = s_ftm.state.submitted && !terminal && !s_ftm.state.end_written && !s_ftm.state.ambiguous;
    err = s_ftm.state.ambiguous ? ESP_ERR_INVALID_STATE : terminal ? ESP_OK : s_ftm.state.end_error;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (terminal && err == ESP_OK) goto done;
    if (submit) {
        err = esp_wifi_ftm_end_session();
        taskENTER_CRITICAL(&s_radio.lock);
        s_ftm.state.end_error = err;
        if (err == ESP_OK) s_ftm.state.end_written = true;
        taskEXIT_CRITICAL(&s_radio.lock);
    }
done:
    wifi_radio_operation_unlock();
    return err;
}

void esp32_mquickjs_wifi_radio_ftm_snapshot(esp32_mquickjs_wifi_ftm_state_t *out)
{
    if (out == NULL) return;
    taskENTER_CRITICAL(&s_radio.lock);
    *out = s_ftm.state;
    taskEXIT_CRITICAL(&s_radio.lock);
}

bool esp32_mquickjs_wifi_radio_ftm_status(const esp32_mquickjs_wifi_ftm_token_t *token,
    esp32_mquickjs_wifi_ftm_state_t *out)
{
    if (out == NULL) return false;
    wifi_radio_operation_lock();
    bool exact = wifi_radio_ftm_exact_locked(token);
    if (exact) esp32_mquickjs_wifi_radio_ftm_snapshot(out);
    wifi_radio_operation_unlock();
    return exact;
}

static void wifi_radio_ftm_timer_fence_callback(void *arg)
{
    (void)arg;
    /* TASK callbacks are serialized: every previously running legacy SDK timer
     * callback has returned before this final store. No Radio mutex here. */
    atomic_store_explicit(&s_ftm_timer_fence.completed, true, memory_order_release);
}

static esp_err_t wifi_radio_ftm_timer_fence_locked(const esp32_mquickjs_wifi_ftm_state_t *state)
{
    if (s_ftm_timer_fence.pending) {
        if (!atomic_load_explicit(&s_ftm_timer_fence.completed, memory_order_acquire)) return ESP_ERR_TIMEOUT;
        s_ftm_timer_fence.pending = false;
        bool exact = s_ftm_timer_fence.token.identity == state->token.identity &&
            s_ftm_timer_fence.token.generation == state->token.generation &&
            s_ftm_timer_fence.revision == state->revision;
        if (exact) return ESP_OK;
        /* An old completion cannot certify a later report revision. */
    }
    if (s_ftm_timer_fence.handle == NULL) {
        const esp_timer_create_args_t args = {.callback = wifi_radio_ftm_timer_fence_callback,
            .dispatch_method = ESP_TIMER_TASK, .name = "qjs_ftm_fence"};
        esp_err_t err = esp_timer_create(&args, &s_ftm_timer_fence.handle);
        if (err != ESP_OK) return err;
    }
    s_ftm_timer_fence.token = state->token;
    s_ftm_timer_fence.revision = state->revision;
    atomic_store_explicit(&s_ftm_timer_fence.completed, false, memory_order_release);
    esp_err_t err = esp_timer_start_once(s_ftm_timer_fence.handle, 1);
    if (err != ESP_OK) return err;
    s_ftm_timer_fence.pending = true;
    return ESP_ERR_TIMEOUT;
}

static esp_err_t wifi_radio_ftm_recovery_timer_locked(void)
{
    if (!wifi_radio_ftm_recovery_exact_locked(&s_radio.lifecycle) || !s_ftm.recovery_stopped ||
        s_radio.started || s_radio.stop_required) return ESP_ERR_INVALID_STATE;
    if (s_ftm.recovery_sdk_ready) return ESP_OK;
    if (!s_ftm.recovery_timer_started) {
        /* An ordinary retirement marker may have completed before STOP. Drain
         * it, then require a fresh marker armed after SDK timer destruction. */
        if (s_ftm_timer_fence.pending) {
            if (!atomic_load_explicit(&s_ftm_timer_fence.completed, memory_order_acquire)) return ESP_ERR_TIMEOUT;
            s_ftm_timer_fence.pending = false;
        }
        s_ftm.recovery_timer_started = true;
    }
    esp_err_t err = ESP_OK;
    if (!s_ftm.recovery_timer_ready) {
        esp32_mquickjs_wifi_ftm_state_t state;
        esp32_mquickjs_wifi_radio_ftm_snapshot(&state);
        err = wifi_radio_ftm_timer_fence_locked(&state);
        if (err != ESP_OK) return err;
        s_ftm.recovery_timer_ready = true;
    }
    /* Drain native work queued by the last timer callback. A queue barrier does
     * not claim the off-channel record is empty; physical deinit follows. */
    err = esp32_mquickjs_wifi_action_sdk_fence();
    if (err == ESP_OK) s_ftm.recovery_sdk_ready = true;
    return err;
}

/* Fixed SDK terminal paths disarm/destroy timers before posting the report and
 * free initiator context after that post. ets_timer_done only queues deletion;
 * first fence the ESP_TIMER_TASK callbacks, then serialize through the native
 * queue before accepting retirement. Also require the shared off-channel record
 * to be fully empty. The event alone and end_session's return are insufficient. */
static esp_err_t wifi_radio_ftm_fence_locked(esp32_mquickjs_wifi_ftm_state_t *out)
{
    esp32_mquickjs_wifi_radio_ftm_snapshot(out);
    if (!out->submitted || !out->terminal || out->ambiguous || out->revision == UINT32_MAX)
        return ESP_ERR_TIMEOUT;
    if (out->sdk_fenced) return ESP_OK;
    uint32_t revision = out->revision;
    esp_err_t err = wifi_radio_ftm_timer_fence_locked(out);
    if (err != ESP_OK) return err;
    err = esp32_mquickjs_wifi_action_sdk_quiescent();
    if (err != ESP_OK) return err;
    taskENTER_CRITICAL(&s_radio.lock);
    bool ready = s_ftm.state.revision == revision && !s_ftm.state.ambiguous;
    if (ready) s_ftm.state.sdk_fenced = true;
    *out = s_ftm.state;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ready ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t esp32_mquickjs_wifi_radio_ftm_collect(const esp32_mquickjs_wifi_ftm_token_t *token,
    wifi_ftm_report_entry_t *entries, size_t bytes, unsigned capacity,
    esp32_mquickjs_wifi_ftm_state_t *out)
{
    if (out == NULL || capacity > ESP32_MQUICKJS_WIFI_FTM_MAX_REPORT_ENTRIES ||
        (entries == NULL) != (capacity == 0U) || bytes != capacity * sizeof(*entries)) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_ftm_exact_locked(token)) goto done;
    if (wifi_radio_ftm_recovery_exact_locked(&s_radio.lifecycle)) {
        esp32_mquickjs_wifi_radio_ftm_snapshot(out);
        err = s_ftm.state.physical_termination ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
        goto done;
    }
    err = wifi_radio_ftm_fence_locked(out);
    if (err != ESP_OK) goto done;
    if (out->report_consumed) { err = ESP_ERR_INVALID_STATE; goto done; }
    uint32_t revision = out->revision;
    unsigned count = out->report.ftm_report_num_entries;
    if (count > capacity) count = capacity;
    if (bytes != 0U) memset(entries, 0, bytes);
    /* Even non-success/empty summaries require the free-only SDK suffix. */
    err = esp_wifi_ftm_get_report(count == 0U ? NULL : entries, (uint8_t)count);
    taskENTER_CRITICAL(&s_radio.lock);
    s_ftm.state.report_error = err;
    if (err == ESP_OK) {
        s_ftm.state.report_consumed = true;
        s_ftm.state.report_discarded = entries == NULL;
        if (s_ftm.state.revision != revision || s_ftm.state.ambiguous) {
            s_ftm.state.ambiguous = true;
            s_ftm.state.sdk_fenced = s_ftm.state.event_fenced = false;
            err = ESP_ERR_INVALID_RESPONSE;
        } else s_ftm.state.copied_entries = (uint8_t)count;
    }
    *out = s_ftm.state;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (err != ESP_OK && bytes != 0U) memset(entries, 0, bytes);
done:
    wifi_radio_operation_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_radio_ftm_retire(esp32_mquickjs_wifi_ftm_token_t *token,
    esp32_mquickjs_wifi_ftm_state_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_ftm_exact_locked(token)) goto done;
    esp32_mquickjs_wifi_radio_ftm_snapshot(out);
    bool physical = out->physical_termination;
    if (wifi_radio_ftm_recovery_exact_locked(&s_radio.lifecycle) && !physical) { err = ESP_ERR_TIMEOUT; goto done; }
    if (physical) {
        if (!wifi_radio_ftm_recovery_exact_locked(&s_radio.lifecycle) || !s_ftm.recovery_stopped ||
            !s_ftm.recovery_sdk_ready || s_radio.driver_owned || s_radio.started || s_radio.stop_required) goto done;
        goto release_owner;
    }
    if (!out->report_consumed) { err = ESP_ERR_TIMEOUT; goto done; }
    err = wifi_radio_ftm_fence_locked(out);
    if (err != ESP_OK) goto done;
    taskENTER_CRITICAL(&s_radio.lock);
    wifi_radio_ftm_fence_t fence = {.token = *token, .revision = s_ftm.state.revision};
    bool post = !s_ftm.state.event_fenced && !s_ftm.fence_posted && s_ftm.state.sdk_fenced && !s_ftm.state.ambiguous;
    if (post) { s_ftm.fence_posted = true; s_ftm.posted_revision = fence.revision; }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (post) {
        err = esp_event_post(ESP32QJS_WIFI_RADIO_CONTROL_EVENT, 3, &fence, sizeof(fence), 0);
        if (err != ESP_OK) {
            taskENTER_CRITICAL(&s_radio.lock);
            if (s_ftm.posted_revision == fence.revision) s_ftm.fence_posted = false;
            taskEXIT_CRITICAL(&s_radio.lock);
            goto done;
        }
    }
release_owner:
    taskENTER_CRITICAL(&s_radio.lock);
    *out = s_ftm.state;
    bool release = physical || (out->sdk_fenced && out->event_fenced && !out->ambiguous);
    if (release) {
        memset(&s_radio.operation, 0, sizeof(s_radio.operation));
        memset(&s_ftm.state, 0, sizeof(s_ftm.state));
        memset(&s_ftm.config, 0, sizeof(s_ftm.config));
        s_ftm.fence_posted = false;
        *token = (esp32_mquickjs_wifi_ftm_token_t){0};
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    if (release) { wifi_radio_release_locked(&s_ftm.lease); err = ESP_OK; }
    else err = ESP_ERR_TIMEOUT;
done:
    wifi_radio_operation_unlock();
    return err;
}
#endif

#endif

#endif
