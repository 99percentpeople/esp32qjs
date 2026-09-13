#include "esp32_mquickjs_wifi_enterprise.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_memory_rtos.h"
#include "esp32_mquickjs_wifi_roaming.h"
#include "esp32_mquickjs_wifi_neighbor_request.h"
#include "esp32_mquickjs_wifi_rrm_request.h"
#include "esp32_mquickjs_wifi_eap_radio.h"
#include "esp32_mquickjs_wifi_smartconfig_session.h"
#include "esp32_mquickjs_wifi_wps_station.h"
#include "esp32_mquickjs_wifi_dpp_station.h"
#include "esp32_mquickjs_wifi_dpp_session.h"
#include "esp32_mquickjs_wifi_nan_session.h"
#include "esp32_mquickjs_wifi_nan.h"
#include "esp32_mquickjs_wifi_mesh.h"
#include "esp32_mquickjs_wifi_mesh_session.h"
#include "esp32_mquickjs_wifi_dpp.h"
#include "esp32_mquickjs_wifi_wps_ap_radio.h"
#include "esp32_mquickjs_wifi_wps_ap_session.h"
#include "esp32_mquickjs_wifi_wps_session.h"
#include "esp32_mquickjs_wifi_smartconfig.h"
#include "esp32_mquickjs_wifi_wps.h"
#include "esp32_mquickjs_wifi_wps_ap.h"
#include "esp32_mquickjs_wifi_smartconfig_connection.h"
#include "esp32_mquickjs_wifi_eap_config.h"
#include "esp32_mquickjs_wifi_raw_tx_ap.h"
#include "esp32_mquickjs_wifi_ftm_session.h"
#include "esp32_mquickjs_wifi_ftm.h"
#include "esp32_mquickjs_wifi_twt.h"
#include "esp32_mquickjs_wifi_action_radio.h"
#include "esp32_mquickjs_wifi_vendor_ie_watch.h"
#include "esp32_mquickjs_wifi_vendor_ie.h"
#include "esp32_mquickjs_wifi_wait.h"
#include "esp32_mquickjs_wifi_wapi_radio.h"
#include "esp32_mquickjs_wifi.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_net.h"
#include "esp32_mquickjs_wifi_netif.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_wifi_raw_tx.h"
#include "esp32_mquickjs_wifi_action.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp32_mquickjs_wifi_runtime_resources.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_netif_private.h"
#include "esp_private/wifi.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netif.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_STARTED_BIT BIT2
#define WIFI_LINK_DRAINED_BIT BIT3
#define WIFI_CONTROL_LINK_DRAINED 1

ESP_EVENT_DEFINE_BASE(ESP32QJS_WIFI_CONTROL_EVENT);
#define WIFI_SCAN_EVENT_QUEUE_LEN 1
#define WIFI_CONNECT_EVENT_QUEUE_LEN 1
#define WIFI_DRIVER_EVENT_QUEUE_LEN 8
#define WIFI_SCAN_BSSID_STR_LEN 18

static const char *TAG = "esp32qjs_wifi";

static esp32_mquickjs_wifi_state_t s_wifi_state;
/* Observation history survives helper teardown/runtime restart. The association
 * marker is not a resettable counter and never drives a reconnect policy. */
static portMUX_TYPE s_wifi_connection_counters_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    uint32_t attempts, reconnect_attempts, submission_failures, associations;
    bool ever_associated;
} s_wifi_connection_counters;

static void wifi_connection_counter_add(uint32_t *counter)
{
    if (*counter != UINT32_MAX) ++*counter;
}

static void wifi_connection_note_submit(void)
{
    portENTER_CRITICAL(&s_wifi_connection_counters_lock);
    wifi_connection_counter_add(&s_wifi_connection_counters.attempts);
    if (s_wifi_connection_counters.ever_associated)
        wifi_connection_counter_add(&s_wifi_connection_counters.reconnect_attempts);
    portEXIT_CRITICAL(&s_wifi_connection_counters_lock);
}

static void wifi_connection_note_failure(void)
{
    portENTER_CRITICAL(&s_wifi_connection_counters_lock);
    wifi_connection_counter_add(&s_wifi_connection_counters.submission_failures);
    portEXIT_CRITICAL(&s_wifi_connection_counters_lock);
}

static void wifi_connection_note_association(void)
{
    portENTER_CRITICAL(&s_wifi_connection_counters_lock);
    s_wifi_connection_counters.ever_associated = true;
    wifi_connection_counter_add(&s_wifi_connection_counters.associations);
    portEXIT_CRITICAL(&s_wifi_connection_counters_lock);
}

void esp32_mquickjs_wifi_reset_connection_counters(void)
{
    portENTER_CRITICAL(&s_wifi_connection_counters_lock);
    s_wifi_connection_counters.attempts = 0;
    s_wifi_connection_counters.reconnect_attempts = 0;
    s_wifi_connection_counters.submission_failures = 0;
    s_wifi_connection_counters.associations = 0;
    portEXIT_CRITICAL(&s_wifi_connection_counters_lock);
}

static JSValue wifi_connection_counters_to_js(JSContext *ctx)
{
    uint32_t attempts, reconnects, failures, associations;
    portENTER_CRITICAL(&s_wifi_connection_counters_lock);
    attempts = s_wifi_connection_counters.attempts;
    reconnects = s_wifi_connection_counters.reconnect_attempts;
    failures = s_wifi_connection_counters.submission_failures;
    associations = s_wifi_connection_counters.associations;
    portEXIT_CRITICAL(&s_wifi_connection_counters_lock);
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "attempts", JS_NewUint32(ctx, attempts)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "reconnectAttempts", JS_NewUint32(ctx, reconnects)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "submissionFailures", JS_NewUint32(ctx, failures)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "associations", JS_NewUint32(ctx, associations))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
/* Helper lock protects native registration and terminal metadata. Outside
 * resettable helper storage; core must retire this owner before helper reset. */
static struct {
    esp32_mquickjs_wifi_radio_operation_t owner;
    uint32_t generation, terminal;
    int32_t reason;
} s_wifi_smartconfig_connect;
/* Boot identities live outside resettable helper storage. */
static uint32_t s_wifi_wps_capture_identity;
static uint32_t s_wifi_wps_next_identity = 1;
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
static uint32_t s_wifi_wps_ap_helper_identity;
static uint32_t s_wifi_wps_ap_next_helper_identity = 1;
#endif
#endif

static esp32_mquickjs_wifi_radio_lease_t s_wifi_application;
static esp32_mquickjs_wifi_radio_lifecycle_t s_wifi_lifecycle;
/* Outside resettable helper storage: a failed final shutdown still owns the
 * exact AP/STA configuration transaction after both netifs have been freed. */
static bool s_wifi_configuration_cleanup;
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
static struct {
    esp32_mquickjs_wifi_eap_config_token_t control;
    uint64_t binding;
    esp32_mquickjs_wifi_eap_profile_t *profile; /* Borrowed from the exact restart control. */
    bool release_ap, checkpoint_attempted;
} s_wifi_eap_stop;
#endif
static bool s_wifi_ap_stop_cleanup;
static wifi_mode_t s_wifi_configuration_mode;
/* An admitted ordinary stop retains driver allocations. Keep this choice
 * outside helper reset so retries cannot silently become a shutdown. */
static bool s_wifi_configuration_stop_only;
static bool s_wifi_configuration_disconnect_pending;
/* Keep the last helper setup failure visible even after successful unwind. */
static const char *s_wifi_setup_stage;
static esp_err_t s_wifi_setup_error;
static _Atomic(esp32_mquickjs_runtime_t *) s_wifi_runtime;
static void wifi_stop_connect_timeout_timer(void);
static void wifi_release_radio_operation(void);
/* A timer callback may outlive public cancellation. Its generation is not
 * rebound until stop_blocking proves the previous callback has exited. */
static _Atomic uint32_t s_wifi_timeout_armed_generation;
static void wifi_lock(void);
static void wifi_unlock(void);
static _Atomic uint32_t s_wifi_timeout_pending;
/* Control callbacks publish observations themselves, after committing native state.
 * The generic observer skips these IDs even if it registered before this helper. */
static _Atomic uint32_t s_wifi_watch_control_mask;

bool esp32_mquickjs_wifi_watch_controlled(int32_t id)
{
    return id >= 0 && id < 32 &&
        (atomic_load_explicit(&s_wifi_watch_control_mask, memory_order_acquire) & (1U << id)) != 0;
}

bool esp32_mquickjs_wifi_watch_control_ready(int32_t id, uint32_t generation)
{
    if (generation == 0) return true;
    wifi_lock();
    bool pending = id == WIFI_EVENT_SCAN_DONE
        ? s_wifi_state.scan_generation == generation && s_wifi_state.scan_future_registered
        : id == WIFI_EVENT_STA_DISCONNECTED &&
          s_wifi_state.connect_generation == generation && esp32_mquickjs_wifi_connection_reserved_locked();
    wifi_unlock();
    return !pending;
}

typedef enum {
    WIFI_DRIVER_EVENT_STARTED = 1,
    WIFI_DRIVER_EVENT_DISCONNECTED,
    WIFI_DRIVER_EVENT_SCAN_DONE,
    WIFI_DRIVER_EVENT_GOT_IP,
    WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,
    WIFI_DRIVER_EVENT_LINK_DRAINED,
} esp32_mquickjs_wifi_driver_event_kind_t;

typedef struct {
    esp32_mquickjs_wifi_driver_event_kind_t kind;
    int32_t reason;
    uint32_t status;
    uint32_t generation;
} esp32_mquickjs_wifi_driver_event_t;

static bool wifi_driver_event_poller(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime,
                                     void *opaque);

static void *wifi_create_lock(void *opaque)
{
    (void)opaque;
    return esp32_mquickjs_memory_mutex_create("wifi");
}

static void wifi_delete_lock(void *value, void *opaque)
{
    (void)opaque;
    esp32_mquickjs_memory_mutex_delete((SemaphoreHandle_t)value);
}

static void *wifi_create_event_group(void *opaque)
{
    (void)opaque;
    return esp32_mquickjs_memory_event_group_create("wifi");
}

static void wifi_delete_event_group(void *value, void *opaque)
{
    (void)opaque;
    esp32_mquickjs_memory_event_group_delete((EventGroupHandle_t)value);
}

static void *wifi_create_queue(size_t length, size_t item_size, void *opaque)
{
    (void)opaque;
    return esp32_mquickjs_memory_queue_create("wifi", length, item_size, true);
}

static void wifi_delete_queue(void *value, void *opaque)
{
    (void)opaque;
    esp32_mquickjs_memory_queue_delete((QueueHandle_t)value);
}

static const esp32_mquickjs_wifi_runtime_resource_ops_t
    s_wifi_runtime_resource_ops = {
        .create_lock = wifi_create_lock,
        .delete_lock = wifi_delete_lock,
        .create_event_group = wifi_create_event_group,
        .delete_event_group = wifi_delete_event_group,
        .create_queue = wifi_create_queue,
        .delete_queue = wifi_delete_queue,
    };

esp32_mquickjs_wifi_state_t *esp32_mquickjs_wifi_state(void)
{
    return &s_wifi_state;
}

static void wifi_lock(void)
{
    if (s_wifi_state.lock != NULL) {
        xSemaphoreTake(s_wifi_state.lock, portMAX_DELAY);
    }
}

static void wifi_unlock(void)
{
    if (s_wifi_state.lock != NULL) {
        xSemaphoreGive(s_wifi_state.lock);
    }
}

void esp32_mquickjs_wifi_lock(void)
{
    wifi_lock();
}

void esp32_mquickjs_wifi_unlock(void)
{
    wifi_unlock();
}

bool esp32_mquickjs_wifi_connection_reserved_locked(void)
{
    return s_wifi_state.connect_future_registered
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
        || s_wifi_smartconfig_connect.owner.identity != 0 || s_wifi_wps_capture_identity != 0
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
        || s_wifi_wps_ap_helper_identity != 0
#endif
#endif
        ;
}

void esp32_mquickjs_wifi_clear_scan_future(void)
{
    wifi_lock();
    s_wifi_state.scan_future_registered = false;
    memset(&s_wifi_state.scan_future_token, 0, sizeof(s_wifi_state.scan_future_token));
    wifi_unlock();
}

void esp32_mquickjs_wifi_clear_connect_future(void)
{
    wifi_stop_connect_timeout_timer();
    wifi_lock();
    s_wifi_state.connect_in_progress = false;
    s_wifi_state.connect_future_registered = false;
    s_wifi_state.connection_future_operation =
        ESP32_MQUICKJS_WIFI_OPERATION_NONE;
    memset(&s_wifi_state.connect_future_token, 0, sizeof(s_wifi_state.connect_future_token));
    wifi_unlock();
    wifi_release_radio_operation();
}

static void wifi_stop_connect_timeout_timer(void)
{
    atomic_store_explicit(&s_wifi_timeout_armed_generation, 0, memory_order_release);
    if (s_wifi_state.connect_timeout_timer == NULL) {
        return;
    }

    esp_err_t err = esp_timer_stop(s_wifi_state.connect_timeout_timer);
    if (err == ESP_ERR_INVALID_STATE) {
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_timer_stop(connect_timeout_timer) failed: %s", esp_err_to_name(err));
    }
}

static void wifi_queue_connect_event(uint32_t generation,
                                     uint32_t kind,
                                     int32_t reason,
                                     const esp32_mquickjs_wifi_link_snapshot_t *link)
{
    esp32_mquickjs_future_token_t token = {0};
    bool should_wake = false;
    esp32_mquickjs_wifi_connect_event_t event = {
        .generation = generation,
        .kind = kind,
        .reason = reason,
        .completed_us = esp_timer_get_time(),
    };
    if (link != NULL) event.link = *link;
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    wifi_lock();
    if (s_wifi_smartconfig_connect.owner.identity && s_wifi_smartconfig_connect.generation == generation &&
        !s_wifi_smartconfig_connect.terminal) {
        s_wifi_smartconfig_connect.terminal = kind;
        s_wifi_smartconfig_connect.reason = reason;
    }
    wifi_unlock();
#endif

    if (s_wifi_state.connect_queue != NULL) {
        wifi_lock();
        should_wake = s_wifi_state.connect_future_registered &&
            s_wifi_state.connect_generation == generation;
        if (should_wake) {
            xQueueOverwrite(s_wifi_state.connect_queue, &event);
            token = s_wifi_state.connect_future_token;
        }
        wifi_unlock();
        esp32_mquickjs_runtime_t *runtime = atomic_load_explicit(&s_wifi_runtime, memory_order_acquire);
        if (runtime != NULL) {
            if (should_wake) (void)esp32_mquickjs_future_wake(runtime, token);
            esp32_mquickjs_notify_activity(runtime);
        }
    }
}

static void wifi_process_driver_event(
    const esp32_mquickjs_wifi_driver_event_t *driver_event);

static bool wifi_publish_driver_event_from_callback(
    const esp32_mquickjs_wifi_driver_event_t *event)
{
    if (event == NULL) return false;
    if (event->kind == WIFI_DRIVER_EVENT_CONNECT_TIMEOUT) {
        /* Timer callbacks never invoke Wi-Fi driver mutations. A coalesced
         * native obligation survives a full queue until the runtime polls. */
        if (event->generation != 0U)
            atomic_store_explicit(&s_wifi_timeout_pending, event->generation,
                                  memory_order_release);
    } else {
        /* ESP event-loop task: commit control state and Future results before
         * any lossy observation. No JS or Wi-Fi driver mutation in this path. */
        wifi_process_driver_event(event);
    }
    esp32_mquickjs_runtime_t *runtime = atomic_load_explicit(&s_wifi_runtime, memory_order_acquire);
    if (runtime != NULL) esp32_mquickjs_notify_activity(runtime);
    return true;
}

static void wifi_connect_timeout_cb(void *arg)
{
    const esp32_mquickjs_wifi_driver_event_t event = {
        .kind = WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,
        .generation = atomic_load_explicit(&s_wifi_timeout_armed_generation,
                                            memory_order_acquire),
    };

    (void)arg;
    atomic_fetch_add_explicit(&s_wifi_state.callbacks_active, 1,
                              memory_order_acq_rel);
    (void)wifi_publish_driver_event_from_callback(&event);
    atomic_fetch_sub_explicit(&s_wifi_state.callbacks_active, 1,
                              memory_order_release);
}

static void wifi_set_scanning_locked(bool scanning)
{
    s_wifi_state.scan_in_progress = scanning;
    s_wifi_state.status.scanning = scanning;
}

void esp32_mquickjs_wifi_set_scanning_locked(bool scanning)
{
    wifi_set_scanning_locked(scanning);
}

/* Runtime task only. Callbacks never wait on the Radio mutation mutex.
 * A public Future ending is insufficient: retain the reservation through
 * SDK submission, terminal events and any pending native cleanup. */
static void wifi_release_radio_operation(void)
{
    esp32_mquickjs_wifi_radio_operation_t released = {0};
    wifi_lock();
    bool ready = s_wifi_state.radio_operation.kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN
        ? !s_wifi_state.scan_start_active && !s_wifi_state.scan_in_progress &&
          !s_wifi_state.scan_stop_active && !s_wifi_state.scan_draining && !s_wifi_state.scan_results_pending
        : !s_wifi_state.connect_start_active && !s_wifi_state.connect_in_progress &&
          !s_wifi_state.connect_draining && !s_wifi_state.disconnect_active;
    if (ready) {
        released = s_wifi_state.radio_operation;
        memset(&s_wifi_state.radio_operation, 0, sizeof(s_wifi_state.radio_operation));
    }
    wifi_unlock();
    esp32_mquickjs_wifi_radio_end_operation(&released);
}

static esp_err_t wifi_prepare_radio_operation(esp32_mquickjs_wifi_radio_operation_kind_t kind)
{
    wifi_release_radio_operation();
    wifi_lock();
    bool reserved = s_wifi_state.radio_operation.identity != 0U;
    bool handoff = reserved && kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_CONNECT &&
        s_wifi_state.radio_operation.kind == kind;
    wifi_unlock();
    /* A cancelled connect retains the same reservation throughout handoff. */
    if (reserved) return handoff ? ESP_OK : ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_radio_operation_t operation = {0};
    esp_err_t err = esp32_mquickjs_wifi_radio_begin_operation(
        &s_wifi_state.radio_lease, kind, &operation);
    if (err == ESP_OK) {
        wifi_lock();
        s_wifi_state.radio_operation = operation;
        wifi_unlock();
    }
    return err;
}

/* Called only by the runtime task; callbacks publish terminal state but never
 * clear driver results or submit another scan. No SDK calls under wifi_lock. */
esp_err_t esp32_mquickjs_wifi_drain_scan(void)
{
    wifi_lock();
    if (!s_wifi_state.scan_draining || s_wifi_state.scan_in_progress ||
        s_wifi_state.scan_stop_active) {
        wifi_unlock();
        return ESP_OK;
    }
    bool clear_results = s_wifi_state.scan_results_pending;
    wifi_unlock();
    esp_err_t err = clear_results ? esp_wifi_clear_ap_list() : ESP_OK;
    wifi_lock();
    s_wifi_state.scan_cleanup_error = err;
    if (err == ESP_OK) {
        s_wifi_state.scan_results_pending = false;
        s_wifi_state.scan_results_consumed_early = false;
        s_wifi_state.scan_draining = false;
        s_wifi_state.scan_stop_submitted = false;
        memset(&s_wifi_state.native_scan_config, 0,
               sizeof(s_wifi_state.native_scan_config));
        memset(s_wifi_state.native_scan_ssid, 0, sizeof(s_wifi_state.native_scan_ssid));
        memset(s_wifi_state.native_scan_bssid, 0, sizeof(s_wifi_state.native_scan_bssid));
    }
    wifi_unlock();
    wifi_release_radio_operation();
    return err;
}

static esp_err_t wifi_start_scan_reserved(const wifi_scan_config_t *config,
                                       uint32_t generation)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    size_t ssid_length = config->ssid != NULL ? strnlen((const char *)config->ssid, 33) : 0;
    if (config->ssid != NULL && (ssid_length == 0 || ssid_length > 32)) return ESP_ERR_INVALID_ARG;
    wifi_lock();
    if (!s_wifi_state.scan_future_registered ||
        generation != s_wifi_state.scan_generation ||
        s_wifi_state.scan_in_progress || s_wifi_state.scan_draining ||
        s_wifi_state.scan_results_pending || s_wifi_state.scan_stop_active ||
        s_wifi_state.connect_in_progress || s_wifi_state.connect_draining) {
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_wifi_state.native_scan_config = *config;
    /* Transfer filters into the native lane before publishing scan-in-progress.
     * A cancelled Future may free its own request before native SCAN_DONE. */
    memset(s_wifi_state.native_scan_ssid, 0, sizeof(s_wifi_state.native_scan_ssid));
    memset(s_wifi_state.native_scan_bssid, 0, sizeof(s_wifi_state.native_scan_bssid));
    if (config->ssid != NULL) {
        memcpy(s_wifi_state.native_scan_ssid, config->ssid, ssid_length);
        s_wifi_state.native_scan_config.ssid = s_wifi_state.native_scan_ssid;
    }
    if (config->bssid != NULL) {
        memcpy(s_wifi_state.native_scan_bssid, config->bssid, 6);
        s_wifi_state.native_scan_config.bssid = s_wifi_state.native_scan_bssid;
    }
    s_wifi_state.scan_cleanup_error = ESP_OK;
    s_wifi_state.scan_stop_submitted = false;
    s_wifi_state.scan_results_consumed_early = false;
    wifi_set_scanning_locked(true);
    wifi_unlock();
    esp_err_t err = esp_wifi_scan_start(&s_wifi_state.native_scan_config, false);
    if (err != ESP_OK) {
        wifi_lock();
        wifi_set_scanning_locked(false);
        memset(&s_wifi_state.native_scan_config, 0,
               sizeof(s_wifi_state.native_scan_config));
        memset(s_wifi_state.native_scan_ssid, 0, sizeof(s_wifi_state.native_scan_ssid));
        memset(s_wifi_state.native_scan_bssid, 0, sizeof(s_wifi_state.native_scan_bssid));
        wifi_unlock();
    }
    return err;
}

esp_err_t esp32_mquickjs_wifi_start_scan(const wifi_scan_config_t *config,
                                       uint32_t generation)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    if (config->ssid != NULL) {
        size_t length = strnlen((const char *)config->ssid, 33);
        if (length == 0 || length > 32) return ESP_ERR_INVALID_ARG;
    }
    if (config->bssid != NULL && ((config->bssid[0] & 1U) != 0U ||
        memcmp(config->bssid, "\0\0\0\0\0\0", 6) == 0)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = wifi_prepare_radio_operation(ESP32_MQUICKJS_WIFI_RADIO_OPERATION_SCAN);
    if (err != ESP_OK) return err;
    err = esp32_mquickjs_wifi_radio_validate_scan_channels(&s_wifi_state.radio_operation, config);
    if (err != ESP_OK) {
        wifi_release_radio_operation();
        return err;
    }
    wifi_lock();
    s_wifi_state.scan_start_active = true;
    wifi_unlock();
    err = wifi_start_scan_reserved(config, generation);
    wifi_lock();
    s_wifi_state.scan_start_active = false;
    wifi_unlock();
    wifi_release_radio_operation();
    return err;
}

esp_err_t esp32_mquickjs_wifi_stop_scan_for_results(uint32_t generation)
{
    wifi_lock();
    if (generation != s_wifi_state.scan_generation || !s_wifi_state.scan_future_registered ||
        s_wifi_state.scan_draining || s_wifi_state.scan_start_active || s_wifi_state.scan_stop_active) {
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_wifi_state.scan_in_progress || s_wifi_state.scan_stop_submitted) {
        wifi_unlock();
        return ESP_OK;
    }
    s_wifi_state.scan_stop_active = true;
    wifi_unlock();
    /* This synchronous SDK call stops the scan, but its SCAN_DONE event may
     * still be pending on the event loop. Preserve both the list owner and
     * native reservation until the caller reads results and that event drains. */
    esp_err_t err = esp_wifi_scan_stop();
    wifi_lock();
    s_wifi_state.scan_stop_active = false;
    s_wifi_state.scan_stop_submitted = err == ESP_OK;
    s_wifi_state.scan_cleanup_error = err;
    wifi_unlock();
    return err;
}

esp_err_t esp32_mquickjs_wifi_cancel_scan(uint32_t generation)
{
    bool stop = false;
    wifi_lock();
    if (generation != s_wifi_state.scan_generation) {
        wifi_unlock();
        return ESP_OK;
    }
    s_wifi_state.scan_future_registered = false;
    memset(&s_wifi_state.scan_future_token, 0, sizeof(s_wifi_state.scan_future_token));
    s_wifi_state.scan_draining = s_wifi_state.scan_in_progress ||
        s_wifi_state.scan_results_pending || s_wifi_state.scan_stop_active;
    if (s_wifi_state.scan_in_progress && !s_wifi_state.scan_stop_submitted &&
        !s_wifi_state.scan_stop_active) {
        stop = true;
        s_wifi_state.scan_stop_active = true;
    }
    wifi_unlock();
    esp_err_t err = ESP_OK;
    if (stop) {
        err = esp_wifi_scan_stop();
        wifi_lock();
        s_wifi_state.scan_stop_active = false;
        s_wifi_state.scan_stop_submitted = err == ESP_OK;
        s_wifi_state.scan_cleanup_error = err;
        wifi_unlock();
    }
    esp_err_t cleanup_err = esp32_mquickjs_wifi_drain_scan();
    /* An early result read can be followed by SCAN_DONE before detachment.
     * Then no list/draining suffix remains, but the reservation is now ready. */
    wifi_release_radio_operation();
    return err != ESP_OK ? err : cleanup_err;
}

void esp32_mquickjs_wifi_scan_results_consumed(void)
{
    wifi_lock();
    s_wifi_state.scan_results_pending = false;
    s_wifi_state.scan_results_consumed_early = s_wifi_state.scan_in_progress;
    wifi_unlock();
    wifi_release_radio_operation();
}

/* The default STA disconnect handler was registered before ours. It waits
 * for netif/DHCP shutdown. A marker on that same FIFO loop then drains already
 * posted IP notifications before this link epoch can admit another connect. */
