#include "esp32_mquickjs_wifi.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_net.h"
#include "esp32_mquickjs_future.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_STARTED_BIT BIT2
#define WIFI_SCAN_EVENT_QUEUE_LEN 1
#define WIFI_SCAN_MAX_RESULTS 32
#define WIFI_CONNECT_EVENT_QUEUE_LEN 1
#define WIFI_SCAN_BSSID_STR_LEN 18
#define WIFI_START_TIMEOUT_MS 5000

static const char *TAG = "esp32qjs_wifi";

static esp32_mquickjs_wifi_state_t s_wifi_state;
static void wifi_stop_connect_timeout_timer(void);

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
}

static void wifi_stop_connect_timeout_timer(void)
{
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
                                     int32_t reason)
{
    esp32_mquickjs_future_token_t token = {0};
    bool should_wake = false;
    esp32_mquickjs_wifi_connect_event_t event = {
        .generation = generation,
        .kind = kind,
        .reason = reason,
    };

    if (s_wifi_state.connect_queue != NULL) {
        xQueueOverwrite(s_wifi_state.connect_queue, &event);
        wifi_lock();
        should_wake = s_wifi_state.connect_future_registered;
        token = s_wifi_state.connect_future_token;
        wifi_unlock();
        if (should_wake) {
            (void)esp32_mquickjs_future_wake(esp32_mquickjs_get_active_runtime(), token);
        }
        esp32_mquickjs_notify_activity(esp32_mquickjs_get_active_runtime());
    }
}

static void wifi_connect_timeout_cb(void *arg)
{
    uint32_t generation = 0;
    bool should_timeout = false;

    (void)arg;

    wifi_lock();
    if (s_wifi_state.connect_future_registered && s_wifi_state.connect_in_progress) {
        generation = s_wifi_state.connect_generation;
        s_wifi_state.connect_in_progress = false;
        s_wifi_state.ignore_disconnect_once = true;
        s_wifi_state.status.connected = false;
        should_timeout = true;
    }
    wifi_unlock();

    if (!should_timeout) {
        return;
    }

    xEventGroupClearBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
    esp_wifi_disconnect();
    wifi_queue_connect_event(generation, ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT, 0);
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

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        wifi_lock();
        s_wifi_state.started = true;
        s_wifi_state.status.started = true;
        wifi_unlock();

        xEventGroupSetBits(s_wifi_state.event_group, WIFI_STARTED_BIT);
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = event_data;
        bool ignore_disconnect;
        bool should_queue_connect_failure = false;
        bool intentional_disconnect = false;
        uint32_t connect_generation = 0;
        int32_t reason = event != NULL ? (int32_t)event->reason : 0;

        wifi_lock();
        ignore_disconnect = s_wifi_state.ignore_disconnect_once;
        s_wifi_state.ignore_disconnect_once = false;
        intentional_disconnect = s_wifi_state.connect_future_registered &&
            s_wifi_state.connection_future_operation ==
                ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT;
        if (intentional_disconnect) {
            ignore_disconnect = false;
        }
        s_wifi_state.status.connected = false;
        if (!ignore_disconnect) {
            s_wifi_state.connect_in_progress = false;
            should_queue_connect_failure = s_wifi_state.connect_future_registered;
            connect_generation = s_wifi_state.connect_generation;
        }
        s_wifi_state.status.last_disconnect_reason = reason;
        wifi_unlock();

        xEventGroupClearBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT);
        if (!ignore_disconnect) {
            wifi_stop_connect_timeout_timer();
            if (!intentional_disconnect) {
                xEventGroupSetBits(s_wifi_state.event_group, WIFI_FAILED_BIT);
            }
            if (should_queue_connect_failure) {
                wifi_queue_connect_event(connect_generation,
                                         intentional_disconnect
                                             ? ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED
                                             : ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_FAILURE,
                                         reason);
            }
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        esp32_mquickjs_wifi_scan_event_t scan_event = {0};
        wifi_event_sta_scan_done_t *event = event_data;
        bool should_queue_future = false;
        esp32_mquickjs_future_token_t scan_token = {0};

        wifi_lock();
        wifi_set_scanning_locked(false);
        scan_event.generation = s_wifi_state.scan_generation;
        scan_event.status = event != NULL ? event->status : 1;
        should_queue_future = s_wifi_state.scan_future_registered;
        scan_token = s_wifi_state.scan_future_token;
        wifi_unlock();

        if (should_queue_future && s_wifi_state.scan_queue != NULL) {
            xQueueOverwrite(s_wifi_state.scan_queue, &scan_event);
            (void)esp32_mquickjs_future_wake(esp32_mquickjs_get_active_runtime(), scan_token);
            esp32_mquickjs_notify_activity(esp32_mquickjs_get_active_runtime());
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        bool should_queue_connect_success = false;
        uint32_t connect_generation = 0;

        wifi_lock();
        s_wifi_state.status.connected = true;
        s_wifi_state.connect_in_progress = false;
        should_queue_connect_success = s_wifi_state.connect_future_registered;
        connect_generation = s_wifi_state.connect_generation;
        s_wifi_state.status.last_disconnect_reason = 0;
        wifi_unlock();

        wifi_stop_connect_timeout_timer();
        xEventGroupClearBits(s_wifi_state.event_group, WIFI_FAILED_BIT);
        xEventGroupSetBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT);
        if (should_queue_connect_success) {
            wifi_queue_connect_event(connect_generation,
                                     ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS,
                                     0);
        }
    }
}

