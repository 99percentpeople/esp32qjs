#include "esp32_mquickjs_espnow.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_wireless_core.h"
#include "utils/esp32_mquickjs_byte_source.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define ESPNOW_ADDRESS_BYTES ESP_NOW_ETH_ALEN
#define ESPNOW_BROADCAST_ADDRESS "ff:ff:ff:ff:ff:ff"
#define ESPNOW_DEFAULT_CHANNEL 0U
static const char *TAG = "esp32qjs_espnow";
#if defined(CONFIG_ESP32_MQUICKJS_ESPNOW_ALLOW_V2_PAYLOAD) && \
    CONFIG_ESP32_MQUICKJS_ESPNOW_ALLOW_V2_PAYLOAD
#define ESPNOW_V2_PAYLOAD_ENABLED 1
#else
#define ESPNOW_V2_PAYLOAD_ENABLED 0
#endif

typedef enum {
    ESPNOW_LIFECYCLE_CLOSED = 0,
    ESPNOW_LIFECYCLE_OPENING,
    ESPNOW_LIFECYCLE_ACTIVE,
    ESPNOW_LIFECYCLE_CLOSING,
    ESPNOW_LIFECYCLE_FAILED,
} espnow_lifecycle_t;

typedef struct {
    uint32_t generation;
    uint32_t sequence;
    int64_t timestamp_us;
    uint8_t source[ESPNOW_ADDRESS_BYTES];
    uint8_t destination[ESPNOW_ADDRESS_BYTES];
    int8_t rssi;
    uint8_t channel;
    uint16_t length;
    uint8_t *payload;
} espnow_rx_slot_t;

typedef struct {
    uint32_t generation;
    uint16_t slot_index;
} espnow_receive_event_t;

typedef struct {
    uint32_t generation;
} espnow_session_ref_t;

typedef struct {
    uint32_t session_generation;
    uint16_t peer_index;
    uint32_t peer_generation;
} espnow_peer_ref_t;

typedef struct {
    bool allocated;
    bool reserved;
    uint32_t generation;
    uint8_t address[ESPNOW_ADDRESS_BYTES];
    uint8_t channel;
    bool encrypted;
    uint8_t lmk[ESP_NOW_KEY_LEN];
} espnow_peer_slot_t;

typedef enum {
    ESPNOW_OPERATION_OPEN = 0,
    ESPNOW_OPERATION_ADD_PEER,
    ESPNOW_OPERATION_UPDATE_PEER,
    ESPNOW_OPERATION_CLOSE_PEER,
    ESPNOW_OPERATION_SEND,
    ESPNOW_OPERATION_SET_POWER_SAVE,
    ESPNOW_OPERATION_CLOSE_SESSION,
} espnow_operation_t;

typedef struct {
    portMUX_TYPE lock;
    espnow_lifecycle_t lifecycle;
    esp32_mquickjs_runtime_t *runtime;
    uint32_t generation;
    esp32_mquickjs_wifi_radio_lease_t radio_lease;
    esp32_mquickjs_event_queue_t *event_queue;
    esp32_mquickjs_wireless_pool_t rx_free;
    espnow_rx_slot_t *rx_slots;
    uint8_t *rx_payloads;
    uint32_t receive_capacity;
    uint32_t max_payload_bytes;
    uint32_t send_timeout_ms;
    uint8_t channel;
    uint32_t channel_generation;
    bool channel_fixed;
    bool now_initialized;
    bool receive_callback_registered;
    bool send_callback_registered;
    bool broadcast_peer_added;
    espnow_peer_slot_t peers[CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS];
    uint32_t peer_count;
    uint32_t encrypted_peer_count;
    _Atomic(esp32_mquickjs_future_driver_state_t *) active_send;
    _Atomic uint32_t pending_sends;
    esp32_mquickjs_wireless_tx_state_t tx_state;
    bool has_pmk;
    uint8_t pmk[ESP_NOW_KEY_LEN];
    bool power_save_enabled;
    uint16_t wake_window_ms;
    uint16_t wake_interval_ms;
    _Atomic uint32_t callbacks_active;
    _Atomic uint32_t sequence;
    _Atomic uint32_t received_packets;
    _Atomic uint32_t received_bytes;
    _Atomic uint32_t dropped_packets;
    _Atomic uint32_t malformed_packets;
    _Atomic uint32_t sent_packets;
    _Atomic uint32_t sent_bytes;
    _Atomic uint32_t send_successes;
    _Atomic uint32_t send_failures;
    _Atomic uint32_t send_timeouts;
} espnow_session_t;

struct esp32_mquickjs_future_driver_state {
    JSContext *ctx;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    JSGCRef event_queue_ref;
    JSGCRef owner_ref;
    espnow_operation_t operation;
    uint32_t generation;
    uint16_t peer_index;
    uint32_t peer_generation;
    uint8_t address[ESPNOW_ADDRESS_BYTES];
    uint8_t peer_channel;
    bool encrypted;
    bool has_lmk;
    uint8_t lmk[ESP_NOW_KEY_LEN];
    uint8_t *payload;
    size_t payload_length;
    uint32_t max_payload_bytes;
    uint32_t receive_capacity;
    uint32_t send_timeout_ms;
    uint8_t channel;
    bool channel_fixed;
    bool has_pmk;
    uint8_t pmk[ESP_NOW_KEY_LEN];
    bool power_save_enabled;
    uint16_t wake_window_ms;
    uint16_t wake_interval_ms;
    int64_t completed_at_us;
    esp_err_t err;
    const char *failed_step;
    bool mac_delivered;
    bool timed_out;
    bool recovery_failed;
    bool result_bool;
    bool event_queue_rooted;
    bool owner_rooted;
    bool peer_reserved;
    bool send_reserved;
    bool started;
    bool transferred;
    bool cancelled;
    _Atomic bool completed;
};

static const uint8_t s_broadcast_address[ESPNOW_ADDRESS_BYTES] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
static espnow_session_t s_espnow_session = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
    .lifecycle = ESPNOW_LIFECYCLE_CLOSED,
};
static _Atomic uint32_t s_espnow_next_generation = 1;
static _Atomic uint32_t s_espnow_next_peer_generation = 1;
static const uint8_t s_espnow_tx_lane_key;
static const uint8_t s_espnow_control_lane_key;

static JSValue espnow_status_to_js(JSContext *ctx,
                                   const espnow_session_t *session);
static void espnow_close_native(espnow_session_t *session);

static JSValue espnow_future_call_and_wait(JSContext *ctx,
                                           JSValue receiver,
                                           const char *method_name,
                                           int argc,
                                           JSValue *argv)
{
    JSGCRef receiver_ref;
    JSGCRef method_ref;
    JSValue *rooted_receiver = JS_PushGCRef(ctx, &receiver_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *rooted_receiver = receiver;
    *method = JS_GetPropertyStr(ctx, *rooted_receiver, method_name);
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *rooted_receiver, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &receiver_ref);
    return result;
}

static bool espnow_is_object(JSContext *ctx, JSValue value)
{
    return JS_GetClassID(ctx, value) >= 0 && !JS_IsArray(ctx, value);
}

static bool espnow_string_equals(JSContext *ctx,
                                 JSValue value,
                                 const char *expected)
{
    JSCStringBuf buffer;
    const char *text;

    if (!JS_IsString(ctx, value)) {
        return false;
    }
    text = JS_ToCString(ctx, value, &buffer);
    return text != NULL && strcmp(text, expected) == 0;
}

static bool espnow_to_u32(JSContext *ctx, JSValue value, uint32_t *out)
{
    int raw;

    if (out == NULL || JS_ToInt32(ctx, &raw, value) != 0 || raw < 0) {
        return false;
    }
    *out = (uint32_t)raw;
    return true;
}

