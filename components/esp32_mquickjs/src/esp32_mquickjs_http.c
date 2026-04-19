#include "esp32_mquickjs_internal.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define ESP32_MQUICKJS_HTTP_MAX_REQUEST_HEADERS 16
#define ESP32_MQUICKJS_HTTP_MAX_RESPONSE_HEADERS 16
#define ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS 4
#define ESP32_MQUICKJS_HTTP_ASYNC_QUEUE_LEN 4
#define ESP32_MQUICKJS_HTTP_USER_AGENT "esp32qjs/1.0"
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
    esp32_mquickjs_http_header_t *headers;
    size_t header_count;
} esp32_mquickjs_http_request_t;

typedef struct {
    bool ok;
    bool truncated;
    int32_t status;
    int64_t content_length;
    char *url;
    char *status_text;
    char *body;
    esp32_mquickjs_http_header_t *headers;
    size_t header_count;
} esp32_mquickjs_http_response_t;

typedef struct {
    esp32_mquickjs_http_response_t *response;
    size_t body_len;
    size_t body_cap;
    esp_err_t error;
    bool drop_body;
    bool drop_headers;
} esp32_mquickjs_http_capture_t;

typedef struct esp32_mquickjs_http_async_slot esp32_mquickjs_http_async_slot_t;

typedef struct {
    uint8_t slot_id;
    uint32_t generation;
    esp_err_t err;
    esp32_mquickjs_http_response_t *response;
    char error_text[ESP32_MQUICKJS_HTTP_ERROR_TEXT_LEN];
} esp32_mquickjs_http_async_event_t;

struct esp32_mquickjs_http_async_slot {
    uint8_t slot_id;
    bool allocated;
    uint32_t generation;
    JSGCRef callback;
    esp32_mquickjs_http_request_t request;
};

typedef struct {
    bool initialized;
    QueueHandle_t queue;
    SemaphoreHandle_t lock;
    esp32_mquickjs_http_async_slot_t slots[ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS];
} esp32_mquickjs_http_state_t;

typedef struct {
    esp32_mquickjs_http_async_slot_t *slot;
    uint32_t generation;
} esp32_mquickjs_http_worker_args_t;

static esp32_mquickjs_http_state_t s_http_state;

static void http_lock(void)
{
    if (s_http_state.lock != NULL) {
        xSemaphoreTake(s_http_state.lock, portMAX_DELAY);
    }
}

static void http_unlock(void)
{
    if (s_http_state.lock != NULL) {
        xSemaphoreGive(s_http_state.lock);
    }
}

static char *http_strdup(const char *value)
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

static void http_free_headers(esp32_mquickjs_http_header_t *headers, size_t header_count)
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

static void http_free_request(esp32_mquickjs_http_request_t *request)
{
    if (request == NULL) {
        return;
    }

    heap_caps_free(request->url);
    heap_caps_free(request->method);
    heap_caps_free(request->body);
    http_free_headers(request->headers, request->header_count);
    memset(request, 0, sizeof(*request));
}

static void http_free_response(esp32_mquickjs_http_response_t *response)
{
    if (response == NULL) {
        return;
    }

    heap_caps_free(response->url);
    heap_caps_free(response->status_text);
    heap_caps_free(response->body);
    http_free_headers(response->headers, response->header_count);
    heap_caps_free(response);
}

static bool http_init_state(void)
{
    int i;

    if (s_http_state.initialized) {
        return true;
    }

    memset(&s_http_state, 0, sizeof(s_http_state));
    s_http_state.queue = xQueueCreate(ESP32_MQUICKJS_HTTP_ASYNC_QUEUE_LEN,
                                      sizeof(esp32_mquickjs_http_async_event_t));
    s_http_state.lock = xSemaphoreCreateMutex();
    if (s_http_state.queue == NULL || s_http_state.lock == NULL) {
        return false;
    }

    for (i = 0; i < ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS; ++i) {
        s_http_state.slots[i].slot_id = (uint8_t)i;
    }
    s_http_state.initialized = true;
    return true;
}