static void wifi_begin_disconnect_locked(void)
{
    if (!s_wifi_state.connect_draining) {
        s_wifi_state.connect_draining = true;
        s_wifi_state.disconnect_active = false;
        s_wifi_state.disconnect_submitted = false;
        s_wifi_state.disconnect_seen = false;
        s_wifi_state.disconnect_fence_posted = false;
        s_wifi_state.disconnect_fence_seen = false;
        if (s_wifi_state.disconnect_epoch == UINT32_MAX) {
            s_wifi_state.disconnect_epoch_exhausted = true;
            s_wifi_state.disconnect_cleanup_error = ESP_ERR_INVALID_STATE;
        } else {
            s_wifi_state.disconnect_epoch++;
            s_wifi_state.disconnect_cleanup_error = ESP_OK;
        }
    }
    s_wifi_state.connect_in_progress = false;
    s_wifi_state.status.connected = false;
    memset(&s_wifi_state.status.link, 0, sizeof(s_wifi_state.status.link));
}

static bool wifi_finish_disconnect_locked(void)
{
    if (!s_wifi_state.connect_draining || s_wifi_state.disconnect_active ||
        !s_wifi_state.disconnect_seen || !s_wifi_state.disconnect_fence_seen ||
        s_wifi_state.disconnect_epoch_exhausted) return false;
    s_wifi_state.connect_draining = false;
    s_wifi_state.disconnect_cleanup_error = ESP_OK;
    return true;
}

static esp_err_t wifi_post_disconnect_fence(void)
{
    wifi_lock();
    if (!s_wifi_state.connect_draining || !s_wifi_state.disconnect_seen ||
        s_wifi_state.disconnect_fence_posted || s_wifi_state.disconnect_epoch_exhausted) {
        esp_err_t err = s_wifi_state.disconnect_cleanup_error;
        wifi_unlock();
        return err;
    }
    uint32_t epoch = s_wifi_state.disconnect_epoch;
    s_wifi_state.disconnect_fence_posted = true;
    wifi_unlock();
    /* Roaming/IP-retention configurations or a failed netif down must not
     * masquerade as a completed DHCP shutdown barrier. */
    esp_err_t err = s_wifi_state.sta_netif != NULL &&
        !esp_netif_is_netif_up(s_wifi_state.sta_netif)
        ? esp_event_post(ESP32QJS_WIFI_CONTROL_EVENT, WIFI_CONTROL_LINK_DRAINED,
                         &epoch, sizeof(epoch), 0) : ESP_ERR_INVALID_STATE;
    wifi_lock();
    if (s_wifi_state.disconnect_epoch == epoch && s_wifi_state.connect_draining) {
        if (err != ESP_OK) s_wifi_state.disconnect_fence_posted = false;
        s_wifi_state.disconnect_cleanup_error = err;
    }
    wifi_unlock();
    return err;
}

static esp_err_t wifi_request_disconnect(void)
{
    wifi_lock();
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (s_wifi_wps_ap_helper_identity) { wifi_unlock(); return ESP_ERR_INVALID_STATE; }
#endif
    if (!s_wifi_state.connect_draining && !s_wifi_state.connect_in_progress &&
        !s_wifi_state.status.connected) {
        wifi_unlock();
        return ESP_OK;
    }
    wifi_begin_disconnect_locked();
    bool submit = !s_wifi_state.disconnect_seen &&
        !s_wifi_state.disconnect_submitted && !s_wifi_state.disconnect_active;
    if (submit) s_wifi_state.disconnect_active = true;
    wifi_unlock();
    wifi_stop_connect_timeout_timer();
    xEventGroupClearBits(s_wifi_state.event_group,
                        WIFI_CONNECTED_BIT | WIFI_FAILED_BIT | WIFI_LINK_DRAINED_BIT);
    esp_err_t err = ESP_OK;
    if (submit) {
        err = esp_wifi_disconnect();
        wifi_lock();
        s_wifi_state.disconnect_active = false;
        s_wifi_state.disconnect_submitted = err == ESP_OK || s_wifi_state.disconnect_seen;
        s_wifi_state.disconnect_cleanup_error = s_wifi_state.disconnect_epoch_exhausted
            ? ESP_ERR_INVALID_STATE : err;
        bool drained = wifi_finish_disconnect_locked();
        wifi_unlock();
        if (drained) xEventGroupSetBits(s_wifi_state.event_group, WIFI_LINK_DRAINED_BIT);
    }
    esp_err_t fence_err = wifi_post_disconnect_fence();
    return err != ESP_OK ? err : fence_err;
}

esp_err_t esp32_mquickjs_wifi_cancel_connect(uint32_t generation)
{
    wifi_lock();
    if (generation != s_wifi_state.connect_generation) {
        wifi_unlock();
        return ESP_OK;
    }
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (s_wifi_smartconfig_connect.owner.identity || s_wifi_wps_capture_identity
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
        || s_wifi_wps_ap_helper_identity
#endif
    ) {
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }
#endif
    if (s_wifi_state.connect_in_progress || s_wifi_state.status.connected ||
        s_wifi_state.connect_draining) wifi_begin_disconnect_locked();
    s_wifi_state.connect_future_registered = false;
    s_wifi_state.connection_future_operation = ESP32_MQUICKJS_WIFI_OPERATION_NONE;
    memset(&s_wifi_state.connect_future_token, 0, sizeof(s_wifi_state.connect_future_token));
    wifi_unlock();
    return wifi_request_disconnect();
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    esp32_mquickjs_wifi_driver_event_t driver_event = {0};

    (void)arg;
    atomic_fetch_add_explicit(&s_wifi_state.callbacks_active, 1,
                              memory_order_acq_rel);
    if (event_base == ESP32QJS_WIFI_CONTROL_EVENT &&
        event_id == WIFI_CONTROL_LINK_DRAINED && event_data != NULL) {
        driver_event.kind = WIFI_DRIVER_EVENT_LINK_DRAINED;
        driver_event.generation = *(const uint32_t *)event_data;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        driver_event.kind = WIFI_DRIVER_EVENT_STARTED;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *event = event_data;
        wifi_lock();
        memset(&s_wifi_state.status.link, 0, sizeof(s_wifi_state.status.link));
        if (!s_wifi_state.connect_draining && event != NULL && event->ssid_len > 0 &&
            event->ssid_len <= sizeof(s_wifi_state.status.link.ssid) &&
            (s_wifi_state.connect_in_progress || s_wifi_state.status.connected)) {
            esp32_mquickjs_wifi_link_snapshot_t *link = &s_wifi_state.status.link;
            memcpy(link->ssid, event->ssid, event->ssid_len);
            link->ssid_len = event->ssid_len;
            memcpy(link->bssid, event->bssid, sizeof(link->bssid));
            link->channel = event->channel;
            link->aid = event->aid;
            link->valid = true;
            wifi_connection_note_association();
        }
        wifi_unlock();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = event_data;
        driver_event.kind = WIFI_DRIVER_EVENT_DISCONNECTED;
        driver_event.reason =
            event != NULL ? (int32_t)event->reason : 0;
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_SCAN_DONE) {
        wifi_event_sta_scan_done_t *event = event_data;
        driver_event.kind = WIFI_DRIVER_EVENT_SCAN_DONE;
        driver_event.status = event != NULL ? event->status : 1U;
    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {
        driver_event.kind = WIFI_DRIVER_EVENT_GOT_IP;
    }
    /* Capture the operation identity before waking its Future: the runtime may
     * finish it and start another operation while this callback is still here. */
    wifi_lock();
    uint32_t observation_generation = event_base != WIFI_EVENT ? 0 :
        event_id == WIFI_EVENT_SCAN_DONE ? s_wifi_state.scan_generation :
        event_id == WIFI_EVENT_STA_DISCONNECTED ? s_wifi_state.connect_generation : 0;
    wifi_unlock();
    if (driver_event.kind != 0) {
        (void)wifi_publish_driver_event_from_callback(&driver_event);
    }
    if (event_base == WIFI_EVENT)
        esp32_mquickjs_wifi_watch_capture(event_id, event_data, observation_generation);
    atomic_fetch_sub_explicit(&s_wifi_state.callbacks_active, 1,
                              memory_order_release);
}

static void wifi_process_driver_event(
    const esp32_mquickjs_wifi_driver_event_t *driver_event)
{
    if (driver_event == NULL) {
        return;
    }
    switch (driver_event->kind) {
    case WIFI_DRIVER_EVENT_STARTED:
        wifi_lock();
        s_wifi_state.started = true;
        s_wifi_state.status.started = true;
        wifi_unlock();
        xEventGroupSetBits(s_wifi_state.event_group, WIFI_STARTED_BIT);
        break;

    case WIFI_DRIVER_EVENT_DISCONNECTED: {
        wifi_lock();
        bool intentional = s_wifi_state.connect_future_registered &&
            s_wifi_state.connection_future_operation == ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT;
        bool failed_connect = esp32_mquickjs_wifi_connection_reserved_locked() &&
            s_wifi_state.connection_future_operation == ESP32_MQUICKJS_WIFI_OPERATION_CONNECT &&
            s_wifi_state.connect_in_progress && !s_wifi_state.connect_draining;
        uint32_t generation = s_wifi_state.connect_generation;
        wifi_begin_disconnect_locked();
        s_wifi_state.disconnect_seen = true;
        s_wifi_state.disconnect_submitted = true;
        s_wifi_state.status.last_disconnect_reason = driver_event->reason;
        wifi_unlock();
        wifi_stop_connect_timeout_timer();
        xEventGroupClearBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT);
        (void)wifi_post_disconnect_fence();
        if (intentional || failed_connect) {
            if (failed_connect) xEventGroupSetBits(s_wifi_state.event_group, WIFI_FAILED_BIT);
            wifi_queue_connect_event(generation, intentional
                ? ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED
                : ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_FAILURE, driver_event->reason, NULL);
        }
        break;
    }

    case WIFI_DRIVER_EVENT_LINK_DRAINED: {
        wifi_lock();
        bool drained = false;
        if (s_wifi_state.connect_draining && s_wifi_state.disconnect_seen &&
            s_wifi_state.disconnect_fence_posted &&
            driver_event->generation == s_wifi_state.disconnect_epoch) {
            s_wifi_state.disconnect_fence_seen = true;
            drained = wifi_finish_disconnect_locked();
        }
        wifi_unlock();
        if (drained) xEventGroupSetBits(s_wifi_state.event_group, WIFI_LINK_DRAINED_BIT);
        break;
    }

    case WIFI_DRIVER_EVENT_SCAN_DONE: {
        esp32_mquickjs_wifi_scan_event_t scan_event = {0};
        bool should_queue_future;
        esp32_mquickjs_future_token_t scan_token;

        wifi_lock();
        if (!s_wifi_state.scan_in_progress) {
            wifi_unlock();
            break;
        }
        wifi_set_scanning_locked(false);
        s_wifi_state.scan_results_pending = !s_wifi_state.scan_results_consumed_early;
        s_wifi_state.scan_cleanup_error = ESP_OK;
        if (!s_wifi_state.scan_future_registered) s_wifi_state.scan_draining = true;
        scan_event.generation = s_wifi_state.scan_generation;
        scan_event.status = driver_event->status;
        should_queue_future = s_wifi_state.scan_future_registered;
        scan_token = s_wifi_state.scan_future_token;
        wifi_unlock();
        if (should_queue_future && s_wifi_state.scan_queue != NULL) {
            xQueueOverwrite(s_wifi_state.scan_queue, &scan_event);
            esp32_mquickjs_runtime_t *runtime = atomic_load_explicit(&s_wifi_runtime, memory_order_acquire);
            if (runtime != NULL) {
                (void)esp32_mquickjs_future_wake(runtime, scan_token);
                esp32_mquickjs_notify_activity(runtime);
            }
        }
        break;
    }

    case WIFI_DRIVER_EVENT_GOT_IP: {
        bool should_queue_connect_success;
        uint32_t connect_generation;
        esp32_mquickjs_wifi_link_snapshot_t link;

        wifi_lock();
        if (s_wifi_state.connect_draining || !s_wifi_state.connect_in_progress ||
            !esp32_mquickjs_wifi_connection_reserved_locked() ||
            s_wifi_state.connection_future_operation != ESP32_MQUICKJS_WIFI_OPERATION_CONNECT) {
            wifi_unlock();
            break;
        }
        s_wifi_state.status.connected = true;
        s_wifi_state.connect_in_progress = false;
        should_queue_connect_success =
            esp32_mquickjs_wifi_connection_reserved_locked();
        connect_generation = s_wifi_state.connect_generation;
        link = s_wifi_state.status.link;
        s_wifi_state.status.last_disconnect_reason = 0;
        wifi_unlock();
        wifi_stop_connect_timeout_timer();
        xEventGroupClearBits(s_wifi_state.event_group, WIFI_FAILED_BIT);
        xEventGroupSetBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT);
        if (should_queue_connect_success) {
            wifi_queue_connect_event(
                connect_generation,
                ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS, 0, &link);
        }
        break;
    }

    case WIFI_DRIVER_EVENT_CONNECT_TIMEOUT: {
        uint32_t generation = 0;
        bool should_timeout = false;

        wifi_lock();
        if (driver_event->generation == s_wifi_state.connect_generation &&
            esp32_mquickjs_wifi_connection_reserved_locked() &&
            s_wifi_state.connection_future_operation == ESP32_MQUICKJS_WIFI_OPERATION_CONNECT &&
            s_wifi_state.connect_in_progress) {
            generation = s_wifi_state.connect_generation;
            wifi_begin_disconnect_locked();
            should_timeout = true;
        }
        wifi_unlock();
        if (should_timeout) {
            xEventGroupClearBits(
                s_wifi_state.event_group,
                WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
            (void)wifi_request_disconnect();
            wifi_queue_connect_event(
                generation,
                ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT, 0, NULL);
        }
        break;
    }

    default:
        break;
    }
}

static bool wifi_driver_event_poller(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime,
                                     void *opaque)
{
    esp32_mquickjs_wifi_driver_event_t event;
    bool handled = false;

    (void)ctx;
    (void)runtime;
    (void)opaque;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
    handled = esp32_mquickjs_wifi_nan_service() || handled;
#endif
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
    handled = esp32_mquickjs_wifi_mesh_service() || handled;
    handled = esp32_mquickjs_wifi_mesh_poll_observations(false) || handled;
#endif
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    handled = esp32_mquickjs_wifi_radio_ap_deauth_poll(NULL) || handled;
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    handled = esp32_mquickjs_wifi_smartconfig_service() || handled;
    handled = esp32_mquickjs_wifi_wps_service() || handled;
#if CONFIG_ESP_WIFI_DPP_SUPPORT
    handled = esp32_mquickjs_wifi_dpp_service() || handled;
    handled = esp32_mquickjs_wifi_dpp_poll_observations(false) || handled;
#endif
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
    handled = esp32_mquickjs_wifi_wps_ap_service() || handled;
#endif
    handled = esp32_mquickjs_wifi_smartconfig_poll_observations(false) || handled;
    handled = esp32_mquickjs_wifi_wps_poll_observations(false) || handled;
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
    handled = esp32_mquickjs_wifi_wps_ap_poll_observations(false) || handled;
#endif
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    handled = esp32_mquickjs_wifi_ftm_service() || handled;
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    handled = esp32_mquickjs_wifi_twt_service() || handled;
#endif
#if CONFIG_ESP_WIFI_RRM_SUPPORT
    handled = esp32_mquickjs_wifi_rrm_service() || handled;
    handled = esp32_mquickjs_wifi_rrm_poll_observations(false) || handled;
#endif
    while (s_wifi_state.driver_event_queue != NULL &&
           xQueueReceive(s_wifi_state.driver_event_queue, &event, 0) ==
               pdTRUE) {
        wifi_process_driver_event(&event);
        handled = true;
    }
    uint32_t timeout_generation = atomic_exchange_explicit(
        &s_wifi_timeout_pending, 0, memory_order_acq_rel);
    if (timeout_generation != 0U) {
        event = (esp32_mquickjs_wifi_driver_event_t){
            .kind = WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,
            .generation = timeout_generation,
        };
        wifi_process_driver_event(&event);
        handled = true;
    }
    wifi_lock();
    bool drain_scan = s_wifi_state.scan_draining &&
        s_wifi_state.scan_cleanup_error == ESP_OK;
    wifi_unlock();
    if (drain_scan) (void)esp32_mquickjs_wifi_drain_scan();
    wifi_release_radio_operation();
    return handled;
}

/* Native identities are boot-scoped even after all helper allocations exit. */
static void wifi_reset_helper_state(void)
{
    uint32_t scan_generation = s_wifi_state.scan_generation;
    uint32_t connect_generation = s_wifi_state.connect_generation;
    uint32_t disconnect_epoch = s_wifi_state.disconnect_epoch;
    bool exhausted = s_wifi_state.disconnect_epoch_exhausted;
    memset(&s_wifi_state, 0, sizeof(s_wifi_state));
    s_wifi_state.scan_generation = scan_generation;
    s_wifi_state.connect_generation = connect_generation;
    s_wifi_state.disconnect_epoch = disconnect_epoch;
    s_wifi_state.disconnect_epoch_exhausted = exhausted;
}

static esp_err_t wifi_prepare_station_netif(void)
{
    if (s_wifi_state.sta_netif != NULL || s_wifi_state.sta_detach_error != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    s_wifi_setup_stage = "netif-create";
    esp_netif_config_t config = ESP_NETIF_DEFAULT_WIFI_STA();
    s_wifi_state.sta_netif = esp_netif_new(&config);
    if (s_wifi_state.sta_netif == NULL) return ESP_ERR_NO_MEM;
    s_wifi_setup_stage = "netif-attach";
    esp_err_t err = esp_netif_attach_wifi_station(s_wifi_state.sta_netif);
    if (err != ESP_OK) return err;
    s_wifi_setup_stage = "netif-handlers";
    return esp_wifi_set_default_wifi_sta_handlers();
}

static esp_err_t wifi_start_existing_station_netif(void)
{
    esp_netif_t *netif = s_wifi_state.sta_netif;
    if (netif == NULL) return ESP_ERR_INVALID_ARG;
    /* Radio's START fence has completed and our STA lease prevents STOP.
     * If START reached the newly attached default handler, netif_add already
     * installed input. Adding the same lwIP netif twice corrupts its list. */
    struct netif *stack = esp_netif_get_netif_impl(netif);
    if (stack != NULL && stack->input != NULL) return ESP_OK;

    /* Monitor/Raw TX/ESP-NOW can start STA before an IP interface exists.
     * Replay only the SDK Station start work, without posting a synthetic
     * Wi-Fi event or changing the shared Radio. RX attaches on CONNECTED. */
    uint8_t mac[6];
    s_wifi_setup_stage = "netif-start-mac";
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK) return err;
    s_wifi_setup_stage = "netif-start-buffer-callbacks";
    err = esp_wifi_internal_reg_netstack_buf_cb(
        esp_netif_netstack_buf_ref, esp_netif_netstack_buf_free);
    if (err != ESP_OK) return err;
    s_wifi_setup_stage = "netif-start-set-mac";
    err = esp_netif_set_mac(netif, mac);
    if (err != ESP_OK) return err;
    s_wifi_setup_stage = "netif-start-stack";
    return esp_netif_start(netif);
}

static esp_err_t wifi_retire_station_netif(void)
{
    return esp32_mquickjs_wifi_netif_retire(&s_wifi_state.sta_netif,
                                          &s_wifi_state.sta_detach_error);
}

static esp_err_t wifi_cleanup_helper(bool finish_lifecycle)
{
    esp32_mquickjs_wifi_runtime_resources_t resources;
    esp_err_t err;

    /* The lock is created first and released last. A failed suffix keeps all
     * callback-visible storage and the exact remaining native handles alive. */
    if (s_wifi_state.sta_detach_error != ESP_OK) {
        s_wifi_state.cleanup_stage = "netif-detach";
        err = s_wifi_state.sta_detach_error;
        goto pending;
    }
    if (s_wifi_state.lock == NULL) goto finish_radio;

    if (s_wifi_state.connect_timeout_timer != NULL) {
        if (!s_wifi_state.cleanup_timer_stopped) {
            s_wifi_state.cleanup_stage = "timer-stop";
            err = esp_timer_stop_blocking(s_wifi_state.connect_timeout_timer,
                                         esp32_mquickjs_wifi_wait_remaining(pdMS_TO_TICKS(1000)));
            if (err != ESP_OK) goto pending;
            s_wifi_state.cleanup_timer_stopped = true;
        }
        s_wifi_state.cleanup_stage = "timer-delete";
        err = esp_timer_delete(s_wifi_state.connect_timeout_timer);
        if (err != ESP_OK) goto pending;
        s_wifi_state.connect_timeout_timer = NULL;
    }
    if (s_wifi_state.control_event_instance != NULL) {
        s_wifi_state.cleanup_stage = "control-unregister";
        err = esp_event_handler_instance_unregister(
            ESP32QJS_WIFI_CONTROL_EVENT, WIFI_CONTROL_LINK_DRAINED,
            s_wifi_state.control_event_instance);
        if (err != ESP_OK) goto pending;
        s_wifi_state.control_event_instance = NULL;
    }
    if (s_wifi_state.ip_event_instance != NULL) {
        s_wifi_state.cleanup_stage = "ip-unregister";
        err = esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_STA_GOT_IP,
            s_wifi_state.ip_event_instance);
        if (err != ESP_OK) goto pending;
        s_wifi_state.ip_event_instance = NULL;
    }
    if (s_wifi_state.wifi_scan_event_instance != NULL) {
        s_wifi_state.cleanup_stage = "scan-unregister";
        err = esp_event_handler_instance_unregister(
            WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
            s_wifi_state.wifi_scan_event_instance);
        if (err != ESP_OK) goto pending;
        s_wifi_state.wifi_scan_event_instance = NULL;
    }
    if (s_wifi_state.wifi_disconnect_event_instance != NULL) {
        s_wifi_state.cleanup_stage = "disconnect-unregister";
        err = esp_event_handler_instance_unregister(
            WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
            s_wifi_state.wifi_disconnect_event_instance);
        if (err != ESP_OK) goto pending;
        s_wifi_state.wifi_disconnect_event_instance = NULL;
    }
    if (s_wifi_state.wifi_connected_event_instance != NULL) {
        s_wifi_state.cleanup_stage = "connected-unregister";
        err = esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
            s_wifi_state.wifi_connected_event_instance);
        if (err != ESP_OK) goto pending;
        s_wifi_state.wifi_connected_event_instance = NULL;
    }
    if (s_wifi_state.wifi_start_event_instance != NULL) {
        s_wifi_state.cleanup_stage = "start-unregister";
        err = esp_event_handler_instance_unregister(
            WIFI_EVENT, WIFI_EVENT_STA_START,
            s_wifi_state.wifi_start_event_instance);
        if (err != ESP_OK) goto pending;
        s_wifi_state.wifi_start_event_instance = NULL;
    }
    while (atomic_load_explicit(&s_wifi_state.callbacks_active,
                                memory_order_acquire) != 0U) {
        s_wifi_state.cleanup_stage = "callbacks-drain";
        if (esp32_mquickjs_wifi_wait_remaining(portMAX_DELAY) == 0) {
            err = ESP_ERR_TIMEOUT;
            goto pending;
        }
        vTaskDelay(1);
    }
    atomic_store_explicit(&s_wifi_watch_control_mask, 0, memory_order_release);
    if (!s_wifi_state.cleanup_radio_released) {
        s_wifi_state.cleanup_stage = "radio-release";
        err = esp32_mquickjs_wifi_radio_release_and_stop_idle(&s_wifi_state.radio_lease);
        if (err != ESP_OK) goto pending;
        s_wifi_state.cleanup_radio_released = true;
    }
    if (finish_lifecycle && s_wifi_lifecycle.identity != 0U) {
        s_wifi_state.cleanup_stage = "radio-stop";
        err = esp32_mquickjs_wifi_radio_quiesce_lifecycle(&s_wifi_lifecycle);
        if (err != ESP_OK) goto pending;
    }
    s_wifi_state.cleanup_stage = "netif-detach";
    err = wifi_retire_station_netif();
    if (err != ESP_OK) goto pending;
    resources = (esp32_mquickjs_wifi_runtime_resources_t){
        .lock = s_wifi_state.lock,
        .event_group = s_wifi_state.event_group,
        .scan_queue = s_wifi_state.scan_queue,
        .connect_queue = s_wifi_state.connect_queue,
        .driver_event_queue = s_wifi_state.driver_event_queue,
    };
    esp32_mquickjs_wifi_runtime_resources_deinit(
        &resources, &s_wifi_runtime_resource_ops);
    wifi_reset_helper_state();
finish_radio:
    if (finish_lifecycle && s_wifi_lifecycle.identity != 0U) {
        s_wifi_state.cleanup_stage = "radio-stop";
        err = esp32_mquickjs_wifi_radio_finish_lifecycle(&s_wifi_lifecycle, false);
        if (err != ESP_OK) goto pending;
        s_wifi_state.cleanup_stage = NULL;
        s_wifi_state.cleanup_error = ESP_OK;
    }
    s_wifi_state.runtime_cleanup_pending = false;
    return ESP_OK;

pending:
    s_wifi_state.cleanup_error = err;
    ESP_LOGE(TAG, "Wi-Fi cleanup pending at %s: %s",
             s_wifi_state.cleanup_stage, esp_err_to_name(err));
    return err;
}

static esp_err_t wifi_cleanup_failed_init(void)
{
    return wifi_cleanup_helper(true);
}

