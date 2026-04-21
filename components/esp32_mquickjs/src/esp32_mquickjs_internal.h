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
#define ESP32_MQUICKJS_BOARD_NAME CONFIG_ESP32_MQUICKJS_BOARD_NAME
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
#define ESP32_MQUICKJS_HTTP_TASK_STACK_SIZE CONFIG_ESP32_MQUICKJS_HTTP_TASK_STACK_SIZE
#define ESP32_MQUICKJS_FS_STREAM_ID_KEY "__esp32qjsStreamId"
#define ESP32_MQUICKJS_FS_STREAM_GENERATION_KEY "__esp32qjsStreamGeneration"
#define ESP32_MQUICKJS_HEADERS_STORE_KEY "__esp32qjsHeadersStore"
#define JS_CLASS_HEADERS (JS_CLASS_USER + 0)
#define JS_CLASS_REQUEST (JS_CLASS_USER + 1)
#define JS_CLASS_RESPONSE (JS_CLASS_USER + 2)
#define JS_CLASS_COUNT (JS_CLASS_USER + 3)

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

typedef struct {
    int32_t stream_id;
    uint32_t generation;
} esp32_mquickjs_fs_stream_ref_t;

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
bool esp32_mquickjs_install_stream_module(JSContext *ctx, JSValue global_obj);
bool esp32_mquickjs_install_gpio_module(JSContext *ctx, JSValue global_obj);
bool esp32_mquickjs_install_i2c_module(JSContext *ctx, JSValue global_obj);
bool esp32_mquickjs_install_esp32_module(JSContext *ctx, JSValue global_obj);
bool esp32_mquickjs_install_wifi_module(JSContext *ctx,
                                        JSValue global_obj,
                                        esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_install_http_module(JSContext *ctx,
                                        JSValue global_obj,
                                        esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_install_http_server_module(JSContext *ctx,
                                               JSValue global_obj,
                                               esp32_mquickjs_runtime_t *runtime);

bool esp32_mquickjs_dispatch_fs(JSContext *ctx,
                                const char *operation,
                                int argc,
                                JSValue *argv,
                                JSValue *result);
bool esp32_mquickjs_dispatch_stream(JSContext *ctx,
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
bool esp32_mquickjs_dispatch_http_server(JSContext *ctx,
                                         const char *operation,
                                         int argc,
                                         JSValue *argv,
                                         JSValue *result);

bool esp32_mquickjs_fs_parse_stream_ref(JSContext *ctx,
                                        JSValue stream_value,
                                        esp32_mquickjs_fs_stream_ref_t *out_ref);
bool esp32_mquickjs_stream_is_stream(JSContext *ctx, JSValue stream_value);
JSValue esp32_mquickjs_stream_open_file(JSContext *ctx,
                                        JSValue global_obj,
                                        const char *path,
                                        const char *mode);
JSValue esp32_mquickjs_stream_open_memory_owned(JSContext *ctx,
                                                JSValue global_obj,
                                                char *data,
                                                size_t data_len);
JSValue esp32_mquickjs_stream_clone(JSContext *ctx,
                                    JSValue global_obj,
                                    JSValue stream_value);
int esp32_mquickjs_stream_read_all_text(JSContext *ctx,
                                        JSValue stream_value,
                                        const char *api_name,
                                        char **out_text,
                                        size_t *out_len);
esp_err_t esp32_mquickjs_stream_close_value(JSContext *ctx, JSValue stream_value);
esp_err_t esp32_mquickjs_fs_stream_read(const esp32_mquickjs_fs_stream_ref_t *ref,
                                        void *buf,
                                        size_t buf_len,
                                        size_t *out_len);
esp_err_t esp32_mquickjs_fs_stream_close(const esp32_mquickjs_fs_stream_ref_t *ref);

bool esp32_mquickjs_is_headers_object(JSContext *ctx, JSValue value);
bool esp32_mquickjs_is_request_object(JSContext *ctx, JSValue value);
bool esp32_mquickjs_is_response_object(JSContext *ctx, JSValue value);
JSValue esp32_mquickjs_make_headers_object(JSContext *ctx,
                                           JSValue global_obj,
                                           JSValue init_value);
JSValue esp32_mquickjs_headers_to_plain_object(JSContext *ctx, JSValue headers_value);
JSValue esp32_mquickjs_make_request_object(JSContext *ctx,
                                           JSValue global_obj,
                                           const char *method,
                                           const char *url,
                                           const char *path,
                                           const char *route,
                                           const char *query_string,
                                           JSValue query_value,
                                           JSValue headers_init,
                                           JSValue body_stream);
JSValue esp32_mquickjs_make_response_object(JSContext *ctx,
                                            JSValue global_obj,
                                            int32_t status,
                                            const char *status_text,
                                            const char *url,
                                            JSValue headers_init,
                                            JSValue body_stream);
JSValue esp32_mquickjs_make_text_body_stream(JSContext *ctx,
                                             JSValue global_obj,
                                             const char *text);

esp_err_t esp32_mquickjs_wifi_get_status(esp32_mquickjs_wifi_status_t *status);