static JSValue http_call_function(JSContext *ctx,
                                  JSValue func,
                                  JSValue this_val,
                                  int argc,
                                  JSValue *argv)
{
    int i;

    if (JS_StackCheck(ctx, (uint32_t)(argc + 2))) {
        return JS_EXCEPTION;
    }

    for (i = argc - 1; i >= 0; --i) {
        JS_PushArg(ctx, argv[i]);
    }
    JS_PushArg(ctx, func);
    JS_PushArg(ctx, this_val);
    return JS_Call(ctx, argc);
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

static esp_err_t http_capture_append_body(esp32_mquickjs_http_capture_t *capture,
                                          const char *data,
                                          size_t data_len)
{
    static const size_t growth_step = 1024;
    size_t needed_cap;
    size_t room_left;
    size_t write_len;
    char *body;

    if (capture == NULL || capture->response == NULL || data == NULL || data_len == 0) {
        return ESP_OK;
    }
    if (capture->drop_body) {
        capture->response->truncated = true;
        return ESP_OK;
    }

    room_left = ESP32_MQUICKJS_HTTP_MAX_RESPONSE_BYTES > capture->body_len
                    ? ESP32_MQUICKJS_HTTP_MAX_RESPONSE_BYTES - capture->body_len
                    : 0;
    write_len = data_len <= room_left ? data_len : room_left;
    if (write_len == 0) {
        capture->response->truncated = true;
        capture->drop_body = true;
        return ESP_OK;
    }

    needed_cap = capture->body_len + write_len + 1;
    if (capture->body_cap < needed_cap) {
        size_t new_cap = needed_cap;

        if (new_cap < growth_step) {
            new_cap = growth_step;
        } else {
            new_cap = ((new_cap + growth_step - 1) / growth_step) * growth_step;
        }
        if (new_cap > ESP32_MQUICKJS_HTTP_MAX_RESPONSE_BYTES + 1) {
            new_cap = ESP32_MQUICKJS_HTTP_MAX_RESPONSE_BYTES + 1;
        }

        body = heap_caps_realloc(capture->response->body, new_cap, MALLOC_CAP_8BIT);
        if (body == NULL) {
            capture->response->truncated = true;
            capture->drop_body = true;
            return ESP_OK;
        }
        capture->response->body = body;
        capture->body_cap = new_cap;
    }

    memcpy(capture->response->body + capture->body_len, data, write_len);
    capture->body_len += write_len;
    capture->response->body[capture->body_len] = '\0';
    if (write_len < data_len) {
        capture->response->truncated = true;
        capture->drop_body = true;
    }

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
    header->key = http_strdup(key);
    header->value = http_strdup(value);
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

    response->status_text = http_strdup("");
    response->body = http_strdup("");
    if (response->status_text == NULL || response->body == NULL) {
        http_free_response(response);
        return NULL;
    }

    return response;
}

static esp32_mquickjs_http_response_t *http_perform_request(const esp32_mquickjs_http_request_t *request,
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
        http_free_response(response);
        return NULL;
    }

    for (i = 0; i < request->header_count; ++i) {
        if (esp_http_client_set_header(client,
                                       request->headers[i].key,
                                       request->headers[i].value) != ESP_OK) {
            *out_err = ESP_FAIL;
            snprintf(error_text, error_text_size, "failed to set request header: %s",
                     request->headers[i].key);
            esp_http_client_cleanup(client);
            http_free_response(response);
            return NULL;
        }
    }

    if (request->body != NULL) {
        if (esp_http_client_set_post_field(client,
                                           request->body,
                                           (int)strlen(request->body)) != ESP_OK) {
            *out_err = ESP_FAIL;
            snprintf(error_text, error_text_size, "failed to set request body");
            esp_http_client_cleanup(client);
            http_free_response(response);
            return NULL;
        }
    }

    *out_err = esp_http_client_perform(client);
    if (*out_err != ESP_OK) {
        snprintf(error_text, error_text_size, "esp_http_client_perform() failed: %s",
                 esp_err_to_name(*out_err));
        esp_http_client_cleanup(client);
        http_free_response(response);
        return NULL;
    }
    if (capture.error != ESP_OK) {
        *out_err = capture.error;
        snprintf(error_text, error_text_size, "failed to capture HTTP response: %s",
                 esp_err_to_name(capture.error));
        esp_http_client_cleanup(client);
        http_free_response(response);
        return NULL;
    }

    response->status = esp_http_client_get_status_code(client);
    response->ok = response->status >= 200 && response->status < 300;
    response->content_length = esp_http_client_get_content_length(client);

    heap_caps_free(response->status_text);
    response->status_text = http_strdup(http_status_text(response->status));
    if (response->status_text == NULL) {
        *out_err = ESP_ERR_NO_MEM;
        snprintf(error_text, error_text_size, "out of memory while storing status text");
        esp_http_client_cleanup(client);
        http_free_response(response);
        return NULL;
    }

    if (esp_http_client_get_url(client, resolved_url, sizeof(resolved_url)) == ESP_OK) {
        response->url = http_strdup(resolved_url);
        if (response->url == NULL) {
            *out_err = ESP_ERR_NO_MEM;
            snprintf(error_text, error_text_size, "out of memory while storing response url");
            esp_http_client_cleanup(client);
            http_free_response(response);
            return NULL;
        }
    }
    if (response->url == NULL) {
        response->url = http_strdup(request->url);
        if (response->url == NULL) {
            *out_err = ESP_ERR_NO_MEM;
            snprintf(error_text, error_text_size, "out of memory while storing response url");
            esp_http_client_cleanup(client);
            http_free_response(response);
            return NULL;
        }
    }

    esp_http_client_cleanup(client);
    return response;
}

