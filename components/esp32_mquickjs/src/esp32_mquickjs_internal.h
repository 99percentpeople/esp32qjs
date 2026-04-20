#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp32_mquickjs.h"

#define ESP32_MQUICKJS_BRIDGE_NAMESPACE "__esp32__"
#define ESP32_MQUICKJS_MAX_TIMERS CONFIG_ESP32_MQUICKJS_MAX_TIMERS
#define ESP32_MQUICKJS_TIMER_QUEUE_LEN CONFIG_ESP32_MQUICKJS_TIMER_QUEUE_LEN
#define ESP32_MQUICKJS_MAX_SCRIPT_PATH CONFIG_ESP32_MQUICKJS_MAX_SCRIPT_PATH
#define ESP32_MQUICKJS_USER_LED_PIN CONFIG_ESP32_MQUICKJS_USER_LED_PIN
#define ESP32_MQUICKJS_I2C_DEFAULT_SDA_PIN CONFIG_ESP32_MQUICKJS_I2C_DEFAULT_SDA_PIN
#define ESP32_MQUICKJS_I2C_DEFAULT_SCL_PIN CONFIG_ESP32_MQUICKJS_I2C_DEFAULT_SCL_PIN
#define ESP32_MQUICKJS_I2C_DEFAULT_FREQ_HZ ((uint32_t)CONFIG_ESP32_MQUICKJS_I2C_DEFAULT_FREQ_HZ)
#define ESP32_MQUICKJS_I2C_DEFAULT_TIMEOUT_MS ((uint32_t)CONFIG_ESP32_MQUICKJS_I2C_DEFAULT_TIMEOUT_MS)
#define ESP32_MQUICKJS_WIFI_SSID_MAX_LEN 32
#define ESP32_MQUICKJS_WIFI_PASSWORD_MAX_LEN 64
#define ESP32_MQUICKJS_WIFI_IPV4_STR_LEN 16
#define ESP32_MQUICKJS_WIFI_HOSTNAME_MAX_LEN 64
#define ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS ((uint32_t)CONFIG_ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS)
#define ESP32_MQUICKJS_HTTP_DEFAULT_TIMEOUT_MS ((uint32_t)CONFIG_ESP32_MQUICKJS_HTTP_DEFAULT_TIMEOUT_MS)
#define ESP32_MQUICKJS_HTTP_MAX_RESPONSE_BYTES ((uint32_t)CONFIG_ESP32_MQUICKJS_HTTP_MAX_RESPONSE_BYTES)
#define ESP32_MQUICKJS_HTTP_TASK_STACK_SIZE CONFIG_ESP32_MQUICKJS_HTTP_TASK_STACK_SIZE

#ifdef CONFIG_ESP32_MQUICKJS_USER_LED_ACTIVE_LOW
#define ESP32_MQUICKJS_USER_LED_ACTIVE_LOW 1
#else
#define ESP32_MQUICKJS_USER_LED_ACTIVE_LOW 0
#endif

#ifdef CONFIG_ESP32_MQUICKJS_I2C_ENABLE_INTERNAL_PULLUP
#define ESP32_MQUICKJS_I2C_ENABLE_INTERNAL_PULLUP 1
#else
#define ESP32_MQUICKJS_I2C_ENABLE_INTERNAL_PULLUP 0
#endif

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
bool esp32_mquickjs_install_i2c_module(JSContext *ctx, JSValue global_obj);
bool esp32_mquickjs_install_esp32_module(JSContext *ctx, JSValue global_obj);
bool esp32_mquickjs_install_wifi_module(JSContext *ctx,
                                        JSValue global_obj,
                                        esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_install_http_module(JSContext *ctx,
                                        JSValue global_obj,
                                        esp32_mquickjs_runtime_t *runtime);

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
bool esp32_mquickjs_dispatch_i2c(JSContext *ctx,
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
bool esp32_mquickjs_dispatch_http(JSContext *ctx,
                                  const char *operation,
                                  int argc,
                                  JSValue *argv,
                                  JSValue *result);

esp_err_t esp32_mquickjs_wifi_get_status(esp32_mquickjs_wifi_status_t *status);
