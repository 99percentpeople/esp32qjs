#include "esp32_mquickjs_websocket.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_net.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define WEBSOCKET_URL_MAX_LEN 512U
#define WEBSOCKET_AUTHORIZATION_MAX_LEN 512U
#define WEBSOCKET_SUBPROTOCOL_MAX_LEN 128U
#define WEBSOCKET_ERROR_TEXT_LEN 160U
#define WEBSOCKET_DEFAULT_RECONNECT_MS 5000
#define WEBSOCKET_DEFAULT_NETWORK_TIMEOUT_MS 10000
#define WEBSOCKET_DEFAULT_SEND_TIMEOUT_MS 1000
#define WEBSOCKET_DEFAULT_PING_INTERVAL_SEC 10
#define WEBSOCKET_OPCODE_TEXT 0x1
#define WEBSOCKET_OPCODE_CLOSE 0x8
#define WEBSOCKET_OPCODE_PING 0x9
#define WEBSOCKET_OPCODE_PONG 0xA

typedef enum {
    WEBSOCKET_CALLBACK_OPEN = 1,
    WEBSOCKET_CALLBACK_MESSAGE,
    WEBSOCKET_CALLBACK_CLOSE,
    WEBSOCKET_CALLBACK_ERROR,
} esp32_mquickjs_websocket_callback_kind_t;

typedef struct {
    esp32_mquickjs_websocket_callback_kind_t kind;
    uint32_t generation;
    char *data;
    size_t data_len;
    int32_t code;
    char message[WEBSOCKET_ERROR_TEXT_LEN];
} esp32_mquickjs_websocket_callback_event_t;

typedef struct {
    bool initialized;
    volatile bool opened;
    volatile bool connected;
    volatile bool closing;
    uint32_t generation;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_event_queue_t *event_queue;
    QueueHandle_t queue;
    esp_websocket_client_handle_t client;
    size_t max_message_bytes;
    uint32_t send_timeout_ms;
    char *fragment;
    size_t fragment_len;
    size_t fragment_expected;
    bool fragment_dropping;
    uint32_t opened_events;
    uint32_t received_messages;
    uint32_t sent_messages;
    uint32_t dropped_events;
    uint32_t oversized_messages;
    volatile bool sending;
    volatile bool close_pending;
} esp32_mquickjs_websocket_state_t;

static esp32_mquickjs_websocket_state_t s_websocket_state;

static bool websocket_poller(JSContext *ctx,
                             esp32_mquickjs_runtime_t *runtime,
                             void *opaque);
static void websocket_event_handler(void *handler_args,
                                    esp_event_base_t base,
                                    int32_t event_id,
                                    void *event_data);
static bool websocket_register_future_driver(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime);

static void websocket_free_callback_event(
    esp32_mquickjs_websocket_callback_event_t *event)
{
    if (event == NULL) {
        return;
    }
    heap_caps_free(event->data);
    event->data = NULL;
}

static void websocket_drain_queue(void)
{
    esp32_mquickjs_websocket_callback_event_t event;

    if (s_websocket_state.queue == NULL) {
        return;
    }
    while (xQueueReceive(s_websocket_state.queue, &event, 0) == pdTRUE) {
        websocket_free_callback_event(&event);
    }
}

static void websocket_reset_fragment(void)
{
    heap_caps_free(s_websocket_state.fragment);
    s_websocket_state.fragment = NULL;
    s_websocket_state.fragment_len = 0;
    s_websocket_state.fragment_expected = 0;
    s_websocket_state.fragment_dropping = false;
}

static void websocket_enqueue(esp32_mquickjs_websocket_callback_event_t *event)
{
    if (event == NULL || s_websocket_state.queue == NULL ||
        !s_websocket_state.opened || s_websocket_state.closing) {
        websocket_free_callback_event(event);
        return;
    }
    event->generation = s_websocket_state.generation;
    if (xQueueSend(s_websocket_state.queue, event, 0) != pdTRUE) {
        s_websocket_state.dropped_events++;
        websocket_free_callback_event(event);
        return;
    }
    esp32_mquickjs_notify_activity(s_websocket_state.runtime);
}

static void websocket_enqueue_simple(esp32_mquickjs_websocket_callback_kind_t kind,
                                     int32_t code,
                                     const char *message)
{
    esp32_mquickjs_websocket_callback_event_t event = {
        .kind = kind,
        .code = code,
    };

    if (message != NULL) {
        snprintf(event.message, sizeof(event.message), "%s", message);
    }
    websocket_enqueue(&event);
}

static void websocket_enqueue_error(const char *message, int32_t code)
{
    websocket_enqueue_simple(WEBSOCKET_CALLBACK_ERROR, code, message);
}

