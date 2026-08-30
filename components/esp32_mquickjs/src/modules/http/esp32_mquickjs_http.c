#include "esp32_mquickjs_http.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_http_client_resources.h"
#include "esp32_mquickjs_http_operation_resources.h"
#include "esp32_mquickjs_options.h"
#include "utils/esp32_mquickjs_request_response.h"
#include "esp32_mquickjs_stream.h"
#include "utils/esp32_mquickjs_byte_source.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
#include "esp_crt_bundle.h"
#endif
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define ESP32_MQUICKJS_HTTP_MAX_REQUEST_HEADERS 16
#define ESP32_MQUICKJS_HTTP_MAX_RESPONSE_HEADERS 16
#define ESP32_MQUICKJS_HTTP_USER_AGENT "esp32qjs/1.0"

struct esp32_mquickjs_http_operation {
    esp32_mquickjs_http_operation_resources_t resources;
    SemaphoreHandle_t lock;
    esp_http_client_handle_t client;
    bool cancel_requested;
};

static void *http_operation_resource_allocate(size_t size, void *opaque)
{
    (void)opaque;
    return heap_caps_calloc(1, size, MALLOC_CAP_8BIT);
}

static void http_operation_resource_release(void *value, void *opaque)
{
    (void)opaque;
    heap_caps_free(value);
}

static void *http_operation_resource_create_lock(void *opaque)
{
    (void)opaque;
    return xSemaphoreCreateMutex();
}

static void http_operation_resource_delete_lock(void *lock, void *opaque)
{
    (void)opaque;
    vSemaphoreDelete((SemaphoreHandle_t)lock);
}

static const esp32_mquickjs_http_operation_resource_ops_t
    s_http_operation_resource_ops = {
        .allocate = http_operation_resource_allocate,
        .release = http_operation_resource_release,
        .create_lock = http_operation_resource_create_lock,
        .delete_lock = http_operation_resource_delete_lock,
    };

static int http_client_resource_cleanup(void *client, void *opaque)
{
    (void)opaque;
    return esp_http_client_cleanup((esp_http_client_handle_t)client);
}

static const esp32_mquickjs_http_client_resource_ops_t
    s_http_client_resource_ops = {
        .cleanup = http_client_resource_cleanup,
    };

static esp_err_t http_operation_cleanup_client(
    esp32_mquickjs_http_operation_t *operation,
    esp_http_client_handle_t client);

typedef struct {
    esp32_mquickjs_http_response_t *response;
    size_t body_len;
    size_t body_cap;
    size_t max_body_bytes;
    esp_err_t error;
    bool body_limit_exceeded;
    bool drop_headers;
} esp32_mquickjs_http_capture_t;

static void http_operation_lock(esp32_mquickjs_http_operation_t *operation)
{
    if (operation != NULL && operation->lock != NULL) {
        xSemaphoreTake(operation->lock, portMAX_DELAY);
    }
}

static void http_operation_unlock(esp32_mquickjs_http_operation_t *operation)
{
    if (operation != NULL && operation->lock != NULL) {
        xSemaphoreGive(operation->lock);
    }
}

esp32_mquickjs_http_operation_t *esp32_mquickjs_http_operation_create(void)
{
    esp32_mquickjs_http_operation_resources_t resources;
    esp32_mquickjs_http_operation_t *operation;

    if (!esp32_mquickjs_http_operation_resources_init(
            &resources, &s_http_operation_resource_ops,
            sizeof(*operation))) {
        return NULL;
    }
    operation = resources.operation;
    operation->resources = resources;
    operation->lock = resources.lock;
    return operation;
}

bool esp32_mquickjs_http_operation_destroy(esp32_mquickjs_http_operation_t *operation)
{
    esp32_mquickjs_http_operation_resources_t resources;
    esp_err_t err;

    if (operation == NULL) {
        return true;
    }
    err = http_operation_cleanup_client(operation, operation->client);
    if (err != ESP_OK) {
        return false;
    }
    resources = operation->resources;
    esp32_mquickjs_http_operation_resources_deinit(
        &resources, &s_http_operation_resource_ops);
    return true;
}

bool esp32_mquickjs_http_operation_cancel(esp32_mquickjs_http_operation_t *operation)
{
    bool changed;

    if (operation == NULL) {
        return false;
    }
    http_operation_lock(operation);
    changed = !operation->cancel_requested;
    operation->cancel_requested = true;
    if (operation->client != NULL) {
        (void)esp_http_client_cancel_request(operation->client);
    }
    http_operation_unlock(operation);
    return changed;
}

bool esp32_mquickjs_http_operation_is_cancelled(esp32_mquickjs_http_operation_t *operation)
{
    bool cancelled;

    if (operation == NULL) {
        return false;
    }
    http_operation_lock(operation);
    cancelled = operation->cancel_requested;
    http_operation_unlock(operation);
    return cancelled;
}

static bool http_operation_attach_client(esp32_mquickjs_http_operation_t *operation,
                                         esp_http_client_handle_t client)
{
    bool attached = true;

    if (operation == NULL) {
        return true;
    }
    http_operation_lock(operation);
    operation->client = client;
    if (operation->cancel_requested) {
        attached = false;
    }
    http_operation_unlock(operation);
    return attached;
}

static esp_err_t http_operation_cleanup_client(
    esp32_mquickjs_http_operation_t *operation,
    esp_http_client_handle_t client)
{
    esp32_mquickjs_http_client_resources_t resources;
    esp_err_t err;

    if (client == NULL) {
        return ESP_OK;
    }
    if (operation == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    http_operation_lock(operation);
    if (operation->client != client) {
        http_operation_unlock(operation);
        return ESP_ERR_INVALID_STATE;
    }
    resources = (esp32_mquickjs_http_client_resources_t){
        .client = operation->client,
    };
    err = esp32_mquickjs_http_client_resources_deinit(
        &resources, &s_http_client_resource_ops);
    operation->client = (esp_http_client_handle_t)resources.client;
    http_operation_unlock(operation);
    return err;
}

static void http_operation_record_client_cleanup(
    esp32_mquickjs_http_operation_t *operation,
    esp_http_client_handle_t client,
    esp_err_t *out_err,
    char *error_text,
    size_t error_text_size)
{
    esp_err_t cleanup_err =
        http_operation_cleanup_client(operation, client);

    if (cleanup_err != ESP_OK) {
        *out_err = cleanup_err;
        snprintf(error_text, error_text_size,
                 "esp_http_client_cleanup() failed: %s",
                 esp_err_to_name(cleanup_err));
    }
}

char *esp32_mquickjs_http_strdup(const char *value)
{
    size_t len;
    char *copy;

    if (value == NULL) {
        return NULL;
    }

    len = strlen(value);
    copy = heap_caps_malloc(len + 1, MALLOC_CAP_8BIT);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, len + 1);
    return copy;
}