static bool espnow_key_allowed(const char *key,
                               const char *const *allowed,
                               size_t allowed_count)
{
    size_t i;

    for (i = 0; i < allowed_count; ++i) {
        if (strcmp(key, allowed[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool espnow_validate_option_keys(JSContext *ctx,
                                        JSValue options,
                                        const char *api_name,
                                        const char *const *allowed,
                                        size_t allowed_count)
{
    JSGCRef global_ref;
    JSGCRef object_ref;
    JSGCRef keys_function_ref;
    JSGCRef keys_ref;
    JSGCRef key_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref);
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *keys_function = JS_PushGCRef(ctx, &keys_function_ref);
    JSValue *keys = JS_PushGCRef(ctx, &keys_ref);
    JSValue *key = JS_PushGCRef(ctx, &key_ref);
    JSValue args[1] = {options};
    uint32_t length = 0;
    uint32_t i;
    bool valid = false;

    *global = JS_GetGlobalObject(ctx);
    *object = JS_IsException(*global)
                  ? JS_EXCEPTION
                  : JS_GetPropertyStr(ctx, *global, "Object");
    *keys_function = JS_IsException(*object)
                         ? JS_EXCEPTION
                         : JS_GetPropertyStr(ctx, *object, "keys");
    *keys = JS_IsException(*keys_function)
                ? JS_EXCEPTION
                : esp32_mquickjs_call(
                      ctx, esp32_mquickjs_get_active_runtime(),
                      *keys_function, *object, 1, args);
    if (JS_IsException(*keys)) {
        goto done;
    }
    *key = JS_GetPropertyStr(ctx, *keys, "length");
    if (JS_IsException(*key) || !espnow_to_u32(ctx, *key, &length)) {
        JS_ThrowTypeError(ctx, "%s options could not be inspected", api_name);
        goto done;
    }
    for (i = 0; i < length; ++i) {
        JSCStringBuf buffer;
        const char *name;

        *key = JS_GetPropertyUint32(ctx, *keys, i);
        name = JS_IsException(*key) ? NULL : JS_ToCString(ctx, *key, &buffer);
        if (name == NULL || !espnow_key_allowed(name, allowed, allowed_count)) {
            JS_ThrowTypeError(ctx, "%s received unknown option '%s'",
                              api_name, name != NULL ? name : "<invalid>");
            goto done;
        }
    }
    valid = true;

done:
    JS_PopGCRef(ctx, &key_ref);
    JS_PopGCRef(ctx, &keys_ref);
    JS_PopGCRef(ctx, &keys_function_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &global_ref);
    return valid;
}

static void espnow_format_address(const uint8_t address[ESPNOW_ADDRESS_BYTES],
                                  char output[18])
{
    esp32_mquickjs_wireless_format_address(address, output);
}

static bool espnow_parse_address(JSContext *ctx,
                                 JSValue value,
                                 const char *api_name,
                                 bool allow_broadcast,
                                 uint8_t output[ESPNOW_ADDRESS_BYTES])
{
    JSCStringBuf buffer;
    const char *text;

    if (!JS_IsString(ctx, value) ||
        (text = JS_ToCString(ctx, value, &buffer)) == NULL ||
        !esp32_mquickjs_wireless_parse_address(text, output)) {
        JS_ThrowTypeError(ctx, "ESPNOW_INVALID_ADDRESS: %s expects xx:xx:xx:xx:xx:xx",
                          api_name);
        return false;
    }
    if ((!allow_broadcast &&
         memcmp(output, s_broadcast_address, ESPNOW_ADDRESS_BYTES) == 0) ||
        (!allow_broadcast && (output[0] & 1U) != 0U)) {
        JS_ThrowTypeError(ctx,
                          "ESPNOW_INVALID_ADDRESS: multicast and broadcast are not ordinary peers");
        return false;
    }
    return true;
}

static espnow_peer_slot_t *espnow_find_peer_by_address(
    espnow_session_t *session,
    const uint8_t address[ESPNOW_ADDRESS_BYTES],
    uint16_t *out_index)
{
    uint16_t i;

    if (session == NULL) {
        return NULL;
    }
    for (i = 0; i < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS; ++i) {
        espnow_peer_slot_t *peer = &session->peers[i];

        if (peer->allocated &&
            memcmp(peer->address, address, ESPNOW_ADDRESS_BYTES) == 0) {
            if (out_index != NULL) {
                *out_index = i;
            }
            return peer;
        }
    }
    return NULL;
}

static espnow_peer_slot_t *espnow_peer_from_value(
    JSContext *ctx,
    JSValue value,
    espnow_peer_ref_t **out_ref,
    bool throw_if_stale)
{
    espnow_peer_ref_t *ref;
    espnow_peer_slot_t *peer = NULL;

    if (JS_GetClassID(ctx, value) != JS_CLASS_ESPNOW_PEER ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "expected an EspNowPeer instance");
        return NULL;
    }
    if (s_espnow_session.lifecycle == ESPNOW_LIFECYCLE_ACTIVE &&
        ref->session_generation == s_espnow_session.generation &&
        ref->peer_index < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS) {
        peer = &s_espnow_session.peers[ref->peer_index];
        if (!peer->allocated || peer->generation != ref->peer_generation) {
            peer = NULL;
        }
    }
    if (peer == NULL && throw_if_stale) {
        JS_ThrowReferenceError(ctx,
                               "ESPNOW_STALE_PEER: ESP-NOW peer is closed");
    }
    if (out_ref != NULL) {
        *out_ref = ref;
    }
    return peer;
}

static JSValue espnow_peer_status_to_js(JSContext *ctx,
                                        const espnow_peer_slot_t *peer)
{
    char address[18];
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);

    if (peer == NULL) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_ThrowReferenceError(ctx,
                                      "ESPNOW_STALE_PEER: peer is closed");
    }
    espnow_format_address(peer->address, address);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "open", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "address",
                                         JS_NewString(ctx, address)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "channel",
            peer->channel == 0
                ? JS_NewString(ctx, "current")
                : JS_NewUint32(ctx, peer->channel)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "encrypted",
                                         JS_NewBool(peer->encrypted))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static JSValue espnow_new_peer_handle(JSContext *ctx,
                                      uint16_t peer_index,
                                      const espnow_peer_slot_t *peer)
{
    espnow_peer_ref_t *ref;
    JSValue object = JS_NewObjectClassUser(ctx, JS_CLASS_ESPNOW_PEER);

    if (JS_IsException(object)) {
        return object;
    }
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    ref->session_generation = s_espnow_session.generation;
    ref->peer_index = peer_index;
    ref->peer_generation = peer->generation;
    JS_SetOpaque(ctx, object, ref);
    return object;
}

static void espnow_clear_peer(espnow_peer_slot_t *peer)
{
    if (peer == NULL) {
        return;
    }
    esp32_mquickjs_wireless_secure_zero(peer->lmk, sizeof(peer->lmk));
    memset(peer, 0, sizeof(*peer));
}

static JSValue espnow_throw_error(
    JSContext *ctx,
    const char *code,
    esp_err_t esp_code,
    const uint8_t *address,
    int channel)
{
    JSGCRef error_ref;
    JSValue *error;
    char normalized_address[18];

    (void)JS_ThrowInternalError(ctx, "%s: ESP-NOW operation failed", code);
    if (!JS_HasException(ctx)) {
        return JS_EXCEPTION;
    }
    error = JS_PushGCRef(ctx, &error_ref);
    *error = JS_GetException(ctx);
    if (address != NULL) {
        espnow_format_address(address, normalized_address);
    }
    if (JS_GetClassID(ctx, *error) >= 0 &&
        (JS_IsException(JS_SetPropertyStr(
             ctx, *error, "code", JS_NewString(ctx, code))) ||
         JS_IsException(JS_SetPropertyStr(
             ctx, *error, "espCode", JS_NewInt32(ctx, esp_code))) ||
         JS_IsException(JS_SetPropertyStr(
             ctx, *error, "address",
             address != NULL ? JS_NewString(ctx, normalized_address)
                             : JS_NULL)) ||
         JS_IsException(JS_SetPropertyStr(
             ctx, *error, "channel",
             channel >= 0 ? JS_NewInt32(ctx, channel) : JS_NULL)))) {
        JS_PopGCRef(ctx, &error_ref);
        return JS_EXCEPTION;
    }
    return JS_Throw(ctx, JS_PopGCRef(ctx, &error_ref));
}

static void espnow_release_rx_slot(espnow_session_t *session,
                                   uint16_t slot_index)
{
    if (session != NULL && slot_index < session->receive_capacity)
        (void)esp32_mquickjs_wireless_pool_release(&session->rx_free,
                                                    slot_index);
}

static void espnow_receive_event_drop(void *event, void *opaque)
{
    espnow_receive_event_t *receive_event = event;
    espnow_session_t *session = opaque;

    if (receive_event == NULL || session == NULL ||
        receive_event->generation != session->generation) {
        return;
    }
    espnow_release_rx_slot(session, receive_event->slot_index);
}

static JSValue espnow_receive_event_to_js(JSContext *ctx,
                                          const void *event,
                                          void *opaque)
{
    const espnow_receive_event_t *receive_event = event;
    espnow_session_t *session = opaque;
    espnow_rx_slot_t *slot;
    uint8_t *payload = NULL;
    char source_address[18];
    char destination_address[18];
    uint32_t sequence;
    int64_t timestamp_us;
    uint16_t payload_length;
    int8_t rssi;
    uint8_t channel;
    bool broadcast;
    JSGCRef object_ref;
    JSGCRef data_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *data = JS_PushGCRef(ctx, &data_ref);

    *object = JS_UNDEFINED;
    *data = JS_UNDEFINED;
    if (receive_event == NULL || session == NULL ||
        receive_event->generation != session->generation ||
        receive_event->slot_index >= session->receive_capacity ||
        session->rx_slots == NULL) {
        JS_ThrowReferenceError(ctx, "ESPNOW_STALE_SESSION: receive event is stale");
        goto fail;
    }
    slot = &session->rx_slots[receive_event->slot_index];
    if (slot->generation != receive_event->generation ||
        slot->length > session->max_payload_bytes) {
        espnow_release_rx_slot(session, receive_event->slot_index);
        JS_ThrowInternalError(ctx, "ESPNOW_QUEUE_FULL: receive slot is invalid");
        goto fail;
    }
    sequence = slot->sequence;
    timestamp_us = slot->timestamp_us;
    payload_length = slot->length;
    rssi = slot->rssi;
    channel = slot->channel;
    if (payload_length > 0) {
        payload = heap_caps_malloc(payload_length, MALLOC_CAP_8BIT);
        if (payload == NULL) {
            espnow_release_rx_slot(session, receive_event->slot_index);
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
        memcpy(payload, slot->payload, payload_length);
    }
    espnow_format_address(slot->source, source_address);
    espnow_format_address(slot->destination, destination_address);
    broadcast = memcmp(slot->destination, s_broadcast_address,
                       ESPNOW_ADDRESS_BYTES) == 0;
    *data = esp32_mquickjs_new_owned_byte_view(ctx, payload, payload_length);
    payload = NULL;
    espnow_release_rx_slot(session, receive_event->slot_index);
    if (JS_IsException(*data)) {
        goto fail;
    }
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "type",
                                         JS_NewString(ctx, "message")) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "sequence",
                                         JS_NewUint32(ctx, sequence)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "timestampUs",
                                         JS_NewInt64(ctx, timestamp_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "sourceAddress",
                                         JS_NewString(ctx, source_address)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "destinationAddress",
                                         JS_NewString(ctx, destination_address)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "broadcast",
                                         JS_NewBool(broadcast)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "rssi",
                                         JS_NewInt32(ctx, rssi)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "channel",
                                         JS_NewInt32(ctx, channel)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "data", *data)) {
        goto fail;
    }
    JS_PopGCRef(ctx, &data_ref);
    return JS_PopGCRef(ctx, &object_ref);

fail:
    heap_caps_free(payload);
    JS_PopGCRef(ctx, &data_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static void espnow_event_queue_close(void *opaque)
{
    espnow_close_native(opaque);
}

static void espnow_receive_callback(const esp_now_recv_info_t *receive_info,
                                    const uint8_t *data,
                                    int data_length)
{
    espnow_session_t *session = &s_espnow_session;
    espnow_receive_event_t event;
    espnow_rx_slot_t *slot;
    uint16_t slot_index;
    uint32_t generation;

    atomic_fetch_add_explicit(&session->callbacks_active, 1,
                              memory_order_acq_rel);
    generation = session->generation;
    if (session->lifecycle != ESPNOW_LIFECYCLE_ACTIVE ||
        receive_info == NULL || receive_info->src_addr == NULL ||
        receive_info->des_addr == NULL || data == NULL || data_length < 0) {
        if (data_length < 0) {
            atomic_fetch_add_explicit(&session->malformed_packets, 1,
                                      memory_order_relaxed);
        }
        goto done;
    }
    if ((uint32_t)data_length > session->max_payload_bytes) {
        atomic_fetch_add_explicit(&session->malformed_packets, 1,
                                  memory_order_relaxed);
        goto done;
    }
    if (!esp32_mquickjs_wireless_pool_acquire(&session->rx_free,
                                               &slot_index)) {
        atomic_fetch_add_explicit(&session->dropped_packets, 1,
                                  memory_order_relaxed);
        goto done;
    }
    slot = &session->rx_slots[slot_index];
    slot->generation = session->generation;
    slot->sequence = atomic_fetch_add_explicit(
                         &session->sequence, 1, memory_order_relaxed) + 1U;
    slot->timestamp_us = esp_timer_get_time();
    memcpy(slot->source, receive_info->src_addr, ESPNOW_ADDRESS_BYTES);
    memcpy(slot->destination, receive_info->des_addr, ESPNOW_ADDRESS_BYTES);
    slot->rssi = receive_info->rx_ctrl != NULL
                     ? receive_info->rx_ctrl->rssi
                     : 0;
    slot->channel = receive_info->rx_ctrl != NULL
                        ? receive_info->rx_ctrl->channel
                        : session->channel;
    slot->length = (uint16_t)data_length;
    if (data_length > 0) {
        memcpy(slot->payload, data, (size_t)data_length);
    }
    event.generation = generation;
    event.slot_index = slot_index;
    if (!esp32_mquickjs_event_queue_send(session->event_queue, &event)) {
        espnow_release_rx_slot(session, slot_index);
        atomic_fetch_add_explicit(&session->dropped_packets, 1,
                                  memory_order_relaxed);
        goto done;
    }
    atomic_fetch_add_explicit(&session->received_packets, 1,
                              memory_order_relaxed);
    atomic_fetch_add_explicit(&session->received_bytes,
                              (uint32_t)data_length, memory_order_relaxed);

done:
    atomic_fetch_sub_explicit(&session->callbacks_active, 1,
                              memory_order_release);
}

static void espnow_send_callback(const esp_now_send_info_t *send_info,
                                 esp_now_send_status_t status)
{
    espnow_session_t *session = &s_espnow_session;
    esp32_mquickjs_future_driver_state_t *state;

    atomic_fetch_add_explicit(&session->callbacks_active, 1,
                              memory_order_acq_rel);
    state = atomic_load_explicit(&session->active_send,
                                 memory_order_acquire);
    if (session->lifecycle == ESPNOW_LIFECYCLE_ACTIVE && send_info != NULL &&
        state != NULL && state->generation == session->generation &&
        state->operation == ESPNOW_OPERATION_SEND &&
        !atomic_load_explicit(&state->completed, memory_order_acquire) &&
        memcmp(state->address, send_info->des_addr,
               ESPNOW_ADDRESS_BYTES) == 0) {
        state->mac_delivered = status == ESP_NOW_SEND_SUCCESS;
        state->completed_at_us = esp_timer_get_time();
        if (status == ESP_NOW_SEND_SUCCESS) {
            atomic_fetch_add_explicit(&session->send_successes, 1,
                                      memory_order_relaxed);
        } else {
            atomic_fetch_add_explicit(&session->send_failures, 1,
                                      memory_order_relaxed);
        }
        atomic_store_explicit(&session->active_send, NULL,
                              memory_order_release);
        atomic_store_explicit(&state->completed, true, memory_order_release);
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
    atomic_fetch_sub_explicit(&session->callbacks_active, 1,
                              memory_order_release);
}

static void espnow_reset_session_storage(espnow_session_t *session)
{
    uint32_t i;

    if (session == NULL) {
        return;
    }
    if (session->event_queue != NULL) {
        (void)esp32_mquickjs_event_queue_discard_all(session->event_queue);
    }
    heap_caps_free(session->rx_payloads);
    session->rx_payloads = NULL;
    heap_caps_free(session->rx_slots);
    session->rx_slots = NULL;
    for (i = 0; i < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS; ++i) {
        espnow_peer_slot_t *peer = &session->peers[i];

        espnow_clear_peer(peer);
    }
    session->peer_count = 0;
    session->encrypted_peer_count = 0;
    atomic_store_explicit(&session->active_send, NULL,
                          memory_order_relaxed);
    session->tx_state = ESP32_MQUICKJS_WIRELESS_TX_READY;
    atomic_store_explicit(&session->pending_sends, 0,
                          memory_order_relaxed);
    esp32_mquickjs_wireless_secure_zero(session->pmk, sizeof(session->pmk));
    session->has_pmk = false;
    session->event_queue = NULL;
    session->receive_capacity = 0;
    session->max_payload_bytes = 0;
}

static void espnow_close_native(espnow_session_t *session)
{
    uint32_t waits = 0;

    if (session == NULL ||
        session->lifecycle == ESPNOW_LIFECYCLE_CLOSED ||
        session->lifecycle == ESPNOW_LIFECYCLE_CLOSING) {
        return;
    }
    session->lifecycle = ESPNOW_LIFECYCLE_CLOSING;
    if (session->event_queue != NULL &&
        !esp32_mquickjs_event_queue_is_closed(session->event_queue)) {
        (void)esp32_mquickjs_event_queue_close(session->event_queue);
    }
    if (session->receive_callback_registered) {
        (void)esp_now_unregister_recv_cb();
        session->receive_callback_registered = false;
    }
    if (session->send_callback_registered) {
        (void)esp_now_unregister_send_cb();
        session->send_callback_registered = false;
    }
    while (atomic_load_explicit(&session->callbacks_active,
                                memory_order_acquire) != 0 &&
           waits++ < 1000U) {
        vTaskDelay(1);
    }
    if (session->now_initialized) {
        (void)esp_now_deinit();
        session->now_initialized = false;
    }
    session->broadcast_peer_added = false;
    esp32_mquickjs_wifi_radio_release(&session->radio_lease);
    espnow_reset_session_storage(session);
    session->runtime = NULL;
    session->lifecycle = ESPNOW_LIFECYCLE_CLOSED;
}

static bool espnow_allocate_receive_pool(JSContext *ctx,
                                         espnow_session_t *session,
                                         uint32_t capacity,
                                         uint32_t max_payload_bytes)
{
    uint32_t i;

    session->rx_slots = heap_caps_calloc(
        capacity, sizeof(*session->rx_slots), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    session->rx_payloads = heap_caps_calloc(
        capacity, max_payload_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (session->rx_slots == NULL || session->rx_payloads == NULL ||
        !esp32_mquickjs_wireless_pool_init(&session->rx_free, capacity)) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    session->receive_capacity = capacity;
    session->max_payload_bytes = max_payload_bytes;
    for (i = 0; i < capacity; ++i) {
        session->rx_slots[i].payload =
            session->rx_payloads + ((size_t)i * max_payload_bytes);
    }
    return true;
}

static bool espnow_parse_power_save(
    JSContext *ctx,
    JSValue value,
    esp32_mquickjs_future_driver_state_t *state)
{
    static const char *const allowed[] = {
        "wakeWindowMs", "wakeIntervalMs",
    };
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    uint32_t window;
    uint32_t interval;
    bool result = false;

    if (!espnow_is_object(ctx, value) ||
        !espnow_validate_option_keys(ctx, value, "espNow.open({ powerSave })",
                                     allowed, 2)) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx,
                              "espNow.open({ powerSave }) expects an options object");
        }
        goto done;
    }
    *property = JS_GetPropertyStr(ctx, value, "wakeWindowMs");
    if (JS_IsException(*property) || !espnow_to_u32(ctx, *property, &window) ||
        window == 0 || window > UINT16_MAX) {
        JS_ThrowRangeError(ctx, "wakeWindowMs must be in the range 1..65535");
        goto done;
    }
    *property = JS_GetPropertyStr(ctx, value, "wakeIntervalMs");
    if (JS_IsException(*property) || !espnow_to_u32(ctx, *property, &interval) ||
        interval == 0 || interval > UINT16_MAX || window > interval) {
        JS_ThrowRangeError(
            ctx, "wakeIntervalMs must be in the range wakeWindowMs..65535");
        goto done;
    }
    state->power_save_enabled = true;
    state->wake_window_ms = (uint16_t)window;
    state->wake_interval_ms = (uint16_t)interval;
    result = true;

done:
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static bool espnow_parse_open_options(
    JSContext *ctx,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t *state)
{
    static const char *const allowed[] = {
        "interface", "channel", "maxPayloadBytes", "receiveCapacity",
        "sendTimeoutMs", "pmk", "powerSave",
    };
    JSGCRef property_ref;
    JSValue *property;
    JSValue options = JS_UNDEFINED;
    uint32_t raw;

    state->max_payload_bytes =
        CONFIG_ESP32_MQUICKJS_ESPNOW_DEFAULT_MAX_PAYLOAD_BYTES;
    state->receive_capacity = CONFIG_ESP32_MQUICKJS_ESPNOW_RX_QUEUE_LEN;
    state->send_timeout_ms =
        CONFIG_ESP32_MQUICKJS_ESPNOW_DEFAULT_SEND_TIMEOUT_MS;
    if (argc > 1) {
        JS_ThrowTypeError(ctx, "espNow.open(options?) accepts at most one argument");
        return false;
    }
    if (argc == 0 || JS_IsUndefined(argv[0].val)) {
        return true;
    }
    options = argv[0].val;
    if (!espnow_is_object(ctx, options)) {
        JS_ThrowTypeError(ctx, "espNow.open(options?) expects an options object");
        return false;
    }
    if (!espnow_validate_option_keys(ctx, options, "espNow.open()", allowed, 7)) {
        return false;
    }

    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, options, "interface");
    if (JS_IsException(*property) ||
        (!JS_IsUndefined(*property) &&
         !espnow_string_equals(ctx, *property, "station"))) {
        JS_ThrowTypeError(ctx, "espNow.open({ interface }) only supports 'station'");
        goto fail;
    }
    *property = JS_GetPropertyStr(ctx, options, "channel");
    if (JS_IsException(*property)) {
        goto fail;
    }
    if (!JS_IsUndefined(*property) &&
        !espnow_string_equals(ctx, *property, "current")) {
        if (!espnow_to_u32(ctx, *property, &raw) || raw < 1 || raw > 14) {
            JS_ThrowRangeError(ctx,
                               "espNow.open({ channel }) expects 'current' or 1..14");
            goto fail;
        }
        state->channel_fixed = true;
        state->channel = (uint8_t)raw;
    }
    *property = JS_GetPropertyStr(ctx, options, "maxPayloadBytes");
    if (JS_IsException(*property)) {
        goto fail;
    }
    if (!JS_IsUndefined(*property)) {
        if (!espnow_to_u32(ctx, *property, &raw) || raw == 0 ||
            raw > CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PAYLOAD_BYTES ||
            (!ESPNOW_V2_PAYLOAD_ENABLED &&
             raw > ESP_NOW_MAX_DATA_LEN)) {
            JS_ThrowRangeError(ctx, "maxPayloadBytes exceeds this Build Context");
            goto fail;
        }
        state->max_payload_bytes = raw;
    }
    *property = JS_GetPropertyStr(ctx, options, "receiveCapacity");
    if (JS_IsException(*property)) {
        goto fail;
    }
    if (!JS_IsUndefined(*property)) {
        if (!espnow_to_u32(ctx, *property, &raw) || raw == 0 ||
            raw > CONFIG_ESP32_MQUICKJS_ESPNOW_RX_MAX_QUEUE_LEN ||
            raw > UINT16_MAX) {
            JS_ThrowRangeError(ctx, "receiveCapacity exceeds this Build Context");
            goto fail;
        }
        state->receive_capacity = raw;
    }
    *property = JS_GetPropertyStr(ctx, options, "sendTimeoutMs");
    if (JS_IsException(*property)) {
        goto fail;
    }
    if (!JS_IsUndefined(*property)) {
        if (!espnow_to_u32(ctx, *property, &raw) || raw == 0 || raw > 60000) {
            JS_ThrowRangeError(ctx, "sendTimeoutMs must be in the range 1..60000");
            goto fail;
        }
        state->send_timeout_ms = raw;
    }
    *property = JS_GetPropertyStr(ctx, options, "pmk");
    if (JS_IsException(*property)) {
        goto fail;
    }
    if (!JS_IsUndefined(*property)) {
        esp32_mquickjs_byte_source_t source;
        uint8_t *owned = NULL;
        JSValue error = JS_UNDEFINED;

        if (!esp32_mquickjs_get_byte_source(
                ctx, *property, "espNow.open({ pmk })", &source, &owned,
                &error)) {
            goto fail;
        }
        if (!esp32_mquickjs_wireless_key_length_valid(source.length,
                                                       ESP_NOW_KEY_LEN)) {
            if (owned != NULL) {
                esp32_mquickjs_wireless_secure_zero(owned, source.length);
            }
            esp32_mquickjs_release_byte_source(owned);
            JS_ThrowRangeError(ctx, "ESPNOW_INVALID_KEY: PMK must be exactly 16 bytes");
            goto fail;
        }
        memcpy(state->pmk, source.data, ESP_NOW_KEY_LEN);
        if (owned != NULL) {
            esp32_mquickjs_wireless_secure_zero(owned, source.length);
        }
        esp32_mquickjs_release_byte_source(owned);
        state->has_pmk = true;
    }
    *property = JS_GetPropertyStr(ctx, options, "powerSave");
    if (JS_IsException(*property) ||
        (!JS_IsUndefined(*property) &&
         !espnow_parse_power_save(ctx, *property, state))) {
        goto fail;
    }
    JS_PopGCRef(ctx, &property_ref);
    return true;

fail:
    JS_PopGCRef(ctx, &property_ref);
    return false;
}

static void espnow_open_release(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (!state->transferred &&
        s_espnow_session.generation == state->generation) {
        espnow_close_native(&s_espnow_session);
    }
    if (state->event_queue_rooted) {
        JS_DeleteGCRef(state->ctx, &state->event_queue_ref);
        state->event_queue_rooted = false;
    }
    esp32_mquickjs_wireless_secure_zero(state->pmk, sizeof(state->pmk));
}

static bool espnow_open_capture(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    espnow_session_t *session = &s_espnow_session;
    JSValue event_queue;
    uint32_t generation;

    (void)this_ref;
    if (out_state == NULL) {
        return false;
    }
    *out_state = NULL;
    if (session->lifecycle != ESPNOW_LIFECYCLE_CLOSED) {
        JS_ThrowReferenceError(ctx, "ESPNOW_ALREADY_OPEN: an ESP-NOW session already exists");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    atomic_init(&state->completed, false);
    if (!espnow_parse_open_options(ctx, argc, argv, state)) {
        esp32_mquickjs_wireless_secure_zero(state->pmk, sizeof(state->pmk));
        heap_caps_free(state);
        return false;
    }
    generation = atomic_fetch_add_explicit(
        &s_espnow_next_generation, 1, memory_order_relaxed);
    if (generation == 0) {
        generation = atomic_fetch_add_explicit(
            &s_espnow_next_generation, 1, memory_order_relaxed);
    }
    memset(session, 0, sizeof(*session));
    session->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    session->lifecycle = ESPNOW_LIFECYCLE_OPENING;
    session->runtime = esp32_mquickjs_get_active_runtime();
    session->generation = generation;
    session->send_timeout_ms = state->send_timeout_ms;
    session->channel_fixed = state->channel_fixed;
    session->channel = state->channel;
    session->has_pmk = state->has_pmk;
    session->power_save_enabled = state->power_save_enabled;
    session->wake_window_ms = state->wake_window_ms;
    session->wake_interval_ms = state->wake_interval_ms;
    atomic_init(&session->active_send, NULL);
    atomic_init(&session->pending_sends, 0);
    atomic_init(&session->callbacks_active, 0);
    atomic_init(&session->sequence, 0);
    atomic_init(&session->received_packets, 0);
    atomic_init(&session->received_bytes, 0);
    atomic_init(&session->dropped_packets, 0);
    atomic_init(&session->malformed_packets, 0);
    atomic_init(&session->sent_packets, 0);
    atomic_init(&session->sent_bytes, 0);
    atomic_init(&session->send_successes, 0);
    atomic_init(&session->send_failures, 0);
    atomic_init(&session->send_timeouts, 0);
    if (state->has_pmk) {
        memcpy(session->pmk, state->pmk, ESP_NOW_KEY_LEN);
    }
    state->generation = generation;

    if (!espnow_allocate_receive_pool(
            ctx, session, state->receive_capacity,
            state->max_payload_bytes)) {
        espnow_open_release(state);
        heap_caps_free(state);
        return false;
    }
    event_queue = esp32_mquickjs_event_queue_new(
        ctx, session->runtime, sizeof(espnow_receive_event_t),
        state->receive_capacity, ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
        espnow_receive_event_to_js, espnow_receive_event_drop,
        espnow_event_queue_close, session);
    if (JS_IsException(event_queue)) {
        espnow_open_release(state);
        heap_caps_free(state);
        return false;
    }
    *JS_AddGCRef(ctx, &state->event_queue_ref) = event_queue;
    state->event_queue_rooted = true;
    session->event_queue = esp32_mquickjs_event_queue_from_value(
        ctx, event_queue);
    if (session->event_queue == NULL ||
        esp32_mquickjs_wifi_radio_acquire(
            ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW, WIFI_MODE_STA,
            &session->radio_lease) != ESP_OK) {
        if (!JS_HasException(ctx)) {
            JS_ThrowInternalError(ctx, "failed to reserve ESP-NOW radio resources");
        }
        espnow_open_release(state);
        heap_caps_free(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool espnow_open_start(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    espnow_session_t *session = &s_espnow_session;
    wifi_second_chan_t secondary;
    esp_now_peer_info_t broadcast_peer = {0};
    uint8_t actual_channel = 0;
    uint32_t actual_generation = 0;

    if (state == NULL || session->generation != state->generation ||
        session->lifecycle != ESPNOW_LIFECYCLE_OPENING) {
        JS_ThrowReferenceError(ctx, "ESPNOW_STALE_SESSION: open reservation is stale");
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    state->failed_step = "wifi_radio_ensure_started";
    state->err = esp32_mquickjs_wifi_radio_ensure_started(
        &session->radio_lease);
    if (state->err == ESP_OK && state->channel_fixed) {
        state->failed_step = "wifi_radio_set_channel";
        state->err = esp32_mquickjs_wifi_radio_set_channel(
            &session->radio_lease, state->channel, WIFI_SECOND_CHAN_NONE);
    }
    if (state->err == ESP_OK) {
        state->failed_step = "wifi_radio_get_channel";
        state->err = esp32_mquickjs_wifi_radio_get_channel(
            &session->channel, &secondary, &session->channel_generation);
    }
    if (state->err == ESP_OK) {
        state->failed_step = "esp_now_init";
        state->err = esp_now_init();
        session->now_initialized = state->err == ESP_OK;
    }
    if (state->err == ESP_OK) {
        state->failed_step = "esp_now_register_recv_cb";
        state->err = esp_now_register_recv_cb(espnow_receive_callback);
        session->receive_callback_registered = state->err == ESP_OK;
    }
    if (state->err == ESP_OK) {
        state->failed_step = "esp_now_register_send_cb";
        state->err = esp_now_register_send_cb(espnow_send_callback);
        session->send_callback_registered = state->err == ESP_OK;
    }
    if (state->err == ESP_OK && session->has_pmk) {
        state->failed_step = "esp_now_set_pmk";
        state->err = esp_now_set_pmk(session->pmk);
    }
    if (state->err == ESP_OK) {
        memcpy(broadcast_peer.peer_addr, s_broadcast_address,
               ESPNOW_ADDRESS_BYTES);
        broadcast_peer.channel = 0;
        broadcast_peer.ifidx = WIFI_IF_STA;
        broadcast_peer.encrypt = false;
        state->failed_step = "esp_now_add_broadcast_peer";
        state->err = esp_now_add_peer(&broadcast_peer);
        session->broadcast_peer_added = state->err == ESP_OK;
    }
    if (state->err == ESP_OK && session->power_save_enabled) {
        state->failed_step = "esp_now_set_wake_window";
        state->err = esp_now_set_wake_window(session->wake_window_ms);
        if (state->err == ESP_OK) {
            state->failed_step = "esp_now_set_wake_interval";
            state->err = esp_wifi_connectionless_module_set_wake_interval(
                session->wake_interval_ms);
        }
    }
    if (state->err == ESP_OK) {
        state->failed_step = "wifi_radio_confirm_channel";
        state->err = esp32_mquickjs_wifi_radio_get_channel(
            &actual_channel, &secondary, &actual_generation);
        if (state->err == ESP_OK && state->channel_fixed &&
            actual_channel != state->channel) {
            state->err = ESP_ERR_ESPNOW_CHAN;
        }
        if (state->err == ESP_OK) {
            session->channel = actual_channel;
            session->channel_generation = actual_generation;
        }
    }
    if (state->err != ESP_OK) {
        session->lifecycle = ESPNOW_LIFECYCLE_FAILED;
        ESP_LOGE(TAG, "open failed at %s: %s (0x%x)",
                 state->failed_step != NULL ? state->failed_step : "unknown",
                 esp_err_to_name(state->err), (unsigned int)state->err);
        espnow_throw_error(ctx, "ESPNOW_NOT_OPEN", state->err, NULL,
                           session->channel > 0 ? (int)session->channel : -1);
        return false;
    }
    state->failed_step = NULL;
    session->lifecycle = ESPNOW_LIFECYCLE_ACTIVE;
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static esp32_mquickjs_future_poll_t espnow_open_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && atomic_load_explicit(
                                &state->completed, memory_order_acquire)
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue espnow_open_finish(
    JSContext *ctx,
    esp32_mquickjs_future_driver_state_t *state)
{
    espnow_session_ref_t *ref;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);

    *object = JS_UNDEFINED;
    if (state == NULL || state->cancelled ||
        s_espnow_session.generation != state->generation ||
        s_espnow_session.lifecycle != ESPNOW_LIFECYCLE_ACTIVE) {
        JS_ThrowReferenceError(ctx, "ESPNOW_NOT_OPEN: ESP-NOW open was cancelled");
        goto fail;
    }
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_ESPNOW_SESSION);
    if (JS_IsException(*object)) {
        goto fail;
    }
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ref->generation = state->generation;
    JS_SetOpaque(ctx, *object, ref);
    if (!esp32_mquickjs_set_property_ref(
            ctx, object, "_eventQueue", state->event_queue_ref.val)) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
        goto fail;
    }
    state->transferred = true;
    esp32_mquickjs_wireless_secure_zero(state->pmk, sizeof(state->pmk));
    return JS_PopGCRef(ctx, &object_ref);

fail:
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static esp32_mquickjs_cancel_result_t espnow_open_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->started) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancelled = true;
    atomic_store_explicit(&state->completed, true, memory_order_release);
    return ESP32_MQUICKJS_CANCELLED;
}

static void espnow_open_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    espnow_open_release(state);
    heap_caps_free(state);
}

static esp32_mquickjs_resource_key_t espnow_open_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    (void)state;
    return esp32_mquickjs_wifi_radio_channel_key();
}

static const esp32_mquickjs_future_driver_t s_espnow_open_driver = {
    .capture = espnow_open_capture,
    .start = espnow_open_start,
    .poll = espnow_open_poll,
    .finish = espnow_open_finish,
    .cancel = espnow_open_cancel,
    .destroy = espnow_open_destroy,
    .resource_key = espnow_open_resource_key,
};

static espnow_session_t *espnow_session_from_value(
    JSContext *ctx,
    JSValue value,
    bool throw_if_stale)
{
    espnow_session_ref_t *ref;

    if (JS_GetClassID(ctx, value) != JS_CLASS_ESPNOW_SESSION ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "expected an EspNowSession instance");
        return NULL;
    }
    if (s_espnow_session.lifecycle != ESPNOW_LIFECYCLE_ACTIVE ||
        ref->generation != s_espnow_session.generation) {
        if (throw_if_stale) {
            JS_ThrowReferenceError(ctx,
                                   "ESPNOW_STALE_SESSION: ESP-NOW session is closed");
        }
        return NULL;
    }
    return &s_espnow_session;
}

static bool espnow_retain_owner(
    JSContext *ctx,
    JSValue owner,
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return false;
    }
    *JS_AddGCRef(ctx, &state->owner_ref) = owner;
    state->owner_rooted = true;
    return true;
}

