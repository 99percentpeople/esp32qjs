#include "esp32_mquickjs_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_SCAN_EVENT_QUEUE_LEN 1
#define WIFI_SCAN_BSSID_STR_LEN 18

static const char *TAG = "esp32qjs_wifi";

typedef struct {
    uint32_t generation;
    uint32_t status;
} esp32_mquickjs_wifi_scan_event_t;

typedef struct {
    bool initialized;
    bool started;
    bool connected;
    bool connect_in_progress;
    bool ignore_disconnect_once;
    bool scan_in_progress;
    bool scan_callback_registered;
    EventGroupHandle_t event_group;
    QueueHandle_t scan_queue;
    SemaphoreHandle_t lock;
    esp_netif_t *sta_netif;
    esp_event_handler_instance_t wifi_disconnect_event_instance;
    esp_event_handler_instance_t wifi_scan_event_instance;
    esp_event_handler_instance_t ip_event_instance;
    uint32_t scan_generation;
    JSGCRef scan_callback;
    esp32_mquickjs_wifi_status_t status;
} esp32_mquickjs_wifi_state_t;

static esp32_mquickjs_wifi_state_t s_wifi_state;

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

static void wifi_set_scanning_locked(bool scanning)
{
    s_wifi_state.scan_in_progress = scanning;
    s_wifi_state.status.scanning = scanning;
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = event_data;
        bool ignore_disconnect;

        wifi_lock();
        ignore_disconnect = s_wifi_state.ignore_disconnect_once;
        s_wifi_state.ignore_disconnect_once = false;
        s_wifi_state.status.connected = false;
        if (!ignore_disconnect) {
            s_wifi_state.connect_in_progress = false;
        }
        s_wifi_state.status.last_disconnect_reason = event != NULL ? (int32_t)event->reason : 0;
        wifi_clear_ip_info_locked();
        wifi_unlock();

        xEventGroupClearBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT);
        if (!ignore_disconnect) {
            xEventGroupSetBits(s_wifi_state.event_group, WIFI_FAILED_BIT);
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
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = event_data;

        wifi_lock();
        s_wifi_state.status.connected = true;
        s_wifi_state.connect_in_progress = false;
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

        xEventGroupClearBits(s_wifi_state.event_group, WIFI_FAILED_BIT);
        xEventGroupSetBits(s_wifi_state.event_group, WIFI_CONNECTED_BIT);
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
    if (s_wifi_state.lock == NULL || s_wifi_state.event_group == NULL || s_wifi_state.scan_queue == NULL) {
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

static esp_err_t wifi_ensure_started(void)
{
    esp_err_t err;

    ESP_RETURN_ON_ERROR(wifi_init_once(), TAG, "wifi_init_once() failed");
    if (s_wifi_state.started) {
        return ESP_OK;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start() failed: %s", esp_err_to_name(err));
        return err;
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
                                     JS_NewInt32(ctx, status.last_disconnect_reason))) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &status_ref);

fail:
    JS_PopGCRef(ctx, &status_ref);
    return JS_EXCEPTION;
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
                                 "wifi.connect() failed for %s (reason=%d, err=%s)",
                                 status.ssid[0] != '\0' ? status.ssid : "<unknown>",
                                 (int)status.last_disconnect_reason,
                                 esp_err_to_name(err));
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

    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(wifi_ensure_started(), TAG, "wifi_ensure_started() failed");
    err = wifi_apply_config(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config() failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_lock();
    s_wifi_state.ignore_disconnect_once = true;
    wifi_unlock();
    esp_wifi_disconnect();
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

static JSValue wifi_scan_sync(JSContext *ctx)
{
    esp_err_t err;

    err = wifi_ensure_started();
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

static JSValue wifi_scan_async(JSContext *ctx, JSValue callback)
{
    JSValue *callback_value;
    esp_err_t err;

    if (!JS_IsFunction(ctx, callback)) {
        return JS_ThrowTypeError(ctx, "wifi.scan(callback) expects a function");
    }

    err = wifi_ensure_started();
    if (err != ESP_OK) {
        return wifi_throw_scan_error(ctx, err);
    }

    wifi_lock();
    if (s_wifi_state.scan_in_progress || s_wifi_state.scan_callback_registered) {
        wifi_unlock();
        return JS_ThrowInternalError(ctx, "wifi.scan() is already in progress");
    }
    s_wifi_state.scan_generation++;
    wifi_set_scanning_locked(true);
    s_wifi_state.scan_callback_registered = true;
    wifi_unlock();

    if (s_wifi_state.scan_queue != NULL) {
        xQueueReset(s_wifi_state.scan_queue);
    }

    callback_value = JS_AddGCRef(ctx, &s_wifi_state.scan_callback);
    *callback_value = callback;

    err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        wifi_lock();
        wifi_set_scanning_locked(false);
        wifi_unlock();
        wifi_clear_scan_callback(ctx);
        return wifi_throw_scan_error(ctx, err);
    }

    return JS_UNDEFINED;
}

bool esp32_mquickjs_install_wifi_module(JSContext *ctx, JSValue global_obj)
{
    JSGCRef module_ref;
    JSValue *module_obj;

    module_obj = JS_PushGCRef(ctx, &module_ref);
    *module_obj = JS_NewObject(ctx);
    if (JS_IsException(*module_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *module_obj, "DEFAULT_TIMEOUT_MS",
                                     JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS)) ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "connect", "wifi.connect") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "disconnect", "wifi.disconnect") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "status", "wifi.status") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "scan", "wifi.scan")) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, global_obj, "wifi", JS_PopGCRef(ctx, &module_ref))) {
        return false;
    }
    return true;