void esp32_mquickjs_http_free_headers(esp32_mquickjs_http_header_t *headers, size_t header_count)
{
    size_t i;

    if (headers == NULL) {
        return;
    }

    for (i = 0; i < header_count; ++i) {
        heap_caps_free(headers[i].key);
        heap_caps_free(headers[i].value);
    }
    heap_caps_free(headers);
}

void esp32_mquickjs_http_free_request(esp32_mquickjs_http_request_t *request)
{
    if (request == NULL) {
        return;
    }

    heap_caps_free(request->url);
    heap_caps_free(request->method);
    heap_caps_free(request->body);
    esp32_mquickjs_http_free_headers(request->headers, request->header_count);
    memset(request, 0, sizeof(*request));
}

int esp32_mquickjs_http_clone_request(const esp32_mquickjs_http_request_t *source,
                                      esp32_mquickjs_http_request_t *target)
{
    size_t i;

    if (source == NULL || target == NULL) {
        return -1;
    }

    memset(target, 0, sizeof(*target));
    target->timeout_ms = source->timeout_ms;
    target->max_body_bytes = source->max_body_bytes;
    target->body_len = source->body_len;
    target->body_present = source->body_present;
    target->body_binary = source->body_binary;

    if (source->url != NULL) {
        target->url = esp32_mquickjs_http_strdup(source->url);
        if (target->url == NULL) {
            goto fail;
        }
    }
    if (source->method != NULL) {
        target->method = esp32_mquickjs_http_strdup(source->method);
        if (target->method == NULL) {
            goto fail;
        }
    }
    if (source->body_len > 0) {
        target->body = esp32_mquickjs_memory_payload_alloc(
            source->body_len, ESP32_MQUICKJS_MEMORY_EXTERNAL);
        if (target->body == NULL) {
            goto fail;
        }
        memcpy(target->body, source->body, source->body_len);
    }
    if (source->header_count > 0) {
        target->headers = heap_caps_calloc(source->header_count, sizeof(*target->headers), MALLOC_CAP_8BIT);
        if (target->headers == NULL) {
            goto fail;
        }
        for (i = 0; i < source->header_count; ++i) {
            target->headers[i].key = esp32_mquickjs_http_strdup(source->headers[i].key);
            target->headers[i].value = esp32_mquickjs_http_strdup(source->headers[i].value);
            if (target->headers[i].key == NULL || target->headers[i].value == NULL) {
                target->header_count = i + 1;
                goto fail;
            }
        }
        target->header_count = source->header_count;
    }

    return 0;

fail:
    esp32_mquickjs_http_free_request(target);
    return -1;
}

void esp32_mquickjs_http_free_response(esp32_mquickjs_http_response_t *response)
{
    if (response == NULL) {
        return;
    }

    heap_caps_free(response->url);
    heap_caps_free(response->status_text);
    heap_caps_free(response->body);
    esp32_mquickjs_http_free_headers(response->headers, response->header_count);
    heap_caps_free(response);
}

JSValue esp32_mquickjs_http_call_function(JSContext *ctx,
                                          JSValue func,
                                          JSValue this_val,
                                          int argc,
                                          JSValue *argv)
{
    return esp32_mquickjs_call(ctx,
                               esp32_mquickjs_get_active_runtime(),
                               func,
                               this_val,
                               argc,
                               argv);
}

static const char *http_status_text(int32_t status)
{
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 202:
        return "Accepted";
    case 204:
        return "No Content";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 307:
        return "Temporary Redirect";
    case 308:
        return "Permanent Redirect";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 413:
        return "Payload Too Large";
    case 429:
        return "Too Many Requests";
    case 500:
        return "Internal Server Error";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    case 504:
        return "Gateway Timeout";
    default:
        return "";
    }
}

static bool http_method_from_string(const char *method, esp_http_client_method_t *out_method)
{
    struct {
        const char *name;
        esp_http_client_method_t method;
    } methods[] = {
        { "GET", HTTP_METHOD_GET },
        { "POST", HTTP_METHOD_POST },
        { "PUT", HTTP_METHOD_PUT },
        { "PATCH", HTTP_METHOD_PATCH },
        { "DELETE", HTTP_METHOD_DELETE },
        { "HEAD", HTTP_METHOD_HEAD },
        { "OPTIONS", HTTP_METHOD_OPTIONS },
    };
    size_t i;

    if (method == NULL || out_method == NULL) {
        return false;
    }

    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
        if (strcasecmp(method, methods[i].name) == 0) {
            *out_method = methods[i].method;
            return true;
        }
    }

    return false;
}

static bool http_request_has_header(const esp32_mquickjs_http_request_t *request, const char *name)
{
    size_t i;

    if (request == NULL || name == NULL) {
        return false;
    }
    for (i = 0; i < request->header_count; ++i) {
        if (request->headers[i].key != NULL && strcasecmp(request->headers[i].key, name) == 0) {
            return true;
        }
    }
    return false;
}