static esp_err_t wifi_init_once(void)
{
    esp32_mquickjs_wifi_radio_status_t radio_status;

    if (s_wifi_state.initialized) {
        return ESP_OK;
    }

    memset(&s_wifi_state, 0, sizeof(s_wifi_state));
    s_wifi_state.lock = xSemaphoreCreateMutex();
    s_wifi_state.event_group = xEventGroupCreate();
    s_wifi_state.scan_queue = xQueueCreate(WIFI_SCAN_EVENT_QUEUE_LEN,
                                           sizeof(esp32_mquickjs_wifi_scan_event_t));
    s_wifi_state.connect_queue = xQueueCreate(WIFI_CONNECT_EVENT_QUEUE_LEN,
                                              sizeof(esp32_mquickjs_wifi_connect_event_t));
    if (s_wifi_state.lock == NULL || s_wifi_state.event_group == NULL ||
        s_wifi_state.scan_queue == NULL || s_wifi_state.connect_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp32_mquickjs_net_ensure_initialized(), TAG,
                        "network runtime initialization failed");

    s_wifi_state.sta_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_state.sta_netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta() failed");
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(
        esp32_mquickjs_wifi_radio_acquire(
            ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,
            WIFI_MODE_STA, &s_wifi_state.radio_lease),
        TAG, "acquire Wi-Fi radio failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            WIFI_EVENT_STA_START,
                                                            wifi_event_handler,
                                                            NULL,
                                                            &s_wifi_state.wifi_start_event_instance),
                        TAG,
                        "register WIFI_EVENT start handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            WIFI_EVENT_STA_DISCONNECTED,
                                                            wifi_event_handler,
                                                            NULL,
                                                            &s_wifi_state.wifi_disconnect_event_instance),
                        TAG,
                        "register WIFI_EVENT disconnect handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            WIFI_EVENT_SCAN_DONE,
                                                            wifi_event_handler,
                                                            NULL,
                                                            &s_wifi_state.wifi_scan_event_instance),
                        TAG,
                        "register WIFI_EVENT scan handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            wifi_event_handler,
                                                            NULL,
                                                            &s_wifi_state.ip_event_instance),
                        TAG,
                        "register IP_EVENT handler failed");
    {
        esp_timer_create_args_t timer_args = {
            .callback = wifi_connect_timeout_cb,
            .name = "wifi_connect_timeout",
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_wifi_state.connect_timeout_timer),
                            TAG,
                            "create Wi-Fi connect timeout timer failed");
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
        radio_status.started) {
        wifi_lock();
        s_wifi_state.started = true;
        s_wifi_state.status.started = true;
        wifi_unlock();
        xEventGroupSetBits(s_wifi_state.event_group, WIFI_STARTED_BIT);
    }

    return ESP_OK;
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
        bits = xEventGroupWaitBits(s_wifi_state.event_group,
                                   bits_to_wait_for,
                                   clear_on_exit ? pdTRUE : pdFALSE,
                                   pdFALSE,
                                   wait_ticks);
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
    esp_err_t err;
    EventBits_t bits;
    bool interrupted = false;

    ESP_RETURN_ON_ERROR(wifi_init_once(), TAG, "wifi_init_once() failed");
    if (s_wifi_state.started) {
        return ESP_OK;
    }

    xEventGroupClearBits(s_wifi_state.event_group, WIFI_STARTED_BIT);
    err = esp32_mquickjs_wifi_radio_get_status(&radio_status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read Wi-Fi radio status failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    if (radio_status.started) {
        wifi_lock();
        s_wifi_state.started = true;
        s_wifi_state.status.started = true;
        wifi_unlock();
        xEventGroupSetBits(s_wifi_state.event_group, WIFI_STARTED_BIT);
        return ESP_OK;
    }
    err = esp32_mquickjs_wifi_radio_ensure_started(
        &s_wifi_state.radio_lease);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start() failed: %s", esp_err_to_name(err));
        return err;
    }

    bits = wifi_wait_for_bits(WIFI_STARTED_BIT,
                              false,
                              WIFI_START_TIMEOUT_MS,
                              &interrupted);
    if ((bits & WIFI_STARTED_BIT) == 0) {
        if (interrupted) {
            ESP_LOGW(TAG, "Interrupted while waiting for WIFI_EVENT_STA_START");
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGE(TAG, "Timed out waiting for WIFI_EVENT_STA_START");
        return ESP_ERR_TIMEOUT;
    }

    wifi_lock();
    s_wifi_state.started = true;
    s_wifi_state.status.started = true;
    wifi_unlock();
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

static JSValue wifi_make_status_object(JSContext *ctx)
{
    esp32_mquickjs_wifi_status_t status;
    esp32_mquickjs_wifi_radio_status_t radio_status;
    JSGCRef status_ref, radio_ref, clients_ref;
    JSValue *status_obj = JS_PushGCRef(ctx, &status_ref);
    JSValue *radio_obj = JS_PushGCRef(ctx, &radio_ref);
    JSValue *clients_obj = JS_PushGCRef(ctx, &clients_ref);
    uint32_t client_total = 0;

    *status_obj = JS_UNDEFINED;
    *radio_obj = JS_UNDEFINED;
    *clients_obj = JS_UNDEFINED;
    if (esp32_mquickjs_wifi_get_status(&status) != ESP_OK ||
        esp32_mquickjs_wifi_radio_get_status(&radio_status) != ESP_OK) {
        JS_ThrowInternalError(ctx, "failed to read Wi-Fi status");
        goto fail;
    }

    *status_obj = JS_NewObject(ctx);
    *radio_obj = JS_NewObject(ctx);
    *clients_obj = JS_NewObject(ctx);
    client_total =
        radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA] +
        radio_status.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW];
    if (JS_IsException(*status_obj) || JS_IsException(*radio_obj) ||
        JS_IsException(*clients_obj)) {
        goto fail;
    }

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
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "generation",
                                         JS_NewUint32(
                                             ctx, radio_status.generation)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "initialized",
                                         JS_NewBool(
                                             radio_status.initialized)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "starting",
                                         JS_NewBool(radio_status.starting)) ||
        !esp32_mquickjs_set_property_ref(ctx, radio_obj, "started",
                                         JS_NewBool(radio_status.started)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, radio_obj, "mode",
            JS_NewString(ctx, wifi_radio_mode_to_string(radio_status.mode))) ||
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
                                         *clients_obj) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "initialized",
                                     JS_NewBool(status.initialized)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "started",
                                     JS_NewBool(radio_status.started)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "connected",
                                     JS_NewBool(status.connected)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "scanning",
                                     JS_NewBool(status.scanning)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "ssid",
                                     JS_NewString(ctx, status.ssid)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "lastDisconnectReason",
                                     JS_NewInt32(ctx, status.last_disconnect_reason)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "lastDisconnectReasonName",
                                     JS_NewString(ctx, wifi_reason_to_string(status.last_disconnect_reason))) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "radio",
                                         *radio_obj)) {
        goto fail;
    }

    JS_PopGCRef(ctx, &clients_ref);
    JS_PopGCRef(ctx, &radio_ref);
    return JS_PopGCRef(ctx, &status_ref);