static esp_err_t wifi_init_helper(bool acquire_lease)
{
    esp32_mquickjs_wifi_runtime_resources_t resources = {0};
    esp32_mquickjs_wifi_radio_status_t radio_status;
    esp_err_t err;

    if (s_wifi_state.runtime_cleanup_pending || s_wifi_state.cleanup_stage != NULL) {
        return s_wifi_state.cleanup_error != ESP_OK
            ? s_wifi_state.cleanup_error : ESP_ERR_INVALID_STATE;
    }
    if (s_wifi_state.initialized) {
        return ESP_OK;
    }

    s_wifi_setup_stage = "runtime-resources";
    s_wifi_setup_error = ESP_OK;
    wifi_reset_helper_state();
    atomic_init(&s_wifi_state.callbacks_active, 0);
    atomic_init(&s_wifi_state.dropped_driver_events, 0);
    atomic_store(&s_wifi_timeout_pending, 0);
    atomic_store(&s_wifi_timeout_armed_generation, 0);
    if (!esp32_mquickjs_wifi_runtime_resources_init(
            &resources, &s_wifi_runtime_resource_ops,
            WIFI_SCAN_EVENT_QUEUE_LEN,
            sizeof(esp32_mquickjs_wifi_scan_event_t),
            WIFI_CONNECT_EVENT_QUEUE_LEN,
            sizeof(esp32_mquickjs_wifi_connect_event_t),
            WIFI_DRIVER_EVENT_QUEUE_LEN,
            sizeof(esp32_mquickjs_wifi_driver_event_t))) {
        s_wifi_setup_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    s_wifi_state.lock = resources.lock;
    s_wifi_state.event_group = resources.event_group;
    s_wifi_state.scan_queue = resources.scan_queue;
    s_wifi_state.connect_queue = resources.connect_queue;
    s_wifi_state.driver_event_queue = resources.driver_event_queue;

    if (acquire_lease) {
        s_wifi_setup_stage = "radio-acquire";
        err = esp32_mquickjs_wifi_radio_acquire(
            ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,
            WIFI_MODE_STA, &s_wifi_state.radio_lease);
        if (err != ESP_OK) goto fail;
    }
    s_wifi_setup_stage = "net-init";
    err = esp32_mquickjs_net_ensure_initialized();
    if (err != ESP_OK) goto fail;
    err = wifi_prepare_station_netif();
    if (err != ESP_OK) goto fail;
    s_wifi_setup_stage = "event-handlers";

    atomic_fetch_or_explicit(&s_wifi_watch_control_mask, 1U << WIFI_EVENT_STA_START, memory_order_release);
    err = esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_STA_START, wifi_event_handler, NULL,
        &s_wifi_state.wifi_start_event_instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register WIFI_EVENT start handler failed: %s",
                 esp_err_to_name(err));
        goto fail;
    }
    atomic_fetch_or_explicit(&s_wifi_watch_control_mask, 1U << WIFI_EVENT_STA_CONNECTED, memory_order_release);
    err = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
        wifi_event_handler, NULL, &s_wifi_state.wifi_connected_event_instance);
    if (err != ESP_OK) goto fail;
    atomic_fetch_or_explicit(&s_wifi_watch_control_mask, 1U << WIFI_EVENT_STA_DISCONNECTED, memory_order_release);
    err = esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_handler, NULL,
        &s_wifi_state.wifi_disconnect_event_instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register WIFI_EVENT disconnect handler failed: %s",
                 esp_err_to_name(err));
        goto fail;
    }
    atomic_fetch_or_explicit(&s_wifi_watch_control_mask, 1U << WIFI_EVENT_SCAN_DONE, memory_order_release);
    err = esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_event_handler, NULL,
        &s_wifi_state.wifi_scan_event_instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register WIFI_EVENT scan handler failed: %s",
                 esp_err_to_name(err));
        goto fail;
    }
    err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL,
        &s_wifi_state.ip_event_instance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register IP_EVENT handler failed: %s",
                 esp_err_to_name(err));
        goto fail;
    }
    err = esp_event_handler_instance_register(
        ESP32QJS_WIFI_CONTROL_EVENT, WIFI_CONTROL_LINK_DRAINED,
        wifi_event_handler, NULL, &s_wifi_state.control_event_instance);
    if (err != ESP_OK) goto fail;
    {
        esp_timer_create_args_t timer_args = {
            .callback = wifi_connect_timeout_cb,
            .name = "wifi_connect_timeout",
        };
        s_wifi_setup_stage = "timer-create";
        err = esp_timer_create(
            &timer_args, &s_wifi_state.connect_timeout_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "create Wi-Fi connect timeout timer failed: %s",
                     esp_err_to_name(err));
            goto fail;
        }
    }

    wifi_lock();
    s_wifi_state.initialized = true;
    s_wifi_state.status.initialized = true;
    s_wifi_state.status.started = false;
    s_wifi_state.status.connected = false;
    s_wifi_state.status.scanning = false;
    s_wifi_state.status.last_disconnect_reason = 0;
    wifi_unlock();

    if (esp32_mquickjs_wifi_radio_get_status(&radio_status) == ESP_OK &&
        radio_status.started && (radio_status.mode & WIFI_MODE_STA) != 0) {
        /* A concurrent owner may still be waiting for the real START event.
         * Join that serialized fence before inspecting/starting its netif.
         * Configuration-only helpers are created under a stopped lifecycle. */
        s_wifi_setup_stage = "netif-start-radio-fence";
        err = esp32_mquickjs_wifi_radio_ensure_started(&s_wifi_state.radio_lease);
        if (err != ESP_OK) goto fail;
        err = wifi_start_existing_station_netif();
        if (err != ESP_OK) goto fail;
        wifi_lock();
        s_wifi_state.started = true;
        s_wifi_state.status.started = true;
        wifi_unlock();
        xEventGroupSetBits(s_wifi_state.event_group, WIFI_STARTED_BIT);
    }

    s_wifi_setup_stage = NULL;
    s_wifi_setup_error = ESP_OK;
    return ESP_OK;

fail:
    s_wifi_setup_error = err;
    (void)wifi_cleanup_helper(acquire_lease);
    return err;
}

static esp_err_t wifi_init_once(void)
{
    return wifi_init_helper(true);
}

static bool wifi_helpers_idle(bool allow_connected)
{
    wifi_lock();
    bool busy = (!allow_connected && s_wifi_state.status.connected) || s_wifi_state.connect_in_progress ||
        s_wifi_state.connect_draining || s_wifi_state.connect_start_active ||
        s_wifi_state.disconnect_active || s_wifi_state.scan_in_progress ||
        s_wifi_state.scan_draining || s_wifi_state.scan_results_pending ||
        s_wifi_state.scan_start_active || s_wifi_state.scan_stop_active ||
        esp32_mquickjs_wifi_connection_reserved_locked() || s_wifi_state.scan_future_registered;
    wifi_unlock();
    return !busy;
}

bool esp32_mquickjs_wifi_raw_tx_ap_ready(void)
{
    return !s_wifi_configuration_cleanup && !s_wifi_ap_stop_cleanup && s_wifi_lifecycle.identity == 0U &&
        !s_wifi_state.runtime_cleanup_pending && s_wifi_state.cleanup_stage == NULL && wifi_helpers_idle(false);
}

esp_err_t esp32_mquickjs_wifi_prepare_for_configuration(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    esp_err_t err = esp32_mquickjs_wifi_radio_check_stopped_lifecycle(token, false);
    if (err != ESP_OK) return err;
    if (s_wifi_state.radio_lease.acquired || !wifi_helpers_idle(false)) return ESP_ERR_INVALID_STATE;
    err = wifi_init_helper(false);
    if (err != ESP_OK) return err;
    wifi_lock();
    s_wifi_state.started = false;
    s_wifi_state.status.started = false;
    wifi_unlock();
    xEventGroupClearBits(s_wifi_state.event_group, WIFI_STARTED_BIT);
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_retire_for_configuration(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    esp_err_t err = esp32_mquickjs_wifi_radio_check_stopped_lifecycle(token, true);
    if (err != ESP_OK) return err;
    if (s_wifi_state.radio_lease.acquired || !wifi_helpers_idle(false)) return ESP_ERR_INVALID_STATE;
    return wifi_cleanup_helper(false);
}

esp_err_t esp32_mquickjs_wifi_retire_for_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    esp_err_t err = esp32_mquickjs_wifi_radio_check_stopped_recovery(token);
    if (err != ESP_OK) return err;
    if (s_wifi_state.radio_lease.acquired || !wifi_helpers_idle(false)) return ESP_ERR_INVALID_STATE;
    return wifi_cleanup_helper(false);
}

static EventBits_t wifi_wait_for_bits(EventBits_t bits_to_wait_for,
                                      bool clear_on_exit,
                                      uint32_t timeout_ms,
                                      bool *out_interrupted)
{
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    esp32_mquickjs_native_wait_t wait;
    uint32_t remaining_ms = timeout_ms;
    EventBits_t result = 0;

    if (out_interrupted != NULL) {
        *out_interrupted = false;
    }
    esp32_mquickjs_native_wait_begin(runtime, &wait);
    for (;;) {
        uint32_t slice_ms = remaining_ms > ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS
                                ? ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS
                                : remaining_ms;
        TickType_t wait_ticks = pdMS_TO_TICKS(slice_ms);
        EventBits_t bits;

        if (!esp32_mquickjs_cooperate(runtime)) {
            if (out_interrupted != NULL) {
                *out_interrupted = true;
            }
            break;
        }
        if (slice_ms > 0 && wait_ticks == 0) {
            wait_ticks = 1;
        }
        (void)wifi_driver_event_poller(NULL, runtime, NULL);
        bits = xEventGroupWaitBits(s_wifi_state.event_group,
                                   bits_to_wait_for,
                                   clear_on_exit ? pdTRUE : pdFALSE,
                                   pdFALSE,
                                   wait_ticks);
        (void)wifi_driver_event_poller(NULL, runtime, NULL);
        bits |= xEventGroupGetBits(s_wifi_state.event_group);
        if (!esp32_mquickjs_cooperate(runtime)) {
            if (out_interrupted != NULL) {
                *out_interrupted = true;
            }
            break;
        }
        if ((bits & bits_to_wait_for) != 0 || remaining_ms <= slice_ms) {
            result = bits;
            break;
        }
        remaining_ms -= slice_ms;
    }
    esp32_mquickjs_native_wait_end(runtime, &wait);
    return result;
}

esp_err_t esp32_mquickjs_wifi_ensure_started(void)
{
    esp32_mquickjs_wifi_radio_status_t radio_status;
    ESP_RETURN_ON_ERROR(wifi_init_once(), TAG, "wifi_init_once() failed");
    /* A cached STA_START bit must not bypass exact lease/fault validation.
     * Radio now waits for native START and its default-loop marker itself. */
    ESP_RETURN_ON_ERROR(esp32_mquickjs_wifi_radio_ensure_started(&s_wifi_state.radio_lease),
                        TAG, "start Wi-Fi radio failed");
    ESP_RETURN_ON_ERROR(esp32_mquickjs_wifi_radio_get_status(&radio_status),
                        TAG, "read Wi-Fi radio status failed");
    if (!radio_status.started || (radio_status.mode & WIFI_MODE_STA) == 0)
        return ESP_ERR_INVALID_STATE;
    /* The early helper snapshot may have seen another owner's START still in
     * progress. Its event can precede helper attachment and esp_wifi_start's
     * return, so always reconcile the IP interface after joining the fence. */
    esp_err_t err = wifi_start_existing_station_netif();
    if (err != ESP_OK) {
        s_wifi_setup_error = err;
        (void)wifi_cleanup_failed_init();
        return err;
    }
    s_wifi_setup_stage = NULL;
    s_wifi_setup_error = ESP_OK;
    wifi_lock();
    s_wifi_state.started = true;
    s_wifi_state.status.started = true;
    wifi_unlock();
    xEventGroupSetBits(s_wifi_state.event_group, WIFI_STARTED_BIT);
    return ESP_OK;
}

static const char *wifi_authmode_to_string(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return "open";
    case WIFI_AUTH_WEP:
        return "wep";
    case WIFI_AUTH_WPA_PSK:
        return "wpa";
    case WIFI_AUTH_WPA2_PSK:
        return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "wpa/wpa2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "wpa2-enterprise";
    case WIFI_AUTH_WPA3_PSK:
        return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "wpa2/wpa3";
    case WIFI_AUTH_WAPI_PSK:
        return "wapi";
    case WIFI_AUTH_OWE:
        return "owe";
    case WIFI_AUTH_WPA3_ENT_192:
        return "wpa3-ent-192";
    default:
        return "unknown";
    }
}

static const char *wifi_reason_to_string(int32_t reason)
{
    switch (reason) {
    case 0:
        return "none";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "beacon-timeout";
    case WIFI_REASON_NO_AP_FOUND:
        return "no-ap-found";
    case WIFI_REASON_AUTH_FAIL:
        return "auth-fail";
    case WIFI_REASON_ASSOC_FAIL:
        return "assoc-fail";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake-timeout";
    case WIFI_REASON_CONNECTION_FAIL:
        return "connection-fail";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "no-ap-compatible-security";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "no-ap-authmode-threshold";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "no-ap-rssi-threshold";
    default:
        return "unknown";
    }
}

const char *esp32_mquickjs_wifi_reason_to_string(int32_t reason)
{
    return wifi_reason_to_string(reason);
}

static const char *wifi_radio_mode_to_string(wifi_mode_t mode)
{
    switch (mode) {
    case WIFI_MODE_STA:
        return "station";
    case WIFI_MODE_AP:
        return "softAP";
    case WIFI_MODE_APSTA:
        return "station+softAP";
    case WIFI_MODE_NAN:
        return "nan";
    case WIFI_MODE_NULL:
    default:
        return "off";
    }
}

static const char *wifi_power_save_to_string(wifi_ps_type_t mode)
{
    switch (mode) {
    case WIFI_PS_NONE:
        return "none";
    case WIFI_PS_MIN_MODEM:
        return "minimum";
    case WIFI_PS_MAX_MODEM:
        return "maximum";
    default:
        return NULL;
    }
}

static const char *wifi_negotiated_phy_name(wifi_phy_mode_t phy)
{
    switch (phy) {
    case WIFI_PHY_MODE_LR: return "lr";
    case WIFI_PHY_MODE_11B: return "11b";
    case WIFI_PHY_MODE_11G: return "11g";
    case WIFI_PHY_MODE_11A: return "11a";
    case WIFI_PHY_MODE_HT20: return "ht20";
    case WIFI_PHY_MODE_HT40: return "ht40";
    case WIFI_PHY_MODE_HE20: return "he20";
    case WIFI_PHY_MODE_VHT20: return "vht20";
    default: return NULL;
    }
}

static bool wifi_link_snapshot_valid(const esp32_mquickjs_wifi_link_snapshot_t *link)
{
    static const uint8_t zero_mac[6] = {0};
    return link != NULL && link->valid && link->ssid_len > 0 &&
        link->ssid_len <= sizeof(link->ssid) && link->channel != 0 &&
        (link->bssid[0] & 1U) == 0 && memcmp(link->bssid, zero_mac, 6) != 0;
}

/* obj is a movable-GC root. Native identity belongs to the caller's value
 * snapshot; the optional RF samples are taken now and can be unavailable. */
static bool wifi_set_link_properties(JSContext *ctx, JSValue *obj,
    const esp32_mquickjs_wifi_link_snapshot_t *link)
{
    bool valid = wifi_link_snapshot_valid(link);
    esp32_mquickjs_wifi_link_sample_t sample = {0};
    char bssid[18];
    if (valid) {
        snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
            link->bssid[0], link->bssid[1], link->bssid[2],
            link->bssid[3], link->bssid[4], link->bssid[5]);
        (void)esp32_mquickjs_wifi_radio_sample_station_link(
            &s_wifi_state.radio_lease, link->bssid, link->channel, &sample);
    }
    const char *phy = sample.phy_valid ? wifi_negotiated_phy_name(sample.phy) : NULL;
    return esp32_mquickjs_set_property_ref(ctx, obj, "bssid",
            valid ? JS_NewString(ctx, bssid) : JS_NULL) &&
        esp32_mquickjs_set_property_ref(ctx, obj, "channel",
            valid ? JS_NewUint32(ctx, link->channel) : JS_NULL) &&
        esp32_mquickjs_set_property_ref(ctx, obj, "aid",
            valid && link->aid != 0 ? JS_NewUint32(ctx, link->aid) : JS_NULL) &&
        esp32_mquickjs_set_property_ref(ctx, obj, "rssi",
            sample.rssi_valid ? JS_NewInt32(ctx, sample.rssi) : JS_NULL) &&
        esp32_mquickjs_set_property_ref(ctx, obj, "negotiatedPhy",
            phy != NULL ? JS_NewString(ctx, phy) : JS_NULL);
}

JSValue esp32_mquickjs_wifi_make_connect_result(JSContext *ctx,
    const esp32_mquickjs_wifi_link_snapshot_t *link, double elapsed_ms)
{
    if (!wifi_link_snapshot_valid(link)) {
        return esp32_mquickjs_wifi_throw_operation_error(ctx,
            "WIFI_CONNECT_FAILED", "wifi.connect", ESP_ERR_INVALID_RESPONSE,
            -1, UINT32_MAX);
    }
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "connected", JS_TRUE) ||
        !esp32_mquickjs_wifi_set_ssid_properties(ctx, result, link->ssid, link->ssid_len) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "elapsedMs", JS_NewFloat64(ctx, elapsed_ms)) ||
        !wifi_set_link_properties(ctx, result, link)) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static JSValue wifi_make_configuration_status(JSContext *ctx,
    const esp32_mquickjs_wifi_radio_config_result_t *configuration)
{
    if (configuration->stage == NULL) return JS_NULL;
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "stage", JS_NewString(ctx, configuration->stage)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "error", JS_NewInt32(ctx, configuration->error)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "mutationAttempted", JS_NewBool(configuration->mutation_attempted)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "rollbackAttempted", JS_NewBool(configuration->rollback_attempted)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "rollbackComplete", JS_NewBool(configuration->rollback_complete)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "rollbackStage",
            configuration->rollback_stage != NULL ? JS_NewString(ctx, configuration->rollback_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "rollbackError", JS_NewInt32(ctx, configuration->rollback_error)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "persistentMutationPossible",
            JS_NewBool(configuration->persistent_mutation_possible))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

/* Shared by wifi.status().radio and wifi.driver.status(). Native Radio fields
 * are captured together. Module diagnostics are separate observations and do
 * not reserve admission for a later mutation. No Station/netif initialization. */
static JSValue wifi_make_radio_status(JSContext *ctx,
    const esp32_mquickjs_wifi_radio_status_t *snapshot)
{
    const esp32_mquickjs_wifi_radio_status_t radio_status = *snapshot;
    JSGCRef radio_ref, clients_ref;
    JSValue *radio_obj = JS_PushGCRef(ctx, &radio_ref);
    JSValue *clients_obj = JS_PushGCRef(ctx, &clients_ref);
    uint32_t client_total = 0;
    *radio_obj = JS_NewObject(ctx);
    *clients_obj = JS_NewObject(ctx);
    if (JS_IsException(*radio_obj) || JS_IsException(*clients_obj)) goto fail;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT; ++i)
        client_total += radio_status.clients[i];
    if (!esp32_mquickjs_set_property_ref(ctx, radio_obj, "configuration",
            wifi_make_configuration_status(ctx, &radio_status.configuration))) goto fail;
    if (!esp32_mquickjs_set_property_ref(ctx, radio_obj, "activation",
            wifi_make_configuration_status(ctx, &radio_status.activation))) goto fail;

    if (!esp32_mquickjs_set_property_ref(ctx, radio_obj, "channelObservationError",
            radio_status.channel_observation_error != ESP_OK
                ? JS_NewInt32(ctx, radio_status.channel_observation_error) : JS_NULL)) goto fail;

    if (!esp32_mquickjs_set_property_ref(ctx, radio_obj, "lifecycleActive",
            JS_NewBool(radio_status.lifecycle_active))) goto fail;
    if (!esp32_mquickjs_set_property_ref(ctx, radio_obj, "eventPhase",
            JS_NewString(ctx, radio_status.event_phase == 1 ? "start" :
                radio_status.event_phase == 2 ? "stop" : radio_status.event_phase == 3 ? "ap-stop" : radio_status.event_phase == 4 ? "ap-start" : radio_status.event_phase == 5 ? "restart" : radio_status.event_phase == 6 ? "sta-start" : "idle")) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "eventIdentity", JS_NewUint32(ctx, radio_status.event_identity)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "eventExpectedMask", JS_NewUint32(ctx, radio_status.event_expected)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "eventSeenMask", JS_NewUint32(ctx, radio_status.event_seen)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "eventStoppedMask", JS_NewUint32(ctx, radio_status.event_stopped)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "eventLiveMask", JS_NewUint32(ctx, radio_status.event_live)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "eventFencePending", JS_NewBool(radio_status.event_fence_pending))) goto fail;


    if (!esp32_mquickjs_set_property_ref(ctx, radio_obj, "activeOperations",
            JS_NewUint32(ctx, radio_status.active_operations))) goto fail;

    if (!esp32_mquickjs_set_property_ref(ctx, radio_obj, "action",
            esp32_mquickjs_wifi_action_status(ctx))) goto fail;
    if (!esp32_mquickjs_set_property_ref(ctx, radio_obj, "rawTx",
            esp32_mquickjs_wifi_raw_tx_status(ctx))) goto fail;

    if (!esp32_mquickjs_set_property_ref(ctx, clients_obj, "wifiAccessPoint",
            JS_NewUint32(ctx, radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP]))) goto fail;

    if (!esp32_mquickjs_set_property_ref(ctx, clients_obj, "application",
            JS_NewUint32(ctx, radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION]))) goto fail;

    if (!esp32_mquickjs_set_property_ref(ctx, clients_obj, "wifiMonitor",
            JS_NewUint32(ctx, radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MONITOR]))) goto fail;

    if (!esp32_mquickjs_set_property_ref(ctx, clients_obj, "wifiRawTx",
            JS_NewUint32(ctx, radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX]))) goto fail;

    if (!esp32_mquickjs_set_property_ref(ctx, clients_obj, "wifiAction",
            JS_NewUint32(ctx, radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION]))) goto fail;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
    if (!esp32_mquickjs_set_property_ref(ctx, clients_obj, "wifiNan",
        JS_NewUint32(ctx, radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_NAN]))) goto fail;
#endif
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
    if (!esp32_mquickjs_set_property_ref(ctx, clients_obj, "wifiMesh",
        JS_NewUint32(ctx, radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MESH]))) goto fail;
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (!esp32_mquickjs_set_property_ref(ctx, clients_obj, "wifiTwt",
        JS_NewUint32(ctx, radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT]))) goto fail;
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (!esp32_mquickjs_set_property_ref(ctx, clients_obj, "wifiFtm",
        JS_NewUint32(ctx, radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_FTM]))) goto fail;
#endif
    if (!esp32_mquickjs_set_property_ref(ctx, clients_obj, "wifiVendorIe",
            JS_NewUint32(ctx, radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE]))) goto fail;

    if (!esp32_mquickjs_set_property_ref(ctx, clients_obj, "total",
                                         JS_NewUint32(ctx, client_total)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, clients_obj, "wifiStation",
            JS_NewUint32(
                ctx, radio_status.clients[
                         ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA])) ||
        !esp32_mquickjs_set_property_ref(
            ctx, clients_obj, "espNow",
            JS_NewUint32(
                ctx, radio_status.clients[
                         ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW])) ||
        !esp32_mquickjs_set_property_ref(
            ctx, clients_obj, "wifiCsi",
            JS_NewUint32(
                ctx, radio_status.clients[
                         ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI])) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "generation",
                                         JS_NewUint32(
                                             ctx, radio_status.generation)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "initialized",
                                         JS_NewBool(
                                             radio_status.initialized)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "driverState",
            JS_NewString(ctx, esp32_mquickjs_wifi_radio_driver_state_name(radio_status.driver_state))) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "requestedMode",
            JS_NewString(ctx, wifi_radio_mode_to_string(radio_status.requested_mode))) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "fixedChannelOwners",
            JS_NewUint32(ctx, radio_status.fixed_channel_owners)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "promiscuousOwners",
            JS_NewUint32(ctx, radio_status.promiscuous_owners)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "promiscuousIdentityExhausted",
            JS_NewBool(radio_status.promiscuous_identity_exhausted)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "wakeLocks",
            JS_NewUint32(ctx, radio_status.wake_locks)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "wakeLockError",
            radio_status.wake_lock_error != ESP_OK ? JS_NewInt32(ctx, radio_status.wake_lock_error) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "conflictedChannelOwners",
            JS_NewUint32(ctx, radio_status.conflicted_channel_owners)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "cleanupStage",
            radio_status.cleanup_stage != NULL ? JS_NewString(ctx, radio_status.cleanup_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "cleanupError",
            radio_status.cleanup_stage != NULL ? JS_NewInt32(ctx, radio_status.cleanup_error) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "driverOwned",
                                         JS_NewBool(radio_status.driver_owned)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "restartSnapshotBytes",
            JS_NewUint32(ctx, esp32_mquickjs_wifi_radio_restart_snapshot_bytes())) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "policies",
            esp32_mquickjs_wifi_policies_to_js(ctx)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "rssiThreshold",
            esp32_mquickjs_wifi_rssi_request_to_js(ctx)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "connectionlessInterval",
            esp32_mquickjs_wifi_interval_to_js(ctx)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "restartRequired",
                                         JS_NewBool(radio_status.restart_required)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "faultStage",
            radio_status.fault_stage != NULL
                ? JS_NewString(ctx, radio_status.fault_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "faultError",
            radio_status.fault_stage != NULL
                ? JS_NewInt32(ctx, radio_status.fault_error) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "starting",
                                         JS_NewBool(radio_status.starting)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "started",
                                         JS_NewBool(radio_status.started)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, radio_obj, "mode",
            JS_NewString(ctx, wifi_radio_mode_to_string(radio_status.mode))) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "storage",
            radio_status.initialized ? JS_NewString(ctx, radio_status.storage == WIFI_STORAGE_RAM ? "ram" : "flash") : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, radio_obj, "channel",
            radio_status.primary_channel > 0
                ? JS_NewUint32(ctx, radio_status.primary_channel)
                : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, radio_obj, "channelGeneration",
            JS_NewUint32(ctx, radio_status.channel_generation)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, radio_obj, "maxTxPowerDbm",
            radio_status.max_tx_power_available
                ? JS_NewFloat64(
                      ctx,
                      (double)radio_status.max_tx_power_quarter_dbm / 4.0)
                : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, radio_obj, "powerSave",
            radio_status.power_save_available &&
                    wifi_power_save_to_string(radio_status.power_save) != NULL
                ? JS_NewString(
                      ctx, wifi_power_save_to_string(radio_status.power_save))
                : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "clients",
                                         *clients_obj)) goto fail;
    JS_PopGCRef(ctx, &clients_ref);
    return JS_PopGCRef(ctx, &radio_ref);