static int http_materialize_stream_body(JSContext *ctx,
                                        JSValue stream_value,
                                        const char *api_name,
                                        esp32_mquickjs_http_request_t *request)
{
    esp32_mquickjs_fs_stream_ref_t ref;
    uint8_t *body = NULL;
    size_t body_len = 0;
    bool binary;
    int result;

    if (!esp32_mquickjs_fs_parse_stream_ref(ctx, stream_value, &ref)) {
        return -1;
    }
    binary = esp32_mquickjs_stream_is_binary(&ref);
    result = esp32_mquickjs_stream_read_all_bytes(
        ctx, stream_value, api_name,
        CONFIG_ESP32_MQUICKJS_HTTP_MAX_REQUEST_BODY_BYTES,
        &body, &body_len);
    if (esp32_mquickjs_stream_close_value(ctx, stream_value) != ESP_OK &&
        result == 0) {
        heap_caps_free(body);
        JS_ThrowInternalError(ctx, "%s failed while closing the body stream",
                              api_name);
        return -1;
    }
    if (result != 0) {
        return -1;
    }

    request->body = body;
    request->body_len = body_len;
    request->body_present = true;
    request->body_binary = binary;
    return 0;
}

static int http_materialize_body(JSContext *ctx,
                                 JSValue body_value,
                                 const char *api_name,
                                 esp32_mquickjs_http_request_t *request)
{
    int class_id;

    if (JS_IsUndefined(body_value) || JS_IsNull(body_value)) {
        return 0;
    }
    if (JS_IsString(ctx, body_value)) {
        JSCStringBuf body_buf;
        const char *body;
        size_t body_len = 0;

        body = JS_ToCStringLen(ctx, &body_len, body_value, &body_buf);
        if (body == NULL) {
            return -1;
        }
        if (body_len > CONFIG_ESP32_MQUICKJS_HTTP_MAX_REQUEST_BODY_BYTES) {
            JS_ThrowRangeError(ctx, "%s exceeds the HTTP request body limit",
                               api_name);
            return -1;
        }
        if (body_len > 0) {
            request->body = esp32_mquickjs_memory_payload_alloc(
                body_len, ESP32_MQUICKJS_MEMORY_EXTERNAL);
            if (request->body == NULL) {
                JS_ThrowOutOfMemory(ctx);
                return -1;
            }
            memcpy(request->body, body, body_len);
        }
        request->body_len = body_len;
        request->body_present = true;
        request->body_binary = false;
        return 0;
    }
    if (esp32_mquickjs_stream_is_stream(ctx, body_value)) {
        return http_materialize_stream_body(ctx, body_value, api_name, request);
    }

    class_id = JS_GetClassID(ctx, body_value);
    if (class_id == JS_CLASS_BYTE_VIEW) {
        esp32_mquickjs_byte_source_t source = {0};
        uint8_t *converted = NULL;
        JSValue error = JS_UNDEFINED;

        if (!esp32_mquickjs_get_byte_source(ctx, body_value, api_name,
                                            &source, &converted, &error)) {
            return -1;
        }
        if (source.length >
            CONFIG_ESP32_MQUICKJS_HTTP_MAX_REQUEST_BODY_BYTES) {
            esp32_mquickjs_release_byte_source(converted);
            JS_ThrowRangeError(ctx, "%s exceeds the HTTP request body limit",
                               api_name);
            return -1;
        }
        if (source.length > 0) {
            request->body = esp32_mquickjs_memory_payload_alloc(
                source.length, ESP32_MQUICKJS_MEMORY_EXTERNAL);
            if (request->body == NULL) {
                esp32_mquickjs_release_byte_source(converted);
                JS_ThrowOutOfMemory(ctx);
                return -1;
            }
            memcpy(request->body, source.data, source.length);
        }
        request->body_len = source.length;
        request->body_present = true;
        request->body_binary = true;
        esp32_mquickjs_release_byte_source(converted);
        return 0;
    }
    if (class_id == JS_CLASS_BYTE_SPAN_SOURCE ||
        class_id == JS_CLASS_BITMAP_SPAN_SOURCE) {
        JSGCRef global_ref;
        JSGCRef stream_ref;
        JSValue *global = JS_PushGCRef(ctx, &global_ref);
        JSValue *stream = JS_PushGCRef(ctx, &stream_ref);
        int result;

        *global = JS_GetGlobalObject(ctx);
        *stream = JS_IsException(*global)
                      ? JS_EXCEPTION
                      : esp32_mquickjs_stream_open_byte_source(
                            ctx, *global, body_value);
        if (JS_IsException(*stream)) {
            JS_PopGCRef(ctx, &stream_ref);
            JS_PopGCRef(ctx, &global_ref);
            return -1;
        }
        result = http_materialize_stream_body(ctx, *stream, api_name, request);
        JS_PopGCRef(ctx, &stream_ref);
        JS_PopGCRef(ctx, &global_ref);
        return result;
    }

    JS_ThrowTypeError(
        ctx,
        "%s expects a string, Stream, ByteView, or ByteSpanSource",
        api_name);
    return -1;
}

static int http_validate_content_length(JSContext *ctx,
                                        const esp32_mquickjs_http_request_t *request)
{
    size_t i;

    for (i = 0; i < request->header_count; ++i) {
        const char *value;
        const char *cursor;
        char *end = NULL;
        unsigned long long parsed;
        size_t actual = request->body_present ? request->body_len : 0;

        if (request->headers[i].key == NULL ||
            strcasecmp(request->headers[i].key, "Content-Length") != 0) {
            continue;
        }
        value = request->headers[i].value;
        cursor = value != NULL ? value : "";
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        errno = 0;
        parsed = strtoull(cursor, &end, 10);
        while (end != NULL && isspace((unsigned char)*end)) {
            end++;
        }
        if (cursor[0] == '\0' || cursor[0] == '-' || errno != 0 ||
            end == cursor || end == NULL || *end != '\0' ||
            parsed > SIZE_MAX) {
            JS_ThrowTypeError(ctx,
                              "Content-Length must be a non-negative integer");
            return -1;
        }
        if ((size_t)parsed != actual) {
            JS_ThrowRangeError(
                ctx,
                "Content-Length (%llu) does not match the request body (%u bytes)",
                parsed, (unsigned)actual);
            return -1;
        }
    }
    return 0;
}

