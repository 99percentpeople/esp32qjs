#include "esp32_mquickjs_wifi.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#include "esp32_mquickjs_core.h"

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
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_STARTED_BIT BIT2
#define WIFI_SCAN_EVENT_QUEUE_LEN 1
#define WIFI_CONNECT_EVENT_QUEUE_LEN 1
#define WIFI_SCAN_BSSID_STR_LEN 18
#define WIFI_START_TIMEOUT_MS 5000

static const char *TAG = "esp32qjs_wifi";

static esp32_mquickjs_wifi_state_t s_wifi_state;

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

static void wifi_clear_ip_info_locked(void)
{
    s_wifi_state.status.ip[0] = '\0';
    s_wifi_state.status.netmask[0] = '\0';
    s_wifi_state.status.gateway[0] = '\0';
}

static void wifi_copy_ipv4(char *dst, size_t dst_size, esp_ip4_addr_t addr)
{
    snprintf(dst, dst_size, IPSTR, IP2STR(&addr));
}

static void wifi_refresh_hostname_locked(void)
{
    const char *hostname = NULL;

    s_wifi_state.status.hostname[0] = '\0';
    if (s_wifi_state.sta_netif == NULL) {
        return;
    }
    if (esp_netif_get_hostname(s_wifi_state.sta_netif, &hostname) != ESP_OK || hostname == NULL) {
        return;
    }

    strncpy(s_wifi_state.status.hostname, hostname, sizeof(s_wifi_state.status.hostname) - 1);
    s_wifi_state.status.hostname[sizeof(s_wifi_state.status.hostname) - 1] = '\0';
}

static void wifi_clear_scan_callback(JSContext *ctx)
{
    if (ctx == NULL || !s_wifi_state.scan_callback_registered) {
        return;
    }

    JS_DeleteGCRef(ctx, &s_wifi_state.scan_callback);
    s_wifi_state.scan_callback_registered = false;
}

void esp32_mquickjs_wifi_clear_scan_callback(JSContext *ctx)
{
    wifi_clear_scan_callback(ctx);
}

static void wifi_clear_connect_callback(JSContext *ctx)
{
    if (ctx == NULL || !s_wifi_state.connect_callback_registered) {
        return;
    }

    JS_DeleteGCRef(ctx, &s_wifi_state.connect_callback);
    s_wifi_state.connect_callback_registered = false;
}

void esp32_mquickjs_wifi_clear_connect_callback(JSContext *ctx)
{
    wifi_clear_connect_callback(ctx);
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
    esp32_mquickjs_wifi_connect_event_t event = {
        .generation = generation,
        .kind = kind,
        .reason = reason,
    };

    if (s_wifi_state.connect_queue != NULL) {
        xQueueOverwrite(s_wifi_state.connect_queue, &event);
        esp32_mquickjs_notify_activity(esp32_mquickjs_get_active_runtime());
    }
}