fail:
    JS_PopGCRef(ctx, &clients_ref);
    JS_PopGCRef(ctx, &radio_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_driver_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.driver.status() takes no arguments");
    esp32_mquickjs_wifi_radio_status_t snapshot;
    if (esp32_mquickjs_wifi_radio_get_status(&snapshot) != ESP_OK)
        return JS_ThrowInternalError(ctx, "failed to read Wi-Fi Radio status");
    return wifi_make_radio_status(ctx, &snapshot);
}

static JSValue wifi_make_status_object(JSContext *ctx)
{
    esp32_mquickjs_wifi_status_t status;
    esp32_mquickjs_wifi_radio_status_t radio_status;
    JSGCRef status_ref, radio_ref;
    JSValue *status_obj = JS_PushGCRef(ctx, &status_ref);
    JSValue *radio_obj = JS_PushGCRef(ctx, &radio_ref);

    *status_obj = JS_UNDEFINED;
    *radio_obj = JS_UNDEFINED;
    if (esp32_mquickjs_wifi_get_status(&status) != ESP_OK ||
        esp32_mquickjs_wifi_radio_get_status(&radio_status) != ESP_OK) {
        JS_ThrowInternalError(ctx, "failed to read Wi-Fi status");
        goto fail;
    }

    *status_obj = JS_NewObject(ctx);
    *radio_obj = wifi_make_radio_status(ctx, &radio_status);
    if (JS_IsException(*status_obj) || JS_IsException(*radio_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, status_obj, "setupStage",
            s_wifi_setup_stage != NULL ? JS_NewString(ctx, s_wifi_setup_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "setupError",
            s_wifi_setup_stage != NULL ? JS_NewInt32(ctx, s_wifi_setup_error) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "stationNetifCleanupError",
            s_wifi_state.sta_detach_error != ESP_OK ? JS_NewInt32(ctx, s_wifi_state.sta_detach_error) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "stationNetifRestartRequired",
            JS_NewBool(s_wifi_state.sta_detach_error != ESP_OK)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "accessPointNetifCleanupError",
            esp32_mquickjs_wifi_ap_netif_cleanup_error() != ESP_OK
                ? JS_NewInt32(ctx, esp32_mquickjs_wifi_ap_netif_cleanup_error()) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "accessPointNetifRestartRequired",
            JS_NewBool(esp32_mquickjs_wifi_ap_netif_cleanup_error() != ESP_OK))) goto fail;

    if (!wifi_set_link_properties(ctx, status_obj, &status.link) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "associated",
            JS_NewBool(wifi_link_snapshot_valid(&status.link))) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "watch", esp32_mquickjs_wifi_watch_status(ctx)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "connectionCounters", wifi_connection_counters_to_js(ctx)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "accessPoint", esp32_mquickjs_wifi_ap_status(ctx)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "cleanupStage",
            s_wifi_state.cleanup_stage != NULL
                ? JS_NewString(ctx, s_wifi_state.cleanup_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "cleanupError",
            s_wifi_state.cleanup_stage != NULL
                ? JS_NewInt32(ctx, s_wifi_state.cleanup_error) : JS_NULL)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, status_obj, "initialized",
                                     JS_NewBool(status.initialized)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "started",
                                     JS_NewBool(radio_status.started)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "connected",
                                     JS_NewBool(status.connected)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "scanning",
                                     JS_NewBool(status.scanning)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "scanDraining",
                                     JS_NewBool(status.scan_draining)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "scanCleanupError",
            status.scan_cleanup_error != ESP_OK
                ? JS_NewInt32(ctx, status.scan_cleanup_error) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "connectDraining",
            JS_NewBool(status.connect_draining)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "disconnectCleanupError",
            status.disconnect_cleanup_error != ESP_OK
                ? JS_NewInt32(ctx, status.disconnect_cleanup_error) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "connectTimerError",
            status.connect_timer_error != ESP_OK
                ? JS_NewInt32(ctx, status.connect_timer_error) : JS_NULL) ||
        !esp32_mquickjs_wifi_set_ssid_properties(ctx, status_obj, (const uint8_t *)status.ssid,
            strnlen(status.ssid, 32)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "lastDisconnectReason",
                                     JS_NewInt32(ctx, status.last_disconnect_reason)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "lastDisconnectReasonName",
                                     JS_NewString(ctx, wifi_reason_to_string(status.last_disconnect_reason))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, status_obj, "droppedDriverEvents",
            JS_NewUint32(
                ctx, atomic_load_explicit(
                         &s_wifi_state.dropped_driver_events,
                         memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "radio",
                                         *radio_obj)) {
        goto fail;
    }

    JS_PopGCRef(ctx, &radio_ref);
    return JS_PopGCRef(ctx, &status_ref);

fail:
    JS_PopGCRef(ctx, &radio_ref);
    JS_PopGCRef(ctx, &status_ref);
    return JS_EXCEPTION;
}

JSValue esp32_mquickjs_wifi_make_status_object(JSContext *ctx)
{
    return wifi_make_status_object(ctx);
}

JSValue esp32_mquickjs_wifi_throw_operation_error(
    JSContext *ctx,
    const char *code,
    const char *operation,
    esp_err_t err,
    int32_t disconnect_reason,
    uint32_t scan_status)
{
    esp32_mquickjs_wifi_status_t status;
    esp32_mquickjs_wifi_radio_status_t radio = {0};
    (void)esp32_mquickjs_wifi_radio_get_status(&radio);
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    JSValue result;
    bool has_status = esp32_mquickjs_wifi_get_status(&status) == ESP_OK;

    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "setupStage",
            s_wifi_setup_stage != NULL ? JS_NewString(ctx, s_wifi_setup_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "cleanupStage",
            s_wifi_state.cleanup_stage != NULL ? JS_NewString(ctx, s_wifi_state.cleanup_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "restartRequired",
            JS_NewBool(radio.restart_required || s_wifi_state.sta_detach_error != ESP_OK ||
                       esp32_mquickjs_wifi_ap_netif_cleanup_error() != ESP_OK)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "espCode", JS_NewInt32(ctx, err)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "espName",
            JS_NewString(ctx, esp_err_to_name(err))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "ssid",
            has_status && status.ssid[0] != '\0'
                ? esp32_mquickjs_wifi_ssid_text(ctx, (const uint8_t *)status.ssid, strnlen(status.ssid, 32))
                : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "disconnectReason",
            disconnect_reason >= 0
                ? JS_NewInt32(ctx, disconnect_reason)
                : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "disconnectReasonName",
            disconnect_reason >= 0
                ? JS_NewString(ctx, wifi_reason_to_string(disconnect_reason))
                : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "scanStatus",
            scan_status != UINT32_MAX
                ? JS_NewUint32(ctx, scan_status)
                : JS_NULL)) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    result = esp32_mquickjs_throw_native_error(
        ctx, code, operation, "Wi-Fi operation failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

/* Capture failures do not query Radio or call a driver. Execution failures
 * include only records reached by this call, never an old transaction's data. */
JSValue esp32_mquickjs_wifi_throw_configuration_error(JSContext *ctx, esp_err_t error,
    const char *option, const esp32_mquickjs_wifi_configuration_execution_t *execution)
{
    esp32_mquickjs_wifi_radio_status_t radio = {0};
    bool activation = false;
    if (execution != NULL) {
        (void)esp32_mquickjs_wifi_radio_get_status(&radio);
        activation = execution->resume_attempted && radio.fault_stage != NULL &&
            (strcmp(radio.fault_stage, "tx-power-snapshot") == 0 ||
             strcmp(radio.fault_stage, "tx-power-config") == 0 ||
             strcmp(radio.fault_stage, "tx-power-readback") == 0);
    }
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stage",
            JS_NewString(ctx, execution != NULL ? execution->stage : "capture")) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "option", option != NULL ? JS_NewString(ctx, option) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, error)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espName", JS_NewString(ctx, esp_err_to_name(error))) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "lifecycleAdmitted", JS_NewBool(execution != NULL && execution->admitted)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stopAttempted", JS_NewBool(execution != NULL && execution->stop_attempted)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "cleanupPending", JS_NewBool(execution != NULL && s_wifi_configuration_cleanup)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "restartRequired", JS_NewBool(execution != NULL &&
            (radio.restart_required || s_wifi_state.sta_detach_error != ESP_OK ||
             esp32_mquickjs_wifi_ap_netif_cleanup_error() != ESP_OK))) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "configuration",
            execution != NULL && execution->configuration_attempted ? wifi_make_configuration_status(ctx, &radio.configuration) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "activation",
            activation ? wifi_make_configuration_status(ctx, &radio.activation) : JS_NULL)) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    JSValue result = esp32_mquickjs_throw_native_error(ctx,
        error == ESP_ERR_NOT_SUPPORTED ? "WIFI_CONFIG_UNSUPPORTED" : "WIFI_CONFIG_FAILED",
        "wifi.configure", "Wi-Fi configuration failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

static JSValue wifi_throw_connect_error(JSContext *ctx, esp_err_t err)
{
    esp32_mquickjs_wifi_status_t status;
    int32_t disconnect_reason = -1;

    if (esp32_mquickjs_wifi_get_status(&status) == ESP_OK) {
        disconnect_reason = status.last_disconnect_reason;
    }
    return esp32_mquickjs_wifi_throw_operation_error(
        ctx,
        err == ESP_ERR_TIMEOUT ? "WIFI_CONNECT_TIMEOUT"
                               : "WIFI_CONNECT_FAILED",
        "wifi.connect", err, disconnect_reason, UINT32_MAX);
}

JSValue esp32_mquickjs_wifi_throw_connect_error(JSContext *ctx, esp_err_t err)
{
    return wifi_throw_connect_error(ctx, err);
}

static JSValue wifi_throw_scan_error(JSContext *ctx, esp_err_t err)
{
    const char *code = err == ESP_ERR_WIFI_STATE
                           ? "WIFI_SCAN_BUSY"
                           : err == ESP_ERR_WIFI_TIMEOUT
                                 ? "WIFI_SCAN_TIMEOUT"
                                 : "WIFI_SCAN_FAILED";
    return esp32_mquickjs_wifi_throw_operation_error(
        ctx, code, "wifi.scan", err, -1, UINT32_MAX);
}

JSValue esp32_mquickjs_wifi_throw_scan_error(JSContext *ctx, esp_err_t err)
{
    return wifi_throw_scan_error(ctx, err);
}

static int js_value_to_timeout_ms(JSContext *ctx,
                                  JSValue value,
                                  uint32_t default_timeout_ms,
                                  uint32_t *out_timeout_ms)
{
    if (JS_IsUndefined(value)) {
        *out_timeout_ms = default_timeout_ms;
        return 0;
    }
    if (!esp32_mquickjs_value_to_bounded_u32(
            ctx, value, 0, INT32_MAX, out_timeout_ms)) {
        return -1;
    }
    return 0;
}

esp_err_t esp32_mquickjs_wifi_get_status(esp32_mquickjs_wifi_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    if (!s_wifi_state.initialized) {
        return ESP_OK;
    }

    wifi_lock();
    *status = s_wifi_state.status;
    status->connect_draining = s_wifi_state.connect_draining;
    status->disconnect_cleanup_error = s_wifi_state.disconnect_cleanup_error;
    status->scan_draining = s_wifi_state.scan_draining;
    status->scan_cleanup_error = s_wifi_state.scan_cleanup_error;
    wifi_unlock();
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_prepare_connect_timer(void)
{
    atomic_store_explicit(&s_wifi_timeout_armed_generation, 0, memory_order_release);
    esp_err_t err = s_wifi_state.connect_timeout_timer != NULL
        ? esp_timer_stop_blocking(s_wifi_state.connect_timeout_timer,
                                  pdMS_TO_TICKS(1000)) : ESP_OK;
    /* Clear only after the callback barrier. A callback already in flight
     * may publish its old generation while stop_blocking is waiting. */
    if (err == ESP_OK)
        atomic_store_explicit(&s_wifi_timeout_pending, 0, memory_order_release);
    wifi_lock();
    s_wifi_state.status.connect_timer_error = err;
    wifi_unlock();
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Wi-Fi connect timer cleanup pending: %s", esp_err_to_name(err));
    return err;
}

static esp_err_t wifi_start_connect_reserved(const wifi_config_t *config,
                                            uint32_t timeout_ms)
{
    wifi_config_t applied_config;
    esp_err_t err;
    bool needs_disconnect = false;
    size_t ssid_len;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(esp32_mquickjs_wifi_prepare_connect_timer(), TAG,
                        "previous connect timer callback has not exited");
    wifi_lock();
    needs_disconnect = s_wifi_state.status.connected || s_wifi_state.connect_in_progress ||
        s_wifi_state.connect_draining;
    wifi_unlock();
    if (needs_disconnect) {
        err = wifi_request_disconnect();
        if (err != ESP_OK) return err;
        wifi_lock();
        bool pending = s_wifi_state.connect_draining;
        wifi_unlock();
        if (pending) {
            bool interrupted = false;
            (void)wifi_wait_for_bits(WIFI_LINK_DRAINED_BIT, true, timeout_ms, &interrupted);
            wifi_lock();
            pending = s_wifi_state.connect_draining;
            wifi_unlock();
            if (interrupted || pending)
                return interrupted ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
        }
    }
    if (!config->sta.pmf_cfg.capable) {
        /* The stopped transaction already applied PMF. A second set_config
         * after START would enable it again. Under the CONNECT reservation,
         * verify the whole accepted config before submitting native connect. */
        memset(&applied_config, 0, sizeof(applied_config));
        err = esp_wifi_get_config(WIFI_IF_STA, &applied_config);
        if (err == ESP_OK &&
            !esp32_mquickjs_wifi_radio_accept_station_config(config, &applied_config))
            err = ESP_ERR_INVALID_RESPONSE;
    } else {
        applied_config = *config;
        err = esp_wifi_set_config(WIFI_IF_STA, &applied_config);
    }
    esp32_mquickjs_wireless_secure_zero(&applied_config, sizeof(applied_config));
    if (err != ESP_OK) return err;

    xEventGroupClearBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);

    wifi_lock();
    s_wifi_state.connect_in_progress = true;
    s_wifi_state.status.connected = false;
    memset(&s_wifi_state.status.link, 0, sizeof(s_wifi_state.status.link));
    s_wifi_state.status.last_disconnect_reason = 0;
    ssid_len = strnlen((const char *)config->sta.ssid,
                       sizeof(config->sta.ssid));
    memcpy(s_wifi_state.status.ssid, config->sta.ssid, ssid_len);
    s_wifi_state.status.ssid[ssid_len] = '\0';
    wifi_unlock();

    wifi_stop_connect_timeout_timer();
    if (s_wifi_state.connect_queue != NULL) {
        xQueueReset(s_wifi_state.connect_queue);
    }

    wifi_connection_note_submit();
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        wifi_connection_note_failure();
        wifi_lock();
        s_wifi_state.connect_in_progress = false;
        wifi_unlock();
        ESP_LOGE(TAG, "esp_wifi_connect() failed: %s", esp_err_to_name(err));
        return err;
    }
    if (timeout_ms == 0) {
        timeout_ms = 1;
    }
    atomic_store_explicit(&s_wifi_timeout_armed_generation,
                          s_wifi_state.connect_generation, memory_order_release);
    err = esp_timer_start_once(s_wifi_state.connect_timeout_timer,
                               (uint64_t)timeout_ms * 1000ULL);
    if (err != ESP_OK) {
        atomic_store_explicit(&s_wifi_timeout_armed_generation, 0, memory_order_release);
        wifi_lock();
        s_wifi_state.status.connect_timer_error = err;
        wifi_begin_disconnect_locked();
        wifi_unlock();
        (void)wifi_request_disconnect();
        ESP_LOGE(TAG, "esp_timer_start_once(connect_timeout_timer) failed: %s",
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t esp32_mquickjs_wifi_start_connect(const wifi_config_t *config,
                                            uint32_t timeout_ms)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = esp32_mquickjs_wifi_ensure_started();
    if (err != ESP_OK) return err;
    err = wifi_prepare_radio_operation(ESP32_MQUICKJS_WIFI_RADIO_OPERATION_CONNECT);
    if (err != ESP_OK) return err;
    wifi_lock();
    s_wifi_state.connect_start_active = true;
    wifi_unlock();
    err = wifi_start_connect_reserved(config, timeout_ms);
    wifi_lock();
    s_wifi_state.connect_start_active = false;
    wifi_unlock();
    wifi_release_radio_operation();
    return err;
}

esp_err_t esp32_mquickjs_wifi_start_disconnect(bool *out_pending)
{
    esp_err_t err;

    if (out_pending == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_pending = false;

    if (!s_wifi_state.initialized || !s_wifi_state.started) {
        return ESP_OK;
    }

    err = wifi_request_disconnect();
    wifi_lock();
    /* Public disconnect completes at the native terminal; reuse still waits
     * for the IP fence. A retry after that terminal must not wait for a
     * second DISCONNECTED event that the driver will never send. */
    *out_pending = s_wifi_state.connect_draining && !s_wifi_state.disconnect_seen;
    wifi_unlock();
    return err;
}

static bool wifi_parse_interface(JSContext *ctx, JSValue value, wifi_interface_t *interface)
{
    if (!JS_IsString(ctx, value)) goto invalid;
    JSCStringBuf buffer;
    size_t length;
    const char *name = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (name == NULL) return false;
    if (length == 7 && memcmp(name, "station", 7) == 0) *interface = WIFI_IF_STA;
    else if (length == 12 && memcmp(name, "access-point", 12) == 0) *interface = WIFI_IF_AP;
    else goto invalid;
    return true;
invalid:
    JS_ThrowTypeError(ctx, "Wi-Fi interface must be station or access-point");
    return false;
}

JSValue js_wifi_get_mac(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.getMac expects one interface argument");
    wifi_interface_t interface;
    if (!wifi_parse_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    uint8_t mac[6];
    esp_err_t err = esp32_mquickjs_wifi_radio_get_mac(interface, mac);
    if (err != ESP_OK)
        return esp32_mquickjs_wifi_throw_operation_error(ctx, "WIFI_MAC_FAILED", "wifi.getMac", err, 0, 0);
    char address[18];
    snprintf(address, sizeof(address), MACSTR, MAC2STR(mac));
    return JS_NewString(ctx, address);
}

static const char *wifi_cipher_name(wifi_cipher_type_t cipher)
{
    switch (cipher) {
    case WIFI_CIPHER_TYPE_NONE: return "none";
    case WIFI_CIPHER_TYPE_WEP40: return "wep40";
    case WIFI_CIPHER_TYPE_WEP104: return "wep104";
    case WIFI_CIPHER_TYPE_TKIP: return "tkip";
    case WIFI_CIPHER_TYPE_CCMP: return "ccmp";
    case WIFI_CIPHER_TYPE_TKIP_CCMP: return "tkip-ccmp";
    case WIFI_CIPHER_TYPE_AES_CMAC128: return "aes-cmac128";
    case WIFI_CIPHER_TYPE_SMS4: return "sms4";
    case WIFI_CIPHER_TYPE_GCMP: return "gcmp";
    case WIFI_CIPHER_TYPE_GCMP256: return "gcmp256";
    case WIFI_CIPHER_TYPE_AES_GMAC128: return "aes-gmac128";
    case WIFI_CIPHER_TYPE_AES_GMAC256: return "aes-gmac256";
    default: return NULL;
    }
}

JSValue esp32_mquickjs_wifi_country_to_js(JSContext *ctx, const wifi_country_t *country)
{
    bool code_valid = (country->cc[0] >= 'A' && country->cc[0] <= 'Z' &&
                       country->cc[1] >= 'A' && country->cc[1] <= 'Z') ||
                      (country->cc[0] == '0' && country->cc[1] == '1');
    if (!code_valid) return JS_NULL;
    const char *environment = country->cc[2] == 'I' ? "indoor" :
                              country->cc[2] == 'O' ? "outdoor" : country->cc[2] == 'X' ? "X" : NULL;
    const char *policy = country->policy == WIFI_COUNTRY_POLICY_AUTO ? "auto" :
                         country->policy == WIFI_COUNTRY_POLICY_MANUAL ? "manual" : NULL;
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "code", JS_NewStringLen(ctx, country->cc, 2)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "environment", environment ? JS_NewString(ctx, environment) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "policy", policy ? JS_NewString(ctx, policy) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "startChannel", JS_NewUint32(ctx, country->schan)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "channelCount", JS_NewUint32(ctx, country->nchan)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "maxTxPowerDbm", JS_NewInt32(ctx, country->max_tx_power)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ghz5ChannelMask",
#if CONFIG_SOC_WIFI_SUPPORT_5G
            JS_NewUint32(ctx, country->wifi_5g_channel_mask)
#else
            JS_NULL
#endif
        )) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static JSValue wifi_scan_protocols_to_js(JSContext *ctx, const wifi_ap_record_t *record)
{
    static const char *const names[] = {"11b", "11g", "11n", "11a", "11ac", "11ax", "lr"};
    const bool flags[] = {record->phy_11b, record->phy_11g, record->phy_11n,
                         record->phy_11a, record->phy_11ac, record->phy_11ax, record->phy_lr};
    JSGCRef array_ref, value_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    uint32_t count = 0;
    for (size_t i = 0; i < sizeof(flags) / sizeof(*flags); ++i) {
        if (!flags[i]) continue;
        *value = JS_NewString(ctx, names[i]);
        if (JS_IsException(*value) || JS_IsException(JS_SetPropertyUint32(ctx, *array, count++, *value))) goto fail;
    }
    JS_PopGCRef(ctx, &value_ref);
    return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_scan_capabilities_to_js(JSContext *ctx, const wifi_ap_record_t *record)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "wps", JS_NewBool(record->wps)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ftmResponder", JS_NewBool(record->ftm_responder)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ftmInitiator", JS_NewBool(record->ftm_initiator)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "he", JS_NewBool(record->phy_11ax)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "vht", JS_NewBool(record->phy_11ac))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static JSValue wifi_make_scan_entry_object(JSContext *ctx, const wifi_ap_record_t *record)
{
    JSGCRef entry_ref;
    JSValue *entry;
    char bssid[WIFI_SCAN_BSSID_STR_LEN];

    entry = JS_PushGCRef(ctx, &entry_ref);
    *entry = JS_NewObject(ctx);
    if (JS_IsException(*entry)) {
        goto fail;
    }

    snprintf(bssid, sizeof(bssid), MACSTR, MAC2STR(record->bssid));
    const char *secondary = record->second == WIFI_SECOND_CHAN_NONE ? "none" :
                            record->second == WIFI_SECOND_CHAN_ABOVE ? "above" :
                            record->second == WIFI_SECOND_CHAN_BELOW ? "below" : NULL;
    const char *band = record->primary >= 1 && record->primary <= 14 ? "2.4GHz" :
        esp32_mquickjs_wifi_radio_5ghz_channel_bit(record->primary) != 0 ? "5GHz" : NULL;
    const char *pairwise = wifi_cipher_name(record->pairwise_cipher);
    const char *group = wifi_cipher_name(record->group_cipher);
    if (!esp32_mquickjs_wifi_set_ssid_properties(ctx, entry, record->ssid,
            strnlen((const char *)record->ssid, 32)) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "bssid",
                                     JS_NewString(ctx, bssid)) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "rssi",
                                     JS_NewInt32(ctx, record->rssi)) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "channel",
                                     JS_NewInt32(ctx, record->primary)) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "authMode",
                                     JS_NewString(ctx, wifi_authmode_to_string(record->authmode))) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "hidden",
                                     JS_NewBool(record->ssid[0] == '\0')) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "secondaryChannel", secondary ? JS_NewString(ctx, secondary) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "band", band ? JS_NewString(ctx, band) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "pairwiseCipher", pairwise ? JS_NewString(ctx, pairwise) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "groupCipher", group ? JS_NewString(ctx, group) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "antenna",
            record->ant == WIFI_ANT_ANT0 || record->ant == WIFI_ANT_ANT1 ? JS_NewInt32(ctx, record->ant) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "protocols", wifi_scan_protocols_to_js(ctx, record)) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "country", esp32_mquickjs_wifi_country_to_js(ctx, &record->country)) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "capabilities", wifi_scan_capabilities_to_js(ctx, record))) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &entry_ref);

fail:
    JS_PopGCRef(ctx, &entry_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_make_scan_results_array(JSContext *ctx, uint16_t max_records)
{
    JSGCRef results_ref;
    JSValue *results;
    wifi_ap_record_t *records = NULL;
    uint16_t count = 0;
    uint16_t i;
    esp_err_t err;

    if (max_records == 0 || max_records > ESP32_MQUICKJS_WIFI_MAX_SCAN_RECORDS)
        return JS_ThrowRangeError(ctx, "wifi.scan maxRecords is out of range");
    results = JS_PushGCRef(ctx, &results_ref);
    *results = JS_NewArray(ctx, 0);
    if (JS_IsException(*results)) {
        goto fail;
    }

    err = esp_wifi_scan_get_ap_num(&count);
    if (err != ESP_OK) {
        JS_ThrowInternalError(ctx, "esp_wifi_scan_get_ap_num() failed: %s", esp_err_to_name(err));
        goto fail;
    }

    if (count == 0) {
        return JS_PopGCRef(ctx, &results_ref);
    }
    if (count > max_records) {
        count = max_records;
    }

    records = esp32_mquickjs_memory_wireless_calloc("wifi", count, sizeof(*records), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
    if (records == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }

    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        JS_ThrowInternalError(ctx, "esp_wifi_scan_get_ap_records() failed: %s", esp_err_to_name(err));
        goto fail;
    }

    /* Successful get_ap_records releases the SDK list, even if JS conversion
     * below fails. Do not repeat that successful cleanup suffix. */
    esp32_mquickjs_wifi_scan_results_consumed();
    for (i = 0; i < count; ++i) {
        JSGCRef entry_ref;
        JSValue *entry = JS_PushGCRef(ctx, &entry_ref);

        *entry = wifi_make_scan_entry_object(ctx, &records[i]);
        if (JS_IsException(*entry) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *results, i, *entry))) {
            JS_PopGCRef(ctx, &entry_ref);
            goto fail;
        }
        JS_PopGCRef(ctx, &entry_ref);
    }

    esp32_mquickjs_memory_payload_free(records);
    return JS_PopGCRef(ctx, &results_ref);

