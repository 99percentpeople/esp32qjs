#include "esp32_mquickjs_http_server.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_request_response.h"
#include "esp32_mquickjs_stream.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define ESP32_MQUICKJS_HTTP_SERVER_MAX_SERVERS 2
#define ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES 16
#define ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN 8
#define ESP32_MQUICKJS_HTTP_SERVER_MAX_BODY_LEN 8192
#define ESP32_MQUICKJS_HTTP_SERVER_MAX_HEADERS 16
#define ESP32_MQUICKJS_HTTP_SERVER_ERROR_TEXT_LEN 160
#define ESP32_MQUICKJS_HTTP_SERVER_STREAM_CHUNK_SIZE 1024

typedef struct {
    char *key;
    char *value;
} esp32_mquickjs_http_server_header_t;

typedef struct {
    bool allocated;
    uint8_t server_id;
    uint16_t port;
    uint16_t ctrl_port;
    char *host;
    bool started;
    httpd_handle_t handle;
    httpd_uri_t dispatch_uri;
} esp32_mquickjs_http_server_slot_t;

typedef struct {
    bool allocated;
    uint8_t route_id;
    uint8_t server_id;
    httpd_method_t method;
    char *uri;
    char *mount_path;
    bool match_with_regex;
    bool pattern_ref_added;
    bool callback_ref_added;
    bool callback_this_ref_added;
    JSGCRef pattern;
    JSGCRef callback;
    JSGCRef callback_this;
    bool registered;
    httpd_uri_t uri_def;
} esp32_mquickjs_http_server_route_t;

typedef struct {
    bool allocated;
    uint8_t request_id;
    uint32_t generation;
    uint8_t server_id;
    httpd_req_t *async_req;
    char *method;
    char *path;
    char *query_string;
    char *body;
    esp32_mquickjs_http_server_header_t *headers;
    size_t header_count;
} esp32_mquickjs_http_server_request_t;

typedef struct {
    uint8_t request_id;
    uint32_t generation;
} esp32_mquickjs_http_server_event_t;

typedef struct {
    bool initialized;
    esp32_mquickjs_runtime_t *runtime;
    QueueHandle_t queue;
    SemaphoreHandle_t lock;
    esp32_mquickjs_http_server_slot_t servers[ESP32_MQUICKJS_HTTP_SERVER_MAX_SERVERS];
    esp32_mquickjs_http_server_route_t routes[ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES];
    esp32_mquickjs_http_server_request_t requests[ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN];
} esp32_mquickjs_http_server_state_t;

typedef struct {
    int32_t status;
    char *status_text;
    bool has_body_stream;
    esp32_mquickjs_fs_stream_ref_t body_stream_ref;
    esp32_mquickjs_http_server_header_t *headers;
    size_t header_count;
} esp32_mquickjs_http_server_response_t;

static const char *TAG = "esp32qjs_httpd";
static esp32_mquickjs_http_server_state_t s_http_server_state;

static bool http_server_async_poller(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime,
                                     void *opaque);
static bool http_server_glob_match(const char *pattern, const char *text);

static httpd_method_t http_server_method_from_name(const char *method_name)
{
    if (method_name == NULL) {
        return HTTP_ANY;
    }
    if (strcmp(method_name, "GET") == 0) {
        return HTTP_GET;
    }
    if (strcmp(method_name, "POST") == 0) {
        return HTTP_POST;
    }
    if (strcmp(method_name, "PUT") == 0) {
        return HTTP_PUT;
    }
    if (strcmp(method_name, "PATCH") == 0) {
        return HTTP_PATCH;
    }
    if (strcmp(method_name, "DELETE") == 0) {
        return HTTP_DELETE;
    }
    if (strcmp(method_name, "HEAD") == 0) {
        return HTTP_HEAD;
    }
    if (strcmp(method_name, "OPTIONS") == 0) {
        return HTTP_OPTIONS;
    }
    return HTTP_ANY;
}

static JSValue http_server_call_function(JSContext *ctx,
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

static bool http_server_is_object(JSContext *ctx, JSValue value)
{
    (void)ctx;
    return JS_GetClassID(ctx, value) >= 0;
}

static void http_server_lock(void)
{
    if (s_http_server_state.lock != NULL) {
        xSemaphoreTake(s_http_server_state.lock, portMAX_DELAY);
    }
}

static void http_server_unlock(void)
{
    if (s_http_server_state.lock != NULL) {
        xSemaphoreGive(s_http_server_state.lock);
    }
}

static char *http_server_strdup(const char *value)
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

static void http_server_free_headers(esp32_mquickjs_http_server_header_t *headers, size_t count)
{
    size_t i;

    if (headers == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        heap_caps_free(headers[i].key);
        heap_caps_free(headers[i].value);
    }
    heap_caps_free(headers);
}

static void http_server_free_response(esp32_mquickjs_http_server_response_t *response)
{
    if (response == NULL) {
        return;
    }
    if (response->has_body_stream) {
        esp32_mquickjs_fs_stream_close(&response->body_stream_ref);
    }
    heap_caps_free(response->status_text);
    http_server_free_headers(response->headers, response->header_count);
    memset(response, 0, sizeof(*response));
}

static void http_server_cleanup_request(esp32_mquickjs_http_server_request_t *request)
{
    if (request == NULL || !request->allocated) {
        return;
    }

    if (request->async_req != NULL) {
        httpd_req_async_handler_complete(request->async_req);
    }
    heap_caps_free(request->method);
    heap_caps_free(request->path);
    heap_caps_free(request->query_string);
    heap_caps_free(request->body);
    http_server_free_headers(request->headers, request->header_count);
    memset(request, 0, sizeof(*request));
}

static void http_server_cleanup_route(JSContext *ctx, esp32_mquickjs_http_server_route_t *route)
{
    if (route == NULL || !route->allocated) {
        return;
    }

    if (ctx != NULL) {
        if (route->pattern_ref_added) {
            JS_DeleteGCRef(ctx, &route->pattern);
        }
        if (route->callback_ref_added) {
            JS_DeleteGCRef(ctx, &route->callback);
        }
        if (route->callback_this_ref_added) {
            JS_DeleteGCRef(ctx, &route->callback_this);
        }
    }
    heap_caps_free(route->uri);
    heap_caps_free(route->mount_path);
    memset(route, 0, sizeof(*route));
}

static void http_server_cleanup_server(esp32_mquickjs_http_server_slot_t *server)
{
    if (server == NULL || !server->allocated) {
        return;
    }
    heap_caps_free(server->host);
    memset(server, 0, sizeof(*server));
}

static const char *http_server_status_text(int32_t status)
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
    case 500:
        return "Internal Server Error";
    case 503:
        return "Service Unavailable";
    default:
        return "OK";
    }
}

static bool http_server_host_is_any(const char *host)
{
    return host == NULL || host[0] == '\0' || strcmp(host, "0.0.0.0") == 0;
}

static bool http_server_resolve_ifreq_for_host(const char *host, struct ifreq *out_ifr)
{
    esp_netif_t *netif = NULL;
    uint32_t target_ip = 0;

    if (out_ifr == NULL) {
        return false;
    }
    memset(out_ifr, 0, sizeof(*out_ifr));

    if (http_server_host_is_any(host)) {
        return true;
    }

    target_ip = esp_ip4addr_aton(host);
    if (target_ip == 0) {
        return false;
    }

    while ((netif = esp_netif_next_unsafe(netif)) != NULL) {
        esp_netif_ip_info_t ip_info;

        if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
            continue;
        }
        if (ip_info.ip.addr != target_ip) {
            continue;
        }
        if (esp_netif_get_netif_impl_name(netif, out_ifr->ifr_name) != ESP_OK) {
            return false;
        }
        return out_ifr->ifr_name[0] != '\0';
    }
    return false;
}

static bool http_server_init_state(esp32_mquickjs_runtime_t *runtime)
{
    int i;

    if (s_http_server_state.initialized) {
        if (runtime != NULL) {
            s_http_server_state.runtime = runtime;
        }
        return true;
    }

    memset(&s_http_server_state, 0, sizeof(s_http_server_state));
    s_http_server_state.queue = xQueueCreate(ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN,
                                             sizeof(esp32_mquickjs_http_server_event_t));
    s_http_server_state.lock = xSemaphoreCreateMutex();
    if (s_http_server_state.queue == NULL || s_http_server_state.lock == NULL) {
        return false;
    }

    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_SERVERS; ++i) {
        s_http_server_state.servers[i].server_id = (uint8_t)i;
    }
    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES; ++i) {
        s_http_server_state.routes[i].route_id = (uint8_t)i;
    }
    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN; ++i) {
        s_http_server_state.requests[i].request_id = (uint8_t)i;
    }

    s_http_server_state.runtime = runtime;
    s_http_server_state.initialized = true;
    return true;
}

