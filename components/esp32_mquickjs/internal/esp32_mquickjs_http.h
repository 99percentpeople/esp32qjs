#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP

#define ESP32_MQUICKJS_HTTP_ERROR_TEXT_LEN 160

typedef struct {
    char *key;
    char *value;
} esp32_mquickjs_http_header_t;

typedef struct {
    char *url;
    char *method;
    char *body;
    uint32_t timeout_ms;
    size_t max_body_bytes;
    esp32_mquickjs_http_header_t *headers;
    size_t header_count;
} esp32_mquickjs_http_request_t;

typedef struct esp32_mquickjs_http_operation esp32_mquickjs_http_operation_t;

typedef struct {
    bool ok;
    int32_t status;
    char *url;
    char *status_text;
    char *body;
    esp32_mquickjs_http_header_t *headers;
    size_t header_count;
} esp32_mquickjs_http_response_t;

bool esp32_mquickjs_init_http_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_init_http_async_runtime(JSContext *ctx,
                                            esp32_mquickjs_runtime_t *runtime);
bool esp32_mquickjs_deinit_http_runtime(JSContext *ctx);

JSValue js_http_fetch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_async_fetch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_async_cancel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_http_get_max_body_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

char *esp32_mquickjs_http_strdup(const char *value);
void esp32_mquickjs_http_free_headers(esp32_mquickjs_http_header_t *headers, size_t header_count);
void esp32_mquickjs_http_free_request(esp32_mquickjs_http_request_t *request);
int esp32_mquickjs_http_clone_request(const esp32_mquickjs_http_request_t *source,
                                      esp32_mquickjs_http_request_t *target);
void esp32_mquickjs_http_free_response(esp32_mquickjs_http_response_t *response);
JSValue esp32_mquickjs_http_call_function(JSContext *ctx,
                                          JSValue func,
                                          JSValue this_val,
                                          int argc,
                                          JSValue *argv);
esp32_mquickjs_http_operation_t *esp32_mquickjs_http_operation_create(void);
void esp32_mquickjs_http_operation_destroy(esp32_mquickjs_http_operation_t *operation);
bool esp32_mquickjs_http_operation_cancel(esp32_mquickjs_http_operation_t *operation);
bool esp32_mquickjs_http_operation_is_cancelled(esp32_mquickjs_http_operation_t *operation);
esp32_mquickjs_http_response_t *esp32_mquickjs_http_perform_request(const esp32_mquickjs_http_request_t *request,
                                                                    esp32_mquickjs_http_operation_t *operation,
                                                                    esp_err_t *out_err,
                                                                    char *error_text,
                                                                    size_t error_text_size);
JSValue esp32_mquickjs_http_make_response_object(JSContext *ctx,
                                                 const esp32_mquickjs_http_response_t *response);
int esp32_mquickjs_http_build_request_from_args(JSContext *ctx,
                                                int argc,
                                                JSValue *argv,
                                                esp32_mquickjs_http_request_t *request);

#endif