static void websocket_handle_data(const esp_websocket_event_data_t *data)
{
    size_t offset;
    size_t chunk_len;
    size_t payload_len;

    if (data == NULL || s_websocket_state.closing) {
        return;
    }
    if (data->op_code == WEBSOCKET_OPCODE_CLOSE ||
        data->op_code == WEBSOCKET_OPCODE_PING ||
        data->op_code == WEBSOCKET_OPCODE_PONG) {
        return;
    }
    if (data->op_code != WEBSOCKET_OPCODE_TEXT) {
        if (data->payload_offset == 0) {
            websocket_enqueue_error("only complete WebSocket text messages are supported",
                                    data->op_code);
        }
        return;
    }

    if (data->payload_offset < 0 || data->data_len < 0 || data->payload_len < 0) {
        websocket_enqueue_error("invalid WebSocket payload bounds", 0);
        websocket_reset_fragment();
        return;
    }
    offset = (size_t)data->payload_offset;
    chunk_len = (size_t)data->data_len;
    payload_len = (size_t)data->payload_len;

    if (offset == 0) {
        websocket_reset_fragment();
        s_websocket_state.fragment_expected = payload_len;
        if (!data->fin || payload_len > s_websocket_state.max_message_bytes) {
            s_websocket_state.fragment_dropping = true;
            if (payload_len > s_websocket_state.max_message_bytes) {
                s_websocket_state.oversized_messages++;
                websocket_enqueue_error("WebSocket message exceeds configured maxMessageBytes",
                                        (int32_t)payload_len);
            } else {
                websocket_enqueue_error("fragmented WebSocket messages are not supported", 0);
            }
        } else {
            s_websocket_state.fragment =
                esp32_mquickjs_memory_payload_alloc(
                    payload_len + 1U, ESP32_MQUICKJS_MEMORY_EXTERNAL);
            if (s_websocket_state.fragment == NULL) {
                s_websocket_state.fragment_dropping = true;
                websocket_enqueue_error("out of memory while receiving WebSocket message", 0);
            }
        }
    }

    if (s_websocket_state.fragment_dropping) {
        if (offset + chunk_len >= payload_len) {
            websocket_reset_fragment();
        }
        return;
    }
    if (s_websocket_state.fragment == NULL ||
        payload_len != s_websocket_state.fragment_expected ||
        offset != s_websocket_state.fragment_len ||
        offset + chunk_len > payload_len) {
        websocket_enqueue_error("non-contiguous WebSocket payload", 0);
        websocket_reset_fragment();
        return;
    }

    if (chunk_len > 0 && data->data_ptr != NULL) {
        memcpy(s_websocket_state.fragment + offset, data->data_ptr, chunk_len);
    }
    s_websocket_state.fragment_len += chunk_len;
    if (s_websocket_state.fragment_len >= payload_len) {
        esp32_mquickjs_websocket_callback_event_t event = {
            .kind = WEBSOCKET_CALLBACK_MESSAGE,
            .data = s_websocket_state.fragment,
            .data_len = payload_len,
        };

        event.data[event.data_len] = '\0';
        s_websocket_state.fragment = NULL;
        s_websocket_state.fragment_len = 0;
        s_websocket_state.fragment_expected = 0;
        websocket_enqueue(&event);
    }
}

static void websocket_event_handler(void *handler_args,
                                    esp_event_base_t base,
                                    int32_t event_id,
                                    void *event_data)
{
    esp_websocket_event_data_t *data = event_data;
    int32_t code = 0;

    (void)handler_args;
    (void)base;
    if (!s_websocket_state.opened || s_websocket_state.closing) {
        return;
    }

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_websocket_state.connected = true;
        websocket_enqueue_simple(WEBSOCKET_CALLBACK_OPEN, 0, NULL);
        break;
    case WEBSOCKET_EVENT_DATA:
        websocket_handle_data(data);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_websocket_state.connected = false;
        if (data != NULL) {
            code = data->error_handle.esp_ws_handshake_status_code;
        }
        websocket_enqueue_simple(WEBSOCKET_CALLBACK_CLOSE, code, "disconnected");
        break;
    case WEBSOCKET_EVENT_CLOSED:
        s_websocket_state.connected = false;
        break;
    case WEBSOCKET_EVENT_ERROR:
        if (data != NULL) {
            code = data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT &&
                           data->error_handle.esp_tls_last_esp_err != ESP_OK
                       ? data->error_handle.esp_tls_last_esp_err
                       : data->error_handle.esp_ws_handshake_status_code;
        }
        websocket_enqueue_error("WebSocket connection error", code);
        break;
    default:
        break;
    }
}

