#include "esp32_mquickjs_http_server.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_http_server_resources.h"
#include "esp32_mquickjs_net.h"
#include "utils/esp32_mquickjs_request_response.h"
#include "esp32_mquickjs_stream.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
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
    uint32_t generation;
    uint16_t port;
    uint16_t ctrl_port;
    char *host;
    bool started;
    bool release_pending;
    httpd_handle_t handle;
    httpd_uri_t dispatch_uri;
    esp32_mquickjs_event_queue_t *events;
} esp32_mquickjs_http_server_slot_t;

typedef struct {
    bool allocated;
    uint8_t route_id;
    uint8_t server_id;
    httpd_method_t method;
    char *uri;
    char *mount_path;
    bool match_with_regex;
    bool registered;
    httpd_uri_t uri_def;
} esp32_mquickjs_http_server_route_t;

typedef struct {
    bool allocated;
    bool responding;
    uint8_t request_id;
    uint32_t generation;
    uint8_t server_id;
    httpd_req_t *async_req;
    char *method;
    char *path;
    char *query_string;
    uint8_t *body;
    size_t body_len;
    esp32_mquickjs_http_server_header_t *headers;
    size_t header_count;
} esp32_mquickjs_http_server_request_t;

typedef struct {
    uint8_t request_id;
    uint32_t generation;
} esp32_mquickjs_http_server_event_t;

typedef struct {
    uint8_t server_id;
    uint32_t generation;
} esp32_mquickjs_http_server_event_source_t;

typedef struct {
    uint8_t server_id;
    uint32_t generation;
} esp32_mquickjs_http_server_ref_t;

typedef struct {
    bool initialized;
    bool shutting_down;
    esp32_mquickjs_runtime_t *runtime;
    SemaphoreHandle_t lock;
    esp32_mquickjs_http_server_slot_t servers[ESP32_MQUICKJS_HTTP_SERVER_MAX_SERVERS];
    esp32_mquickjs_http_server_route_t routes[ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES];
    esp32_mquickjs_http_server_request_t requests[ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN];
} esp32_mquickjs_http_server_state_t;

typedef struct {
    int32_t status;
    char *status_text;
    bool has_body_stream;
    bool body_binary;
    bool has_known_length;
    size_t known_length;
    esp32_mquickjs_fs_stream_ref_t body_stream_ref;
    esp32_mquickjs_http_server_header_t *headers;
    size_t header_count;
} esp32_mquickjs_http_server_response_t;

static const char *TAG = "esp32qjs_httpd";
static esp32_mquickjs_http_server_state_t s_http_server_state;

static bool http_server_glob_match(const char *pattern, const char *text);
static void http_server_close_request_body(JSContext *ctx,
                                           JSValue request_value);
static esp_err_t http_server_stop_slot(
    esp32_mquickjs_http_server_slot_t *server);
static esp_err_t http_server_close_slot(
    JSContext *ctx, esp32_mquickjs_http_server_slot_t *server);
static esp_err_t http_server_cleanup_all(JSContext *ctx);
static void http_server_reset_state(void);

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
    return esp32_mquickjs_call(ctx,
                               esp32_mquickjs_get_active_runtime(),
                               func,
                               this_val,
                               argc,
                               argv);
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
    uint8_t request_id;
    uint32_t generation;

    if (request == NULL || !request->allocated) {
        return;
    }

    request_id = request->request_id;
    generation = request->generation;
    if (request->async_req != NULL) {
        httpd_req_async_handler_complete(request->async_req);
    }
    heap_caps_free(request->method);
    heap_caps_free(request->path);
    heap_caps_free(request->query_string);
    heap_caps_free(request->body);
    http_server_free_headers(request->headers, request->header_count);
    http_server_lock();
    memset(request, 0, sizeof(*request));
    request->request_id = request_id;
    request->generation = generation;
    http_server_unlock();
}

static void http_server_cleanup_route(JSContext *ctx, esp32_mquickjs_http_server_route_t *route)
{
    if (route == NULL || !route->allocated) {
        return;
    }

    (void)ctx;
    heap_caps_free(route->uri);
    heap_caps_free(route->mount_path);
    memset(route, 0, sizeof(*route));
}