static esp32_mquickjs_http_server_slot_t *http_server_get_slot(int32_t server_id)
{
    if (server_id < 0 || server_id >= ESP32_MQUICKJS_HTTP_SERVER_MAX_SERVERS) {
        return NULL;
    }
    if (!s_http_server_state.servers[server_id].allocated) {
        return NULL;
    }
    return &s_http_server_state.servers[server_id];
}

static esp32_mquickjs_http_server_request_t *http_server_alloc_request(void)
{
    int i;

    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN; ++i) {
        if (!s_http_server_state.requests[i].allocated) {
            s_http_server_state.requests[i].allocated = true;
            s_http_server_state.requests[i].generation++;
            return &s_http_server_state.requests[i];
        }
    }
    return NULL;
}

static bool http_server_parse_path_and_query(const char *uri, char **out_path, char **out_query)
{
    const char *query;
    size_t path_len;
    char *path;

    *out_path = NULL;
    *out_query = NULL;

    if (uri == NULL) {
        return false;
    }

    query = strchr(uri, '?');
    if (query == NULL) {
        *out_path = http_server_strdup(uri);
        return *out_path != NULL;
    }

    path_len = (size_t)(query - uri);
    path = heap_caps_malloc(path_len + 1, MALLOC_CAP_8BIT);
    if (path == NULL) {
        return false;
    }
    memcpy(path, uri, path_len);
    path[path_len] = '\0';

    *out_path = path;
    *out_query = http_server_strdup(query + 1);
    if (*out_query == NULL) {
        heap_caps_free(path);
        *out_path = NULL;
        return false;
    }
    return true;
}

static int http_server_hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static char *http_server_url_decode(const char *value)
{
    size_t len;
    char *out;
    size_t i;
    size_t j = 0;

    if (value == NULL) {
        return http_server_strdup("");
    }

    len = strlen(value);
    out = heap_caps_malloc(len + 1, MALLOC_CAP_8BIT);
    if (out == NULL) {
        return NULL;
    }

    for (i = 0; i < len; ++i) {
        if (value[i] == '%' && i + 2 < len) {
            int hi = http_server_hex_value(value[i + 1]);
            int lo = http_server_hex_value(value[i + 2]);

            if (hi >= 0 && lo >= 0) {
                out[j++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        if (value[i] == '+') {
            out[j++] = ' ';
        } else {
            out[j++] = value[i];
        }
    }
    out[j] = '\0';
    return out;
}

static bool http_server_capture_headers(httpd_req_t *req,
                                        esp32_mquickjs_http_server_header_t **out_headers,
                                        size_t *out_count)
{
    static const char *fields[] = {
        "host",
        "user-agent",
        "accept",
        "accept-encoding",
        "content-type",
        "content-length",
        "authorization",
        "cookie",
        "connection",
        "origin",
        "referer",
    };
    esp32_mquickjs_http_server_header_t *headers = NULL;
    size_t count = 0;
    size_t i;

    *out_headers = NULL;
    *out_count = 0;
    headers = heap_caps_calloc(sizeof(fields) / sizeof(fields[0]), sizeof(*headers), MALLOC_CAP_8BIT);
    if (headers == NULL) {
        return false;
    }

    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        size_t len = httpd_req_get_hdr_value_len(req, fields[i]);
        char *value;

        if (len == 0) {
            continue;
        }
        value = heap_caps_malloc(len + 1, MALLOC_CAP_8BIT);
        if (value == NULL) {
            http_server_free_headers(headers, count);
            return false;
        }
        if (httpd_req_get_hdr_value_str(req, fields[i], value, len + 1) != ESP_OK) {
            heap_caps_free(value);
            continue;
        }
        headers[count].key = http_server_strdup(fields[i]);
        headers[count].value = value;
        if (headers[count].key == NULL) {
            heap_caps_free(value);
            http_server_free_headers(headers, count);
            return false;
        }
        count++;
    }

    *out_headers = headers;
    *out_count = count;
    return true;
}

static bool http_server_compile_route_pattern(JSContext *ctx,
                                              JSValue pattern_value,
                                              esp32_mquickjs_http_server_route_t *route)
{
    JSCStringBuf uri_buf;
    const char *uri = NULL;

    if (route == NULL) {
        return false;
    }

    if (JS_IsString(ctx, pattern_value)) {
        uri = JS_ToCString(ctx, pattern_value, &uri_buf);
        if (uri == NULL) {
            return false;
        }
        route->uri = http_server_strdup(uri);
        if (route->uri == NULL) {
            return false;
        }
        if (strchr(uri, '*') == NULL) {
            route->match_with_regex = false;
            return true;
        }
        {
            const char *wildcard = strchr(uri, '*');
            size_t prefix_len = (size_t)(wildcard - uri);

            route->mount_path = heap_caps_malloc(prefix_len + 1, MALLOC_CAP_8BIT);
            if (route->mount_path == NULL) {
                return false;
            }
            memcpy(route->mount_path, uri, prefix_len);
            route->mount_path[prefix_len] = '\0';
        }
        route->match_with_regex = true;
        return true;
    }

    if (http_server_is_object(ctx, pattern_value)) {
        JSGCRef source_ref;
        JSGCRef flags_ref;
        JSValue *source_value;
        JSValue *flags_value;
        JSCStringBuf source_buf;
        JSCStringBuf flags_buf;
        const char *source;
        const char *flags;
        size_t source_len;
        size_t flags_len;
        char *route_text;

        source_value = JS_PushGCRef(ctx, &source_ref);
        flags_value = JS_PushGCRef(ctx, &flags_ref);
        *source_value = JS_GetPropertyStr(ctx, pattern_value, "source");
        *flags_value = JS_GetPropertyStr(ctx, pattern_value, "flags");
        if (JS_IsException(*source_value) || JS_IsException(*flags_value)) {
            JS_PopGCRef(ctx, &flags_ref);
            JS_PopGCRef(ctx, &source_ref);
            return false;
        }

        source = JS_ToCString(ctx, *source_value, &source_buf);
        flags = JS_ToCString(ctx, *flags_value, &flags_buf);
        if (source != NULL && flags != NULL) {
            source_len = strlen(source);
            flags_len = strlen(flags);
            route_text = heap_caps_malloc(source_len + flags_len + 3, MALLOC_CAP_8BIT);
            if (route_text == NULL) {
                JS_PopGCRef(ctx, &flags_ref);
                JS_PopGCRef(ctx, &source_ref);
                return false;
            }
            route_text[0] = '/';
            memcpy(route_text + 1, source, source_len);
            route_text[source_len + 1] = '/';
            memcpy(route_text + source_len + 2, flags, flags_len);
            route_text[source_len + flags_len + 2] = '\0';
            route->uri = route_text;
        } else {
            uri = JS_ToCString(ctx, pattern_value, &uri_buf);
            route->uri = http_server_strdup(uri != NULL ? uri : "<regexp>");
        }
        JS_PopGCRef(ctx, &flags_ref);
        JS_PopGCRef(ctx, &source_ref);
        *JS_AddGCRef(ctx, &route->pattern) = pattern_value;
        route->pattern_ref_added = true;
        route->match_with_regex = true;
        return route->uri != NULL;
    }

    return false;
}

static bool http_server_regex_test(JSContext *ctx, JSValue pattern, const char *path)
{
    JSGCRef test_ref;
    JSGCRef path_ref;
    JSValue *test_fn;
    JSValue *path_value;
    JSValue argv[1];
    JSValue result;
    JSValue reset_index;
    int matched = 0;

    if (JS_IsException(pattern)) {
        return false;
    }

    test_fn = JS_PushGCRef(ctx, &test_ref);
    path_value = JS_PushGCRef(ctx, &path_ref);
    *test_fn = JS_GetPropertyStr(ctx, pattern, "test");
    *path_value = JS_NewString(ctx, path != NULL ? path : "");
    if (JS_IsException(*test_fn) || !JS_IsFunction(ctx, *test_fn) || JS_IsException(*path_value)) {
        JS_PopGCRef(ctx, &path_ref);
        JS_PopGCRef(ctx, &test_ref);
        return false;
    }
    reset_index = JS_NewInt32(ctx, 0);
    if (!JS_IsException(reset_index)) {
        JS_SetPropertyStr(ctx, pattern, "lastIndex", reset_index);
    }
    argv[0] = *path_value;
    result = http_server_call_function(ctx, *test_fn, pattern, 1, argv);
    if (!JS_IsException(result) && JS_ToInt32(ctx, &matched, result) == 0) {
        JS_PopGCRef(ctx, &path_ref);
        JS_PopGCRef(ctx, &test_ref);
        return matched != 0;
    }
    JS_PopGCRef(ctx, &path_ref);
    JS_PopGCRef(ctx, &test_ref);
    return false;
}

static bool http_server_glob_match(const char *pattern, const char *text)
{
    const char *star = NULL;
    const char *retry = NULL;

    if (pattern == NULL || text == NULL) {
        return false;
    }

    while (*text != '\0') {
        if (*pattern == '*') {
            star = pattern++;
            retry = text;
            continue;
        }
        if (*pattern == *text) {
            pattern++;
            text++;
            continue;
        }
        if (star != NULL) {
            pattern = star + 1;
            text = ++retry;
            continue;
        }
        return false;
    }

    while (*pattern == '*') {
        pattern++;
    }
    return *pattern == '\0';
}

static char *http_server_recv_body(httpd_req_t *req, size_t max_len, esp_err_t *out_err)
{
    size_t remaining;
    size_t offset = 0;
    char *body;

    *out_err = ESP_OK;
    if (req->content_len <= 0) {
        return http_server_strdup("");
    }
    if (req->content_len > (int)max_len) {
        *out_err = ESP_ERR_HTTPD_RESP_SEND;
        return NULL;
    }

    body = heap_caps_malloc((size_t)req->content_len + 1, MALLOC_CAP_8BIT);
    if (body == NULL) {
        *out_err = ESP_ERR_NO_MEM;
        return NULL;
    }

    remaining = (size_t)req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, body + offset, remaining);

        if (received <= 0) {
            heap_caps_free(body);
            *out_err = received == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
            return NULL;
        }
        offset += (size_t)received;
        remaining -= (size_t)received;
    }

    body[offset] = '\0';
    return body;
}