static void websocket_finalize_close_source(void)
{
    esp_websocket_client_handle_t client = s_websocket_state.client;

    if (client != NULL) {
        if (esp_websocket_client_is_connected(client)) {
            if (esp_websocket_client_close(
                    client, pdMS_TO_TICKS(WEBSOCKET_DEFAULT_SEND_TIMEOUT_MS)) != ESP_OK) {
                (void)esp_websocket_client_stop(client);
            }
        } else {
            (void)esp_websocket_client_stop(client);
        }
        (void)esp_websocket_unregister_events(
            client, WEBSOCKET_EVENT_ANY, websocket_event_handler);
        (void)esp_websocket_client_destroy(client);
        s_websocket_state.client = NULL;
    }
    websocket_reset_fragment();
    websocket_drain_queue();
    s_websocket_state.closing = false;
    s_websocket_state.close_pending = false;
}

static void websocket_close_source(void *opaque)
{
    (void)opaque;
    s_websocket_state.event_queue = NULL;
    s_websocket_state.opened = false;
    s_websocket_state.connected = false;
    s_websocket_state.closing = true;
    if (s_websocket_state.sending) {
        s_websocket_state.close_pending = true;
        return;
    }
    websocket_finalize_close_source();
}

static void websocket_close_internal(void)
{
    esp32_mquickjs_event_queue_t *event_queue = s_websocket_state.event_queue;

    if (event_queue != NULL) {
        (void)esp32_mquickjs_event_queue_close(event_queue);
    } else {
        websocket_close_source(NULL);
    }
}

static bool websocket_make_event_object(
    JSContext *ctx,
    const esp32_mquickjs_websocket_callback_event_t *event,
    JSValue *out_event)
{
    JSGCRef object_ref;
    JSValue *object;
    const char *type;

    switch (event->kind) {
    case WEBSOCKET_CALLBACK_OPEN:
        type = "open";
        break;
    case WEBSOCKET_CALLBACK_MESSAGE:
        type = "message";
        break;
    case WEBSOCKET_CALLBACK_CLOSE:
        type = "close";
        break;
    case WEBSOCKET_CALLBACK_ERROR:
    default:
        type = "error";
        break;
    }

    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "type", JS_NewString(ctx, type))) {
        JS_PopGCRef(ctx, &object_ref);
        return false;
    }
    if (event->kind == WEBSOCKET_CALLBACK_MESSAGE &&
        !esp32_mquickjs_set_property_ref(ctx, object, "data",
                                         JS_NewStringLen(ctx, event->data, event->data_len))) {
        JS_PopGCRef(ctx, &object_ref);
        return false;
    }
    if ((event->kind == WEBSOCKET_CALLBACK_CLOSE ||
         event->kind == WEBSOCKET_CALLBACK_ERROR) &&
        (!esp32_mquickjs_set_property_ref(ctx, object, "code",
                                          JS_NewInt32(ctx, event->code)) ||
         !esp32_mquickjs_set_property_ref(ctx, object, "message",
                                          JS_NewString(ctx, event->message)) ||
         !esp32_mquickjs_set_property_ref(ctx, object, "reconnecting",
                                          JS_NewBool(s_websocket_state.opened &&
                                                     !s_websocket_state.closing)))) {
        JS_PopGCRef(ctx, &object_ref);
        return false;
    }
    *out_event = JS_PopGCRef(ctx, &object_ref);
    return true;
}

static JSValue websocket_event_to_js(JSContext *ctx,
                                     const void *data,
                                     void *opaque)
{
    esp32_mquickjs_websocket_callback_event_t *event =
        (esp32_mquickjs_websocket_callback_event_t *)data;
    JSValue result = JS_EXCEPTION;

    (void)opaque;
    if (websocket_make_event_object(ctx, event, &result)) {
        websocket_free_callback_event(event);
        return result;
    }
    websocket_free_callback_event(event);
    return JS_EXCEPTION;
}

static void websocket_drop_event(void *data, void *opaque)
{
    (void)opaque;
    websocket_free_callback_event(data);
}

static bool websocket_poller(JSContext *ctx,
                             esp32_mquickjs_runtime_t *runtime,
                             void *opaque)
{
    esp32_mquickjs_websocket_callback_event_t event;
    bool handled = false;

    (void)opaque;
    (void)ctx;
    (void)runtime;
    if (ctx == NULL || s_websocket_state.queue == NULL) {
        return false;
    }

    while (xQueueReceive(s_websocket_state.queue, &event, 0) == pdTRUE) {
        if (event.generation != s_websocket_state.generation ||
            s_websocket_state.event_queue == NULL) {
            websocket_free_callback_event(&event);
            continue;
        }
        handled = true;
        if (event.kind == WEBSOCKET_CALLBACK_OPEN) {
            s_websocket_state.opened_events++;
        } else if (event.kind == WEBSOCKET_CALLBACK_MESSAGE) {
            s_websocket_state.received_messages++;
        }

        if (!esp32_mquickjs_event_queue_send(s_websocket_state.event_queue, &event)) {
            websocket_free_callback_event(&event);
        }
    }
    return handled;
}