static esp_err_t http_capture_append_body(esp32_mquickjs_http_capture_t *capture,
                                          const char *data,
                                          size_t data_len)
{
    static const size_t growth_step = 1024;
    size_t needed_cap;
    uint8_t *body;

    if (capture == NULL || capture->response == NULL || data == NULL || data_len == 0) {
        return ESP_OK;
    }

    if (data_len > capture->max_body_bytes ||
        capture->body_len > capture->max_body_bytes - data_len) {
        capture->body_limit_exceeded = true;
        return ESP_ERR_INVALID_SIZE;
    }

    needed_cap = capture->body_len + data_len;
    if (capture->body_cap < needed_cap) {
        size_t new_cap = needed_cap;

        if (new_cap < growth_step) {
            new_cap = growth_step;
        } else {
            new_cap = ((new_cap + growth_step - 1) / growth_step) * growth_step;
        }
        if (new_cap > capture->max_body_bytes) {
            new_cap = capture->max_body_bytes;
        }

        body = esp32_mquickjs_memory_payload_realloc(
            capture->response->body,
            new_cap,
            ESP32_MQUICKJS_MEMORY_EXTERNAL);
        if (body == NULL) {
            return ESP_ERR_NO_MEM;
        }
        capture->response->body = body;
        capture->body_cap = new_cap;
    }

    memcpy(capture->response->body + capture->body_len, data, data_len);
    capture->body_len += data_len;
    capture->response->body_len = capture->body_len;

    return ESP_OK;
}

static esp_err_t http_capture_add_header(esp32_mquickjs_http_capture_t *capture,
                                         const char *key,
                                         const char *value)
{
    esp32_mquickjs_http_header_t *header;

    if (capture == NULL || capture->response == NULL || key == NULL || value == NULL) {
        return ESP_OK;
    }
    if (capture->drop_headers) {
        return ESP_OK;
    }
    if (capture->response->header_count >= ESP32_MQUICKJS_HTTP_MAX_RESPONSE_HEADERS) {
        capture->drop_headers = true;
        return ESP_OK;
    }

    if (capture->response->headers == NULL) {
        capture->response->headers = heap_caps_calloc(ESP32_MQUICKJS_HTTP_MAX_RESPONSE_HEADERS,
                                                      sizeof(*capture->response->headers),
                                                      MALLOC_CAP_8BIT);
        if (capture->response->headers == NULL) {
            capture->drop_headers = true;
            return ESP_OK;
        }
    }

    header = &capture->response->headers[capture->response->header_count++];
    header->key = esp32_mquickjs_http_strdup(key);
    header->value = esp32_mquickjs_http_strdup(value);
    if (header->key == NULL || header->value == NULL) {
        heap_caps_free(header->key);
        heap_caps_free(header->value);
        header->key = NULL;
        header->value = NULL;
        capture->response->header_count--;
        capture->drop_headers = true;
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    esp32_mquickjs_http_capture_t *capture;

    if (evt == NULL) {
        return ESP_OK;
    }
    capture = evt->user_data;
    if (capture == NULL) {
        return ESP_OK;
    }
    if (capture->error != ESP_OK) {
        return capture->error;
    }

    switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
        capture->error = http_capture_add_header(capture, evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        capture->error = http_capture_append_body(capture, evt->data, (size_t)evt->data_len);
        break;
    default:
        break;
    }

    return capture->error;
}

static esp32_mquickjs_http_response_t *http_alloc_response(void)
{
    esp32_mquickjs_http_response_t *response;

    response = heap_caps_calloc(1, sizeof(*response), MALLOC_CAP_8BIT);
    if (response == NULL) {
        return NULL;
    }

    response->status_text = esp32_mquickjs_http_strdup("");
    if (response->status_text == NULL) {
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }

    return response;
}

esp32_mquickjs_http_response_t *esp32_mquickjs_http_perform_request(const esp32_mquickjs_http_request_t *request,
                                                                    esp32_mquickjs_http_operation_t *operation,
                                                                    esp_err_t *out_err,
                                                                    esp32_mquickjs_tls_error_t *out_tls_error,
                                                                    char *error_text,
                                                                    size_t error_text_size)
{
    esp_http_client_config_t config = {0};
    esp_http_client_handle_t client = NULL;
    esp32_mquickjs_http_capture_t capture = {0};
    esp32_mquickjs_http_response_t *response = NULL;
    esp_http_client_method_t method = HTTP_METHOD_GET;
    size_t i;
    char resolved_url[384];

    if (out_err == NULL || out_tls_error == NULL || error_text == NULL ||
        error_text_size == 0) {
        return NULL;
    }

    *out_err = ESP_OK;
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    esp32_mquickjs_tls_error_reset(out_tls_error);
#else
    memset(out_tls_error, 0, sizeof(*out_tls_error));
#endif
    error_text[0] = '\0';
    if (request == NULL || request->url == NULL || request->method == NULL) {
        *out_err = ESP_ERR_INVALID_ARG;
        snprintf(error_text, error_text_size, "invalid fetch request");
        return NULL;
    }
#if !CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    if (strncmp(request->url, "https://", 8) == 0) {
        *out_err = ESP_ERR_NOT_SUPPORTED;
        snprintf(error_text, error_text_size,
                 "HTTPS requires the TLS firmware capability");
        return NULL;
    }
#endif
    if (!http_method_from_string(request->method, &method)) {
        *out_err = ESP_ERR_INVALID_ARG;
        snprintf(error_text, error_text_size, "unsupported HTTP method: %s", request->method);
        return NULL;
    }

    response = http_alloc_response();
    if (response == NULL) {
        *out_err = ESP_ERR_NO_MEM;
        snprintf(error_text, error_text_size, "out of memory while allocating response");
        return NULL;
    }

    capture.response = response;
    capture.max_body_bytes = request->max_body_bytes;
    config.url = request->url;
    config.method = method;
    config.timeout_ms = (int)request->timeout_ms;
    config.event_handler = http_event_handler;
    config.user_data = &capture;
    config.user_agent = ESP32_MQUICKJS_HTTP_USER_AGENT;
    config.buffer_size = 1024;
    config.buffer_size_tx = 1024;
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    config.crt_bundle_attach = esp32_mquickjs_tls_crt_bundle_attach;
#endif
    client = esp_http_client_init(&config);
    if (client == NULL) {
        *out_err = ESP_FAIL;
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
        if (strncmp(request->url, "https://", 8) == 0) {
            esp32_mquickjs_tls_error_set(
                out_tls_error, ESP_ERR_NO_MEM, ESP_OK, 0, 0);
        }
#endif
        snprintf(error_text, error_text_size, "esp_http_client_init() failed");
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }
    if (!http_operation_attach_client(operation, client)) {
        *out_err = ESP_ERR_INVALID_STATE;
        snprintf(error_text, error_text_size, "fetch cancelled");
        http_operation_record_client_cleanup(
            operation, client, out_err, error_text, error_text_size);
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }

    for (i = 0; i < request->header_count; ++i) {
        if (esp_http_client_set_header(client,
                                       request->headers[i].key,
                                       request->headers[i].value) != ESP_OK) {
            *out_err = ESP_FAIL;
            snprintf(error_text, error_text_size, "failed to set request header: %s",
                     request->headers[i].key);
            http_operation_record_client_cleanup(
                operation, client, out_err, error_text, error_text_size);
            esp32_mquickjs_http_free_response(response);
            return NULL;
        }
    }

    if (request->body_present) {
        esp_err_t body_err;

        if (!request->body_binary &&
            !http_request_has_header(request, "Content-Type")) {
            body_err = esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
            if (body_err != ESP_OK) {
                *out_err = body_err;
                snprintf(error_text,
                         error_text_size,
                         "failed to set default Content-Type for request body: %s",
                         esp_err_to_name(body_err));
                http_operation_record_client_cleanup(
                    operation, client, out_err, error_text, error_text_size);
                esp32_mquickjs_http_free_response(response);
                return NULL;
            }
        }

        body_err = esp_http_client_set_post_field(
            client,
            request->body_len > 0 ? (const char *)request->body : "",
            (int)request->body_len);
        if (body_err != ESP_OK) {
            *out_err = body_err;
            snprintf(error_text,
                     error_text_size,
                     "failed to set request body: %s",
                     esp_err_to_name(body_err));
            http_operation_record_client_cleanup(
                operation, client, out_err, error_text, error_text_size);
            esp32_mquickjs_http_free_response(response);
            return NULL;
        }
    }

    *out_err = esp_http_client_perform(client);
#if CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    if (strncmp(request->url, "https://", 8) == 0) {
        int mbedtls_error = 0;
        int verify_flags = 0;
        esp_err_t esp_tls_error =
            esp_http_client_get_and_clear_last_tls_error(
                client, &mbedtls_error, &verify_flags);

        esp32_mquickjs_tls_error_set(
            out_tls_error, *out_err, esp_tls_error, mbedtls_error,
            (uint32_t)verify_flags);
        esp32_mquickjs_tls_error_merge_verify_flags(out_tls_error);
    }
