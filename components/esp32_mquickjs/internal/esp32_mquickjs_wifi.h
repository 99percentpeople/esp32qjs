#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI

#include <stdint.h>

#include "esp32_mquickjs_future.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

typedef struct {
    uint32_t generation;
    uint32_t status;
} esp32_mquickjs_wifi_scan_event_t;

typedef struct {
    uint32_t generation;
    uint32_t kind;
    int32_t reason;
} esp32_mquickjs_wifi_connect_event_t;

typedef struct {
    bool initialized;
    bool started;
    bool connected;
    bool connect_in_progress;
    bool ignore_disconnect_once;
    bool scan_in_progress;
    bool scan_future_registered;
    bool connect_future_registered;
    uint32_t connection_future_operation;
    EventGroupHandle_t event_group;
    QueueHandle_t scan_queue;
    QueueHandle_t connect_queue;
    SemaphoreHandle_t lock;
    esp_netif_t *sta_netif;
    esp_event_handler_instance_t wifi_start_event_instance;
    esp_event_handler_instance_t wifi_disconnect_event_instance;
    esp_event_handler_instance_t wifi_scan_event_instance;
    esp_event_handler_instance_t ip_event_instance;
    uint32_t scan_generation;
    uint32_t connect_generation;
    esp32_mquickjs_future_token_t scan_future_token;
    esp32_mquickjs_future_token_t connect_future_token;
    esp_timer_handle_t connect_timeout_timer;
    esp32_mquickjs_wifi_status_t status;
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
JSValue js_wifi_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

esp_err_t esp32_mquickjs_wifi_get_status(esp32_mquickjs_wifi_status_t *status);
esp_err_t esp32_mquickjs_wifi_ensure_started(void);
esp_err_t esp32_mquickjs_wifi_start_connect(const char *ssid,
                                            const char *password,
                                            uint32_t timeout_ms);
esp_err_t esp32_mquickjs_wifi_start_disconnect(bool *out_pending);

esp32_mquickjs_wifi_state_t *esp32_mquickjs_wifi_state(void);
void esp32_mquickjs_wifi_lock(void);
void esp32_mquickjs_wifi_unlock(void);
void esp32_mquickjs_wifi_set_scanning_locked(bool scanning);
void esp32_mquickjs_wifi_clear_scan_future(void);
void esp32_mquickjs_wifi_clear_connect_future(void);
const char *esp32_mquickjs_wifi_reason_to_string(int32_t reason);
JSValue esp32_mquickjs_wifi_make_status_object(JSContext *ctx);
JSValue esp32_mquickjs_wifi_make_scan_results_array(JSContext *ctx);
JSValue esp32_mquickjs_wifi_throw_connect_error(JSContext *ctx, esp_err_t err);
JSValue esp32_mquickjs_wifi_throw_scan_error(JSContext *ctx, esp_err_t err);
int esp32_mquickjs_wifi_value_to_timeout_ms(JSContext *ctx,
                                            JSValue value,
                                            uint32_t default_timeout_ms,
                                            uint32_t *out_timeout_ms);

#endif