fail:
    esp32_mquickjs_memory_payload_free(records);
    JS_PopGCRef(ctx, &results_ref);
    return JS_EXCEPTION;
}

JSValue esp32_mquickjs_wifi_scan_record_to_js(JSContext *ctx, const wifi_ap_record_t *record)
{
    return wifi_make_scan_entry_object(ctx, record);
}

JSValue esp32_mquickjs_wifi_make_scan_results_array(JSContext *ctx, uint16_t max_records)
{
    return wifi_make_scan_results_array(ctx, max_records);
}

esp_err_t esp32_mquickjs_wifi_reopen_ap_shared(wifi_config_t *config, bool *handled)
{
    if (config == NULL || handled == NULL) return ESP_ERR_INVALID_ARG;
    *handled = s_wifi_state.radio_lease.acquired || s_wifi_application.acquired;
    if (!*handled) return ESP_OK;
    if (s_wifi_configuration_cleanup || s_wifi_ap_stop_cleanup || s_wifi_lifecycle.identity != 0U ||
        s_wifi_state.runtime_cleanup_pending || s_wifi_state.cleanup_stage != NULL || !wifi_helpers_idle(true))
        return ESP_ERR_INVALID_STATE;
    wifi_release_radio_operation();
    esp_err_t err = esp32_mquickjs_wifi_ap_reopen(&s_wifi_application, &s_wifi_state.radio_lease,
                                               config, &s_wifi_lifecycle);
    /* A failed post-admission attempt now owns the AP removal suffix. The
     * existing public stop/runtime takeover drains Station before whole cleanup. */
    if (s_wifi_lifecycle.identity != 0U) s_wifi_ap_stop_cleanup = true;
    return err;
}

bool esp32_mquickjs_wifi_ap_stop_pending(void)
{
    return s_wifi_ap_stop_cleanup;
}

esp_err_t esp32_mquickjs_wifi_stop_ap_shared(bool *handled)
{
    if (handled == NULL) return ESP_ERR_INVALID_ARG;
    *handled = s_wifi_ap_stop_cleanup || s_wifi_state.radio_lease.acquired || s_wifi_application.acquired;
    if (!*handled) return ESP_OK;
    if (!s_wifi_ap_stop_cleanup) {
        if (s_wifi_configuration_cleanup || s_wifi_lifecycle.identity != 0U ||
            s_wifi_state.runtime_cleanup_pending || s_wifi_state.cleanup_stage != NULL ||
            !wifi_helpers_idle(true)) return ESP_ERR_INVALID_STATE;
        wifi_release_radio_operation();
        esp_err_t err = esp32_mquickjs_wifi_ap_begin_partial_stop(
            &s_wifi_application, &s_wifi_state.radio_lease, &s_wifi_lifecycle);
        if (err != ESP_OK) return err;
        s_wifi_ap_stop_cleanup = true;
    }
    esp_err_t err = esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&s_wifi_lifecycle);
    if (err != ESP_OK) return err;
    err = esp32_mquickjs_wifi_ap_retire_partial_stop(&s_wifi_lifecycle);
    if (err != ESP_OK) return err;
    err = esp32_mquickjs_wifi_ap_finish_partial_stop(&s_wifi_lifecycle);
    if (err == ESP_OK) s_wifi_ap_stop_cleanup = false;
    return err;
}

/* Explicit whole stop/runtime retirement, after Station and native operations
 * have drained. Keep the same reservation and AP netif cleanup suffix. */
static esp_err_t wifi_adopt_ap_stop_cleanup(void)
{
    if (!s_wifi_ap_stop_cleanup || s_wifi_configuration_cleanup || !wifi_helpers_idle(false))
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp32_mquickjs_wifi_ap_adopt_partial_stop(&s_wifi_lifecycle);
    if (err != ESP_OK) return err;
    s_wifi_ap_stop_cleanup = false;
    s_wifi_configuration_cleanup = true;
    s_wifi_configuration_mode = WIFI_MODE_APSTA;
    s_wifi_configuration_stop_only = false;
    s_wifi_configuration_disconnect_pending = false;
    s_wifi_state.runtime_cleanup_pending = true;
    return ESP_OK;
}

/* One reservation spans both helpers. No public Future or native operation
 * may remain at handoff; failure before begin has no owner/driver effects. */
static esp_err_t wifi_begin_configuration_cleanup(bool allow_disconnect)
{
    if (s_wifi_configuration_cleanup) return ESP_OK;
    if (s_wifi_lifecycle.identity != 0U || !wifi_helpers_idle(allow_disconnect)) return ESP_ERR_INVALID_STATE;
    wifi_release_radio_operation();
    esp_err_t err = esp32_mquickjs_wifi_ap_begin_configuration(
        &s_wifi_application, &s_wifi_state.radio_lease, &s_wifi_lifecycle);
    if (err != ESP_OK) return err;
    s_wifi_configuration_cleanup = true;
    s_wifi_configuration_mode = WIFI_MODE_NULL;
    s_wifi_configuration_stop_only = false;
    wifi_lock();
    s_wifi_configuration_disconnect_pending = s_wifi_state.status.connected || s_wifi_state.connect_draining;
    wifi_unlock();
    return ESP_OK;
}

/* Configuration admission also resolves defaults and requires permission for
 * every running AP. Cleanup/explicit stop keep their separate authorization. */
static esp_err_t wifi_begin_selected_configuration(
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    const esp32_mquickjs_wifi_radio_start_controls_t *start_controls)
{
    if (s_wifi_configuration_cleanup || s_wifi_lifecycle.identity != 0U ||
        !wifi_helpers_idle(selection->allow_disconnect)) return ESP_ERR_INVALID_STATE;
    wifi_release_radio_operation();
    esp_err_t err = esp32_mquickjs_wifi_ap_begin_selected_configuration(
        &s_wifi_application, &s_wifi_state.radio_lease, selection, controls, start_controls, &s_wifi_lifecycle);
    if (err != ESP_OK) return err;
    s_wifi_configuration_cleanup = true;
    s_wifi_configuration_mode = selection->mode;
    s_wifi_configuration_stop_only = false;
    wifi_lock();
    s_wifi_configuration_disconnect_pending = s_wifi_state.status.connected || s_wifi_state.connect_draining;
    wifi_unlock();
    return ESP_OK;
}

/* Only the admitted configuration may disconnect this established Station.
 * Pending public Futures/scan/connect were rejected before taking the token.
 * Reuse the native disconnect epoch/fence; never run a JS event poller here. */
static esp_err_t wifi_finish_configuration_disconnect(void)
{
    if (!s_wifi_configuration_disconnect_pending) return ESP_OK;
    if (!s_wifi_configuration_cleanup || s_wifi_lifecycle.identity == 0U)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = wifi_request_disconnect();
    if (err != ESP_OK) return err;
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    esp32_mquickjs_native_wait_t wait;
    esp32_mquickjs_native_wait_begin(runtime, &wait);
    TickType_t started = xTaskGetTickCount();
    TickType_t timeout = esp32_mquickjs_wifi_wait_remaining(pdMS_TO_TICKS(1000));
    for (;;) {
        wifi_lock();
        bool pending = s_wifi_state.status.connected || s_wifi_state.connect_draining ||
            s_wifi_state.disconnect_active || s_wifi_state.connect_start_active;
        bool exhausted = s_wifi_state.disconnect_epoch_exhausted;
        wifi_unlock();
        if (!pending) {
            s_wifi_configuration_disconnect_pending = false;
            err = ESP_OK;
            break;
        }
        if (exhausted || !esp32_mquickjs_cooperate(runtime)) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        /* A saturated event queue retains the unposted native fence. Do not
         * resubmit an already accepted disconnect while waiting for it. */
        err = wifi_post_disconnect_fence();
        if (err != ESP_OK) break;
        if ((TickType_t)(xTaskGetTickCount() - started) >= timeout) {
            err = ESP_ERR_TIMEOUT;
            break;
        }
        vTaskDelay(1);
    }
    esp32_mquickjs_native_wait_end(runtime, &wait);
    return err;
}

#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
static esp_err_t wifi_finish_enterprise_clear(void)
{
    if (!s_wifi_eap_stop.control.identity) return ESP_OK;
    esp_err_t error;
    if (s_wifi_eap_stop.binding) {
        esp32_mquickjs_wifi_eap_install_result_t result;
        s_wifi_state.cleanup_stage = "configuration-enterprise-clear";
        error = esp32_mquickjs_wifi_radio_eap_clear_lifecycle(s_wifi_eap_stop.binding, &s_wifi_lifecycle, &result);
        if (error != ESP_OK) return error;
        s_wifi_eap_stop.binding = 0;
    }
    if (s_wifi_eap_stop.release_ap) {
        s_wifi_state.cleanup_stage = "configuration-enterprise-ap-release";
        error = esp32_mquickjs_wifi_ap_release_enterprise_stop(&s_wifi_lifecycle);
        if (error != ESP_OK) return error;
        s_wifi_eap_stop.release_ap = false;
    }
    return ESP_OK;
}
#endif

static esp_err_t wifi_finish_configuration_cleanup(void)
{
    esp_err_t err;
    if (!s_wifi_configuration_cleanup || s_wifi_lifecycle.identity == 0U)
        return ESP_ERR_INVALID_STATE;
    bool recovery = esp32_mquickjs_wifi_radio_recovery_active(&s_wifi_lifecycle);
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (s_wifi_eap_stop.control.identity) {
        s_wifi_state.runtime_cleanup_pending = true;
        err = wifi_finish_enterprise_clear();
        if (err != ESP_OK) goto pending;
    }
#endif
    s_wifi_state.runtime_cleanup_pending = true;
    s_wifi_state.cleanup_stage = "configuration-vendor-ie-clear";
    err = esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle(&s_wifi_lifecycle);
    if (err != ESP_OK) goto pending;
    if (recovery) {
        s_wifi_state.cleanup_stage = "configuration-recovery-prepare";
        err = esp32_mquickjs_wifi_radio_prepare_recovery(&s_wifi_lifecycle);
        if (err != ESP_OK) goto pending;
    }
    s_wifi_state.cleanup_stage = "configuration-disconnect";
    err = wifi_finish_configuration_disconnect();
    if (err != ESP_OK) goto pending;
    /* Release is exact and idempotent. The lifecycle token excludes new owners
     * throughout the driver and helper suffix, including a failed retry. */
    esp32_mquickjs_wifi_radio_release(&s_wifi_application);
    esp32_mquickjs_wifi_radio_release(&s_wifi_state.radio_lease);
    s_wifi_state.cleanup_stage = "configuration-stop";
    err = recovery ? esp32_mquickjs_wifi_radio_stop_recovery(&s_wifi_lifecycle)
        : esp32_mquickjs_wifi_radio_quiesce_lifecycle(&s_wifi_lifecycle);
    if (err != ESP_OK) goto pending;
    s_wifi_state.cleanup_stage = "configuration-ap-retire";
    err = recovery ? esp32_mquickjs_wifi_ap_retire_for_recovery(&s_wifi_lifecycle)
        : esp32_mquickjs_wifi_ap_retire_for_configuration(&s_wifi_lifecycle);
    if (err != ESP_OK) goto pending;
    err = recovery ? esp32_mquickjs_wifi_retire_for_recovery(&s_wifi_lifecycle)
        : wifi_cleanup_helper(false);
    if (err != ESP_OK) goto pending;
    if (recovery) {
        s_wifi_state.cleanup_stage = "configuration-recovery-shutdown";
        err = esp32_mquickjs_wifi_radio_shutdown_recovery(&s_wifi_lifecycle);
        if (err != ESP_OK) goto pending;
    }
    s_wifi_state.cleanup_stage = s_wifi_configuration_stop_only
        ? "configuration-finish-stop" : "configuration-shutdown";
    err = esp32_mquickjs_wifi_radio_finish_lifecycle(&s_wifi_lifecycle, !s_wifi_configuration_stop_only);
    if (err != ESP_OK) goto pending;
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (s_wifi_eap_stop.control.identity) {
        /* Native clear and driver/helper suffix have returned. The exact config
         * token can finish even after runtime begin_close invalidated revision. */
        (void)esp32_mquickjs_wifi_eap_config_finish(&s_wifi_eap_stop.control, false);
        memset(&s_wifi_eap_stop, 0, sizeof(s_wifi_eap_stop));
    }
#endif
    s_wifi_configuration_cleanup = false;
    s_wifi_configuration_mode = WIFI_MODE_NULL;
    s_wifi_configuration_stop_only = false;
    s_wifi_state.runtime_cleanup_pending = false;
    s_wifi_state.cleanup_stage = NULL;
    s_wifi_state.cleanup_error = ESP_OK;
    return ESP_OK;
pending:
    s_wifi_state.runtime_cleanup_pending = true;
    s_wifi_state.cleanup_error = err;
    return err;
}

bool esp32_mquickjs_prepare_wifi_recovery_runtime_destroy(void)
{
    /* Only continue an already admitted physical recovery. Future cancellation
     * has ended public restoration; the exact central lifecycle owns cleanup.
     * Other configuration cleanup follows normal runtime deinit, after native
     * owners drain. Never grant a new recovery or borrow its original lease. */
    if (!s_wifi_configuration_cleanup ||
        !esp32_mquickjs_wifi_radio_recovery_active(&s_wifi_lifecycle)) return true;
    return wifi_finish_configuration_cleanup() == ESP_OK;
}

bool esp32_mquickjs_wifi_configuration_pending(void)
{
    return s_wifi_configuration_cleanup;
}

esp_err_t esp32_mquickjs_wifi_cleanup_ap_configuration(void)
{
    if (!s_wifi_configuration_cleanup || s_wifi_configuration_mode != WIFI_MODE_AP)
        return ESP_ERR_INVALID_STATE;
    return wifi_finish_configuration_cleanup();
}

static esp_err_t wifi_begin_start_configuration(
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection, bool *already_started)
{
    if (s_wifi_configuration_cleanup || s_wifi_ap_stop_cleanup || s_wifi_lifecycle.identity != 0U ||
        !wifi_helpers_idle(true)) return ESP_ERR_INVALID_STATE;
    wifi_release_radio_operation();
    esp_err_t err = esp32_mquickjs_wifi_ap_begin_start_configuration(
        &s_wifi_application, &s_wifi_state.radio_lease, selection, &s_wifi_lifecycle, already_started);
    if (err != ESP_OK || *already_started) return err;
    s_wifi_configuration_cleanup = true;
    s_wifi_configuration_mode = selection->mode;
    s_wifi_configuration_stop_only = false;
    s_wifi_configuration_disconnect_pending = false;
    return ESP_OK;
}

/* Matching running starts do not reconstruct helper/netif or mutate settings.
 * A Station-only Radio previously started by another feature can still acquire
 * the ordinary Wi-Fi helper owners through the existing exact lease path. */
static esp_err_t wifi_start_existing_running(const esp32_mquickjs_wifi_radio_configuration_selection_t *selection)
{
    if (!(selection->mode & WIFI_MODE_STA)) return ESP_OK;
    esp_err_t err = wifi_init_once();
    if (err == ESP_OK && !s_wifi_application.acquired)
        err = esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
                                               WIFI_MODE_STA, &s_wifi_application);
    if (err == ESP_OK) err = esp32_mquickjs_wifi_ensure_started();
    return err;
}

/* The exact admitted lifecycle and old helper retirement precede this common
 * reconstruction suffix. Ordinary restart and Action recovery replay the same
 * frozen credentials/policies and publish leases only after final readback. */
static esp_err_t wifi_restart_restore_interfaces(wifi_mode_t mode, wifi_config_t *stored_ap,
    esp32_mquickjs_wifi_configuration_execution_t *execution, bool restore_off)
{
    const char *stage = "restart-rebuild";
    esp_err_t err;
    err = esp32_mquickjs_wifi_radio_rebuild_restart_lifecycle(&s_wifi_lifecycle, mode);
    if (err != ESP_OK) goto pending;
    /* Dual-band replay can temporarily START Station even for final AP-only.
     * Install its handlers/netif before replay and retire it after final STOP. */
    stage = "restart-station-prepare";
    err = esp32_mquickjs_wifi_prepare_for_configuration(&s_wifi_lifecycle);
    if (err != ESP_OK) goto pending;
    if (mode & WIFI_MODE_AP) {
        stage = "restart-ap-prepare";
        err = esp32_mquickjs_wifi_ap_prepare_for_configuration(&s_wifi_lifecycle);
        if (err != ESP_OK) goto pending;
    }
    stage = "restart-replay";
    if (execution != NULL) execution->configuration_attempted = true;
    err = esp32_mquickjs_wifi_radio_replay_restart_lifecycle(&s_wifi_lifecycle, mode);
    if (err != ESP_OK) goto pending;
    if (restore_off) {
        stage = "restart-off-resume";
        if (execution != NULL) execution->resume_attempted = true;
        err = esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle(&s_wifi_lifecycle, mode);
        if (err != ESP_OK) goto pending;
        stage = "restart-off-ap-retire";
        err = esp32_mquickjs_wifi_ap_retire_for_configuration(&s_wifi_lifecycle);
        if (err != ESP_OK) goto pending;
        stage = "restart-off-station-retire";
        err = wifi_cleanup_helper(false);
        if (err != ESP_OK) { stage = s_wifi_state.cleanup_stage; goto pending; }
        stage = "restart-off-finish";
        err = esp32_mquickjs_wifi_radio_finish_lifecycle(&s_wifi_lifecycle, false);
        if (err != ESP_OK) goto pending;
        goto complete;
    }
    if (!(mode & WIFI_MODE_STA)) {
        stage = "restart-temporary-station-retire";
        err = esp32_mquickjs_wifi_retire_for_configuration(&s_wifi_lifecycle);
        if (err != ESP_OK) { stage = s_wifi_state.cleanup_stage != NULL ? s_wifi_state.cleanup_stage : stage; goto pending; }
    }
    esp32_mquickjs_wifi_radio_lease_t *ap_slot = NULL;
    if (mode & WIFI_MODE_AP) {
        stage = "restart-ap-config";
        err = esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration(&s_wifi_lifecycle, stored_ap);
        if (err != ESP_OK) goto pending;
        stage = "restart-ap-slot";
        ap_slot = esp32_mquickjs_wifi_ap_configuration_slot(&s_wifi_lifecycle);
        if (ap_slot == NULL) { err = ESP_ERR_INVALID_STATE; goto pending; }
    }
    stage = "restart-resume";
    if (execution != NULL) execution->resume_attempted = true;
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (s_wifi_eap_stop.profile) {
        stage = "restart-enterprise-resume";
        esp32_mquickjs_wifi_eap_config_status_t config;
        esp32_mquickjs_wifi_eap_config_status(&config);
        if (!config.busy || config.closing) { err = ESP_ERR_INVALID_STATE; goto pending; }
        err = esp32_mquickjs_wifi_radio_eap_resume_restart(&s_wifi_lifecycle, mode,
            &s_wifi_application, &s_wifi_state.radio_lease, ap_slot, s_wifi_eap_stop.profile);
        s_wifi_eap_stop.binding = esp32_mquickjs_wifi_radio_eap_identity();
    } else
#endif
    err = esp32_mquickjs_wifi_radio_resume_lifecycle(&s_wifi_lifecycle, mode, true,
        (mode & WIFI_MODE_STA) ? &s_wifi_application : NULL,
        (mode & WIFI_MODE_STA) ? &s_wifi_state.radio_lease : NULL, ap_slot, NULL);
    if (err != ESP_OK) goto pending;
complete:
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (s_wifi_eap_stop.profile) {
        (void)esp32_mquickjs_wifi_eap_config_finish(&s_wifi_eap_stop.control, false);
        memset(&s_wifi_eap_stop, 0, sizeof(s_wifi_eap_stop));
    }
#endif
    s_wifi_configuration_cleanup = false;
    s_wifi_configuration_mode = WIFI_MODE_NULL;
    s_wifi_state.runtime_cleanup_pending = false;
    s_wifi_state.cleanup_stage = NULL;
    s_wifi_state.cleanup_error = ESP_OK;
    if (execution != NULL) execution->stage = "complete";
    return ESP_OK;
pending:
    /* Central stop/runtime cleanup owns the same reservation from here. Do
     * not continue restoration after a helper or driver failure, or discard
     * its frozen checkpoint before physical cleanup has completed. */
    s_wifi_state.runtime_cleanup_pending = true;
    s_wifi_state.cleanup_stage = stage;
    s_wifi_state.cleanup_error = err;
    if (execution != NULL) execution->stage = stage;
    return err;
}

#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
static esp_err_t wifi_begin_enterprise_restart(uint64_t binding, bool allow_ap_restart, wifi_mode_t *mode)
{
    esp32_mquickjs_wifi_eap_config_status_t config;
    esp32_mquickjs_wifi_eap_config_status(&config);
    esp32_mquickjs_wifi_eap_profile_t *profile = NULL;
    esp_err_t error = esp32_mquickjs_wifi_eap_config_begin(config.revision,
        ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE, &s_wifi_eap_stop.control, &profile);
    if (error != ESP_OK) return error;
    wifi_release_radio_operation();
    error = esp32_mquickjs_wifi_ap_begin_enterprise_restart(binding, &s_wifi_application,
        &s_wifi_state.radio_lease, profile, allow_ap_restart, &s_wifi_lifecycle, mode);
    if (error != ESP_OK) {
        (void)esp32_mquickjs_wifi_eap_config_finish(&s_wifi_eap_stop.control, false);
        return error;
    }
    s_wifi_eap_stop.binding = binding;
    s_wifi_eap_stop.profile = profile;
    s_wifi_eap_stop.release_ap = true;
    s_wifi_eap_stop.checkpoint_attempted = false;
    return ESP_OK;
}

static esp_err_t wifi_restart_enterprise_interfaces(wifi_config_t *stored_ap, bool allow_ap_restart,
    esp32_mquickjs_wifi_configuration_execution_t *execution)
{
    const char *stage = "restart-enterprise-admission";
    if (execution) execution->stage = stage;
    esp32_mquickjs_wifi_eap_config_status_t config;
    esp32_mquickjs_wifi_eap_config_status(&config);
    wifi_mode_t mode = s_wifi_configuration_mode;
    if (!s_wifi_eap_stop.profile || !s_wifi_eap_stop.control.identity || !config.busy || config.closing ||
        !s_wifi_configuration_cleanup || !s_wifi_lifecycle.identity ||
        (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) || (mode == WIFI_MODE_APSTA && !allow_ap_restart))
        return ESP_ERR_INVALID_STATE;
    if (execution) execution->admitted = true;
    esp_err_t error = wifi_finish_enterprise_clear();
    if (error != ESP_OK) { stage = s_wifi_state.cleanup_stage; goto pending; }
    esp32_mquickjs_wifi_radio_release(&s_wifi_application);
    esp32_mquickjs_wifi_radio_release(&s_wifi_state.radio_lease);
    if (!s_wifi_eap_stop.checkpoint_attempted) {
        stage = "restart-enterprise-checkpoint";
        s_wifi_eap_stop.checkpoint_attempted = true;
        if (execution) execution->stop_attempted = true;
        error = esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(&s_wifi_lifecycle, mode);
    } else {
        stage = "restart-enterprise-retry";
        esp32_mquickjs_wifi_radio_restart_selection_t selection = {.allow_ap_restart = allow_ap_restart};
        error = esp32_mquickjs_wifi_radio_admit_restart_retry(&s_wifi_lifecycle, &selection);
        if (error == ESP_OK && (selection.mode != mode || selection.cold || selection.restore_off)) error = ESP_ERR_INVALID_STATE;
        if (error == ESP_OK) {
            if (execution) execution->stop_attempted = true;
            error = esp32_mquickjs_wifi_radio_quiesce_lifecycle(&s_wifi_lifecycle);
        }
    }
    if (error != ESP_OK) goto pending;
    stage = "restart-enterprise-ap-retire";
    error = esp32_mquickjs_wifi_ap_retire_for_configuration(&s_wifi_lifecycle);
    if (error != ESP_OK) goto pending;
    stage = "restart-enterprise-station-retire";
    error = wifi_cleanup_helper(false);
    if (error != ESP_OK) { stage = s_wifi_state.cleanup_stage; goto pending; }
    return wifi_restart_restore_interfaces(mode, stored_ap, execution, false);
pending:
    s_wifi_state.runtime_cleanup_pending = true;
    s_wifi_state.cleanup_stage = stage;
    s_wifi_state.cleanup_error = error;
    if (execution) execution->stage = stage;
    return error;
}
#endif

static esp_err_t wifi_restart_retry_interfaces(bool allow_ap_restart,
    esp32_mquickjs_wifi_configuration_execution_t *execution)
{
    const char *stage = "restart-retry-admission";
    if (execution != NULL) execution->stage = stage;
    if (!s_wifi_configuration_cleanup || s_wifi_lifecycle.identity == 0U ||
        s_wifi_configuration_stop_only || s_wifi_configuration_disconnect_pending ||
        s_wifi_ap_stop_cleanup || !wifi_helpers_idle(false)) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_radio_restart_selection_t selection = {.allow_ap_restart = allow_ap_restart};
    esp_err_t err;
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    bool enterprise = s_wifi_eap_stop.profile != NULL;
    if (enterprise) selection.mode = s_wifi_configuration_mode;
    err = enterprise ? ESP_OK : esp32_mquickjs_wifi_radio_admit_restart_retry(&s_wifi_lifecycle, &selection);
#else
    err = esp32_mquickjs_wifi_radio_admit_restart_retry(&s_wifi_lifecycle, &selection);
#endif
    if (err != ESP_OK) return err;
    if (execution != NULL) execution->admitted = true;
    wifi_config_t *stored_ap = NULL;
    if (!selection.restore_off && (selection.mode & WIFI_MODE_AP)) {
        stage = "restart-retry-ap-allocate";
        stored_ap = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*stored_ap),
            ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
        if (stored_ap == NULL) { err = ESP_ERR_NO_MEM; goto pending; }
    }
    s_wifi_state.runtime_cleanup_pending = true;
    s_wifi_configuration_mode = selection.mode;
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (enterprise) {
        err = wifi_restart_enterprise_interfaces(stored_ap, allow_ap_restart, execution);
        goto free_ap;
    }
