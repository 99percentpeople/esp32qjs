#include "esp32_mquickjs_websocket.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_net.h"
#include "utils/esp32_mquickjs_byte_source.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
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
#define WEBSOCKET_OPCODE_BINARY 0x2
#define WEBSOCKET_OPCODE_CLOSE 0x8
#define WEBSOCKET_OPCODE_PING 0x9
#define WEBSOCKET_OPCODE_PONG 0xA

typedef enum {
    WEBSOCKET_CALLBACK_OPEN = 1,
    WEBSOCKET_CALLBACK_MESSAGE,
    WEBSOCKET_CALLBACK_CLOSE,
    WEBSOCKET_CALLBACK_ERROR,
} esp32_mquickjs_websocket_callback_kind_t;

typedef enum {
    WEBSOCKET_LIFECYCLE_IDLE = 0,
    WEBSOCKET_LIFECYCLE_OPEN,
    WEBSOCKET_LIFECYCLE_CONNECTED,
    WEBSOCKET_LIFECYCLE_CLOSING,
} esp32_mquickjs_websocket_lifecycle_t;

typedef struct {
    esp32_mquickjs_websocket_callback_kind_t kind;
    uint32_t generation;
    char *data;
    size_t data_len;
    uint32_t sequence;
    int64_t timestamp_us;
    int32_t code;
    bool binary;
    char message[WEBSOCKET_ERROR_TEXT_LEN];
} esp32_mquickjs_websocket_callback_event_t;

typedef struct {
    bool initialized;
    _Atomic esp32_mquickjs_websocket_lifecycle_t lifecycle;
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
    bool fragment_binary;
    bool fragment_dropping;
    uint32_t opened_events;
    uint32_t received_messages;
    uint32_t sent_messages;
    _Atomic uint32_t dropped_events;
    _Atomic uint32_t oversized_messages;
    _Atomic uint32_t event_sequence;
    bool sending;
    bool close_pending;
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

static bool websocket_lifecycle_is_active(
    esp32_mquickjs_websocket_lifecycle_t lifecycle)
{
    return lifecycle == WEBSOCKET_LIFECYCLE_OPEN ||
           lifecycle == WEBSOCKET_LIFECYCLE_CONNECTED;
}

static void websocket_reset_state(void)
{
    memset(&s_websocket_state, 0, sizeof(s_websocket_state));
    atomic_init(&s_websocket_state.lifecycle, WEBSOCKET_LIFECYCLE_IDLE);
    atomic_init(&s_websocket_state.dropped_events, 0);
    atomic_init(&s_websocket_state.oversized_messages, 0);
    atomic_init(&s_websocket_state.event_sequence, 0);
}

static bool websocket_callback_begin(uint32_t *generation)
{
    esp32_mquickjs_websocket_lifecycle_t lifecycle =
        atomic_load_explicit(&s_websocket_state.lifecycle,
                             memory_order_acquire);

    if (generation == NULL || !websocket_lifecycle_is_active(lifecycle)) {
        return false;
    }
    /* The release-store that opened this generation publishes all immutable
     * callback inputs, including queue, runtime, limits, and generation. */
    *generation = s_websocket_state.generation;
    return true;
}

static bool websocket_generation_is_active(uint32_t generation)
{
    esp32_mquickjs_websocket_lifecycle_t lifecycle =
        atomic_load_explicit(&s_websocket_state.lifecycle,
                             memory_order_acquire);

    return websocket_lifecycle_is_active(lifecycle) &&
           generation == s_websocket_state.generation;
}

static bool websocket_callback_set_connected(uint32_t generation,
                                             bool connected,
                                             bool *changed)
{
    esp32_mquickjs_websocket_lifecycle_t current =
        atomic_load_explicit(&s_websocket_state.lifecycle,
                             memory_order_acquire);
    esp32_mquickjs_websocket_lifecycle_t desired =
        connected ? WEBSOCKET_LIFECYCLE_CONNECTED
                  : WEBSOCKET_LIFECYCLE_OPEN;

    if (changed != NULL) {
        *changed = false;
    }
    while (websocket_lifecycle_is_active(current) &&
           generation == s_websocket_state.generation) {
        if (current == desired) {
            return true;
        }
        if (atomic_compare_exchange_weak_explicit(
                &s_websocket_state.lifecycle, &current, desired,
                memory_order_acq_rel, memory_order_acquire)) {
            if (changed != NULL) {
                *changed = true;
            }
            return true;
        }
    }
    return false;
}

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
    s_websocket_state.fragment_binary = false;
    s_websocket_state.fragment_dropping = false;
}