static void http_server_cleanup_server(esp32_mquickjs_http_server_slot_t *server)
{
    uint8_t server_id;
    uint32_t generation;

    if (server == NULL || !server->allocated) {
        return;
    }
    server_id = server->server_id;
    generation = server->generation;
    heap_caps_free(server->host);
    memset(server, 0, sizeof(*server));
    server->server_id = server_id;
    server->generation = generation;
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

static esp_err_t http_server_init_state(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime)
{
    esp_err_t err;
    int i;

    if (s_http_server_state.initialized) {
        if (s_http_server_state.shutting_down) {
            err = http_server_cleanup_all(ctx);
            if (err != ESP_OK) {
                return err;
            }
            http_server_reset_state();
        } else {
            if (runtime != NULL) {
                s_http_server_state.runtime = runtime;
            }
            return ESP_OK;
        }
    }

    memset(&s_http_server_state, 0, sizeof(s_http_server_state));
    s_http_server_state.lock = xSemaphoreCreateMutex();
    if (s_http_server_state.lock == NULL) {
        memset(&s_http_server_state, 0, sizeof(s_http_server_state));
        return ESP_ERR_NO_MEM;
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
    return ESP_OK;
}

static void http_server_reset_state(void)
{
    if (s_http_server_state.lock != NULL) {
        vSemaphoreDelete(s_http_server_state.lock);
    }
    memset(&s_http_server_state, 0, sizeof(s_http_server_state));
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
    esp32_mquickjs_http_server_request_t *request = NULL;
    int i;

    http_server_lock();
    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN; ++i) {
        if (!s_http_server_state.requests[i].allocated) {
            request = &s_http_server_state.requests[i];
            request->allocated = true;
            request->generation++;
            if (request->generation == 0) {
                request->generation++;
            }
            break;
        }
    }
    http_server_unlock();
    return request;
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
                                              JSValue *pattern_value,
                                              esp32_mquickjs_http_server_route_t *route)
{
    JSCStringBuf uri_buf;
    const char *uri = NULL;

    if (route == NULL) {
        return false;
    }

    if (JS_IsString(ctx, *pattern_value)) {
        uri = JS_ToCString(ctx, *pattern_value, &uri_buf);
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

static uint8_t *http_server_recv_body(httpd_req_t *req,
                                      size_t max_len,
                                      size_t *out_len,
                                      esp_err_t *out_err)
{
    size_t remaining;
    size_t offset = 0;
    uint8_t *body;

    *out_err = ESP_OK;
    *out_len = 0;
    if (req->content_len == 0) {
        return NULL;
    }
    if (req->content_len > max_len) {
        *out_err = ESP_ERR_HTTPD_RESP_SEND;
        return NULL;
    }

    body = esp32_mquickjs_memory_payload_alloc(
        req->content_len, ESP32_MQUICKJS_MEMORY_EXTERNAL);
    if (body == NULL) {
        *out_err = ESP_ERR_NO_MEM;
        return NULL;
    }

    remaining = req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, (char *)(body + offset), remaining);

        if (received <= 0) {
            heap_caps_free(body);
            *out_err = received == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
            return NULL;
        }
        offset += (size_t)received;
        remaining -= (size_t)received;
    }

    *out_len = offset;
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

    (void)ctx;

    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES; ++i) {
        esp32_mquickjs_http_server_route_t *route = &s_http_server_state.routes[i];

        if (!route->allocated || route->server_id != server_id || route->uri == NULL) {
            continue;
        }
        if (route->method != HTTP_ANY && route->method != method) {
            continue;
        }
        if (route->match_with_regex) {
            if (http_server_glob_match(route->uri, path != NULL ? path : "")) {
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

    if (server == NULL || !server->allocated || server->release_pending ||
        s_http_server_state.shutting_down) {
        if (req != NULL) {
            http_server_send_error(req, 503, "server shutting down");
        }
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

    request->body = http_server_recv_body(
        async_req, ESP32_MQUICKJS_HTTP_SERVER_MAX_BODY_LEN,
        &request->body_len, &err);
    if (request->body == NULL && err != ESP_OK) {
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
    if (server->events == NULL ||
        !esp32_mquickjs_event_queue_send(server->events, &event)) {
        http_server_send_error(async_req, 503, "request queue full");
        http_server_cleanup_request(request);
        return ESP_OK;
    }

    return ESP_OK;
}

static bool http_server_make_request_object(JSContext *ctx,
                                            esp32_mquickjs_http_server_request_t *request,
                                            const esp32_mquickjs_http_server_route_t *route,
                                            JSValue *out_obj)
{
    JSGCRef global_ref;
    JSGCRef query_ref;
    JSGCRef headers_ref;
    JSGCRef headers_value_ref;
    JSGCRef body_ref;
    JSValue *global_obj;
    JSValue *query_obj;
    JSValue *headers_plain;
    JSValue *headers_value;
    JSValue *body_value;
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
    headers_value = JS_PushGCRef(ctx, &headers_value_ref);
    body_value = JS_PushGCRef(ctx, &body_ref);
    *global_obj = JS_GetGlobalObject(ctx);
    *query_obj = JS_NewObject(ctx);
    *headers_plain = JS_NewObject(ctx);
    *headers_value = JS_UNDEFINED;
    *body_value = JS_UNDEFINED;
    if (JS_IsException(*global_obj) || JS_IsException(*query_obj) || JS_IsException(*headers_plain)) {
        goto fail;
    }

    for (i = 0; i < request->header_count; ++i) {
        if (!esp32_mquickjs_set_property_ref(ctx,
                                         headers_plain,
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
                !esp32_mquickjs_set_property_ref(ctx, query_obj, key, JS_NewString(ctx, value))) {
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

    *headers_value = esp32_mquickjs_make_headers_object(ctx, *global_obj, *headers_plain);
    if (JS_IsException(*headers_value)) {
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

    *body_value = esp32_mquickjs_stream_open_memory_owned_binary(
        ctx, *global_obj, request->body, request->body_len);
    request->body = NULL;
    request->body_len = 0;
    if (JS_IsException(*body_value)) {
        goto fail;
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
                                                  *headers_value,
                                                  *body_value);
    heap_caps_free(url);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_value_ref);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &query_ref);
    JS_PopGCRef(ctx, &global_ref);
    return !JS_IsException(*out_obj);

fail:
    heap_caps_free(url);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_value_ref);
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
                                     JSValue *value,
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
    if (JS_IsUndefined(*value) || JS_IsNull(*value)) {
        return 0;
    }
    if (esp32_mquickjs_is_headers_object(ctx, *value)) {
        *value = esp32_mquickjs_headers_to_plain_object(ctx, *value);
        if (JS_IsException(*value)) {
            return -1;
        }
    }
    if (!http_server_is_object(ctx, *value)) {
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
    *keys_array = http_server_call_function(ctx, *keys_fn, *object_ctor, 1, value);
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
        char *key_copy;

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
        key_copy = http_server_strdup(key_str);
        if (key_copy == NULL) {
            JS_PopGCRef(ctx, &val_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto fail;
        }
        headers[i].key = key_copy;
        *val = JS_GetPropertyStr(ctx, *value, key_copy);
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

        headers[i].value = http_server_strdup(val_str);
        JS_PopGCRef(ctx, &val_ref);
        JS_PopGCRef(ctx, &key_ref);
        if (headers[i].value == NULL) {
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
                                     JSValue *value,
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
    out_response->has_known_length = true;

    if (!esp32_mquickjs_is_response_object(ctx, *value)) {
        JS_ThrowTypeError(ctx, "http.server handlers must return a Response");
        return -1;
    }

    status_value = JS_PushGCRef(ctx, &status_ref);
    headers_value = JS_PushGCRef(ctx, &headers_ref);
    body_value = JS_PushGCRef(ctx, &body_ref);
    status_text_value = JS_PushGCRef(ctx, &status_text_ref);

    *status_value = JS_GetPropertyStr(ctx, *value, "status");
    *headers_value = JS_GetPropertyStr(ctx, *value, "headers");
    *body_value = JS_GetPropertyStr(ctx, *value, "body");
    *status_text_value = JS_GetPropertyStr(ctx, *value, "statusText");
    if (JS_IsException(*status_value) || JS_IsException(*headers_value) || JS_IsException(*body_value) ||
        JS_IsException(*status_text_value)) {
        goto fail;
    }
    if (http_server_parse_status(ctx, *status_value, &out_response->status) != 0) {
        goto fail;
    }
    if (http_server_parse_headers(ctx,
                                  headers_value,
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
        out_response->body_binary =
            esp32_mquickjs_stream_is_binary(&out_response->body_stream_ref);
        out_response->has_known_length = esp32_mquickjs_stream_known_length(
            &out_response->body_stream_ref, &out_response->known_length);
    }

    {
        size_t i;

        for (i = 0; i < out_response->header_count; ++i) {
            const char *cursor;
            char *end = NULL;
            unsigned long long parsed;

            if (strcasecmp(out_response->headers[i].key,
                           "Content-Length") != 0) {
                continue;
            }
            if (!out_response->has_known_length) {
                JS_ThrowTypeError(
                    ctx,
                    "Response Content-Length requires a body with known length");
                goto fail;
            }
            cursor = out_response->headers[i].value != NULL
                         ? out_response->headers[i].value
                         : "";
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            errno = 0;
            parsed = strtoull(cursor, &end, 10);
            while (end != NULL && isspace((unsigned char)*end)) {
                end++;
            }
            if (*cursor == '\0' || *cursor == '-' || errno != 0 ||
                end == cursor || end == NULL || *end != '\0' ||
                parsed > SIZE_MAX) {
                JS_ThrowTypeError(
                    ctx, "Response Content-Length must be a non-negative integer");
                goto fail;
            }
            if ((size_t)parsed != out_response->known_length) {
                JS_ThrowRangeError(
                    ctx,
                    "Response Content-Length (%llu) does not match the body (%u bytes)",
                    parsed, (unsigned)out_response->known_length);
                goto fail;
            }
        }
    }

    if (out_response->has_body_stream &&
        esp32_mquickjs_fs_stream_acquire(
            &out_response->body_stream_ref) != ESP_OK) {
        JS_ThrowInternalError(
            ctx, "Response.body stream is already in use");
        goto fail;
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

static bool http_server_has_header(
    const esp32_mquickjs_http_server_response_t *response,
    const char *name)
{
    size_t i;

    for (i = 0; i < response->header_count; ++i) {
        if (strcasecmp(response->headers[i].key, name) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t http_server_close_response_body(
    esp32_mquickjs_http_server_response_t *response)
{
    esp_err_t err;

    if (response == NULL || !response->has_body_stream) {
        return ESP_OK;
    }
    err = esp32_mquickjs_fs_stream_close(&response->body_stream_ref);
    response->has_body_stream = false;
    return err;
}

static esp_err_t http_server_raw_send_all(httpd_req_t *req,
                                          const char *data,
                                          size_t length)
{
    while (length > 0) {
        int sent = httpd_send(req, data, length);

        if (sent <= 0) {
            return ESP_ERR_HTTPD_RESP_SEND;
        }
        data += sent;
        length -= (size_t)sent;
    }
    return ESP_OK;
}

static esp_err_t http_server_send_known_length_response(
    httpd_req_t *req,
    esp32_mquickjs_http_server_response_t *response)
{
    char line[192];
    uint8_t *chunk_buffer = NULL;
    size_t sent_body = 0;
    size_t i;
    int line_len;
    esp_err_t err;

    line_len = snprintf(
        line, sizeof(line), "HTTP/1.1 %" PRId32 " %s\r\n",
        response->status,
        response->status_text != NULL
            ? response->status_text
            : http_server_status_text(response->status));
    if (line_len < 0 || (size_t)line_len >= sizeof(line)) {
        return ESP_ERR_HTTPD_RESP_HDR;
    }
    err = http_server_raw_send_all(req, line, (size_t)line_len);
    if (err != ESP_OK) {
        return err;
    }
    if (response->has_body_stream && !response->body_binary &&
        !http_server_has_content_type(response)) {
        static const char default_type[] =
            "Content-Type: text/plain; charset=utf-8\r\n";

        err = http_server_raw_send_all(req, default_type,
                                       sizeof(default_type) - 1);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (!http_server_has_header(response, "Content-Length")) {
        line_len = snprintf(line, sizeof(line), "Content-Length: %u\r\n",
                            (unsigned)response->known_length);
        if (line_len < 0 || (size_t)line_len >= sizeof(line)) {
            return ESP_ERR_HTTPD_RESP_HDR;
        }
        err = http_server_raw_send_all(req, line, (size_t)line_len);
        if (err != ESP_OK) {
            return err;
        }
    }
    for (i = 0; i < response->header_count; ++i) {
        err = http_server_raw_send_all(req, response->headers[i].key,
                                       strlen(response->headers[i].key));
        if (err == ESP_OK) {
            err = http_server_raw_send_all(req, ": ", 2);
        }
        if (err == ESP_OK) {
            err = http_server_raw_send_all(
                req, response->headers[i].value,
                strlen(response->headers[i].value));
        }
        if (err == ESP_OK) {
            err = http_server_raw_send_all(req, "\r\n", 2);
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    err = http_server_raw_send_all(req, "\r\n", 2);
    if (err != ESP_OK || !response->has_body_stream) {
        return err;
    }
    if (req->method == HTTP_HEAD) {
        return http_server_close_response_body(response);
    }

    chunk_buffer = esp32_mquickjs_memory_payload_alloc(
        ESP32_MQUICKJS_HTTP_SERVER_STREAM_CHUNK_SIZE,
        ESP32_MQUICKJS_MEMORY_EXTERNAL);
    if (chunk_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    while (sent_body < response->known_length) {
        size_t read_len = 0;

        err = esp32_mquickjs_fs_stream_read(
            &response->body_stream_ref, chunk_buffer,
            ESP32_MQUICKJS_HTTP_SERVER_STREAM_CHUNK_SIZE, &read_len);
        if (err != ESP_OK || read_len == 0 ||
            read_len > response->known_length - sent_body) {
            heap_caps_free(chunk_buffer);
            (void)http_server_close_response_body(response);
            return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
        }
        err = http_server_raw_send_all(req, (const char *)chunk_buffer,
                                       read_len);
        if (err != ESP_OK) {
            heap_caps_free(chunk_buffer);
            (void)http_server_close_response_body(response);
            return err;
        }
        sent_body += read_len;
    }
    {
        uint8_t trailing_byte;
        size_t trailing_len = 0;

        err = esp32_mquickjs_fs_stream_read(
            &response->body_stream_ref, &trailing_byte, 1, &trailing_len);
        if (err != ESP_OK || trailing_len != 0) {
            heap_caps_free(chunk_buffer);
            (void)http_server_close_response_body(response);
            return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
        }
    }
    heap_caps_free(chunk_buffer);
    return http_server_close_response_body(response);
}

static esp_err_t http_server_send_response(httpd_req_t *req,
                                           esp32_mquickjs_http_server_response_t *response)
{
    char status_line[32];
    uint8_t *chunk_buffer = NULL;
    size_t i;
    esp_err_t err;

    if (response->has_known_length) {
        return http_server_send_known_length_response(req, response);
    }

    snprintf(status_line,
             sizeof(status_line),
             "%" PRId32 " %s",
             response->status,
             response->status_text != NULL ? response->status_text : http_server_status_text(response->status));
    err = httpd_resp_set_status(req, status_line);
    if (err != ESP_OK) {
        return err;
    }
    if (response->has_body_stream && !response->body_binary &&
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
            err = http_server_close_response_body(response);
            if (err != ESP_OK) {
                return err;
            }
            return httpd_resp_send_chunk(req, NULL, 0);
        }
        chunk_buffer = esp32_mquickjs_memory_payload_alloc(
            ESP32_MQUICKJS_HTTP_SERVER_STREAM_CHUNK_SIZE,
            ESP32_MQUICKJS_MEMORY_EXTERNAL);
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
                (void)http_server_close_response_body(response);
                return err;
            }
            if (read_len > 0) {
                err = httpd_resp_send_chunk(req, (const char *)chunk_buffer, read_len);
                if (err != ESP_OK) {
                    heap_caps_free(chunk_buffer);
                    (void)http_server_close_response_body(response);
                    return err;
                }
            }
        } while (read_len > 0);
        heap_caps_free(chunk_buffer);
        err = http_server_close_response_body(response);
        if (err != ESP_OK) {
            return err;
        }
        return httpd_resp_send_chunk(req, NULL, 0);
    }
    return httpd_resp_send(req, NULL, 0);
}

static int http_server_native_stop(void *handle, void *opaque)
{
    (void)opaque;
    return (int)httpd_stop((httpd_handle_t)handle);
}

static const esp32_mquickjs_http_server_resource_ops_t
    s_http_server_resource_ops = {
        .stop = http_server_native_stop,
        .opaque = NULL,
    };

static esp_err_t http_server_start_slot(
    esp32_mquickjs_http_server_slot_t *server)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    struct ifreq ifr;
    esp_err_t err;

    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (server->started) {
        return ESP_OK;
    }
    if (server->handle != NULL) {
        err = http_server_stop_slot(server);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = esp32_mquickjs_net_ensure_initialized();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network runtime initialization failed before HTTP server start: %s",
                 esp_err_to_name(err));
        return err;
    }

    config.server_port = server->port;
    config.ctrl_port = server->ctrl_port;
    config.uri_match_fn = httpd_uri_match_wildcard;
    if (!http_server_resolve_ifreq_for_host(server->host, &ifr)) {
        ESP_LOGE(TAG, "failed to resolve HTTP server host binding for %s",
                 server->host != NULL ? server->host : "(null)");
        return ESP_ERR_INVALID_ARG;
    }
    if (!http_server_host_is_any(server->host)) {
        config.if_name = &ifr;
    }
    err = httpd_start(&server->handle, &config);
    if (err != ESP_OK) {
        return err;
    }

    memset(&server->dispatch_uri, 0, sizeof(server->dispatch_uri));
    server->dispatch_uri.uri = "/*";
    server->dispatch_uri.method = HTTP_ANY;
    server->dispatch_uri.handler = http_server_dispatch_handler;
    server->dispatch_uri.user_ctx = server;
    err = httpd_register_uri_handler(server->handle, &server->dispatch_uri);
    if (err != ESP_OK) {
        esp_err_t stop_err = http_server_stop_slot(server);

        return stop_err != ESP_OK ? stop_err : err;
    }

    server->started = true;
    return ESP_OK;
}

static esp_err_t http_server_stop_slot(
    esp32_mquickjs_http_server_slot_t *server)
{
    esp32_mquickjs_http_server_resources_t resources;
    esp_err_t err;
    int i;

    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    resources.handle = server->handle;
    err = (esp_err_t)esp32_mquickjs_http_server_resources_stop(
        &resources, &s_http_server_resource_ops);
    server->handle = (httpd_handle_t)resources.handle;
    if (err != ESP_OK) {
        return err;
    }

    server->started = false;
    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES; ++i) {
        if (s_http_server_state.routes[i].allocated &&
            s_http_server_state.routes[i].server_id == server->server_id) {
            s_http_server_state.routes[i].registered = false;
        }
    }
    return ESP_OK;
}

static int http_server_clear_routes_for_server(JSContext *ctx, uint8_t server_id)
{
    int cleared = 0;
    int i;

    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES; ++i) {
        if (s_http_server_state.routes[i].allocated &&
            s_http_server_state.routes[i].server_id == server_id) {
            http_server_cleanup_route(ctx, &s_http_server_state.routes[i]);
            cleared++;
        }
    }
    return cleared;
}

static void http_server_cleanup_requests_for_server(uint8_t server_id)
{
    int i;

    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN; ++i) {
        if (s_http_server_state.requests[i].allocated &&
            s_http_server_state.requests[i].server_id == server_id) {
            http_server_cleanup_request(&s_http_server_state.requests[i]);
        }
    }
}

static bool http_server_has_active_response(uint8_t server_id)
{
    int i;

    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN; ++i) {
        if (s_http_server_state.requests[i].allocated &&
            s_http_server_state.requests[i].server_id == server_id &&
            s_http_server_state.requests[i].responding) {
            return true;
        }
    }
    return false;
}

static esp_err_t http_server_close_slot(
    JSContext *ctx, esp32_mquickjs_http_server_slot_t *server)
{
    esp_err_t err;

    if (server == NULL) {
        return ESP_OK;
    }
    err = http_server_stop_slot(server);
    if (err != ESP_OK) {
        return err;
    }
    if (server->events != NULL) {
        esp32_mquickjs_event_queue_close(server->events);
    }
    http_server_cleanup_requests_for_server(server->server_id);
    (void)http_server_clear_routes_for_server(ctx, server->server_id);
    http_server_cleanup_server(server);
    return ESP_OK;
}

static esp_err_t http_server_cleanup_all(JSContext *ctx)
{
    esp_err_t err;
    int i;

    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_SERVERS; ++i) {
        esp32_mquickjs_http_server_slot_t *server =
            &s_http_server_state.servers[i];

        if (!server->allocated) {
            continue;
        }
        server->release_pending = true;
        err = http_server_close_slot(ctx, server);
        if (err != ESP_OK) {
            return err;
        }
    }
    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN; ++i) {
        http_server_cleanup_request(&s_http_server_state.requests[i]);
    }
    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES; ++i) {
        http_server_cleanup_route(ctx, &s_http_server_state.routes[i]);
    }
    return ESP_OK;
}

static int http_server_server_id_from_object(JSContext *ctx,
                                             JSValue server_value,
                                             const char *api_name,
                                             int32_t *out_server_id)
{
    esp32_mquickjs_http_server_ref_t *ref;
    esp32_mquickjs_http_server_slot_t *server;

    if (out_server_id == NULL || JS_GetClassID(ctx, server_value) != JS_CLASS_HTTP_SERVER) {
        JS_ThrowTypeError(ctx, "%s expects an HttpServer instance", api_name);
        return -1;
    }

    ref = JS_GetOpaque(ctx, server_value);
    if (ref == NULL) {
        JS_ThrowTypeError(ctx, "%s expects a valid HttpServer instance", api_name);
        return -1;
    }

    server = http_server_get_slot(ref->server_id);
    if (server == NULL || server->generation != ref->generation ||
        server->release_pending) {
        JS_ThrowInternalError(ctx, "%s cannot use a closed or stale HttpServer", api_name);
        return -1;
    }
    *out_server_id = ref->server_id;
    return 0;
}

static JSValue http_server_event_to_js(JSContext *ctx,
                                       const void *event_value,
                                       void *opaque)
{
    JSGCRef result_ref;
    const esp32_mquickjs_http_server_event_t *event = event_value;
    const esp32_mquickjs_http_server_event_source_t *source = opaque;
    esp32_mquickjs_http_server_request_t *request;
    esp32_mquickjs_http_server_route_t *route;
    JSValue *result;

    if (event == NULL || source == NULL ||
        event->request_id >= ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN) {
        return JS_NULL;
    }
    request = &s_http_server_state.requests[event->request_id];
    if (!request->allocated || request->generation != event->generation ||
        request->server_id != source->server_id ||
        http_server_get_slot(source->server_id) == NULL ||
        http_server_get_slot(source->server_id)->generation != source->generation) {
        return JS_NULL;
    }
    route = http_server_find_route(ctx,
                                   request->server_id,
                                   request->method,
                                   request->path);
    if (route == NULL) {
        http_server_send_error(request->async_req, 404,
                               "Nothing matches the given URI");
        http_server_cleanup_request(request);
        return JS_NULL;
    }
    result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_UNDEFINED;
    if (!http_server_make_request_object(ctx, request, route, result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "_requestId",
                                         JS_NewInt32(ctx, request->request_id)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "_requestGeneration",
                                         JS_NewUint32(ctx, request->generation))) {
        http_server_send_error(request->async_req, 500,
                               "failed to build request object");
        http_server_cleanup_request(request);
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static void http_server_event_drop(void *event_value, void *opaque)
{
    const esp32_mquickjs_http_server_event_t *event = event_value;
    const esp32_mquickjs_http_server_event_source_t *source = opaque;
    esp32_mquickjs_http_server_request_t *request;

    if (event == NULL || source == NULL ||
        event->request_id >= ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN) {
        return;
    }
    request = &s_http_server_state.requests[event->request_id];
    if (!request->allocated || request->generation != event->generation ||
        request->server_id != source->server_id) {
        return;
    }
    http_server_send_error(request->async_req, 503, "request queue full");
    http_server_cleanup_request(request);
}

static void http_server_event_close(void *opaque)
{
    esp32_mquickjs_http_server_event_source_t *source = opaque;
    esp32_mquickjs_http_server_slot_t *server;

    if (source == NULL) {
        return;
    }
    server = http_server_get_slot(source->server_id);
    if (server != NULL && server->generation == source->generation) {
        server->events = NULL;
    }
    heap_caps_free(source);
}

static JSValue http_server_make_server_object(JSContext *ctx, JSValue global_obj, int32_t server_id)
{
    JSGCRef server_ref;
    JSGCRef events_ref;
    JSValue *server_obj;
    JSValue *events_obj;
    esp32_mquickjs_http_server_slot_t *server = http_server_get_slot(server_id);
    esp32_mquickjs_http_server_event_source_t *source = NULL;
    esp32_mquickjs_http_server_ref_t *ref = NULL;

    (void)global_obj;
    server_obj = JS_PushGCRef(ctx, &server_ref);
    events_obj = JS_PushGCRef(ctx, &events_ref);
    *server_obj = JS_NewObjectClassUser(ctx, JS_CLASS_HTTP_SERVER);
    *events_obj = JS_UNDEFINED;
    if (JS_IsException(*server_obj) || server == NULL) {
        goto fail;
    }
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ref->server_id = (uint8_t)server_id;
    ref->generation = server->generation;

    source = heap_caps_calloc(1, sizeof(*source), MALLOC_CAP_8BIT);
    if (source == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    source->server_id = (uint8_t)server_id;
    source->generation = server->generation;
    *events_obj = esp32_mquickjs_event_queue_new(
        ctx,
        s_http_server_state.runtime,
        sizeof(esp32_mquickjs_http_server_event_t),
        ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
        http_server_event_to_js,
        http_server_event_drop,
        http_server_event_close,
        source);
    if (JS_IsException(*events_obj)) {
        heap_caps_free(source);
        source = NULL;
        goto fail;
    }
    source = NULL;
    server->events = esp32_mquickjs_event_queue_from_value(ctx, *events_obj);
    if (server->events == NULL) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, server_obj, "port", JS_NewInt32(ctx, server->port)) ||
        !esp32_mquickjs_set_property_ref(ctx, server_obj, "ctrlPort", JS_NewInt32(ctx, server->ctrl_port)) ||
        !esp32_mquickjs_set_property_ref(ctx,
                                     server_obj,
                                     "host",
                                     JS_NewString(ctx,
                                                  server->host != NULL
                                                      ? server->host
                                                      : "0.0.0.0")) ||
        !esp32_mquickjs_set_property_ref(ctx, server_obj, "started", JS_NewBool(server->started)) ||
        !esp32_mquickjs_set_property_ref(ctx, server_obj, "closed", JS_FALSE) ||
        !esp32_mquickjs_set_property_ref(ctx, server_obj, "_eventQueue", *events_obj)) {
        goto fail;
    }
    JS_SetOpaque(ctx, *server_obj, ref);
    ref = NULL;

    JS_PopGCRef(ctx, &events_ref);
    return JS_PopGCRef(ctx, &server_ref);

fail:
    heap_caps_free(ref);
    heap_caps_free(source);
    JS_PopGCRef(ctx, &events_ref);
    JS_PopGCRef(ctx, &server_ref);
    return JS_EXCEPTION;
}

struct esp32_mquickjs_future_driver_state {
    JSContext *ctx;
    JSGCRef request_ref;
    JSGCRef server_ref;
    esp32_mquickjs_http_server_response_t response;
    uint8_t server_id;
    uint8_t request_id;
    uint32_t request_generation;
    esp_err_t send_result;
    _Atomic bool worker_completed;
    bool request_retained;
    bool server_retained;
    bool request_claimed;
    bool started;
    bool finished;
    bool cancelled;
};

static esp32_mquickjs_http_server_request_t *http_server_respond_request(
    const esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_http_server_request_t *request;

    if (state == NULL ||
        state->request_id >= ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN) {
        return NULL;
    }
    request = &s_http_server_state.requests[state->request_id];
    return request->allocated &&
                   request->generation == state->request_generation &&
                   request->server_id == state->server_id
               ? request
               : NULL;
}

static void http_server_respond_release(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    http_server_free_response(&state->response);
    if (state->request_retained) {
        JS_DeleteGCRef(state->ctx, &state->request_ref);
        state->request_retained = false;
    }
    if (state->server_retained) {
        JS_DeleteGCRef(state->ctx, &state->server_ref);
        state->server_retained = false;
    }
    heap_caps_free(state);
}

static bool http_server_respond_future_capture(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_http_server_request_t *request;
    JSGCRef request_id_ref;
    JSGCRef generation_ref;
    JSValue *request_id_value;
    JSValue *generation_value;
    uint32_t generation = 0;
    int request_id = -1;
    int32_t server_id = -1;

    if (out_state == NULL || argc != 2 || this_ref == NULL ||
        http_server_server_id_from_object(
            ctx, this_ref->val, "server.respond", &server_id) != 0) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(
                ctx, "server.respond(request, response) expects two arguments");
        }
        return false;
    }
    request_id_value = JS_PushGCRef(ctx, &request_id_ref);
    generation_value = JS_PushGCRef(ctx, &generation_ref);
    *request_id_value = JS_GetPropertyStr(ctx, argv[0].val, "_requestId");
    *generation_value = JS_GetPropertyStr(
        ctx, argv[0].val, "_requestGeneration");
    if (JS_IsException(*request_id_value) ||
        JS_IsException(*generation_value) ||
        JS_ToInt32(ctx, &request_id, *request_id_value) != 0 ||
        JS_ToUint32(ctx, &generation, *generation_value) != 0 ||
        request_id < 0 ||
        request_id >= ESP32_MQUICKJS_HTTP_SERVER_QUEUE_LEN) {
        JS_PopGCRef(ctx, &generation_ref);
        JS_PopGCRef(ctx, &request_id_ref);
        JS_ThrowTypeError(
            ctx,
            "server.respond expects a live request returned by server.receive");
        return false;
    }
    JS_PopGCRef(ctx, &generation_ref);
    JS_PopGCRef(ctx, &request_id_ref);
    request = &s_http_server_state.requests[request_id];
    if (!request->allocated || request->generation != generation ||
        request->server_id != (uint8_t)server_id ||
        request->async_req == NULL || request->responding) {
        JS_ThrowInternalError(
            ctx, "server.respond cannot use a completed or stale request");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    atomic_init(&state->worker_completed, false);
    state->ctx = ctx;
    state->server_id = (uint8_t)server_id;
    state->request_id = (uint8_t)request_id;
    state->request_generation = generation;
    state->send_result = ESP_FAIL;
    if (http_server_make_response(ctx, &argv[1].val, &state->response) != 0) {
        http_server_respond_release(state);
        return false;
    }
    *JS_AddGCRef(ctx, &state->request_ref) = argv[0].val;
    state->request_retained = true;
    *JS_AddGCRef(ctx, &state->server_ref) = this_ref->val;
    state->server_retained = true;
    request->responding = true;
    state->request_claimed = true;
    http_server_close_request_body(ctx, argv[0].val);
    if (!esp32_mquickjs_set_property_ref(
            ctx, &state->request_ref.val, "_requestId", JS_NewInt32(ctx, -1))) {
        request->responding = false;
        state->request_claimed = false;
        http_server_respond_release(state);
        return false;
    }
    *out_state = state;
    return true;
}

static void http_server_respond_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    esp32_mquickjs_http_server_request_t *request =
        http_server_respond_request(state);

    if (state != NULL && request != NULL && request->async_req != NULL) {
        state->send_result = http_server_send_response(
            request->async_req, &state->response);
    }
    if (state != NULL) {
        atomic_store_explicit(
            &state->worker_completed, true, memory_order_release);
    }
}

static bool http_server_respond_future_start(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_http_server_request_t *request =
        http_server_respond_request(state);

    if (state == NULL || request == NULL || !request->responding ||
        request->async_req == NULL || s_http_server_state.shutting_down) {
        JS_ThrowReferenceError(
            ctx, "HTTP request closed before response started");
        return false;
    }
    state->started = true;
    if (!esp32_mquickjs_future_submit_worker(
            runtime, token, http_server_respond_worker, state)) {
        state->started = false;
        JS_ThrowInternalError(ctx, "HTTP response worker queue is full");
        return false;
    }
    return true;
}

static esp32_mquickjs_future_poll_t http_server_respond_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && atomic_load_explicit(
                                &state->worker_completed,
                                memory_order_acquire)
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue http_server_respond_future_finish(
    JSContext *ctx,
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_http_server_request_t *request =
        http_server_respond_request(state);
    esp_err_t send_result;

    if (state == NULL || state->cancelled) {
        return JS_ThrowInternalError(ctx, "HTTP response was cancelled");
    }
    send_result = state->send_result;
    http_server_free_response(&state->response);
    if (request != NULL) {
        request->responding = false;
        http_server_cleanup_request(request);
    }
    state->request_claimed = false;
    state->finished = true;
    if (send_result != ESP_OK) {
        return JS_ThrowInternalError(ctx, "failed to send HTTP response");
    }
    return JS_TRUE;
}

static esp32_mquickjs_cancel_result_t http_server_respond_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->finished || state->cancelled) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    if (state->started) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancelled = true;
    atomic_store_explicit(
        &state->worker_completed, true, memory_order_release);
    return ESP32_MQUICKJS_CANCELLED;
}

static void http_server_respond_future_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_http_server_request_t *request =
        http_server_respond_request(state);

    if (state == NULL) {
        return;
    }
    if (state->request_claimed && request != NULL) {
        if (state->started && atomic_load_explicit(
                                  &state->worker_completed,
                                  memory_order_acquire)) {
            request->responding = false;
            http_server_cleanup_request(request);
        } else {
            request->responding = false;
            if (state->request_retained) {
                (void)esp32_mquickjs_set_property_ref(
                    state->ctx, &state->request_ref.val, "_requestId",
                    JS_NewInt32(state->ctx, state->request_id));
            }
        }
    }
    http_server_respond_release(state);
}

static const esp32_mquickjs_future_driver_t s_http_server_respond_driver = {
    .capture = http_server_respond_future_capture,
    .start = http_server_respond_future_start,
    .poll = http_server_respond_future_poll,
    .finish = http_server_respond_future_finish,
    .cancel = http_server_respond_future_cancel,
    .destroy = http_server_respond_future_destroy,
};

bool esp32_mquickjs_init_http_server_runtime(JSContext *ctx,
                                             esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef object_ref;
    JSGCRef receive_ref;
    JSGCRef respond_ref;
    JSValue *object;
    JSValue *receive;
    JSValue *respond;
    bool registered;
    esp_err_t err;

    err = http_server_init_state(ctx, runtime);
    if (err != ESP_OK) {
        JS_ThrowInternalError(ctx,
                              "HTTP server runtime cleanup failed: %s",
                              esp_err_to_name(err));
        return false;
    }
    object = JS_PushGCRef(ctx, &object_ref);
    receive = JS_PushGCRef(ctx, &receive_ref);
    respond = JS_PushGCRef(ctx, &respond_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_HTTP_SERVER);
    *receive = JS_IsException(*object)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *object, "receive");
    *respond = JS_IsException(*object)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *object, "respond");
    registered = !JS_IsException(*receive) && !JS_IsException(*respond) &&
                 esp32_mquickjs_event_queue_register_receive_alias(
                     ctx, runtime, *receive) &&
                 esp32_mquickjs_future_register_driver(
                     ctx, runtime, *respond, &s_http_server_respond_driver);
    if (!registered && !JS_IsException(*object) &&
        !JS_IsException(*receive) && !JS_IsException(*respond)) {
        JS_ThrowInternalError(
            ctx, "failed to register HttpServer Future drivers");
    }
    JS_PopGCRef(ctx, &respond_ref);
    JS_PopGCRef(ctx, &receive_ref);
    JS_PopGCRef(ctx, &object_ref);
    return registered;
}

JSValue js_http_server_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "HttpServer cannot be constructed directly");
}