static JSValue http_make_headers_object(JSContext *ctx,
                                        const esp32_mquickjs_http_response_t *response)
{
    JSGCRef headers_ref;
    JSValue *headers_obj;
    size_t i;

    headers_obj = JS_PushGCRef(ctx, &headers_ref);
    *headers_obj = JS_NewObject(ctx);
    if (JS_IsException(*headers_obj)) {
        goto fail;
    }

    if (response != NULL) {
        for (i = 0; i < response->header_count; ++i) {
            if (!esp32_mquickjs_set_property(ctx,
                                             *headers_obj,
                                             response->headers[i].key,
                                             JS_NewString(ctx, response->headers[i].value))) {
                goto fail;
            }
        }
    }

    return JS_PopGCRef(ctx, &headers_ref);

fail:
    JS_PopGCRef(ctx, &headers_ref);
    return JS_EXCEPTION;
}

static JSValue http_make_response_object(JSContext *ctx,
                                         const esp32_mquickjs_http_response_t *response)
{
    JSGCRef response_ref;
    JSValue *response_obj;
    JSValue headers_obj;

    response_obj = JS_PushGCRef(ctx, &response_ref);
    *response_obj = JS_NewObject(ctx);
    if (JS_IsException(*response_obj)) {
        goto fail;
    }

    headers_obj = http_make_headers_object(ctx, response);
    if (JS_IsException(headers_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *response_obj, "ok",
                                     JS_NewBool(response->ok)) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "status",
                                     JS_NewInt32(ctx, response->status)) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "statusText",
                                     JS_NewString(ctx, response->status_text != NULL ? response->status_text : "")) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "url",
                                     JS_NewString(ctx, response->url != NULL ? response->url : "")) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "body",
                                     JS_NewString(ctx, response->body != NULL ? response->body : "")) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "headers", headers_obj) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "contentLength",
                                     JS_NewInt64(ctx, response->content_length)) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "truncated",
                                     JS_NewBool(response->truncated))) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &response_ref);

fail:
    JS_PopGCRef(ctx, &response_ref);
    return JS_EXCEPTION;
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