#endif
    stage = "restart-retry-stop";
    if (execution != NULL) execution->stop_attempted = true;
    err = esp32_mquickjs_wifi_radio_quiesce_lifecycle(&s_wifi_lifecycle);
    if (err != ESP_OK) goto pending;
    stage = "restart-retry-ap-retire";
    err = esp32_mquickjs_wifi_ap_retire_for_configuration(&s_wifi_lifecycle);
    if (err != ESP_OK) goto pending;
    stage = "restart-retry-station-retire";
    err = wifi_cleanup_helper(false);
    if (err != ESP_OK) { stage = s_wifi_state.cleanup_stage; goto pending; }
    /* Rebuild uses the original complete checkpoint, including its off flag.
     * Never checkpoint the faulted generation or finish/discard the token on
     * the way to replay. SDK/helper suffixes record their own completed work. */
    err = wifi_restart_restore_interfaces(selection.mode, stored_ap, execution, selection.restore_off);
    goto free_ap;
pending:
    s_wifi_state.runtime_cleanup_pending = true;
    s_wifi_state.cleanup_stage = stage;
    s_wifi_state.cleanup_error = err;
    if (execution != NULL) execution->stage = stage;
free_ap:
    if (stored_ap != NULL) {
        esp32_mquickjs_wireless_secure_zero(stored_ap, sizeof(*stored_ap));
        esp32_mquickjs_memory_payload_free(stored_ap);
    }
    return err;
}

/* Internal reconstruction shared by managed-owner and zero-owner admission.
 * Never disconnect a Station or cancel a pending native operation here. */