static esp_err_t http_server_send_error(httpd_req_t *req, int32_t status, const char *message)
{
    char status_line[32];
    const char *reason = http_server_status_text(status);

    snprintf(status_line, sizeof(status_line), "%" PRId32 " %s", status, reason);
    httpd_resp_set_status(req, status_line);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, message != NULL ? message : "");
}

static esp32_mquickjs_http_server_route_t *http_server_find_route(JSContext *ctx,
                                                                  uint8_t server_id,
                                                                  const char *method_name,
                                                                  const char *path)
{
    int i;
    httpd_method_t method = http_server_method_from_name(method_name);

    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES; ++i) {
        esp32_mquickjs_http_server_route_t *route = &s_http_server_state.routes[i];

        if (!route->allocated || route->server_id != server_id || route->uri == NULL) {
            continue;
        }
        if (route->method != HTTP_ANY && route->method != method) {
            continue;
        }
        if (route->match_with_regex) {
            if ((route->pattern_ref_added && http_server_regex_test(ctx, route->pattern.val, path)) ||
                (!route->pattern_ref_added && http_server_glob_match(route->uri, path != NULL ? path : ""))) {
                return route;
            }
        } else if (strcmp(route->uri, path) == 0) {
            return route;
        }
    }
    return NULL;
}

static esp_err_t http_server_dispatch_handler(httpd_req_t *req)
{
    esp32_mquickjs_http_server_slot_t *server = req != NULL ? req->user_ctx : NULL;
    esp32_mquickjs_http_server_request_t *request;
    esp32_mquickjs_http_server_event_t event;
    httpd_req_t *async_req = NULL;
    esp_err_t err = ESP_OK;
    char *path = NULL;
    char *query_string = NULL;

    if (server == NULL || !server->allocated) {
        return ESP_FAIL;
    }
    if (!http_server_parse_path_and_query(req->uri, &path, &query_string)) {
        http_server_send_error(req, 500, "out of memory");
        return ESP_FAIL;
    }

    request = http_server_alloc_request();
    if (request == NULL) {
        heap_caps_free(path);
        heap_caps_free(query_string);
        http_server_send_error(req, 503, "server busy");
        return ESP_FAIL;
    }

    err = httpd_req_async_handler_begin(req, &async_req);
    if (err != ESP_OK || async_req == NULL) {
        http_server_cleanup_request(request);
        heap_caps_free(path);
        heap_caps_free(query_string);
        http_server_send_error(req, 500, "failed to start async request");
        return ESP_FAIL;
    }

    request->server_id = server->server_id;
    request->async_req = async_req;
    request->path = path;
    request->query_string = query_string;
    request->method = http_server_strdup(req->method == HTTP_POST ? "POST" :
                                         req->method == HTTP_PUT ? "PUT" :
                                         req->method == HTTP_DELETE ? "DELETE" :
                                         req->method == HTTP_PATCH ? "PATCH" :
                                         req->method == HTTP_HEAD ? "HEAD" :
                                         req->method == HTTP_OPTIONS ? "OPTIONS" : "GET");
    if (request->method == NULL) {
        http_server_send_error(async_req, 500, "out of memory");
        http_server_cleanup_request(request);
        return ESP_OK;
    }
    if (!http_server_capture_headers(async_req, &request->headers, &request->header_count)) {
        http_server_send_error(async_req, 500, "failed to capture request headers");
        http_server_cleanup_request(request);
        return ESP_OK;
    }

    request->body = http_server_recv_body(async_req, ESP32_MQUICKJS_HTTP_SERVER_MAX_BODY_LEN, &err);
    if (request->body == NULL) {
        if (err == ESP_ERR_HTTPD_RESP_SEND) {
            http_server_send_error(async_req, 413, "request body too large");
        } else {
            http_server_send_error(async_req, 500, "failed to read request body");
        }
        http_server_cleanup_request(request);
        return ESP_OK;
    }

    event.request_id = request->request_id;
    event.generation = request->generation;
    if (xQueueSend(s_http_server_state.queue, &event, 0) != pdTRUE) {
        http_server_send_error(async_req, 503, "request queue full");
        http_server_cleanup_request(request);
        return ESP_OK;
    }

    if (s_http_server_state.runtime != NULL) {
        esp32_mquickjs_notify_activity(s_http_server_state.runtime);
    }
    return ESP_OK;
}