void js_http_server_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_http_server_ref_t *ref = opaque;
    esp32_mquickjs_http_server_slot_t *server;

    if (ref == NULL) {
        return;
    }
    server = http_server_get_slot(ref->server_id);
    if (server != NULL && server->generation == ref->generation) {
        server->release_pending = true;
        (void)http_server_close_slot(ctx, server);
    }
    heap_caps_free(ref);
}

JSValue js_http_server_start(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int32_t server_id = -1;
    esp32_mquickjs_http_server_slot_t *server;
    bool was_started;
    esp_err_t err;

    (void)argc;
    (void)argv;
    if (http_server_server_id_from_object(ctx, *this_val, "server.start", &server_id) != 0) {
        return JS_EXCEPTION;
    }
    server = http_server_get_slot(server_id);
    was_started = server->started;
    err = http_server_start_slot(server);
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx,
                                     "failed to start HTTP server: %s",
                                     esp_err_to_name(err));
    }
    esp32_mquickjs_set_property_ref(ctx, this_val, "started", JS_TRUE);
    return JS_NewBool(!was_started);
}

JSValue js_http_server_stop(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int32_t server_id = -1;
    esp32_mquickjs_http_server_slot_t *server;
    bool was_started;
    esp_err_t err;

    (void)argc;
    (void)argv;
    if (http_server_server_id_from_object(ctx, *this_val, "server.stop", &server_id) != 0) {
        return JS_EXCEPTION;
    }
    if (http_server_has_active_response((uint8_t)server_id)) {
        return JS_ThrowInternalError(
            ctx, "server.stop() refused while a response is pending");
    }
    server = http_server_get_slot(server_id);
    was_started = server->started;
    err = http_server_stop_slot(server);
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx,
                                     "failed to stop HTTP server: %s",
                                     esp_err_to_name(err));
    }
    esp32_mquickjs_set_property_ref(ctx, this_val, "started", JS_FALSE);
    return JS_NewBool(was_started);
}

