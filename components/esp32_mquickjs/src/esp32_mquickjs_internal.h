#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp32_mquickjs.h"

#define ESP32_MQUICKJS_BRIDGE_NAMESPACE "__esp32__"
#define ESP32_MQUICKJS_MAX_TIMERS 16
#define ESP32_MQUICKJS_TIMER_QUEUE_LEN 16
#define ESP32_MQUICKJS_MAX_SCRIPT_PATH 256
#define ESP32_MQUICKJS_USER_LED_PIN 21
#define ESP32_MQUICKJS_USER_LED_ACTIVE_LOW 1
#define ESP32_MQUICKJS_WIFI_SSID_MAX_LEN 32
#define ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN 64
#define ESP32_MQUICKJS_WIFI_IPV4_STR_LEN 16
#define ESP32_MQUICKJS_WIFI_HOSTNAME_MAX_LEN 64
#define ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS 15000U

typedef struct {
    bool initialized;
    bool started;
    bool connected;
    bool scanning;
    char ssid[ESP32_MQUICKJS_WIFI_SSID_MAX_LEN + 1];
    char hostname[ESP32_MQUICKJS_WIFI_HOSTNAME_MAX_LEN + 1];
    char ip[ESP32_MQUICKJS_WIFI_IPV4_STR_LEN];
    char netmask[ESP32_MQUICKJS_WIFI_IPV4_STR_LEN];
    char gateway[ESP32_MQUICKJS_WIFI_IPV4_STR_LEN];
    int32_t last_disconnect_reason;
} esp32_mquickjs_wifi_status_t;

bool esp32_mquickjs_set_property(JSContext *ctx,
                                 JSValue target_obj,
                                 const char *name,
                                 JSValue value);

bool esp32_mquickjs_set_alias(JSContext *ctx,
                              JSValue target_obj,
                              JSValue source_obj,
                              const char *target_name,
                              const char *source_name);

bool esp32_mquickjs_set_bound_bridge_function(JSContext *ctx,
                                              JSValue target_obj,
                                              JSValue global_obj,
                                              const char *target_name,
                                              const char *operation);

esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void);

JSValue esp32_mquickjs_load_from_littlefs(JSContext *ctx,
                                          esp32_mquickjs_runtime_t *runtime,
                                          const char *script_path);

bool esp32_mquickjs_mount_littlefs(bool format_if_mount_failed);

bool esp32_mquickjs_install_fs_module(JSContext *ctx, JSValue global_obj);
bool esp32_mquickjs_install_gpio_module(JSContext *ctx, JSValue global_obj);
bool esp32_mquickjs_install_esp32_module(JSContext *ctx, JSValue global_obj);
bool esp32_mquickjs_install_wifi_module(JSContext *ctx, JSValue global_obj);

bool esp32_mquickjs_dispatch_fs(JSContext *ctx,
                                const char *operation,
                                int argc,
                                JSValue *argv,
                                JSValue *result);

bool esp32_mquickjs_dispatch_gpio(JSContext *ctx,
                                  const char *operation,
                                  int argc,
                                  JSValue *argv,
                                  JSValue *result);

bool esp32_mquickjs_dispatch_esp32(JSContext *ctx,
                                   const char *operation,
                                   int argc,
                                   JSValue *argv,
                                   JSValue *result);

bool esp32_mquickjs_dispatch_wifi(JSContext *ctx,
                                  const char *operation,
                                  int argc,
                                  JSValue *argv,
                                  JSValue *result);

esp_err_t esp32_mquickjs_wifi_get_status(esp32_mquickjs_wifi_status_t *status);
bool esp32_mquickjs_poll_wifi(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime);