static bool espnow_parse_peer_options(
    JSContext *ctx,
    JSValue options,
    bool updating,
    const espnow_peer_slot_t *existing,
    esp32_mquickjs_future_driver_state_t *state)
{
    static const char *const add_allowed[] = {
        "address", "channel", "encrypted", "lmk",
    };
    static const char *const update_allowed[] = {
        "channel", "encrypted", "lmk",
    };
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    uint32_t raw;
    bool result = false;

    if (!espnow_is_object(ctx, options) ||
        !espnow_validate_option_keys(
            ctx, options, updating ? "EspNowPeer.update()"
                                   : "EspNowSession.addPeer()",
            updating ? update_allowed : add_allowed,
            updating ? 3U : 4U)) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx, "ESP-NOW peer options must be an object");
        }
        goto done;
    }
    if (updating) {
        memcpy(state->address, existing->address, ESPNOW_ADDRESS_BYTES);
        state->peer_channel = existing->channel;
        state->encrypted = existing->encrypted;
        state->has_lmk = existing->encrypted;
        if (existing->encrypted) {
            memcpy(state->lmk, existing->lmk, ESP_NOW_KEY_LEN);
        }
    } else {
        *property = JS_GetPropertyStr(ctx, options, "address");
        if (JS_IsException(*property) ||
            !espnow_parse_address(ctx, *property,
                                  "EspNowSession.addPeer({ address })", false,
                                  state->address)) {
            goto done;
        }
    }
    *property = JS_GetPropertyStr(ctx, options, "channel");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (espnow_string_equals(ctx, *property, "current")) {
            state->peer_channel = 0;
        } else if (!espnow_to_u32(ctx, *property, &raw) || raw < 1 ||
                   raw > 14 || raw != s_espnow_session.channel) {
            espnow_throw_error(ctx, "ESPNOW_CHANNEL_MISMATCH",
                               ESP_ERR_ESPNOW_CHAN, state->address,
                               (int)s_espnow_session.channel);
            goto done;
        } else {
            state->peer_channel = (uint8_t)raw;
        }
    }
    *property = JS_GetPropertyStr(ctx, options, "encrypted");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (*property != JS_TRUE && *property != JS_FALSE) {
            JS_ThrowTypeError(ctx, "encrypted must be a boolean");
            goto done;
        }
        state->encrypted = *property == JS_TRUE;
        if (!state->encrypted) {
            esp32_mquickjs_wireless_secure_zero(state->lmk,
                                                 sizeof(state->lmk));
            state->has_lmk = false;
        }
    }
    *property = JS_GetPropertyStr(ctx, options, "lmk");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        esp32_mquickjs_byte_source_t source;
        uint8_t *owned = NULL;
        JSValue error = JS_UNDEFINED;

        if (!esp32_mquickjs_get_byte_source(
                ctx, *property, "ESP-NOW LMK", &source, &owned, &error)) {
            goto done;
        }
        if (!esp32_mquickjs_wireless_key_length_valid(source.length,
                                                       ESP_NOW_KEY_LEN)) {
            if (owned != NULL) {
                esp32_mquickjs_wireless_secure_zero(owned, source.length);
            }
            esp32_mquickjs_release_byte_source(owned);
            espnow_throw_error(ctx, "ESPNOW_INVALID_KEY", ESP_ERR_INVALID_ARG,
                               state->address, s_espnow_session.channel);
            goto done;
        }
        memcpy(state->lmk, source.data, ESP_NOW_KEY_LEN);
        if (owned != NULL) {
            esp32_mquickjs_wireless_secure_zero(owned, source.length);
        }
        esp32_mquickjs_release_byte_source(owned);
        state->has_lmk = true;
    }
    if (state->encrypted &&
        (!s_espnow_session.has_pmk || !state->has_lmk)) {
        espnow_throw_error(ctx, "ESPNOW_INVALID_KEY", ESP_ERR_INVALID_ARG,
                           state->address, s_espnow_session.channel);
        goto done;
    }
    if (!state->encrypted && state->has_lmk) {
        espnow_throw_error(ctx, "ESPNOW_INVALID_KEY", ESP_ERR_INVALID_ARG,
                           state->address, s_espnow_session.channel);
        goto done;
    }
    result = true;