JSValue js_http_server_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef closed_ref;
    JSValue *closed_value;
    esp32_mquickjs_http_server_ref_t *ref;
    esp32_mquickjs_http_server_slot_t *server;
    int32_t server_id = -1;
    esp_err_t err;

    (void)argc;
    (void)argv;
    if (JS_GetClassID(ctx, *this_val) != JS_CLASS_HTTP_SERVER) {
        return JS_ThrowTypeError(ctx, "server.close expects an HttpServer instance");
    }
    closed_value = JS_PushGCRef(ctx, &closed_ref);
    *closed_value = JS_GetPropertyStr(ctx, *this_val, "closed");
    if (JS_IsException(*closed_value)) {
        JS_PopGCRef(ctx, &closed_ref);
        return JS_EXCEPTION;
    }
    if (*closed_value == JS_TRUE) {
        JS_PopGCRef(ctx, &closed_ref);
        return JS_FALSE;
    }
    JS_PopGCRef(ctx, &closed_ref);

    ref = JS_GetOpaque(ctx, *this_val);
    if (ref == NULL) {
        return JS_ThrowTypeError(
            ctx, "server.close expects a valid HttpServer instance");
    }
    server_id = ref->server_id;
    server = http_server_get_slot(server_id);
    if (server == NULL || server->generation != ref->generation) {
        return JS_ThrowInternalError(
            ctx, "server.close cannot use a closed or stale HttpServer");
    }
    if (http_server_has_active_response((uint8_t)server_id)) {
        return JS_ThrowInternalError(
            ctx, "server.close() refused while a response is pending");
    }
    server->release_pending = true;
    err = http_server_close_slot(ctx, server);
    if (err != ESP_OK) {
        return JS_ThrowInternalError(ctx,
                                     "failed to close HTTP server: %s",
                                     esp_err_to_name(err));
    }
    JS_SetOpaque(ctx, *this_val, NULL);
    heap_caps_free(ref);
    if (!esp32_mquickjs_set_property_ref(ctx, this_val, "started", JS_FALSE) ||
        !esp32_mquickjs_set_property_ref(ctx, this_val, "closed", JS_TRUE)) {
        return JS_EXCEPTION;
    }
    return JS_TRUE;
}

