#include "esp32_mquickjs_http.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "utils/esp32_mquickjs_request_response.h"
#include "esp32_mquickjs_stream.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define ESP32_MQUICKJS_HTTP_MAX_REQUEST_HEADERS 16
#define ESP32_MQUICKJS_HTTP_MAX_RESPONSE_HEADERS 16
#define ESP32_MQUICKJS_HTTP_USER_AGENT "esp32qjs/1.0"

struct esp32_mquickjs_http_operation {
    SemaphoreHandle_t lock;
    esp_http_client_handle_t client;
    bool cancel_requested;
};

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
    esp32_mquickjs_http_operation_t *operation =
        heap_caps_calloc(1, sizeof(*operation), MALLOC_CAP_8BIT);

    if (operation == NULL) {
        return NULL;
    }
    operation->lock = xSemaphoreCreateMutex();
    if (operation->lock == NULL) {
        heap_caps_free(operation);
        return NULL;
    }
    return operation;
}

void esp32_mquickjs_http_operation_destroy(esp32_mquickjs_http_operation_t *operation)
{
    if (operation == NULL) {
        return;
    }
    if (operation->lock != NULL) {
        vSemaphoreDelete(operation->lock);
    }
    heap_caps_free(operation);
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
    if (operation->cancel_requested) {
        attached = false;
    } else {
        operation->client = client;
    }
    http_operation_unlock(operation);
    return attached;
}