done:
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static bool espnow_peer_add_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    espnow_peer_slot_t *slot = NULL;
    uint16_t i;
    uint32_t peer_generation;

    if (out_state == NULL || argc != 1 ||
        espnow_session_from_value(ctx, this_ref->val, true) == NULL) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx,
                              "EspNowSession.addPeer(options) expects one options object");
        }
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = ESPNOW_OPERATION_ADD_PEER;
    state->generation = s_espnow_session.generation;
    atomic_init(&state->completed, false);
    if (!espnow_parse_peer_options(ctx, argv[0].val, false, NULL, state)) {
        goto fail;
    }
    if (espnow_find_peer_by_address(
            &s_espnow_session, state->address, NULL) != NULL) {
        espnow_throw_error(ctx, "ESPNOW_PEER_TABLE_FULL",
                           ESP_ERR_ESPNOW_EXIST, state->address,
                           s_espnow_session.channel);
        goto fail;
    }
    if (state->encrypted &&
        s_espnow_session.encrypted_peer_count >=
            CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM) {
        espnow_throw_error(ctx, "ESPNOW_ENCRYPTED_PEER_LIMIT",
                           ESP_ERR_ESPNOW_FULL, state->address,
                           s_espnow_session.channel);
        goto fail;
    }
    for (i = 0; i < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS; ++i) {
        if (!s_espnow_session.peers[i].allocated &&
            !s_espnow_session.peers[i].reserved) {
            slot = &s_espnow_session.peers[i];
            state->peer_index = i;
            break;
        }
    }
    if (slot == NULL) {
        espnow_throw_error(ctx, "ESPNOW_PEER_TABLE_FULL",
                           ESP_ERR_ESPNOW_FULL, state->address,
                           s_espnow_session.channel);
        goto fail;
    }
    peer_generation = atomic_fetch_add_explicit(
        &s_espnow_next_peer_generation, 1, memory_order_relaxed);
    if (peer_generation == 0) {
        peer_generation = atomic_fetch_add_explicit(
            &s_espnow_next_peer_generation, 1, memory_order_relaxed);
    }
    slot->reserved = true;
    slot->generation = peer_generation;
    state->peer_generation = peer_generation;
    state->peer_reserved = true;
    espnow_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;