#endif
    if (esp32_mquickjs_http_operation_is_cancelled(operation)) {
        *out_err = ESP_ERR_INVALID_STATE;
        snprintf(error_text, error_text_size, "fetch cancelled");
        http_operation_record_client_cleanup(
            operation, client, out_err, error_text, error_text_size);
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }
    if (capture.error != ESP_OK) {
        *out_err = capture.error;
        if (capture.body_limit_exceeded) {
            snprintf(error_text,
                     error_text_size,
                     "HTTP response body exceeds maxBodyBytes (%u)",
                     (unsigned)request->max_body_bytes);
        } else {
            snprintf(error_text, error_text_size, "failed to capture HTTP response: %s",
                     esp_err_to_name(capture.error));
        }
        http_operation_record_client_cleanup(
            operation, client, out_err, error_text, error_text_size);
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }
    if (*out_err != ESP_OK) {
        snprintf(error_text, error_text_size, "esp_http_client_perform() failed: %s",
                 esp_err_to_name(*out_err));
        http_operation_record_client_cleanup(
            operation, client, out_err, error_text, error_text_size);
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }

    response->status = esp_http_client_get_status_code(client);
    response->ok = response->status >= 200 && response->status < 300;
    heap_caps_free(response->status_text);
    response->status_text = esp32_mquickjs_http_strdup(http_status_text(response->status));
    if (response->status_text == NULL) {
        *out_err = ESP_ERR_NO_MEM;
        snprintf(error_text, error_text_size, "out of memory while storing status text");
        http_operation_record_client_cleanup(
            operation, client, out_err, error_text, error_text_size);
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }

    if (esp_http_client_get_url(client, resolved_url, sizeof(resolved_url)) == ESP_OK) {
        response->url = esp32_mquickjs_http_strdup(resolved_url);
        if (response->url == NULL) {
            *out_err = ESP_ERR_NO_MEM;
            snprintf(error_text, error_text_size, "out of memory while storing response url");
            http_operation_record_client_cleanup(
                operation, client, out_err, error_text, error_text_size);
            esp32_mquickjs_http_free_response(response);
            return NULL;
        }
    }
    if (response->url == NULL) {
        response->url = esp32_mquickjs_http_strdup(request->url);
        if (response->url == NULL) {
            *out_err = ESP_ERR_NO_MEM;
            snprintf(error_text, error_text_size, "out of memory while storing response url");
            http_operation_record_client_cleanup(
                operation, client, out_err, error_text, error_text_size);
            esp32_mquickjs_http_free_response(response);
            return NULL;
        }
    }

    http_operation_record_client_cleanup(
        operation, client, out_err, error_text, error_text_size);
    if (*out_err != ESP_OK) {
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }
    return response;
}