fail:
    JS_PopGCRef(ctx, &module_ref);
    return false;
}

bool esp32_mquickjs_dispatch_wifi(JSContext *ctx,
                                  const char *operation,
                                  int argc,
                                  JSValue *argv,
                                  JSValue *result)
{
    if (strcmp(operation, "status") == 0) {
        *result = wifi_make_status_object(ctx);
        return true;
    }

    if (strcmp(operation, "scan") == 0) {
        if (argc == 0 || JS_IsUndefined(argv[0])) {
            *result = wifi_scan_sync(ctx);
            return true;
        }
        if (argc == 1 && JS_IsFunction(ctx, argv[0])) {
            *result = wifi_scan_async(ctx, argv[0]);
            return true;
        }

        *result = JS_ThrowTypeError(ctx,
                                    "wifi.scan(callback?) expects no arguments or a single callback function");
        return true;
    }

    if (strcmp(operation, "connect") == 0) {
        JSCStringBuf ssid_buf;
        JSCStringBuf password_buf;
        const char *ssid;
        const char *password;
        uint32_t timeout_ms = ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS;
        esp_err_t err;

        if (argc < 2 || !JS_IsString(ctx, argv[0]) || !JS_IsString(ctx, argv[1])) {
            *result = JS_ThrowTypeError(ctx,
                                        "wifi.connect(ssid, password, timeoutMs?) expects two strings and an optional timeout");
            return true;
        }
        if (argc >= 3 &&
            js_value_to_timeout_ms(ctx, argv[2], ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS, &timeout_ms) != 0) {
            *result = JS_ThrowTypeError(ctx, "wifi.connect(..., timeoutMs) expects a non-negative integer");
            return true;
        }

        ssid = JS_ToCString(ctx, argv[0], &ssid_buf);
        password = JS_ToCString(ctx, argv[1], &password_buf);
        err = wifi_connect(ssid, password, timeout_ms);
        if (err != ESP_OK) {
            *result = wifi_throw_connect_error(ctx, err);
            return true;
        }

        *result = wifi_make_status_object(ctx);
        return true;
    }

    if (strcmp(operation, "disconnect") == 0) {
        esp_err_t err = wifi_disconnect();

        if (err != ESP_OK) {
            *result = JS_ThrowInternalError(ctx, "wifi.disconnect() failed: %s", esp_err_to_name(err));
            return true;
        }

        *result = wifi_make_status_object(ctx);
        return true;
    }

    return false;
}

bool esp32_mquickjs_poll_wifi(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_wifi_scan_event_t event;
    bool needs_redraw = false;

    (void)runtime;
    if (ctx == NULL || s_wifi_state.scan_queue == NULL) {
        return false;
    }

    while (xQueueReceive(s_wifi_state.scan_queue, &event, 0) == pdTRUE) {
        JSGCRef callback_ref;
        JSValue *callback_fn;
        JSValue callback_ret;
        JSValue results;
        bool callback_matches;

        wifi_lock();
        callback_matches = s_wifi_state.scan_callback_registered &&
                           event.generation == s_wifi_state.scan_generation;
        wifi_unlock();
        if (!callback_matches) {
            continue;
        }

        if (JS_StackCheck(ctx, 3)) {
            wifi_lock();
            s_wifi_state.scan_callback_registered = false;
            wifi_unlock();
            JS_DeleteGCRef(ctx, &s_wifi_state.scan_callback);
            ESP_LOGW(TAG, "Skipping Wi-Fi scan callback due to JS stack pressure");
            needs_redraw = true;
            continue;
        }

        callback_fn = JS_PushGCRef(ctx, &callback_ref);
        *callback_fn = s_wifi_state.scan_callback.val;

        wifi_lock();
        s_wifi_state.scan_callback_registered = false;
        wifi_unlock();
        JS_DeleteGCRef(ctx, &s_wifi_state.scan_callback);

        if (event.status != 0) {
            ESP_LOGW(TAG, "Wi-Fi scan completed with failure status=%" PRIu32, event.status);
            needs_redraw = true;
            results = JS_NewArray(ctx, 0);
        } else {
            results = wifi_make_scan_results_array(ctx);
        }
        if (JS_IsException(results)) {
            esp32_mquickjs_print_exception(ctx);
            JS_PopGCRef(ctx, &callback_ref);
            continue;
        }

        JS_PushArg(ctx, results);
        JS_PushArg(ctx, *callback_fn);
        JS_PushArg(ctx, JS_NULL);
        callback_ret = JS_Call(ctx, 1);
        if (JS_IsException(callback_ret)) {
            esp32_mquickjs_print_exception(ctx);
        }

        JS_PopGCRef(ctx, &callback_ref);
    }

    return needs_redraw;
}