static int http_parse_headers(JSContext *ctx,
                              JSValue headers_value,
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

    if (JS_IsUndefined(headers_value) || JS_IsNull(headers_value)) {
        *out_headers = NULL;
        *out_header_count = 0;
        return 0;
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

    *keys_array = http_call_function(ctx, *keys_fn, *object_ctor, 1, &headers_value);
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

        *value_value = JS_GetPropertyStr(ctx, headers_value, key);
        if (JS_IsException(*value_value)) {
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }

        value = JS_ToCString(ctx, *value_value, &value_buf);
        if (value == NULL) {
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }

        headers[header_count].key = http_strdup(key);
        headers[header_count].value = http_strdup(value);
        if (headers[header_count].key == NULL || headers[header_count].value == NULL) {
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }

        header_count++;
        JS_PopGCRef(ctx, &value_ref);
        JS_PopGCRef(ctx, &key_ref);
    }

    *out_headers = headers;
    *out_header_count = (size_t)header_count;
    headers = NULL;
    result = 0;

done:
    http_free_headers(headers, (size_t)header_count);
    JS_PopGCRef(ctx, &length_ref);
    JS_PopGCRef(ctx, &keys_array_ref);
    JS_PopGCRef(ctx, &keys_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

static int http_parse_options(JSContext *ctx,
                              JSValue options_value,
                              esp32_mquickjs_http_request_t *request)
{
    JSGCRef method_ref;
    JSGCRef body_ref;
    JSGCRef timeout_ref;
    JSGCRef headers_ref;
    JSValue *method_value;
    JSValue *body_value;
    JSValue *timeout_value;
    JSValue *headers_value;
    JSCStringBuf method_buf;
    JSCStringBuf body_buf;
    const char *method;
    const char *body;

    if (JS_IsUndefined(options_value) || JS_IsNull(options_value)) {
        return 0;
    }
    if (JS_IsString(ctx, options_value) || JS_IsBool(options_value) || JS_IsFunction(ctx, options_value)) {
        JS_ThrowTypeError(ctx, "fetch(url, options) expects options to be an object");
        return -1;
    }

    method_value = JS_PushGCRef(ctx, &method_ref);
    body_value = JS_PushGCRef(ctx, &body_ref);
    timeout_value = JS_PushGCRef(ctx, &timeout_ref);
    headers_value = JS_PushGCRef(ctx, &headers_ref);
    *method_value = JS_GetPropertyStr(ctx, options_value, "method");
    *body_value = JS_GetPropertyStr(ctx, options_value, "body");
    *timeout_value = JS_GetPropertyStr(ctx, options_value, "timeoutMs");
    *headers_value = JS_GetPropertyStr(ctx, options_value, "headers");

    if (JS_IsException(*method_value) || JS_IsException(*body_value) ||
        JS_IsException(*timeout_value) || JS_IsException(*headers_value)) {
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
        request->method = http_strdup(method);
        if (request->method == NULL) {
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
    }

    if (!JS_IsUndefined(*body_value) && !JS_IsNull(*body_value)) {
        if (!JS_IsString(ctx, *body_value)) {
            JS_ThrowTypeError(ctx, "fetch(url, options.body) expects a string");
            goto fail;
        }

        body = JS_ToCString(ctx, *body_value, &body_buf);
        if (body == NULL) {
            goto fail;
        }
        request->body = http_strdup(body);
        if (request->body == NULL) {
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
    }

    if (http_parse_timeout(ctx, *timeout_value, &request->timeout_ms) != 0) {
        JS_ThrowTypeError(ctx, "fetch(url, options.timeoutMs) expects a non-negative integer");
        goto fail;
    }

    if (http_parse_headers(ctx, *headers_value, &request->headers, &request->header_count) != 0) {
        goto fail;
    }

    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &timeout_ref);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &method_ref);
    return 0;

fail:
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &timeout_ref);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &method_ref);
    return -1;
}

static JSValue http_fetch_sync(JSContext *ctx,
                               const esp32_mquickjs_http_request_t *request)
{
    esp32_mquickjs_http_response_t *response;
    char error_text[ESP32_MQUICKJS_HTTP_ERROR_TEXT_LEN];
    esp_err_t err;
    JSValue result;

    response = http_perform_request(request, &err, error_text, sizeof(error_text));
    if (response == NULL) {
        return JS_ThrowInternalError(ctx, "%s", error_text);
    }

    result = http_make_response_object(ctx, response);
    http_free_response(response);
    return result;
}