static bool websocket_get_int_option(JSContext *ctx,
                                     JSValue options,
                                     const char *name,
                                     int default_value,
                                     int min_value,
                                     int max_value,
                                     int *out_value)
{
    JSValue value = JS_GetPropertyStr(ctx, options, name);
    int raw_value;

    if (JS_IsUndefined(value)) {
        *out_value = default_value;
        return true;
    }
    if (JS_ToInt32(ctx, &raw_value, value) != 0 ||
        raw_value < min_value || raw_value > max_value) {
        return false;
    }
    *out_value = raw_value;
    return true;
}

static bool websocket_get_bool_option(JSContext *ctx,
                                      JSValue options,
                                      const char *name,
                                      bool default_value,
                                      bool *out_value)
{
    JSValue value = JS_GetPropertyStr(ctx, options, name);
    int raw_value;

    if (JS_IsUndefined(value)) {
        *out_value = default_value;
        return true;
    }
    if (JS_ToInt32(ctx, &raw_value, value) != 0 ||
        (raw_value != 0 && raw_value != 1)) {
        return false;
    }
    *out_value = raw_value != 0;
    return true;
}

bool esp32_mquickjs_init_websocket_runtime(JSContext *ctx,
                                            esp32_mquickjs_runtime_t *runtime)
{
    if (s_websocket_state.initialized) {
        s_websocket_state.runtime = runtime;
        return true;
    }

    memset(&s_websocket_state, 0, sizeof(s_websocket_state));
    s_websocket_state.queue = xQueueCreate(
        CONFIG_ESP32_MQUICKJS_WEBSOCKET_EVENT_QUEUE_LEN,
        sizeof(esp32_mquickjs_websocket_callback_event_t));
    if (s_websocket_state.queue == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    s_websocket_state.runtime = runtime;
    if (!esp32_mquickjs_register_async_poller(runtime, websocket_poller, NULL)) {
        vQueueDelete(s_websocket_state.queue);
        memset(&s_websocket_state, 0, sizeof(s_websocket_state));
        JS_ThrowInternalError(ctx, "failed to register WebSocket poller");
        return false;
    }
    s_websocket_state.initialized = true;
    if (!websocket_register_future_driver(ctx, runtime)) {
        return false;
    }
    return true;
}

void esp32_mquickjs_deinit_websocket_runtime(JSContext *ctx)
{
    (void)ctx;
    if (!s_websocket_state.initialized) {
        return;
    }
    websocket_close_internal();
    if (s_websocket_state.queue != NULL) {
        vQueueDelete(s_websocket_state.queue);
    }
    memset(&s_websocket_state, 0, sizeof(s_websocket_state));
}

JSValue js_websocket_open(JSContext *ctx,
                          JSValue *this_val,
                          int argc,
                          JSValue *argv)
{
    JSCStringBuf url_buf;
    JSCStringBuf authorization_buf;
    JSCStringBuf subprotocol_buf;
    JSValue url_value;
    JSValue authorization_value;
    JSValue subprotocol_value;
    const char *url;
    const char *authorization = NULL;
    const char *subprotocol = NULL;
    char *url_copy = NULL;
    char *subprotocol_copy = NULL;
    size_t url_len = 0;
    size_t authorization_len = 0;
    size_t subprotocol_len = 0;
    char *headers = NULL;
    bool auto_reconnect;
    bool use_cert_bundle;
    int reconnect_ms;
    int network_timeout_ms;
    int send_timeout_ms;
    int ping_interval_sec;
    int max_message_bytes;
    esp_websocket_client_config_t config = {0};
    esp_websocket_client_handle_t client;
    JSGCRef queue_ref;
    JSGCRef receive_ref;
    JSGCRef send_ref;
    JSGCRef status_ref;
    JSValue *queue_object;
    JSValue *receive;
    JSValue *send;
    JSValue *status;
    esp_err_t err;

    if (!s_websocket_state.initialized) {
        return JS_ThrowInternalError(ctx, "websocketClient is not initialized");
    }
    if (s_websocket_state.opened || s_websocket_state.client != NULL) {
        return JS_ThrowInternalError(ctx, "websocketClient is already open");
    }
    if (argc != 1 || JS_GetClassID(ctx, argv[0]) < 0) {
        return JS_ThrowTypeError(ctx,
                                 "websocketClient.open(options) expects one options object");
    }

    url_value = JS_GetPropertyStr(ctx, argv[0], "url");
    if (!JS_IsString(ctx, url_value)) {
        return JS_ThrowTypeError(ctx, "websocketClient.open() requires options.url");
    }
    url = JS_ToCStringLen(ctx, &url_len, url_value, &url_buf);
    if (url == NULL || url_len == 0 || url_len > WEBSOCKET_URL_MAX_LEN ||
        (strncmp(url, "ws://", 5) != 0 && strncmp(url, "wss://", 6) != 0)) {
        return JS_ThrowTypeError(ctx, "websocketClient.open() url must use ws:// or wss://");
    }
    url_copy = heap_caps_malloc(url_len + 1U, MALLOC_CAP_8BIT);
    if (url_copy == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    memcpy(url_copy, url, url_len);
    url_copy[url_len] = '\0';

    authorization_value = JS_GetPropertyStr(ctx, argv[0], "authorization");
    if (!JS_IsUndefined(authorization_value)) {
        if (!JS_IsString(ctx, authorization_value)) {
            heap_caps_free(url_copy);
            return JS_ThrowTypeError(ctx, "websocketClient.open() authorization must be a string");
        }
        authorization = JS_ToCStringLen(ctx,
                                        &authorization_len,
                                        authorization_value,
                                        &authorization_buf);
        if (authorization == NULL || authorization_len == 0 ||
            authorization_len > WEBSOCKET_AUTHORIZATION_MAX_LEN ||
            memchr(authorization, '\r', authorization_len) != NULL ||
            memchr(authorization, '\n', authorization_len) != NULL) {
            heap_caps_free(url_copy);
            return JS_ThrowTypeError(ctx, "invalid WebSocket authorization header");
        }
        headers = heap_caps_malloc(authorization_len + 18U, MALLOC_CAP_8BIT);
        if (headers == NULL) {
            heap_caps_free(url_copy);
            return JS_ThrowOutOfMemory(ctx);
        }
        snprintf(headers,
                 authorization_len + 18U,
                 "Authorization: %.*s\r\n",
                 (int)authorization_len,
                 authorization);
    }

    subprotocol_value = JS_GetPropertyStr(ctx, argv[0], "subprotocol");
    if (!JS_IsUndefined(subprotocol_value)) {
        if (!JS_IsString(ctx, subprotocol_value)) {
            heap_caps_free(headers);
            heap_caps_free(url_copy);
            return JS_ThrowTypeError(ctx, "websocketClient.open() subprotocol must be a string");
        }
        subprotocol = JS_ToCStringLen(ctx,
                                     &subprotocol_len,
                                     subprotocol_value,
                                     &subprotocol_buf);
        if (subprotocol == NULL || subprotocol_len == 0 ||
            subprotocol_len > WEBSOCKET_SUBPROTOCOL_MAX_LEN) {
            heap_caps_free(headers);
            heap_caps_free(url_copy);
            return JS_ThrowTypeError(ctx, "invalid WebSocket subprotocol");
        }
        subprotocol_copy = heap_caps_malloc(subprotocol_len + 1U, MALLOC_CAP_8BIT);
        if (subprotocol_copy == NULL) {
            heap_caps_free(headers);
            heap_caps_free(url_copy);
            return JS_ThrowOutOfMemory(ctx);
        }
        memcpy(subprotocol_copy, subprotocol, subprotocol_len);
        subprotocol_copy[subprotocol_len] = '\0';
    }

    if (!websocket_get_bool_option(ctx, argv[0], "autoReconnect", true,
                                   &auto_reconnect) ||
        !websocket_get_bool_option(ctx, argv[0], "useCertBundle", true,
                                   &use_cert_bundle) ||
        !websocket_get_int_option(ctx, argv[0], "reconnectMs",
                                  WEBSOCKET_DEFAULT_RECONNECT_MS, 0, 120000,
                                  &reconnect_ms) ||
        !websocket_get_int_option(ctx, argv[0], "networkTimeoutMs",
                                  WEBSOCKET_DEFAULT_NETWORK_TIMEOUT_MS, 1000, 120000,
                                  &network_timeout_ms) ||
        !websocket_get_int_option(ctx, argv[0], "sendTimeoutMs",
                                  WEBSOCKET_DEFAULT_SEND_TIMEOUT_MS, 0, 5000,
                                  &send_timeout_ms) ||
        !websocket_get_int_option(ctx, argv[0], "pingIntervalSec",
                                  WEBSOCKET_DEFAULT_PING_INTERVAL_SEC, 1, 3600,
                                  &ping_interval_sec) ||
        !websocket_get_int_option(ctx, argv[0], "maxMessageBytes",
                                  CONFIG_ESP32_MQUICKJS_WEBSOCKET_MAX_MESSAGE_BYTES,
                                  256,
                                  CONFIG_ESP32_MQUICKJS_WEBSOCKET_MAX_MESSAGE_BYTES,
                                  &max_message_bytes)) {
        heap_caps_free(subprotocol_copy);
        heap_caps_free(headers);
        heap_caps_free(url_copy);
        return JS_ThrowRangeError(ctx, "invalid websocketClient.open() option");
    }
    if (!esp32_mquickjs_net_is_ready()) {
        heap_caps_free(subprotocol_copy);
        heap_caps_free(headers);
        heap_caps_free(url_copy);
        return JS_ThrowInternalError(
            ctx, "a network interface must be ready before opening WebSocket");
    }

    config.uri = url_copy;
    config.headers = headers;
    config.subprotocol = subprotocol_copy;
    config.user_context = &s_websocket_state;
    config.disable_auto_reconnect = !auto_reconnect || reconnect_ms == 0;
    config.enable_close_reconnect = auto_reconnect && reconnect_ms > 0;
    config.reconnect_timeout_ms = reconnect_ms;
    config.network_timeout_ms = network_timeout_ms;
    config.ping_interval_sec = (size_t)ping_interval_sec;
    config.task_stack = CONFIG_ESP32_MQUICKJS_WEBSOCKET_TASK_STACK_SIZE;
    config.task_prio = 5;
    config.buffer_size = 1024;
    if (use_cert_bundle && strncmp(url_copy, "wss://", 6) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    client = esp_websocket_client_init(&config);
    heap_caps_free(subprotocol_copy);
    heap_caps_free(headers);
    heap_caps_free(url_copy);
    if (client == NULL) {
        return JS_ThrowInternalError(ctx, "failed to initialize WebSocket client");
    }

    s_websocket_state.generation++;
    if (s_websocket_state.generation == 0) {
        s_websocket_state.generation++;
    }
    s_websocket_state.client = client;
    s_websocket_state.max_message_bytes = (size_t)max_message_bytes;
    s_websocket_state.send_timeout_ms = (uint32_t)send_timeout_ms;
    queue_object = JS_PushGCRef(ctx, &queue_ref);
    receive = JS_PushGCRef(ctx, &receive_ref);
    send = JS_PushGCRef(ctx, &send_ref);
    status = JS_PushGCRef(ctx, &status_ref);
    *queue_object = esp32_mquickjs_event_queue_new(
        ctx,
        s_websocket_state.runtime,
        sizeof(esp32_mquickjs_websocket_callback_event_t),
        CONFIG_ESP32_MQUICKJS_WEBSOCKET_EVENT_QUEUE_LEN,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
        websocket_event_to_js,
        websocket_drop_event,
        websocket_close_source,
        NULL);
    *receive = JS_IsException(*queue_object)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *queue_object, "receive");
    *send = JS_GetPropertyStr(ctx, *this_val, "send");
    *status = JS_GetPropertyStr(ctx, *this_val, "status");
    if (JS_IsException(*queue_object) || JS_IsException(*receive) ||
        JS_IsException(*send) || JS_IsException(*status) ||
        JS_IsException(JS_SetPropertyStr(ctx, *queue_object, "recv", *receive)) ||
        JS_IsException(JS_SetPropertyStr(ctx, *queue_object, "send", *send)) ||
        JS_IsException(JS_SetPropertyStr(ctx, *queue_object, "status", *status))) {
        websocket_close_source(NULL);
        JS_PopGCRef(ctx, &status_ref);
        JS_PopGCRef(ctx, &send_ref);
        JS_PopGCRef(ctx, &receive_ref);
        JS_PopGCRef(ctx, &queue_ref);
        return JS_EXCEPTION;
    }
    s_websocket_state.event_queue = JS_GetOpaque(ctx, *queue_object);
    s_websocket_state.opened = true;

    err = esp_websocket_register_events(client,
                                        WEBSOCKET_EVENT_ANY,
                                        websocket_event_handler,
                                        &s_websocket_state);
    if (err == ESP_OK) {
        err = esp_websocket_client_start(client);
    }
    if (err != ESP_OK) {
        websocket_close_internal();
        JS_PopGCRef(ctx, &status_ref);
        JS_PopGCRef(ctx, &send_ref);
        JS_PopGCRef(ctx, &receive_ref);
        JS_PopGCRef(ctx, &queue_ref);
        return JS_ThrowInternalError(ctx,
                                     "failed to start WebSocket client: %s",
                                     esp_err_to_name(err));
    }
    JS_PopGCRef(ctx, &status_ref);
    JS_PopGCRef(ctx, &send_ref);
    JS_PopGCRef(ctx, &receive_ref);
    return JS_PopGCRef(ctx, &queue_ref);
}

JSValue js_websocket_close(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    bool was_open = s_websocket_state.opened || s_websocket_state.client != NULL;

    (void)this_val;
    (void)argc;
    (void)argv;
    websocket_close_internal();
    return JS_NewBool(was_open);
}

JSValue js_websocket_send(JSContext *ctx,
                          JSValue *this_val,
                          int argc,
                          JSValue *argv)
{
    JSCStringBuf text_buf;
    const char *text;
    size_t text_len = 0;
    int sent;

    (void)this_val;
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "websocketClient.send(text) expects a string");
    }
    if (!s_websocket_state.opened || s_websocket_state.client == NULL ||
        !esp_websocket_client_is_connected(s_websocket_state.client)) {
        return JS_ThrowInternalError(ctx, "websocketClient is not connected");
    }
    if (s_websocket_state.sending) {
        return JS_ThrowInternalError(
            ctx, "websocketClient.send() failed because another send is active");
    }
    text = JS_ToCStringLen(ctx, &text_len, argv[0], &text_buf);
    if (text == NULL) {
        return JS_EXCEPTION;
    }
    if (text_len > s_websocket_state.max_message_bytes || text_len > INT32_MAX) {
        return JS_ThrowRangeError(ctx, "websocketClient.send() message is too large");
    }

    s_websocket_state.sending = true;
    sent = esp_websocket_client_send_text(
        s_websocket_state.client,
        text,
        (int)text_len,
        pdMS_TO_TICKS(s_websocket_state.send_timeout_ms));
    s_websocket_state.sending = false;
    if (s_websocket_state.close_pending) {
        websocket_finalize_close_source();
    }
    if (sent < 0 || (size_t)sent != text_len) {
        return JS_ThrowInternalError(ctx, "websocketClient.send() failed");
    }
    s_websocket_state.sent_messages++;
    return JS_NewInt32(ctx, sent);
}