static void websocket_enqueue(esp32_mquickjs_websocket_callback_event_t *event,
                              uint32_t generation)
{
    if (event == NULL || s_websocket_state.queue == NULL ||
        !websocket_generation_is_active(generation)) {
        websocket_free_callback_event(event);
        return;
    }
    event->generation = generation;
    event->sequence = atomic_fetch_add_explicit(
                          &s_websocket_state.event_sequence, 1,
                          memory_order_relaxed) +
                      1U;
    event->timestamp_us = esp_timer_get_time();
    if (xQueueSend(s_websocket_state.queue, event, 0) != pdTRUE) {
        atomic_fetch_add_explicit(&s_websocket_state.dropped_events, 1,
                                  memory_order_relaxed);
        websocket_free_callback_event(event);
        return;
    }
    esp32_mquickjs_notify_activity(s_websocket_state.runtime);
}

static void websocket_enqueue_simple(esp32_mquickjs_websocket_callback_kind_t kind,
                                     int32_t code,
                                     const char *message,
                                     uint32_t generation)
{
    esp32_mquickjs_websocket_callback_event_t event = {
        .kind = kind,
        .code = code,
    };

    if (message != NULL) {
        snprintf(event.message, sizeof(event.message), "%s", message);
    }
    websocket_enqueue(&event, generation);
}

static void websocket_enqueue_error(const char *message, int32_t code,
                                    uint32_t generation)
{
    websocket_enqueue_simple(WEBSOCKET_CALLBACK_ERROR, code, message,
                             generation);
}