fail:
    JS_PopGCRef(ctx, &clients_ref);
    JS_PopGCRef(ctx, &radio_ref);
    JS_PopGCRef(ctx, &status_ref);
    return JS_EXCEPTION;
}

JSValue esp32_mquickjs_wifi_make_status_object(JSContext *ctx)
{
    return wifi_make_status_object(ctx);
}

static JSValue wifi_throw_connect_error(JSContext *ctx, esp_err_t err)
{
    esp32_mquickjs_wifi_status_t status;

    if (esp32_mquickjs_wifi_get_status(&status) != ESP_OK) {
        return JS_ThrowInternalError(ctx, "wifi.connect() failed: %s", esp_err_to_name(err));
    }

    if (err == ESP_ERR_TIMEOUT) {
        return JS_ThrowInternalError(ctx,
                                     "wifi.connect() timed out while connecting to %s",
                                     status.ssid[0] != '\0' ? status.ssid : "<unknown>");
    }

    return JS_ThrowInternalError(ctx,
                                 "wifi.connect() failed for %s (reason=%d:%s, err=%s)",
                                 status.ssid[0] != '\0' ? status.ssid : "<unknown>",
                                 (int)status.last_disconnect_reason,
                                 wifi_reason_to_string(status.last_disconnect_reason),
                                 esp_err_to_name(err));
}