struct esp32_mquickjs_future_driver_state {
    esp_websocket_client_handle_t client;
    char *text;
    size_t text_length;
    uint32_t generation;
    uint32_t timeout_ms;
    int sent;
    _Atomic bool worker_completed;
    bool started;
    _Atomic bool cancelled;
};

static void websocket_send_future_release(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    heap_caps_free(state->text);
    heap_caps_free(state);
}

static bool websocket_send_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    JSCStringBuf text_buf;
    const char *text;
    size_t text_length = 0;

    (void)this_ref;
    if (out_state == NULL || argc != 1 || !JS_IsString(ctx, argv[0].val)) {
        JS_ThrowTypeError(
            ctx, "websocketClient.send(text) expects one string");
        return false;
    }
    if (!s_websocket_state.opened || s_websocket_state.client == NULL ||
        !esp_websocket_client_is_connected(s_websocket_state.client)) {
        JS_ThrowInternalError(ctx, "websocketClient is not connected");
        return false;
    }
    if (s_websocket_state.sending) {
        JS_ThrowInternalError(
            ctx,
            "websocketClient.send() failed because another send is active");
        return false;
    }
    text = JS_ToCStringLen(ctx, &text_length, argv[0].val, &text_buf);
    if (text == NULL) {
        return false;
    }
    if (text_length > s_websocket_state.max_message_bytes ||
        text_length > INT32_MAX) {
        JS_ThrowRangeError(ctx,
                           "websocketClient.send() message is too large");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    atomic_init(&state->worker_completed, false);
    atomic_init(&state->cancelled, false);
    state->text = heap_caps_malloc(text_length + 1U, MALLOC_CAP_8BIT);
    if (state->text == NULL) {
        websocket_send_future_release(state);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    memcpy(state->text, text, text_length);
    state->text[text_length] = '\0';
    state->text_length = text_length;
    state->client = s_websocket_state.client;
    state->generation = s_websocket_state.generation;
    state->timeout_ms = s_websocket_state.send_timeout_ms;
    state->sent = -1;
    *out_state = state;
    return true;
}

static void websocket_send_future_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state == NULL) {
        return;
    }
    if (!atomic_load_explicit(&state->cancelled, memory_order_acquire)) {
        state->sent = esp_websocket_client_send_text(
            state->client, state->text, (int)state->text_length,
            pdMS_TO_TICKS(state->timeout_ms));
    }
    atomic_store_explicit(
        &state->worker_completed, true, memory_order_release);
}