static void http_async_cleanup_slot(JSContext *ctx, esp32_mquickjs_http_async_slot_t *slot)
{
    if (slot == NULL || !slot->allocated) {
        return;
    }

    JS_DeleteGCRef(ctx, &slot->callback);
    http_free_request(&slot->request);
    slot->allocated = false;
}

static void http_worker_task(void *opaque)
{
    esp32_mquickjs_http_worker_args_t *args = opaque;
    esp32_mquickjs_http_async_slot_t *slot;
    esp32_mquickjs_http_async_event_t event = {0};

    if (args == NULL) {
        vTaskDelete(NULL);
        return;
    }

    slot = args->slot;
    event.slot_id = slot->slot_id;
    event.generation = args->generation;
    event.response = http_perform_request(&slot->request,
                                          &event.err,
                                          event.error_text,
                                          sizeof(event.error_text));
    if (event.err == ESP_OK && event.response == NULL) {
        event.err = ESP_FAIL;
        snprintf(event.error_text, sizeof(event.error_text), "fetch worker returned no response");
    }

    heap_caps_free(args);
    xQueueSend(s_http_state.queue, &event, portMAX_DELAY);
    vTaskDelete(NULL);
}

static JSValue http_fetch_async(JSContext *ctx,
                                const esp32_mquickjs_http_request_t *request,
                                JSValue callback)
{
    esp32_mquickjs_http_async_slot_t *slot = NULL;
    esp32_mquickjs_http_worker_args_t *worker_args = NULL;
    JSValue *callback_ref;
    int i;

    if (!http_init_state()) {
        return JS_ThrowOutOfMemory(ctx);
    }
    if (!JS_IsFunction(ctx, callback)) {
        return JS_ThrowTypeError(ctx, "fetch(url, callback) expects a function");
    }

    http_lock();
    for (i = 0; i < ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS; ++i) {
        if (!s_http_state.slots[i].allocated) {
            slot = &s_http_state.slots[i];
            slot->allocated = true;
            slot->generation++;
            break;
        }
    }
    http_unlock();

    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "too many asynchronous fetch requests");
    }

    callback_ref = JS_AddGCRef(ctx, &slot->callback);
    *callback_ref = callback;
    slot->request = *request;
    worker_args = heap_caps_calloc(1, sizeof(*worker_args), MALLOC_CAP_8BIT);
    if (worker_args == NULL) {
        http_async_cleanup_slot(ctx, slot);
        return JS_ThrowOutOfMemory(ctx);
    }

    worker_args->slot = slot;
    worker_args->generation = slot->generation;
    if (xTaskCreate(http_worker_task,
                    "http_fetch",
                    ESP32_MQUICKJS_HTTP_TASK_STACK_SIZE,
                    worker_args,
                    tskIDLE_PRIORITY + 4,
                    NULL) != pdPASS) {
        heap_caps_free(worker_args);
        http_async_cleanup_slot(ctx, slot);
        return JS_ThrowInternalError(ctx, "failed to start fetch worker task");
    }

    return JS_UNDEFINED;
}

bool esp32_mquickjs_install_http_module(JSContext *ctx, JSValue global_obj)
{
    JSGCRef module_ref;
    JSValue *module_obj;

    module_obj = JS_PushGCRef(ctx, &module_ref);
    *module_obj = JS_NewObject(ctx);
    if (JS_IsException(*module_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *module_obj, "DEFAULT_TIMEOUT_MS",
                                     JS_NewUint32(ctx, ESP32_MQUICKJS_HTTP_DEFAULT_TIMEOUT_MS)) ||
        !esp32_mquickjs_set_property(ctx, *module_obj, "MAX_RESPONSE_BYTES",
                                     JS_NewUint32(ctx, ESP32_MQUICKJS_HTTP_MAX_RESPONSE_BYTES)) ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "fetch", "http.fetch")) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, global_obj, "http", JS_PopGCRef(ctx, &module_ref))) {
        return false;
    }
    return true;