static bool http_server_make_request_object(JSContext *ctx,
                                            const esp32_mquickjs_http_server_request_t *request,
                                            const esp32_mquickjs_http_server_route_t *route,
                                            JSValue *out_obj)
{
    JSGCRef global_ref;
    JSGCRef query_ref;
    JSGCRef headers_ref;
    JSValue *global_obj;
    JSValue *query_obj;
    JSValue *headers_plain;
    JSValue headers_value = JS_UNDEFINED;
    char *url = NULL;
    const char *route_text;
    const char *mount_path;
    const char *relative_path;
    size_t i;

    if (request == NULL || out_obj == NULL) {
        return false;
    }

    global_obj = JS_PushGCRef(ctx, &global_ref);
    query_obj = JS_PushGCRef(ctx, &query_ref);
    headers_plain = JS_PushGCRef(ctx, &headers_ref);
    *global_obj = JS_GetGlobalObject(ctx);
    *query_obj = JS_NewObject(ctx);
    *headers_plain = JS_NewObject(ctx);
    if (JS_IsException(*global_obj) || JS_IsException(*query_obj) || JS_IsException(*headers_plain)) {
        goto fail;
    }

    for (i = 0; i < request->header_count; ++i) {
        if (!esp32_mquickjs_set_property(ctx,
                                         *headers_plain,
                                         request->headers[i].key,
                                         JS_NewString(ctx, request->headers[i].value))) {
            goto fail;
        }
    }

    if (request->query_string != NULL && request->query_string[0] != '\0') {
        const char *cursor = request->query_string;

        while (*cursor != '\0') {
            const char *segment_start = cursor;
            const char *separator;
            const char *equals;
            size_t key_len;
            size_t value_len;
            char *key_raw;
            char *value_raw;
            char *key;
            char *value;

            while (*cursor != '\0' && *cursor != '&') {
                cursor++;
            }
            separator = cursor;
            if (*cursor == '&') {
                cursor++;
            }

            equals = memchr(segment_start, '=', (size_t)(separator - segment_start));
            key_len = equals != NULL ? (size_t)(equals - segment_start) : (size_t)(separator - segment_start);
            value_len = equals != NULL ? (size_t)(separator - equals - 1) : 0;

            key_raw = heap_caps_malloc(key_len + 1, MALLOC_CAP_8BIT);
            value_raw = heap_caps_malloc(value_len + 1, MALLOC_CAP_8BIT);
            if (key_raw == NULL || value_raw == NULL) {
                heap_caps_free(key_raw);
                heap_caps_free(value_raw);
                goto fail;
            }
            memcpy(key_raw, segment_start, key_len);
            key_raw[key_len] = '\0';
            memcpy(value_raw, equals != NULL ? equals + 1 : "", value_len);
            value_raw[value_len] = '\0';
            key = http_server_url_decode(key_raw);
            value = http_server_url_decode(value_raw);
            heap_caps_free(key_raw);
            heap_caps_free(value_raw);
            if (key == NULL || value == NULL) {
                heap_caps_free(key);
                heap_caps_free(value);
                goto fail;
            }
            if (key[0] != '\0' &&
                !esp32_mquickjs_set_property(ctx, *query_obj, key, JS_NewString(ctx, value))) {
                heap_caps_free(key);
                heap_caps_free(value);
                goto fail;
            }
            heap_caps_free(key);
            heap_caps_free(value);
        }
    }

    if (request->header_count > 0 && request->headers[0].value != NULL) {
        const char *host = NULL;
        size_t url_len;

        for (i = 0; i < request->header_count; ++i) {
            if (strcasecmp(request->headers[i].key, "host") == 0) {
                host = request->headers[i].value;
                break;
            }
        }
        if (host != NULL) {
            url_len = strlen(host) + strlen(request->path != NULL ? request->path : "/") +
                      strlen(request->query_string != NULL ? request->query_string : "") + 9;
            url = heap_caps_malloc(url_len, MALLOC_CAP_8BIT);
            if (url == NULL) {
                goto fail;
            }
            snprintf(url, url_len, "http://%s%s%s%s",
                     host,
                     request->path != NULL ? request->path : "/",
                     (request->query_string != NULL && request->query_string[0] != '\0') ? "?" : "",
                     request->query_string != NULL ? request->query_string : "");
        }
    }

    headers_value = esp32_mquickjs_make_headers_object(ctx, *global_obj, *headers_plain);
    if (JS_IsException(headers_value)) {
        goto fail;
    }

    route_text = route != NULL && route->uri != NULL ? route->uri :
        (request->path != NULL ? request->path : "/");
    mount_path = route != NULL && route->mount_path != NULL ? route->mount_path : "";
    relative_path = "";
    if (request->path != NULL && route != NULL && route->mount_path != NULL) {
        size_t mount_len = strlen(route->mount_path);
        if (strncmp(request->path, route->mount_path, mount_len) == 0) {
            relative_path = request->path + mount_len;
            while (*relative_path == '/') {
                relative_path++;
            }
        }
    }

    *out_obj = esp32_mquickjs_make_request_object(ctx,
                                                  *global_obj,
                                                  request->method != NULL ? request->method : "GET",
                                                  url != NULL ? url : (request->path != NULL ? request->path : "/"),
                                                  request->path != NULL ? request->path : "/",
                                                  route_text,
                                                  mount_path,
                                                  relative_path,
                                                  request->query_string != NULL ? request->query_string : "",
                                                  *query_obj,
                                                  headers_value,
                                                  JS_NewString(ctx, request->body != NULL ? request->body : ""));
    heap_caps_free(url);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &query_ref);
    JS_PopGCRef(ctx, &global_ref);
    return !JS_IsException(*out_obj);

fail:
    heap_caps_free(url);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &query_ref);
    JS_PopGCRef(ctx, &global_ref);
    *out_obj = JS_EXCEPTION;
    return false;
}

static int http_server_parse_status(JSContext *ctx, JSValue value, int32_t *out_status)
{
    int status = 0;

    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        *out_status = 200;
        return 0;
    }
    if (JS_ToInt32(ctx, &status, value) != 0 || status < 100 || status > 599) {
        return -1;
    }
    *out_status = status;
    return 0;
}