JSValue js_http_server_receive(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv)
{
    JSGCRef server_ref;
    JSGCRef events_ref;
    JSGCRef receive_ref;
    JSValue *server_obj = JS_PushGCRef(ctx, &server_ref);
    JSValue *events_obj = JS_PushGCRef(ctx, &events_ref);
    JSValue *receive_fn = JS_PushGCRef(ctx, &receive_ref);
    JSValue result;
    int32_t server_id = -1;

    *server_obj = this_val != NULL ? *this_val : JS_UNDEFINED;
    *events_obj = JS_UNDEFINED;
    *receive_fn = JS_UNDEFINED;
    if (http_server_server_id_from_object(ctx, *server_obj,
                                          "server.receive", &server_id) != 0) {
        result = JS_EXCEPTION;
        goto done;
    }
    *events_obj = JS_GetPropertyStr(ctx, *server_obj, "_eventQueue");
    *receive_fn = JS_IsException(*events_obj)
                      ? JS_EXCEPTION
                      : JS_GetPropertyStr(ctx, *events_obj, "receive");
    if (JS_IsException(*events_obj) || JS_IsException(*receive_fn)) {
        result = JS_EXCEPTION;
        goto done;
    }
    result = esp32_mquickjs_future_call_and_wait(
        ctx, esp32_mquickjs_get_active_runtime(), *receive_fn,
        *events_obj, argc, argv);

done:
    JS_PopGCRef(ctx, &receive_ref);
    JS_PopGCRef(ctx, &events_ref);
    JS_PopGCRef(ctx, &server_ref);
    return result;
}