fail:
    JS_PopGCRef(ctx, &module_ref);
    return false;
}

bool esp32_mquickjs_dispatch_http(JSContext *ctx,
                                  const char *operation,
                                  int argc,
                                  JSValue *argv,
                                  JSValue *result)
{
    esp32_mquickjs_http_request_t request = {0};
    JSCStringBuf url_buf;
    const char *url;
    JSValue options = JS_UNDEFINED;
    JSValue callback = JS_UNDEFINED;
    bool is_async = false;

    if (strcmp(operation, "fetch") != 0) {
        return false;
    }

    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        *result = JS_ThrowTypeError(ctx,
                                    "fetch(url, options?, callback?) expects a URL string");
        return true;
    }

    url = JS_ToCString(ctx, argv[0], &url_buf);
    if (url == NULL) {
        *result = JS_EXCEPTION;
        return true;
    }

    request.url = http_strdup(url);
    request.method = http_strdup("GET");
    request.timeout_ms = ESP32_MQUICKJS_HTTP_DEFAULT_TIMEOUT_MS;
    if (request.url == NULL || request.method == NULL) {
        http_free_request(&request);
        *result = JS_ThrowOutOfMemory(ctx);
        return true;
    }

    if (argc >= 2) {
        if (JS_IsFunction(ctx, argv[1])) {
            callback = argv[1];
            is_async = true;
        } else {
            options = argv[1];
        }
    }
    if (argc >= 3) {
        if (!JS_IsFunction(ctx, argv[2])) {
            http_free_request(&request);
            *result = JS_ThrowTypeError(ctx, "fetch(url, options, callback) expects a callback function");
            return true;
        }
        callback = argv[2];
        is_async = true;
    }

    if (http_parse_options(ctx, options, &request) != 0) {
        http_free_request(&request);
        *result = JS_EXCEPTION;
        return true;
    }

    if (is_async) {
        *result = http_fetch_async(ctx, &request, callback);
        if (!JS_IsException(*result)) {
            memset(&request, 0, sizeof(request));
        }
        http_free_request(&request);
        return true;
    }

    *result = http_fetch_sync(ctx, &request);
    http_free_request(&request);
    return true;
}

bool esp32_mquickjs_poll_http(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_http_async_event_t event;
    bool needs_redraw = false;

    (void)runtime;
    if (ctx == NULL || s_http_state.queue == NULL) {
        return false;
    }

    while (xQueueReceive(s_http_state.queue, &event, 0) == pdTRUE) {
        esp32_mquickjs_http_async_slot_t *slot;
        JSGCRef callback_ref;
        JSValue *callback_fn;
        JSValue argv[2];
        JSValue callback_result;

        if (event.slot_id >= ESP32_MQUICKJS_HTTP_MAX_ASYNC_REQUESTS) {
            http_free_response(event.response);
            continue;
        }

        slot = &s_http_state.slots[event.slot_id];
        if (!slot->allocated || slot->generation != event.generation) {
            http_free_response(event.response);
            continue;
        }

        callback_fn = JS_PushGCRef(ctx, &callback_ref);
        *callback_fn = slot->callback.val;
        http_async_cleanup_slot(ctx, slot);

        if (event.err != ESP_OK) {
            argv[0] = JS_NewString(ctx, event.error_text[0] != '\0' ? event.error_text : esp_err_to_name(event.err));
            argv[1] = JS_NULL;
        } else {
            argv[0] = JS_NULL;
            argv[1] = http_make_response_object(ctx, event.response);
            if (JS_IsException(argv[1])) {
                JS_PopGCRef(ctx, &callback_ref);
                http_free_response(event.response);
                return true;
            }
        }

        callback_result = http_call_function(ctx, *callback_fn, JS_NULL, 2, argv);
        if (JS_IsException(callback_result)) {
            esp32_mquickjs_print_exception(ctx);
            needs_redraw = true;
        }

        JS_PopGCRef(ctx, &callback_ref);
        http_free_response(event.response);
    }

    return needs_redraw;
}
