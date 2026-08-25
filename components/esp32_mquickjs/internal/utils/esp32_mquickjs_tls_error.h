#pragma once

#include "esp32_mquickjs_types.h"

#include "esp_err.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
#include "esp_tls.h"
#endif

typedef struct {
    esp_err_t operation_error;
    esp_err_t esp_tls_error;
    int mbedtls_error;
    uint32_t verify_flags;
    bool present;
} esp32_mquickjs_tls_error_t;

#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
esp_err_t esp32_mquickjs_tls_crt_bundle_attach(void *conf);
void esp32_mquickjs_tls_error_reset(esp32_mquickjs_tls_error_t *error);
void esp32_mquickjs_tls_error_set(esp32_mquickjs_tls_error_t *error,
                                  esp_err_t operation_error,
                                  esp_err_t esp_tls_error,
                                  int mbedtls_error,
                                  uint32_t verify_flags);
void esp32_mquickjs_tls_error_capture(esp32_mquickjs_tls_error_t *error,
                                      esp_tls_t *tls,
                                      esp_err_t operation_error);
void esp32_mquickjs_tls_error_merge_verify_flags(
    esp32_mquickjs_tls_error_t *error);
const char *esp32_mquickjs_tls_error_code(
    const esp32_mquickjs_tls_error_t *error);
JSValue esp32_mquickjs_throw_tls_error(
    JSContext *ctx, const char *operation,
    const esp32_mquickjs_tls_error_t *error);
#endif