static int http_server_parse_headers(JSContext *ctx,
                                     JSValue value,
                                     esp32_mquickjs_http_server_header_t **out_headers,
                                     size_t *out_count)
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
    esp32_mquickjs_http_server_header_t *headers = NULL;
    int length = 0;
    int i;

    *out_headers = NULL;
    *out_count = 0;
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        return 0;
    }
    if (esp32_mquickjs_is_headers_object(ctx, value)) {
        value = esp32_mquickjs_headers_to_plain_object(ctx, value);
        if (JS_IsException(value)) {
            return -1;
        }
    }
    if (!http_server_is_object(ctx, value)) {
        return -1;
    }

    global_obj = JS_PushGCRef(ctx, &global_ref);
    object_ctor = JS_PushGCRef(ctx, &object_ref);
    keys_fn = JS_PushGCRef(ctx, &keys_ref);
    keys_array = JS_PushGCRef(ctx, &keys_array_ref);
    length_value = JS_PushGCRef(ctx, &length_ref);

    *global_obj = JS_GetGlobalObject(ctx);
    if (JS_IsException(*global_obj)) {
        goto fail;
    }
    *object_ctor = JS_GetPropertyStr(ctx, *global_obj, "Object");
    if (JS_IsException(*object_ctor) || !JS_IsFunction(ctx, *object_ctor)) {
        goto fail;
    }
    *keys_fn = JS_GetPropertyStr(ctx, *object_ctor, "keys");
    if (JS_IsException(*keys_fn) || !JS_IsFunction(ctx, *keys_fn)) {
        goto fail;
    }
    *keys_array = http_server_call_function(ctx, *keys_fn, *object_ctor, 1, &value);
    if (JS_IsException(*keys_array)) {
        goto fail;
    }
    *length_value = JS_GetPropertyStr(ctx, *keys_array, "length");
    if (JS_IsException(*length_value) || JS_ToInt32(ctx, &length, *length_value) != 0) {
        goto fail;
    }
    if (length <= 0) {
        JS_PopGCRef(ctx, &length_ref);
        JS_PopGCRef(ctx, &keys_array_ref);
        JS_PopGCRef(ctx, &keys_ref);
        JS_PopGCRef(ctx, &object_ref);
        JS_PopGCRef(ctx, &global_ref);
        return 0;
    }
    if (length > ESP32_MQUICKJS_HTTP_SERVER_MAX_HEADERS) {
        goto fail;
    }

    headers = heap_caps_calloc((size_t)length, sizeof(*headers), MALLOC_CAP_8BIT);
    if (headers == NULL) {
        goto fail;
    }

    for (i = 0; i < length; ++i) {
        JSGCRef key_ref;
        JSGCRef val_ref;
        JSValue *key;
        JSValue *val;
        JSCStringBuf key_buf;
        JSCStringBuf val_buf;
        const char *key_str;
        const char *val_str;

        key = JS_PushGCRef(ctx, &key_ref);
        val = JS_PushGCRef(ctx, &val_ref);
        *key = JS_GetPropertyUint32(ctx, *keys_array, (uint32_t)i);
        if (JS_IsException(*key)) {
            JS_PopGCRef(ctx, &val_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto fail;
        }
        key_str = JS_ToCString(ctx, *key, &key_buf);
        if (key_str == NULL) {
            JS_PopGCRef(ctx, &val_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto fail;
        }
        *val = JS_GetPropertyStr(ctx, value, key_str);
        if (JS_IsException(*val)) {
            JS_PopGCRef(ctx, &val_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto fail;
        }
        val_str = JS_ToCString(ctx, *val, &val_buf);
        if (key_str == NULL || val_str == NULL) {
            JS_PopGCRef(ctx, &val_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto fail;
        }

        headers[i].key = http_server_strdup(key_str);
        headers[i].value = http_server_strdup(val_str);
        JS_PopGCRef(ctx, &val_ref);
        JS_PopGCRef(ctx, &key_ref);
        if (headers[i].key == NULL || headers[i].value == NULL) {
            goto fail;
        }
    }

    JS_PopGCRef(ctx, &length_ref);
    JS_PopGCRef(ctx, &keys_array_ref);
    JS_PopGCRef(ctx, &keys_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &global_ref);
    *out_headers = headers;
    *out_count = (size_t)length;
    return 0;

fail:
    JS_PopGCRef(ctx, &length_ref);
    JS_PopGCRef(ctx, &keys_array_ref);
    JS_PopGCRef(ctx, &keys_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &global_ref);
    http_server_free_headers(headers, length > 0 ? (size_t)length : 0U);
    *out_headers = NULL;
    *out_count = 0;
    return -1;
}

static int http_server_make_response(JSContext *ctx,
                                     JSValue value,
                                     esp32_mquickjs_http_server_response_t *out_response)
{
    JSGCRef status_ref;
    JSGCRef headers_ref;
    JSGCRef body_ref;
    JSValue *status_value;
    JSValue *headers_value;
    JSValue *body_value;
    JSGCRef status_text_ref;
    JSValue *status_text_value;
    JSCStringBuf status_text_buf;
    const char *status_text;

    memset(out_response, 0, sizeof(*out_response));
    out_response->status = 200;
    out_response->status_text = NULL;

    if (!esp32_mquickjs_is_response_object(ctx, value)) {
        JS_ThrowTypeError(ctx, "http.server handlers must return a Response");
        return -1;
    }

    status_value = JS_PushGCRef(ctx, &status_ref);
    headers_value = JS_PushGCRef(ctx, &headers_ref);
    body_value = JS_PushGCRef(ctx, &body_ref);
    status_text_value = JS_PushGCRef(ctx, &status_text_ref);

    *status_value = JS_GetPropertyStr(ctx, value, "status");
    *headers_value = JS_GetPropertyStr(ctx, value, "headers");
    *body_value = JS_GetPropertyStr(ctx, value, "body");
    *status_text_value = JS_GetPropertyStr(ctx, value, "statusText");
    if (JS_IsException(*status_value) || JS_IsException(*headers_value) || JS_IsException(*body_value) ||
        JS_IsException(*status_text_value)) {
        goto fail;
    }
    if (http_server_parse_status(ctx, *status_value, &out_response->status) != 0) {
        goto fail;
    }
    if (http_server_parse_headers(ctx,
                                  *headers_value,
                                  &out_response->headers,
                                  &out_response->header_count) != 0) {
        goto fail;
    }
    status_text = JS_IsUndefined(*status_text_value) || JS_IsNull(*status_text_value)
                      ? http_server_status_text(out_response->status)
                      : JS_ToCString(ctx, *status_text_value, &status_text_buf);
    if (status_text == NULL) {
        goto fail;
    }
    out_response->status_text = http_server_strdup(status_text);
    if (out_response->status_text == NULL) {
        goto fail;
    }
    if (!JS_IsUndefined(*body_value) && !JS_IsNull(*body_value)) {
        if (!esp32_mquickjs_fs_parse_stream_ref(ctx, *body_value, &out_response->body_stream_ref)) {
            JS_ThrowTypeError(ctx, "Response.body must be a Stream");
            goto fail;
        }
        out_response->has_body_stream = true;
    }

    JS_PopGCRef(ctx, &status_text_ref);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &status_ref);
    return 0;

fail:
    JS_PopGCRef(ctx, &status_text_ref);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &status_ref);
    http_server_free_response(out_response);
    return -1;
}

static bool http_server_has_content_type(const esp32_mquickjs_http_server_response_t *response)
{
    size_t i;

    for (i = 0; i < response->header_count; ++i) {
        if (strcasecmp(response->headers[i].key, "content-type") == 0) {
            return true;
        }
    }
    return false;
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER && CONFIG_ESP32_MQUICKJS_FEATURE_FS
static bool http_server_resolve_littlefs_path(const char *input_path,
                                              char *out_path,
                                              size_t out_path_size)
{
    static const size_t base_len = sizeof(ESP32_MQUICKJS_LITTLEFS_BASE_PATH) - 1;
    const char *cursor;
    size_t out_len;

    if (input_path == NULL || out_path == NULL || out_path_size <= base_len + 1) {
        return false;
    }

    memcpy(out_path, ESP32_MQUICKJS_LITTLEFS_BASE_PATH, base_len);
    out_path[base_len] = '\0';
    out_len = base_len;

    if (input_path[0] == '\0' || strcmp(input_path, ".") == 0) {
        return true;
    }

    cursor = input_path[0] == '/' ? input_path + 1 : input_path;
    while (*cursor != '\0') {
        const char *segment_start;
        size_t segment_len;

        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        segment_start = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }
        segment_len = (size_t)(cursor - segment_start);
        if (segment_len == 1 && segment_start[0] == '.') {
            continue;
        }
        if (segment_len == 2 && segment_start[0] == '.' && segment_start[1] == '.') {
            return false;
        }
        if (out_len + 1 + segment_len >= out_path_size) {
            return false;
        }
        out_path[out_len++] = '/';
        memcpy(out_path + out_len, segment_start, segment_len);
        out_len += segment_len;
        out_path[out_len] = '\0';
    }
    return true;
}

static const char *http_server_guess_content_type(const char *path)
{
    const char *ext = strrchr(path != NULL ? path : "", '.');

    if (ext == NULL) {
        return "application/octet-stream";
    }
    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) {
        return "text/html; charset=utf-8";
    }
    if (strcasecmp(ext, ".css") == 0) {
        return "text/css; charset=utf-8";
    }
    if (strcasecmp(ext, ".js") == 0 || strcasecmp(ext, ".mjs") == 0) {
        return "application/javascript; charset=utf-8";
    }
    if (strcasecmp(ext, ".json") == 0) {
        return "application/json; charset=utf-8";
    }
    if (strcasecmp(ext, ".txt") == 0) {
        return "text/plain; charset=utf-8";
    }
    if (strcasecmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcasecmp(ext, ".png") == 0) {
        return "image/png";
    }
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcasecmp(ext, ".gif") == 0) {
        return "image/gif";
    }
    if (strcasecmp(ext, ".ico") == 0) {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

static bool http_server_join_path(char *out,
                                  size_t out_size,
                                  const char *root,
                                  const char *relative,
                                  const char *suffix)
{
    size_t root_len = strlen(root);
    size_t relative_len = strlen(relative);
    size_t suffix_len = suffix != NULL ? strlen(suffix) : 0;
    size_t total = root_len + 1 + relative_len + suffix_len;

    if (total + 1 > out_size) {
        return false;
    }

    memcpy(out, root, root_len);
    out[root_len] = '/';
    memcpy(out + root_len + 1, relative, relative_len);
    if (suffix_len > 0) {
        memcpy(out + root_len + 1 + relative_len, suffix, suffix_len);
    }
    out[total] = '\0';
    return true;
}

static JSValue http_server_make_static_handler(JSContext *ctx, const char *root_path)
{
    JSGCRef handler_ref;
    JSValue *handler_obj;

    handler_obj = JS_PushGCRef(ctx, &handler_ref);
    *handler_obj = JS_NewObjectClassUser(ctx, JS_CLASS_STATIC_FILE_HANDLER);
    if (JS_IsException(*handler_obj) ||
        !esp32_mquickjs_set_property(ctx, *handler_obj, "root", JS_NewString(ctx, root_path))) {
        JS_PopGCRef(ctx, &handler_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &handler_ref);
}
#endif

static esp_err_t http_server_send_response(httpd_req_t *req,
                                           const esp32_mquickjs_http_server_response_t *response)
{
    char status_line[32];
    uint8_t *chunk_buffer = NULL;
    size_t i;
    esp_err_t err;

    snprintf(status_line,
             sizeof(status_line),
             "%" PRId32 " %s",
             response->status,
             response->status_text != NULL ? response->status_text : http_server_status_text(response->status));
    err = httpd_resp_set_status(req, status_line);
    if (err != ESP_OK) {
        return err;
    }
    if (response->has_body_stream &&
        !http_server_has_content_type(response)) {
        err = httpd_resp_set_type(req, "text/plain; charset=utf-8");
        if (err != ESP_OK) {
            return err;
        }
    }

    for (i = 0; i < response->header_count; ++i) {
        err = httpd_resp_set_hdr(req, response->headers[i].key, response->headers[i].value);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (response->has_body_stream) {
        size_t read_len = 0;

        if (req->method == HTTP_HEAD) {
            err = esp32_mquickjs_fs_stream_close(&response->body_stream_ref);
            if (err != ESP_OK) {
                return err;
            }
            return httpd_resp_send_chunk(req, NULL, 0);
        }
        chunk_buffer = heap_caps_malloc(ESP32_MQUICKJS_HTTP_SERVER_STREAM_CHUNK_SIZE, MALLOC_CAP_8BIT);
        if (chunk_buffer == NULL) {
            return ESP_ERR_NO_MEM;
        }
        do {
            err = esp32_mquickjs_fs_stream_read(&response->body_stream_ref,
                                                chunk_buffer,
                                                ESP32_MQUICKJS_HTTP_SERVER_STREAM_CHUNK_SIZE,
                                                &read_len);
            if (err != ESP_OK) {
                heap_caps_free(chunk_buffer);
                esp32_mquickjs_fs_stream_close(&response->body_stream_ref);
                return err;
            }
            if (read_len > 0) {
                err = httpd_resp_send_chunk(req, (const char *)chunk_buffer, read_len);
                if (err != ESP_OK) {
                    heap_caps_free(chunk_buffer);
                    esp32_mquickjs_fs_stream_close(&response->body_stream_ref);
                    return err;
                }
            }
        } while (read_len > 0);
        heap_caps_free(chunk_buffer);
        err = esp32_mquickjs_fs_stream_close(&response->body_stream_ref);
        if (err != ESP_OK) {
            return err;
        }
        return httpd_resp_send_chunk(req, NULL, 0);
    }
    return httpd_resp_send(req, NULL, 0);
}

static bool http_server_start_slot(esp32_mquickjs_http_server_slot_t *server)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    struct ifreq ifr;

    if (server == NULL) {
        return false;
    }
    if (server->started) {
        return true;
    }

    config.server_port = server->port;
    config.ctrl_port = server->ctrl_port;
    config.uri_match_fn = httpd_uri_match_wildcard;
    if (!http_server_resolve_ifreq_for_host(server->host, &ifr)) {
        ESP_LOGE(TAG, "failed to resolve HTTP server host binding for %s",
                 server->host != NULL ? server->host : "(null)");
        return false;
    }
    if (!http_server_host_is_any(server->host)) {
        config.if_name = &ifr;
    }
    if (httpd_start(&server->handle, &config) != ESP_OK) {
        return false;
    }

    memset(&server->dispatch_uri, 0, sizeof(server->dispatch_uri));
    server->dispatch_uri.uri = "/*";
    server->dispatch_uri.method = HTTP_ANY;
    server->dispatch_uri.handler = http_server_dispatch_handler;
    server->dispatch_uri.user_ctx = server;
    if (httpd_register_uri_handler(server->handle, &server->dispatch_uri) != ESP_OK) {
        httpd_stop(server->handle);
        server->handle = NULL;
        return false;
    }

    server->started = true;
    return true;
}

static void http_server_stop_slot(esp32_mquickjs_http_server_slot_t *server)
{
    int i;

    if (server == NULL || !server->started) {
        return;
    }

    httpd_stop(server->handle);
    server->handle = NULL;
    server->started = false;
    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES; ++i) {
        if (s_http_server_state.routes[i].allocated &&
            s_http_server_state.routes[i].server_id == server->server_id) {
            s_http_server_state.routes[i].registered = false;
        }
    }
}

static int http_server_server_id_from_object(JSContext *ctx,
                                             JSValue server_value,
                                             const char *api_name,
                                             int32_t *out_server_id)
{
    JSGCRef server_id_ref;
    JSValue *server_id_value;
    int server_id = -1;

    if (out_server_id == NULL || JS_GetClassID(ctx, server_value) != JS_CLASS_HTTP_SERVER) {
        JS_ThrowTypeError(ctx, "%s expects an HttpServer instance", api_name);
        return -1;
    }

    server_id_value = JS_PushGCRef(ctx, &server_id_ref);
    *server_id_value = JS_GetPropertyStr(ctx, server_value, "serverId");
    if (JS_IsException(*server_id_value) || JS_ToInt32(ctx, &server_id, *server_id_value) != 0) {
        JS_PopGCRef(ctx, &server_id_ref);
        JS_ThrowTypeError(ctx, "%s expects a valid HttpServer instance", api_name);
        return -1;
    }
    JS_PopGCRef(ctx, &server_id_ref);
    *out_server_id = server_id;
    return 0;
}

static JSValue http_server_make_server_object(JSContext *ctx, JSValue global_obj, int32_t server_id)
{
    JSGCRef server_ref;
    JSValue *server_obj;
    esp32_mquickjs_http_server_slot_t *server = http_server_get_slot(server_id);

    (void)global_obj;
    server_obj = JS_PushGCRef(ctx, &server_ref);
    *server_obj = JS_NewObjectClassUser(ctx, JS_CLASS_HTTP_SERVER);
    if (JS_IsException(*server_obj) || server == NULL) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *server_obj, "serverId", JS_NewInt32(ctx, server_id)) ||
        !esp32_mquickjs_set_property(ctx, *server_obj, "port", JS_NewInt32(ctx, server->port)) ||
        !esp32_mquickjs_set_property(ctx, *server_obj, "ctrlPort", JS_NewInt32(ctx, server->ctrl_port)) ||
        !esp32_mquickjs_set_property(ctx,
                                     *server_obj,
                                     "host",
                                     JS_NewString(ctx,
                                                  server->host != NULL
                                                      ? server->host
                                                      : "0.0.0.0")) ||
        !esp32_mquickjs_set_property(ctx, *server_obj, "started", JS_NewBool(server->started))) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &server_ref);