JSValue esp32_mquickjs_http_make_response_object(JSContext *ctx,
                                                 esp32_mquickjs_http_response_t *response)
{
    JSGCRef global_ref;
    JSGCRef headers_ref;
    JSGCRef body_ref;
    JSGCRef plain_ref;
    JSValue *global_obj;
    JSValue *headers_obj;
    JSValue *body_stream;
    JSValue *plain_headers;
    JSValue result = JS_EXCEPTION;

    global_obj = JS_PushGCRef(ctx, &global_ref);
    headers_obj = JS_PushGCRef(ctx, &headers_ref);
    body_stream = JS_PushGCRef(ctx, &body_ref);
    plain_headers = JS_PushGCRef(ctx, &plain_ref);
    *global_obj = JS_GetGlobalObject(ctx);
    *headers_obj = JS_NewObject(ctx);
    *body_stream = JS_UNDEFINED;
    *plain_headers = JS_UNDEFINED;
    if (JS_IsException(*global_obj) || JS_IsException(*headers_obj)) {
        goto done;
    }

    if (response != NULL) {
        size_t i;

        for (i = 0; i < response->header_count; ++i) {
            if (!esp32_mquickjs_set_property_ref(ctx,
                                             headers_obj,
                                             response->headers[i].key,
                                             JS_NewString(ctx, response->headers[i].value))) {
                goto done;
            }
        }
    }

    {
        uint8_t *body = response != NULL ? response->body : NULL;
        size_t body_len = response != NULL ? response->body_len : 0;

        if (response != NULL) {
            response->body = NULL;
            response->body_len = 0;
        }
        *body_stream = esp32_mquickjs_stream_open_memory_owned_binary(
            ctx, *global_obj, body, body_len);
    }
    if (JS_IsException(*body_stream)) {
        goto done;
    }

    *plain_headers = esp32_mquickjs_make_headers_object(ctx, *global_obj, *headers_obj);
    if (JS_IsException(*plain_headers)) {
        goto done;
    }

    result = esp32_mquickjs_make_response_object(ctx,
                                                 *global_obj,
                                                 response != NULL ? response->status : 200,
                                                 response != NULL ? response->status_text : "OK",
                                                 response != NULL ? response->url : "",
                                                 *plain_headers,
                                                 *body_stream);

done:
    JS_PopGCRef(ctx, &plain_ref);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

static int http_parse_timeout(JSContext *ctx,
                              JSValue value,
                              uint32_t *out_timeout_ms)
{
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        *out_timeout_ms = ESP32_MQUICKJS_HTTP_DEFAULT_TIMEOUT_MS;
        return 0;
    }
    if (!esp32_mquickjs_value_to_bounded_u32(
            ctx, value, 0, INT32_MAX, out_timeout_ms)) {
        return -1;
    }
    return 0;
}

static int http_parse_max_body_bytes(JSContext *ctx,
                                     JSValue value,
                                     size_t *out_max_body_bytes)
{
    int max_body_bytes = 0;

    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        *out_max_body_bytes = CONFIG_ESP32_MQUICKJS_HTTP_MAX_RESPONSE_BODY_BYTES;
        return 0;
    }
    if (JS_ToInt32(ctx, &max_body_bytes, value) != 0 || max_body_bytes <= 0) {
        return -1;
    }

    *out_max_body_bytes = (size_t)max_body_bytes;
    return 0;
}

static int http_parse_headers(JSContext *ctx,
                              JSValue *headers_value,
                              esp32_mquickjs_http_header_t **out_headers,
                              size_t *out_header_count)
{
    JSGCRef keys_array_ref;
    JSGCRef length_ref;
    JSValue *keys_array;
    JSValue *length_value;
    esp32_mquickjs_http_header_t *headers = NULL;
    int length = 0;
    int header_count = 0;
    int i;
    int result = -1;

    if (JS_IsUndefined(*headers_value) || JS_IsNull(*headers_value)) {
        *out_headers = NULL;
        *out_header_count = 0;
        return 0;
    }
    if (esp32_mquickjs_is_headers_object(ctx, *headers_value)) {
        *headers_value = esp32_mquickjs_headers_to_plain_object(ctx, *headers_value);
        if (JS_IsException(*headers_value)) {
            return -1;
        }
    }

    keys_array = JS_PushGCRef(ctx, &keys_array_ref);
    length_value = JS_PushGCRef(ctx, &length_ref);

    *keys_array = JS_UNDEFINED;
    *length_value = JS_UNDEFINED;

    if (JS_GetClassID(ctx, *headers_value) != JS_CLASS_OBJECT) {
        JS_ThrowTypeError(
            ctx, "fetch(url, options) expects options.headers to be a plain object");
        goto done;
    }
    *keys_array = esp32_mquickjs_own_property_keys(ctx, *headers_value);
    if (JS_IsException(*keys_array)) {
        goto done;
    }

    *length_value = JS_GetPropertyStr(ctx, *keys_array, "length");
    if (JS_IsException(*length_value) || JS_ToInt32(ctx, &length, *length_value) != 0 || length < 0) {
        JS_ThrowTypeError(ctx, "fetch(url, options.headers) expects a plain object");
        goto done;
    }

    if (length > ESP32_MQUICKJS_HTTP_MAX_REQUEST_HEADERS) {
        length = ESP32_MQUICKJS_HTTP_MAX_REQUEST_HEADERS;
    }

    if (length > 0) {
        headers = heap_caps_calloc((size_t)length, sizeof(*headers), MALLOC_CAP_8BIT);
        if (headers == NULL) {
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }
    }

    for (i = 0; i < length; ++i) {
        JSGCRef key_ref;
        JSGCRef value_ref;
        JSValue *key_value;
        JSValue *value_value;
        JSCStringBuf key_buf;
        JSCStringBuf value_buf;
        const char *key;
        const char *value;
        char *key_copy;

        key_value = JS_PushGCRef(ctx, &key_ref);
        value_value = JS_PushGCRef(ctx, &value_ref);
        *key_value = JS_GetPropertyUint32(ctx, *keys_array, (uint32_t)i);
        *value_value = JS_UNDEFINED;
        if (JS_IsException(*key_value)) {
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }

        key = JS_ToCString(ctx, *key_value, &key_buf);
        if (key == NULL) {
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }
        key_copy = esp32_mquickjs_http_strdup(key);
        if (key_copy == NULL) {
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }

        *value_value = JS_GetPropertyStr(ctx, *headers_value, key_copy);
        if (JS_IsException(*value_value)) {
            heap_caps_free(key_copy);
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }

        value = JS_ToCString(ctx, *value_value, &value_buf);
        if (value == NULL) {
            heap_caps_free(key_copy);
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }

        headers[header_count].value = esp32_mquickjs_http_strdup(value);
        if (headers[header_count].value == NULL) {
            heap_caps_free(key_copy);
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }
        headers[header_count].key = key_copy;
        header_count++;
        JS_PopGCRef(ctx, &value_ref);
        JS_PopGCRef(ctx, &key_ref);
    }

    *out_headers = headers;
    *out_header_count = (size_t)header_count;
    headers = NULL;
    result = 0;

done:
    esp32_mquickjs_http_free_headers(headers, (size_t)header_count);
    JS_PopGCRef(ctx, &length_ref);
    JS_PopGCRef(ctx, &keys_array_ref);
    return result;
}