static void websocket_handle_data(const esp_websocket_event_data_t *data,
                                  uint32_t generation)
{
    size_t offset;
    size_t chunk_len;
    size_t payload_len;

    if (data == NULL || !websocket_generation_is_active(generation)) {
        return;
    }
    if (data->op_code == WEBSOCKET_OPCODE_CLOSE ||
        data->op_code == WEBSOCKET_OPCODE_PING ||
        data->op_code == WEBSOCKET_OPCODE_PONG) {
        return;
    }
    if (data->payload_offset == 0 &&
        data->op_code != WEBSOCKET_OPCODE_TEXT &&
        data->op_code != WEBSOCKET_OPCODE_BINARY) {
        if (data->payload_offset == 0) {
            websocket_enqueue_error("unsupported WebSocket data opcode",
                                    data->op_code, generation);
        }
        return;
    }

    if (data->payload_offset < 0 || data->data_len < 0 || data->payload_len < 0) {
        websocket_enqueue_error("invalid WebSocket payload bounds", 0,
                                generation);
        websocket_reset_fragment();
        return;
    }
    offset = (size_t)data->payload_offset;
    chunk_len = (size_t)data->data_len;
    payload_len = (size_t)data->payload_len;

    if (offset == 0) {
        websocket_reset_fragment();
        s_websocket_state.fragment_expected = payload_len;
        s_websocket_state.fragment_binary =
            data->op_code == WEBSOCKET_OPCODE_BINARY;
        if (!data->fin || payload_len > s_websocket_state.max_message_bytes) {
            s_websocket_state.fragment_dropping = true;
            if (payload_len > s_websocket_state.max_message_bytes) {
                atomic_fetch_add_explicit(
                    &s_websocket_state.oversized_messages, 1,
                    memory_order_relaxed);
                websocket_enqueue_error(
                    "WebSocket message exceeds configured maxMessageBytes",
                    (int32_t)payload_len, generation);
            } else {
                websocket_enqueue_error(
                    "fragmented WebSocket messages are not supported", 0,
                    generation);
            }
        } else {
            size_t allocation_size =
                payload_len + (s_websocket_state.fragment_binary ? 0U : 1U);

            s_websocket_state.fragment =
                esp32_mquickjs_memory_payload_alloc(
                    allocation_size > 0 ? allocation_size : 1U,
                    ESP32_MQUICKJS_MEMORY_EXTERNAL);
            if (s_websocket_state.fragment == NULL) {
                s_websocket_state.fragment_dropping = true;
                websocket_enqueue_error(
                    "out of memory while receiving WebSocket message", 0,
                    generation);
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
        websocket_enqueue_error("non-contiguous WebSocket payload", 0,
                                generation);
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
            .binary = s_websocket_state.fragment_binary,
        };

        if (!event.binary) {
            event.data[event.data_len] = '\0';
        }
        s_websocket_state.fragment = NULL;
        s_websocket_state.fragment_len = 0;
        s_websocket_state.fragment_expected = 0;
        websocket_enqueue(&event, generation);
    }
}

static void websocket_event_handler(void *handler_args,
                                    esp_event_base_t base,
                                    int32_t event_id,
                                    void *event_data)
{
    esp_websocket_event_data_t *data = event_data;
    uint32_t generation;
    int32_t code = 0;
    bool changed = false;

    (void)handler_args;
    (void)base;
    if (!websocket_callback_begin(&generation)) {
        return;
    }

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        if (websocket_callback_set_connected(generation, true, &changed) &&
            changed) {
            websocket_enqueue_simple(WEBSOCKET_CALLBACK_OPEN, 0, NULL,
                                     generation);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        websocket_handle_data(data, generation);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        if (!websocket_callback_set_connected(generation, false, NULL)) {
            break;
        }
        if (data != NULL) {
            code = data->error_handle.esp_ws_handshake_status_code;
        }
        websocket_enqueue_simple(WEBSOCKET_CALLBACK_CLOSE, code,
                                 "disconnected", generation);
        break;
    case WEBSOCKET_EVENT_CLOSED:
        (void)websocket_callback_set_connected(generation, false, NULL);
        break;
    case WEBSOCKET_EVENT_ERROR:
        if (data != NULL) {
            code = data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT &&
                           data->error_handle.esp_tls_last_esp_err != ESP_OK
                       ? data->error_handle.esp_tls_last_esp_err
                       : data->error_handle.esp_ws_handshake_status_code;
        }
        websocket_enqueue_error("WebSocket connection error", code,
                                generation);
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
    s_websocket_state.close_pending = false;
    atomic_store_explicit(&s_websocket_state.lifecycle,
                          WEBSOCKET_LIFECYCLE_IDLE,
                          memory_order_release);
}

static void websocket_close_source(void *opaque)
{
    (void)opaque;
    s_websocket_state.event_queue = NULL;
    atomic_store_explicit(&s_websocket_state.lifecycle,
                          WEBSOCKET_LIFECYCLE_CLOSING,
                          memory_order_release);
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
    esp32_mquickjs_websocket_callback_event_t *event,
    JSValue *out_event)
{
    JSGCRef object_ref;
    JSGCRef data_ref;
    JSValue *object;
    JSValue *data;
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
    data = JS_PushGCRef(ctx, &data_ref);
    *data = JS_UNDEFINED;
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "type",
                                         JS_NewString(ctx, type)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "sequence",
                                         JS_NewUint32(ctx, event->sequence)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "timestampUs",
                                         JS_NewInt64(ctx, event->timestamp_us))) {
        JS_PopGCRef(ctx, &data_ref);
        JS_PopGCRef(ctx, &object_ref);
        return false;
    }
    if (event->kind == WEBSOCKET_CALLBACK_MESSAGE) {
        *data = event->binary
                    ? esp32_mquickjs_new_owned_byte_view(
                          ctx, (uint8_t *)event->data, event->data_len)
                    : JS_NewStringLen(ctx, event->data, event->data_len);
        if (event->binary && !JS_IsException(*data)) {
            event->data = NULL;
        }
        if (JS_IsException(*data) ||
            !esp32_mquickjs_set_property_ref(ctx, object, "data", *data)) {
            JS_PopGCRef(ctx, &data_ref);
            JS_PopGCRef(ctx, &object_ref);
            return false;
        }
        *data = JS_UNDEFINED;
    }
    if ((event->kind == WEBSOCKET_CALLBACK_CLOSE ||
         event->kind == WEBSOCKET_CALLBACK_ERROR) &&
        (!esp32_mquickjs_set_property_ref(ctx, object, "code",
                                          JS_NewInt32(ctx, event->code)) ||
         !esp32_mquickjs_set_property_ref(ctx, object, "message",
                                          JS_NewString(ctx, event->message)) ||
         !esp32_mquickjs_set_property_ref(ctx, object, "reconnecting",
                                          JS_NewBool(
                                              websocket_generation_is_active(
                                              event->generation))))) {
        JS_PopGCRef(ctx, &data_ref);
        JS_PopGCRef(ctx, &object_ref);
        return false;
    }
    JS_PopGCRef(ctx, &data_ref);
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

    websocket_reset_state();
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
        websocket_reset_state();
        JS_ThrowInternalError(ctx, "failed to register WebSocket poller");
        return false;
    }
    s_websocket_state.initialized = true;
    if (!websocket_register_future_driver(ctx, runtime)) {
        vQueueDelete(s_websocket_state.queue);
        websocket_reset_state();
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
    websocket_reset_state();
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
    JSGCRef send_ref;
    JSGCRef status_ref;
    JSValue *queue_object;
    JSValue *send;
    JSValue *status;
    esp_err_t err;

    if (!s_websocket_state.initialized) {
        return JS_ThrowInternalError(ctx, "websocketClient is not initialized");
    }
    if (atomic_load_explicit(&s_websocket_state.lifecycle,
                             memory_order_acquire) !=
            WEBSOCKET_LIFECYCLE_IDLE ||
        s_websocket_state.client != NULL) {
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
    *send = JS_GetPropertyStr(ctx, *this_val, "send");
    *status = JS_GetPropertyStr(ctx, *this_val, "status");
    if (JS_IsException(*queue_object) ||
        JS_IsException(*send) || JS_IsException(*status) ||
        JS_IsException(JS_SetPropertyStr(ctx, *queue_object, "send", *send)) ||
        JS_IsException(JS_SetPropertyStr(ctx, *queue_object, "status", *status))) {
        websocket_close_source(NULL);
        JS_PopGCRef(ctx, &status_ref);
        JS_PopGCRef(ctx, &send_ref);
        JS_PopGCRef(ctx, &queue_ref);
        return JS_EXCEPTION;
    }
    s_websocket_state.event_queue = JS_GetOpaque(ctx, *queue_object);
    /* Publish the complete generation before ESP-IDF can invoke callbacks. */
    atomic_store_explicit(&s_websocket_state.lifecycle,
                          WEBSOCKET_LIFECYCLE_OPEN,
                          memory_order_release);

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
        JS_PopGCRef(ctx, &queue_ref);
        return JS_ThrowInternalError(ctx,
                                     "failed to start WebSocket client: %s",
                                     esp_err_to_name(err));
    }
    JS_PopGCRef(ctx, &status_ref);
    JS_PopGCRef(ctx, &send_ref);
    return JS_PopGCRef(ctx, &queue_ref);
}