JSValue js_http_server_stats(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv)
{
    JSGCRef events_ref;
    JSGCRef stats_ref;
    JSValue *events = JS_PushGCRef(ctx, &events_ref);
    JSValue *stats = JS_PushGCRef(ctx, &stats_ref);
    JSValue result;
    int32_t server_id = -1;

    (void)argc;
    (void)argv;
    if (this_val == NULL ||
        http_server_server_id_from_object(
            ctx, *this_val, "server.stats", &server_id) != 0) {
        result = JS_EXCEPTION;
        goto done;
    }
    *events = JS_GetPropertyStr(ctx, *this_val, "_eventQueue");
    *stats = JS_IsException(*events)
                 ? JS_EXCEPTION
                 : JS_GetPropertyStr(ctx, *events, "stats");
    result = JS_IsException(*events) || JS_IsException(*stats)
                 ? JS_EXCEPTION
                 : http_server_call_function(
                       ctx, *stats, *events, 0, NULL);

done:
    JS_PopGCRef(ctx, &stats_ref);
    JS_PopGCRef(ctx, &events_ref);
    return result;
}

JSValue js_http_server_clear_routes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int32_t server_id = -1;
    int cleared;

    (void)argc;
    (void)argv;
    if (http_server_server_id_from_object(ctx, *this_val, "server.clearRoutes", &server_id) != 0) {
        return JS_EXCEPTION;
    }
    cleared = http_server_clear_routes_for_server(ctx, (uint8_t)server_id);
    return JS_NewInt32(ctx, cleared);
}