static int http_parse_options(JSContext *ctx,
                              JSValue *options_value,
                              esp32_mquickjs_http_request_t *request)
{
    JSGCRef method_ref;
    JSGCRef body_ref;
    JSGCRef timeout_ref;
    JSGCRef max_body_ref;
    JSGCRef headers_ref;
    JSValue *method_value;
    JSValue *body_value;
    JSValue *timeout_value;
    JSValue *max_body_value;
    JSValue *headers_value;
    JSCStringBuf method_buf;
    const char *method;

    if (JS_IsUndefined(*options_value) || JS_IsNull(*options_value)) {
        return 0;
    }
    if (JS_IsString(ctx, *options_value) || JS_IsBool(*options_value) || JS_IsFunction(ctx, *options_value)) {
        JS_ThrowTypeError(ctx, "fetch(url, options) expects options to be an object");
        return -1;
    }

    method_value = JS_PushGCRef(ctx, &method_ref);
    body_value = JS_PushGCRef(ctx, &body_ref);
    timeout_value = JS_PushGCRef(ctx, &timeout_ref);
    max_body_value = JS_PushGCRef(ctx, &max_body_ref);
    headers_value = JS_PushGCRef(ctx, &headers_ref);
    *method_value = JS_GetPropertyStr(ctx, *options_value, "method");
    *body_value = JS_GetPropertyStr(ctx, *options_value, "body");
    *timeout_value = JS_GetPropertyStr(ctx, *options_value, "timeoutMs");
    *max_body_value = JS_GetPropertyStr(ctx, *options_value, "maxBodyBytes");
    *headers_value = JS_GetPropertyStr(ctx, *options_value, "headers");

    if (JS_IsException(*method_value) || JS_IsException(*body_value) ||
        JS_IsException(*timeout_value) || JS_IsException(*max_body_value) ||
        JS_IsException(*headers_value)) {
        goto fail;
    }

    if (!JS_IsUndefined(*method_value) && !JS_IsNull(*method_value)) {
        if (!JS_IsString(ctx, *method_value)) {
            JS_ThrowTypeError(ctx, "fetch(url, options.method) expects a string");
            goto fail;
        }

        method = JS_ToCString(ctx, *method_value, &method_buf);
        if (method == NULL) {
            goto fail;
        }
        heap_caps_free(request->method);
        request->method = esp32_mquickjs_http_strdup(method);
        if (request->method == NULL) {
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
    }

    if (http_materialize_body(ctx, *body_value,
                              "fetch(url, options.body)", request) != 0) {
        goto fail;
    }

    if (http_parse_timeout(ctx, *timeout_value, &request->timeout_ms) != 0) {
        JS_ThrowTypeError(ctx, "fetch(url, options.timeoutMs) expects a non-negative integer");
        goto fail;
    }

    if (http_parse_max_body_bytes(ctx, *max_body_value, &request->max_body_bytes) != 0) {
        JS_ThrowTypeError(ctx, "fetch(url, options.maxBodyBytes) expects a positive integer");
        goto fail;
    }

    if (http_parse_headers(ctx, headers_value, &request->headers, &request->header_count) != 0) {
        goto fail;
    }
    if (http_validate_content_length(ctx, request) != 0) {
        goto fail;
    }

    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &max_body_ref);
    JS_PopGCRef(ctx, &timeout_ref);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &method_ref);
    return 0;

fail:
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &max_body_ref);
    JS_PopGCRef(ctx, &timeout_ref);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &method_ref);
    return -1;
}

static int http_parse_request_object(JSContext *ctx,
                                     JSValue *request_value,
                                     esp32_mquickjs_http_request_t *request)
{
    JSGCRef method_ref;
    JSGCRef url_ref;
    JSGCRef headers_ref;
    JSGCRef body_ref;
    JSGCRef timeout_ref;
    JSGCRef max_body_ref;
    JSValue *method_value;
    JSValue *url_value;
    JSValue *headers_value;
    JSValue *body_value;
    JSValue *timeout_value;
    JSValue *max_body_value;
    JSCStringBuf method_buf;
    JSCStringBuf url_buf;
    const char *method;
    const char *url;

    method_value = JS_PushGCRef(ctx, &method_ref);
    url_value = JS_PushGCRef(ctx, &url_ref);
    headers_value = JS_PushGCRef(ctx, &headers_ref);
    body_value = JS_PushGCRef(ctx, &body_ref);
    timeout_value = JS_PushGCRef(ctx, &timeout_ref);
    max_body_value = JS_PushGCRef(ctx, &max_body_ref);
    *method_value = JS_GetPropertyStr(ctx, *request_value, "method");
    *url_value = JS_GetPropertyStr(ctx, *request_value, "url");
    *headers_value = JS_GetPropertyStr(ctx, *request_value, "headers");
    *body_value = JS_GetPropertyStr(ctx, *request_value, "body");
    *timeout_value = JS_GetPropertyStr(ctx, *request_value, "timeoutMs");
    *max_body_value = JS_GetPropertyStr(ctx, *request_value, "maxBodyBytes");

    if (JS_IsException(*method_value) || JS_IsException(*url_value) || JS_IsException(*headers_value) ||
        JS_IsException(*body_value) || JS_IsException(*timeout_value) || JS_IsException(*max_body_value)) {
        goto fail;
    }
    if (!JS_IsString(ctx, *method_value) || !JS_IsString(ctx, *url_value)) {
        JS_ThrowTypeError(ctx, "fetch(request) expects Request.method and Request.url");
        goto fail;
    }

    method = JS_ToCString(ctx, *method_value, &method_buf);
    if (method == NULL) {
        goto fail;
    }
    request->method = esp32_mquickjs_http_strdup(method);
    if (request->method == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }

    url = JS_ToCString(ctx, *url_value, &url_buf);
    if (url == NULL) {
        goto fail;
    }
#if !CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    if (strncmp(url, "https://", 8) == 0) {
        JS_ThrowTypeError(ctx, "HTTPS requires the TLS firmware capability");
        goto fail;
    }