static void http_operation_cleanup_client(esp32_mquickjs_http_operation_t *operation,
                                          esp_http_client_handle_t client)
{
    if (client == NULL) {
        return;
    }
    if (operation != NULL) {
        http_operation_lock(operation);
        if (operation->client == client) {
            operation->client = NULL;
        }
        http_operation_unlock(operation);
    }
    esp_http_client_cleanup(client);
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
    if (source->body != NULL) {
        target->body = esp32_mquickjs_http_strdup(source->body);
        if (target->body == NULL) {
            goto fail;
        }
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

static esp_err_t http_capture_append_body(esp32_mquickjs_http_capture_t *capture,
                                          const char *data,
                                          size_t data_len)
{
    static const size_t growth_step = 1024;
    size_t needed_cap;
    char *body;

    if (capture == NULL || capture->response == NULL || data == NULL || data_len == 0) {
        return ESP_OK;
    }

    if (data_len > capture->max_body_bytes ||
        capture->body_len > capture->max_body_bytes - data_len) {
        capture->body_limit_exceeded = true;
        return ESP_ERR_INVALID_SIZE;
    }

    needed_cap = capture->body_len + data_len + 1;
    if (capture->body_cap < needed_cap) {
        size_t new_cap = needed_cap;

        if (new_cap < growth_step) {
            new_cap = growth_step;
        } else {
            new_cap = ((new_cap + growth_step - 1) / growth_step) * growth_step;
        }
        if (new_cap > capture->max_body_bytes + 1) {
            new_cap = capture->max_body_bytes + 1;
        }

        body = heap_caps_realloc(capture->response->body, new_cap, MALLOC_CAP_8BIT);
        if (body == NULL) {
            return ESP_ERR_NO_MEM;
        }
        capture->response->body = body;
        capture->body_cap = new_cap;
    }

    memcpy(capture->response->body + capture->body_len, data, data_len);
    capture->body_len += data_len;
    capture->response->body[capture->body_len] = '\0';

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
    response->body = esp32_mquickjs_http_strdup("");
    if (response->status_text == NULL || response->body == NULL) {
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }

    return response;
}

esp32_mquickjs_http_response_t *esp32_mquickjs_http_perform_request(const esp32_mquickjs_http_request_t *request,
                                                                    esp32_mquickjs_http_operation_t *operation,
                                                                    esp_err_t *out_err,
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

    if (out_err == NULL || error_text == NULL || error_text_size == 0) {
        return NULL;
    }

    *out_err = ESP_OK;
    error_text[0] = '\0';
    if (request == NULL || request->url == NULL || request->method == NULL) {
        *out_err = ESP_ERR_INVALID_ARG;
        snprintf(error_text, error_text_size, "invalid fetch request");
        return NULL;
    }
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
    config.crt_bundle_attach = esp_crt_bundle_attach;
    client = esp_http_client_init(&config);
    if (client == NULL) {
        *out_err = ESP_FAIL;
        snprintf(error_text, error_text_size, "esp_http_client_init() failed");
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }
    if (!http_operation_attach_client(operation, client)) {
        *out_err = ESP_ERR_INVALID_STATE;
        snprintf(error_text, error_text_size, "fetch cancelled");
        http_operation_cleanup_client(operation, client);
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
            http_operation_cleanup_client(operation, client);
            esp32_mquickjs_http_free_response(response);
            return NULL;
        }
    }

    if (request->body != NULL) {
        esp_err_t body_err;

        if (!http_request_has_header(request, "Content-Type")) {
            body_err = esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
            if (body_err != ESP_OK) {
                *out_err = body_err;
                snprintf(error_text,
                         error_text_size,
                         "failed to set default Content-Type for request body: %s",
                         esp_err_to_name(body_err));
                http_operation_cleanup_client(operation, client);
                esp32_mquickjs_http_free_response(response);
                return NULL;
            }
        }

        body_err = esp_http_client_set_post_field(client, request->body, (int)strlen(request->body));
        if (body_err != ESP_OK) {
            *out_err = body_err;
            snprintf(error_text,
                     error_text_size,
                     "failed to set request body: %s",
                     esp_err_to_name(body_err));
            http_operation_cleanup_client(operation, client);
            esp32_mquickjs_http_free_response(response);
            return NULL;
        }
    }

    *out_err = esp_http_client_perform(client);
    if (esp32_mquickjs_http_operation_is_cancelled(operation)) {
        *out_err = ESP_ERR_INVALID_STATE;
        snprintf(error_text, error_text_size, "fetch cancelled");
        http_operation_cleanup_client(operation, client);
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
        http_operation_cleanup_client(operation, client);
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }
    if (*out_err != ESP_OK) {
        snprintf(error_text, error_text_size, "esp_http_client_perform() failed: %s",
                 esp_err_to_name(*out_err));
        http_operation_cleanup_client(operation, client);
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
        http_operation_cleanup_client(operation, client);
        esp32_mquickjs_http_free_response(response);
        return NULL;
    }

    if (esp_http_client_get_url(client, resolved_url, sizeof(resolved_url)) == ESP_OK) {
        response->url = esp32_mquickjs_http_strdup(resolved_url);
        if (response->url == NULL) {
            *out_err = ESP_ERR_NO_MEM;
            snprintf(error_text, error_text_size, "out of memory while storing response url");
            http_operation_cleanup_client(operation, client);
            esp32_mquickjs_http_free_response(response);
            return NULL;
        }
    }
    if (response->url == NULL) {
        response->url = esp32_mquickjs_http_strdup(request->url);
        if (response->url == NULL) {
            *out_err = ESP_ERR_NO_MEM;
            snprintf(error_text, error_text_size, "out of memory while storing response url");
            http_operation_cleanup_client(operation, client);
            esp32_mquickjs_http_free_response(response);
            return NULL;
        }
    }

    http_operation_cleanup_client(operation, client);
    return response;
}

JSValue esp32_mquickjs_http_make_response_object(JSContext *ctx,
                                                 const esp32_mquickjs_http_response_t *response)
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

    *body_stream = esp32_mquickjs_make_text_body_stream(ctx,
                                                        *global_obj,
                                                        response != NULL && response->body != NULL
                                                            ? response->body
                                                            : "");
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
    int timeout_ms = 0;

    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        *out_timeout_ms = ESP32_MQUICKJS_HTTP_DEFAULT_TIMEOUT_MS;
        return 0;
    }
    if (JS_ToInt32(ctx, &timeout_ms, value) != 0 || timeout_ms < 0) {
        return -1;
    }

    *out_timeout_ms = (uint32_t)timeout_ms;
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
    JSGCRef global_ref;
    JSGCRef object_ref;
    JSGCRef keys_ref;
    JSGCRef keys_array_ref;
    JSGCRef length_ref;
    JSValue *global_obj;
    JSValue *object_ctor;
    JSValue *keys_fn;
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

    global_obj = JS_PushGCRef(ctx, &global_ref);
    object_ctor = JS_PushGCRef(ctx, &object_ref);
    keys_fn = JS_PushGCRef(ctx, &keys_ref);
    keys_array = JS_PushGCRef(ctx, &keys_array_ref);
    length_value = JS_PushGCRef(ctx, &length_ref);

    *global_obj = JS_GetGlobalObject(ctx);
    *object_ctor = JS_UNDEFINED;
    *keys_fn = JS_UNDEFINED;
    *keys_array = JS_UNDEFINED;
    *length_value = JS_UNDEFINED;

    if (JS_IsException(*global_obj)) {
        goto done;
    }

    *object_ctor = JS_GetPropertyStr(ctx, *global_obj, "Object");
    if (JS_IsException(*object_ctor) || JS_IsUndefined(*object_ctor) || JS_IsNull(*object_ctor)) {
        JS_ThrowTypeError(ctx, "fetch(url, options) expects options.headers to be an object");
        goto done;
    }

    *keys_fn = JS_GetPropertyStr(ctx, *object_ctor, "keys");
    if (JS_IsException(*keys_fn) || !JS_IsFunction(ctx, *keys_fn)) {
        JS_ThrowInternalError(ctx, "Object.keys() is not available");
        goto done;
    }

    *keys_array = esp32_mquickjs_http_call_function(ctx, *keys_fn, *object_ctor, 1, headers_value);
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
    JS_PopGCRef(ctx, &keys_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &global_ref);
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
    JSCStringBuf body_buf;
    const char *method;
    const char *body;

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

    if (!JS_IsUndefined(*body_value) && !JS_IsNull(*body_value)) {
        if (esp32_mquickjs_stream_is_stream(ctx, *body_value)) {
            char *body_text = NULL;
            size_t body_len = 0;

            if (esp32_mquickjs_stream_read_all_text(ctx,
                                                    *body_value,
                                                    "fetch(url, options.body)",
                                                    &body_text,
                                                    &body_len) != 0) {
                goto fail;
            }
            request->body = body_text;
        } else {
            if (!JS_IsString(ctx, *body_value)) {
                JS_ThrowTypeError(ctx, "fetch(url, options.body) expects a string or Stream");
                goto fail;
            }

            body = JS_ToCString(ctx, *body_value, &body_buf);
            if (body == NULL) {
                goto fail;
            }
            request->body = esp32_mquickjs_http_strdup(body);
            if (request->body == NULL) {
                JS_ThrowOutOfMemory(ctx);
                goto fail;
            }
        }
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
    if (!JS_IsUndefined(*body_value) && !JS_IsNull(*body_value)) {
        if (esp32_mquickjs_stream_is_stream(ctx, *body_value)) {
            char *body_text = NULL;
            size_t body_len = 0;

            if (esp32_mquickjs_stream_read_all_text(ctx, *body_value, "fetch(request.body)", &body_text, &body_len) != 0) {
                goto fail;
            }
            request->body = body_text;
        } else if (JS_IsString(ctx, *body_value)) {
            JSCStringBuf body_buf;
            const char *body = JS_ToCString(ctx, *body_value, &body_buf);

            if (body == NULL) {
                goto fail;
            }
            request->body = esp32_mquickjs_http_strdup(body);
            if (request->body == NULL) {
                JS_ThrowOutOfMemory(ctx);
                goto fail;
            }
        } else {
            JS_ThrowTypeError(ctx, "fetch(request.body) expects a string or Stream");
            goto fail;
        }
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
    JSGCRef fetch_ref;
    JSValue *global;
    JSValue *fetch;
    JSValue result;

    (void)this_val;
    global = JS_PushGCRef(ctx, &global_ref);
    fetch = JS_PushGCRef(ctx, &fetch_ref);
    *global = JS_GetGlobalObject(ctx);
    *fetch = JS_GetPropertyStr(ctx, *global, "fetch");
    if (JS_IsException(*global) || JS_IsException(*fetch)) {
        result = JS_EXCEPTION;
    } else {
        result = esp32_mquickjs_future_call_and_wait(ctx,
                                                     esp32_mquickjs_get_active_runtime(),
                                                     *fetch,
                                                     *global,
                                                     argc,
                                                     argv);
    }
    JS_PopGCRef(ctx, &fetch_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

#endif