JSValue js_http_server_remove_route(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_http_server_route_t pattern = {0};
    JSGCRef pattern_ref;
    JSValue *rooted_pattern;
    httpd_method_t method = HTTP_ANY;
    bool filter_method = false;
    int32_t server_id = -1;
    int removed = 0;
    int i;

    if (http_server_server_id_from_object(ctx, *this_val, "server.removeRoute", &server_id) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "server.removeRoute(path, method?) expects a string path");
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        JSCStringBuf method_buf;
        const char *method_name;

        if (!JS_IsString(ctx, argv[1])) {
            return JS_ThrowTypeError(ctx, "server.removeRoute(..., method) expects an HTTP method string");
        }
        method_name = JS_ToCString(ctx, argv[1], &method_buf);
        if (method_name == NULL) {
            return JS_EXCEPTION;
        }
        if (strcasecmp(method_name, "GET") == 0) {
            method = HTTP_GET;
        } else if (strcasecmp(method_name, "POST") == 0) {
            method = HTTP_POST;
        } else if (strcasecmp(method_name, "PUT") == 0) {
            method = HTTP_PUT;
        } else if (strcasecmp(method_name, "PATCH") == 0) {
            method = HTTP_PATCH;
        } else if (strcasecmp(method_name, "DELETE") == 0) {
            method = HTTP_DELETE;
        } else if (strcasecmp(method_name, "HEAD") == 0) {
            method = HTTP_HEAD;
        } else if (strcasecmp(method_name, "OPTIONS") == 0) {
            method = HTTP_OPTIONS;
        } else if (strcasecmp(method_name, "ANY") != 0 && strcasecmp(method_name, "ALL") != 0) {
            return JS_ThrowTypeError(ctx, "server.removeRoute(..., method) received an unsupported HTTP method");
        }
        filter_method = true;
    }

    rooted_pattern = JS_PushGCRef(ctx, &pattern_ref);
    *rooted_pattern = argv[0];
    pattern.allocated = true;
    if (!http_server_compile_route_pattern(ctx, rooted_pattern, &pattern)) {
        http_server_cleanup_route(ctx, &pattern);
        JS_PopGCRef(ctx, &pattern_ref);
        return JS_ThrowTypeError(ctx, "server.removeRoute(path, method?) expects a valid string path");
    }

    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES; ++i) {
        esp32_mquickjs_http_server_route_t *route = &s_http_server_state.routes[i];

        if (!route->allocated || route->server_id != (uint8_t)server_id ||
            route->match_with_regex != pattern.match_with_regex ||
            route->uri == NULL || pattern.uri == NULL || strcmp(route->uri, pattern.uri) != 0 ||
            (filter_method && route->method != method)) {
            continue;
        }
        http_server_cleanup_route(ctx, route);
        removed++;
    }

    http_server_cleanup_route(ctx, &pattern);
    JS_PopGCRef(ctx, &pattern_ref);
    return JS_NewInt32(ctx, removed);
}