static esp_err_t wifi_restart_interfaces_inner(wifi_mode_t mode, bool stopped_only,
    esp32_mquickjs_wifi_configuration_execution_t *execution, const bool *wapi_enabled,
    bool allow_ap_restart)
{
    if (execution != NULL) *execution = (esp32_mquickjs_wifi_configuration_execution_t){.stage = "restart-admission"};
    /* Only an explicit public restart may resume a retained full checkpoint.
     * WAPI controls and other coordinators keep their own operation ownership. */
    if (stopped_only && wapi_enabled == NULL && s_wifi_configuration_cleanup)
        return wifi_restart_retry_interfaces(allow_ap_restart, execution);
    if (!stopped_only && mode != WIFI_MODE_STA && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA)
        return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (mode & WIFI_MODE_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    if (s_wifi_configuration_cleanup || s_wifi_ap_stop_cleanup || s_wifi_lifecycle.identity != 0U ||
        s_wifi_state.runtime_cleanup_pending || s_wifi_state.cleanup_stage != NULL ||
        !wifi_helpers_idle(false)) return ESP_ERR_INVALID_STATE;
    /* Reserve fallible AP validation storage before transferring any owner. */
    wifi_config_t *stored_ap = NULL;
    bool needs_ap_storage = (mode & WIFI_MODE_AP) != 0;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    /* The atomic selector has not resolved the mode yet. */
    needs_ap_storage = needs_ap_storage || stopped_only;
#endif
    if (needs_ap_storage) {
        stored_ap = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*stored_ap), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
        if (stored_ap == NULL) {
            if (execution != NULL) execution->stage = "restart-ap-allocate";
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t err;
    bool cold_source = false, restore_off = false;
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    uint64_t enterprise_binding = stopped_only && wapi_enabled == NULL ? esp32_mquickjs_wifi_radio_eap_identity() : 0;
    if (enterprise_binding) {
        err = wifi_begin_enterprise_restart(enterprise_binding, allow_ap_restart, &mode);
        if (err == ESP_OK) {
            s_wifi_configuration_cleanup = true;
            s_wifi_configuration_stop_only = false;
            s_wifi_configuration_disconnect_pending = false;
        }
    } else
#endif
    if (stopped_only) {
        esp32_mquickjs_wifi_radio_restart_selection_t selection = {.allow_ap_restart = allow_ap_restart};
        err = esp32_mquickjs_wifi_ap_begin_stopped_restart(&s_wifi_lifecycle, &selection);
        if (err == ESP_OK) {
            mode = selection.mode;
            cold_source = selection.cold;
            restore_off = selection.restore_off;
            s_wifi_configuration_cleanup = true;
            s_wifi_configuration_stop_only = false;
            s_wifi_configuration_disconnect_pending = false;
        }
    } else {
        err = wifi_begin_configuration_cleanup(false);
    }
    if (err != ESP_OK) goto free_ap;
    if (execution != NULL) execution->admitted = true;
    s_wifi_configuration_mode = mode;
    s_wifi_state.runtime_cleanup_pending = true;
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (s_wifi_eap_stop.profile) {
        err = wifi_restart_enterprise_interfaces(stored_ap, allow_ap_restart, execution);
        goto free_ap;
    }
#endif
    esp32_mquickjs_wifi_radio_release(&s_wifi_application);
    esp32_mquickjs_wifi_radio_release(&s_wifi_state.radio_lease);
    const char *stage = "restart-checkpoint";
    esp32_mquickjs_wifi_radio_stop_snapshot_t stopped;
    esp32_mquickjs_wifi_radio_stop_snapshot(&stopped);
    if (!cold_source && (stopped_only || stopped.unchanged)) {
        /* Public stop may have removed every helper. Old-generation capture
         * can need a temporary Station START to read an inactive band, so
         * retire stale helper storage and prepare Station before capture. */
        stage = "restart-stopped-ap-retire";
        err = esp32_mquickjs_wifi_ap_retire_for_configuration(&s_wifi_lifecycle);
        if (err != ESP_OK) goto pending;
        stage = "restart-stopped-station-retire";
        err = wifi_cleanup_helper(false);
        if (err != ESP_OK) { stage = s_wifi_state.cleanup_stage; goto pending; }
        stage = "restart-stopped-station-prepare";
        err = esp32_mquickjs_wifi_prepare_for_configuration(&s_wifi_lifecycle);
        if (err != ESP_OK) goto pending;
        if (!stopped.unchanged && (mode & WIFI_MODE_AP)) {
            /* Without qualified STOP history, checkpoint starts the current
             * configured source before reading runtime-only observations. */
            stage = "restart-stopped-ap-prepare";
            err = esp32_mquickjs_wifi_ap_prepare_for_configuration(&s_wifi_lifecycle);
            if (err != ESP_OK) goto pending;
        }
    }
    stage = "restart-checkpoint";
    if (execution != NULL) execution->stop_attempted = true;
    err = esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(&s_wifi_lifecycle, mode);
    if (err != ESP_OK) goto pending;
    stage = "restart-ap-retire";
    err = esp32_mquickjs_wifi_ap_retire_for_configuration(&s_wifi_lifecycle);
    if (err != ESP_OK) goto pending;
    stage = "restart-station-retire";
    err = wifi_cleanup_helper(false);
    if (err != ESP_OK) { stage = s_wifi_state.cleanup_stage; goto pending; }
#if CONFIG_ESP_WIFI_WAPI_PSK
    if (wapi_enabled) {
        stage = "wapi-policy-select";
        err = esp32_mquickjs_wifi_radio_wapi_select(&s_wifi_lifecycle, mode, *wapi_enabled);
        if (err != ESP_OK) goto pending;
    }
#else
    (void)wapi_enabled;
#endif
    err = wifi_restart_restore_interfaces(mode, stored_ap, execution, restore_off);
    goto free_ap;
pending:
    s_wifi_state.runtime_cleanup_pending = true;
    s_wifi_state.cleanup_stage = stage;
    s_wifi_state.cleanup_error = err;
    if (execution != NULL) execution->stage = stage;
free_ap:
    if (stored_ap != NULL) {
        esp32_mquickjs_wireless_secure_zero(stored_ap, sizeof(*stored_ap));
        esp32_mquickjs_memory_payload_free(stored_ap);
    }
    return err;
}

esp_err_t esp32_mquickjs_wifi_restart_interfaces(wifi_mode_t mode,
    esp32_mquickjs_wifi_configuration_execution_t *execution)
{
    return wifi_restart_interfaces_inner(mode, false, execution, NULL, false);
}

esp_err_t esp32_mquickjs_wifi_restart_stopped_interfaces(
    bool allow_ap_restart, esp32_mquickjs_wifi_configuration_execution_t *execution)
{
    return wifi_restart_interfaces_inner(WIFI_MODE_NULL, true, execution, NULL, allow_ap_restart);
}

/* Per-caller progress is kept across scheduler turns; the existing global
 * reservation is the sole authority. Public stop/runtime cleanup can consume
 * it, so every step checks identity before any driver/helper mutation. */
enum {
    WIFI_RECOVERY_NEW,
    WIFI_RECOVERY_CHECKPOINT,
    WIFI_RECOVERY_STOP,
    WIFI_RECOVERY_AP_RETIRE,
    WIFI_RECOVERY_STA_RETIRE,
    WIFI_RECOVERY_NATIVE_DRAIN,
    WIFI_RECOVERY_RESTORE,
    WIFI_RECOVERY_COMPLETE,
    WIFI_RECOVERY_FAILED,
};

void esp32_mquickjs_wifi_recovery_dispose(esp32_mquickjs_wifi_recovery_t *state)
{
    if (state == NULL) return;
    if (state->access_point != NULL) {
        esp32_mquickjs_wireless_secure_zero(state->access_point, sizeof(*state->access_point));
        esp32_mquickjs_memory_payload_free(state->access_point);
    }
    /* The central coordinator retains any admitted cleanup obligation. */
    memset(state, 0, sizeof(*state));
}

esp_err_t esp32_mquickjs_wifi_recovery_begin(
    esp32_mquickjs_wifi_recovery_t *state,
    const esp32_mquickjs_wifi_recovery_request_t *operation, bool allow_disconnect)
{
    if (state == NULL || state->phase != WIFI_RECOVERY_NEW || state->access_point != NULL ||
        state->lifecycle.identity != 0U || state->lifecycle.generation != 0U || operation == NULL ||
        operation->generation == 0U) return ESP_ERR_INVALID_ARG;
    bool generation_recovery = false;
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    generation_recovery = operation->kind == ESP32_MQUICKJS_WIFI_RECOVERY_TWT;
#endif
    if (generation_recovery ? operation->identity != 0U || !allow_disconnect : operation->identity == 0U)
        return ESP_ERR_INVALID_ARG;
    if (!generation_recovery && operation->kind != ESP32_MQUICKJS_WIFI_RECOVERY_ACTION
        && operation->kind != ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
        && operation->kind != ESP32_MQUICKJS_WIFI_RECOVERY_FTM
#endif
    ) return ESP_ERR_INVALID_ARG;
    state->execution = (esp32_mquickjs_wifi_configuration_execution_t){.stage = "recovery-admission"};
    if (s_wifi_configuration_cleanup || s_wifi_ap_stop_cleanup || s_wifi_lifecycle.identity != 0U ||
        s_wifi_state.runtime_cleanup_pending || s_wifi_state.cleanup_stage != NULL ||
        !wifi_helpers_idle(allow_disconnect)) return ESP_ERR_INVALID_STATE;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    /* Mode is resolved by atomic Radio admission; reserve fallible AP storage
     * before transferring any owner, including a later APSTA resolution. */
    state->access_point = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*state->access_point), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state->access_point == NULL) {
        state->execution.stage = "recovery-ap-allocate";
        return ESP_ERR_NO_MEM;
    }
#endif
    esp_err_t err = esp32_mquickjs_wifi_ap_begin_recovery(
        &s_wifi_application, &s_wifi_state.radio_lease, operation, &s_wifi_lifecycle, &state->mode);
    if (err != ESP_OK) {
        if (state->access_point != NULL) {
            esp32_mquickjs_wireless_secure_zero(state->access_point, sizeof(*state->access_point));
            esp32_mquickjs_memory_payload_free(state->access_point);
            state->access_point = NULL;
        }
        return err;
    }
    state->lifecycle = s_wifi_lifecycle;
    state->execution.admitted = true;
    state->phase = WIFI_RECOVERY_CHECKPOINT;
    s_wifi_configuration_cleanup = true;
    s_wifi_configuration_mode = state->mode;
    s_wifi_configuration_stop_only = false;
    wifi_lock();
    s_wifi_configuration_disconnect_pending = s_wifi_state.status.connected || s_wifi_state.connect_draining;
    wifi_unlock();
    s_wifi_state.runtime_cleanup_pending = true;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_recovery_step(
    esp32_mquickjs_wifi_recovery_t *state, bool *complete)
{
    if (complete == NULL) return ESP_ERR_INVALID_ARG;
    *complete = false;
    if (state == NULL || state->phase == WIFI_RECOVERY_NEW ||
        state->phase == WIFI_RECOVERY_FAILED) return ESP_ERR_INVALID_STATE;
    if (state->phase == WIFI_RECOVERY_COMPLETE) { *complete = true; return ESP_OK; }
    if (!s_wifi_configuration_cleanup || state->lifecycle.identity == 0U ||
        state->lifecycle.identity != s_wifi_lifecycle.identity ||
        state->lifecycle.generation != s_wifi_lifecycle.generation) {
        state->execution.stage = "recovery-lifecycle-lost";
        state->phase = WIFI_RECOVERY_FAILED;
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    const char *stage = "recovery-phase";
    switch (state->phase) {
    case WIFI_RECOVERY_CHECKPOINT:
        stage = "recovery-prepare";
        err = esp32_mquickjs_wifi_radio_prepare_recovery(&s_wifi_lifecycle);
        if (err != ESP_OK) break;
        stage = "recovery-disconnect";
        err = wifi_finish_configuration_disconnect();
        if (err != ESP_OK) break;
        esp32_mquickjs_wifi_radio_release(&s_wifi_application);
        esp32_mquickjs_wifi_radio_release(&s_wifi_state.radio_lease);
        stage = "recovery-checkpoint";
        state->execution.stop_attempted = true;
        err = esp32_mquickjs_wifi_radio_checkpoint_recovery(&s_wifi_lifecycle, state->mode);
        break;
    case WIFI_RECOVERY_STOP:
        stage = "recovery-stop";
        err = esp32_mquickjs_wifi_radio_stop_recovery(&s_wifi_lifecycle);
        break;
    case WIFI_RECOVERY_AP_RETIRE:
        stage = "recovery-ap-retire";
        err = esp32_mquickjs_wifi_ap_retire_for_recovery(&s_wifi_lifecycle);
        break;
    case WIFI_RECOVERY_STA_RETIRE:
        stage = "recovery-station-retire";
        err = esp32_mquickjs_wifi_retire_for_recovery(&s_wifi_lifecycle);
        if (err != ESP_OK && s_wifi_state.cleanup_stage != NULL) stage = s_wifi_state.cleanup_stage;
        break;
    case WIFI_RECOVERY_NATIVE_DRAIN:
        stage = "recovery-native-drain";
        err = esp32_mquickjs_wifi_radio_shutdown_recovery(&s_wifi_lifecycle);
        if (err == ESP_ERR_TIMEOUT) {
            /* Yield to the original native owner's Future or cleanup worker.
             * TWT drains before deinit; no owner lease is copied here. */
            state->execution.stage = stage;
            s_wifi_state.runtime_cleanup_pending = true;
            s_wifi_state.cleanup_stage = stage;
            s_wifi_state.cleanup_error = err;
            return ESP_OK;
        }
        if (err == ESP_OK) {
            stage = "recovery-restore-handoff";
            err = esp32_mquickjs_wifi_radio_finish_recovery(&s_wifi_lifecycle);
        }
        break;
    case WIFI_RECOVERY_RESTORE:
        /* Old helpers and the native owner have retired under this exact
         * lifecycle. Clear the stepper's drain diagnostics so helper init can
         * run; the central configuration reservation still owns cleanup, and
         * the restore suffix records any new failure before returning. */
        s_wifi_state.runtime_cleanup_pending = false;
        s_wifi_state.cleanup_stage = NULL;
        s_wifi_state.cleanup_error = ESP_OK;
        err = wifi_restart_restore_interfaces(state->mode, state->access_point, &state->execution, false);
        if (err != ESP_OK) { state->phase = WIFI_RECOVERY_FAILED; return err; }
        state->phase = WIFI_RECOVERY_COMPLETE;
        *complete = true;
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_STATE;
    }
    state->execution.stage = stage;
    s_wifi_state.runtime_cleanup_pending = true;
    s_wifi_state.cleanup_stage = stage;
    s_wifi_state.cleanup_error = err;
    if (err != ESP_OK) { state->phase = WIFI_RECOVERY_FAILED; return err; }
    ++state->phase;
    return ESP_OK;
}

static esp_err_t wifi_configure_selected_interfaces(
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    wifi_config_t *station, esp32_mquickjs_wifi_config_accept_fn accept_station,
    wifi_config_t *access_point, esp32_mquickjs_wifi_config_accept_fn accept_access_point,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    const esp32_mquickjs_wifi_radio_start_controls_t *start_controls,
    esp32_mquickjs_wifi_configuration_execution_t *execution)
{
    if (execution != NULL) *execution = (esp32_mquickjs_wifi_configuration_execution_t){.stage = "admission"};
    if (selection == NULL || selection->station_set != (station != NULL) ||
        selection->access_point_set != (access_point != NULL) ||
        (station != NULL && accept_station == NULL) ||
        (access_point != NULL && accept_access_point == NULL)) return ESP_ERR_INVALID_ARG;
    if (s_wifi_configuration_cleanup || s_wifi_state.runtime_cleanup_pending ||
        s_wifi_state.cleanup_stage != NULL) return ESP_ERR_INVALID_STATE;
    if (access_point != NULL) {
        esp_err_t validated = esp32_mquickjs_wifi_radio_validate_ap_config(access_point);
        if (validated != ESP_OK) return validated;
    }
    bool already_started = false;
    esp_err_t err = selection->start_only
        ? wifi_begin_start_configuration(selection, &already_started)
        : wifi_begin_selected_configuration(selection, controls, start_controls);
    if (err != ESP_OK) return err;
    if (already_started) return wifi_start_existing_running(selection);
    if (execution != NULL) execution->admitted = true;
    wifi_mode_t mode = selection->mode;
    wifi_storage_t storage = selection->storage;
    bool start = selection->start;
    s_wifi_state.runtime_cleanup_pending = true;
    wifi_config_t *stored_ap = NULL;
    const char *stage = "start-ap-allocate";
    if (selection->start_only && (mode & WIFI_MODE_AP)) {
        stored_ap = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*stored_ap), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
        if (stored_ap == NULL) { err = ESP_ERR_NO_MEM; goto pending; }
    }
    stage = "configuration-disconnect";
    err = wifi_finish_configuration_disconnect();
    if (err != ESP_OK) goto pending;
    esp32_mquickjs_wifi_radio_release(&s_wifi_application);
    esp32_mquickjs_wifi_radio_release(&s_wifi_state.radio_lease);
    stage = "configuration-stop";
    if (execution != NULL) execution->stop_attempted = true;
    err = esp32_mquickjs_wifi_radio_quiesce_lifecycle(&s_wifi_lifecycle);
    if (err != ESP_OK) goto pending;
    stage = "configuration-ap-retire";
    err = esp32_mquickjs_wifi_ap_retire_for_configuration(&s_wifi_lifecycle);
    if (err != ESP_OK) goto pending;
    err = wifi_cleanup_helper(false);
    if (err != ESP_OK) { stage = s_wifi_state.cleanup_stage; goto pending; }
    stage = "configuration-initialize";
    err = esp32_mquickjs_wifi_radio_initialize_lifecycle(&s_wifi_lifecycle);
    if (err != ESP_OK) goto pending;
    /* Prepare helper allocations/registrations before changing the configs.
     * The native transaction allocates its secure snapshots before its writes. */
    if (start && (mode & WIFI_MODE_STA)) {
        stage = "configuration-station-prepare";
        err = esp32_mquickjs_wifi_prepare_for_configuration(&s_wifi_lifecycle);
        if (err != ESP_OK) goto pending;
    }
    if (start && (mode & WIFI_MODE_AP)) {
        stage = "configuration-ap-prepare";
        err = esp32_mquickjs_wifi_ap_prepare_for_configuration(&s_wifi_lifecycle);
        if (err != ESP_OK) goto pending;
    }
    esp32_mquickjs_wifi_radio_lease_t *ap_slot = NULL;
    if (start && (mode & WIFI_MODE_AP)) {
        stage = "configuration-ap-slot";
        ap_slot = esp32_mquickjs_wifi_ap_configuration_slot(&s_wifi_lifecycle);
        if (ap_slot == NULL) { err = ESP_ERR_INVALID_STATE; goto pending; }
    }
    stage = "configuration-commit";
    esp32_mquickjs_wifi_radio_config_result_t result;
    if (execution != NULL) execution->configuration_attempted = true;
    err = esp32_mquickjs_wifi_radio_configure_lifecycle(&s_wifi_lifecycle,
        mode, storage, station, accept_station, access_point, accept_access_point, controls, &result);
    if (err != ESP_OK) goto pending;
    if (stored_ap != NULL) {
        stage = "start-ap-config";
        err = esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration(&s_wifi_lifecycle, stored_ap);
        if (err != ESP_OK) goto pending;
    }
    stage = "configuration-resume";
    if (execution != NULL) execution->resume_attempted = true;
    err = esp32_mquickjs_wifi_radio_resume_lifecycle(&s_wifi_lifecycle, mode, start,
        start && (mode & WIFI_MODE_STA) ? &s_wifi_application : NULL,
        start && (mode & WIFI_MODE_STA) ? &s_wifi_state.radio_lease : NULL, ap_slot, start_controls);
    if (err != ESP_OK) goto pending;
    s_wifi_configuration_cleanup = false;
    s_wifi_configuration_mode = WIFI_MODE_NULL;
    s_wifi_state.runtime_cleanup_pending = false;
    s_wifi_state.cleanup_stage = NULL;
    s_wifi_state.cleanup_error = ESP_OK;
    if (execution != NULL) execution->stage = "complete";
    if (stored_ap != NULL) {
        esp32_mquickjs_wireless_secure_zero(stored_ap, sizeof(*stored_ap));
        esp32_mquickjs_memory_payload_free(stored_ap);
    }
    return ESP_OK;
pending:
    if (stored_ap != NULL) {
        esp32_mquickjs_wireless_secure_zero(stored_ap, sizeof(*stored_ap));
        esp32_mquickjs_memory_payload_free(stored_ap);
    }
    /* Inputs belong to the caller and are never retained here. A failed
     * transaction must be cleaned, never resumed with recycled secret storage. */
    s_wifi_state.runtime_cleanup_pending = true;
    s_wifi_state.cleanup_stage = stage;
    s_wifi_state.cleanup_error = err;
    if (execution != NULL) execution->stage = stage;
    return err;
}

/* No Future is registered yet. Defaults resolve atomically under the Radio
 * mutation lock: preserve storage/mode, require START, and supply only STA.
 * An existing AP/APSTA mode cannot pass admission without an AP config, and
 * foreign leases cannot pass the exclusive lifecycle reservation. */
esp_err_t esp32_mquickjs_wifi_prepare_connect(wifi_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    if (config->sta.pmf_cfg.capable)
        return esp32_mquickjs_wifi_ensure_started();
    if (config->sta.pmf_cfg.required ||
        !esp32_mquickjs_wifi_radio_pmf_disable_allowed(WIFI_IF_STA, config))
        return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_radio_configuration_selection_t selection = {
        .station_set = true, .start_set = true, .start = true,
        .allow_disconnect = true,
    };
    return wifi_configure_selected_interfaces(&selection, config,
        esp32_mquickjs_wifi_radio_accept_station_config,
        NULL, NULL, NULL, NULL, NULL);
}

esp_err_t esp32_mquickjs_wifi_configure_interfaces(wifi_mode_t mode,
    wifi_storage_t storage, bool start, wifi_config_t *station,
    esp32_mquickjs_wifi_config_accept_fn accept_station, wifi_config_t *access_point,
    esp32_mquickjs_wifi_config_accept_fn accept_access_point,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    const esp32_mquickjs_wifi_radio_start_controls_t *start_controls, bool allow_disconnect)
{
    esp32_mquickjs_wifi_radio_configuration_selection_t selection = {
        .mode_set = true, .storage_set = true, .start_set = true,
        .mode = mode, .storage = storage, .start = start, .allow_disconnect = allow_disconnect,
        .station_set = station != NULL, .access_point_set = access_point != NULL,
    };
    return wifi_configure_selected_interfaces(&selection, station, accept_station,
        access_point, accept_access_point, controls, start_controls, NULL);
}

/* Caller has captured explicit allowDisconnect:true. Reuse the configuration
 * coordinator for STOP/fence, helper retirement, secure config readback and
 * START. On error only stop/runtime cleanup may consume the retained suffix;
 * the caller's credential buffer is never retained for automatic replay. */
esp_err_t esp32_mquickjs_wifi_activate_ap(wifi_config_t *config)
{
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_radio_configuration_selection_t selection = {
        .start_set = true, .start = true, .allow_disconnect = true,
        .access_point_set = true, .activate_ap = true,
    };
    return wifi_configure_selected_interfaces(&selection, NULL, NULL,
        config, esp32_mquickjs_wifi_radio_accept_ap_config, NULL, NULL, NULL);
#else
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t esp32_mquickjs_wifi_apply_configuration(esp32_mquickjs_wifi_configuration_t *configuration,
    esp32_mquickjs_wifi_configuration_execution_t *execution)
{
    if (execution != NULL) *execution = (esp32_mquickjs_wifi_configuration_execution_t){.stage = "admission"};
    if (configuration == NULL) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_radio_configuration_selection_t selection = {
        .mode_set = configuration->mode_set, .storage_set = configuration->storage_set,
        .start_set = configuration->start_set, .mode = configuration->mode,
        .storage = configuration->storage, .start = configuration->start,
        .allow_disconnect = configuration->allow_disconnect,
        .station_set = configuration->station_set, .access_point_set = configuration->access_point_set,
    };
    esp_err_t err = wifi_configure_selected_interfaces(&selection,
        configuration->station_set ? &configuration->station : NULL,
        esp32_mquickjs_wifi_radio_accept_station_config,
        configuration->access_point_set ? &configuration->access_point : NULL,
        esp32_mquickjs_wifi_radio_accept_ap_config, &configuration->controls, &configuration->start_controls, execution);
    if (err == ESP_OK) {
        configuration->mode = selection.mode;
        configuration->storage = selection.storage;
        configuration->start = selection.start;
    }
    return err;
}

JSValue js_wifi_configure(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.configure(options) expects one argument");
    esp32_mquickjs_wifi_configuration_t *configuration = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*configuration), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (configuration == NULL) return JS_ThrowOutOfMemory(ctx);
    bool captured = esp32_mquickjs_wifi_capture_configuration(ctx, argv[0], configuration);
    esp32_mquickjs_wifi_configuration_execution_t execution = {0};
    esp_err_t err = captured ? esp32_mquickjs_wifi_apply_configuration(configuration, &execution) : ESP_OK;
    esp32_mquickjs_wireless_secure_zero(configuration, sizeof(*configuration));
    esp32_mquickjs_memory_payload_free(configuration);
    if (!captured) return JS_EXCEPTION;
    if (err != ESP_OK) return esp32_mquickjs_wifi_throw_configuration_error(ctx, err, NULL, &execution);
    /* Result allocation is after commit. Preserve the accepted state on OOM;
     * callers inspect status before any explicit retry of a failed delivery. */
    return esp32_mquickjs_wifi_make_status_object(ctx);
}

static esp_err_t wifi_finish_runtime_cleanup(bool wait_for_terminal)
{
    if (s_wifi_configuration_cleanup) return wifi_finish_configuration_cleanup();
    esp_err_t vendor_error = esp32_mquickjs_wifi_radio_vendor_ie_clear(-1);
    if (vendor_error != ESP_OK) {
        s_wifi_state.cleanup_stage = "vendor-ie-clear";
        s_wifi_state.cleanup_error = vendor_error;
        return vendor_error;
    }
    if (s_wifi_state.cleanup_stage != NULL && !strcmp(s_wifi_state.cleanup_stage, "vendor-ie-clear")) {
        s_wifi_state.cleanup_stage = NULL;
        s_wifi_state.cleanup_error = ESP_OK;
    }
    if (!s_wifi_state.runtime_cleanup_pending) return wifi_cleanup_failed_init();
    TickType_t started = xTaskGetTickCount();
    for (;;) {
        (void)esp32_mquickjs_wifi_drain_scan();
        wifi_release_radio_operation();
        wifi_lock();
        bool scan_pending = s_wifi_state.scan_in_progress || s_wifi_state.scan_draining ||
            s_wifi_state.scan_results_pending || s_wifi_state.scan_start_active || s_wifi_state.scan_stop_active;
        bool connect_pending = s_wifi_state.connect_in_progress || s_wifi_state.connect_draining ||
            s_wifi_state.connect_start_active || s_wifi_state.disconnect_active;
        esp_err_t native_error = scan_pending ? s_wifi_state.scan_cleanup_error :
            s_wifi_state.disconnect_cleanup_error;
        wifi_unlock();
        if (!scan_pending && !connect_pending) {
            if (s_wifi_ap_stop_cleanup) {
                esp_err_t err = wifi_adopt_ap_stop_cleanup();
                if (err == ESP_OK) return wifi_finish_configuration_cleanup();
                s_wifi_state.cleanup_stage = "ap-stop-admission";
                s_wifi_state.cleanup_error = err;
                return err;
            }
            if (esp32_mquickjs_wifi_ap_control_lease() != NULL) {
                esp_err_t err = wifi_begin_configuration_cleanup(false);
                if (err == ESP_OK) return wifi_finish_configuration_cleanup();
                s_wifi_state.cleanup_stage = "configuration-admission";
                s_wifi_state.cleanup_error = err;
                return err;
            }
            return wifi_cleanup_failed_init();
        }
        if (!wait_for_terminal || native_error != ESP_OK ||
            (TickType_t)(xTaskGetTickCount() - started) >= pdMS_TO_TICKS(1000)) {
            s_wifi_state.cleanup_stage = scan_pending ? "scan-drain" : "connection-drain";
            s_wifi_state.cleanup_error = native_error != ESP_OK ? native_error : ESP_ERR_INVALID_STATE;
            return s_wifi_state.cleanup_error;
        }
        /* No JS/runtime consumer is attached here. Only native callbacks can
         * satisfy the barrier; the bounded wait never resubmits driver work. */
        vTaskDelay(1);
    }
}

bool esp32_mquickjs_init_wifi_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime)
{
    if (!esp32_mquickjs_init_wifi_wake_runtime(ctx)) return false;
    /* A new runtime is an explicit retry boundary for an unfinished helper
     * cleanup. Do not register a new consumer over retained callback storage. */
    if (s_wifi_state.runtime_cleanup_pending) {
        /* One explicit retry of unfinished native cancellation, followed by
         * a bounded wait for terminal notifications before attaching JS. */
        wifi_lock();
        bool cancel_connect = s_wifi_state.connect_in_progress ||
            s_wifi_state.connect_draining || s_wifi_state.status.connected;
        uint32_t connect_generation = s_wifi_state.connect_generation;
        uint32_t scan_generation = s_wifi_state.scan_generation;
        wifi_unlock();
        if (cancel_connect) (void)esp32_mquickjs_wifi_cancel_connect(connect_generation);
        (void)esp32_mquickjs_wifi_cancel_scan(scan_generation);
    }
    if ((s_wifi_state.runtime_cleanup_pending || s_wifi_state.cleanup_stage != NULL) &&
        wifi_finish_runtime_cleanup(true) != ESP_OK) {
        JS_ThrowInternalError(ctx, "Wi-Fi cleanup pending at %s: %s",
            s_wifi_state.cleanup_stage,
            esp_err_to_name(s_wifi_state.cleanup_error));
        return false;
    }
    /* A borrowed AP transaction must retire through the central coordinator
     * before the independent AP helper is allowed to attach another runtime. */
    if (!esp32_mquickjs_init_wifi_ap_runtime(ctx)) return false;
    s_wifi_runtime = runtime;
    if (!esp32_mquickjs_init_wifi_future_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
    if (!esp32_mquickjs_init_wifi_action_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (!esp32_mquickjs_init_wifi_twt_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (!esp32_mquickjs_init_wifi_ftm_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
#endif
#if CONFIG_ESP_WIFI_RRM_SUPPORT
    if (!esp32_mquickjs_init_wifi_neighbor_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
#endif
    if (!esp32_mquickjs_init_wifi_raw_tx_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (!esp32_mquickjs_init_wifi_smartconfig_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
    if (esp32_mquickjs_wifi_smartconfig_open_runtime() != ESP_OK) {
        JS_ThrowInternalError(ctx, "SmartConfig native cleanup or handles remain");
        s_wifi_runtime = NULL;
        return false;
    }
    if (!esp32_mquickjs_init_wifi_wps_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
    if (esp32_mquickjs_wifi_wps_open_runtime() != ESP_OK) {
        JS_ThrowInternalError(ctx, "WPS native cleanup or handles remain");
        s_wifi_runtime = NULL;
        return false;
    }
#if CONFIG_ESP_WIFI_DPP_SUPPORT
    if (!esp32_mquickjs_init_wifi_dpp_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
    if (esp32_mquickjs_wifi_dpp_open_runtime() != ESP_OK) {
        JS_ThrowInternalError(ctx, "DPP native cleanup or handles remain");
        s_wifi_runtime = NULL;
        return false;
    }
#endif
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
    if (esp32_mquickjs_wifi_wps_ap_open_runtime() != ESP_OK) {
        JS_ThrowInternalError(ctx, "AP WPS native cleanup or handles remain");
        s_wifi_runtime = NULL;
        return false;
    }
#endif
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE
    if (!esp32_mquickjs_init_wifi_nan_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
    if (esp32_mquickjs_wifi_nan_open_runtime() != ESP_OK) {
        JS_ThrowInternalError(ctx, "NAN native cleanup or handles remain");
        s_wifi_runtime = NULL;
        return false;
    }
#endif
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
    if (!esp32_mquickjs_init_wifi_mesh_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
    if (esp32_mquickjs_wifi_mesh_open_runtime() != ESP_OK) {
        JS_ThrowInternalError(ctx, "Mesh native cleanup remains");
        s_wifi_runtime = NULL;
        return false;
    }
#endif
    if (!esp32_mquickjs_register_async_poller(
            runtime, wifi_driver_event_poller, NULL)) {
        JS_ThrowInternalError(ctx,
                              "failed to register Wi-Fi driver event poller");
        s_wifi_runtime = NULL;
        return false;
    }
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (esp32_mquickjs_wifi_eap_config_open() != ESP_OK) {
        JS_ThrowInternalError(ctx, "enterprise configuration retirement or revision exhausted");
        s_wifi_runtime = NULL;
        return false;
    }
    if (!esp32_mquickjs_init_wifi_enterprise_runtime(ctx, runtime)) {
        s_wifi_runtime = NULL;
        return false;
    }
#endif
    return true;
}

void esp32_mquickjs_deinit_wifi_runtime(JSContext *ctx)
{
    (void)ctx;
    /* Detach before waiting: callbacks that start now cannot retain the dying
     * runtime. Already-entered callbacks must exit before its storage is freed. */
    s_wifi_runtime = NULL;
    while (atomic_load_explicit(&s_wifi_state.callbacks_active, memory_order_acquire) != 0U)
        vTaskDelay(1);
    esp32_mquickjs_deinit_wifi_vendor_ie_watch_runtime();
    esp32_mquickjs_deinit_wifi_watch_runtime();
    esp32_mquickjs_deinit_wifi_wake_runtime();
    /* Keep a healthy AP owner until STA scan/connect have drained, then admit
     * all Wi-Fi owners together. Independent failed AP setup owns its own token
     * and continues through that helper's existing suffix instead. */
    bool coordinated = s_wifi_configuration_cleanup || s_wifi_ap_stop_cleanup ||
        esp32_mquickjs_wifi_ap_control_lease() != NULL;
    if (!coordinated) {
        esp32_mquickjs_deinit_wifi_ap_runtime();
        esp32_mquickjs_wifi_radio_release(&s_wifi_application);
    }
    if (!s_wifi_state.initialized) {
        if (coordinated) s_wifi_state.runtime_cleanup_pending = true;
        if (s_wifi_state.runtime_cleanup_pending || s_wifi_state.cleanup_stage != NULL)
            (void)wifi_finish_runtime_cleanup(false);
        return;
    }
    s_wifi_state.runtime_cleanup_pending = true;
    if (!s_wifi_state.cleanup_timer_stopped &&
        esp32_mquickjs_wifi_prepare_connect_timer() == ESP_OK)
        s_wifi_state.cleanup_timer_stopped = true;
    wifi_lock();
    bool cancel_connect = s_wifi_state.connect_in_progress ||
        s_wifi_state.connect_draining || s_wifi_state.status.connected;
    uint32_t connect_generation = s_wifi_state.connect_generation;
    uint32_t scan_generation = s_wifi_state.scan_generation;
    wifi_unlock();
    if (cancel_connect) (void)esp32_mquickjs_wifi_cancel_connect(connect_generation);
    (void)esp32_mquickjs_wifi_cancel_scan(scan_generation);

    wifi_lock();
    if (s_wifi_state.connect_generation != UINT32_MAX) s_wifi_state.connect_generation++;
    s_wifi_state.scan_future_registered = false;
    s_wifi_state.connect_future_registered = false;
    s_wifi_state.connection_future_operation = ESP32_MQUICKJS_WIFI_OPERATION_NONE;
    memset(&s_wifi_state.scan_future_token, 0, sizeof(s_wifi_state.scan_future_token));
    memset(&s_wifi_state.connect_future_token, 0, sizeof(s_wifi_state.connect_future_token));
    wifi_unlock();
    if (s_wifi_state.scan_queue != NULL) xQueueReset(s_wifi_state.scan_queue);
    if (s_wifi_state.connect_queue != NULL) xQueueReset(s_wifi_state.connect_queue);
    (void)wifi_finish_runtime_cleanup(false);
}

/* Runtime task only. Public stop disables the AP (including its clients), but
 * never disconnects a live Station or cancels a pending Future/foreign owner. */
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
static esp_err_t wifi_begin_enterprise_stop(uint64_t binding)
{
    if (s_wifi_lifecycle.identity || s_wifi_ap_stop_cleanup || !wifi_helpers_idle(false))
        return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_eap_config_status_t config;
    esp32_mquickjs_wifi_eap_config_status(&config);
    esp32_mquickjs_wifi_eap_profile_t *profile = NULL;
    esp_err_t err = esp32_mquickjs_wifi_eap_config_begin(config.revision,
        ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE, &s_wifi_eap_stop.control, &profile);
    if (err != ESP_OK) return err;
    wifi_release_radio_operation();
    bool has_ap = esp32_mquickjs_wifi_ap_control_lease() != NULL;
    err = esp32_mquickjs_wifi_ap_begin_enterprise_stop(binding,
        &s_wifi_application, &s_wifi_state.radio_lease, &s_wifi_lifecycle);
    if (err != ESP_OK) {
        (void)esp32_mquickjs_wifi_eap_config_finish(&s_wifi_eap_stop.control, false);
        return err;
    }
    s_wifi_eap_stop.binding = binding;
    s_wifi_eap_stop.release_ap = true;
    s_wifi_configuration_cleanup = true;
    s_wifi_configuration_stop_only = true;
    s_wifi_configuration_disconnect_pending = false;
    s_wifi_configuration_mode = has_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    return ESP_OK;
}
#endif

static esp_err_t wifi_stop_idle(void)
{
    if (s_wifi_configuration_cleanup) return wifi_finish_configuration_cleanup();
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    esp32_mquickjs_wifi_eap_config_status_t config;
    esp32_mquickjs_wifi_eap_config_status(&config);
    if (config.busy || config.closing) return ESP_ERR_INVALID_STATE;
    uint64_t binding = esp32_mquickjs_wifi_radio_eap_identity();
    if (binding) {
        esp_err_t err = wifi_begin_enterprise_stop(binding);
        return err == ESP_OK ? wifi_finish_configuration_cleanup() : err;
    }
#endif
    if (s_wifi_ap_stop_cleanup) {
        esp_err_t err = wifi_adopt_ap_stop_cleanup();
        return err == ESP_OK ? wifi_finish_configuration_cleanup() : err;
    }
    if (s_wifi_lifecycle.identity == 0U && esp32_mquickjs_wifi_ap_control_lease() != NULL) {
        bool station_owner = s_wifi_application.acquired || s_wifi_state.radio_lease.acquired;
        esp_err_t err = wifi_begin_configuration_cleanup(false);
        if (err != ESP_OK) return err;
        s_wifi_configuration_stop_only = true;
        s_wifi_configuration_mode = station_owner ? WIFI_MODE_APSTA : WIFI_MODE_AP;
        return wifi_finish_configuration_cleanup();
    }
    if (s_wifi_lifecycle.identity == 0U) {
        if (!wifi_helpers_idle(false)) return ESP_ERR_INVALID_STATE;
        wifi_release_radio_operation();
        esp_err_t err = esp32_mquickjs_wifi_radio_begin_lifecycle(
            &s_wifi_application, &s_wifi_state.radio_lease, NULL, &s_wifi_lifecycle);
        if (err != ESP_OK) return err;
    }
    s_wifi_state.runtime_cleanup_pending = true;
    esp32_mquickjs_wifi_radio_release(&s_wifi_application);
    /* Successful prefixes are removed once; a failure keeps the reservation
     * and rejects new owners until stop/runtime cleanup retries the suffix. */
    return wifi_cleanup_failed_init();
}

JSValue js_wifi_start(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc > 1) return JS_ThrowTypeError(ctx, "wifi.start(options?) expects at most one argument");
    esp32_mquickjs_wifi_radio_configuration_selection_t selection;
    if (!esp32_mquickjs_wifi_capture_start(ctx, argc ? argv[0] : JS_UNDEFINED, &selection)) return JS_EXCEPTION;
    esp_err_t err = wifi_configure_selected_interfaces(&selection, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (err != ESP_OK) return esp32_mquickjs_wifi_throw_operation_error(
        ctx, "WIFI_START_FAILED", "wifi.start", err, 0, 0);
    return wifi_make_status_object(ctx);
}

JSValue js_wifi_stop(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc > 1) return JS_ThrowTypeError(ctx, "wifi.stop(options?) expects at most one argument");
    uint32_t timeout_ms;
    if (!esp32_mquickjs_wifi_capture_stop(ctx, argc ? argv[0] : JS_UNDEFINED, &timeout_ms)) return JS_EXCEPTION;
    esp_err_t err = esp32_mquickjs_wifi_wait_begin(timeout_ms);
    if (err == ESP_OK) {
        err = wifi_stop_idle();
        esp32_mquickjs_wifi_wait_end();
    }
    if (err != ESP_OK) return esp32_mquickjs_wifi_throw_operation_error(
        ctx, "WIFI_STOP_FAILED", "wifi.stop", err, 0, 0);
    return wifi_make_status_object(ctx);
}

JSValue js_wifi_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS);
}

static JSValue wifi_throw_restart_error(JSContext *ctx, esp_err_t error,
    const esp32_mquickjs_wifi_configuration_execution_t *execution)
{
    esp32_mquickjs_wifi_radio_status_t radio = {0};
    (void)esp32_mquickjs_wifi_radio_get_status(&radio);
    uint32_t snapshot_bytes = esp32_mquickjs_wifi_radio_restart_snapshot_bytes();
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stage", JS_NewString(ctx,
            execution->stage != NULL ? execution->stage : "restart-cleanup")) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, error)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espName", JS_NewString(ctx, esp_err_to_name(error))) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "lifecycleAdmitted", JS_NewBool(execution->admitted)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "checkpointAttempted", JS_NewBool(execution->stop_attempted)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "replayAttempted", JS_NewBool(execution->configuration_attempted)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "resumeAttempted", JS_NewBool(execution->resume_attempted)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "cleanupPending", JS_NewBool(s_wifi_configuration_cleanup)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "restartRequired", JS_NewBool(radio.restart_required ||
            s_wifi_state.sta_detach_error != ESP_OK || esp32_mquickjs_wifi_ap_netif_cleanup_error() != ESP_OK)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "restartSnapshotBytes", JS_NewUint32(ctx, snapshot_bytes)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "radioFaultStage", radio.fault_stage != NULL ? JS_NewString(ctx, radio.fault_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "radioFaultError", radio.fault_stage != NULL ? JS_NewInt32(ctx, radio.fault_error) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "configuration", execution->stop_attempted ?
            wifi_make_configuration_status(ctx, &radio.configuration) : JS_NULL)) {
        JS_PopGCRef(ctx, &ref);
        return JS_EXCEPTION;
    }
    JSValue result = esp32_mquickjs_throw_native_error(ctx, "WIFI_RESTART_FAILED",
        "wifi.driver.restart", "Wi-Fi driver restart failed", *details);
    JS_PopGCRef(ctx, &ref);
    return result;
}

JSValue js_wifi_driver_restart(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc > 1) return JS_ThrowTypeError(ctx, "wifi.driver.restart(options?) expects at most one argument");
    uint32_t timeout_ms;
    bool allow_ap_restart;
    if (!esp32_mquickjs_wifi_capture_restart(ctx, argc ? argv[0] : JS_UNDEFINED, &timeout_ms, &allow_ap_restart)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_configuration_execution_t execution = {.stage = "restart-wait-admission"};
    esp_err_t err = esp32_mquickjs_wifi_wait_begin(timeout_ms);
    if (err == ESP_OK) {
        err = esp32_mquickjs_wifi_restart_stopped_interfaces(allow_ap_restart, &execution);
        esp32_mquickjs_wifi_wait_end();
    }
    if (err != ESP_OK) return wifi_throw_restart_error(ctx, err, &execution);
    return wifi_make_status_object(ctx);
}

static bool wifi_driver_helpers_ready(void)
{
    return !s_wifi_configuration_cleanup && !s_wifi_ap_stop_cleanup && s_wifi_lifecycle.identity == 0U &&
        !s_wifi_state.runtime_cleanup_pending && s_wifi_state.cleanup_stage == NULL && wifi_helpers_idle(true);
}

#if CONFIG_ESP_WIFI_WAPI_PSK
#include "esp32_mquickjs_wifi_wapi_public.inc"
#endif

#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp32_mquickjs_wifi_wps_station.inc"
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
#include "esp32_mquickjs_wifi_wps_ap_helper.inc"
#endif

esp_err_t esp32_mquickjs_wifi_smartconfig_capture_owners(esp32_mquickjs_wifi_radio_lease_t owners[3])
{
    if (!owners) return ESP_ERR_INVALID_ARG;
    if (!wifi_driver_helpers_ready() || !wifi_helpers_idle(false)) return ESP_ERR_INVALID_STATE;
    owners[0] = s_wifi_application;
    owners[1] = s_wifi_state.radio_lease;
    const esp32_mquickjs_wifi_radio_lease_t *ap = esp32_mquickjs_wifi_ap_control_lease();
    owners[2] = ap ? *ap : (esp32_mquickjs_wifi_radio_lease_t){0};
    return ESP_OK;
}

static bool wifi_smartconfig_connection_exact_locked(const esp32_mquickjs_wifi_radio_operation_t *owner,
    uint32_t generation)
{
    return owner && owner->identity && generation && generation == s_wifi_state.connect_generation &&
        generation == s_wifi_smartconfig_connect.generation &&
        owner->identity == s_wifi_smartconfig_connect.owner.identity &&
        owner->generation == s_wifi_smartconfig_connect.owner.generation &&
        owner->lease_identity == s_wifi_smartconfig_connect.owner.lease_identity &&
        owner->kind == s_wifi_smartconfig_connect.owner.kind;
}

static esp_err_t wifi_station_connect_submit(const esp32_mquickjs_wifi_radio_operation_t *owner,
    const wifi_config_t *config, uint32_t timeout_ms, uint32_t *generation)
{
    wifi_lock();
    s_wifi_smartconfig_connect.owner = *owner;
    s_wifi_smartconfig_connect.generation = ++s_wifi_state.connect_generation;
    s_wifi_smartconfig_connect.terminal = 0;
    s_wifi_smartconfig_connect.reason = 0;
    *generation = s_wifi_smartconfig_connect.generation;
    s_wifi_state.connection_future_operation = ESP32_MQUICKJS_WIFI_OPERATION_CONNECT;
    s_wifi_state.connect_start_active = true;
    wifi_unlock();
    esp_err_t error = wifi_start_connect_reserved(config, timeout_ms);
    wifi_lock();
    s_wifi_state.connect_start_active = false;
    wifi_unlock();
    /* A failed submission still returns its exact retained cleanup generation. */
    return error;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_connect_begin(
    const esp32_mquickjs_wifi_radio_operation_t *owner, const wifi_config_t *config,
    uint32_t timeout_ms, uint32_t *generation)
{
    if (!owner || !config || !timeout_ms || !generation || *generation ||
        !config->sta.pmf_cfg.capable) return ESP_ERR_INVALID_ARG;
    if (!wifi_driver_helpers_ready() || !wifi_helpers_idle(false)) {
        wifi_lock();
        bool draining = s_wifi_state.connect_draining && !esp32_mquickjs_wifi_connection_reserved_locked();
        wifi_unlock();
        return draining ? ESP_ERR_NOT_FINISHED : ESP_ERR_INVALID_STATE;
    }
    wifi_lock();
    bool exhausted = s_wifi_state.connect_generation == UINT32_MAX;
    wifi_unlock();
    if (exhausted) return ESP_ERR_NO_MEM;
    esp_err_t error = esp32_mquickjs_wifi_radio_smartconfig_connection_begin(owner);
    if (error != ESP_OK) return error;
    return wifi_station_connect_submit(owner, config, timeout_ms, generation);
}

esp_err_t esp32_mquickjs_wifi_smartconfig_connect_status(
    const esp32_mquickjs_wifi_radio_operation_t *owner, uint32_t generation,
    esp32_mquickjs_wifi_smartconfig_connection_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_lock();
    bool exact = wifi_smartconfig_connection_exact_locked(owner, generation);
    *status = (esp32_mquickjs_wifi_smartconfig_connection_status_t){0};
    if (exact) *status = (esp32_mquickjs_wifi_smartconfig_connection_status_t){
        .generation = generation, .terminal = s_wifi_smartconfig_connect.terminal,
        .reason = s_wifi_smartconfig_connect.reason, .connected = s_wifi_state.status.connected,
        .draining = s_wifi_state.connect_draining || s_wifi_state.disconnect_active || s_wifi_state.connect_start_active,
    };
    wifi_unlock();
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_connect_end(
    const esp32_mquickjs_wifi_radio_operation_t *owner, uint32_t generation, bool keep_connected)
{
    wifi_lock();
    bool exact = wifi_smartconfig_connection_exact_locked(owner, generation);
    bool keep = exact && keep_connected && !s_wifi_state.connect_in_progress &&
        s_wifi_smartconfig_connect.terminal == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS &&
        s_wifi_state.status.connected && !s_wifi_state.connect_draining;
    wifi_unlock();
#if CONFIG_ESP_WIFI_DPP_SUPPORT
    if (keep_connected && owner && owner->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP) return ESP_ERR_INVALID_STATE;
#endif
    if (!exact || (keep_connected && !keep)) return ESP_ERR_INVALID_STATE;
    esp_err_t error = ESP_OK;
    if (!keep) error = wifi_request_disconnect();
    wifi_lock();
    bool pending = s_wifi_state.connect_in_progress || s_wifi_state.connect_draining ||
        s_wifi_state.connect_start_active || s_wifi_state.disconnect_active;
    wifi_unlock();
    if (error != ESP_OK || pending) return error != ESP_OK ? error : ESP_ERR_NOT_FINISHED;
    error = esp32_mquickjs_wifi_prepare_connect_timer();
    if (error != ESP_OK) return error;
#if CONFIG_ESP_WIFI_DPP_SUPPORT
    if (owner->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP)
        error = esp32_mquickjs_wifi_radio_dpp_connection_end(owner);
    else
#endif
        error = esp32_mquickjs_wifi_radio_smartconfig_connection_end(owner);
    if (error != ESP_OK) return error;
    wifi_lock();
    memset(&s_wifi_smartconfig_connect, 0, sizeof(s_wifi_smartconfig_connect));
    s_wifi_state.connection_future_operation = ESP32_MQUICKJS_WIFI_OPERATION_NONE;
    wifi_unlock();
    return ESP_OK;
}
#if CONFIG_ESP_WIFI_DPP_SUPPORT
esp_err_t esp32_mquickjs_wifi_dpp_connect_begin(uint32_t capture_identity,
    const esp32_mquickjs_wifi_radio_operation_t *owner, const wifi_config_t *config,
    uint32_t timeout_ms, uint32_t *generation)
{
    if (!owner || owner->kind != ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP || !config ||
        !timeout_ms || !generation || *generation || !config->sta.pmf_cfg.capable) return ESP_ERR_INVALID_ARG;
    if (s_wifi_configuration_cleanup || s_wifi_ap_stop_cleanup || s_wifi_lifecycle.identity ||
        s_wifi_state.runtime_cleanup_pending || s_wifi_state.cleanup_stage || !s_wifi_state.sta_netif)
        return ESP_ERR_INVALID_STATE;
    wifi_lock();
    bool ready = capture_identity && capture_identity == s_wifi_wps_capture_identity && s_wifi_station_capture_dpp &&
        !s_wifi_smartconfig_connect.owner.identity && !s_wifi_state.connect_future_registered &&
        !s_wifi_state.scan_future_registered && !s_wifi_state.status.connected &&
        !s_wifi_state.connect_in_progress && !s_wifi_state.connect_draining && !s_wifi_state.connect_start_active &&
        !s_wifi_state.disconnect_active && !s_wifi_state.scan_in_progress && !s_wifi_state.scan_draining &&
        !s_wifi_state.scan_results_pending && !s_wifi_state.scan_start_active && !s_wifi_state.scan_stop_active;
    bool exhausted = s_wifi_state.connect_generation == UINT32_MAX;
    wifi_unlock();
    if (!ready || exhausted) return exhausted ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE;
    esp_err_t error = esp32_mquickjs_wifi_prepare_connect_timer();
    if (error != ESP_OK) return error;
    error = esp32_mquickjs_wifi_radio_dpp_connection_begin(owner);
    if (error != ESP_OK) return error;
    return wifi_station_connect_submit(owner, config, timeout_ms, generation);
}

esp_err_t esp32_mquickjs_wifi_dpp_connect_status(const esp32_mquickjs_wifi_radio_operation_t *owner,
    uint32_t generation, esp32_mquickjs_wifi_dpp_connection_status_t *status)
{
    if (!status || !owner || owner->kind != ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP) return ESP_ERR_INVALID_ARG;
    *status = (esp32_mquickjs_wifi_dpp_connection_status_t){0};
    wifi_lock();
    bool exact = wifi_smartconfig_connection_exact_locked(owner, generation);
    if (exact) {
        status->generation = generation; status->terminal = s_wifi_smartconfig_connect.terminal;
        status->connected = s_wifi_state.status.connected;
        status->draining = s_wifi_state.connect_draining || s_wifi_state.disconnect_active || s_wifi_state.connect_start_active;
        status->reason = status->terminal == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS && !status->connected ?
            s_wifi_state.status.last_disconnect_reason : s_wifi_smartconfig_connect.reason;
        if (status->connected) status->link = s_wifi_state.status.link;
    }
    wifi_unlock();
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_dpp_connect_end(const esp32_mquickjs_wifi_radio_operation_t *owner,
    uint32_t generation)
{
    if (!owner || owner->kind != ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP) return ESP_ERR_INVALID_ARG;
    return esp32_mquickjs_wifi_smartconfig_connect_end(owner, generation, false);
}
#endif

#endif

#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
/* Runtime-only snapshot. Worker receives values, never mutable helper pointers;
 * Radio revalidates all exact identities under its operation mutex. */
esp_err_t esp32_mquickjs_wifi_eap_capture_owners(esp32_mquickjs_wifi_radio_lease_t owners[3])
{
    if (!owners) return ESP_ERR_INVALID_ARG;
    if (!wifi_driver_helpers_ready() || !wifi_helpers_idle(false)) return ESP_ERR_INVALID_STATE;
    owners[0] = s_wifi_application;
    owners[1] = s_wifi_state.radio_lease;
    const esp32_mquickjs_wifi_radio_lease_t *ap = esp32_mquickjs_wifi_ap_control_lease();
    owners[2] = ap ? *ap : (esp32_mquickjs_wifi_radio_lease_t){0};
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_eap_activate(esp32_mquickjs_wifi_eap_profile_t *profile,
    esp32_mquickjs_wifi_eap_install_result_t *result)
{
    if (!result || !profile) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_eap_install_result_t){.stage = "helper-admission", .error = ESP_ERR_INVALID_STATE};
    if (!wifi_driver_helpers_ready() || !wifi_helpers_idle(false)) return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_radio_eap_install(&s_wifi_application, &s_wifi_state.radio_lease,
        esp32_mquickjs_wifi_ap_control_lease(), profile, result);
}

bool esp32_mquickjs_wifi_eap_prepare_runtime_destroy(void)
{
    esp32_mquickjs_wifi_eap_config_begin_close();
    if (s_wifi_eap_stop.control.identity && wifi_finish_configuration_cleanup() != ESP_OK)
        return false;
    esp32_mquickjs_wifi_eap_config_status_t config;
    esp32_mquickjs_wifi_eap_config_status(&config);
    if (config.busy) return false;
    uint64_t identity = esp32_mquickjs_wifi_radio_eap_identity();
    if (!identity) return esp32_mquickjs_wifi_eap_config_finish_close();
    /* Reuse native disconnect/epoch/fence handling. An observed DISCONNECTED
     * terminal alone is insufficient: idle also requires the reuse barrier. */
    bool pending = false;
    if (esp32_mquickjs_wifi_start_disconnect(&pending) != ESP_OK || pending) return false;
    (void)esp32_mquickjs_wifi_drain_scan();
    wifi_release_radio_operation();
    if (!wifi_helpers_idle(false)) return false;
    esp32_mquickjs_wifi_eap_install_result_t result;
    return esp32_mquickjs_wifi_radio_eap_clear(identity, &result) == ESP_OK &&
        esp32_mquickjs_wifi_eap_config_finish_close();
}

esp_err_t esp32_mquickjs_wifi_eap_disconnect_ready(bool *ready)
{
    if (!ready) return ESP_ERR_INVALID_ARG;
    *ready = false;
    bool pending = false;
    esp_err_t error = esp32_mquickjs_wifi_start_disconnect(&pending);
    if (error != ESP_OK || pending) return error;
    (void)esp32_mquickjs_wifi_drain_scan();
    wifi_release_radio_operation();
    *ready = wifi_helpers_idle(false);
    return ESP_OK;
}
#endif

#if CONFIG_ESP_WIFI_RRM_SUPPORT || CONFIG_ESP_WIFI_WNM_SUPPORT
esp_err_t esp32_mquickjs_wifi_roaming_execute(const esp32_mquickjs_wifi_btm_query_t *query,
    esp32_mquickjs_wifi_roaming_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_roaming_result_t){.stage = "helper-admission", .error = ESP_ERR_INVALID_STATE};
    if (!wifi_driver_helpers_ready()) return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_radio_roaming(&s_wifi_application, &s_wifi_state.radio_lease,
        esp32_mquickjs_wifi_ap_control_lease(), query, result);
}
#endif

#if CONFIG_ESP_WIFI_RRM_SUPPORT
esp_err_t esp32_mquickjs_wifi_rrm_reserve(esp32_mquickjs_wifi_radio_operation_t *token)
{
    if (!wifi_driver_helpers_ready()) return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_radio_rrm_reserve(&s_wifi_application, &s_wifi_state.radio_lease,
        esp32_mquickjs_wifi_ap_control_lease(), token);
}
#endif

esp_err_t esp32_mquickjs_wifi_apply_interval(uint16_t milliseconds,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "helper-admission", .error = ESP_ERR_INVALID_STATE};
    if (!wifi_driver_helpers_ready()) return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_radio_write_interval(&s_wifi_application, &s_wifi_state.radio_lease,
        esp32_mquickjs_wifi_ap_control_lease(), milliseconds, result);
}

esp_err_t esp32_mquickjs_wifi_apply_policy(esp32_mquickjs_wifi_policy_control_t control,
    wifi_interface_t interface, bool requested, bool *accepted,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (accepted == NULL || result == NULL) return ESP_ERR_INVALID_ARG;
    *accepted = false;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "helper-admission", .error = ESP_ERR_INVALID_STATE};
    if (!wifi_driver_helpers_ready()) return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_radio_write_policy(&s_wifi_application, &s_wifi_state.radio_lease,
        esp32_mquickjs_wifi_ap_control_lease(), control, interface, requested, accepted, result);
}

esp_err_t esp32_mquickjs_wifi_apply_band(bool band_mode, int32_t requested,
    int32_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (actual == NULL || result == NULL) return ESP_ERR_INVALID_ARG;
    *actual = 0;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "helper-admission", .error = ESP_ERR_INVALID_STATE};
    if (!wifi_driver_helpers_ready()) return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_radio_change_band(&s_wifi_application, &s_wifi_state.radio_lease,
        esp32_mquickjs_wifi_ap_control_lease(), band_mode, requested, actual, result);
}

#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
esp_err_t esp32_mquickjs_wifi_apply_twt_control(esp32_mquickjs_wifi_twt_control_kind_t kind,
    esp32_mquickjs_wifi_twt_control_t *value, uint32_t *generation,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (!value || !generation || !result) return ESP_ERR_INVALID_ARG;
    *generation = 0;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "helper-admission", .error = ESP_ERR_INVALID_STATE};
    if (!wifi_driver_helpers_ready() || !wifi_helpers_idle(true)) return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_radio_twt_control(&s_wifi_application, &s_wifi_state.radio_lease,
        esp32_mquickjs_wifi_ap_control_lease(), kind, value, generation, result);
}
#endif

esp_err_t esp32_mquickjs_wifi_apply_he_statistics(bool rx, uint8_t selection, bool enabled,
    esp32_mquickjs_wifi_he_statistics_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (!actual || !result) return ESP_ERR_INVALID_ARG;
    *actual = (esp32_mquickjs_wifi_he_statistics_t){0};
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "helper-admission", .error = ESP_ERR_INVALID_STATE};
    if (!wifi_driver_helpers_ready() || !wifi_helpers_idle(true)) return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_radio_write_he_statistics(&s_wifi_application, &s_wifi_state.radio_lease,
        esp32_mquickjs_wifi_ap_control_lease(), rx, selection, enabled, actual, result);
}