static void wifi_connect_timeout_cb(void *arg)
{
    uint32_t generation = 0;
    bool should_timeout = false;

    (void)arg;

    wifi_lock();
    if (s_wifi_state.connect_callback_registered && s_wifi_state.connect_in_progress) {
        generation = s_wifi_state.connect_generation;
        s_wifi_state.connect_in_progress = false;
        s_wifi_state.ignore_disconnect_once = true;
        s_wifi_state.status.connected = false;
        wifi_clear_ip_info_locked();
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
        uint32_t connect_generation = 0;
        int32_t reason = event != NULL ? (int32_t)event->reason : 0;

        wifi_lock();
        ignore_disconnect = s_wifi_state.ignore_disconnect_once;
        s_wifi_state.ignore_disconnect_once = false;
        s_wifi_state.status.connected = false;
        if (!ignore_disconnect) {
            s_wifi_state.connect_in_progress = false;
            should_queue_connect_failure = s_wifi_state.connect_callback_registered;
            connect_generation = s_wifi_state.connect_generation;
        }
        s_wifi_state.status.last_disconnect_reason = reason;
        wifi_clear_ip_info_locked();
        wifi_unlock();

        wifi_stop_connect_timeout_timer();
        xEventGroupClearBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT);
        if (!ignore_disconnect) {
            xEventGroupSetBits(s_wifi_state.event_group, WIFI_FAILED_BIT);
            if (should_queue_connect_failure) {
                wifi_queue_connect_event(connect_generation,
                                         ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_FAILURE,
                                         reason);
            }
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        esp32_mquickjs_wifi_scan_event_t scan_event = {0};
        wifi_event_sta_scan_done_t *event = event_data;
        bool should_queue_callback = false;

        wifi_lock();
        wifi_set_scanning_locked(false);
        scan_event.generation = s_wifi_state.scan_generation;
        scan_event.status = event != NULL ? event->status : 1;
        should_queue_callback = s_wifi_state.scan_callback_registered;
        wifi_unlock();

        if (should_queue_callback && s_wifi_state.scan_queue != NULL) {
            xQueueOverwrite(s_wifi_state.scan_queue, &scan_event);
            esp32_mquickjs_notify_activity(esp32_mquickjs_get_active_runtime());
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = event_data;
        bool should_queue_connect_success = false;
        uint32_t connect_generation = 0;

        wifi_lock();
        s_wifi_state.status.connected = true;
        s_wifi_state.connect_in_progress = false;
        should_queue_connect_success = s_wifi_state.connect_callback_registered;
        connect_generation = s_wifi_state.connect_generation;
        s_wifi_state.status.last_disconnect_reason = 0;
        if (event != NULL) {
            wifi_copy_ipv4(s_wifi_state.status.ip,
                           sizeof(s_wifi_state.status.ip),
                           event->ip_info.ip);
            wifi_copy_ipv4(s_wifi_state.status.netmask,
                           sizeof(s_wifi_state.status.netmask),
                           event->ip_info.netmask);
            wifi_copy_ipv4(s_wifi_state.status.gateway,
                           sizeof(s_wifi_state.status.gateway),
                           event->ip_info.gw);
        } else {
            wifi_clear_ip_info_locked();
        }
        wifi_refresh_hostname_locked();
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

static esp_err_t wifi_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs_flash_erase() failed");
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t wifi_init_once(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err;

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

    ESP_RETURN_ON_ERROR(wifi_init_nvs(), TAG, "nvs_flash_init() failed");

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init() failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default() failed: %s", esp_err_to_name(err));
        return err;
    }

    s_wifi_state.sta_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_state.sta_netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta() failed");
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init() failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "esp_wifi_set_storage() failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode() failed");

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
    wifi_refresh_hostname_locked();
    wifi_unlock();

    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_ensure_started(void)
{
    esp_err_t err;
    EventBits_t bits;

    ESP_RETURN_ON_ERROR(wifi_init_once(), TAG, "wifi_init_once() failed");
    if (s_wifi_state.started) {
        return ESP_OK;
    }

    xEventGroupClearBits(s_wifi_state.event_group, WIFI_STARTED_BIT);
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start() failed: %s", esp_err_to_name(err));
        return err;
    }

    bits = xEventGroupWaitBits(s_wifi_state.event_group,
                               WIFI_STARTED_BIT,
                               pdFALSE,
                               pdFALSE,
                               pdMS_TO_TICKS(WIFI_START_TIMEOUT_MS));
    if ((bits & WIFI_STARTED_BIT) == 0) {
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

static JSValue wifi_make_status_object(JSContext *ctx)
{
    esp32_mquickjs_wifi_status_t status;
    JSGCRef status_ref;
    JSValue *status_obj;

    if (esp32_mquickjs_wifi_get_status(&status) != ESP_OK) {
        return JS_ThrowInternalError(ctx, "failed to read Wi-Fi status");
    }

    status_obj = JS_PushGCRef(ctx, &status_ref);
    *status_obj = JS_NewObject(ctx);
    if (JS_IsException(*status_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *status_obj, "initialized",
                                     JS_NewBool(status.initialized)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "started",
                                     JS_NewBool(status.started)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "connected",
                                     JS_NewBool(status.connected)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "scanning",
                                     JS_NewBool(status.scanning)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "ssid",
                                     JS_NewString(ctx, status.ssid)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "hostname",
                                     JS_NewString(ctx, status.hostname)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "ip",
                                     JS_NewString(ctx, status.ip)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "netmask",
                                     JS_NewString(ctx, status.netmask)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "gateway",
                                     JS_NewString(ctx, status.gateway)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "lastDisconnectReason",
                                     JS_NewInt32(ctx, status.last_disconnect_reason)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "lastDisconnectReasonName",
                                     JS_NewString(ctx, wifi_reason_to_string(status.last_disconnect_reason)))) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &status_ref);

fail:
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

static esp_err_t wifi_wait_for_connection(uint32_t timeout_ms)
{
    EventBits_t bits;
    TickType_t wait_ticks = timeout_ms == 0 ? 0 : pdMS_TO_TICKS(timeout_ms);

    bits = xEventGroupWaitBits(s_wifi_state.event_group,
                               WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                               pdTRUE,
                               pdFALSE,
                               wait_ticks);

    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }
    if ((bits & WIFI_FAILED_BIT) != 0) {
        return ESP_FAIL;
    }

    wifi_lock();
    s_wifi_state.connect_in_progress = false;
    s_wifi_state.ignore_disconnect_once = true;
    wifi_unlock();
    esp_wifi_disconnect();
    return ESP_ERR_TIMEOUT;
}

static esp_err_t wifi_apply_config(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = {0};
    size_t ssid_len = strlen(ssid);
    size_t password_len = password != NULL ? strlen(password) : 0;

    if (ssid_len == 0 || ssid_len > ESP32_MQUICKJS_WIFI_SSID_MAX_LEN ||
        password_len > ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(wifi_config.sta.ssid, ssid, ssid_len);
    if (password_len > 0) {
        memcpy(wifi_config.sta.password, password, password_len);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

static esp_err_t wifi_connect(const char *ssid, const char *password, uint32_t timeout_ms)
{
    esp_err_t err;
    bool needs_disconnect = false;

    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(esp32_mquickjs_wifi_ensure_started(), TAG, "wifi_ensure_started() failed");
    err = wifi_apply_config(ssid, password);
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
    strncpy(s_wifi_state.status.ssid, ssid, sizeof(s_wifi_state.status.ssid) - 1);
    s_wifi_state.status.ssid[sizeof(s_wifi_state.status.ssid) - 1] = '\0';
    wifi_clear_ip_info_locked();
    wifi_unlock();

    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "esp_wifi_connect() failed");
    return wifi_wait_for_connection(timeout_ms);
}

esp_err_t esp32_mquickjs_wifi_connect_async(const char *ssid,
                                            const char *password,
                                            uint32_t timeout_ms)
{
    esp_err_t err;
    bool needs_disconnect = false;

    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(esp32_mquickjs_wifi_ensure_started(), TAG, "wifi_ensure_started() failed");
    err = wifi_apply_config(ssid, password);
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
    strncpy(s_wifi_state.status.ssid, ssid, sizeof(s_wifi_state.status.ssid) - 1);
    s_wifi_state.status.ssid[sizeof(s_wifi_state.status.ssid) - 1] = '\0';
    wifi_clear_ip_info_locked();
    wifi_unlock();

    wifi_stop_connect_timeout_timer();
    if (s_wifi_state.connect_queue != NULL) {
        xQueueReset(s_wifi_state.connect_queue);
    }

    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "esp_wifi_connect() failed");
    if (timeout_ms == 0) {
        timeout_ms = 1;
    }
    return esp_timer_start_once(s_wifi_state.connect_timeout_timer, (uint64_t)timeout_ms * 1000ULL);
}

static esp_err_t wifi_disconnect(void)
{
    esp_err_t err;

    if (!s_wifi_state.initialized || !s_wifi_state.started) {
        return ESP_OK;
    }

    wifi_lock();
    s_wifi_state.connect_in_progress = false;
    s_wifi_state.ignore_disconnect_once = true;
    s_wifi_state.status.connected = false;
    wifi_clear_ip_info_locked();
    wifi_unlock();

    xEventGroupClearBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);

    err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_CONNECT || err == ESP_ERR_WIFI_NOT_STARTED) {
        return ESP_OK;
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
    if (!esp32_mquickjs_set_property(ctx, *entry, "ssid",
                                     JS_NewString(ctx, (const char *)record->ssid)) ||
        !esp32_mquickjs_set_property(ctx, *entry, "bssid",
                                     JS_NewString(ctx, bssid)) ||
        !esp32_mquickjs_set_property(ctx, *entry, "rssi",
                                     JS_NewInt32(ctx, record->rssi)) ||
        !esp32_mquickjs_set_property(ctx, *entry, "channel",
                                     JS_NewInt32(ctx, record->primary)) ||
        !esp32_mquickjs_set_property(ctx, *entry, "authMode",
                                     JS_NewString(ctx, wifi_authmode_to_string(record->authmode))) ||
        !esp32_mquickjs_set_property(ctx, *entry, "hidden",
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
        JSValue entry = wifi_make_scan_entry_object(ctx, &records[i]);

        if (JS_IsException(entry) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *results, i, entry))) {
            goto fail;
        }
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

static JSValue wifi_scan_sync(JSContext *ctx)
{
    esp_err_t err;

    err = esp32_mquickjs_wifi_ensure_started();
    if (err != ESP_OK) {
        return wifi_throw_scan_error(ctx, err);
    }

    wifi_lock();
    if (s_wifi_state.scan_in_progress || s_wifi_state.scan_callback_registered) {
        wifi_unlock();
        return JS_ThrowInternalError(ctx, "wifi.scan() is already in progress");
    }
    wifi_set_scanning_locked(true);
    wifi_unlock();

    err = esp_wifi_scan_start(NULL, true);

    wifi_lock();
    wifi_set_scanning_locked(false);
    wifi_unlock();

    if (err != ESP_OK) {
        return wifi_throw_scan_error(ctx, err);
    }

    return wifi_make_scan_results_array(ctx);
}

bool esp32_mquickjs_init_wifi_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime)
{
    return esp32_mquickjs_init_wifi_async_runtime(ctx, runtime);
}

void esp32_mquickjs_deinit_wifi_runtime(JSContext *ctx)
{
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
    wifi_unlock();

    wifi_clear_scan_callback(ctx);
    wifi_clear_connect_callback(ctx);
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

JSValue js_wifi_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return wifi_make_status_object(ctx);
}

JSValue js_wifi_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;

    if (argc == 0) {
        return wifi_scan_sync(ctx);
    }
    if (argc == 1 && JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx,
                                 "wifi.scan() no longer accepts a callback; use wifi.async.scan(callback)");
    }

    return JS_ThrowTypeError(ctx, "wifi.scan() expects no arguments");
}