JSValue js_http_server_route(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_http_server_route_t candidate = {0};
    esp32_mquickjs_http_server_route_t *route = NULL;
    JSGCRef path_ref;
    JSValue *path_value;
    JSCStringBuf method_buf;
    const char *method_name;
    httpd_method_t method;
    int32_t server_id = -1;
    int i;

    if (http_server_server_id_from_object(ctx, *this_val, "server.route", &server_id) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 2 || !JS_IsString(ctx, argv[0]) || !JS_IsString(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "server.route(method, path) expects two strings");
    }
    method_name = JS_ToCString(ctx, argv[0], &method_buf);
    if (method_name == NULL) {
        return JS_EXCEPTION;
    }
    if (strcasecmp(method_name, "GET") == 0) {
        method = HTTP_GET;
    } else if (strcasecmp(method_name, "POST") == 0) {
        method = HTTP_POST;
    } else if (strcasecmp(method_name, "PUT") == 0) {
        method = HTTP_PUT;
    } else if (strcasecmp(method_name, "PATCH") == 0) {
        method = HTTP_PATCH;
    } else if (strcasecmp(method_name, "DELETE") == 0) {
        method = HTTP_DELETE;
    } else if (strcasecmp(method_name, "HEAD") == 0) {
        method = HTTP_HEAD;
    } else if (strcasecmp(method_name, "OPTIONS") == 0) {
        method = HTTP_OPTIONS;
    } else if (strcasecmp(method_name, "ANY") == 0 || strcasecmp(method_name, "ALL") == 0) {
        method = HTTP_ANY;
    } else {
        return JS_ThrowTypeError(ctx, "server.route received an unsupported HTTP method");
    }

    path_value = JS_PushGCRef(ctx, &path_ref);
    *path_value = argv[1];
    candidate.allocated = true;
    candidate.server_id = (uint8_t)server_id;
    candidate.method = method;
    if (!http_server_compile_route_pattern(ctx, path_value, &candidate)) {
        http_server_cleanup_route(ctx, &candidate);
        JS_PopGCRef(ctx, &path_ref);
        return JS_ThrowTypeError(ctx, "server.route(method, path) expects a valid string path");
    }
    JS_PopGCRef(ctx, &path_ref);

    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_ROUTES; ++i) {
        if (!s_http_server_state.routes[i].allocated) {
            uint8_t route_id = s_http_server_state.routes[i].route_id;

            route = &s_http_server_state.routes[i];
            *route = candidate;
            route->route_id = route_id;
            memset(&candidate, 0, sizeof(candidate));
            break;
        }
    }
    if (route == NULL) {
        http_server_cleanup_route(ctx, &candidate);
        return JS_ThrowInternalError(ctx, "too many server routes");
    }
    return JS_TRUE;
}

static void http_server_close_request_body(JSContext *ctx, JSValue request_value)
{
    JSGCRef body_ref;
    JSValue *body_value = JS_PushGCRef(ctx, &body_ref);

    *body_value = JS_GetPropertyStr(ctx, request_value, "body");
    if (!JS_IsException(*body_value)) {
        esp32_mquickjs_stream_close_value(ctx, *body_value);
    }
    JS_PopGCRef(ctx, &body_ref);
}

JSValue js_http_server_respond(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *method = this_val != NULL
                  ? JS_GetPropertyStr(ctx, *this_val, "respond")
                  : JS_EXCEPTION;
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
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
    esp_err_t err;
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

    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_SERVERS; ++i) {
        server = &s_http_server_state.servers[i];
        if (!server->allocated || !server->release_pending) {
            continue;
        }
        err = http_server_close_slot(ctx, server);
        if (err != ESP_OK) {
            JS_PopGCRef(ctx, &global_ref);
            JS_PopGCRef(ctx, &host_ref);
            JS_PopGCRef(ctx, &port_ref);
            JS_PopGCRef(ctx, &options_ref);
            return JS_ThrowInternalError(
                ctx, "HTTP server pending cleanup failed: %s",
                esp_err_to_name(err));
        }
    }
    server = NULL;

    http_server_lock();
    for (i = 0; i < ESP32_MQUICKJS_HTTP_SERVER_MAX_SERVERS; ++i) {
        if (!s_http_server_state.servers[i].allocated) {
            server = &s_http_server_state.servers[i];
            server->allocated = true;
            server->generation++;
            if (server->generation == 0) {
                server->generation++;
            }
            server->port = (uint16_t)port;
            server->ctrl_port = (uint16_t)(ctrl_port + i);
            server->host = http_server_strdup(host);
            server->started = false;
            server->release_pending = false;
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

        if (JS_IsException(result)) {
            server->release_pending = true;
            (void)http_server_close_slot(ctx, server);
        }
        JS_PopGCRef(ctx, &global_ref);
        JS_PopGCRef(ctx, &host_ref);
        JS_PopGCRef(ctx, &port_ref);
        JS_PopGCRef(ctx, &options_ref);
        return result;
    }
}

void esp32_mquickjs_deinit_http_server_runtime(JSContext *ctx)
{
    esp_err_t err;

    if (!s_http_server_state.initialized) {
        http_server_reset_state();
        return;
    }

    s_http_server_state.shutting_down = true;
    s_http_server_state.runtime = NULL;
    err = http_server_cleanup_all(ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "HTTP server runtime cleanup retained for retry: %s",
                 esp_err_to_name(err));
        return;
    }
    http_server_reset_state();
}

#endif