esp_err_t esp32_mquickjs_wifi_apply_scan_parameters(const wifi_scan_default_params_t *requested,
    wifi_scan_default_params_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (actual == NULL || result == NULL) return ESP_ERR_INVALID_ARG;
    memset(actual, 0, sizeof(*actual));
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "helper-admission", .error = ESP_ERR_INVALID_STATE};
    if (!wifi_driver_helpers_ready() || !wifi_helpers_idle(true)) return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_radio_write_scan_parameters(&s_wifi_application, &s_wifi_state.radio_lease,
        esp32_mquickjs_wifi_ap_control_lease(), requested, actual, result);
}

esp_err_t esp32_mquickjs_wifi_apply_connection_control(
    esp32_mquickjs_wifi_connection_control_t control, wifi_interface_t interface,
    int32_t requested, int32_t *actual, esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (actual == NULL || result == NULL) return ESP_ERR_INVALID_ARG;
    *actual = 0;
    *result = (esp32_mquickjs_wifi_radio_config_result_t){.stage = "helper-admission", .error = ESP_ERR_INVALID_STATE};
    /* Runtime task only. No implicit helper/driver initialization and no SDK
     * calls while holding the helper mutex. Radio revalidates every identity. */
    if (!wifi_driver_helpers_ready()) return ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_radio_connection_control(&s_wifi_application, &s_wifi_state.radio_lease,
        esp32_mquickjs_wifi_ap_control_lease(), control, interface, requested, actual, result);
}

JSValue js_wifi_set_power_save(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv)
{
    JSCStringBuf buffer;
    const char *text;
    const char *actual_text;
    wifi_ps_type_t requested;
    wifi_ps_type_t actual;
    esp_err_t err;

    (void)this_val;
    if (argc != 1 || !JS_IsString(ctx, argv[0]) ||
        (text = JS_ToCString(ctx, argv[0], &buffer)) == NULL) {
        return JS_ThrowTypeError(
            ctx, "wifi.setPowerSave(mode) expects none, minimum, or maximum");
    }
    if (strcmp(text, "none") == 0) {
        requested = WIFI_PS_NONE;
    } else if (strcmp(text, "minimum") == 0) {
        requested = WIFI_PS_MIN_MODEM;
    } else if (strcmp(text, "maximum") == 0) {
        requested = WIFI_PS_MAX_MODEM;
    } else {
        return JS_ThrowRangeError(
            ctx, "wifi.setPowerSave(mode) expects none, minimum, or maximum");
    }
    err = esp32_mquickjs_wifi_ensure_started();
    if (err == ESP_OK) {
        err = esp32_mquickjs_wifi_radio_set_power_save(
            &s_wifi_state.radio_lease, requested, &actual);
    }
    actual_text = err == ESP_OK ? wifi_power_save_to_string(actual) : NULL;
    if (err != ESP_OK || actual_text == NULL) {
        return JS_ThrowInternalError(
            ctx, "wifi.setPowerSave() failed: %s", esp_err_to_name(err));
    }
    return JS_NewString(ctx, actual_text);
}

JSValue js_wifi_set_tx_power(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv)
{
    double dbm;
    double quarter_dbm;
    int8_t requested;
    int8_t actual;
    esp_err_t err;

    (void)this_val;
    if (argc != 1 || JS_ToNumber(ctx, &dbm, argv[0]) != 0 ||
        !isfinite(dbm) || dbm < 2.0 || dbm > 20.0) {
        return JS_ThrowRangeError(
            ctx, "wifi.setTxPower(dbm) expects 2..20 dBm");
    }
    quarter_dbm = dbm * 4.0;
    if (floor(quarter_dbm) != quarter_dbm) {
        return JS_ThrowRangeError(
            ctx, "wifi.setTxPower(dbm) expects 0.25 dBm increments");
    }
    requested = (int8_t)quarter_dbm;
    err = esp32_mquickjs_wifi_ensure_started();
    if (err == ESP_OK) {
        err = esp32_mquickjs_wifi_radio_set_tx_power(
            &s_wifi_state.radio_lease, requested, &actual);
    }
    if (err != ESP_OK) {
        return JS_ThrowInternalError(
            ctx, "wifi.setTxPower() failed: %s", esp_err_to_name(err));
    }
    return JS_NewFloat64(ctx, (double)actual / 4.0);
}

static bool wifi_control_enum(JSContext *ctx, JSValue value, const char *name)
{
    JSCStringBuf buffer;
    size_t length;
    const char *text;
    return JS_IsString(ctx, value) &&
        (text = JS_ToCStringLen(ctx, &length, value, &buffer)) != NULL &&
        length == strlen(name) && memcmp(text, name, length) == 0;
}

static JSValue wifi_control_error(JSContext *ctx, const char *code, const char *operation,
                                  esp_err_t err, const esp32_mquickjs_wifi_radio_mutation_t *mutation)
{
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, err)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stage", mutation->stage ? JS_NewString(ctx, mutation->stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "driverAccepted", JS_NewBool(mutation->driver_accepted))) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    JSValue result = esp32_mquickjs_throw_native_error(ctx, code, operation,
        "Wi-Fi control operation failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

JSValue js_wifi_set_mac(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 2) return JS_ThrowTypeError(ctx, "wifi.setMac(interface, address) expects two arguments");
    wifi_interface_t interface;
    if (!wifi_parse_interface(ctx, argv[0], &interface)) return JS_EXCEPTION;
    if (!JS_IsString(ctx, argv[1])) return JS_ThrowTypeError(ctx, "wifi.setMac address must be a MAC string");
    JSCStringBuf buffer;
    size_t length;
    const char *text = JS_ToCStringLen(ctx, &length, argv[1], &buffer);
    if (text == NULL) return JS_EXCEPTION;
    uint8_t requested[6], actual[6];
    if (length != 17 || !esp32_mquickjs_wireless_parse_address(text, requested) ||
        (requested[0] & 1U) != 0 || memcmp(requested, "\0\0\0\0\0\0", 6) == 0)
        return JS_ThrowTypeError(ctx, "wifi.setMac expects a nonzero unicast MAC in xx:xx:xx:xx:xx:xx form");
    esp32_mquickjs_wifi_radio_mutation_t mutation;
    esp_err_t err = esp32_mquickjs_wifi_radio_set_mac(interface, requested, actual, &mutation);
    if (err != ESP_OK) return wifi_control_error(ctx, "WIFI_MAC_SET_FAILED", "wifi.setMac", err, &mutation);
    char address[18];
    esp32_mquickjs_wireless_format_address(actual, address);
    return JS_NewString(ctx, address);
}

JSValue js_wifi_set_country(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc < 1 || argc > 2 || !JS_IsString(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "wifi.setCountry(code, options?) expects a country code");
    JSCStringBuf buffer;
    size_t length;
    const char *text = JS_ToCStringLen(ctx, &length, argv[0], &buffer);
    if (text == NULL) return JS_EXCEPTION;
    if (length != 2 || !((text[0] >= 'A' && text[0] <= 'Z' && text[1] >= 'A' && text[1] <= 'Z') ||
                        (text[0] == '0' && text[1] == '1')))
        return JS_ThrowTypeError(ctx, "wifi.setCountry code must be two uppercase letters or 01");
    char code[3] = {text[0], text[1], 0};
    bool ieee80211d = true, policy_supplied = false;
    JSGCRef options_ref, value_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    if (argc == 2) {
        static const char *const keys[] = {"policy", "ieee80211d"};
        *options = argv[1];
        if (!esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.setCountry", keys, 2)) goto fail;
        *value = JS_GetPropertyStr(ctx, *options, "policy");
        if (JS_IsException(*value)) goto fail;
        if (!JS_IsUndefined(*value)) {
            policy_supplied = true;
            if (wifi_control_enum(ctx, *value, "auto")) ieee80211d = true;
            else if (wifi_control_enum(ctx, *value, "manual")) ieee80211d = false;
            else {
                JS_ThrowTypeError(ctx, "wifi.setCountry policy must be auto or manual");
                goto fail;
            }
        }
        *value = JS_GetPropertyStr(ctx, *options, "ieee80211d");
        if (JS_IsException(*value)) goto fail;
        if (!JS_IsUndefined(*value)) {
            if (!JS_IsBool(*value) || (policy_supplied && ieee80211d != (*value == JS_TRUE))) {
                JS_ThrowTypeError(ctx, "wifi.setCountry ieee80211d must be boolean and agree with policy");
                goto fail;
            }
            ieee80211d = *value == JS_TRUE;
        }
    }
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    wifi_country_t actual;
    esp32_mquickjs_wifi_radio_mutation_t mutation;
    esp_err_t err = esp32_mquickjs_wifi_radio_set_country_code(
        &s_wifi_state.radio_lease, code, ieee80211d, &actual, &mutation);
    if (err != ESP_OK) return wifi_control_error(ctx, "WIFI_COUNTRY_FAILED", "wifi.setCountry", err, &mutation);
    return esp32_mquickjs_wifi_country_to_js(ctx, &actual);
fail:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_set_channel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    uint32_t channel;
    if (argc < 1 || argc > 2 ||
        !esp32_mquickjs_value_to_bounded_u32(ctx, argv[0], 1, 177, &channel) ||
        (channel > 14 && esp32_mquickjs_wifi_radio_5ghz_channel_bit(channel) == 0))
        return JS_ThrowTypeError(ctx, "wifi.setChannel expects a valid channel number");
#if !CONFIG_SOC_WIFI_SUPPORT_5G
    if (channel > 14) return JS_ThrowTypeError(ctx, "wifi.setChannel 5 GHz is unsupported on this target");
#endif
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    JSGCRef options_ref, value_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    if (argc == 2) {
        static const char *const keys[] = {"secondaryChannel"};
        *options = argv[1];
        if (!esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.setChannel", keys, 1)) goto fail;
        *value = JS_GetPropertyStr(ctx, *options, "secondaryChannel");
        if (JS_IsException(*value)) goto fail;
        if (!JS_IsUndefined(*value)) {
            if (wifi_control_enum(ctx, *value, "none")) secondary = WIFI_SECOND_CHAN_NONE;
            else if (wifi_control_enum(ctx, *value, "above")) secondary = WIFI_SECOND_CHAN_ABOVE;
            else if (wifi_control_enum(ctx, *value, "below")) secondary = WIFI_SECOND_CHAN_BELOW;
            else {
                JS_ThrowTypeError(ctx, "wifi.setChannel secondaryChannel must be none, above or below");
                goto fail;
            }
        }
    }
    if (channel > 14 && secondary != WIFI_SECOND_CHAN_NONE) {
        JS_ThrowTypeError(ctx, "wifi.setChannel 5 GHz secondary channel is determined by the driver");
        goto fail;
    }
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    const esp32_mquickjs_wifi_radio_lease_t *lease = esp32_mquickjs_wifi_ap_control_lease();
    if (lease == NULL) lease = &s_wifi_state.radio_lease;
    uint8_t actual_primary;
    wifi_second_chan_t actual_secondary;
    uint32_t generation;
    esp32_mquickjs_wifi_radio_mutation_t mutation;
    esp_err_t err = esp32_mquickjs_wifi_radio_change_channel(lease, channel, secondary,
        &actual_primary, &actual_secondary, &generation, &mutation);
    if (err != ESP_OK) return wifi_control_error(ctx, "WIFI_CHANNEL_FAILED", "wifi.setChannel", err, &mutation);
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    const char *band = actual_primary >= 1 && actual_primary <= 14 ? "2.4GHz" :
        esp32_mquickjs_wifi_radio_5ghz_channel_bit(actual_primary) ? "5GHz" : NULL;
    const char *actual_second = actual_secondary == WIFI_SECOND_CHAN_NONE ? "none" :
        actual_secondary == WIFI_SECOND_CHAN_ABOVE ? "above" :
        actual_secondary == WIFI_SECOND_CHAN_BELOW ? "below" : NULL;
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "channel", JS_NewUint32(ctx, actual_primary)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "secondaryChannel", actual_second ? JS_NewString(ctx, actual_second) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "band", band ? JS_NewString(ctx, band) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "channelGeneration", JS_NewUint32(ctx, generation))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return wifi_make_status_object(ctx);
}

JSValue js_wifi_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef global_ref;
    JSGCRef wifi_ref;
    JSGCRef scan_ref;
    JSValue *global;
    JSValue *wifi;
    JSValue *scan;
    JSValue result;

    (void)this_val;
    global = JS_PushGCRef(ctx, &global_ref);
    wifi = JS_PushGCRef(ctx, &wifi_ref);
    scan = JS_PushGCRef(ctx, &scan_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_GetPropertyStr(ctx, *global, "wifi");
    *scan = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "scan");
    if (JS_IsException(*global) || JS_IsException(*wifi) || JS_IsException(*scan)) {
        result = JS_EXCEPTION;
    } else {
        result = esp32_mquickjs_future_call_and_wait(ctx,
                                                     esp32_mquickjs_get_active_runtime(),
                                                     *scan,
                                                     *wifi,
                                                     argc,
                                                     argv);
    }
    JS_PopGCRef(ctx, &scan_ref);
    JS_PopGCRef(ctx, &wifi_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

JSValue js_wifi_connect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef global_ref;
    JSGCRef wifi_ref;
    JSGCRef connect_ref;
    JSValue *global;
    JSValue *wifi;
    JSValue *connect;
    JSValue result;

    (void)this_val;
    global = JS_PushGCRef(ctx, &global_ref);
    wifi = JS_PushGCRef(ctx, &wifi_ref);
    connect = JS_PushGCRef(ctx, &connect_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_GetPropertyStr(ctx, *global, "wifi");
    *connect = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "connect");
    if (JS_IsException(*global) || JS_IsException(*wifi) || JS_IsException(*connect)) {
        result = JS_EXCEPTION;
    } else {
        result = esp32_mquickjs_future_call_and_wait(ctx,
                                                     esp32_mquickjs_get_active_runtime(),
                                                     *connect,
                                                     *wifi,
                                                     argc,
                                                     argv);
    }
    JS_PopGCRef(ctx, &connect_ref);
    JS_PopGCRef(ctx, &wifi_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

JSValue js_wifi_disconnect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef global_ref;
    JSGCRef wifi_ref;
    JSGCRef disconnect_ref;
    JSValue *global;
    JSValue *wifi;
    JSValue *disconnect;
    JSValue result;

    (void)this_val;
    global = JS_PushGCRef(ctx, &global_ref);
    wifi = JS_PushGCRef(ctx, &wifi_ref);
    disconnect = JS_PushGCRef(ctx, &disconnect_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_GetPropertyStr(ctx, *global, "wifi");
    *disconnect = JS_IsException(*wifi)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "disconnect");
    if (JS_IsException(*global) || JS_IsException(*wifi) ||
        JS_IsException(*disconnect)) {
        result = JS_EXCEPTION;
    } else {
        result = esp32_mquickjs_future_call_and_wait(
            ctx, esp32_mquickjs_get_active_runtime(), *disconnect, *wifi,
            argc, argv);
    }
    JS_PopGCRef(ctx, &disconnect_ref);
    JS_PopGCRef(ctx, &wifi_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

#endif