JSValue esp32_mquickjs_wifi_throw_connect_error(JSContext *ctx, esp_err_t err)
{
    return wifi_throw_connect_error(ctx, err);
}

static JSValue wifi_throw_scan_error(JSContext *ctx, esp_err_t err)
{
    if (err == ESP_ERR_WIFI_STATE) {
        return JS_ThrowInternalError(ctx,
                                     "wifi.scan() cannot run while Wi-Fi is still connecting");
    }
    if (err == ESP_ERR_WIFI_TIMEOUT) {
        return JS_ThrowInternalError(ctx, "wifi.scan() timed out");
    }
    return JS_ThrowInternalError(ctx, "wifi.scan() failed: %s", esp_err_to_name(err));
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
    int timeout_ms = 0;

    if (JS_IsUndefined(value)) {
        *out_timeout_ms = default_timeout_ms;
        return 0;
    }
    if (JS_ToInt32(ctx, &timeout_ms, value) != 0 || timeout_ms < 0) {
        return -1;
    }

    *out_timeout_ms = (uint32_t)timeout_ms;
    return 0;
}

int esp32_mquickjs_wifi_value_to_timeout_ms(JSContext *ctx,
                                            JSValue value,
                                            uint32_t default_timeout_ms,
                                            uint32_t *out_timeout_ms)
{
    return js_value_to_timeout_ms(ctx, value, default_timeout_ms, out_timeout_ms);
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
    wifi_unlock();
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_start_connect(const wifi_config_t *config,
                                            uint32_t timeout_ms)
{
    wifi_config_t applied_config;
    esp_err_t err;
    bool needs_disconnect = false;
    size_t ssid_len;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    applied_config = *config;

    ESP_RETURN_ON_ERROR(esp32_mquickjs_wifi_ensure_started(), TAG, "wifi_ensure_started() failed");
    err = esp_wifi_set_config(WIFI_IF_STA, &applied_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config() failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_lock();
    needs_disconnect = s_wifi_state.status.connected || s_wifi_state.connect_in_progress;
    s_wifi_state.ignore_disconnect_once = needs_disconnect;
    wifi_unlock();

    if (needs_disconnect) {
        err = esp_wifi_disconnect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
            ESP_LOGE(TAG, "esp_wifi_disconnect() failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    xEventGroupClearBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);

    wifi_lock();
    s_wifi_state.connect_in_progress = true;
    s_wifi_state.status.connected = false;
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

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        wifi_lock();
        s_wifi_state.connect_in_progress = false;
        wifi_unlock();
        ESP_LOGE(TAG, "esp_wifi_connect() failed: %s", esp_err_to_name(err));
        return err;
    }
    if (timeout_ms == 0) {
        timeout_ms = 1;
    }
    err = esp_timer_start_once(s_wifi_state.connect_timeout_timer,
                               (uint64_t)timeout_ms * 1000ULL);
    if (err != ESP_OK) {
        wifi_lock();
        s_wifi_state.connect_in_progress = false;
        s_wifi_state.ignore_disconnect_once = true;
        wifi_unlock();
        (void)esp_wifi_disconnect();
        ESP_LOGE(TAG, "esp_timer_start_once(connect_timeout_timer) failed: %s",
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t esp32_mquickjs_wifi_start_disconnect(bool *out_pending)
{
    esp_err_t err;
    bool was_active;

    if (out_pending == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_pending = false;

    if (!s_wifi_state.initialized || !s_wifi_state.started) {
        return ESP_OK;
    }

    wifi_stop_connect_timeout_timer();
    wifi_lock();
    was_active = s_wifi_state.status.connected || s_wifi_state.connect_in_progress;
    s_wifi_state.connect_in_progress = false;
    s_wifi_state.ignore_disconnect_once =
        was_active && s_wifi_state.connection_future_operation !=
                          ESP32_MQUICKJS_WIFI_OPERATION_DISCONNECT;
    s_wifi_state.status.connected = false;
    wifi_unlock();

    xEventGroupClearBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);

    err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_CONNECT || err == ESP_ERR_WIFI_NOT_STARTED) {
        return ESP_OK;
    }
    if (err == ESP_OK) {
        *out_pending = was_active;
    }
    return err;
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
    if (!esp32_mquickjs_set_property_ref(ctx, entry, "ssid",
                                     JS_NewString(ctx, (const char *)record->ssid)) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "bssid",
                                     JS_NewString(ctx, bssid)) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "rssi",
                                     JS_NewInt32(ctx, record->rssi)) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "channel",
                                     JS_NewInt32(ctx, record->primary)) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "authMode",
                                     JS_NewString(ctx, wifi_authmode_to_string(record->authmode))) ||
        !esp32_mquickjs_set_property_ref(ctx, entry, "hidden",
                                     JS_NewBool(record->ssid[0] == '\0'))) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &entry_ref);

fail:
    JS_PopGCRef(ctx, &entry_ref);
    return JS_EXCEPTION;
}