fail:
    esp32_mquickjs_wireless_secure_zero(state->lmk, sizeof(state->lmk));
    heap_caps_free(state);
    return false;
}

static bool espnow_peer_update_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    espnow_peer_ref_t *ref = NULL;
    espnow_peer_slot_t *peer;

    if (out_state == NULL || argc != 1 ||
        (peer = espnow_peer_from_value(
             ctx, this_ref->val, &ref, true)) == NULL) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx,
                              "EspNowPeer.update(options) expects one options object");
        }
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = ESPNOW_OPERATION_UPDATE_PEER;
    state->generation = ref->session_generation;
    state->peer_index = ref->peer_index;
    state->peer_generation = ref->peer_generation;
    atomic_init(&state->completed, false);
    if (!espnow_parse_peer_options(
            ctx, argv[0].val, true, peer, state)) {
        esp32_mquickjs_wireless_secure_zero(state->lmk, sizeof(state->lmk));
        heap_caps_free(state);
        return false;
    }
    if (!peer->encrypted && state->encrypted &&
        s_espnow_session.encrypted_peer_count >=
            CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM) {
        esp32_mquickjs_wireless_secure_zero(state->lmk, sizeof(state->lmk));
        heap_caps_free(state);
        espnow_throw_error(ctx, "ESPNOW_ENCRYPTED_PEER_LIMIT",
                           ESP_ERR_ESPNOW_FULL, peer->address,
                           s_espnow_session.channel);
        return false;
    }
    espnow_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
}

static bool espnow_peer_close_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    espnow_peer_ref_t *ref = NULL;
    espnow_peer_slot_t *peer;

    (void)argv;
    if (out_state == NULL || argc != 0) {
        JS_ThrowTypeError(ctx, "EspNowPeer.close() expects no arguments");
        return false;
    }
    peer = espnow_peer_from_value(ctx, this_ref->val, &ref, true);
    if (peer == NULL) {
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = ESPNOW_OPERATION_CLOSE_PEER;
    state->generation = ref->session_generation;
    state->peer_index = ref->peer_index;
    state->peer_generation = ref->peer_generation;
    memcpy(state->address, peer->address, ESPNOW_ADDRESS_BYTES);
    atomic_init(&state->completed, false);
    espnow_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
}

static bool espnow_power_save_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    if (out_state == NULL || argc != 1 ||
        espnow_session_from_value(ctx, this_ref->val, true) == NULL) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx,
                              "EspNowSession.setPowerSave(options) expects one options object");
        }
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = ESPNOW_OPERATION_SET_POWER_SAVE;
    state->generation = s_espnow_session.generation;
    atomic_init(&state->completed, false);
    if (!espnow_parse_power_save(ctx, argv[0].val, state)) {
        heap_caps_free(state);
        return false;
    }
    espnow_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
}

static bool espnow_session_close_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    espnow_session_ref_t *ref;

    (void)argv;
    if (out_state == NULL || argc != 0 ||
        JS_GetClassID(ctx, this_ref->val) != JS_CLASS_ESPNOW_SESSION ||
        (ref = JS_GetOpaque(ctx, this_ref->val)) == NULL) {
        JS_ThrowTypeError(ctx, "EspNowSession.close() expects no arguments");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = ESPNOW_OPERATION_CLOSE_SESSION;
    state->generation = ref->generation;
    state->result_bool = ref->generation == s_espnow_session.generation &&
                         s_espnow_session.lifecycle == ESPNOW_LIFECYCLE_ACTIVE;
    atomic_init(&state->completed, false);
    espnow_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
}