static bool websocket_send_future_start(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || s_websocket_state.sending ||
        !s_websocket_state.opened ||
        state->client != s_websocket_state.client ||
        state->generation != s_websocket_state.generation) {
        JS_ThrowInternalError(
            ctx, "websocketClient changed before send started");
        return false;
    }
    state->started = true;
    s_websocket_state.sending = true;
    if (!esp32_mquickjs_future_submit_worker(
            runtime, token, websocket_send_future_worker, state)) {
        s_websocket_state.sending = false;
        JS_ThrowInternalError(
            ctx, "websocketClient.send() worker queue is full");
        return false;
    }
    return true;
}

static esp32_mquickjs_future_poll_t websocket_send_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL && atomic_load_explicit(
                             &state->worker_completed, memory_order_acquire)) {
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    return ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue websocket_send_future_finish(
    JSContext *ctx,
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL ||
        atomic_load_explicit(&state->cancelled, memory_order_acquire)) {
        return JS_ThrowInternalError(
            ctx, "websocketClient.send() was cancelled");
    }
    if (state->generation != s_websocket_state.generation ||
        state->sent < 0 || (size_t)state->sent != state->text_length) {
        return JS_ThrowInternalError(ctx, "websocketClient.send() failed");
    }
    s_websocket_state.sent_messages++;
    return JS_NewInt32(ctx, state->sent);
}