JSValue js_websocket_close(JSContext *ctx,
                           JSValue *this_val,
                           int argc,
                           JSValue *argv)
{
    bool was_open =
        atomic_load_explicit(&s_websocket_state.lifecycle,
                             memory_order_acquire) !=
            WEBSOCKET_LIFECYCLE_IDLE ||
        s_websocket_state.client != NULL;

    (void)this_val;
    (void)argc;
    (void)argv;
    websocket_close_internal();
    return JS_NewBool(was_open);
}

static bool websocket_copy_send_payload(JSContext *ctx,
                                        JSValue value,
                                        char **out_data,
                                        size_t *out_length,
                                        bool *out_binary)
{
    int class_id = JS_GetClassID(ctx, value);

    *out_data = NULL;
    *out_length = 0;
    *out_binary = !JS_IsString(ctx, value);
    if (JS_IsString(ctx, value)) {
        JSCStringBuf text_buf;
        const char *text;
        size_t length = 0;

        text = JS_ToCStringLen(ctx, &length, value, &text_buf);
        if (text == NULL) {
            return false;
        }
        if (length > s_websocket_state.max_message_bytes ||
            length > INT32_MAX) {
            JS_ThrowRangeError(ctx,
                               "websocketClient.send() message is too large");
            return false;
        }
        *out_data = esp32_mquickjs_memory_payload_alloc(
            length > 0 ? length : 1U, ESP32_MQUICKJS_MEMORY_EXTERNAL);
        if (*out_data == NULL) {
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        if (length > 0) {
            memcpy(*out_data, text, length);
        }
        *out_length = length;
        return true;
    }
    if (class_id == JS_CLASS_BYTE_SPAN_SOURCE ||
        class_id == JS_CLASS_BITMAP_SPAN_SOURCE) {
        esp32_mquickjs_byte_span_source_t source;
        JSValue error = JS_UNDEFINED;
        size_t capacity = 0;
        size_t length = 0;
        bool opened = false;
        bool ok = false;

        if (!esp32_mquickjs_open_byte_span_source(
                ctx, value, "websocketClient.send(data)", &source, &error)) {
            if (!JS_IsUndefined(error) && !JS_IsException(error)) {
                (void)JS_Throw(ctx, error);
            }
            return false;
        }
        opened = true;
        for (;;) {
            esp32_mquickjs_byte_span_t span;
            size_t required;

            if (!esp32_mquickjs_byte_span_source_next(ctx, &source, &span)) {
                ok = !JS_HasException(ctx);
                break;
            }
            if (span.length == 0) {
                continue;
            }
            if (span.data == NULL ||
                span.length > s_websocket_state.max_message_bytes ||
                length > s_websocket_state.max_message_bytes - span.length) {
                JS_ThrowRangeError(
                    ctx, "websocketClient.send() message is too large");
                break;
            }
            required = length + span.length;
            if (required > capacity) {
                size_t next_capacity = capacity == 0 ? 256U : capacity;
                char *next;

                while (next_capacity < required) {
                    if (next_capacity >=
                        s_websocket_state.max_message_bytes / 2U) {
                        next_capacity = s_websocket_state.max_message_bytes;
                        break;
                    }
                    next_capacity *= 2U;
                }
                next = esp32_mquickjs_memory_payload_realloc(
                    *out_data, next_capacity,
                    ESP32_MQUICKJS_MEMORY_EXTERNAL);
                if (next == NULL) {
                    JS_ThrowOutOfMemory(ctx);
                    break;
                }
                *out_data = next;
                capacity = next_capacity;
            }
            memcpy(*out_data + length, span.data, span.length);
            length = required;
        }
        if (opened) {
            esp32_mquickjs_byte_span_source_close(ctx, &source);
        }
        if (!ok) {
            heap_caps_free(*out_data);
            *out_data = NULL;
            return false;
        }
        if (*out_data == NULL) {
            *out_data = esp32_mquickjs_memory_payload_alloc(
                1U, ESP32_MQUICKJS_MEMORY_EXTERNAL);
            if (*out_data == NULL) {
                JS_ThrowOutOfMemory(ctx);
                return false;
            }
        }
        *out_length = length;
        return true;
    }
    {
        esp32_mquickjs_byte_source_t source;
        uint8_t *owned = NULL;
        JSValue error = JS_UNDEFINED;

        if (!esp32_mquickjs_get_byte_source(
                ctx, value, "websocketClient.send(data)", &source, &owned,
                &error)) {
            if (!JS_IsUndefined(error) && !JS_IsException(error)) {
                (void)JS_Throw(ctx, error);
            }
            return false;
        }
        if (source.length > s_websocket_state.max_message_bytes ||
            source.length > INT32_MAX) {
            esp32_mquickjs_release_byte_source(owned);
            JS_ThrowRangeError(ctx,
                               "websocketClient.send() message is too large");
            return false;
        }
        *out_data = esp32_mquickjs_memory_payload_alloc(
            source.length > 0 ? source.length : 1U,
            ESP32_MQUICKJS_MEMORY_EXTERNAL);
        if (*out_data == NULL) {
            esp32_mquickjs_release_byte_source(owned);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        if (source.length > 0) {
            memcpy(*out_data, source.data, source.length);
        }
        *out_length = source.length;
        esp32_mquickjs_release_byte_source(owned);
        return true;
    }
}

JSValue js_websocket_send(JSContext *ctx,
                          JSValue *this_val,
                          int argc,
                          JSValue *argv)
{
    char *data = NULL;
    size_t length = 0;
    bool binary = false;
    int sent;

    (void)this_val;
    if (argc != 1) {
        return JS_ThrowTypeError(
            ctx, "websocketClient.send(data) expects one data argument");
    }
    if (atomic_load_explicit(&s_websocket_state.lifecycle,
                             memory_order_acquire) !=
            WEBSOCKET_LIFECYCLE_CONNECTED ||
        s_websocket_state.client == NULL ||
        !esp_websocket_client_is_connected(s_websocket_state.client)) {
        return JS_ThrowInternalError(ctx, "websocketClient is not connected");
    }
    if (s_websocket_state.sending) {
        return JS_ThrowInternalError(
            ctx, "websocketClient.send() failed because another send is active");
    }
    if (!websocket_copy_send_payload(ctx, argv[0], &data, &length,
                                     &binary)) {
        return JS_EXCEPTION;
    }

    s_websocket_state.sending = true;
    sent = binary
               ? esp_websocket_client_send_bin(
                     s_websocket_state.client, data, (int)length,
                     pdMS_TO_TICKS(s_websocket_state.send_timeout_ms))
               : esp_websocket_client_send_text(
                     s_websocket_state.client, data, (int)length,
                     pdMS_TO_TICKS(s_websocket_state.send_timeout_ms));
    s_websocket_state.sending = false;
    heap_caps_free(data);
    if (s_websocket_state.close_pending) {
        websocket_finalize_close_source();
    }
    if (sent < 0 || (size_t)sent != length) {
        return JS_ThrowInternalError(ctx, "websocketClient.send() failed");
    }
    s_websocket_state.sent_messages++;
    return JS_NewInt32(ctx, sent);
}

struct esp32_mquickjs_future_driver_state {
    esp_websocket_client_handle_t client;
    char *data;
    size_t length;
    uint32_t generation;
    uint32_t timeout_ms;
    int sent;
    bool binary;
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
    heap_caps_free(state->data);
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

    (void)this_ref;
    if (out_state == NULL || argc != 1) {
        JS_ThrowTypeError(
            ctx, "websocketClient.send(data) expects one data argument");
        return false;
    }
    if (atomic_load_explicit(&s_websocket_state.lifecycle,
                             memory_order_acquire) !=
            WEBSOCKET_LIFECYCLE_CONNECTED ||
        s_websocket_state.client == NULL ||
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
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    atomic_init(&state->worker_completed, false);
    atomic_init(&state->cancelled, false);
    if (!websocket_copy_send_payload(ctx, argv[0].val, &state->data,
                                     &state->length, &state->binary)) {
        websocket_send_future_release(state);
        return false;
    }
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
        state->sent = state->binary
                          ? esp_websocket_client_send_bin(
                                state->client, state->data,
                                (int)state->length,
                                pdMS_TO_TICKS(state->timeout_ms))
                          : esp_websocket_client_send_text(
                                state->client, state->data,
                                (int)state->length,
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
        atomic_load_explicit(&s_websocket_state.lifecycle,
                             memory_order_acquire) !=
            WEBSOCKET_LIFECYCLE_CONNECTED ||
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
        state->sent < 0 || (size_t)state->sent != state->length) {
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
    esp32_mquickjs_websocket_lifecycle_t lifecycle;

    (void)this_val;
    (void)argc;
    (void)argv;
    lifecycle = atomic_load_explicit(&s_websocket_state.lifecycle,
                                     memory_order_acquire);
    status = JS_PushGCRef(ctx, &status_ref);
    *status = JS_NewObject(ctx);
    if (JS_IsException(*status) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "open",
                                         JS_NewBool(
                                             websocket_lifecycle_is_active(
                                                 lifecycle))) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "connected",
                                         JS_NewBool(
                                             lifecycle ==
                                             WEBSOCKET_LIFECYCLE_CONNECTED)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "maxMessageBytes",
                                         JS_NewInt32(ctx, (int32_t)s_websocket_state.max_message_bytes)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "openedEvents",
                                         JS_NewInt32(ctx, (int32_t)s_websocket_state.opened_events)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "receivedMessages",
                                         JS_NewInt32(ctx, (int32_t)s_websocket_state.received_messages)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "sentMessages",
                                         JS_NewInt32(ctx, (int32_t)s_websocket_state.sent_messages)) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "droppedEvents",
                                         JS_NewUint32(
                                             ctx, atomic_load_explicit(
                                                      &s_websocket_state.dropped_events,
                                                      memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "oversizedMessages",
                                         JS_NewUint32(
                                             ctx, atomic_load_explicit(
                                                      &s_websocket_state.oversized_messages,
                                                      memory_order_relaxed))) ||
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