static bool espnow_control_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    espnow_session_t *session = &s_espnow_session;
    espnow_peer_slot_t *peer = NULL;
    esp_now_peer_info_t peer_info = {0};

    if (state == NULL) {
        JS_ThrowInternalError(ctx, "ESP-NOW control state is missing");
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (state->operation != ESPNOW_OPERATION_CLOSE_SESSION &&
        (session->lifecycle != ESPNOW_LIFECYCLE_ACTIVE ||
         state->generation != session->generation)) {
        espnow_throw_error(ctx, "ESPNOW_NOT_OPEN",
                           ESP_ERR_ESPNOW_NOT_INIT, NULL, -1);
        return false;
    }
    switch (state->operation) {
    case ESPNOW_OPERATION_ADD_PEER:
        peer = state->peer_index < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS
                   ? &session->peers[state->peer_index]
                   : NULL;
        if (peer == NULL || !peer->reserved ||
            peer->generation != state->peer_generation) {
            espnow_throw_error(ctx, "ESPNOW_STALE_PEER",
                               ESP_ERR_INVALID_STATE, state->address,
                               session->channel);
            return false;
        }
        memcpy(peer_info.peer_addr, state->address, ESPNOW_ADDRESS_BYTES);
        peer_info.channel = state->peer_channel;
        peer_info.ifidx = WIFI_IF_STA;
        peer_info.encrypt = state->encrypted;
        if (state->encrypted) {
            memcpy(peer_info.lmk, state->lmk, ESP_NOW_KEY_LEN);
        }
        state->err = esp_now_add_peer(&peer_info);
        if (state->err == ESP_OK) {
            memcpy(peer->address, state->address, ESPNOW_ADDRESS_BYTES);
            peer->channel = state->peer_channel;
            peer->encrypted = state->encrypted;
            if (state->encrypted) {
                memcpy(peer->lmk, state->lmk, ESP_NOW_KEY_LEN);
            }
            peer->allocated = true;
            peer->reserved = false;
            state->peer_reserved = false;
            session->peer_count++;
            if (peer->encrypted) {
                session->encrypted_peer_count++;
            }
        }
        break;
    case ESPNOW_OPERATION_UPDATE_PEER:
        peer = state->peer_index < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS
                   ? &session->peers[state->peer_index]
                   : NULL;
        if (peer == NULL || !peer->allocated ||
            peer->generation != state->peer_generation) {
            espnow_throw_error(ctx, "ESPNOW_STALE_PEER",
                               ESP_ERR_INVALID_STATE, state->address,
                               session->channel);
            return false;
        }
        memcpy(peer_info.peer_addr, state->address, ESPNOW_ADDRESS_BYTES);
        peer_info.channel = state->peer_channel;
        peer_info.ifidx = WIFI_IF_STA;
        peer_info.encrypt = state->encrypted;
        if (state->encrypted) {
            memcpy(peer_info.lmk, state->lmk, ESP_NOW_KEY_LEN);
        }
        state->err = esp_now_mod_peer(&peer_info);
        if (state->err == ESP_OK) {
            if (peer->encrypted != state->encrypted) {
                if (state->encrypted) {
                    session->encrypted_peer_count++;
                } else if (session->encrypted_peer_count > 0) {
                    session->encrypted_peer_count--;
                }
            }
            esp32_mquickjs_wireless_secure_zero(peer->lmk,
                                                 sizeof(peer->lmk));
            peer->channel = state->peer_channel;
            peer->encrypted = state->encrypted;
            if (state->encrypted) {
                memcpy(peer->lmk, state->lmk, ESP_NOW_KEY_LEN);
            }
        }
        break;
    case ESPNOW_OPERATION_CLOSE_PEER:
        peer = state->peer_index < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS
                   ? &session->peers[state->peer_index]
                   : NULL;
        if (peer == NULL || !peer->allocated ||
            peer->generation != state->peer_generation) {
            state->result_bool = false;
            state->err = ESP_OK;
            break;
        }
        state->err = esp_now_del_peer(peer->address);
        if (state->err == ESP_OK || state->err == ESP_ERR_ESPNOW_NOT_FOUND) {
            if (peer->encrypted && session->encrypted_peer_count > 0) {
                session->encrypted_peer_count--;
            }
            if (session->peer_count > 0) {
                session->peer_count--;
            }
            espnow_clear_peer(peer);
            state->result_bool = true;
            state->err = ESP_OK;
        }
        break;
    case ESPNOW_OPERATION_SET_POWER_SAVE:
        state->err = esp_now_set_wake_window(state->wake_window_ms);
        if (state->err == ESP_OK) {
            state->err = esp_wifi_connectionless_module_set_wake_interval(
                state->wake_interval_ms);
        }
        if (state->err == ESP_OK) {
            session->power_save_enabled = true;
            session->wake_window_ms = state->wake_window_ms;
            session->wake_interval_ms = state->wake_interval_ms;
            state->result_bool = true;
        }
        break;
    case ESPNOW_OPERATION_CLOSE_SESSION:
        if (state->result_bool) {
            (void)esp32_mquickjs_event_queue_close(session->event_queue);
        }
        state->err = ESP_OK;
        break;
    default:
        JS_ThrowInternalError(ctx, "invalid ESP-NOW control operation");
        return false;
    }
    if (state->err != ESP_OK) {
        const char *code = "ESPNOW_SEND_FAILED";

        if (state->operation == ESPNOW_OPERATION_ADD_PEER &&
            (state->err == ESP_ERR_ESPNOW_FULL ||
             state->err == ESP_ERR_ESPNOW_EXIST)) {
            code = "ESPNOW_PEER_TABLE_FULL";
        } else if ((state->operation == ESPNOW_OPERATION_UPDATE_PEER ||
                    state->operation == ESPNOW_OPERATION_CLOSE_PEER) &&
                   state->err == ESP_ERR_ESPNOW_NOT_FOUND) {
            code = "ESPNOW_PEER_NOT_FOUND";
        } else if (state->operation == ESPNOW_OPERATION_SET_POWER_SAVE) {
            code = "ESPNOW_NOT_SUPPORTED";
        }
        espnow_throw_error(ctx, code, state->err,
                           state->address, session->channel);
        return false;
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static esp32_mquickjs_future_poll_t espnow_control_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && atomic_load_explicit(
                                &state->completed, memory_order_acquire)
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue espnow_control_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    espnow_peer_slot_t *peer;

    if (state == NULL || state->cancelled) {
        return JS_ThrowInternalError(ctx, "ESP-NOW control operation cancelled");
    }
    if (state->operation == ESPNOW_OPERATION_ADD_PEER) {
        peer = state->peer_index < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS
                   ? &s_espnow_session.peers[state->peer_index]
                   : NULL;
        if (peer == NULL || !peer->allocated ||
            peer->generation != state->peer_generation) {
            return espnow_throw_error(ctx, "ESPNOW_STALE_PEER",
                                      ESP_ERR_INVALID_STATE, state->address,
                                      s_espnow_session.channel);
        }
        return espnow_new_peer_handle(ctx, state->peer_index, peer);
    }
    if (state->operation == ESPNOW_OPERATION_UPDATE_PEER) {
        peer = state->peer_index < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS
                   ? &s_espnow_session.peers[state->peer_index]
                   : NULL;
        return espnow_peer_status_to_js(ctx, peer);
    }
    return JS_NewBool(state->result_bool);
}

static esp32_mquickjs_cancel_result_t espnow_control_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->started) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancelled = true;
    atomic_store_explicit(&state->completed, true, memory_order_release);
    return ESP32_MQUICKJS_CANCELLED;
}

static void espnow_control_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->peer_reserved &&
        state->generation == s_espnow_session.generation &&
        state->peer_index < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS) {
        espnow_peer_slot_t *peer = &s_espnow_session.peers[state->peer_index];

        if (peer->reserved && peer->generation == state->peer_generation) {
            espnow_clear_peer(peer);
        }
    }
    if (state->owner_rooted) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
    }
    esp32_mquickjs_wireless_secure_zero(state->lmk, sizeof(state->lmk));
    heap_caps_free(state);
}

static esp32_mquickjs_resource_key_t espnow_control_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL && state->operation == ESPNOW_OPERATION_CLOSE_SESSION) {
        return &s_espnow_tx_lane_key;
    }
    return &s_espnow_control_lane_key;
}

static const esp32_mquickjs_future_driver_t s_espnow_peer_add_driver = {
    .capture = espnow_peer_add_capture,
    .start = espnow_control_start,
    .poll = espnow_control_poll,
    .finish = espnow_control_finish,
    .cancel = espnow_control_cancel,
    .destroy = espnow_control_destroy,
    .resource_key = espnow_control_resource_key,
};

static const esp32_mquickjs_future_driver_t s_espnow_peer_update_driver = {
    .capture = espnow_peer_update_capture,
    .start = espnow_control_start,
    .poll = espnow_control_poll,
    .finish = espnow_control_finish,
    .cancel = espnow_control_cancel,
    .destroy = espnow_control_destroy,
    .resource_key = espnow_control_resource_key,
};

static const esp32_mquickjs_future_driver_t s_espnow_peer_close_driver = {
    .capture = espnow_peer_close_capture,
    .start = espnow_control_start,
    .poll = espnow_control_poll,
    .finish = espnow_control_finish,
    .cancel = espnow_control_cancel,
    .destroy = espnow_control_destroy,
    .resource_key = espnow_control_resource_key,
};

static const esp32_mquickjs_future_driver_t s_espnow_power_save_driver = {
    .capture = espnow_power_save_capture,
    .start = espnow_control_start,
    .poll = espnow_control_poll,
    .finish = espnow_control_finish,
    .cancel = espnow_control_cancel,
    .destroy = espnow_control_destroy,
    .resource_key = espnow_control_resource_key,
};

static const esp32_mquickjs_future_driver_t s_espnow_session_close_driver = {
    .capture = espnow_session_close_capture,
    .start = espnow_control_start,
    .poll = espnow_control_poll,
    .finish = espnow_control_finish,
    .cancel = espnow_control_cancel,
    .destroy = espnow_control_destroy,
    .resource_key = espnow_control_resource_key,
};

static bool espnow_parse_send_timeout(
    JSContext *ctx,
    JSValue options,
    uint32_t *timeout_ms)
{
    static const char *const allowed[] = {"timeoutMs"};
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    uint32_t raw;
    bool result = false;

    if (JS_IsUndefined(options)) {
        result = true;
        goto done;
    }
    if (!espnow_is_object(ctx, options) ||
        !espnow_validate_option_keys(ctx, options, "ESP-NOW send()",
                                     allowed, 1)) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx, "ESP-NOW send options must be an object");
        }
        goto done;
    }
    *property = JS_GetPropertyStr(ctx, options, "timeoutMs");
    if (JS_IsException(*property)) {
        goto done;
    }
    if (!JS_IsUndefined(*property)) {
        if (!espnow_to_u32(ctx, *property, &raw) || raw == 0 || raw > 60000) {
            JS_ThrowRangeError(ctx, "timeoutMs must be in the range 1..60000");
            goto done;
        }
        *timeout_ms = raw;
    }
    result = true;

done:
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static bool espnow_send_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    espnow_peer_ref_t *peer_ref = NULL;
    espnow_peer_slot_t *peer = NULL;
    espnow_session_t *session = NULL;
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue error = JS_UNDEFINED;
    uint8_t actual_channel;
    wifi_second_chan_t secondary;
    uint32_t channel_generation;

    if (out_state == NULL || argc < 1 || argc > 2) {
        JS_ThrowTypeError(ctx, "ESP-NOW send(data, options?) expects one ByteSource");
        return false;
    }
    if (JS_GetClassID(ctx, this_ref->val) == JS_CLASS_ESPNOW_PEER) {
        peer = espnow_peer_from_value(ctx, this_ref->val, &peer_ref, true);
        if (peer == NULL) {
            return false;
        }
        session = &s_espnow_session;
    } else if (JS_GetClassID(ctx, this_ref->val) == JS_CLASS_ESPNOW_SESSION) {
        session = espnow_session_from_value(ctx, this_ref->val, true);
        if (session == NULL) {
            return false;
        }
    } else {
        JS_ThrowTypeError(ctx,
                          "ESP-NOW send requires an EspNowPeer or EspNowSession receiver");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = ESPNOW_OPERATION_SEND;
    state->generation = session->generation;
    state->send_timeout_ms = session->send_timeout_ms;
    atomic_init(&state->completed, false);
    if (peer != NULL) {
        state->peer_index = peer_ref->peer_index;
        state->peer_generation = peer_ref->peer_generation;
        state->peer_channel = peer->channel;
        memcpy(state->address, peer->address, ESPNOW_ADDRESS_BYTES);
    } else {
        memcpy(state->address, s_broadcast_address, ESPNOW_ADDRESS_BYTES);
    }
    if (!espnow_parse_send_timeout(
            ctx, argc == 2 ? argv[1].val : JS_UNDEFINED,
            &state->send_timeout_ms)) {
        goto fail;
    }
    if (!esp32_mquickjs_get_byte_source(
            ctx, argv[0].val, "ESP-NOW send(data)", &source, &owned,
            &error)) {
        goto fail;
    }
    if (source.length > session->max_payload_bytes) {
        esp32_mquickjs_release_byte_source(owned);
        owned = NULL;
        espnow_throw_error(ctx, "ESPNOW_PAYLOAD_TOO_LARGE",
                           ESP_ERR_ESPNOW_ARG, state->address,
                           session->channel);
        goto fail;
    }
    state->payload = heap_caps_malloc(
        source.length > 0 ? source.length : 1U, MALLOC_CAP_8BIT);
    if (state->payload == NULL) {
        esp32_mquickjs_release_byte_source(owned);
        owned = NULL;
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    state->payload_length = source.length;
    if (source.length > 0) {
        memcpy(state->payload, source.data, source.length);
    }
    esp32_mquickjs_release_byte_source(owned);
    owned = NULL;
    if (esp32_mquickjs_wifi_radio_get_channel(
            &actual_channel, &secondary, &channel_generation) != ESP_OK ||
        actual_channel != session->channel ||
        channel_generation != session->channel_generation ||
        (state->peer_channel != 0 &&
         state->peer_channel != actual_channel)) {
        espnow_throw_error(ctx, "ESPNOW_CHANNEL_MISMATCH",
                           ESP_ERR_ESPNOW_CHAN, state->address,
                           session->channel);
        goto fail;
    }
    espnow_retain_owner(ctx, this_ref->val, state);
    atomic_fetch_add_explicit(&session->pending_sends, 1,
                              memory_order_relaxed);
    state->send_reserved = true;
    *out_state = state;
    return true;

fail:
    esp32_mquickjs_release_byte_source(owned);
    heap_caps_free(state->payload);
    heap_caps_free(state);
    return false;
}

static bool espnow_send_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    espnow_session_t *session = &s_espnow_session;

    if (state == NULL || state->generation != session->generation ||
        session->lifecycle != ESPNOW_LIFECYCLE_ACTIVE) {
        espnow_throw_error(ctx, "ESPNOW_NOT_OPEN",
                           ESP_ERR_ESPNOW_NOT_INIT,
                           state != NULL ? state->address : NULL, -1);
        return false;
    }
    if (session->tx_state != ESP32_MQUICKJS_WIRELESS_TX_READY ||
        atomic_load_explicit(&session->active_send,
                             memory_order_acquire) != NULL) {
        espnow_throw_error(ctx, "ESPNOW_CLOSING", ESP_ERR_INVALID_STATE,
                           state->address, session->channel);
        return false;
    }
    if (state->peer_generation != 0) {
        espnow_peer_slot_t *peer =
            state->peer_index < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS
                ? &session->peers[state->peer_index]
                : NULL;

        if (peer == NULL || !peer->allocated ||
            peer->generation != state->peer_generation) {
            espnow_throw_error(ctx, "ESPNOW_STALE_PEER",
                               ESP_ERR_ESPNOW_NOT_FOUND, state->address,
                               session->channel);
            return false;
        }
    }
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    atomic_store_explicit(&session->active_send, state,
                          memory_order_release);
    state->err = esp_now_send(state->address, state->payload,
                              state->payload_length);
    if (state->err != ESP_OK) {
        atomic_store_explicit(&session->active_send, NULL,
                              memory_order_release);
        atomic_fetch_add_explicit(&session->send_failures, 1,
                                  memory_order_relaxed);
        espnow_throw_error(ctx, "ESPNOW_SEND_FAILED", state->err,
                           state->address, session->channel);
        return false;
    }
    atomic_fetch_add_explicit(&session->sent_packets, 1,
                              memory_order_relaxed);
    atomic_fetch_add_explicit(&session->sent_bytes,
                              (uint32_t)state->payload_length,
                              memory_order_relaxed);
    return true;
}