fail:
    JS_PopGCRef(ctx, &server_ref);
    return JS_EXCEPTION;
}

static bool http_server_register_route_js(JSContext *ctx,
                                          int32_t server_id,
                                          const char *method_name,
                                          JSValue pattern_value,
                                          JSValue handler,
                                          JSValue *out_result)
{
    esp32_mquickjs_http_server_slot_t *server;
    esp32_mquickjs_http_server_route_t *route = NULL;
    JSGCRef handle_ref;
    JSValue *handle_value = NULL;
    JSValue callback = JS_UNDEFINED;
    JSValue callback_this = JS_UNDEFINED;
    int i;
    httpd_method_t method;

    if (JS_IsFunction(ctx, handler)) {
        callback = handler;
    } else if (http_server_is_object(ctx, handler)) {
        handle_value = JS_PushGCRef(ctx, &handle_ref);
        *handle_value = JS_GetPropertyStr(ctx, handler, "handle");
        if (JS_IsException(*handle_value)) {
            JS_PopGCRef(ctx, &handle_ref);
            *out_result = JS_EXCEPTION;
            return true;
        }
        if (!JS_IsFunction(ctx, *handle_value)) {
            JS_PopGCRef(ctx, &handle_ref);
            *out_result = JS_ThrowTypeError(ctx,
                                            "server.<method>(pathOrPattern, handler) expects a function or object with handle(req)");
            return true;
        }
        callback = *handle_value;
        callback_this = handler;
    } else {
        *out_result = JS_ThrowTypeError(ctx,
                                        "server.<method>(pathOrPattern, handler) expects a function or object with handle(req)");
        return true;
    }

    server = http_server_get_slot(server_id);
    if (server == NULL) {
        *out_result = JS_ThrowInternalError(ctx, "invalid server handle");
        return true;
    }

    method = http_server_method_from_name(method_name);

    http_server_lock();
    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES; ++i) {
        if (!s_http_server_state.routes[i].allocated) {
            route = &s_http_server_state.routes[i];
            route->allocated = true;
            route->server_id = (uint8_t)server_id;
            route->method = method;
            break;
        }
    }
    http_server_unlock();

    if (route == NULL) {
        if (handle_value != NULL) {
            JS_PopGCRef(ctx, &handle_ref);
        }
        *out_result = JS_ThrowInternalError(ctx, "too many server routes");
        return true;
    }

    if (!http_server_compile_route_pattern(ctx, pattern_value, route)) {
        http_server_cleanup_route(ctx, route);
        if (handle_value != NULL) {
            JS_PopGCRef(ctx, &handle_ref);
        }
        *out_result = JS_ThrowTypeError(ctx, "server.<method>(pathOrPattern, handler) expects a string path or RegExp");
        return true;
    }
    *JS_AddGCRef(ctx, &route->callback) = callback;
    route->callback_ref_added = true;
    if (!JS_IsUndefined(callback_this) && !JS_IsNull(callback_this)) {
        *JS_AddGCRef(ctx, &route->callback_this) = callback_this;
        route->callback_this_ref_added = true;
    }
    if (handle_value != NULL) {
        JS_PopGCRef(ctx, &handle_ref);
    }

    *out_result = JS_UNDEFINED;
    return true;
}

bool esp32_mquickjs_init_http_server_runtime(JSContext *ctx,
                                             esp32_mquickjs_runtime_t *runtime)
{
    (void)ctx;

    if (!http_server_init_state(runtime)) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    if (!esp32_mquickjs_register_async_poller(runtime, http_server_async_poller, NULL)) {
        JS_ThrowInternalError(ctx, "failed to register http server async poller");
        return false;
    }
    return true;
}

JSValue js_http_server_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "HttpServer cannot be constructed directly");
}