static esp32_mquickjs_cancel_result_t websocket_send_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL ||
        atomic_load_explicit(&state->worker_completed, memory_order_acquire) ||
        atomic_load_explicit(&state->cancelled, memory_order_acquire)) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    atomic_store_explicit(&state->cancelled, true, memory_order_release);
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}

static void websocket_send_future_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL && state->started) {
        s_websocket_state.sending = false;
        if (s_websocket_state.close_pending) {
            websocket_finalize_close_source();
        }
    }
    websocket_send_future_release(state);
}

static uint32_t websocket_send_future_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && state->timeout_ms < UINT32_MAX - 1000U
               ? state->timeout_ms + 1000U
               : 0;
}

static const esp32_mquickjs_future_driver_t s_websocket_send_driver = {
    .capture = websocket_send_future_prepare,
    .start = websocket_send_future_start,
    .poll = websocket_send_future_poll,
    .finish = websocket_send_future_finish,
    .cancel = websocket_send_future_cancel,
    .destroy = websocket_send_future_destroy,
    .timeout_ms = websocket_send_future_timeout_ms,
};

static bool websocket_register_future_driver(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref;
    JSGCRef module_ref;
    JSGCRef send_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref);
    JSValue *send = JS_PushGCRef(ctx, &send_ref);
    bool registered;

    *global = JS_GetGlobalObject(ctx);
    *module = JS_IsException(*global)
                  ? JS_EXCEPTION
                  : JS_GetPropertyStr(ctx, *global, "websocketClient");
    *send = JS_IsException(*module)
                ? JS_EXCEPTION
                : JS_GetPropertyStr(ctx, *module, "send");
    registered = !JS_IsException(*send) &&
                 esp32_mquickjs_future_register_driver(
                     ctx, runtime, *send, &s_websocket_send_driver);
    if (!registered && !JS_HasException(ctx)) {
        JS_ThrowInternalError(
            ctx, "failed to register WebSocket Future driver");
    }
    JS_PopGCRef(ctx, &send_ref);
    JS_PopGCRef(ctx, &module_ref);
    JS_PopGCRef(ctx, &global_ref);
    return registered;
}