static esp_err_t espnow_restore_native_session(espnow_session_t *session)
{
    esp_now_peer_info_t peer_info = {0};
    esp_err_t err;
    uint32_t i;

    err = esp_now_init();
    if (err != ESP_OK) {
        return err;
    }
    session->now_initialized = true;
    err = esp_now_register_recv_cb(espnow_receive_callback);
    if (err != ESP_OK) {
        return err;
    }
    session->receive_callback_registered = true;
    err = esp_now_register_send_cb(espnow_send_callback);
    if (err != ESP_OK) {
        return err;
    }
    session->send_callback_registered = true;
    if (session->has_pmk) {
        err = esp_now_set_pmk(session->pmk);
        if (err != ESP_OK) {
            return err;
        }
    }
    memset(&peer_info, 0, sizeof(peer_info));
    memcpy(peer_info.peer_addr, s_broadcast_address,
           ESPNOW_ADDRESS_BYTES);
    peer_info.ifidx = WIFI_IF_STA;
    err = esp_now_add_peer(&peer_info);
    if (err != ESP_OK) {
        return err;
    }
    session->broadcast_peer_added = true;
    for (i = 0; i < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS; ++i) {
        espnow_peer_slot_t *peer = &session->peers[i];

        if (!peer->allocated) {
            continue;
        }
        memset(&peer_info, 0, sizeof(peer_info));
        memcpy(peer_info.peer_addr, peer->address, ESPNOW_ADDRESS_BYTES);
        peer_info.channel = peer->channel;
        peer_info.ifidx = WIFI_IF_STA;
        peer_info.encrypt = peer->encrypted;
        if (peer->encrypted) {
            memcpy(peer_info.lmk, peer->lmk, ESP_NOW_KEY_LEN);
        }
        err = esp_now_add_peer(&peer_info);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (session->power_save_enabled) {
        err = esp_now_set_wake_window(session->wake_window_ms);
        if (err == ESP_OK) {
            err = esp_wifi_connectionless_module_set_wake_interval(
                session->wake_interval_ms);
        }
    }
    return err;
}

static esp_err_t espnow_recover_after_timeout(
    espnow_session_t *session,
    esp32_mquickjs_future_driver_state_t *state)
{
    esp_err_t err = ESP_OK;
    uint32_t waits = 0;

    if (session == NULL || state == NULL ||
        session->generation != state->generation) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!esp32_mquickjs_wireless_tx_timeout(&session->tx_state) ||
        !esp32_mquickjs_wireless_tx_begin_recovery(&session->tx_state))
        return ESP_ERR_INVALID_STATE;
    if (session->receive_callback_registered) {
        err = esp_now_unregister_recv_cb();
        session->receive_callback_registered = false;
    }
    if (session->send_callback_registered) {
        esp_err_t send_err = esp_now_unregister_send_cb();

        session->send_callback_registered = false;
        if (err == ESP_OK) {
            err = send_err;
        }
    }
    while (atomic_load_explicit(&session->callbacks_active,
                                memory_order_acquire) != 0 &&
           waits++ < 1000U) {
        vTaskDelay(1);
    }
    if (session->now_initialized) {
        esp_err_t deinit_err = esp_now_deinit();

        session->now_initialized = false;
        session->broadcast_peer_added = false;
        if (err == ESP_OK) {
            err = deinit_err;
        }
    }
    atomic_store_explicit(&session->active_send, NULL,
                          memory_order_release);
    if (err == ESP_OK) {
        err = espnow_restore_native_session(session);
    }
    state->timed_out = true;
    state->recovery_failed = err != ESP_OK;
    state->err = err == ESP_OK ? ESP_ERR_TIMEOUT : err;
    state->completed_at_us = esp_timer_get_time();
    (void)esp32_mquickjs_wireless_tx_finish_recovery(&session->tx_state,
                                                      err == ESP_OK);
    atomic_fetch_add_explicit(&session->send_timeouts, 1,
                              memory_order_relaxed);
    if (err != ESP_OK) {
        session->lifecycle = ESPNOW_LIFECYCLE_FAILED;
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    return err;
}

static esp32_mquickjs_future_poll_t espnow_send_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && atomic_load_explicit(
                                &state->completed, memory_order_acquire)
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue espnow_send_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    char address[18];
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);

    if (state == NULL || state->cancelled) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_ThrowInternalError(ctx, "ESP-NOW send was cancelled");
    }
    if (state->timed_out) {
        JS_PopGCRef(ctx, &result_ref);
        return espnow_throw_error(
            ctx, state->recovery_failed ? "ESPNOW_RECOVERY_FAILED"
                                        : "ESPNOW_SEND_TIMEOUT",
            state->err, state->address, s_espnow_session.channel);
    }
    espnow_format_address(state->address, address);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "address",
                                         JS_NewString(ctx, address)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "bytes",
            JS_NewInt64(ctx, (int64_t)state->payload_length)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "macDelivered",
            JS_NewBool(state->mac_delivered)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "completedAtUs",
            JS_NewInt64(ctx, state->completed_at_us))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static esp32_mquickjs_cancel_result_t espnow_send_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->started) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancelled = true;
    atomic_store_explicit(&state->completed, true, memory_order_release);
    return ESP32_MQUICKJS_CANCELLED;
}

static void espnow_send_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->generation == s_espnow_session.generation &&
        atomic_load_explicit(&s_espnow_session.active_send,
                             memory_order_acquire) == state) {
        atomic_store_explicit(&s_espnow_session.active_send, NULL,
                              memory_order_release);
    }
    if (state->send_reserved &&
        state->generation == s_espnow_session.generation) {
        uint32_t pending = atomic_load_explicit(
            &s_espnow_session.pending_sends, memory_order_relaxed);

        if (pending > 0) {
            atomic_fetch_sub_explicit(&s_espnow_session.pending_sends, 1,
                                      memory_order_relaxed);
        }
    }
    if (state->owner_rooted) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
    }
    heap_caps_free(state->payload);
    heap_caps_free(state);
}

static uint32_t espnow_send_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL ? state->send_timeout_ms : 0;
}

static JSValue espnow_send_on_timeout(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state,
    uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (state == NULL || !state->started) {
        return espnow_throw_error(
            ctx, "ESPNOW_SEND_TIMEOUT", ESP_ERR_TIMEOUT,
            state != NULL ? state->address : NULL,
            s_espnow_session.channel);
    }
    (void)espnow_recover_after_timeout(&s_espnow_session, state);
    return espnow_throw_error(
        ctx, state->recovery_failed ? "ESPNOW_RECOVERY_FAILED"
                                    : "ESPNOW_SEND_TIMEOUT",
        state->err, state->address, s_espnow_session.channel);
}

static esp32_mquickjs_resource_key_t espnow_send_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    (void)state;
    return &s_espnow_tx_lane_key;
}

static const esp32_mquickjs_future_driver_t s_espnow_send_driver = {
    .capture = espnow_send_capture,
    .start = espnow_send_start,
    .poll = espnow_send_poll,
    .finish = espnow_send_finish,
    .cancel = espnow_send_cancel,
    .destroy = espnow_send_destroy,
    .timeout_ms = espnow_send_timeout_ms,
    .on_timeout = espnow_send_on_timeout,
    .resource_key = espnow_send_resource_key,
};

static bool espnow_register_future_drivers(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime)
{
    static const char *const session_names[] = {
        "addPeer", "broadcast", "setPowerSave", "close",
    };
    static const esp32_mquickjs_future_driver_t *const session_drivers[] = {
        &s_espnow_peer_add_driver,
        &s_espnow_send_driver,
        &s_espnow_power_save_driver,
        &s_espnow_session_close_driver,
    };
    static const char *const peer_names[] = {
        "send", "update", "close",
    };
    static const esp32_mquickjs_future_driver_t *const peer_drivers[] = {
        &s_espnow_send_driver,
        &s_espnow_peer_update_driver,
        &s_espnow_peer_close_driver,
    };
    JSGCRef global_ref;
    JSGCRef module_ref;
    JSGCRef method_ref;
    JSGCRef session_ref;
    JSGCRef peer_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue *session = JS_PushGCRef(ctx, &session_ref);
    JSValue *peer = JS_PushGCRef(ctx, &peer_ref);
    size_t index;
    bool result;

    *global = JS_GetGlobalObject(ctx);
    *module = JS_IsException(*global)
                  ? JS_EXCEPTION
                  : JS_GetPropertyStr(ctx, *global, "espNow");
    *method = JS_IsException(*module)
                  ? JS_EXCEPTION
                  : JS_GetPropertyStr(ctx, *module, "open");
    result = !JS_IsException(*method) &&
             esp32_mquickjs_future_register_driver(
                 ctx, runtime, *method, &s_espnow_open_driver);
    *session = result
                   ? JS_NewObjectClassUser(ctx, JS_CLASS_ESPNOW_SESSION)
                   : JS_EXCEPTION;
    *method = JS_IsException(*session)
                  ? JS_EXCEPTION
                  : JS_GetPropertyStr(ctx, *session, "receive");
    result = result && !JS_IsException(*method) &&
             esp32_mquickjs_event_queue_register_receive_alias(
                 ctx, runtime, *method);
    for (index = 0;
         result && index < sizeof(session_names) / sizeof(session_names[0]);
         ++index) {
        *method = JS_GetPropertyStr(ctx, *session, session_names[index]);
        result = !JS_IsException(*method) &&
                 esp32_mquickjs_future_register_driver(
                     ctx, runtime, *method, session_drivers[index]);
    }
    *peer = result ? JS_NewObjectClassUser(ctx, JS_CLASS_ESPNOW_PEER)
                   : JS_EXCEPTION;
    for (index = 0;
         result && index < sizeof(peer_names) / sizeof(peer_names[0]);
         ++index) {
        *method = JS_GetPropertyStr(ctx, *peer, peer_names[index]);
        result = !JS_IsException(*method) &&
                 esp32_mquickjs_future_register_driver(
                     ctx, runtime, *method, peer_drivers[index]);
    }
    if (!result && !JS_HasException(ctx)) {
        JS_ThrowInternalError(ctx,
                              "failed to register ESP-NOW Future drivers");
    }
    JS_PopGCRef(ctx, &peer_ref);
    JS_PopGCRef(ctx, &session_ref);
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &module_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

bool esp32_mquickjs_init_espnow_runtime(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime)
{
    if (ctx == NULL || runtime == NULL ||
        s_espnow_session.lifecycle != ESPNOW_LIFECYCLE_CLOSED) {
        return false;
    }
    return espnow_register_future_drivers(ctx, runtime);
}

void esp32_mquickjs_deinit_espnow_runtime(JSContext *ctx)
{
    (void)ctx;
    espnow_close_native(&s_espnow_session);
}

JSValue js_espnow_session_constructor(JSContext *ctx, JSValue *this_val,
                                      int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx,
                             "EspNowSession cannot be constructed directly");
}