static JSValue wifi_make_scan_results_array(JSContext *ctx)
{
    JSGCRef results_ref;
    JSValue *results;
    wifi_ap_record_t *records = NULL;
    uint16_t count = 0;
    uint16_t i;
    esp_err_t err;

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
    if (count > WIFI_SCAN_MAX_RESULTS) {
        count = WIFI_SCAN_MAX_RESULTS;
    }

    records = heap_caps_calloc(count, sizeof(*records), MALLOC_CAP_8BIT);
    if (records == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }

    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        JS_ThrowInternalError(ctx, "esp_wifi_scan_get_ap_records() failed: %s", esp_err_to_name(err));
        goto fail;
    }

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

    heap_caps_free(records);
    return JS_PopGCRef(ctx, &results_ref);

fail:
    heap_caps_free(records);
    JS_PopGCRef(ctx, &results_ref);
    return JS_EXCEPTION;
}

JSValue esp32_mquickjs_wifi_make_scan_results_array(JSContext *ctx)
{
    return wifi_make_scan_results_array(ctx);
}

bool esp32_mquickjs_init_wifi_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime)
{
    return esp32_mquickjs_init_wifi_future_runtime(ctx, runtime);
}

void esp32_mquickjs_deinit_wifi_runtime(JSContext *ctx)
{
    (void)ctx;
    if (!s_wifi_state.initialized) {
        return;
    }

    wifi_stop_connect_timeout_timer();
    if (s_wifi_state.scan_in_progress) {
        esp_wifi_scan_stop();
    }

    wifi_lock();
    s_wifi_state.scan_generation++;
    s_wifi_state.connect_generation++;
    s_wifi_state.scan_in_progress = false;
    s_wifi_state.connect_in_progress = false;
    s_wifi_state.scan_future_registered = false;
    s_wifi_state.connect_future_registered = false;
    s_wifi_state.connection_future_operation =
        ESP32_MQUICKJS_WIFI_OPERATION_NONE;
    memset(&s_wifi_state.scan_future_token, 0, sizeof(s_wifi_state.scan_future_token));
    memset(&s_wifi_state.connect_future_token, 0, sizeof(s_wifi_state.connect_future_token));
    wifi_unlock();

    if (s_wifi_state.scan_queue != NULL) {
        xQueueReset(s_wifi_state.scan_queue);
    }
    if (s_wifi_state.connect_queue != NULL) {
        xQueueReset(s_wifi_state.connect_queue);
    }
}

JSValue js_wifi_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS);
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
        err = esp_wifi_set_ps(requested);
    }
    if (err == ESP_OK) {
        err = esp_wifi_get_ps(&actual);
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
        err = esp_wifi_set_max_tx_power(requested);
    }
    if (err == ESP_OK) {
        err = esp_wifi_get_max_tx_power(&actual);
    }
    if (err != ESP_OK) {
        return JS_ThrowInternalError(
            ctx, "wifi.setTxPower() failed: %s", esp_err_to_name(err));
    }
    return JS_NewFloat64(ctx, (double)actual / 4.0);
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