#endif
    request->url = esp32_mquickjs_http_strdup(url);
    request->timeout_ms = ESP32_MQUICKJS_HTTP_DEFAULT_TIMEOUT_MS;
    request->max_body_bytes = CONFIG_ESP32_MQUICKJS_HTTP_MAX_RESPONSE_BODY_BYTES;
    if (request->url == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }

    if (http_parse_timeout(ctx, *timeout_value, &request->timeout_ms) != 0) {
        JS_ThrowTypeError(ctx, "fetch(request) expects Request.timeoutMs to be a non-negative integer");
        goto fail;
    }
    if (http_parse_max_body_bytes(ctx, *max_body_value, &request->max_body_bytes) != 0) {
        JS_ThrowTypeError(ctx, "fetch(request) expects Request.maxBodyBytes to be a positive integer");
        goto fail;
    }
    if (http_parse_headers(ctx, headers_value, &request->headers, &request->header_count) != 0) {
        goto fail;
    }
    if (http_materialize_body(ctx, *body_value, "fetch(request.body)",
                              request) != 0) {
        goto fail;
    }
    if (http_validate_content_length(ctx, request) != 0) {
        goto fail;
    }

    JS_PopGCRef(ctx, &max_body_ref);
    JS_PopGCRef(ctx, &timeout_ref);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &url_ref);
    JS_PopGCRef(ctx, &method_ref);
    return 0;

fail:
    JS_PopGCRef(ctx, &max_body_ref);
    JS_PopGCRef(ctx, &timeout_ref);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &url_ref);
    JS_PopGCRef(ctx, &method_ref);
    return -1;
}

int esp32_mquickjs_http_build_request_from_args(JSContext *ctx,
                                                int argc,
                                                JSGCRef *argv,
                                                esp32_mquickjs_http_request_t *request)
{
    JSCStringBuf url_buf;
    const char *url;

    if (argc < 1 || argc > 2) {
        JS_ThrowTypeError(ctx, "fetch(input, options?) expects a URL string or Request");
        return -1;
    }

    if (argc >= 2) {
        if (JS_IsFunction(ctx, argv[1].val)) {
            JS_ThrowTypeError(ctx,
                              "fetch(input, options?) does not accept a callback; use Future.call(fetch, globalThis, args)");
            return -1;
        }
    }

    if (esp32_mquickjs_is_request_object(ctx, argv[0].val)) {
        if (argc >= 2 && !JS_IsUndefined(argv[1].val) && !JS_IsNull(argv[1].val)) {
            JS_ThrowTypeError(ctx, "fetch(request) does not accept a separate options object");
            return -1;
        }
        return http_parse_request_object(ctx, &argv[0].val, request);
    }

    if (!JS_IsString(ctx, argv[0].val)) {
        JS_ThrowTypeError(ctx, "fetch(input, options?) expects a URL string or Request");
        return -1;
    }

    url = JS_ToCString(ctx, argv[0].val, &url_buf);
    if (url == NULL) {
        return -1;
    }
#if !CONFIG_ESP32_MQUICKJS_FEATURE_TLS
    if (strncmp(url, "https://", 8) == 0) {
        JS_ThrowTypeError(ctx, "HTTPS requires the TLS firmware capability");
        return -1;
    }
#endif

    request->url = esp32_mquickjs_http_strdup(url);
    request->method = esp32_mquickjs_http_strdup("GET");
    request->timeout_ms = ESP32_MQUICKJS_HTTP_DEFAULT_TIMEOUT_MS;
    request->max_body_bytes = CONFIG_ESP32_MQUICKJS_HTTP_MAX_RESPONSE_BODY_BYTES;
    if (request->url == NULL || request->method == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }

    return argc >= 2 ? http_parse_options(ctx, &argv[1].val, request) : 0;
}

bool esp32_mquickjs_init_http_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime)
{
    return esp32_mquickjs_init_http_future_runtime(ctx, runtime);
}

JSValue js_http_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_HTTP_DEFAULT_TIMEOUT_MS);
}

JSValue js_http_get_max_body_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, CONFIG_ESP32_MQUICKJS_HTTP_MAX_RESPONSE_BODY_BYTES);
}

JSValue js_http_fetch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef global_ref;
    JSGCRef http_ref;
    JSGCRef fetch_ref;
    JSValue *global;
    JSValue *http;
    JSValue *fetch;
    JSValue result;

    (void)this_val;
    global = JS_PushGCRef(ctx, &global_ref);
    http = JS_PushGCRef(ctx, &http_ref);
    fetch = JS_PushGCRef(ctx, &fetch_ref);
    *global = JS_GetGlobalObject(ctx);
    *http = JS_GetPropertyStr(ctx, *global, "http");
    *fetch = JS_IsException(*http)
                 ? JS_EXCEPTION
                 : JS_GetPropertyStr(ctx, *http, "fetch");
    if (JS_IsException(*global) || JS_IsException(*http) ||
        JS_IsException(*fetch)) {
        result = JS_EXCEPTION;
    } else {
        result = esp32_mquickjs_future_call_and_wait(ctx,
                                                     esp32_mquickjs_get_active_runtime(),
                                                     *fetch,
                                                     *http,
                                                     argc,
                                                     argv);
    }
    JS_PopGCRef(ctx, &fetch_ref);
    JS_PopGCRef(ctx, &http_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

#endif