void js_espnow_session_finalizer(JSContext *ctx, void *opaque)
{
    espnow_session_ref_t *ref = opaque;

    (void)ctx;
    if (ref != NULL && ref->generation == s_espnow_session.generation) {
        espnow_close_native(&s_espnow_session);
    }
    heap_caps_free(ref);
}

JSValue js_espnow_session_receive(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    if (this_val == NULL ||
        espnow_session_from_value(ctx, *this_val, true) == NULL) {
        JS_PopGCRef(ctx, &method_ref);
        return JS_EXCEPTION;
    }
    *method = JS_GetPropertyStr(ctx, *this_val, "receive");
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

JSValue js_espnow_session_stats(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv)
{
    if (this_val == NULL ||
        espnow_session_from_value(ctx, *this_val, true) == NULL) {
        return JS_EXCEPTION;
    }
    return js_event_queue_stats(ctx, this_val, argc, argv);
}

JSValue js_espnow_session_status(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    espnow_session_t *session;

    (void)argc;
    (void)argv;
    if (this_val == NULL ||
        (session = espnow_session_from_value(ctx, *this_val, true)) == NULL) {
        return JS_EXCEPTION;
    }
    return espnow_status_to_js(ctx, session);
}

JSValue js_espnow_session_add_peer(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    if (this_val == NULL) {
        return JS_ThrowTypeError(ctx, "EspNowSession.addPeer() has no receiver");
    }
    return espnow_future_call_and_wait(ctx, *this_val, "addPeer", argc, argv);
}

JSValue js_espnow_session_peer(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv)
{
    espnow_session_t *session;
    espnow_peer_slot_t *peer;
    uint8_t address[ESPNOW_ADDRESS_BYTES];
    uint16_t peer_index;

    if (this_val == NULL || argc != 1 ||
        (session = espnow_session_from_value(ctx, *this_val, true)) == NULL) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx,
                              "EspNowSession.peer(address) expects one address");
        }
        return JS_EXCEPTION;
    }
    if (!espnow_parse_address(ctx, argv[0], "EspNowSession.peer(address)",
                              false, address)) {
        return JS_EXCEPTION;
    }
    peer = espnow_find_peer_by_address(session, address, &peer_index);
    return peer == NULL ? JS_NULL
                        : espnow_new_peer_handle(ctx, peer_index, peer);
}

JSValue js_espnow_session_peers(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv)
{
    espnow_session_t *session;
    JSGCRef array_ref;
    JSGCRef entry_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *entry = JS_PushGCRef(ctx, &entry_ref);
    uint32_t peer_index;
    uint32_t output_index = 0;

    (void)argv;
    *array = JS_UNDEFINED;
    *entry = JS_UNDEFINED;
    if (this_val == NULL || argc != 0 ||
        (session = espnow_session_from_value(ctx, *this_val, true)) == NULL) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx, "EspNowSession.peers() expects no arguments");
        }
        goto fail;
    }
    *array = JS_NewArray(ctx, session->peer_count);
    if (JS_IsException(*array)) {
        goto fail;
    }
    for (peer_index = 0;
         peer_index < CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS;
         ++peer_index) {
        if (!session->peers[peer_index].allocated) {
            continue;
        }
        *entry = espnow_peer_status_to_js(ctx, &session->peers[peer_index]);
        if (JS_IsException(*entry) ||
            JS_IsException(JS_SetPropertyUint32(
                ctx, *array, output_index++, *entry))) {
            goto fail;
        }
    }
    JS_PopGCRef(ctx, &entry_ref);
    return JS_PopGCRef(ctx, &array_ref);

fail:
    JS_PopGCRef(ctx, &entry_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

JSValue js_espnow_session_broadcast(JSContext *ctx, JSValue *this_val,
                                    int argc, JSValue *argv)
{
    if (this_val == NULL) {
        return JS_ThrowTypeError(ctx,
                                 "EspNowSession.broadcast() has no receiver");
    }
    return espnow_future_call_and_wait(ctx, *this_val, "broadcast", argc,
                                       argv);
}

JSValue js_espnow_session_set_power_save(JSContext *ctx, JSValue *this_val,
                                         int argc, JSValue *argv)
{
    if (this_val == NULL) {
        return JS_ThrowTypeError(
            ctx, "EspNowSession.setPowerSave() has no receiver");
    }
    return espnow_future_call_and_wait(ctx, *this_val, "setPowerSave", argc,
                                       argv);
}

JSValue js_espnow_session_close(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv)
{
    if (this_val == NULL) {
        return JS_ThrowTypeError(ctx, "EspNowSession.close() has no receiver");
    }
    return espnow_future_call_and_wait(ctx, *this_val, "close", argc, argv);
}

JSValue js_espnow_peer_constructor(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx,
                             "EspNowPeer cannot be constructed directly");
}

void js_espnow_peer_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

JSValue js_espnow_peer_status(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    espnow_peer_slot_t *peer;

    (void)argv;
    if (this_val == NULL || argc != 0 ||
        (peer = espnow_peer_from_value(ctx, *this_val, NULL, true)) == NULL) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx, "EspNowPeer.status() expects no arguments");
        }
        return JS_EXCEPTION;
    }
    return espnow_peer_status_to_js(ctx, peer);
}

JSValue js_espnow_peer_send(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv)
{
    if (this_val == NULL) {
        return JS_ThrowTypeError(ctx, "EspNowPeer.send() has no receiver");
    }
    return espnow_future_call_and_wait(ctx, *this_val, "send", argc, argv);
}

JSValue js_espnow_peer_update(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    if (this_val == NULL) {
        return JS_ThrowTypeError(ctx, "EspNowPeer.update() has no receiver");
    }
    return espnow_future_call_and_wait(ctx, *this_val, "update", argc, argv);
}

JSValue js_espnow_peer_close(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv)
{
    if (this_val == NULL) {
        return JS_ThrowTypeError(ctx, "EspNowPeer.close() has no receiver");
    }
    return espnow_future_call_and_wait(ctx, *this_val, "close", argc, argv);
}

JSValue js_espnow_capabilities(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);

    (void)this_val;
    (void)argc;
    (void)argv;
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "maxPeers",
            JS_NewUint32(ctx, CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PEERS)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "maxEncryptedPeers",
            JS_NewUint32(ctx, CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "maxV1PayloadBytes",
            JS_NewUint32(ctx, ESP_NOW_MAX_DATA_LEN)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "maxPayloadBytes",
            JS_NewUint32(
                ctx, CONFIG_ESP32_MQUICKJS_ESPNOW_MAX_PAYLOAD_BYTES)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "v2Payloads",
            JS_NewBool(ESPNOW_V2_PAYLOAD_ENABLED)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "stationInterface",
                                         JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "softApInterface",
                                         JS_FALSE) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "powerSave", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "peerRateConfig",
                                         JS_TRUE)) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

JSValue js_espnow_open(JSContext *ctx, JSValue *this_val,
                       int argc, JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    if (this_val == NULL) {
        JS_PopGCRef(ctx, &method_ref);
        return JS_ThrowTypeError(ctx, "espNow.open() has no receiver");
    }
    *method = JS_GetPropertyStr(ctx, *this_val, "open");
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

static JSValue espnow_status_to_js(JSContext *ctx,
                                   const espnow_session_t *session)
{
    JSGCRef result_ref;
    JSGCRef power_save_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *power_save = JS_PushGCRef(ctx, &power_save_ref);
    uint8_t channel = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    uint32_t channel_generation = 0;
    bool synchronized = false;

    *result = JS_NewObject(ctx);
    *power_save = JS_NewObject(ctx);
    if (esp32_mquickjs_wifi_radio_get_channel(
            &channel, &secondary, &channel_generation) == ESP_OK) {
        synchronized = channel == session->channel &&
                       channel_generation == session->channel_generation;
    }
    if (JS_IsException(*result) || JS_IsException(*power_save) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "open", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "interface",
                                         JS_NewString(ctx, "station")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "channel",
                                         JS_NewUint32(ctx, session->channel)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "channelGeneration",
            JS_NewUint32(ctx, session->channel_generation)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "channelSynchronized",
                                         JS_NewBool(synchronized)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "maxPayloadBytes",
            JS_NewUint32(ctx, session->max_payload_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "v1Compatible",
            JS_NewBool(session->max_payload_bytes <= ESP_NOW_MAX_DATA_LEN)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "peerCount",
                                         JS_NewUint32(ctx, session->peer_count)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "encryptedPeerCount",
                                         JS_NewUint32(
                                             ctx,
                                             session->encrypted_peer_count)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "pendingSends",
                                         JS_NewUint32(
                                             ctx,
                                             atomic_load_explicit(
                                                 &session->pending_sends,
                                                 memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "txRecovering",
            JS_NewBool(session->tx_state ==
                       ESP32_MQUICKJS_WIRELESS_TX_RECOVERING)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "receivedPackets",
            JS_NewUint32(ctx, atomic_load_explicit(
                                  &session->received_packets,
                                  memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "receivedBytes",
            JS_NewUint32(ctx, atomic_load_explicit(
                                  &session->received_bytes,
                                  memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "droppedPackets",
            JS_NewUint32(ctx, atomic_load_explicit(
                                  &session->dropped_packets,
                                  memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "malformedPackets",
            JS_NewUint32(ctx, atomic_load_explicit(
                                  &session->malformed_packets,
                                  memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sentPackets",
            JS_NewUint32(ctx, atomic_load_explicit(
                                  &session->sent_packets,
                                  memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sentBytes",
            JS_NewUint32(ctx, atomic_load_explicit(
                                  &session->sent_bytes,
                                  memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sendSuccesses",
            JS_NewUint32(ctx, atomic_load_explicit(
                                  &session->send_successes,
                                  memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sendFailures",
            JS_NewUint32(ctx, atomic_load_explicit(
                                  &session->send_failures,
                                  memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "sendTimeouts",
            JS_NewUint32(ctx, atomic_load_explicit(
                                  &session->send_timeouts,
                                  memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, power_save, "enabled",
            JS_NewBool(session->power_save_enabled)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, power_save, "wakeWindowMs",
            JS_NewUint32(ctx, session->wake_window_ms)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, power_save, "wakeIntervalMs",
            JS_NewUint32(ctx, session->wake_interval_ms)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "powerSave",
                                         *power_save)) {
        JS_PopGCRef(ctx, &power_save_ref);
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    JS_PopGCRef(ctx, &power_save_ref);
    return JS_PopGCRef(ctx, &result_ref);
}

#endif