JSValue js_http_static_file_handler_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "StaticFileHandler cannot be constructed directly");
}

static JSValue http_server_route_method(JSContext *ctx,
                                        JSValue this_value,
                                        const char *api_name,
                                        const char *method_name,
                                        int argc,
                                        JSValue *argv)
{
    int32_t server_id = -1;
    JSValue result = JS_UNDEFINED;

    if (http_server_server_id_from_object(ctx, this_value, api_name, &server_id) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "%s(pathOrPattern, handler) expects a path and handler", api_name);
    }
    if (http_server_register_route_js(ctx, server_id, method_name, argv[0], argv[1], &result)) {
        return result;
    }
    return JS_ThrowInternalError(ctx, "%s failed", api_name);
}

JSValue js_http_server_start(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int32_t server_id = -1;
    esp32_mquickjs_http_server_slot_t *server;

    (void)argc;
    (void)argv;
    if (http_server_server_id_from_object(ctx, *this_val, "server.start", &server_id) != 0) {
        return JS_EXCEPTION;
    }
    server = http_server_get_slot(server_id);
    if (!http_server_start_slot(server)) {
        return JS_ThrowInternalError(ctx, "failed to start HTTP server");
    }
    esp32_mquickjs_set_property(ctx, *this_val, "started", JS_TRUE);
    return JS_UNDEFINED;
}

JSValue js_http_server_stop(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int32_t server_id = -1;

    (void)argc;
    (void)argv;
    if (http_server_server_id_from_object(ctx, *this_val, "server.stop", &server_id) != 0) {
        return JS_EXCEPTION;
    }
    http_server_stop_slot(http_server_get_slot(server_id));
    esp32_mquickjs_set_property(ctx, *this_val, "started", JS_FALSE);
    return JS_UNDEFINED;
}

JSValue js_http_server_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return http_server_route_method(ctx, *this_val, "server.get", "GET", argc, argv);
}

JSValue js_http_server_post(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return http_server_route_method(ctx, *this_val, "server.post", "POST", argc, argv);
}

JSValue js_http_server_put(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return http_server_route_method(ctx, *this_val, "server.put", "PUT", argc, argv);
}

JSValue js_http_server_patch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return http_server_route_method(ctx, *this_val, "server.patch", "PATCH", argc, argv);
}

JSValue js_http_server_delete(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return http_server_route_method(ctx, *this_val, "server.delete", "DELETE", argc, argv);
}

JSValue js_http_server_head(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return http_server_route_method(ctx, *this_val, "server.head", "HEAD", argc, argv);
}

JSValue js_http_server_options(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return http_server_route_method(ctx, *this_val, "server.options", "OPTIONS", argc, argv);
}

JSValue js_http_server_all(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return http_server_route_method(ctx, *this_val, "server.all", "ANY", argc, argv);
}

JSValue js_http_server_create(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef options_ref;
    JSGCRef port_ref;
    JSGCRef host_ref;
    JSGCRef global_ref;
    JSValue *options;
    JSValue *port_value;
    JSValue *host_value;
    JSValue *global_obj;
    httpd_config_t default_config = HTTPD_DEFAULT_CONFIG();
    int port = 80;
    int ctrl_port = default_config.ctrl_port;
    const char *host = "0.0.0.0";
    JSCStringBuf host_buf;
    int i;
    esp32_mquickjs_http_server_slot_t *server = NULL;

    (void)this_val;
    options = JS_PushGCRef(ctx, &options_ref);
    port_value = JS_PushGCRef(ctx, &port_ref);
    host_value = JS_PushGCRef(ctx, &host_ref);
    global_obj = JS_PushGCRef(ctx, &global_ref);
    *options = argc >= 1 ? argv[0] : JS_UNDEFINED;
    *port_value = JS_UNDEFINED;
    *host_value = JS_UNDEFINED;
    *global_obj = JS_GetGlobalObject(ctx);
    if (JS_IsException(*global_obj)) {
        JS_PopGCRef(ctx, &global_ref);
        JS_PopGCRef(ctx, &host_ref);
        JS_PopGCRef(ctx, &port_ref);
        JS_PopGCRef(ctx, &options_ref);
        return JS_EXCEPTION;
    }

    if (!JS_IsUndefined(*options) && !JS_IsNull(*options)) {
        if (!http_server_is_object(ctx, *options)) {
            JS_PopGCRef(ctx, &global_ref);
            JS_PopGCRef(ctx, &host_ref);
            JS_PopGCRef(ctx, &port_ref);
            JS_PopGCRef(ctx, &options_ref);
            return JS_ThrowTypeError(ctx, "http.server(options) expects an object");
        }
        *port_value = JS_GetPropertyStr(ctx, *options, "port");
        *host_value = JS_GetPropertyStr(ctx, *options, "host");
        if (JS_IsException(*port_value) || JS_IsException(*host_value)) {
            JS_PopGCRef(ctx, &global_ref);
            JS_PopGCRef(ctx, &host_ref);
            JS_PopGCRef(ctx, &port_ref);
            JS_PopGCRef(ctx, &options_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*port_value) && !JS_IsNull(*port_value)) {
            if (JS_ToInt32(ctx, &port, *port_value) != 0 || port <= 0 || port > 65535) {
                JS_PopGCRef(ctx, &global_ref);
                JS_PopGCRef(ctx, &host_ref);
                JS_PopGCRef(ctx, &port_ref);
                JS_PopGCRef(ctx, &options_ref);
                return JS_ThrowTypeError(ctx, "http.server({ port }) expects a valid TCP port");
            }
        }
        if (!JS_IsUndefined(*host_value) && !JS_IsNull(*host_value)) {
            if (!JS_IsString(ctx, *host_value)) {
                JS_PopGCRef(ctx, &global_ref);
                JS_PopGCRef(ctx, &host_ref);
                JS_PopGCRef(ctx, &port_ref);
                JS_PopGCRef(ctx, &options_ref);
                return JS_ThrowTypeError(ctx, "http.server({ host }) expects an IPv4 address string");
            }
            host = JS_ToCString(ctx, *host_value, &host_buf);
            if (host == NULL) {
                JS_PopGCRef(ctx, &global_ref);
                JS_PopGCRef(ctx, &host_ref);
                JS_PopGCRef(ctx, &port_ref);
                JS_PopGCRef(ctx, &options_ref);
                return JS_EXCEPTION;
            }
        }
    }

    http_server_lock();
    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_SERVERS; ++i) {
        if (!s_http_server_state.servers[i].allocated) {
            server = &s_http_server_state.servers[i];
            server->allocated = true;
            server->port = (uint16_t)port;
            server->ctrl_port = (uint16_t)(ctrl_port + i);
            server->host = http_server_strdup(host);
            server->started = false;
            server->handle = NULL;
            if (server->host == NULL) {
                http_server_cleanup_server(server);
                server = NULL;
            }
            break;
        }
    }
    http_server_unlock();

    if (server == NULL) {
        JS_PopGCRef(ctx, &global_ref);
        JS_PopGCRef(ctx, &host_ref);
        JS_PopGCRef(ctx, &port_ref);
        JS_PopGCRef(ctx, &options_ref);
        return JS_ThrowInternalError(ctx, "failed to allocate HTTP server");
    }

    {
        JSValue result = http_server_make_server_object(ctx, *global_obj, server->server_id);
        JS_PopGCRef(ctx, &global_ref);
        JS_PopGCRef(ctx, &host_ref);
        JS_PopGCRef(ctx, &port_ref);
        JS_PopGCRef(ctx, &options_ref);
        return result;
    }
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER && CONFIG_ESP32_MQUICKJS_FEATURE_FS
static JSValue http_server_static_file_handler_handle_internal(JSContext *ctx,
                                                               JSValue handler_value,
                                                               JSValue request_value);

JSValue js_http_static_file_handler(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSCStringBuf root_buf;
    const char *root;

    (void)this_val;
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "staticFileHandler(root) expects a LittleFS root path");
    }
    root = JS_ToCString(ctx, argv[0], &root_buf);
    if (root == NULL) {
        return JS_EXCEPTION;
    }
    return http_server_make_static_handler(ctx, root);
}

JSValue js_http_static_file_handler_handle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return http_server_static_file_handler_handle_internal(ctx,
                                                           *this_val,
                                                           argc >= 1 ? argv[0] : JS_UNDEFINED);
}