JSValue js_websocket_status(JSContext *ctx,
                            JSValue *this_val,
                            int argc,
                            JSValue *argv)
{
    JSGCRef status_ref;
    JSValue *status;

    (void)this_val;
    (void)argc;
    (void)argv;
    status = JS_PushGCRef(ctx, &status_ref);
    *status = JS_NewObject(ctx);
    if (JS_IsException(*status) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "open",
                                         JS_NewBool(s_websocket_state.opened)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "connected",
                                         JS_NewBool(s_websocket_state.connected)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "maxMessageBytes",
                                         JS_NewInt32(ctx, (int32_t)s_websocket_state.max_message_bytes)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "openedEvents",
                                         JS_NewInt32(ctx, (int32_t)s_websocket_state.opened_events)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "receivedMessages",
                                         JS_NewInt32(ctx, (int32_t)s_websocket_state.received_messages)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "sentMessages",
                                         JS_NewInt32(ctx, (int32_t)s_websocket_state.sent_messages)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "droppedEvents",
                                         JS_NewInt32(ctx, (int32_t)s_websocket_state.dropped_events)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "oversizedMessages",
                                         JS_NewInt32(ctx, (int32_t)s_websocket_state.oversized_messages)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "queueDroppedEvents",
                                         JS_NewUint32(ctx,
                                             esp32_mquickjs_event_queue_dropped(
                                                 s_websocket_state.event_queue)))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &status_ref);
}

JSValue js_websocket_get_max_message_bytes(JSContext *ctx,
                                            JSValue *this_val,
                                            int argc,
                                            JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, CONFIG_ESP32_MQUICKJS_WEBSOCKET_MAX_MESSAGE_BYTES);
}

#endif