JSValue js_wifi_connect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSCStringBuf ssid_buf;
    JSCStringBuf password_buf;
    const char *ssid;
    const char *password;
    uint32_t timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
    esp_err_t err;

    (void)this_val;
    if ((argc >= 3 && JS_IsFunction(ctx, argv[2])) || (argc >= 4 && JS_IsFunction(ctx, argv[3]))) {
        return JS_ThrowTypeError(ctx,
                                 "wifi.connect(...) no longer accepts a callback; use wifi.async.connect(...)");
    }
    if (argc < 2 || argc > 3 || !JS_IsString(ctx, argv[0]) || !JS_IsString(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx,
                                 "wifi.connect(ssid, password, timeoutMs?) expects two strings and an optional timeout");
    }
    if (argc >= 3) {
        if (js_value_to_timeout_ms(ctx,
                                   argv[2],
                                   ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS,
                                   &timeout_ms) != 0) {
            return JS_ThrowTypeError(ctx, "wifi.connect(..., timeoutMs) expects a non-negative integer");
        }
    }
    ssid = JS_ToCString(ctx, argv[0], &ssid_buf);
    password = JS_ToCString(ctx, argv[1], &password_buf);
    err = wifi_connect(ssid, password, timeout_ms);
    if (err != ESP_OK) {
        return wifi_throw_connect_error(ctx, err);
    }

    return wifi_make_status_object(ctx);
}

JSValue js_wifi_disconnect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp_err_t err;

    (void)this_val;
    (void)argc;
    (void)argv;
    err = wifi_disconnect();
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx, "wifi.disconnect() failed: %s", esp_err_to_name(err));
    }
    return wifi_make_status_object(ctx);
}

#endif