static JSValue http_server_static_file_handler_handle_internal(JSContext *ctx,
                                                               JSValue handler_value,
                                                               JSValue request_value)
{
    JSGCRef root_ref;
    JSGCRef relative_ref;
    JSValue *root_value;
    JSValue *relative_value;
    JSCStringBuf root_buf;
    JSCStringBuf relative_buf;
    const char *root;
    const char *relative_path;
    char littlefs_root[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    char target_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
    FILE *file;
    const char *content_type;
    JSValue response_obj;

    if (JS_GetClassID(ctx, handler_value) != JS_CLASS_STATIC_FILE_HANDLER || !http_server_is_object(ctx, request_value)) {
        return JS_ThrowTypeError(ctx, "staticFileHandler.handle(req) expects a request object");
    }

    root_value = JS_PushGCRef(ctx, &root_ref);
    relative_value = JS_PushGCRef(ctx, &relative_ref);
    *root_value = JS_GetPropertyStr(ctx, handler_value, "root");
    *relative_value = JS_GetPropertyStr(ctx, request_value, "relativePath");
    if (JS_IsException(*root_value) || JS_IsException(*relative_value)) {
        JS_PopGCRef(ctx, &relative_ref);
        JS_PopGCRef(ctx, &root_ref);
        return JS_EXCEPTION;
    }

    root = JS_ToCString(ctx, *root_value, &root_buf);
    relative_path = JS_ToCString(ctx, *relative_value, &relative_buf);
    if (root == NULL || relative_path == NULL) {
        JS_PopGCRef(ctx, &relative_ref);
        JS_PopGCRef(ctx, &root_ref);
        return JS_EXCEPTION;
    }

    if (!http_server_resolve_littlefs_path(root, littlefs_root, sizeof(littlefs_root))) {
        JS_PopGCRef(ctx, &relative_ref);
        JS_PopGCRef(ctx, &root_ref);
        return JS_ThrowTypeError(ctx, "staticFileHandler(root) expects a path under /littlefs");
    }
    while (*relative_path == '/') {
        relative_path++;
    }
    if (*relative_path == '\0') {
        relative_path = "index.html";
    }

    if (!http_server_join_path(target_path, sizeof(target_path), littlefs_root, relative_path, NULL)) {
        JS_PopGCRef(ctx, &relative_ref);
        JS_PopGCRef(ctx, &root_ref);
        return JS_ThrowInternalError(ctx, "static file path is too long");
    }
    file = fopen(target_path, "rb");
    if (file == NULL && strchr(relative_path, '.') == NULL) {
        if (!http_server_join_path(target_path,
                                   sizeof(target_path),
                                   littlefs_root,
                                   relative_path,
                                   "/index.html")) {
            JS_PopGCRef(ctx, &relative_ref);
            JS_PopGCRef(ctx, &root_ref);
            return JS_ThrowInternalError(ctx, "static file path is too long");
        }
        file = fopen(target_path, "rb");
    }
    if (file == NULL) {
        JSGCRef global_ref;
        JSValue *global_obj;

        global_obj = JS_PushGCRef(ctx, &global_ref);
        *global_obj = JS_GetGlobalObject(ctx);
        JS_PopGCRef(ctx, &relative_ref);
        JS_PopGCRef(ctx, &root_ref);
        response_obj = JS_IsException(*global_obj)
                           ? JS_EXCEPTION
                           : esp32_mquickjs_make_response_object(ctx,
                                                                 *global_obj,
                                                                 404,
                                                                 "Not Found",
                                                                 "",
                                                                 JS_UNDEFINED,
                                                                 JS_NewString(ctx, "Not Found"));
        JS_PopGCRef(ctx, &global_ref);
        return response_obj;
    }

    fclose(file);
    content_type = http_server_guess_content_type(target_path);
    {
        JSGCRef global_ref;
        JSGCRef headers_ref;
        JSValue *global_obj;
        JSValue *headers_obj;
        JSValue stream_obj;

        global_obj = JS_PushGCRef(ctx, &global_ref);
        headers_obj = JS_PushGCRef(ctx, &headers_ref);
        *global_obj = JS_GetGlobalObject(ctx);
        *headers_obj = JS_NewObject(ctx);
        if (JS_IsException(*global_obj) || JS_IsException(*headers_obj)) {
            JS_PopGCRef(ctx, &headers_ref);
            JS_PopGCRef(ctx, &global_ref);
            JS_PopGCRef(ctx, &relative_ref);
            JS_PopGCRef(ctx, &root_ref);
            return JS_EXCEPTION;
        }
        if (!esp32_mquickjs_set_property(ctx,
                                         *headers_obj,
                                         "content-type",
                                         JS_NewString(ctx, content_type))) {
            JS_PopGCRef(ctx, &headers_ref);
            JS_PopGCRef(ctx, &global_ref);
            JS_PopGCRef(ctx, &relative_ref);
            JS_PopGCRef(ctx, &root_ref);
            return JS_EXCEPTION;
        }
        stream_obj = esp32_mquickjs_stream_open_file(ctx, *global_obj, target_path, "rb");
        if (JS_IsException(stream_obj)) {
            JS_PopGCRef(ctx, &headers_ref);
            JS_PopGCRef(ctx, &global_ref);
            JS_PopGCRef(ctx, &relative_ref);
            JS_PopGCRef(ctx, &root_ref);
            return JS_EXCEPTION;
        }
        response_obj = esp32_mquickjs_make_response_object(ctx,
                                                           *global_obj,
                                                           200,
                                                           "OK",
                                                           "",
                                                           *headers_obj,
                                                           stream_obj);
        JS_PopGCRef(ctx, &headers_ref);
        JS_PopGCRef(ctx, &global_ref);
    }
    JS_PopGCRef(ctx, &relative_ref);
    JS_PopGCRef(ctx, &root_ref);
    return response_obj;
}
#endif

static bool http_server_async_poller(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime,
                                     void *opaque)
{
    esp32_mquickjs_http_server_event_t event;
    bool handled = false;

    (void)runtime;
    (void)opaque;
    if (ctx == NULL || s_http_server_state.queue == NULL) {
        return false;
    }

    while (xQueueReceive(s_http_server_state.queue, &event, 0) == pdTRUE) {
        esp32_mquickjs_http_server_request_t *request;
        esp32_mquickjs_http_server_route_t *route;
        JSGCRef callback_ref;
        JSValue *callback_fn;
        JSValue req_obj = JS_UNDEFINED;
        JSValue callback_this = JS_NULL;
        JSValue callback_result = JS_UNDEFINED;
        esp32_mquickjs_http_server_response_t response = {0};

        if (event.request_id >= ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN) {
            continue;
        }

        request = &s_http_server_state.requests[event.request_id];
        if (!request->allocated || request->generation != event.generation) {
            continue;
        }

        route = http_server_find_route(ctx, request->server_id, request->method, request->path);
        if (route == NULL) {
            http_server_send_error(request->async_req, 404, "Nothing matches the given URI");
            http_server_cleanup_request(request);
            handled = true;
            continue;
        }

        callback_fn = JS_PushGCRef(ctx, &callback_ref);
        *callback_fn = route->callback.val;
        callback_this = route->callback_this_ref_added ? route->callback_this.val : JS_NULL;
        if (!http_server_make_request_object(ctx, request, route, &req_obj)) {
            JS_PopGCRef(ctx, &callback_ref);
            http_server_send_error(request->async_req, 500, "failed to build request object");
            http_server_cleanup_request(request);
            handled = true;
            continue;
        }

        callback_result = http_server_call_function(ctx, *callback_fn, callback_this, 1, &req_obj);
        if (JS_IsException(callback_result)) {
            esp32_mquickjs_print_exception(ctx);
            http_server_send_error(request->async_req, 500, "handler exception");
        } else if (http_server_make_response(ctx, callback_result, &response) != 0) {
            http_server_send_error(request->async_req, 500, "invalid response object");
        } else if (http_server_send_response(request->async_req, &response) != ESP_OK) {
            ESP_LOGE(TAG, "failed to send response");
        }

        if (!JS_IsException(req_obj)) {
            JSGCRef req_body_ref;
            JSValue *req_body_value;

            req_body_value = JS_PushGCRef(ctx, &req_body_ref);
            *req_body_value = JS_GetPropertyStr(ctx, req_obj, "body");
            if (!JS_IsException(*req_body_value)) {
                esp32_mquickjs_stream_close_value(ctx, *req_body_value);
            }
            JS_PopGCRef(ctx, &req_body_ref);
        }

        JS_PopGCRef(ctx, &callback_ref);
        http_server_free_response(&response);
        http_server_cleanup_request(request);
        handled = true;
    }

    return handled;
}

#endif
