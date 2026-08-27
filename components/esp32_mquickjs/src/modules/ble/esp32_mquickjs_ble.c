#include "esp32_mquickjs_ble.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_BLE

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_nvs_flash_boot.h"
#include "esp32_mquickjs_wireless_core.h"
#include "utils/esp32_mquickjs_byte_source.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"

#define BLE_DEFAULT_TIMEOUT_MS 10000U
#define BLE_PAIRING_TIMEOUT_MS 30000U
#define BLE_DEFAULT_MTU 256U
#define BLE_MAX_DEVICE_NAME 64U
#define BLE_INVALID_CONN_HANDLE UINT16_MAX
#define BLE_MAX_SUBSCRIPTIONS CONFIG_ESP32_MQUICKJS_BLE_MAX_CHARACTERISTICS
#define BLE_UUID_TEXT_MAX 37U

typedef enum {
    BLE_LIFECYCLE_CLOSED = 0,
    BLE_LIFECYCLE_OPENING,
    BLE_LIFECYCLE_ACTIVE,
    BLE_LIFECYCLE_CLOSING,
    BLE_LIFECYCLE_FAILED,
} ble_lifecycle_t;

typedef enum {
    BLE_STOP_RUNNING = 0,
    BLE_STOP_COMPLETED,
    BLE_STOP_CONNECTED,
    BLE_STOP_CLOSED,
    BLE_STOP_ERROR,
} ble_stop_reason_t;

typedef enum {
    BLE_OWN_ADDRESS_AUTO = 0,
    BLE_OWN_ADDRESS_PUBLIC,
    BLE_OWN_ADDRESS_RANDOM_STATIC,
    BLE_OWN_ADDRESS_RPA,
} ble_own_address_request_t;

typedef enum {
    BLE_CONTROL_DISCONNECTED = 0,
    BLE_CONTROL_MTU,
    BLE_CONTROL_SECURITY,
    BLE_CONTROL_PAIRING,
} ble_control_event_kind_t;

typedef struct {
    uint32_t generation;
    uint32_t sequence;
    int64_t timestamp_us;
    ble_addr_t peer;
    uint8_t event_type;
    int8_t rssi;
    uint8_t length;
    uint16_t pool_index;
} ble_scan_event_t;

typedef struct {
    uint32_t generation;
    uint32_t sequence;
    int64_t timestamp_us;
    uint16_t connection_index;
    uint32_t connection_generation;
} ble_advertiser_event_t;

typedef struct {
    uint32_t adapter_generation;
    uint32_t connection_generation;
    uint32_t sequence;
    int64_t timestamp_us;
    ble_control_event_kind_t kind;
    int status;
    uint16_t mtu;
    uint32_t request_id;
    uint8_t pairing_action;
    uint32_t passkey;
    int64_t expires_at_us;
} ble_connection_event_t;

typedef struct {
    uint32_t adapter_generation;
    uint32_t connection_generation;
    uint32_t subscription_generation;
    uint32_t sequence;
    int64_t timestamp_us;
    bool indication;
    uint16_t length;
    uint16_t pool_index;
} ble_notification_event_t;

typedef enum {
    BLE_SERVER_EVENT_WRITE = 0,
    BLE_SERVER_EVENT_SUBSCRIPTION,
} ble_server_event_kind_t;

typedef struct {
    uint32_t adapter_generation;
    uint32_t sequence;
    int64_t timestamp_us;
    ble_server_event_kind_t kind;
    uint16_t conn_handle;
    uint16_t characteristic_index;
    uint16_t offset;
    uint16_t length;
    uint16_t pool_index;
    bool notify;
    bool indicate;
} ble_server_event_t;

typedef struct {
    uint32_t generation;
} ble_adapter_ref_t;

typedef struct {
    uint32_t adapter_generation;
    uint32_t generation;
} ble_scanner_ref_t;

typedef struct {
    uint32_t adapter_generation;
    uint32_t generation;
} ble_advertiser_ref_t;

typedef struct {
    uint32_t adapter_generation;
    uint16_t index;
    uint32_t generation;
} ble_connection_ref_t;

typedef struct {
    uint32_t adapter_generation;
    uint16_t connection_index;
    uint32_t connection_generation;
    uint32_t discovery_generation;
    uint16_t index;
} ble_attribute_ref_t;

typedef struct {
    uint32_t adapter_generation;
    uint16_t connection_index;
    uint32_t connection_generation;
    uint16_t subscription_index;
    uint32_t subscription_generation;
} ble_subscription_ref_t;

typedef struct {
    uint32_t adapter_generation;
} ble_server_ref_t;

typedef struct {
    uint32_t adapter_generation;
    uint16_t index;
} ble_local_characteristic_ref_t;

typedef struct {
    ble_uuid_any_t uuid;
    uint16_t start_handle;
    uint16_t end_handle;
    uint16_t first_characteristic;
    uint16_t characteristic_count;
} ble_remote_service_t;

typedef struct {
    ble_uuid_any_t uuid;
    uint16_t declaration_handle;
    uint16_t value_handle;
    uint8_t properties;
    uint16_t first_descriptor;
    uint16_t descriptor_count;
} ble_remote_characteristic_t;

typedef struct {
    ble_uuid_any_t uuid;
    uint16_t handle;
} ble_remote_descriptor_t;

typedef struct {
    bool allocated;
    bool open;
    uint32_t generation;
    uint16_t value_handle;
    uint16_t cccd_handle;
    bool indication;
    esp32_mquickjs_event_queue_t *queue;
    JSGCRef queue_ref;
    bool queue_rooted;
    esp32_mquickjs_wireless_pool_t free_slots;
    uint8_t *payloads;
    uint16_t *lengths;
    uint32_t capacity;
    _Atomic uint32_t sequence;
    _Atomic uint32_t received;
    _Atomic uint32_t dropped;
} ble_subscription_t;

typedef struct {
    bool allocated;
    bool reserved;
    bool open;
    bool release_on_disconnect;
    bool central;
    uint16_t conn_handle;
    uint32_t generation;
    ble_addr_t peer;
    uint16_t mtu;
    int8_t rssi;
    bool rssi_valid;
    struct ble_gap_sec_state security;
    esp32_mquickjs_event_queue_t *queue;
    JSGCRef queue_ref;
    bool queue_rooted;
    const uint8_t gatt_lane_key;
    _Atomic uint32_t sequence;
    _Atomic bool terminal_pending;
    _Atomic uint32_t gatt_pending;
    uint32_t pairing_request_id;
    int64_t pairing_expires_at_us;
    uint8_t pairing_action;
    uint32_t pairing_passkey;
    uint32_t discovery_generation;
    ble_remote_service_t *services;
    ble_remote_characteristic_t *characteristics;
    ble_remote_descriptor_t *descriptors;
    uint16_t service_count;
    uint16_t characteristic_count;
    uint16_t descriptor_count;
    ble_subscription_t subscriptions[BLE_MAX_SUBSCRIPTIONS];
    _Atomic(struct esp32_mquickjs_future_driver_state *) active_gap_state;
    _Atomic(struct esp32_mquickjs_future_driver_state *) active_gatt_state;
} ble_connection_slot_t;

typedef struct {
    bool allocated;
    bool open;
    bool active;
    uint32_t generation;
    esp32_mquickjs_event_queue_t *queue;
    JSGCRef queue_ref;
    bool queue_rooted;
    esp32_mquickjs_wireless_pool_t free_slots;
    uint8_t *payloads;
    uint32_t capacity;
    int64_t started_at_us;
    ble_stop_reason_t stop_reason;
    _Atomic uint32_t sequence;
    _Atomic uint32_t reports;
    _Atomic uint32_t dropped;
    _Atomic uint32_t malformed;
} ble_scanner_t;

typedef struct {
    bool allocated;
    bool open;
    bool active;
    bool connectable;
    bool scannable;
    uint32_t generation;
    esp32_mquickjs_event_queue_t *queue;
    JSGCRef queue_ref;
    bool queue_rooted;
    ble_stop_reason_t stop_reason;
    _Atomic uint32_t sequence;
    _Atomic uint32_t incoming;
    _Atomic uint32_t dropped;
} ble_advertiser_t;

typedef struct {
    char *id;
    ble_uuid_any_t uuid;
    uint16_t value_handle;
    uint16_t max_length;
    uint32_t properties;
    bool store_writes;
    uint8_t *value;
    uint16_t length;
    portMUX_TYPE lock;
} ble_local_characteristic_t;

typedef struct {
    bool configured;
    bool open;
    struct ble_gatt_svc_def *services;
    struct ble_gatt_chr_def **characteristic_defs;
    ble_uuid_any_t *service_uuids;
    ble_local_characteristic_t *characteristics;
    uint16_t service_count;
    uint16_t characteristic_count;
    esp32_mquickjs_event_queue_t *event_queue;
    JSGCRef event_queue_ref;
    bool event_queue_rooted;
    esp32_mquickjs_wireless_pool_t free_slots;
    uint8_t *event_payloads;
    uint32_t event_capacity;
    _Atomic uint32_t sequence;
    _Atomic uint32_t writes;
    _Atomic uint32_t notifications;
    _Atomic uint32_t indications;
    _Atomic uint32_t dropped;
} ble_gatt_server_t;

typedef struct {
    portMUX_TYPE lock;
    ble_lifecycle_t lifecycle;
    JSContext *ctx;
    esp32_mquickjs_runtime_t *runtime;
    uint32_t generation;
    bool synchronized;
    bool host_started;
    ble_own_address_request_t own_address_request;
    uint8_t own_addr_type;
    uint8_t address[6];
    char device_name[BLE_MAX_DEVICE_NAME + 1];
    bool role_central;
    bool role_peripheral;
    uint16_t preferred_mtu;
    uint16_t max_connections;
    uint32_t pairing_timeout_ms;
    bool bonding;
    bool secure_connections;
    bool mitm;
    uint8_t io_capability;
    _Atomic uint32_t reset_count;
    _Atomic uint32_t dropped_connection_events;
    _Atomic uint32_t next_pairing_request;
    ble_scanner_t scanner;
    ble_advertiser_t advertiser;
    ble_connection_slot_t connections[CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS];
    ble_gatt_server_t server;
} ble_adapter_t;

typedef enum {
    BLE_OP_OPEN = 0,
    BLE_OP_SCAN,
    BLE_OP_SCANNER_CLOSE,
    BLE_OP_ADVERTISE,
    BLE_OP_ADVERTISER_CLOSE,
    BLE_OP_CONNECT,
    BLE_OP_CONNECTION_CLOSE,
    BLE_OP_PAIR,
    BLE_OP_EXCHANGE_MTU,
    BLE_OP_READ_RSSI,
    BLE_OP_DISCOVER,
    BLE_OP_GATT_READ,
    BLE_OP_GATT_WRITE,
    BLE_OP_SUBSCRIBE,
    BLE_OP_SUBSCRIPTION_CLOSE,
    BLE_OP_SERVER_NOTIFY,
    BLE_OP_REMOVE_BOND,
    BLE_OP_CLEAR_BONDS,
    BLE_OP_ADAPTER_CLOSE,
} ble_operation_t;

struct esp32_mquickjs_future_driver_state {
    JSContext *ctx;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    ble_operation_t operation;
    uint32_t adapter_generation;
    uint16_t connection_index;
    uint32_t connection_generation;
    uint16_t attribute_index;
    uint16_t subscription_index;
    uint32_t subscription_generation;
    uint32_t timeout_ms;
    JSGCRef owner_ref;
    bool owner_rooted;
    JSGCRef queue_ref;
    bool queue_rooted;
    uint8_t *payload;
    size_t payload_length;
    uint16_t attribute_handle;
    uint16_t cccd_handle;
    uint16_t mtu;
    uint16_t max_bytes;
    uint16_t max_services;
    uint16_t max_characteristics;
    uint16_t max_descriptors;
    uint16_t discover_service_index;
    bool include_descriptors;
    bool response;
    bool indication;
    bool auto_pair;
    bool gatt_accounted;
    bool gatt_started;
    bool started;
    bool cancelled;
    bool transferred;
    bool detached;
    uint16_t callback_refs;
    bool result_bool;
    int result_count;
    uint16_t attempted_connections;
    uint16_t submitted_connections;
    _Atomic uint16_t pending_confirmations;
    int host_code;
    int att_code;
    ble_addr_t peer;
    struct ble_gap_disc_params scan_params;
    int32_t duration_ms;
    struct ble_gap_adv_params adv_params;
    uint8_t adv_data[BLE_HS_ADV_MAX_SZ];
    uint8_t adv_data_length;
    uint8_t scan_response[BLE_HS_ADV_MAX_SZ];
    uint8_t scan_response_length;
    _Atomic bool completed;
};

static ble_adapter_t s_ble = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
    .lifecycle = BLE_LIFECYCLE_CLOSED,
};
static _Atomic uint32_t s_ble_next_generation = 1;
static _Atomic uint32_t s_ble_next_scanner_generation = 1;
static _Atomic uint32_t s_ble_next_advertiser_generation = 1;
static _Atomic uint32_t s_ble_next_connection_generation = 1;
static _Atomic uint32_t s_ble_next_subscription_generation = 1;
static const uint8_t s_ble_gap_lane_key;
static const uint8_t s_ble_adapter_lane_key;
static const uint8_t s_ble_server_lane_key;
static _Atomic(esp32_mquickjs_future_driver_state_t *) s_ble_open_state;
static _Atomic(esp32_mquickjs_future_driver_state_t *) s_ble_server_notify_state;

static int ble_gap_event_callback(struct ble_gap_event *event, void *arg);
static int ble_gatt_server_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static void ble_gatt_server_register(void);
static void ble_publish_server_subscription(const struct ble_gap_event *event);
static void ble_free_pools(ble_adapter_t *adapter);
static bool ble_parse_gatt_server(JSContext *ctx, JSValue definition,
                                  ble_adapter_t *adapter);
static int ble_server_event_pool_acquire(ble_gatt_server_t *server,
                                         uint16_t *out_index);
static void ble_server_event_pool_release(ble_gatt_server_t *server,
                                          uint16_t index);
static void ble_future_state_storage_free(
    esp32_mquickjs_future_driver_state_t *state);
static JSValue ble_bool_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state);
static esp32_mquickjs_resource_key_t ble_gap_resource_key(
    const esp32_mquickjs_future_driver_state_t *state);
static esp32_mquickjs_resource_key_t ble_gatt_resource_key(
    const esp32_mquickjs_future_driver_state_t *state);
static esp32_mquickjs_resource_key_t ble_adapter_resource_key(
    const esp32_mquickjs_future_driver_state_t *state);

static uint32_t ble_next_generation(_Atomic uint32_t *counter)
{
    uint32_t generation = atomic_fetch_add_explicit(
        counter, 1, memory_order_relaxed);
    if (generation == 0) {
        generation = atomic_fetch_add_explicit(counter, 1,
                                               memory_order_relaxed);
    }
    return generation;
}

static bool ble_is_object(JSContext *ctx, JSValue value)
{
    return JS_GetClassID(ctx, value) >= 0 && !JS_IsArray(ctx, value);
}

static bool ble_to_u32(JSContext *ctx, JSValue value, uint32_t *out)
{
    double raw;
    if (out == NULL || JS_ToNumber(ctx, &raw, value) != 0 || raw < 0 ||
        raw > UINT32_MAX || (double)(uint32_t)raw != raw) {
        return false;
    }
    *out = (uint32_t)raw;
    return true;
}

static bool ble_string_equals(JSContext *ctx, JSValue value,
                              const char *expected)
{
    JSCStringBuf buffer;
    const char *text;
    return JS_IsString(ctx, value) &&
           (text = JS_ToCString(ctx, value, &buffer)) != NULL &&
           strcmp(text, expected) == 0;
}

static bool ble_key_allowed(const char *key, const char *const *allowed,
                            size_t allowed_count)
{
    size_t index;
    for (index = 0; index < allowed_count; ++index) {
        if (strcmp(key, allowed[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool ble_validate_option_keys(JSContext *ctx, JSValue options,
                                     const char *api_name,
                                     const char *const *allowed,
                                     size_t allowed_count)
{
    JSGCRef global_ref, object_ref, keys_fn_ref, keys_ref, key_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref);
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *keys_fn = JS_PushGCRef(ctx, &keys_fn_ref);
    JSValue *keys = JS_PushGCRef(ctx, &keys_ref);
    JSValue *key = JS_PushGCRef(ctx, &key_ref);
    JSValue args[1] = {options};
    uint32_t length = 0;
    uint32_t index;
    bool valid = false;

    *global = JS_GetGlobalObject(ctx);
    *object = JS_IsException(*global) ? JS_EXCEPTION
                                     : JS_GetPropertyStr(ctx, *global, "Object");
    *keys_fn = JS_IsException(*object) ? JS_EXCEPTION
                                      : JS_GetPropertyStr(ctx, *object, "keys");
    *keys = JS_IsException(*keys_fn)
                ? JS_EXCEPTION
                : esp32_mquickjs_call(ctx, esp32_mquickjs_get_active_runtime(),
                                      *keys_fn, *object, 1, args);
    *key = JS_IsException(*keys) ? JS_EXCEPTION
                                 : JS_GetPropertyStr(ctx, *keys, "length");
    if (JS_IsException(*key) || !ble_to_u32(ctx, *key, &length)) {
        goto done;
    }
    for (index = 0; index < length; ++index) {
        JSCStringBuf buffer;
        const char *name;
        *key = JS_GetPropertyUint32(ctx, *keys, index);
        name = JS_IsException(*key) ? NULL : JS_ToCString(ctx, *key, &buffer);
        if (name == NULL || !ble_key_allowed(name, allowed, allowed_count)) {
            JS_ThrowTypeError(ctx, "%s received unknown option '%s'", api_name,
                              name != NULL ? name : "<invalid>");
            goto done;
        }
    }
    valid = true;
done:
    JS_PopGCRef(ctx, &key_ref);
    JS_PopGCRef(ctx, &keys_ref);
    JS_PopGCRef(ctx, &keys_fn_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &global_ref);
    return valid;
}

static const char *ble_address_type_name(uint8_t type)
{
    switch (type) {
    case BLE_ADDR_PUBLIC:
        return "public";
    case BLE_ADDR_RANDOM:
        return "random-static";
    case BLE_ADDR_PUBLIC_ID:
        return "random-private-resolvable";
    case BLE_ADDR_RANDOM_ID:
        return "random-private-resolvable";
    default:
        return "random-private-nonresolvable";
    }
}

static void ble_format_address(const uint8_t address[6], char output[18])
{
    uint8_t network_order[6];
    size_t index;
    for (index = 0; index < 6U; ++index)
        network_order[index] = address[5U - index];
    esp32_mquickjs_wireless_format_address(network_order, output);
}

static bool ble_parse_address_text(JSContext *ctx, JSValue value,
                                   uint8_t output[6])
{
    JSCStringBuf buffer;
    const char *text;
    size_t index;
    uint8_t network_order[6];
    if (!JS_IsString(ctx, value) ||
        (text = JS_ToCString(ctx, value, &buffer)) == NULL ||
        !esp32_mquickjs_wireless_parse_address(text, network_order)) {
        JS_ThrowTypeError(ctx, "BLE address must be xx:xx:xx:xx:xx:xx");
        return false;
    }
    for (index = 0; index < 6U; ++index)
        output[5U - index] = network_order[index];
    return true;
}

static bool ble_parse_address(JSContext *ctx, JSValue value, ble_addr_t *out)
{
    static const char *const allowed[] = {"address", "type"};
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool result = false;

    if (out == NULL || !ble_is_object(ctx, value) ||
        !ble_validate_option_keys(ctx, value, "BLEAddress", allowed, 2)) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx, "BLE address must be an object");
        }
        goto done;
    }
    memset(out, 0, sizeof(*out));
    *property = JS_GetPropertyStr(ctx, value, "address");
    if (JS_IsException(*property) ||
        !ble_parse_address_text(ctx, *property, out->val)) {
        goto done;
    }
    *property = JS_GetPropertyStr(ctx, value, "type");
    if (JS_IsException(*property)) goto done;
    if (ble_string_equals(ctx, *property, "public")) out->type = BLE_ADDR_PUBLIC;
    else if (ble_string_equals(ctx, *property, "random-static")) out->type = BLE_ADDR_RANDOM;
    else if (ble_string_equals(ctx, *property, "random-private-resolvable")) out->type = BLE_ADDR_RANDOM_ID;
    else if (ble_string_equals(ctx, *property, "random-private-nonresolvable")) out->type = BLE_ADDR_RANDOM;
    else {
        JS_ThrowTypeError(ctx, "invalid BLE address type");
        goto done;
    }
    result = true;
done:
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static int ble_gatt_server_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    ble_local_characteristic_t *local = arg;
    ble_gatt_server_t *server = &s_ble.server;
    size_t characteristic_index;
    uint16_t length;
    int rc;
    if (ctxt == NULL || local == NULL || !server->open) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    characteristic_index = (size_t)(local - server->characteristics);
    if (characteristic_index >= server->characteristic_count ||
        attr_handle != local->value_handle) return BLE_ATT_ERR_INVALID_HANDLE;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        taskENTER_CRITICAL(&local->lock);
        if (ctxt->offset > local->length) {
            taskEXIT_CRITICAL(&local->lock);
            return BLE_ATT_ERR_INVALID_OFFSET;
        }
        length = local->length - ctxt->offset;
        rc = os_mbuf_append(ctxt->om, local->value + ctxt->offset, length);
        taskEXIT_CRITICAL(&local->lock);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        ble_server_event_t server_event = {0};
        uint16_t pool_index;
        length = OS_MBUF_PKTLEN(ctxt->om);
        if ((uint32_t)ctxt->offset + length > local->max_length)
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (!ble_server_event_pool_acquire(server, &pool_index)) {
            atomic_fetch_add_explicit(&server->dropped, 1,
                                      memory_order_relaxed);
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        rc = os_mbuf_copydata(
            ctxt->om, 0, length,
            server->event_payloads + pool_index *
                                         CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES);
        if (rc != 0) {
            ble_server_event_pool_release(server, pool_index);
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (local->store_writes) {
            taskENTER_CRITICAL(&local->lock);
            (void)esp32_mquickjs_wireless_local_value_write(
                local->value, local->max_length, &local->length, ctxt->offset,
                server->event_payloads + pool_index *
                    CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES,
                length);
            taskEXIT_CRITICAL(&local->lock);
        }
        server_event.adapter_generation = s_ble.generation;
        server_event.sequence = atomic_fetch_add_explicit(
            &server->sequence, 1, memory_order_relaxed) + 1U;
        server_event.timestamp_us = esp_timer_get_time();
        server_event.kind = BLE_SERVER_EVENT_WRITE;
        server_event.conn_handle = conn_handle;
        server_event.characteristic_index = characteristic_index;
        server_event.offset = ctxt->offset;
        server_event.length = length;
        server_event.pool_index = pool_index;
        if (!esp32_mquickjs_event_queue_send(server->event_queue,
                                             &server_event)) {
            ble_server_event_pool_release(server, pool_index);
            atomic_fetch_add_explicit(&server->dropped, 1,
                                      memory_order_relaxed);
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        atomic_fetch_add_explicit(&server->writes, 1, memory_order_relaxed);
        return 0;
    }
    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

static void ble_gatt_server_register(void)
{
    ble_gatt_server_t *server = &s_ble.server;
    esp32_mquickjs_future_driver_state_t *open_state =
        atomic_load_explicit(&s_ble_open_state, memory_order_acquire);
    int rc;
    if (!server->configured) return;
    rc = ble_gatts_count_cfg(server->services);
    if (rc == 0) rc = ble_gatts_add_svcs(server->services);
    server->open = rc == 0;
    if (rc != 0 && open_state != NULL) open_state->host_code = rc;
}

static void ble_publish_server_subscription(const struct ble_gap_event *event)
{
    ble_gatt_server_t *server = &s_ble.server;
    ble_server_event_t server_event = {0};
    uint16_t index;
    if (event == NULL || !server->open || server->event_queue == NULL) return;
    for (index = 0; index < server->characteristic_count; ++index) {
        if (server->characteristics[index].value_handle ==
            event->subscribe.attr_handle) {
            server_event.adapter_generation = s_ble.generation;
            server_event.sequence = atomic_fetch_add_explicit(
                &server->sequence, 1, memory_order_relaxed) + 1U;
            server_event.timestamp_us = esp_timer_get_time();
            server_event.kind = BLE_SERVER_EVENT_SUBSCRIPTION;
            server_event.conn_handle = event->subscribe.conn_handle;
            server_event.characteristic_index = index;
            server_event.notify = event->subscribe.cur_notify;
            server_event.indicate = event->subscribe.cur_indicate;
            if (!esp32_mquickjs_event_queue_send(server->event_queue,
                                                 &server_event))
                atomic_fetch_add_explicit(&server->dropped, 1,
                                          memory_order_relaxed);
            return;
        }
    }
}

static JSValue ble_address_to_js(JSContext *ctx, const ble_addr_t *address)
{
    char text[18];
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    ble_format_address(address->val, text);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "address",
                                         JS_NewString(ctx, text)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "type",
            JS_NewString(ctx, ble_address_type_name(address->type)))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

static JSValue ble_throw_error(JSContext *ctx, const char *code, int host_code,
                               int att_code, int connection_id,
                               int attribute_handle)
{
    JSGCRef error_ref;
    JSValue *error;
    (void)JS_ThrowInternalError(ctx, "%s: Bluetooth LE operation failed", code);
    error = JS_PushGCRef(ctx, &error_ref);
    *error = JS_GetException(ctx);
    if (JS_GetClassID(ctx, *error) >= 0 &&
        (JS_IsException(JS_SetPropertyStr(ctx, *error, "code",
                                         JS_NewString(ctx, code))) ||
         JS_IsException(JS_SetPropertyStr(ctx, *error, "hostCode",
                                         JS_NewInt32(ctx, host_code))) ||
         JS_IsException(JS_SetPropertyStr(ctx, *error, "attCode",
             att_code >= 0 ? JS_NewInt32(ctx, att_code) : JS_NULL)) ||
         JS_IsException(JS_SetPropertyStr(ctx, *error, "connectionId",
             connection_id >= 0 ? JS_NewInt32(ctx, connection_id) : JS_NULL)) ||
         JS_IsException(JS_SetPropertyStr(ctx, *error, "attributeHandle",
             attribute_handle >= 0 ? JS_NewInt32(ctx, attribute_handle)
                                   : JS_NULL)))) {
        JS_PopGCRef(ctx, &error_ref);
        return JS_EXCEPTION;
    }
    return JS_Throw(ctx, JS_PopGCRef(ctx, &error_ref));
}

static JSValue ble_future_call_and_wait(JSContext *ctx, JSValue receiver,
                                        const char *method_name, int argc,
                                        JSValue *argv)
{
    JSGCRef receiver_ref, method_ref;
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

static void ble_retain_owner(JSContext *ctx, JSValue owner,
                             esp32_mquickjs_future_driver_state_t *state)
{
    *JS_AddGCRef(ctx, &state->owner_ref) = owner;
    state->owner_rooted = true;
}

static uint16_t ble_ms_to_625us(double value)
{
    double units = value / 0.625;
    if (units < 1.0) units = 1.0;
    if (units > 65535.0) units = 65535.0;
    return (uint16_t)(units + 0.5);
}

static bool ble_get_number(JSContext *ctx, JSValue object, const char *name,
                           double *value, bool *present)
{
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool result = false;
    *property = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(*property)) goto done;
    *present = !JS_IsUndefined(*property);
    if (!*present || JS_ToNumber(ctx, value, *property) == 0) result = true;
done:
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static bool ble_get_bool(JSContext *ctx, JSValue object, const char *name,
                         bool default_value, bool *out)
{
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    int raw;
    bool result = false;
    *out = default_value;
    *property = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!JS_IsBool(*property)) {
            JS_ThrowTypeError(ctx, "%s must be a boolean", name);
            goto done;
        }
        raw = JS_VALUE_GET_SPECIAL_VALUE(*property);
        *out = raw != 0;
    }
    result = true;
done:
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static ble_adapter_t *ble_adapter_from_value(JSContext *ctx, JSValue value,
                                             bool throw_if_stale)
{
    ble_adapter_ref_t *ref;
    if (JS_GetClassID(ctx, value) != JS_CLASS_BLE_ADAPTER ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "expected a BLEAdapter instance");
        return NULL;
    }
    if (s_ble.lifecycle != BLE_LIFECYCLE_ACTIVE ||
        ref->generation != s_ble.generation) {
        if (throw_if_stale) {
            JS_ThrowReferenceError(ctx,
                "BLE_STALE_ADAPTER: BLE adapter is closed");
        }
        return NULL;
    }
    return &s_ble;
}

static ble_connection_slot_t *ble_connection_from_value(
    JSContext *ctx, JSValue value, ble_connection_ref_t **out_ref,
    bool throw_if_stale)
{
    ble_connection_ref_t *ref;
    ble_connection_slot_t *slot = NULL;
    if (JS_GetClassID(ctx, value) != JS_CLASS_BLE_CONNECTION ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "expected a BLEConnection instance");
        return NULL;
    }
    if (s_ble.lifecycle == BLE_LIFECYCLE_ACTIVE &&
        ref->adapter_generation == s_ble.generation &&
        ref->index < s_ble.max_connections) {
        slot = &s_ble.connections[ref->index];
        if (!slot->allocated || slot->generation != ref->generation) slot = NULL;
    }
    if (slot == NULL && throw_if_stale) {
        JS_ThrowReferenceError(ctx,
            "BLE_STALE_CONNECTION: BLE connection is closed");
    }
    if (out_ref != NULL) *out_ref = ref;
    return slot;
}

static ble_connection_slot_t *ble_find_connection(uint16_t conn_handle,
                                                   uint16_t *out_index)
{
    uint16_t index;
    for (index = 0; index < s_ble.max_connections; ++index) {
        ble_connection_slot_t *slot = &s_ble.connections[index];
        if (slot->allocated && slot->conn_handle == conn_handle) {
            if (out_index != NULL) *out_index = index;
            return slot;
        }
    }
    return NULL;
}

static ble_connection_slot_t *ble_reserve_connection(bool central,
                                                       uint16_t *out_index)
{
    uint16_t index;
    for (index = 0; index < s_ble.max_connections; ++index) {
        ble_connection_slot_t *slot = &s_ble.connections[index];
        if (!slot->allocated && !slot->reserved) {
            if (slot->queue != NULL)
                (void)esp32_mquickjs_event_queue_discard_all(slot->queue);
            slot->reserved = true;
            slot->release_on_disconnect = false;
            slot->central = central;
            slot->generation = ble_next_generation(
                &s_ble_next_connection_generation);
            slot->conn_handle = BLE_INVALID_CONN_HANDLE;
            slot->mtu = 23;
            slot->rssi = 0;
            slot->rssi_valid = false;
            memset(&slot->peer, 0, sizeof(slot->peer));
            memset(&slot->security, 0, sizeof(slot->security));
            slot->pairing_request_id = 0;
            slot->pairing_expires_at_us = 0;
            slot->pairing_action = 0;
            slot->pairing_passkey = 0;
            slot->discovery_generation = 1;
            atomic_store_explicit(&slot->sequence, 0, memory_order_relaxed);
            atomic_store_explicit(&slot->terminal_pending, false,
                                  memory_order_relaxed);
            atomic_store_explicit(&slot->gatt_pending, 0,
                                  memory_order_relaxed);
            if (out_index != NULL) *out_index = index;
            return slot;
        }
    }
    return NULL;
}

static void ble_close_subscription(ble_subscription_t *subscription)
{
    if (subscription == NULL || !subscription->allocated) return;
    subscription->open = false;
    if (subscription->queue != NULL) {
        (void)esp32_mquickjs_event_queue_close(subscription->queue);
        (void)esp32_mquickjs_event_queue_discard_all(subscription->queue);
    }
    if (subscription->queue_rooted) {
        JS_DeleteGCRef(s_ble.ctx, &subscription->queue_ref);
    }
    heap_caps_free(subscription->payloads);
    heap_caps_free(subscription->lengths);
    memset(subscription, 0, sizeof(*subscription));
}

static void ble_clear_remote_discovery(ble_connection_slot_t *slot)
{
    if (slot == NULL) return;
    heap_caps_free(slot->services);
    heap_caps_free(slot->characteristics);
    heap_caps_free(slot->descriptors);
    slot->services = NULL;
    slot->characteristics = NULL;
    slot->descriptors = NULL;
    slot->service_count = 0;
    slot->characteristic_count = 0;
    slot->descriptor_count = 0;
    slot->discovery_generation++;
    if (slot->discovery_generation == 0) slot->discovery_generation = 1;
}

static void ble_release_connection(ble_connection_slot_t *slot)
{
    uint16_t index;
    if (slot == NULL) return;
    slot->open = false;
    slot->reserved = false;
    if (slot->queue != NULL) {
        (void)esp32_mquickjs_event_queue_close(slot->queue);
        (void)esp32_mquickjs_event_queue_discard_all(slot->queue);
    }
    for (index = 0; index < BLE_MAX_SUBSCRIPTIONS; ++index) {
        ble_close_subscription(&slot->subscriptions[index]);
    }
    ble_clear_remote_discovery(slot);
    slot->allocated = false;
    slot->conn_handle = BLE_INVALID_CONN_HANDLE;
}

static void ble_recycle_connection(ble_connection_slot_t *slot)
{
    uint16_t index;
    if (slot == NULL) return;
    slot->open = false;
    slot->reserved = false;
    if (slot->queue != NULL)
        (void)esp32_mquickjs_event_queue_discard_all(slot->queue);
    for (index = 0; index < BLE_MAX_SUBSCRIPTIONS; ++index)
        ble_close_subscription(&slot->subscriptions[index]);
    ble_clear_remote_discovery(slot);
    atomic_store_explicit(&slot->active_gap_state, NULL,
                          memory_order_release);
    atomic_store_explicit(&slot->active_gatt_state, NULL,
                          memory_order_release);
    slot->allocated = false;
    slot->conn_handle = BLE_INVALID_CONN_HANDLE;
}

static JSValue ble_new_connection_handle(JSContext *ctx, uint16_t index,
                                         ble_connection_slot_t *slot)
{
    ble_connection_ref_t *ref;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BLE_CONNECTION);
    if (JS_IsException(*object)) goto fail;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ref->adapter_generation = s_ble.generation;
    ref->index = index;
    ref->generation = slot->generation;
    JS_SetOpaque(ctx, *object, ref);
    if (!esp32_mquickjs_set_property_ref(ctx, object, "_eventQueue",
                                         slot->queue_ref.val)) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
        goto fail;
    }
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static int ble_scan_pool_acquire(ble_scanner_t *scanner, uint16_t *slot)
{
    return scanner != NULL &&
           esp32_mquickjs_wireless_pool_acquire(&scanner->free_slots, slot);
}

static void ble_scan_pool_release(ble_scanner_t *scanner, uint16_t slot)
{
    if (scanner != NULL && slot < scanner->capacity)
        (void)esp32_mquickjs_wireless_pool_release(&scanner->free_slots, slot);
}

static void ble_scan_event_drop(void *event, void *opaque)
{
    ble_scan_event_t *scan_event = event;
    ble_scanner_t *scanner = opaque;
    if (scan_event != NULL && scanner != NULL &&
        scan_event->generation == scanner->generation) {
        ble_scan_pool_release(scanner, scan_event->pool_index);
    }
}

static JSValue ble_scan_event_to_js(JSContext *ctx, const void *event,
                                    void *opaque)
{
    const ble_scan_event_t *scan_event = event;
    ble_scanner_t *scanner = opaque;
    uint8_t *payload = NULL;
    JSGCRef object_ref, peer_ref, data_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *peer = JS_PushGCRef(ctx, &peer_ref);
    JSValue *data = JS_PushGCRef(ctx, &data_ref);
    *object = JS_UNDEFINED;
    *peer = JS_UNDEFINED;
    *data = JS_UNDEFINED;
    if (scan_event == NULL || scanner == NULL ||
        scan_event->generation != scanner->generation ||
        scan_event->pool_index >= scanner->capacity) {
        JS_ThrowReferenceError(ctx, "BLE_STALE_ADAPTER: stale scan report");
        goto fail;
    }
    if (scan_event->length > 0) {
        payload = heap_caps_malloc(scan_event->length, MALLOC_CAP_8BIT);
        if (payload == NULL) {
            ble_scan_pool_release(scanner, scan_event->pool_index);
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
        memcpy(payload,
               scanner->payloads + scan_event->pool_index * BLE_HS_ADV_MAX_SZ,
               scan_event->length);
    }
    ble_scan_pool_release(scanner, scan_event->pool_index);
    *data = esp32_mquickjs_new_owned_byte_view(ctx, payload,
                                               scan_event->length);
    payload = NULL;
    *peer = ble_address_to_js(ctx, &scan_event->peer);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*data) || JS_IsException(*peer) ||
        JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "type",
                                         JS_NewString(ctx, "report")) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "sequence",
                                         JS_NewUint32(ctx, scan_event->sequence)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "timestampUs",
                                         JS_NewInt64(ctx, scan_event->timestamp_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "peer", *peer) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "eventType",
            JS_NewString(ctx,
                scan_event->event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP
                    ? "scan-response"
                    : scan_event->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND
                          ? "directed-advertisement"
                          : "advertisement")) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "rssi",
                                         JS_NewInt32(ctx, scan_event->rssi)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "connectable",
            JS_NewBool(scan_event->event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
                       scan_event->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "scannable",
            JS_NewBool(scan_event->event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
                       scan_event->event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "directed",
            JS_NewBool(scan_event->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "data", *data)) {
        goto fail;
    }
    JS_PopGCRef(ctx, &data_ref);
    JS_PopGCRef(ctx, &peer_ref);
    return JS_PopGCRef(ctx, &object_ref);
fail:
    heap_caps_free(payload);
    JS_PopGCRef(ctx, &data_ref);
    JS_PopGCRef(ctx, &peer_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static JSValue ble_advertiser_event_to_js(JSContext *ctx, const void *event,
                                          void *opaque)
{
    const ble_advertiser_event_t *incoming = event;
    ble_advertiser_t *advertiser = opaque;
    ble_connection_slot_t *slot;
    JSGCRef object_ref, connection_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *connection = JS_PushGCRef(ctx, &connection_ref);
    *object = JS_UNDEFINED;
    *connection = JS_UNDEFINED;
    if (incoming == NULL || advertiser == NULL ||
        incoming->generation != advertiser->generation ||
        incoming->connection_index >= s_ble.max_connections) {
        JS_ThrowReferenceError(ctx, "BLE_STALE_CONNECTION: incoming connection is stale");
        goto fail;
    }
    slot = &s_ble.connections[incoming->connection_index];
    if (!slot->open || slot->generation != incoming->connection_generation) {
        JS_ThrowReferenceError(ctx, "BLE_STALE_CONNECTION: incoming connection is closed");
        goto fail;
    }
    *connection = ble_new_connection_handle(ctx, incoming->connection_index, slot);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*connection) || JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "type",
                                         JS_NewString(ctx, "connection")) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "sequence",
                                         JS_NewUint32(ctx, incoming->sequence)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "timestampUs",
                                         JS_NewInt64(ctx, incoming->timestamp_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "connection", *connection)) {
        goto fail;
    }
    JS_PopGCRef(ctx, &connection_ref);
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &connection_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static JSValue ble_security_to_js(JSContext *ctx,
                                  const struct ble_gap_sec_state *security,
                                  const ble_addr_t *peer)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    bool secure_connections = false;
    if (security->bonded && peer != NULL) {
        struct ble_store_key_sec key = {.peer_addr = *peer};
        struct ble_store_value_sec value = {0};
        if (ble_store_read_peer_sec(&key, &value) == 0)
            secure_connections = value.sc != 0;
    }
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "encrypted",
                                         JS_NewBool(security->encrypted)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "authenticated",
                                         JS_NewBool(security->authenticated)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "bonded",
                                         JS_NewBool(security->bonded)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "secureConnections",
                                         JS_NewBool(secure_connections))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

static JSValue ble_connection_event_to_js(JSContext *ctx, const void *event,
                                          void *opaque)
{
    const ble_connection_event_t *control = event;
    ble_connection_slot_t *slot = opaque;
    JSGCRef object_ref, status_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *status = JS_PushGCRef(ctx, &status_ref);
    const char *type;
    *object = JS_UNDEFINED;
    *status = JS_UNDEFINED;
    if (control == NULL || slot == NULL ||
        control->adapter_generation != s_ble.generation ||
        control->connection_generation != slot->generation) {
        JS_ThrowReferenceError(ctx, "BLE_STALE_CONNECTION: stale connection event");
        goto fail;
    }
    type = control->kind == BLE_CONTROL_DISCONNECTED ? "disconnected" :
           control->kind == BLE_CONTROL_MTU ? "mtu" :
           control->kind == BLE_CONTROL_SECURITY ? "security" :
                                                  "pairing-request";
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "type",
                                         JS_NewString(ctx, type)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "sequence",
                                         JS_NewUint32(ctx, control->sequence)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "timestampUs",
                                         JS_NewInt64(ctx, control->timestamp_us))) {
        goto fail;
    }
    if (control->kind == BLE_CONTROL_DISCONNECTED) {
        if (!esp32_mquickjs_set_property_ref(ctx, object, "reasonCode",
                                             JS_NewInt32(ctx, control->status)) ||
            !esp32_mquickjs_set_property_ref(ctx, object, "reasonName",
                                             JS_NewString(ctx, "host"))) goto fail;
        atomic_store_explicit(&slot->terminal_pending, false, memory_order_release);
    } else if (control->kind == BLE_CONTROL_MTU) {
        if (!esp32_mquickjs_set_property_ref(ctx, object, "mtu",
                                             JS_NewUint32(ctx, control->mtu))) goto fail;
    } else if (control->kind == BLE_CONTROL_SECURITY) {
        *status = ble_security_to_js(ctx, &slot->security, &slot->peer);
        if (JS_IsException(*status) ||
            !esp32_mquickjs_set_property_ref(ctx, object, "status", *status)) goto fail;
    } else {
        const char *action = control->pairing_action == BLE_SM_IOACT_DISP
                                 ? "display-passkey"
                                 : control->pairing_action == BLE_SM_IOACT_INPUT
                                       ? "input-passkey"
                                       : "numeric-comparison";
        if (!esp32_mquickjs_set_property_ref(ctx, object, "requestId",
                                             JS_NewUint32(ctx, control->request_id)) ||
            !esp32_mquickjs_set_property_ref(ctx, object, "action",
                                             JS_NewString(ctx, action)) ||
            !esp32_mquickjs_set_property_ref(ctx, object, "expiresAtUs",
                                             JS_NewInt64(ctx, control->expires_at_us)) ||
            ((control->pairing_action == BLE_SM_IOACT_DISP ||
              control->pairing_action == BLE_SM_IOACT_NUMCMP) &&
             !esp32_mquickjs_set_property_ref(ctx, object, "passkey",
                                              JS_NewUint32(ctx, control->passkey)))) goto fail;
    }
    JS_PopGCRef(ctx, &status_ref);
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &status_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static void ble_notification_pool_release(ble_subscription_t *subscription,
                                          uint16_t index)
{
    if (subscription != NULL && index < subscription->capacity)
        (void)esp32_mquickjs_wireless_pool_release(
            &subscription->free_slots, index);
}

static void ble_notification_event_drop(void *event, void *opaque)
{
    ble_notification_event_t *value_event = event;
    ble_subscription_t *subscription = opaque;
    if (value_event != NULL && subscription != NULL &&
        value_event->subscription_generation == subscription->generation) {
        ble_notification_pool_release(subscription, value_event->pool_index);
    }
}

static JSValue ble_notification_event_to_js(JSContext *ctx, const void *event,
                                            void *opaque)
{
    const ble_notification_event_t *value_event = event;
    ble_subscription_t *subscription = opaque;
    uint8_t *payload = NULL;
    JSGCRef object_ref, data_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *data = JS_PushGCRef(ctx, &data_ref);
    *object = JS_UNDEFINED;
    *data = JS_UNDEFINED;
    if (value_event == NULL || subscription == NULL ||
        value_event->subscription_generation != subscription->generation ||
        value_event->pool_index >= subscription->capacity) {
        JS_ThrowReferenceError(ctx, "BLE_STALE_SUBSCRIPTION: stale value event");
        goto fail;
    }
    if (value_event->length > 0) {
        payload = heap_caps_malloc(value_event->length, MALLOC_CAP_8BIT);
        if (payload == NULL) {
            ble_notification_pool_release(subscription, value_event->pool_index);
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
        memcpy(payload,
               subscription->payloads + value_event->pool_index *
                                            CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES,
               value_event->length);
    }
    ble_notification_pool_release(subscription, value_event->pool_index);
    *data = esp32_mquickjs_new_owned_byte_view(ctx, payload, value_event->length);
    payload = NULL;
    *object = JS_NewObject(ctx);
    if (JS_IsException(*data) || JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "type",
                                         JS_NewString(ctx, "value")) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "sequence",
                                         JS_NewUint32(ctx, value_event->sequence)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "timestampUs",
                                         JS_NewInt64(ctx, value_event->timestamp_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "indication",
                                         JS_NewBool(value_event->indication)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "data", *data)) goto fail;
    JS_PopGCRef(ctx, &data_ref);
    return JS_PopGCRef(ctx, &object_ref);
fail:
    heap_caps_free(payload);
    JS_PopGCRef(ctx, &data_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

/*
 * NimBLE callbacks run on the host task, independently of the JS runtime.
 * Callback arguments therefore point only at boot-lived connection slots;
 * Future state is published through a protected slot and is detached before
 * the runtime can free it.  A callback reference keeps a detached state alive
 * until that callback has returned, without touching JS from the host task.
 */
static void ble_active_state_bind(
    _Atomic(esp32_mquickjs_future_driver_state_t *) *location,
    esp32_mquickjs_future_driver_state_t *state)
{
    taskENTER_CRITICAL(&s_ble.lock);
    atomic_store_explicit(location, state, memory_order_release);
    taskEXIT_CRITICAL(&s_ble.lock);
}

static void ble_gatt_state_bind(
    ble_connection_slot_t *slot,
    esp32_mquickjs_future_driver_state_t *state)
{
    if (slot == NULL || state == NULL) return;
    if (state->gatt_accounted && !state->gatt_started) {
        (void)atomic_fetch_sub_explicit(&slot->gatt_pending, 1,
                                        memory_order_acq_rel);
        state->gatt_started = true;
    }
    ble_active_state_bind(&slot->active_gatt_state, state);
}

static esp32_mquickjs_future_driver_state_t *ble_active_state_acquire(
    _Atomic(esp32_mquickjs_future_driver_state_t *) *location)
{
    esp32_mquickjs_future_driver_state_t *state;
    taskENTER_CRITICAL(&s_ble.lock);
    state = atomic_load_explicit(location, memory_order_acquire);
    if (state != NULL && !state->detached) state->callback_refs++;
    else state = NULL;
    taskEXIT_CRITICAL(&s_ble.lock);
    return state;
}

static void ble_future_state_hold(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) return;
    taskENTER_CRITICAL(&s_ble.lock);
    state->callback_refs++;
    taskEXIT_CRITICAL(&s_ble.lock);
}

static void ble_future_state_drop(
    esp32_mquickjs_future_driver_state_t *state)
{
    bool release = false;
    if (state == NULL) return;
    taskENTER_CRITICAL(&s_ble.lock);
    if (state->callback_refs > 0) state->callback_refs--;
    if (state->callback_refs == 0 && state->detached) release = true;
    taskEXIT_CRITICAL(&s_ble.lock);
    if (release) ble_future_state_storage_free(state);
}

static int ble_gap_event_callback(struct ble_gap_event *event, void *arg)
{
    ble_connection_slot_t *callback_slot = arg;
    esp32_mquickjs_future_driver_state_t *state = NULL;
    ble_connection_slot_t *slot;
    uint16_t connection_index;
    int task_woken = 0;

    if (event == NULL || (s_ble.lifecycle != BLE_LIFECYCLE_ACTIVE &&
                          s_ble.lifecycle != BLE_LIFECYCLE_OPENING &&
                          s_ble.lifecycle != BLE_LIFECYCLE_CLOSING)) return 0;
    if (callback_slot != NULL)
        state = ble_active_state_acquire(&callback_slot->active_gap_state);
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        if (s_ble.scanner.active && s_ble.scanner.queue != NULL) {
            ble_scan_event_t scan_event = {0};
            uint16_t pool_index;
            scan_event.generation = s_ble.scanner.generation;
            scan_event.sequence = atomic_fetch_add_explicit(
                &s_ble.scanner.sequence, 1, memory_order_relaxed) + 1U;
            if (event->disc.length_data > BLE_HS_ADV_MAX_SZ) {
                atomic_fetch_add_explicit(&s_ble.scanner.malformed, 1,
                                          memory_order_relaxed);
            } else if (!ble_scan_pool_acquire(&s_ble.scanner, &pool_index)) {
                atomic_fetch_add_explicit(&s_ble.scanner.dropped, 1,
                                          memory_order_relaxed);
            } else {
                scan_event.timestamp_us = esp_timer_get_time();
                scan_event.peer = event->disc.addr;
                scan_event.event_type = event->disc.event_type;
                scan_event.rssi = event->disc.rssi;
                scan_event.length = event->disc.length_data;
                scan_event.pool_index = pool_index;
                memcpy(s_ble.scanner.payloads + pool_index * BLE_HS_ADV_MAX_SZ,
                       event->disc.data, event->disc.length_data);
                if (!esp32_mquickjs_event_queue_send_from_isr(
                        s_ble.scanner.queue, &scan_event, &task_woken)) {
                    ble_scan_pool_release(&s_ble.scanner, pool_index);
                    atomic_fetch_add_explicit(&s_ble.scanner.dropped, 1,
                                              memory_order_relaxed);
                } else {
                    atomic_fetch_add_explicit(&s_ble.scanner.reports, 1,
                                              memory_order_relaxed);
                }
            }
        }
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        s_ble.scanner.active = false;
        s_ble.scanner.stop_reason =
            event->disc_complete.reason == 0 ||
            event->disc_complete.reason == BLE_HS_ETIMEOUT
                ? BLE_STOP_COMPLETED : BLE_STOP_ERROR;
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_ble.advertiser.active = false;
        if (s_ble.advertiser.stop_reason == BLE_STOP_RUNNING) {
            s_ble.advertiser.stop_reason =
                event->adv_complete.reason == 0 ? BLE_STOP_CONNECTED :
                event->adv_complete.reason == BLE_HS_ETIMEOUT
                    ? BLE_STOP_COMPLETED : BLE_STOP_ERROR;
        }
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (callback_slot != NULL) {
            slot = callback_slot;
            connection_index = (uint16_t)(slot - s_ble.connections);
        } else {
            slot = ble_reserve_connection(false, &connection_index);
            if (slot != NULL) state = NULL;
        }
        if (callback_slot != NULL && state == NULL) {
            if (event->connect.status == 0)
                (void)ble_gap_terminate(event->connect.conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
            slot->reserved = false;
            slot->allocated = false;
            break;
        }
        if (slot == NULL) {
            if (event->connect.status == 0)
                (void)ble_gap_terminate(event->connect.conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        if (event->connect.status == 0) {
            struct ble_gap_conn_desc descriptor;
            int pairing_rc = 0;
            slot->allocated = true;
            slot->reserved = false;
            slot->open = true;
            slot->conn_handle = event->connect.conn_handle;
            if (ble_gap_conn_find(slot->conn_handle, &descriptor) == 0) {
                slot->peer = descriptor.peer_id_addr;
                slot->central = descriptor.role == BLE_GAP_ROLE_MASTER;
                slot->security = descriptor.sec_state;
            }
            if (state == NULL && s_ble.advertiser.queue != NULL) {
                ble_advertiser_event_t incoming = {
                    .generation = s_ble.advertiser.generation,
                    .sequence = atomic_fetch_add_explicit(
                        &s_ble.advertiser.sequence, 1, memory_order_relaxed) + 1U,
                    .timestamp_us = esp_timer_get_time(),
                    .connection_index = connection_index,
                    .connection_generation = slot->generation,
                };
                s_ble.advertiser.active = false;
                s_ble.advertiser.stop_reason = BLE_STOP_CONNECTED;
                atomic_fetch_add_explicit(&s_ble.advertiser.incoming, 1,
                                          memory_order_relaxed);
                if (!esp32_mquickjs_event_queue_send_from_isr(
                        s_ble.advertiser.queue, &incoming, &task_woken)) {
                    atomic_fetch_add_explicit(&s_ble.advertiser.dropped, 1,
                                              memory_order_relaxed);
                    slot->release_on_disconnect = true;
                    (void)ble_gap_terminate(slot->conn_handle,
                                            BLE_ERR_REM_USER_CONN_TERM);
                }
            }
            if (state != NULL && state->operation == BLE_OP_CONNECT &&
                state->auto_pair) {
                pairing_rc = ble_gap_security_initiate(slot->conn_handle);
                if (pairing_rc != 0) state->host_code = pairing_rc;
            }
        } else {
            slot->reserved = false;
            slot->allocated = false;
        }
        if (state != NULL && state->operation == BLE_OP_CONNECT) {
            if (event->connect.status != 0) state->host_code = event->connect.status;
            if (event->connect.status != 0 || !state->auto_pair ||
                state->host_code != 0) {
                atomic_store_explicit(&state->completed, true,
                                      memory_order_release);
                (void)esp32_mquickjs_future_wake_from_isr(
                    state->runtime, state->token, &task_woken);
            }
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        slot = ble_find_connection(event->disconnect.conn.conn_handle,
                                   &connection_index);
        if (slot != NULL) {
            esp32_mquickjs_future_driver_state_t *gatt_state =
                ble_active_state_acquire(&slot->active_gatt_state);
            slot->open = false;
            if (slot->release_on_disconnect) {
                slot->release_on_disconnect = false;
                slot->allocated = false;
                slot->reserved = false;
                slot->conn_handle = BLE_INVALID_CONN_HANDLE;
            } else {
                ble_connection_event_t control = {
                    .adapter_generation = s_ble.generation,
                    .connection_generation = slot->generation,
                    .sequence = atomic_fetch_add_explicit(
                        &slot->sequence, 1, memory_order_relaxed) + 1U,
                    .timestamp_us = esp_timer_get_time(),
                    .kind = BLE_CONTROL_DISCONNECTED,
                    .status = event->disconnect.reason,
                };
                atomic_store_explicit(&slot->terminal_pending, true,
                                      memory_order_release);
                if (!esp32_mquickjs_event_queue_send_from_isr(
                        slot->queue, &control, &task_woken)) {
                    atomic_fetch_add_explicit(
                        &s_ble.dropped_connection_events, 1,
                        memory_order_relaxed);
                }
            }
            if (state != NULL) {
                state->host_code =
                    state->operation == BLE_OP_CONNECTION_CLOSE
                        ? 0 : BLE_HS_ENOTCONN;
                atomic_store_explicit(&state->completed, true,
                                      memory_order_release);
                (void)esp32_mquickjs_future_wake_from_isr(
                    state->runtime, state->token, &task_woken);
            }
            if (gatt_state != NULL && gatt_state != state) {
                gatt_state->host_code = BLE_HS_ENOTCONN;
                atomic_store_explicit(&gatt_state->completed, true,
                                      memory_order_release);
                (void)esp32_mquickjs_future_wake_from_isr(
                    gatt_state->runtime, gatt_state->token, &task_woken);
            }
            ble_future_state_drop(gatt_state);
        }
        break;
    case BLE_GAP_EVENT_MTU:
        slot = ble_find_connection(event->mtu.conn_handle, NULL);
        if (slot != NULL) {
            ble_connection_event_t control = {
                .adapter_generation = s_ble.generation,
                .connection_generation = slot->generation,
                .sequence = atomic_fetch_add_explicit(&slot->sequence, 1,
                                                       memory_order_relaxed) + 1U,
                .timestamp_us = esp_timer_get_time(),
                .kind = BLE_CONTROL_MTU,
                .mtu = event->mtu.value,
            };
            slot->mtu = event->mtu.value;
            if (!esp32_mquickjs_event_queue_send_from_isr(
                    slot->queue, &control, &task_woken)) {
                atomic_fetch_add_explicit(&s_ble.dropped_connection_events, 1,
                                          memory_order_relaxed);
            }
        }
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        slot = ble_find_connection(event->enc_change.conn_handle, NULL);
        if (slot != NULL) {
            struct ble_gap_conn_desc descriptor;
            ble_connection_event_t control = {
                .adapter_generation = s_ble.generation,
                .connection_generation = slot->generation,
                .sequence = atomic_fetch_add_explicit(&slot->sequence, 1,
                                                       memory_order_relaxed) + 1U,
                .timestamp_us = esp_timer_get_time(),
                .kind = BLE_CONTROL_SECURITY,
                .status = event->enc_change.status,
            };
            if (ble_gap_conn_find(slot->conn_handle, &descriptor) == 0)
                slot->security = descriptor.sec_state;
            (void)esp32_mquickjs_event_queue_send_from_isr(
                slot->queue, &control, &task_woken);
        }
        if (state != NULL && (state->operation == BLE_OP_PAIR ||
                              (state->operation == BLE_OP_CONNECT &&
                               state->auto_pair))) {
            state->host_code = event->enc_change.status;
            atomic_store_explicit(&state->completed, true, memory_order_release);
            (void)esp32_mquickjs_future_wake_from_isr(state->runtime,
                                                       state->token, &task_woken);
        }
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        slot = ble_find_connection(event->passkey.conn_handle, NULL);
        if (slot != NULL) {
            ble_connection_event_t control = {
                .adapter_generation = s_ble.generation,
                .connection_generation = slot->generation,
                .sequence = atomic_fetch_add_explicit(&slot->sequence, 1,
                                                       memory_order_relaxed) + 1U,
                .timestamp_us = esp_timer_get_time(),
                .kind = BLE_CONTROL_PAIRING,
                .request_id = atomic_fetch_add_explicit(
                    &s_ble.next_pairing_request, 1, memory_order_relaxed) + 1U,
                .pairing_action = event->passkey.params.action,
                .passkey = event->passkey.params.numcmp,
                .expires_at_us = esp_timer_get_time() +
                                 (int64_t)s_ble.pairing_timeout_ms * 1000,
            };
            slot->pairing_request_id = control.request_id;
            slot->pairing_action = control.pairing_action;
            slot->pairing_passkey = control.passkey;
            slot->pairing_expires_at_us = control.expires_at_us;
            if (!esp32_mquickjs_event_queue_send_from_isr(
                    slot->queue, &control, &task_woken)) {
                struct ble_sm_io reject = {.action = event->passkey.params.action};
                if (reject.action == BLE_SM_IOACT_NUMCMP) reject.numcmp_accept = 0;
                (void)ble_sm_inject_io(slot->conn_handle, &reject);
                atomic_fetch_add_explicit(&s_ble.dropped_connection_events, 1,
                                          memory_order_relaxed);
            }
        }
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        slot = ble_find_connection(event->notify_rx.conn_handle, NULL);
        if (slot != NULL) {
            uint16_t index;
            uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
            for (index = 0; index < BLE_MAX_SUBSCRIPTIONS; ++index) {
                ble_subscription_t *subscription = &slot->subscriptions[index];
                uint16_t pool_index;
                ble_notification_event_t value_event;
                if (!subscription->open ||
                    subscription->value_handle != event->notify_rx.attr_handle)
                    continue;
                if (length > CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES ||
                    !esp32_mquickjs_wireless_pool_acquire(
                        &subscription->free_slots, &pool_index)) {
                    atomic_fetch_add_explicit(&subscription->dropped, 1,
                                              memory_order_relaxed);
                    break;
                }
                if (os_mbuf_copydata(event->notify_rx.om, 0, length,
                        subscription->payloads + pool_index *
                            CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES) != 0) {
                    ble_notification_pool_release(subscription, pool_index);
                    atomic_fetch_add_explicit(&subscription->dropped, 1,
                                              memory_order_relaxed);
                    break;
                }
                value_event = (ble_notification_event_t){
                    .adapter_generation = s_ble.generation,
                    .connection_generation = slot->generation,
                    .subscription_generation = subscription->generation,
                    .sequence = atomic_fetch_add_explicit(
                        &subscription->sequence, 1, memory_order_relaxed) + 1U,
                    .timestamp_us = esp_timer_get_time(),
                    .indication = event->notify_rx.indication != 0,
                    .length = length,
                    .pool_index = pool_index,
                };
                if (!esp32_mquickjs_event_queue_send_from_isr(
                        subscription->queue, &value_event, &task_woken)) {
                    ble_notification_pool_release(subscription, pool_index);
                    atomic_fetch_add_explicit(&subscription->dropped, 1,
                                              memory_order_relaxed);
                } else {
                    atomic_fetch_add_explicit(&subscription->received, 1,
                                              memory_order_relaxed);
                }
                break;
            }
        }
        break;
    case BLE_GAP_EVENT_NOTIFY_TX: {
        esp32_mquickjs_future_driver_state_t *notify_state =
            ble_active_state_acquire(&s_ble_server_notify_state);
        if (notify_state != NULL && notify_state->indication &&
            notify_state->attribute_handle == event->notify_tx.attr_handle &&
            event->notify_tx.indication) {
            uint16_t pending;
            if (event->notify_tx.status != 0 &&
                event->notify_tx.status != BLE_HS_EDONE &&
                notify_state->host_code == 0)
                notify_state->host_code = event->notify_tx.status;
            pending = atomic_fetch_sub_explicit(
                &notify_state->pending_confirmations, 1,
                memory_order_acq_rel);
            if (pending <= 1U) {
                atomic_store_explicit(&s_ble_server_notify_state, NULL,
                                      memory_order_release);
                atomic_store_explicit(&notify_state->completed, true,
                                      memory_order_release);
                (void)esp32_mquickjs_future_wake_from_isr(
                    notify_state->runtime, notify_state->token, &task_woken);
            }
        }
        ble_future_state_drop(notify_state);
        break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        ble_publish_server_subscription(event);
        break;
    default:
        break;
    }
    ble_future_state_drop(state);
    return 0;
}

static void ble_on_reset(int reason)
{
    (void)reason;
    s_ble.synchronized = false;
    atomic_fetch_add_explicit(&s_ble.reset_count, 1, memory_order_relaxed);
}

static void ble_on_sync(void)
{
    esp32_mquickjs_future_driver_state_t *state =
        ble_active_state_acquire(&s_ble_open_state);
    ble_addr_t random_address;
    uint8_t identity_type = BLE_ADDR_PUBLIC;
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        switch (s_ble.own_address_request) {
        case BLE_OWN_ADDRESS_PUBLIC:
            s_ble.own_addr_type = BLE_OWN_ADDR_PUBLIC;
            rc = ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, s_ble.address, NULL);
            break;
        case BLE_OWN_ADDRESS_RANDOM_STATIC:
            rc = ble_hs_id_gen_rnd(0, &random_address);
            if (rc == 0) rc = ble_hs_id_set_rnd(random_address.val);
            if (rc == 0) {
                s_ble.own_addr_type = BLE_OWN_ADDR_RANDOM;
                memcpy(s_ble.address, random_address.val,
                       sizeof(s_ble.address));
            }
            break;
        case BLE_OWN_ADDRESS_RPA:
            rc = ble_hs_id_infer_auto(1, &s_ble.own_addr_type);
            if (rc == 0) {
                identity_type =
                    s_ble.own_addr_type == BLE_OWN_ADDR_RPA_RANDOM_DEFAULT
                        ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
                rc = ble_hs_id_copy_addr(identity_type, s_ble.address, NULL);
            }
            break;
        default:
            rc = ble_hs_id_infer_auto(0, &s_ble.own_addr_type);
            if (rc == 0) {
                identity_type = s_ble.own_addr_type == BLE_OWN_ADDR_RANDOM
                                    ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
                rc = ble_hs_id_copy_addr(identity_type, s_ble.address, NULL);
            }
            break;
        }
    }
    if (state != NULL) {
        state->host_code = rc;
        s_ble.synchronized = rc == 0;
        atomic_store_explicit(&state->completed, true, memory_order_release);
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
    ble_future_state_drop(state);
}

static void ble_host_task(void *opaque)
{
    (void)opaque;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_future_state_storage_free(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) return;
    heap_caps_free(state->payload);
    heap_caps_free(state);
}

static void ble_future_state_release(esp32_mquickjs_future_driver_state_t *state)
{
    bool release = false;
    uint16_t index;
    if (state == NULL) return;
    if (state->owner_rooted) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
        state->owner_rooted = false;
    }
    if (state->queue_rooted) {
        JS_DeleteGCRef(state->ctx, &state->queue_ref);
        state->queue_rooted = false;
    }
    if (state->gatt_accounted && !state->gatt_started &&
        state->connection_index < s_ble.max_connections) {
        ble_connection_slot_t *slot =
            &s_ble.connections[state->connection_index];
        if (slot->generation == state->connection_generation)
            (void)atomic_fetch_sub_explicit(&slot->gatt_pending, 1,
                                            memory_order_acq_rel);
        state->gatt_started = true;
    }
    taskENTER_CRITICAL(&s_ble.lock);
    if (atomic_load_explicit(&s_ble_open_state, memory_order_relaxed) == state)
        atomic_store_explicit(&s_ble_open_state, NULL, memory_order_release);
    if (atomic_load_explicit(&s_ble_server_notify_state,
                             memory_order_relaxed) == state)
        atomic_store_explicit(&s_ble_server_notify_state, NULL,
                              memory_order_release);
    for (index = 0; index < CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS;
         ++index) {
        ble_connection_slot_t *slot = &s_ble.connections[index];
        if (atomic_load_explicit(&slot->active_gap_state,
                                 memory_order_relaxed) == state)
            atomic_store_explicit(&slot->active_gap_state, NULL,
                                  memory_order_release);
        if (atomic_load_explicit(&slot->active_gatt_state,
                                 memory_order_relaxed) == state)
            atomic_store_explicit(&slot->active_gatt_state, NULL,
                                  memory_order_release);
    }
    if (state->callback_refs == 0) release = true;
    else state->detached = true;
    taskEXIT_CRITICAL(&s_ble.lock);
    if (release) ble_future_state_storage_free(state);
}

static esp32_mquickjs_future_poll_t ble_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && atomic_load_explicit(&state->completed,
                                                  memory_order_acquire)
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static esp32_mquickjs_cancel_result_t ble_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->started) return ESP32_MQUICKJS_CANCEL_REJECTED;
    state->cancelled = true;
    atomic_store_explicit(&state->completed, true, memory_order_release);
    return ESP32_MQUICKJS_CANCELLED;
}

static uint32_t ble_future_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL ? state->timeout_ms : 0;
}

static JSValue ble_future_on_timeout(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state,
    uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (state != NULL && state->started) {
        switch (state->operation) {
        case BLE_OP_SCAN:
        case BLE_OP_SCANNER_CLOSE:
            (void)ble_gap_disc_cancel();
            s_ble.scanner.active = false;
            s_ble.scanner.stop_reason = BLE_STOP_ERROR;
            break;
        case BLE_OP_ADVERTISE:
        case BLE_OP_ADVERTISER_CLOSE:
            (void)ble_gap_adv_stop();
            s_ble.advertiser.active = false;
            s_ble.advertiser.stop_reason = BLE_STOP_ERROR;
            break;
        case BLE_OP_CONNECT:
            (void)ble_gap_conn_cancel();
            break;
        case BLE_OP_CONNECTION_CLOSE:
            if (state->connection_index < s_ble.max_connections) {
                ble_connection_slot_t *slot =
                    &s_ble.connections[state->connection_index];
                if (slot->open) {
                    (void)ble_gap_terminate(slot->conn_handle,
                                            BLE_ERR_REM_USER_CONN_TERM);
                }
            }
            break;
        case BLE_OP_DISCOVER:
        case BLE_OP_GATT_READ:
        case BLE_OP_GATT_WRITE:
        case BLE_OP_SUBSCRIBE:
        case BLE_OP_SUBSCRIPTION_CLOSE:
        case BLE_OP_PAIR:
        case BLE_OP_EXCHANGE_MTU:
        case BLE_OP_READ_RSSI:
            if (state->connection_index < s_ble.max_connections) {
                ble_connection_slot_t *slot =
                    &s_ble.connections[state->connection_index];
                if (slot->open)
                    (void)ble_gap_terminate(slot->conn_handle,
                                            BLE_ERR_REM_USER_CONN_TERM);
            }
            break;
        case BLE_OP_SERVER_NOTIFY: {
            uint16_t index;
            for (index = 0; index < s_ble.max_connections; ++index) {
                ble_connection_slot_t *slot = &s_ble.connections[index];
                if (slot->open &&
                    (state->connection_index == UINT16_MAX ||
                     state->connection_index == index)) {
                    (void)ble_gap_terminate(slot->conn_handle,
                                            BLE_ERR_REM_USER_CONN_TERM);
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return ble_throw_error(ctx, "BLE_TIMEOUT", BLE_HS_ETIMEOUT,
                           -1,
                           state != NULL ? state->connection_index : -1,
                           state != NULL ? state->attribute_handle : -1);
}

static bool ble_allocate_connection_queues(JSContext *ctx,
                                           ble_adapter_t *adapter)
{
    uint16_t index;
    for (index = 0; index < adapter->max_connections; ++index) {
        ble_connection_slot_t *slot = &adapter->connections[index];
        atomic_init(&slot->active_gap_state, NULL);
        atomic_init(&slot->active_gatt_state, NULL);
        atomic_init(&slot->gatt_pending, 0);
        JSValue queue = esp32_mquickjs_event_queue_new(
            ctx, adapter->runtime, sizeof(ble_connection_event_t),
            CONFIG_ESP32_MQUICKJS_BLE_CONNECTION_QUEUE_LEN,
            ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST,
            ble_connection_event_to_js, NULL, NULL, slot);
        if (JS_IsException(queue)) return false;
        *JS_AddGCRef(ctx, &slot->queue_ref) = queue;
        slot->queue_rooted = true;
        slot->queue = esp32_mquickjs_event_queue_from_value(ctx, queue);
        if (slot->queue == NULL) return false;
    }
    return true;
}

static void ble_free_server(ble_gatt_server_t *server)
{
    uint16_t index;
    if (server == NULL) return;
    if (server->event_queue != NULL) {
        (void)esp32_mquickjs_event_queue_close(server->event_queue);
        (void)esp32_mquickjs_event_queue_discard_all(server->event_queue);
    }
    if (server->event_queue_rooted) {
        JS_DeleteGCRef(s_ble.ctx, &server->event_queue_ref);
    }
    heap_caps_free(server->event_payloads);
    for (index = 0; index < server->characteristic_count; ++index) {
        heap_caps_free(server->characteristics[index].id);
        heap_caps_free(server->characteristics[index].value);
    }
    if (server->characteristic_defs != NULL) {
        for (index = 0; index < server->service_count; ++index) {
            heap_caps_free(server->characteristic_defs[index]);
        }
    }
    heap_caps_free(server->characteristic_defs);
    heap_caps_free(server->characteristics);
    heap_caps_free(server->service_uuids);
    heap_caps_free(server->services);
    memset(server, 0, sizeof(*server));
}

static void ble_release_scanner(ble_adapter_t *adapter)
{
    ble_scanner_t *scanner;
    if (adapter == NULL) return;
    scanner = &adapter->scanner;
    if (scanner->queue != NULL) {
        (void)esp32_mquickjs_event_queue_close(scanner->queue);
        (void)esp32_mquickjs_event_queue_discard_all(scanner->queue);
    }
    if (scanner->queue_rooted)
        JS_DeleteGCRef(adapter->ctx, &scanner->queue_ref);
    heap_caps_free(scanner->payloads);
    memset(scanner, 0, sizeof(*scanner));
}

static void ble_release_advertiser(ble_adapter_t *adapter)
{
    ble_advertiser_t *advertiser;
    if (adapter == NULL) return;
    advertiser = &adapter->advertiser;
    if (advertiser->queue != NULL) {
        (void)esp32_mquickjs_event_queue_close(advertiser->queue);
        (void)esp32_mquickjs_event_queue_discard_all(advertiser->queue);
    }
    if (advertiser->queue_rooted)
        JS_DeleteGCRef(adapter->ctx, &advertiser->queue_ref);
    memset(advertiser, 0, sizeof(*advertiser));
}

static void ble_free_pools(ble_adapter_t *adapter)
{
    uint16_t index;
    if (adapter == NULL) return;
    ble_release_scanner(adapter);
    ble_release_advertiser(adapter);
    for (index = 0; index < adapter->max_connections; ++index) {
        ble_connection_slot_t *slot = &adapter->connections[index];
        ble_release_connection(slot);
        if (slot->queue_rooted)
            JS_DeleteGCRef(adapter->ctx, &slot->queue_ref);
        memset(slot, 0, sizeof(*slot));
    }
    ble_free_server(&adapter->server);
}

static bool ble_parse_roles(JSContext *ctx, JSValue value,
                            bool *central, bool *peripheral)
{
    JSGCRef length_ref, item_ref;
    JSValue *length_value = JS_PushGCRef(ctx, &length_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    uint32_t length = 0;
    uint32_t index;
    bool result = false;
    *central = true;
    *peripheral = true;
    if (JS_IsUndefined(value)) {
        result = true;
        goto done;
    }
    if (!JS_IsArray(ctx, value)) {
        JS_ThrowTypeError(ctx, "roles must be an array");
        goto done;
    }
    *central = false;
    *peripheral = false;
    *length_value = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(*length_value) ||
        !ble_to_u32(ctx, *length_value, &length) || length == 0 || length > 2) {
        JS_ThrowRangeError(ctx, "roles must contain central and/or peripheral");
        goto done;
    }
    for (index = 0; index < length; ++index) {
        *item = JS_GetPropertyUint32(ctx, value, index);
        if (ble_string_equals(ctx, *item, "central")) {
            if (*central) {
                JS_ThrowTypeError(ctx, "roles must not contain duplicates");
                goto done;
            }
            *central = true;
        } else if (ble_string_equals(ctx, *item, "peripheral")) {
            if (*peripheral) {
                JS_ThrowTypeError(ctx, "roles must not contain duplicates");
                goto done;
            }
            *peripheral = true;
        } else {
            JS_ThrowTypeError(ctx, "unknown BLE role");
            goto done;
        }
    }
    result = true;
done:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &length_ref);
    return result;
}

static bool ble_array_length(JSContext *ctx, JSValue value,
                             const char *name, uint32_t *out)
{
    JSGCRef length_ref;
    JSValue *length = JS_PushGCRef(ctx, &length_ref);
    bool result = false;
    if (!JS_IsArray(ctx, value)) {
        JS_ThrowTypeError(ctx, "%s must be an array", name);
        goto done;
    }
    *length = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(*length) || !ble_to_u32(ctx, *length, out)) goto done;
    result = true;
done:
    JS_PopGCRef(ctx, &length_ref);
    return result;
}

static char *ble_copy_string(JSContext *ctx, JSValue value,
                             const char *name, size_t max_length)
{
    JSCStringBuf buffer;
    const char *text = JS_IsString(ctx, value)
                           ? JS_ToCString(ctx, value, &buffer) : NULL;
    size_t length;
    char *copy;
    if (text == NULL || (length = strlen(text)) == 0 || length > max_length) {
        JS_ThrowRangeError(ctx, "%s must contain 1..%u bytes", name,
                           (unsigned)max_length);
        return NULL;
    }
    copy = heap_caps_malloc(length + 1U, MALLOC_CAP_8BIT);
    if (copy == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    memcpy(copy, text, length + 1U);
    return copy;
}

static bool ble_parse_uuid(JSContext *ctx, JSValue value,
                           const char *name, ble_uuid_any_t *out)
{
    JSCStringBuf buffer;
    const char *text = JS_IsString(ctx, value)
                           ? JS_ToCString(ctx, value, &buffer) : NULL;
    if (text == NULL || !esp32_mquickjs_wireless_uuid_valid(text) ||
        ble_uuid_from_str(out, text) != 0) {
        JS_ThrowTypeError(ctx, "%s must be a 16, 32, or 128-bit UUID", name);
        return false;
    }
    return true;
}

static bool ble_parse_properties(JSContext *ctx, JSValue value,
                                 uint32_t *out_flags)
{
    JSGCRef item_ref;
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    uint32_t length, index;
    uint32_t flags = 0;
    bool result = false;
    if (!ble_array_length(ctx, value, "characteristic properties", &length) ||
        length == 0 || length > 7) goto done;
    for (index = 0; index < length; ++index) {
        uint32_t flag;
        *item = JS_GetPropertyUint32(ctx, value, index);
        if (ble_string_equals(ctx, *item, "broadcast")) flag = BLE_GATT_CHR_F_BROADCAST;
        else if (ble_string_equals(ctx, *item, "read")) flag = BLE_GATT_CHR_F_READ;
        else if (ble_string_equals(ctx, *item, "write")) flag = BLE_GATT_CHR_F_WRITE;
        else if (ble_string_equals(ctx, *item, "write-without-response")) flag = BLE_GATT_CHR_F_WRITE_NO_RSP;
        else if (ble_string_equals(ctx, *item, "notify")) flag = BLE_GATT_CHR_F_NOTIFY;
        else if (ble_string_equals(ctx, *item, "indicate")) flag = BLE_GATT_CHR_F_INDICATE;
        else if (ble_string_equals(ctx, *item, "authenticated-signed-write")) flag = BLE_GATT_CHR_F_AUTH_SIGN_WRITE;
        else {
            JS_ThrowTypeError(ctx, "unknown GATT characteristic property");
            goto done;
        }
        if ((flags & flag) != 0) {
            JS_ThrowTypeError(ctx, "duplicate GATT characteristic property");
            goto done;
        }
        flags |= flag;
    }
    *out_flags = flags;
    result = true;
done:
    JS_PopGCRef(ctx, &item_ref);
    return result;
}

static bool ble_parse_permissions(JSContext *ctx, JSValue value,
                                  uint32_t *flags)
{
    JSGCRef item_ref;
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    uint32_t length, index;
    bool result = false;
    if (JS_IsUndefined(value)) {
        result = true;
        goto done;
    }
    if (!ble_array_length(ctx, value, "characteristic permissions", &length) ||
        length > 4) goto done;
    for (index = 0; index < length; ++index) {
        uint32_t flag;
        *item = JS_GetPropertyUint32(ctx, value, index);
        if (ble_string_equals(ctx, *item, "encrypted-read")) flag = BLE_GATT_CHR_F_READ_ENC;
        else if (ble_string_equals(ctx, *item, "encrypted-write")) flag = BLE_GATT_CHR_F_WRITE_ENC;
        else if (ble_string_equals(ctx, *item, "authenticated-read")) flag = BLE_GATT_CHR_F_READ_AUTHEN;
        else if (ble_string_equals(ctx, *item, "authenticated-write")) flag = BLE_GATT_CHR_F_WRITE_AUTHEN;
        else {
            JS_ThrowTypeError(ctx, "unknown GATT characteristic permission");
            goto done;
        }
        if ((*flags & flag) != 0) {
            JS_ThrowTypeError(ctx, "duplicate GATT characteristic permission");
            goto done;
        }
        *flags |= flag;
    }
    result = true;
done:
    JS_PopGCRef(ctx, &item_ref);
    return result;
}

static int ble_server_event_pool_acquire(ble_gatt_server_t *server,
                                         uint16_t *index)
{
    return server != NULL &&
           esp32_mquickjs_wireless_pool_acquire(&server->free_slots, index);
}

static void ble_server_event_pool_release(ble_gatt_server_t *server,
                                          uint16_t index)
{
    if (server != NULL && index < server->event_capacity)
        (void)esp32_mquickjs_wireless_pool_release(&server->free_slots, index);
}

static void ble_server_event_drop(void *event, void *opaque)
{
    ble_server_event_t *server_event = event;
    ble_gatt_server_t *server = opaque;
    if (server_event != NULL && server_event->kind == BLE_SERVER_EVENT_WRITE)
        ble_server_event_pool_release(server, server_event->pool_index);
}

static JSValue ble_server_event_to_js(JSContext *ctx, const void *event,
                                      void *opaque)
{
    const ble_server_event_t *server_event = event;
    ble_gatt_server_t *server = opaque;
    ble_connection_slot_t *slot;
    uint16_t connection_index;
    uint8_t *payload = NULL;
    JSGCRef object_ref, connection_ref, data_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *connection = JS_PushGCRef(ctx, &connection_ref);
    JSValue *data = JS_PushGCRef(ctx, &data_ref);
    *object = JS_UNDEFINED;
    *connection = JS_UNDEFINED;
    *data = JS_UNDEFINED;
    if (server_event == NULL || server == NULL ||
        server_event->adapter_generation != s_ble.generation ||
        server_event->characteristic_index >= server->characteristic_count ||
        (slot = ble_find_connection(server_event->conn_handle,
                                    &connection_index)) == NULL) {
        JS_ThrowReferenceError(ctx, "BLE_STALE_CONNECTION: server event is stale");
        goto fail;
    }
    *connection = ble_new_connection_handle(ctx, connection_index, slot);
    *object = JS_NewObject(ctx);
    if (server_event->kind == BLE_SERVER_EVENT_WRITE) {
        if (server_event->length > 0) {
            payload = heap_caps_malloc(server_event->length, MALLOC_CAP_8BIT);
            if (payload == NULL) {
                ble_server_event_pool_release(server, server_event->pool_index);
                JS_ThrowOutOfMemory(ctx);
                goto fail;
            }
            memcpy(payload,
                   server->event_payloads + server_event->pool_index *
                                                CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES,
                   server_event->length);
        }
        ble_server_event_pool_release(server, server_event->pool_index);
        *data = esp32_mquickjs_new_owned_byte_view(ctx, payload,
                                                   server_event->length);
        payload = NULL;
    }
    if (JS_IsException(*object) || JS_IsException(*connection) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "type", JS_NewString(ctx,
                server_event->kind == BLE_SERVER_EVENT_WRITE
                    ? "write" : "subscription")) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "sequence",
                                         JS_NewUint32(ctx, server_event->sequence)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "timestampUs",
                                         JS_NewInt64(ctx, server_event->timestamp_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "connection", *connection) ||
        !esp32_mquickjs_set_property_ref(
            ctx, object, "characteristicId",
            JS_NewString(ctx, server->characteristics[
                                  server_event->characteristic_index].id))) goto fail;
    if (server_event->kind == BLE_SERVER_EVENT_WRITE) {
        if (JS_IsException(*data) ||
            !esp32_mquickjs_set_property_ref(ctx, object, "offset",
                                             JS_NewUint32(ctx, server_event->offset)) ||
            !esp32_mquickjs_set_property_ref(ctx, object, "data", *data)) goto fail;
    } else if (!esp32_mquickjs_set_property_ref(ctx, object, "notify",
                                                JS_NewBool(server_event->notify)) ||
               !esp32_mquickjs_set_property_ref(ctx, object, "indicate",
                                                JS_NewBool(server_event->indicate))) goto fail;
    JS_PopGCRef(ctx, &data_ref);
    JS_PopGCRef(ctx, &connection_ref);
    return JS_PopGCRef(ctx, &object_ref);
fail:
    heap_caps_free(payload);
    JS_PopGCRef(ctx, &data_ref);
    JS_PopGCRef(ctx, &connection_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static bool ble_parse_gatt_server(JSContext *ctx, JSValue definition,
                                  ble_adapter_t *adapter)
{
    static const char *const server_allowed[] = {"services"};
    static const char *const service_allowed[] = {
        "id", "uuid", "primary", "characteristics",
    };
    static const char *const characteristic_allowed[] = {
        "id", "uuid", "properties", "permissions", "maxLength",
        "initialValue", "storeWrites",
    };
    ble_gatt_server_t *server = &adapter->server;
    JSGCRef services_ref, service_ref, characteristics_ref, characteristic_ref,
        property_ref;
    JSValue *services = JS_PushGCRef(ctx, &services_ref);
    JSValue *service = JS_PushGCRef(ctx, &service_ref);
    JSValue *characteristics = JS_PushGCRef(ctx, &characteristics_ref);
    JSValue *characteristic = JS_PushGCRef(ctx, &characteristic_ref);
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    uint32_t service_count, total_characteristics = 0;
    uint32_t service_index, characteristic_index, global_index = 0;
    char *service_ids[CONFIG_ESP32_MQUICKJS_BLE_MAX_SERVICES] = {0};
    bool result = false;
    JSValue queue;

    if (!ble_is_object(ctx, definition) ||
        !ble_validate_option_keys(ctx, definition, "BLE GATT server",
                                  server_allowed, 1)) goto done;
    *services = JS_GetPropertyStr(ctx, definition, "services");
    if (JS_IsException(*services) ||
        !ble_array_length(ctx, *services, "server services", &service_count) ||
        service_count == 0 ||
        service_count > CONFIG_ESP32_MQUICKJS_BLE_MAX_SERVICES) {
        if (!JS_HasException(ctx))
            ble_throw_error(ctx, "BLE_SERVER_LIMIT", BLE_HS_ENOMEM,
                            -1, -1, -1);
        goto done;
    }
    for (service_index = 0; service_index < service_count; ++service_index) {
        uint32_t count;
        *service = JS_GetPropertyUint32(ctx, *services, service_index);
        if (!ble_is_object(ctx, *service) ||
            !ble_validate_option_keys(ctx, *service, "BLE local service",
                                      service_allowed, 4)) goto done;
        *characteristics = JS_GetPropertyStr(ctx, *service, "characteristics");
        if (JS_IsException(*characteristics) ||
            !ble_array_length(ctx, *characteristics,
                              "service characteristics", &count) || count == 0)
            goto done;
        total_characteristics += count;
        if (total_characteristics > CONFIG_ESP32_MQUICKJS_BLE_MAX_CHARACTERISTICS) {
            ble_throw_error(ctx, "BLE_SERVER_LIMIT", BLE_HS_ENOMEM,
                            -1, -1, -1);
            goto done;
        }
    }
    server->services = heap_caps_calloc(service_count + 1U,
                                         sizeof(*server->services), MALLOC_CAP_8BIT);
    server->service_uuids = heap_caps_calloc(service_count,
                                              sizeof(*server->service_uuids), MALLOC_CAP_8BIT);
    server->characteristic_defs = heap_caps_calloc(
        service_count, sizeof(*server->characteristic_defs), MALLOC_CAP_8BIT);
    server->characteristics = heap_caps_calloc(
        total_characteristics, sizeof(*server->characteristics), MALLOC_CAP_8BIT);
    if (server->services == NULL || server->service_uuids == NULL ||
        server->characteristic_defs == NULL || server->characteristics == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto done;
    }
    server->service_count = service_count;
    server->characteristic_count = total_characteristics;
    for (service_index = 0; service_index < service_count; ++service_index) {
        uint32_t count;
        uint32_t prior_service;
        bool primary = true;
        *service = JS_GetPropertyUint32(ctx, *services, service_index);
        *property = JS_GetPropertyStr(ctx, *service, "id");
        service_ids[service_index] = JS_IsException(*property) ? NULL
            : ble_copy_string(ctx, *property, "service id", 64);
        if (service_ids[service_index] == NULL) goto done;
        for (prior_service = 0; prior_service < service_index;
             ++prior_service) {
            if (strcmp(service_ids[service_index],
                       service_ids[prior_service]) == 0) {
                JS_ThrowTypeError(ctx, "GATT service ids must be unique");
                goto done;
            }
        }
        *property = JS_GetPropertyStr(ctx, *service, "uuid");
        if (JS_IsException(*property) ||
            !ble_parse_uuid(ctx, *property, "service uuid",
                            &server->service_uuids[service_index])) goto done;
        *property = JS_GetPropertyStr(ctx, *service, "primary");
        if (JS_IsException(*property)) goto done;
        if (!JS_IsUndefined(*property)) {
            if (!JS_IsBool(*property)) {
                JS_ThrowTypeError(ctx, "service primary must be a boolean");
                goto done;
            }
            primary = JS_VALUE_GET_SPECIAL_VALUE(*property) != 0;
        }
        *characteristics = JS_GetPropertyStr(ctx, *service, "characteristics");
        if (!ble_array_length(ctx, *characteristics, "service characteristics",
                              &count)) goto done;
        server->characteristic_defs[service_index] = heap_caps_calloc(
            count + 1U, sizeof(struct ble_gatt_chr_def), MALLOC_CAP_8BIT);
        if (server->characteristic_defs[service_index] == NULL) {
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }
        server->services[service_index].type =
            primary ? BLE_GATT_SVC_TYPE_PRIMARY : BLE_GATT_SVC_TYPE_SECONDARY;
        server->services[service_index].uuid =
            &server->service_uuids[service_index].u;
        server->services[service_index].characteristics =
            server->characteristic_defs[service_index];
        for (characteristic_index = 0; characteristic_index < count;
             ++characteristic_index, ++global_index) {
            ble_local_characteristic_t *local =
                &server->characteristics[global_index];
            struct ble_gatt_chr_def *native =
                &server->characteristic_defs[service_index][characteristic_index];
            uint32_t max_length;
            esp32_mquickjs_byte_source_t source;
            uint8_t *owned = NULL;
            JSValue error = JS_UNDEFINED;
            bool store_writes = true;
            uint32_t prior;
            *characteristic = JS_GetPropertyUint32(
                ctx, *characteristics, characteristic_index);
            if (!ble_is_object(ctx, *characteristic) ||
                !ble_validate_option_keys(ctx, *characteristic,
                                          "BLE local characteristic",
                                          characteristic_allowed, 7)) goto done;
            *property = JS_GetPropertyStr(ctx, *characteristic, "id");
            local->id = JS_IsException(*property) ? NULL
                : ble_copy_string(ctx, *property, "characteristic id", 64);
            if (local->id == NULL) goto done;
            for (prior = 0; prior < global_index; ++prior) {
                if (strcmp(local->id, server->characteristics[prior].id) == 0) {
                    JS_ThrowTypeError(ctx,
                                      "GATT characteristic ids must be unique");
                    goto done;
                }
            }
            *property = JS_GetPropertyStr(ctx, *characteristic, "uuid");
            if (JS_IsException(*property) ||
                !ble_parse_uuid(ctx, *property, "characteristic uuid",
                                &local->uuid)) goto done;
            *property = JS_GetPropertyStr(ctx, *characteristic, "properties");
            if (JS_IsException(*property) ||
                !ble_parse_properties(ctx, *property, &local->properties)) goto done;
            *property = JS_GetPropertyStr(ctx, *characteristic, "permissions");
            if (JS_IsException(*property) ||
                !ble_parse_permissions(ctx, *property, &local->properties)) goto done;
            *property = JS_GetPropertyStr(ctx, *characteristic, "maxLength");
            if (JS_IsException(*property) ||
                !ble_to_u32(ctx, *property, &max_length) || max_length == 0 ||
                max_length > CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES) {
                JS_ThrowRangeError(ctx, "maxLength exceeds the compiled limit");
                goto done;
            }
            local->max_length = max_length;
            local->store_writes = true;
            local->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
            local->value = heap_caps_calloc(max_length, 1,
                                             MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
            if (local->value == NULL) {
                JS_ThrowOutOfMemory(ctx);
                goto done;
            }
            if (!ble_get_bool(ctx, *characteristic, "storeWrites", true,
                              &store_writes)) goto done;
            local->store_writes = store_writes;
            *property = JS_GetPropertyStr(ctx, *characteristic, "initialValue");
            if (JS_IsException(*property)) goto done;
            if (!JS_IsUndefined(*property)) {
                if (!esp32_mquickjs_get_byte_source(
                        ctx, *property, "GATT initialValue", &source,
                        &owned, &error)) goto done;
                if (source.length > max_length) {
                    esp32_mquickjs_release_byte_source(owned);
                    ble_throw_error(ctx, "BLE_PAYLOAD_TOO_LARGE",
                                    BLE_HS_EMSGSIZE, -1, -1, -1);
                    goto done;
                }
                memcpy(local->value, source.data, source.length);
                local->length = source.length;
                esp32_mquickjs_release_byte_source(owned);
            }
            native->uuid = &local->uuid.u;
            native->access_cb = ble_gatt_server_access;
            native->arg = local;
            native->flags = local->properties;
            native->val_handle = &local->value_handle;
        }
    }
    server->event_capacity = CONFIG_ESP32_MQUICKJS_BLE_SERVER_QUEUE_LEN;
    server->event_payloads = heap_caps_calloc(
        server->event_capacity, CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES,
        MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!esp32_mquickjs_wireless_pool_init(&server->free_slots,
                                            server->event_capacity) ||
        server->event_payloads == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto done;
    }
    queue = esp32_mquickjs_event_queue_new(
        ctx, adapter->runtime, sizeof(ble_server_event_t),
        server->event_capacity, ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
        ble_server_event_to_js, ble_server_event_drop, NULL, server);
    if (JS_IsException(queue)) goto done;
    *JS_AddGCRef(ctx, &server->event_queue_ref) = queue;
    server->event_queue_rooted = true;
    server->event_queue = esp32_mquickjs_event_queue_from_value(ctx, queue);
    if (server->event_queue == NULL) goto done;
    server->configured = true;
    atomic_init(&server->sequence, 0);
    atomic_init(&server->writes, 0);
    atomic_init(&server->notifications, 0);
    atomic_init(&server->indications, 0);
    atomic_init(&server->dropped, 0);
    result = true;
done:
    for (service_index = 0;
         service_index < CONFIG_ESP32_MQUICKJS_BLE_MAX_SERVICES;
         ++service_index)
        heap_caps_free(service_ids[service_index]);
    if (!result) ble_free_server(server);
    JS_PopGCRef(ctx, &property_ref);
    JS_PopGCRef(ctx, &characteristic_ref);
    JS_PopGCRef(ctx, &characteristics_ref);
    JS_PopGCRef(ctx, &service_ref);
    JS_PopGCRef(ctx, &services_ref);
    return result;
}

static bool ble_parse_open_options(JSContext *ctx, int argc, JSGCRef *argv,
                                   ble_adapter_t *adapter)
{
    static const char *const allowed[] = {
        "roles", "deviceName", "ownAddressType", "preferredMtu",
        "maxConnections", "security", "server",
    };
    static const char *const security_allowed[] = {
        "bonding", "secureConnections", "mitm", "ioCapability",
        "pairingTimeoutMs",
    };
    JSGCRef property_ref, nested_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    JSValue *nested = JS_PushGCRef(ctx, &nested_ref);
    uint32_t raw;
    JSCStringBuf text_buffer;
    const char *text;
    bool result = false;
    JSValue options = argc == 1 ? argv[0].val : JS_UNDEFINED;

    adapter->role_central = true;
    adapter->role_peripheral = true;
    adapter->own_address_request = BLE_OWN_ADDRESS_AUTO;
    adapter->preferred_mtu = BLE_DEFAULT_MTU;
    adapter->max_connections = CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS;
    adapter->pairing_timeout_ms = BLE_PAIRING_TIMEOUT_MS;
    adapter->bonding = CONFIG_ESP32_MQUICKJS_BLE_BONDING;
    adapter->secure_connections = true;
    adapter->mitm = false;
    adapter->io_capability = BLE_HS_IO_NO_INPUT_OUTPUT;
    memcpy(adapter->device_name, "ESP32QJS", sizeof("ESP32QJS"));
    if (argc > 1) {
        JS_ThrowTypeError(ctx, "ble.open(options?) expects at most one argument");
        goto done;
    }
    if (JS_IsUndefined(options)) {
        result = true;
        goto done;
    }
    if (!ble_is_object(ctx, options) ||
        !ble_validate_option_keys(ctx, options, "ble.open()", allowed, 7)) {
        if (!JS_HasException(ctx))
            JS_ThrowTypeError(ctx, "ble.open options must be an object");
        goto done;
    }
    *property = JS_GetPropertyStr(ctx, options, "roles");
    if (JS_IsException(*property) ||
        !ble_parse_roles(ctx, *property, &adapter->role_central,
                         &adapter->role_peripheral)) goto done;
    *property = JS_GetPropertyStr(ctx, options, "deviceName");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        text = JS_IsString(ctx, *property)
                   ? JS_ToCString(ctx, *property, &text_buffer) : NULL;
        if (text == NULL || strlen(text) == 0 || strlen(text) > BLE_MAX_DEVICE_NAME) {
            JS_ThrowRangeError(ctx, "deviceName must contain 1..64 bytes");
            goto done;
        }
        memcpy(adapter->device_name, text, strlen(text) + 1U);
    }
    *property = JS_GetPropertyStr(ctx, options, "preferredMtu");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!ble_to_u32(ctx, *property, &raw) || raw < 23 ||
            raw > CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU) {
            JS_ThrowRangeError(ctx, "preferredMtu is outside the compiled range");
            goto done;
        }
        adapter->preferred_mtu = raw;
    }
    *property = JS_GetPropertyStr(ctx, options, "maxConnections");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!ble_to_u32(ctx, *property, &raw) || raw == 0 ||
            raw > CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS) {
            JS_ThrowRangeError(ctx, "maxConnections exceeds the compiled limit");
            goto done;
        }
        adapter->max_connections = raw;
    }
    *property = JS_GetPropertyStr(ctx, options, "ownAddressType");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (ble_string_equals(ctx, *property, "public"))
            adapter->own_address_request = BLE_OWN_ADDRESS_PUBLIC;
        else if (ble_string_equals(ctx, *property, "random-static"))
            adapter->own_address_request = BLE_OWN_ADDRESS_RANDOM_STATIC;
        else if (ble_string_equals(ctx, *property, "rpa"))
            adapter->own_address_request = BLE_OWN_ADDRESS_RPA;
        else {
            JS_ThrowTypeError(ctx, "invalid ownAddressType");
            goto done;
        }
    }
    *nested = JS_GetPropertyStr(ctx, options, "security");
    if (JS_IsException(*nested)) goto done;
    if (!JS_IsUndefined(*nested)) {
        if (!ble_is_object(ctx, *nested) ||
            !ble_validate_option_keys(ctx, *nested, "BLE security",
                                      security_allowed, 5)) {
            if (!JS_HasException(ctx))
                JS_ThrowTypeError(ctx, "security must be an object");
            goto done;
        }
        if (!ble_get_bool(ctx, *nested, "bonding", adapter->bonding,
                          &adapter->bonding) ||
            !ble_get_bool(ctx, *nested, "secureConnections", true,
                          &adapter->secure_connections) ||
            !ble_get_bool(ctx, *nested, "mitm", false, &adapter->mitm)) goto done;
        *property = JS_GetPropertyStr(ctx, *nested, "pairingTimeoutMs");
        if (JS_IsException(*property)) goto done;
        if (!JS_IsUndefined(*property)) {
            if (!ble_to_u32(ctx, *property, &raw) || raw < 1000 || raw > 60000) {
                JS_ThrowRangeError(ctx, "pairingTimeoutMs must be in 1000..60000");
                goto done;
            }
            adapter->pairing_timeout_ms = raw;
        }
        *property = JS_GetPropertyStr(ctx, *nested, "ioCapability");
        if (JS_IsException(*property)) goto done;
        if (!JS_IsUndefined(*property)) {
            if (ble_string_equals(ctx, *property, "none"))
                adapter->io_capability = BLE_HS_IO_NO_INPUT_OUTPUT;
            else if (ble_string_equals(ctx, *property, "display-only"))
                adapter->io_capability = BLE_HS_IO_DISPLAY_ONLY;
            else if (ble_string_equals(ctx, *property, "keyboard-only"))
                adapter->io_capability = BLE_HS_IO_KEYBOARD_ONLY;
            else if (ble_string_equals(ctx, *property, "display-keyboard"))
                adapter->io_capability = BLE_HS_IO_KEYBOARD_DISPLAY;
            else if (ble_string_equals(ctx, *property, "display-yes-no"))
                adapter->io_capability = BLE_HS_IO_DISPLAY_YESNO;
            else {
                JS_ThrowTypeError(ctx, "invalid BLE ioCapability");
                goto done;
            }
        }
    }
    *property = JS_GetPropertyStr(ctx, options, "server");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property) &&
        !ble_parse_gatt_server(ctx, *property, adapter)) goto done;
    result = true;
done:
    JS_PopGCRef(ctx, &nested_ref);
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static bool ble_open_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    uint32_t generation;
    (void)this_ref;
    if (out_state == NULL) return false;
    *out_state = NULL;
    if (s_ble.lifecycle != BLE_LIFECYCLE_CLOSED) {
        ble_throw_error(ctx, "BLE_ALREADY_OPEN", BLE_HS_EALREADY, -1, -1, -1);
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_OPEN;
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    atomic_init(&state->completed, false);
    generation = ble_next_generation(&s_ble_next_generation);
    memset(&s_ble, 0, sizeof(s_ble));
    s_ble.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    s_ble.lifecycle = BLE_LIFECYCLE_OPENING;
    s_ble.ctx = ctx;
    s_ble.runtime = esp32_mquickjs_get_active_runtime();
    s_ble.generation = generation;
    atomic_init(&s_ble.reset_count, 0);
    atomic_init(&s_ble.dropped_connection_events, 0);
    atomic_init(&s_ble.next_pairing_request, 0);
    state->adapter_generation = generation;
    if (!ble_parse_open_options(ctx, argc, argv, &s_ble) ||
        !ble_allocate_connection_queues(ctx, &s_ble)) {
        ble_free_pools(&s_ble);
        s_ble.lifecycle = BLE_LIFECYCLE_CLOSED;
        heap_caps_free(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool ble_open_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    esp_err_t err;
    if (state == NULL || state->adapter_generation != s_ble.generation ||
        s_ble.lifecycle != BLE_LIFECYCLE_OPENING) {
        ble_throw_error(ctx, "BLE_STALE_ADAPTER", BLE_HS_EINVAL, -1, -1, -1);
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    ble_active_state_bind(&s_ble_open_state, state);
    err = esp32_mquickjs_nvs_flash_ensure_initialized();
    if (err == ESP_OK) err = nimble_port_init();
    if (err != ESP_OK) {
        state->host_code = err;
        s_ble.lifecycle = BLE_LIFECYCLE_FAILED;
        ble_throw_error(ctx, "BLE_NOT_OPEN", err, -1, -1, -1);
        return false;
    }
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.sm_io_cap = s_ble.io_capability;
    ble_hs_cfg.sm_bonding = s_ble.bonding;
    ble_hs_cfg.sm_sc = s_ble.secure_connections;
    ble_hs_cfg.sm_mitm = s_ble.mitm;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                 BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                   BLE_SM_PAIR_KEY_DIST_ID;
    if (ble_svc_gap_device_name_set(s_ble.device_name) != 0 ||
        ble_att_set_preferred_mtu(s_ble.preferred_mtu) != 0) {
        (void)nimble_port_deinit();
        s_ble.lifecycle = BLE_LIFECYCLE_FAILED;
        ble_throw_error(ctx, "BLE_NOT_OPEN", BLE_HS_EINVAL, -1, -1, -1);
        return false;
    }
    ble_gatt_server_register();
    if (s_ble.server.configured && !s_ble.server.open) {
        int host_code = state->host_code != 0 ? state->host_code : BLE_HS_EUNKNOWN;
        (void)nimble_port_deinit();
        s_ble.lifecycle = BLE_LIFECYCLE_FAILED;
        ble_throw_error(ctx, "BLE_GATT_ERROR", host_code, -1, -1, -1);
        return false;
    }
    nimble_port_freertos_init(ble_host_task);
    s_ble.host_started = true;
    return true;
}

static JSValue ble_open_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    ble_adapter_ref_t *ref;
    JSValue object;
    if (state == NULL || state->host_code != 0 || !s_ble.synchronized) {
        return ble_throw_error(ctx, "BLE_NOT_OPEN",
                               state != NULL ? state->host_code : BLE_HS_EINVAL,
                               -1, -1, -1);
    }
    object = JS_NewObjectClassUser(ctx, JS_CLASS_BLE_ADAPTER);
    if (JS_IsException(object)) return object;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) return JS_ThrowOutOfMemory(ctx);
    ref->generation = s_ble.generation;
    JS_SetOpaque(ctx, object, ref);
    s_ble.lifecycle = BLE_LIFECYCLE_ACTIVE;
    ble_active_state_bind(&s_ble_open_state, NULL);
    state->transferred = true;
    return object;
}

static void ble_open_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL && !state->transferred &&
        state->adapter_generation == s_ble.generation) {
        if (s_ble.host_started) {
            (void)nimble_port_stop();
            (void)nimble_port_deinit();
        }
        ble_free_pools(&s_ble);
        s_ble.lifecycle = BLE_LIFECYCLE_CLOSED;
    }
    ble_future_state_release(state);
}

static const esp32_mquickjs_future_driver_t s_ble_open_driver = {
    .capture = ble_open_capture,
    .start = ble_open_start,
    .poll = ble_future_poll,
    .finish = ble_open_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_open_destroy,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_adapter_resource_key,
};

static ble_scanner_t *ble_scanner_from_value(JSContext *ctx, JSValue value,
                                             bool throw_if_stale)
{
    ble_scanner_ref_t *ref;
    if (JS_GetClassID(ctx, value) != JS_CLASS_BLE_SCANNER ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "expected a BLEScanner instance");
        return NULL;
    }
    if (ref->adapter_generation != s_ble.generation ||
        ref->generation != s_ble.scanner.generation ||
        !s_ble.scanner.allocated) {
        if (throw_if_stale)
            JS_ThrowReferenceError(ctx, "BLE_STALE_ADAPTER: scanner is closed");
        return NULL;
    }
    return &s_ble.scanner;
}

static bool ble_scan_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    static const char *const allowed[] = {
        "active", "intervalMs", "windowMs", "durationMs",
        "filterDuplicates", "limited", "capacity",
    };
    esp32_mquickjs_future_driver_state_t *state;
    ble_adapter_t *adapter;
    ble_scanner_t *scanner = &s_ble.scanner;
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    JSValue options = argc == 1 ? argv[0].val : JS_UNDEFINED;
    uint32_t capacity = CONFIG_ESP32_MQUICKJS_BLE_SCAN_QUEUE_LEN;
    uint32_t raw;
    double number;
    bool present;
    bool active = true, filter_duplicates = true, limited = false;
    JSValue queue;

    if (out_state == NULL || argc > 1 ||
        (adapter = ble_adapter_from_value(ctx, this_ref->val, true)) == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    *out_state = NULL;
    if (scanner->allocated || adapter->advertiser.active) {
        JS_PopGCRef(ctx, &property_ref);
        ble_throw_error(ctx, "BLE_GAP_CONFLICT", BLE_HS_EBUSY, -1, -1, -1);
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_SCAN;
    state->adapter_generation = adapter->generation;
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    state->duration_ms = BLE_HS_FOREVER;
    atomic_init(&state->completed, false);
    if (!JS_IsUndefined(options)) {
        if (!ble_is_object(ctx, options) ||
            !ble_validate_option_keys(ctx, options, "BLEAdapter.scan()",
                                      allowed, 7) ||
            !ble_get_bool(ctx, options, "active", true, &active) ||
            !ble_get_bool(ctx, options, "filterDuplicates", true,
                          &filter_duplicates) ||
            !ble_get_bool(ctx, options, "limited", false, &limited)) goto fail;
        *property = JS_GetPropertyStr(ctx, options, "capacity");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (!ble_to_u32(ctx, *property, &raw) || raw == 0 ||
                raw > CONFIG_ESP32_MQUICKJS_BLE_SCAN_QUEUE_MAX_LEN) {
                JS_ThrowRangeError(ctx, "scan capacity exceeds the compiled limit");
                goto fail;
            }
            capacity = raw;
        }
        if (!ble_get_number(ctx, options, "intervalMs", &number, &present)) goto fail;
        if (present) {
            if (number < 2.5 || number > 10240.0) {
                JS_ThrowRangeError(ctx, "intervalMs must be in 2.5..10240");
                goto fail;
            }
            state->scan_params.itvl = ble_ms_to_625us(number);
        }
        if (!ble_get_number(ctx, options, "windowMs", &number, &present)) goto fail;
        if (present) {
            if (number < 2.5 || number > 10240.0) {
                JS_ThrowRangeError(ctx, "windowMs must be in 2.5..10240");
                goto fail;
            }
            state->scan_params.window = ble_ms_to_625us(number);
        }
        if (state->scan_params.itvl != 0 && state->scan_params.window != 0 &&
            state->scan_params.window > state->scan_params.itvl) {
            JS_ThrowRangeError(ctx, "windowMs must not exceed intervalMs");
            goto fail;
        }
        *property = JS_GetPropertyStr(ctx, options, "durationMs");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (!ble_to_u32(ctx, *property, &raw) || raw == 0 || raw > INT32_MAX) {
                JS_ThrowRangeError(ctx, "durationMs must be in 1..2147483647");
                goto fail;
            }
            state->duration_ms = raw;
            state->timeout_ms = raw + 1000U;
        }
    }
    state->scan_params.passive = !active;
    state->scan_params.filter_duplicates = filter_duplicates;
    state->scan_params.limited = limited;
    memset(scanner, 0, sizeof(*scanner));
    scanner->allocated = true;
    scanner->open = true;
    scanner->generation = ble_next_generation(&s_ble_next_scanner_generation);
    scanner->capacity = capacity;
    scanner->stop_reason = BLE_STOP_RUNNING;
    atomic_init(&scanner->sequence, 0);
    atomic_init(&scanner->reports, 0);
    atomic_init(&scanner->dropped, 0);
    atomic_init(&scanner->malformed, 0);
    scanner->payloads = heap_caps_calloc(capacity, BLE_HS_ADV_MAX_SZ,
                                         MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!esp32_mquickjs_wireless_pool_init(&scanner->free_slots, capacity) ||
        scanner->payloads == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail_scanner;
    }
    queue = esp32_mquickjs_event_queue_new(
        ctx, adapter->runtime, sizeof(ble_scan_event_t), capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST, ble_scan_event_to_js,
        ble_scan_event_drop, NULL, scanner);
    if (JS_IsException(queue)) goto fail_scanner;
    *JS_AddGCRef(ctx, &scanner->queue_ref) = queue;
    scanner->queue_rooted = true;
    scanner->queue = esp32_mquickjs_event_queue_from_value(ctx, queue);
    if (scanner->queue == NULL) goto fail_scanner;
    state->connection_generation = scanner->generation;
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    JS_PopGCRef(ctx, &property_ref);
    return true;
fail_scanner:
    if (scanner->queue_rooted) JS_DeleteGCRef(ctx, &scanner->queue_ref);
    heap_caps_free(scanner->payloads);
    memset(scanner, 0, sizeof(*scanner));
fail:
    heap_caps_free(state);
    JS_PopGCRef(ctx, &property_ref);
    return false;
}

static bool ble_scan_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    int rc;
    if (state == NULL || s_ble.lifecycle != BLE_LIFECYCLE_ACTIVE ||
        state->adapter_generation != s_ble.generation ||
        state->connection_generation != s_ble.scanner.generation) {
        ble_throw_error(ctx, "BLE_STALE_ADAPTER", BLE_HS_EINVAL, -1, -1, -1);
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    rc = ble_gap_disc(s_ble.own_addr_type, state->duration_ms,
                      &state->scan_params, ble_gap_event_callback, NULL);
    if (rc != 0) {
        state->host_code = rc;
        s_ble.scanner.stop_reason = BLE_STOP_ERROR;
        ble_throw_error(ctx, "BLE_GAP_CONFLICT", rc, -1, -1, -1);
        return false;
    }
    s_ble.scanner.active = true;
    s_ble.scanner.started_at_us = esp_timer_get_time();
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static JSValue ble_scan_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    ble_scanner_ref_t *ref;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BLE_SCANNER);
    if (JS_IsException(*object)) goto fail;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ref->adapter_generation = s_ble.generation;
    ref->generation = s_ble.scanner.generation;
    JS_SetOpaque(ctx, *object, ref);
    if (!esp32_mquickjs_set_property_ref(ctx, object, "_eventQueue",
                                         s_ble.scanner.queue_ref.val)) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
        goto fail;
    }
    state->transferred = true;
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static void ble_scan_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL && !state->transferred &&
        state->connection_generation == s_ble.scanner.generation) {
        if (s_ble.scanner.active) (void)ble_gap_disc_cancel();
        if (s_ble.scanner.queue != NULL)
            (void)esp32_mquickjs_event_queue_close(s_ble.scanner.queue);
        if (s_ble.scanner.queue_rooted)
            JS_DeleteGCRef(state->ctx, &s_ble.scanner.queue_ref);
        heap_caps_free(s_ble.scanner.payloads);
        memset(&s_ble.scanner, 0, sizeof(s_ble.scanner));
    }
    ble_future_state_release(state);
}

static bool ble_scanner_close_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    ble_scanner_t *scanner;
    (void)argv;
    if (out_state == NULL || argc != 0 ||
        (scanner = ble_scanner_from_value(ctx, this_ref->val, true)) == NULL)
        return false;
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_SCANNER_CLOSE;
    state->adapter_generation = s_ble.generation;
    state->connection_generation = scanner->generation;
    state->timeout_ms = 2000;
    atomic_init(&state->completed, false);
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
}

static bool ble_scanner_close_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    int rc = 0;
    (void)ctx;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (s_ble.scanner.active) {
        rc = ble_gap_disc_cancel();
        if (rc == BLE_HS_EALREADY) rc = 0;
    }
    state->host_code = rc;
    if (rc == 0) {
        s_ble.scanner.active = false;
        s_ble.scanner.stop_reason = BLE_STOP_CLOSED;
    } else {
        s_ble.scanner.stop_reason = BLE_STOP_ERROR;
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static JSValue ble_scanner_close_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->host_code != 0)
        return ble_bool_finish(ctx, state);
    ble_release_scanner(&s_ble);
    return JS_TRUE;
}

static JSValue ble_bool_finish(JSContext *ctx,
                               esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->host_code != 0)
        return ble_throw_error(ctx, "BLE_GATT_ERROR",
                               state != NULL ? state->host_code : BLE_HS_EINVAL,
                               state != NULL ? state->att_code : -1,
                               state != NULL ? state->connection_index : -1,
                               state != NULL ? state->attribute_handle : -1);
    return JS_NewBool(state->result_bool || true);
}

static const esp32_mquickjs_future_driver_t s_ble_scan_driver = {
    .capture = ble_scan_capture,
    .start = ble_scan_start,
    .poll = ble_future_poll,
    .finish = ble_scan_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_scan_destroy,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gap_resource_key,
};

static const esp32_mquickjs_future_driver_t s_ble_scanner_close_driver = {
    .capture = ble_scanner_close_capture,
    .start = ble_scanner_close_start,
    .poll = ble_future_poll,
    .finish = ble_scanner_close_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gap_resource_key,
};

static ble_advertiser_t *ble_advertiser_from_value(JSContext *ctx,
                                                   JSValue value,
                                                   bool throw_if_stale)
{
    ble_advertiser_ref_t *ref;
    if (JS_GetClassID(ctx, value) != JS_CLASS_BLE_ADVERTISER ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "expected a BLEAdvertiser instance");
        return NULL;
    }
    if (ref->adapter_generation != s_ble.generation ||
        ref->generation != s_ble.advertiser.generation ||
        !s_ble.advertiser.allocated) {
        if (throw_if_stale)
            JS_ThrowReferenceError(ctx, "BLE_STALE_ADAPTER: advertiser is closed");
        return NULL;
    }
    return &s_ble.advertiser;
}

static bool ble_copy_small_byte_source(JSContext *ctx, JSValue value,
                                       const char *api_name,
                                       uint8_t output[BLE_HS_ADV_MAX_SZ],
                                       uint8_t *out_length)
{
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue error = JS_UNDEFINED;
    if (!esp32_mquickjs_get_byte_source(ctx, value, api_name, &source, &owned,
                                        &error)) return false;
    if (source.length > BLE_HS_ADV_MAX_SZ) {
        esp32_mquickjs_release_byte_source(owned);
        ble_throw_error(ctx, "BLE_PAYLOAD_TOO_LARGE", BLE_HS_EMSGSIZE,
                        -1, -1, -1);
        return false;
    }
    if (source.length > 0) memcpy(output, source.data, source.length);
    *out_length = source.length;
    esp32_mquickjs_release_byte_source(owned);
    return true;
}

static bool ble_advertise_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    static const char *const allowed[] = {
        "connectable", "scannable", "intervalMinMs", "intervalMaxMs",
        "durationMs", "data", "scanResponse", "capacity",
    };
    esp32_mquickjs_future_driver_state_t *state;
    ble_adapter_t *adapter;
    ble_advertiser_t *advertiser = &s_ble.advertiser;
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool connectable = false, scannable = false;
    bool present;
    double number;
    uint32_t raw;
    uint32_t capacity = CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS;
    JSValue queue;

    if (out_state == NULL || argc != 1 ||
        (adapter = ble_adapter_from_value(ctx, this_ref->val, true)) == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    *out_state = NULL;
    if (!adapter->role_peripheral) {
        JS_PopGCRef(ctx, &property_ref);
        ble_throw_error(ctx, "BLE_NOT_SUPPORTED", BLE_HS_ENOTSUP, -1, -1, -1);
        return false;
    }
    if (advertiser->allocated || adapter->scanner.active) {
        JS_PopGCRef(ctx, &property_ref);
        ble_throw_error(ctx, "BLE_GAP_CONFLICT", BLE_HS_EBUSY, -1, -1, -1);
        return false;
    }
    if (!ble_is_object(ctx, argv[0].val) ||
        !ble_validate_option_keys(ctx, argv[0].val, "BLEAdapter.advertise()",
                                  allowed, 8)) {
        JS_PopGCRef(ctx, &property_ref);
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "advertising options must be an object");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_ADVERTISE;
    state->adapter_generation = adapter->generation;
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    state->duration_ms = BLE_HS_FOREVER;
    atomic_init(&state->completed, false);
    if (!ble_get_bool(ctx, argv[0].val, "connectable", false, &connectable) ||
        !ble_get_bool(ctx, argv[0].val, "scannable", false, &scannable)) goto fail;
    *property = JS_GetPropertyStr(ctx, argv[0].val, "data");
    if (JS_IsException(*property) || JS_IsUndefined(*property) ||
        !ble_copy_small_byte_source(ctx, *property, "advertising data",
                                    state->adv_data, &state->adv_data_length)) goto fail;
    *property = JS_GetPropertyStr(ctx, argv[0].val, "scanResponse");
    if (JS_IsException(*property)) goto fail;
    if (!JS_IsUndefined(*property)) {
        if (!scannable) {
            JS_ThrowTypeError(ctx, "scanResponse requires scannable:true");
            goto fail;
        }
        if (!ble_copy_small_byte_source(ctx, *property, "scan response",
                                        state->scan_response,
                                        &state->scan_response_length)) goto fail;
    }
    *property = JS_GetPropertyStr(ctx, argv[0].val, "capacity");
    if (JS_IsException(*property)) goto fail;
    if (!JS_IsUndefined(*property)) {
        if (!ble_to_u32(ctx, *property, &raw) || raw == 0 ||
            raw > CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS) {
            JS_ThrowRangeError(ctx, "advertiser capacity exceeds maxConnections");
            goto fail;
        }
        capacity = raw;
    }
    if (!ble_get_number(ctx, argv[0].val, "intervalMinMs", &number, &present)) goto fail;
    if (present) {
        if (number < 20.0 || number > 10240.0) {
            JS_ThrowRangeError(ctx, "intervalMinMs must be in 20..10240");
            goto fail;
        }
        state->adv_params.itvl_min = ble_ms_to_625us(number);
    }
    if (!ble_get_number(ctx, argv[0].val, "intervalMaxMs", &number, &present)) goto fail;
    if (present) {
        if (number < 20.0 || number > 10240.0) {
            JS_ThrowRangeError(ctx, "intervalMaxMs must be in 20..10240");
            goto fail;
        }
        state->adv_params.itvl_max = ble_ms_to_625us(number);
    }
    if (state->adv_params.itvl_min != 0 && state->adv_params.itvl_max != 0 &&
        state->adv_params.itvl_min > state->adv_params.itvl_max) {
        JS_ThrowRangeError(ctx, "intervalMinMs must not exceed intervalMaxMs");
        goto fail;
    }
    *property = JS_GetPropertyStr(ctx, argv[0].val, "durationMs");
    if (JS_IsException(*property)) goto fail;
    if (!JS_IsUndefined(*property)) {
        if (!ble_to_u32(ctx, *property, &raw) || raw == 0 || raw > INT32_MAX) {
            JS_ThrowRangeError(ctx, "durationMs must be in 1..2147483647");
            goto fail;
        }
        state->duration_ms = raw;
        state->timeout_ms = raw + 1000U;
    }
    state->adv_params.conn_mode = connectable ? BLE_GAP_CONN_MODE_UND
                                               : BLE_GAP_CONN_MODE_NON;
    state->adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    memset(advertiser, 0, sizeof(*advertiser));
    advertiser->allocated = true;
    advertiser->open = true;
    advertiser->connectable = connectable;
    advertiser->scannable = scannable;
    advertiser->generation = ble_next_generation(&s_ble_next_advertiser_generation);
    advertiser->stop_reason = BLE_STOP_RUNNING;
    atomic_init(&advertiser->sequence, 0);
    atomic_init(&advertiser->incoming, 0);
    atomic_init(&advertiser->dropped, 0);
    queue = esp32_mquickjs_event_queue_new(
        ctx, adapter->runtime, sizeof(ble_advertiser_event_t), capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
        ble_advertiser_event_to_js, NULL, NULL, advertiser);
    if (JS_IsException(queue)) goto fail_advertiser;
    *JS_AddGCRef(ctx, &advertiser->queue_ref) = queue;
    advertiser->queue_rooted = true;
    advertiser->queue = esp32_mquickjs_event_queue_from_value(ctx, queue);
    if (advertiser->queue == NULL) goto fail_advertiser;
    state->connection_generation = advertiser->generation;
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    JS_PopGCRef(ctx, &property_ref);
    return true;
fail_advertiser:
    if (advertiser->queue_rooted) JS_DeleteGCRef(ctx, &advertiser->queue_ref);
    memset(advertiser, 0, sizeof(*advertiser));
fail:
    heap_caps_free(state);
    JS_PopGCRef(ctx, &property_ref);
    return false;
}

static bool ble_advertise_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    int rc;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    rc = ble_gap_adv_set_data(state->adv_data, state->adv_data_length);
    if (rc == 0 && state->scan_response_length > 0)
        rc = ble_gap_adv_rsp_set_data(state->scan_response,
                                      state->scan_response_length);
    if (rc == 0)
        rc = ble_gap_adv_start(s_ble.own_addr_type, NULL, state->duration_ms,
                               &state->adv_params, ble_gap_event_callback, NULL);
    if (rc != 0) {
        state->host_code = rc;
        s_ble.advertiser.stop_reason = BLE_STOP_ERROR;
        ble_throw_error(ctx, "BLE_GAP_CONFLICT", rc, -1, -1, -1);
        return false;
    }
    s_ble.advertiser.active = true;
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static JSValue ble_advertise_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    ble_advertiser_ref_t *ref;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BLE_ADVERTISER);
    if (JS_IsException(*object)) goto fail;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ref->adapter_generation = s_ble.generation;
    ref->generation = s_ble.advertiser.generation;
    JS_SetOpaque(ctx, *object, ref);
    if (!esp32_mquickjs_set_property_ref(ctx, object, "_eventQueue",
                                         s_ble.advertiser.queue_ref.val)) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
        goto fail;
    }
    state->transferred = true;
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static void ble_advertise_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL && !state->transferred &&
        state->connection_generation == s_ble.advertiser.generation) {
        if (s_ble.advertiser.active) (void)ble_gap_adv_stop();
        if (s_ble.advertiser.queue != NULL)
            (void)esp32_mquickjs_event_queue_close(s_ble.advertiser.queue);
        if (s_ble.advertiser.queue_rooted)
            JS_DeleteGCRef(state->ctx, &s_ble.advertiser.queue_ref);
        memset(&s_ble.advertiser, 0, sizeof(s_ble.advertiser));
    }
    ble_future_state_release(state);
}

static bool ble_advertiser_close_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    ble_advertiser_t *advertiser;
    (void)argv;
    if (out_state == NULL || argc != 0 ||
        (advertiser = ble_advertiser_from_value(ctx, this_ref->val, true)) == NULL)
        return false;
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_ADVERTISER_CLOSE;
    state->adapter_generation = s_ble.generation;
    state->connection_generation = advertiser->generation;
    state->timeout_ms = 2000;
    atomic_init(&state->completed, false);
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
}

static bool ble_advertiser_close_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    int rc = 0;
    (void)ctx;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (s_ble.advertiser.active) {
        rc = ble_gap_adv_stop();
        if (rc == BLE_HS_EALREADY) rc = 0;
    }
    state->host_code = rc;
    if (rc == 0) {
        s_ble.advertiser.active = false;
        s_ble.advertiser.stop_reason = BLE_STOP_CLOSED;
    } else {
        s_ble.advertiser.stop_reason = BLE_STOP_ERROR;
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static JSValue ble_advertiser_close_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->host_code != 0)
        return ble_bool_finish(ctx, state);
    ble_release_advertiser(&s_ble);
    return JS_TRUE;
}

static const esp32_mquickjs_future_driver_t s_ble_advertise_driver = {
    .capture = ble_advertise_capture,
    .start = ble_advertise_start,
    .poll = ble_future_poll,
    .finish = ble_advertise_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_advertise_destroy,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gap_resource_key,
};

static const esp32_mquickjs_future_driver_t s_ble_advertiser_close_driver = {
    .capture = ble_advertiser_close_capture,
    .start = ble_advertiser_close_start,
    .poll = ble_future_poll,
    .finish = ble_advertiser_close_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gap_resource_key,
};

static bool ble_parse_timeout_option(JSContext *ctx, JSValue options,
                                     const char *api_name,
                                     uint32_t *timeout_ms)
{
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    uint32_t raw;
    bool result = false;
    if (JS_IsUndefined(options)) {
        result = true;
        goto done;
    }
    if (!ble_is_object(ctx, options)) {
        JS_ThrowTypeError(ctx, "%s options must be an object", api_name);
        goto done;
    }
    *property = JS_GetPropertyStr(ctx, options, "timeoutMs");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!ble_to_u32(ctx, *property, &raw) || raw == 0 || raw > 60000) {
            JS_ThrowRangeError(ctx, "timeoutMs must be in 1..60000");
            goto done;
        }
        *timeout_ms = raw;
    }
    result = true;
done:
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static bool ble_connect_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    static const char *const allowed[] = {"timeoutMs", "preferredMtu", "autoPair"};
    esp32_mquickjs_future_driver_state_t *state;
    ble_adapter_t *adapter;
    ble_connection_slot_t *slot;
    uint16_t index;
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    JSValue options = argc == 2 ? argv[1].val : JS_UNDEFINED;
    uint32_t raw;

    if (out_state == NULL || argc < 1 || argc > 2 ||
        (adapter = ble_adapter_from_value(ctx, this_ref->val, true)) == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    *out_state = NULL;
    if (!adapter->role_central || adapter->scanner.active ||
        adapter->advertiser.active) {
        JS_PopGCRef(ctx, &property_ref);
        ble_throw_error(ctx, !adapter->role_central ? "BLE_NOT_SUPPORTED"
                                                    : "BLE_GAP_CONFLICT",
                        BLE_HS_EBUSY, -1, -1, -1);
        return false;
    }
    if (!JS_IsUndefined(options) &&
        (!ble_is_object(ctx, options) ||
         !ble_validate_option_keys(ctx, options, "BLEAdapter.connect()",
                                   allowed, 3))) {
        JS_PopGCRef(ctx, &property_ref);
        if (!JS_HasException(ctx))
            JS_ThrowTypeError(ctx, "connect options must be an object");
        return false;
    }
    slot = ble_reserve_connection(true, &index);
    if (slot == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        ble_throw_error(ctx, "BLE_QUEUE_FULL", BLE_HS_ENOMEM, -1, -1, -1);
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        slot->reserved = false;
        JS_PopGCRef(ctx, &property_ref);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_CONNECT;
    state->adapter_generation = adapter->generation;
    state->connection_index = index;
    state->connection_generation = slot->generation;
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    state->mtu = adapter->preferred_mtu;
    atomic_init(&state->completed, false);
    if (!ble_parse_address(ctx, argv[0].val, &state->peer)) goto fail;
    if (!JS_IsUndefined(options)) {
        if (!ble_parse_timeout_option(ctx, options, "connect", &state->timeout_ms) ||
            !ble_get_bool(ctx, options, "autoPair", false,
                          &state->auto_pair))
            goto fail;
        *property = JS_GetPropertyStr(ctx, options, "preferredMtu");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (!ble_to_u32(ctx, *property, &raw) || raw < 23 ||
                raw > CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU) {
                JS_ThrowRangeError(ctx, "preferredMtu is outside the compiled range");
                goto fail;
            }
            state->mtu = raw;
        }
    }
    slot->peer = state->peer;
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    JS_PopGCRef(ctx, &property_ref);
    return true;
fail:
    slot->reserved = false;
    heap_caps_free(state);
    JS_PopGCRef(ctx, &property_ref);
    return false;
}

static bool ble_connect_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot =
        &s_ble.connections[state->connection_index];
    int rc;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    ble_active_state_bind(&slot->active_gap_state, state);
    rc = ble_gap_connect(s_ble.own_addr_type, &state->peer,
                         state->timeout_ms, NULL, ble_gap_event_callback, slot);
    if (rc != 0) {
        ble_active_state_bind(&slot->active_gap_state, NULL);
        state->host_code = rc;
        s_ble.connections[state->connection_index].reserved = false;
        ble_throw_error(ctx, "BLE_CONNECTION_FAILED", rc, -1, -1, -1);
        return false;
    }
    return true;
}

static JSValue ble_connect_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot;
    if (state == NULL || state->connection_index >= s_ble.max_connections)
        return ble_throw_error(ctx, "BLE_CONNECTION_FAILED", BLE_HS_EINVAL,
                               -1, -1, -1);
    slot = &s_ble.connections[state->connection_index];
    if (state->host_code != 0 || !slot->open ||
        slot->generation != state->connection_generation) {
        return ble_throw_error(ctx, "BLE_CONNECTION_FAILED", state->host_code,
                               -1, state->connection_index, -1);
    }
    (void)ble_gap_set_event_cb(slot->conn_handle, ble_gap_event_callback, NULL);
    ble_active_state_bind(&slot->active_gap_state, NULL);
    state->transferred = true;
    return ble_new_connection_handle(ctx, state->connection_index, slot);
}

static void ble_connect_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL && !state->transferred &&
        state->connection_index < s_ble.max_connections) {
        ble_connection_slot_t *slot = &s_ble.connections[state->connection_index];
        if (slot->generation == state->connection_generation) {
            if (slot->open)
                (void)ble_gap_terminate(slot->conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
            slot->reserved = false;
            slot->allocated = false;
        }
    }
    ble_future_state_release(state);
}

static esp32_mquickjs_resource_key_t ble_gap_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    (void)state;
    return &s_ble_gap_lane_key;
}

static const esp32_mquickjs_future_driver_t s_ble_connect_driver = {
    .capture = ble_connect_capture,
    .start = ble_connect_start,
    .poll = ble_future_poll,
    .finish = ble_connect_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_connect_destroy,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gap_resource_key,
};

static bool ble_connection_operation_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state,
    ble_operation_t operation, uint32_t default_timeout)
{
    esp32_mquickjs_future_driver_state_t *state;
    ble_connection_ref_t *ref = NULL;
    ble_connection_slot_t *slot = ble_connection_from_value(
        ctx, this_ref->val, &ref, true);
    if (out_state == NULL || slot == NULL) return false;
    *out_state = NULL;
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = operation;
    state->adapter_generation = ref->adapter_generation;
    state->connection_index = ref->index;
    state->connection_generation = ref->generation;
    state->timeout_ms = default_timeout;
    atomic_init(&state->completed, false);
    if (argc > 1 ||
        (argc == 1 && !ble_parse_timeout_option(
                          ctx, argv[0].val, "BLE connection operation",
                          &state->timeout_ms))) {
        heap_caps_free(state);
        return false;
    }
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
}

static bool ble_connection_close_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    (void)argv;
    if (argc != 0) {
        JS_ThrowTypeError(ctx, "BLEConnection.close() takes no arguments");
        return false;
    }
    return ble_connection_operation_capture(ctx, this_ref, 0, argv, out_state,
                                            BLE_OP_CONNECTION_CLOSE, 5000);
}

static bool ble_connection_close_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot;
    int rc;
    if (state->connection_index >= s_ble.max_connections) return false;
    slot = &s_ble.connections[state->connection_index];
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    ble_gatt_state_bind(slot, state);
    if (!slot->open || slot->generation != state->connection_generation) {
        state->host_code = 0;
        atomic_store_explicit(&state->completed, true, memory_order_release);
        (void)esp32_mquickjs_future_wake(runtime, token);
        return true;
    }
    ble_active_state_bind(&slot->active_gap_state, state);
    (void)ble_gap_set_event_cb(slot->conn_handle, ble_gap_event_callback, slot);
    rc = ble_gap_terminate(slot->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc == BLE_HS_ENOTCONN) {
        slot->open = false;
        rc = 0;
        atomic_store_explicit(&state->completed, true, memory_order_release);
        (void)esp32_mquickjs_future_wake(runtime, token);
    }
    if (rc != 0) {
        ble_active_state_bind(&slot->active_gap_state, NULL);
        state->host_code = rc;
        ble_throw_error(ctx, "BLE_DISCONNECTED", rc, -1,
                        state->connection_index, -1);
        return false;
    }
    return true;
}

static JSValue ble_connection_close_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot;
    if (state == NULL || state->host_code != 0)
        return ble_bool_finish(ctx, state);
    slot = state->connection_index < s_ble.max_connections
               ? &s_ble.connections[state->connection_index] : NULL;
    if (slot != NULL && slot->generation == state->connection_generation) {
        (void)ble_gap_set_event_cb(slot->conn_handle, ble_gap_event_callback,
                                   NULL);
        ble_active_state_bind(&slot->active_gap_state, NULL);
        ble_active_state_bind(&slot->active_gatt_state, NULL);
        ble_recycle_connection(slot);
    }
    return JS_TRUE;
}

static const esp32_mquickjs_future_driver_t s_ble_connection_close_driver = {
    .capture = ble_connection_close_capture,
    .start = ble_connection_close_start,
    .poll = ble_future_poll,
    .finish = ble_connection_close_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gatt_resource_key,
};

static bool ble_pair_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    static const char *const allowed[] = {"timeoutMs"};
    if (argc == 1 && (!ble_is_object(ctx, argv[0].val) ||
        !ble_validate_option_keys(ctx, argv[0].val, "BLEConnection.pair()",
                                  allowed, 1))) return false;
    return ble_connection_operation_capture(ctx, this_ref, argc, argv,
                                            out_state, BLE_OP_PAIR,
                                            s_ble.pairing_timeout_ms);
}

static bool ble_pair_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot = &s_ble.connections[state->connection_index];
    int rc;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    ble_gatt_state_bind(slot, state);
    ble_active_state_bind(&slot->active_gap_state, state);
    (void)ble_gap_set_event_cb(slot->conn_handle, ble_gap_event_callback, slot);
    rc = ble_gap_security_initiate(slot->conn_handle);
    if (rc != 0) {
        (void)ble_gap_set_event_cb(slot->conn_handle, ble_gap_event_callback, NULL);
        ble_active_state_bind(&slot->active_gap_state, NULL);
        state->host_code = rc;
        ble_throw_error(ctx, "BLE_SECURITY_ERROR", rc, -1,
                        state->connection_index, -1);
        return false;
    }
    return true;
}

static JSValue ble_pair_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot = &s_ble.connections[state->connection_index];
    (void)ble_gap_set_event_cb(slot->conn_handle, ble_gap_event_callback, NULL);
    ble_active_state_bind(&slot->active_gap_state, NULL);
    if (state->host_code != 0)
        return ble_throw_error(ctx, "BLE_SECURITY_ERROR", state->host_code,
                               -1, state->connection_index, -1);
    return ble_security_to_js(ctx, &slot->security, &slot->peer);
}

static esp32_mquickjs_resource_key_t ble_gatt_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_future_driver_state_t *mutable_state =
        (esp32_mquickjs_future_driver_state_t *)state;
    if (state == NULL || state->connection_index >= s_ble.max_connections)
        return &s_ble_gap_lane_key;
    if (!mutable_state->gatt_accounted) {
        mutable_state->gatt_accounted = true;
        atomic_fetch_add_explicit(
            &s_ble.connections[state->connection_index].gatt_pending, 1,
            memory_order_relaxed);
    }
    return &s_ble.connections[state->connection_index].gatt_lane_key;
}

static const esp32_mquickjs_future_driver_t s_ble_pair_driver = {
    .capture = ble_pair_capture,
    .start = ble_pair_start,
    .poll = ble_future_poll,
    .finish = ble_pair_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gatt_resource_key,
};

static int ble_exchange_mtu_callback(uint16_t conn_handle,
                                     const struct ble_gatt_error *error,
                                     uint16_t mtu, void *arg)
{
    ble_connection_slot_t *slot = arg;
    esp32_mquickjs_future_driver_state_t *state =
        slot != NULL
            ? ble_active_state_acquire(&slot->active_gatt_state) : NULL;
    (void)conn_handle;
    if (state == NULL) return 0;
    state->host_code = error != NULL ? error->status : BLE_HS_EUNKNOWN;
    state->mtu = mtu;
    if (slot != NULL && state->host_code == 0) slot->mtu = mtu;
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    ble_future_state_drop(state);
    return 0;
}

static bool ble_exchange_mtu_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    ble_connection_ref_t *ref = NULL;
    uint32_t raw;
    if (out_state == NULL || argc > 2 ||
        ble_connection_from_value(ctx, this_ref->val, &ref, true) == NULL)
        return false;
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_EXCHANGE_MTU;
    state->adapter_generation = ref->adapter_generation;
    state->connection_index = ref->index;
    state->connection_generation = ref->generation;
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    state->mtu = s_ble.preferred_mtu;
    atomic_init(&state->completed, false);
    if (argc >= 1 && !JS_IsUndefined(argv[0].val)) {
        if (!ble_to_u32(ctx, argv[0].val, &raw) || raw < 23 ||
            raw > CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU) {
            JS_ThrowRangeError(ctx, "mtu is outside the compiled range");
            goto fail;
        }
        state->mtu = raw;
    }
    if (argc == 2 && !JS_IsUndefined(argv[1].val)) {
        if (!ble_to_u32(ctx, argv[1].val, &raw) || raw == 0 || raw > 60000) {
            JS_ThrowRangeError(ctx, "timeoutMs must be in 1..60000");
            goto fail;
        }
        state->timeout_ms = raw;
    }
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
fail:
    heap_caps_free(state);
    return false;
}

static bool ble_exchange_mtu_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot = &s_ble.connections[state->connection_index];
    int rc;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    ble_gatt_state_bind(slot, state);
    rc = ble_att_set_preferred_mtu(state->mtu);
    if (rc == 0)
        rc = ble_gattc_exchange_mtu(slot->conn_handle,
                                    ble_exchange_mtu_callback, slot);
    if (rc != 0) {
        ble_active_state_bind(&slot->active_gatt_state, NULL);
        state->host_code = rc;
        ble_throw_error(ctx, "BLE_GATT_ERROR", rc, -1,
                        state->connection_index, -1);
        return false;
    }
    return true;
}

static JSValue ble_exchange_mtu_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->host_code != 0)
        return ble_throw_error(ctx, "BLE_GATT_ERROR", state->host_code,
                               -1, state->connection_index, -1);
    return JS_NewUint32(ctx, state->mtu);
}

static const esp32_mquickjs_future_driver_t s_ble_exchange_mtu_driver = {
    .capture = ble_exchange_mtu_capture,
    .start = ble_exchange_mtu_start,
    .poll = ble_future_poll,
    .finish = ble_exchange_mtu_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gatt_resource_key,
};

static bool ble_read_rssi_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    ble_connection_ref_t *ref = NULL;
    uint32_t raw;
    if (out_state == NULL || argc > 1 ||
        ble_connection_from_value(ctx, this_ref->val, &ref, true) == NULL)
        return false;
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_READ_RSSI;
    state->adapter_generation = ref->adapter_generation;
    state->connection_index = ref->index;
    state->connection_generation = ref->generation;
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    atomic_init(&state->completed, false);
    if (argc == 1 && !JS_IsUndefined(argv[0].val)) {
        if (!ble_to_u32(ctx, argv[0].val, &raw) || raw == 0 || raw > 60000) {
            JS_ThrowRangeError(ctx, "timeoutMs must be in 1..60000");
            heap_caps_free(state);
            return false;
        }
        state->timeout_ms = raw;
    }
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
}

static bool ble_read_rssi_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot = &s_ble.connections[state->connection_index];
    int8_t rssi;
    int rc;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    ble_gatt_state_bind(slot, state);
    rc = ble_gap_conn_rssi(slot->conn_handle, &rssi);
    state->host_code = rc;
    state->result_count = rssi;
    if (rc != 0) {
        ble_throw_error(ctx, "BLE_DISCONNECTED", rc, -1,
                        state->connection_index, -1);
        return false;
    }
    slot->rssi = rssi;
    slot->rssi_valid = true;
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static JSValue ble_read_rssi_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->host_code != 0)
        return ble_throw_error(ctx, "BLE_DISCONNECTED", state->host_code,
                               -1, state->connection_index, -1);
    return JS_NewInt32(ctx, state->result_count);
}

static const esp32_mquickjs_future_driver_t s_ble_read_rssi_driver = {
    .capture = ble_read_rssi_capture,
    .start = ble_read_rssi_start,
    .poll = ble_future_poll,
    .finish = ble_read_rssi_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gatt_resource_key,
};

static void ble_uuid_to_text(const ble_uuid_any_t *uuid,
                             char output[BLE_UUID_TEXT_MAX])
{
    if (ble_uuid_to_str(&uuid->u, output) == NULL) output[0] = '\0';
}

static int ble_discover_descriptor_callback(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    uint16_t chr_val_handle, const struct ble_gatt_dsc *descriptor, void *arg);

static int ble_discover_start_descriptors(
    esp32_mquickjs_future_driver_state_t *state,
    ble_connection_slot_t *slot)
{
    ble_remote_characteristic_t *characteristic;
    ble_remote_service_t *service = NULL;
    uint16_t end_handle;
    uint16_t next_declaration_handle = 0;
    uint16_t service_index;
    if (state->attribute_index >= slot->characteristic_count) return BLE_HS_EDONE;
    characteristic = &slot->characteristics[state->attribute_index];
    for (service_index = 0; service_index < slot->service_count;
         ++service_index) {
        ble_remote_service_t *candidate = &slot->services[service_index];
        if (state->attribute_index >= candidate->first_characteristic &&
            state->attribute_index < candidate->first_characteristic +
                                         candidate->characteristic_count) {
            service = candidate;
            break;
        }
    }
    if (service == NULL) return BLE_HS_EDONE;
    if (state->attribute_index + 1U < slot->characteristic_count) {
        next_declaration_handle =
            slot->characteristics[state->attribute_index + 1U]
                .declaration_handle;
    }
    if (!esp32_mquickjs_wireless_gatt_descriptor_end(
            state->attribute_index, service->first_characteristic,
            service->characteristic_count, service->end_handle,
            next_declaration_handle, &end_handle)) return BLE_HS_EDONE;
    characteristic->first_descriptor = slot->descriptor_count;
    characteristic->descriptor_count = 0;
    if (characteristic->value_handle >= end_handle) return BLE_HS_EDONE;
    return ble_gattc_disc_all_dscs(
        slot->conn_handle, characteristic->value_handle, end_handle,
        ble_discover_descriptor_callback, slot);
}

static void ble_discover_complete(
    esp32_mquickjs_future_driver_state_t *state, int status)
{
    state->host_code = status == BLE_HS_EDONE ? 0 : status;
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(state->runtime, state->token);
}

static int ble_discover_descriptor_callback(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    uint16_t chr_val_handle, const struct ble_gatt_dsc *descriptor, void *arg)
{
    ble_connection_slot_t *slot = arg;
    esp32_mquickjs_future_driver_state_t *state =
        slot != NULL
            ? ble_active_state_acquire(&slot->active_gatt_state) : NULL;
    int result = 0;
    (void)conn_handle;
    (void)chr_val_handle;
    if (state == NULL || slot == NULL) return 0;
    if (error->status == 0 && descriptor != NULL) {
        if (slot->descriptor_count >= state->max_descriptors) {
            ble_discover_complete(state, BLE_HS_ENOMEM);
            result = BLE_HS_ENOMEM;
            goto done;
        }
        slot->descriptors[slot->descriptor_count].uuid = descriptor->uuid;
        slot->descriptors[slot->descriptor_count].handle = descriptor->handle;
        slot->descriptor_count++;
        slot->characteristics[state->attribute_index].descriptor_count++;
        goto done;
    }
    if (error->status == BLE_HS_EDONE) {
        int rc;
        state->attribute_index++;
        while (state->attribute_index < slot->characteristic_count) {
            rc = ble_discover_start_descriptors(state, slot);
            if (rc == 0) goto done;
            if (rc != BLE_HS_EDONE) {
                ble_discover_complete(state, rc);
                result = rc;
                goto done;
            }
            state->attribute_index++;
        }
        ble_discover_complete(state, 0);
        goto done;
    }
    ble_discover_complete(state, error->status);
done:
    ble_future_state_drop(state);
    return result;
}

static int ble_discover_characteristic_callback(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    const struct ble_gatt_chr *characteristic, void *arg)
{
    ble_connection_slot_t *slot = arg;
    esp32_mquickjs_future_driver_state_t *state =
        slot != NULL
            ? ble_active_state_acquire(&slot->active_gatt_state) : NULL;
    int result = 0;
    (void)conn_handle;
    if (state == NULL || slot == NULL) return 0;
    if (error->status == 0 && characteristic != NULL) {
        ble_remote_service_t *service =
            &slot->services[state->discover_service_index];
        ble_remote_characteristic_t *target;
        if (slot->characteristic_count >= state->max_characteristics) {
            ble_discover_complete(state, BLE_HS_ENOMEM);
            result = BLE_HS_ENOMEM;
            goto done;
        }
        target = &slot->characteristics[slot->characteristic_count++];
        target->uuid = characteristic->uuid;
        target->declaration_handle = characteristic->def_handle;
        target->value_handle = characteristic->val_handle;
        target->properties = characteristic->properties;
        service->characteristic_count++;
        goto done;
    }
    if (error->status == BLE_HS_EDONE) {
        int rc;
        state->discover_service_index++;
        while (state->discover_service_index < slot->service_count) {
            ble_remote_service_t *service =
                &slot->services[state->discover_service_index];
            service->first_characteristic = slot->characteristic_count;
            rc = ble_gattc_disc_all_chrs(
                slot->conn_handle, service->start_handle, service->end_handle,
                ble_discover_characteristic_callback, slot);
            if (rc == 0) goto done;
            if (rc != BLE_HS_EDONE) {
                ble_discover_complete(state, rc);
                result = rc;
                goto done;
            }
            state->discover_service_index++;
        }
        if (!state->include_descriptors || slot->characteristic_count == 0) {
            ble_discover_complete(state, 0);
            goto done;
        }
        state->attribute_index = 0;
        rc = ble_discover_start_descriptors(state, slot);
        if (rc == BLE_HS_EDONE) {
            state->attribute_index++;
            while (state->attribute_index < slot->characteristic_count &&
                   (rc = ble_discover_start_descriptors(state, slot)) == BLE_HS_EDONE)
                state->attribute_index++;
        }
        if (rc != 0) ble_discover_complete(state, rc == BLE_HS_EDONE ? 0 : rc);
        goto done;
    }
    ble_discover_complete(state, error->status);
done:
    ble_future_state_drop(state);
    return result;
}

static int ble_discover_service_callback(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    const struct ble_gatt_svc *service, void *arg)
{
    ble_connection_slot_t *slot = arg;
    esp32_mquickjs_future_driver_state_t *state =
        slot != NULL
            ? ble_active_state_acquire(&slot->active_gatt_state) : NULL;
    (void)conn_handle;
    if (state == NULL || slot == NULL) return 0;
    if (error->status == 0 && service != NULL) {
        ble_remote_service_t *target;
        if (slot->service_count >= state->max_services) {
            ble_discover_complete(state, BLE_HS_ENOMEM);
            ble_future_state_drop(state);
            return BLE_HS_ENOMEM;
        }
        target = &slot->services[slot->service_count++];
        target->uuid = service->uuid;
        target->start_handle = service->start_handle;
        target->end_handle = service->end_handle;
        ble_future_state_drop(state);
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (slot->service_count == 0) {
            ble_discover_complete(state, 0);
            ble_future_state_drop(state);
            return 0;
        }
        state->discover_service_index = 0;
        slot->services[0].first_characteristic = 0;
        state->host_code = ble_gattc_disc_all_chrs(
            slot->conn_handle, slot->services[0].start_handle,
            slot->services[0].end_handle,
            ble_discover_characteristic_callback, slot);
        if (state->host_code != 0)
            ble_discover_complete(state, state->host_code);
        ble_future_state_drop(state);
        return 0;
    }
    ble_discover_complete(state, error->status);
    ble_future_state_drop(state);
    return 0;
}

static bool ble_discover_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    static const char *const allowed[] = {
        "includeDescriptors", "maxServices", "maxCharacteristics",
        "maxDescriptors", "timeoutMs",
    };
    esp32_mquickjs_future_driver_state_t *state;
    ble_connection_ref_t *ref = NULL;
    ble_connection_slot_t *slot;
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    JSValue options = argc == 1 ? argv[0].val : JS_UNDEFINED;
    uint32_t raw;
    if (out_state == NULL || argc > 1 ||
        (slot = ble_connection_from_value(ctx, this_ref->val, &ref, true)) == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    if (!JS_IsUndefined(options) &&
        (!ble_is_object(ctx, options) ||
         !ble_validate_option_keys(ctx, options, "BLEConnection.discover()",
                                   allowed, 5))) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_DISCOVER;
    state->adapter_generation = ref->adapter_generation;
    state->connection_index = ref->index;
    state->connection_generation = ref->generation;
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    state->max_services = CONFIG_ESP32_MQUICKJS_BLE_MAX_SERVICES;
    state->max_characteristics = CONFIG_ESP32_MQUICKJS_BLE_MAX_CHARACTERISTICS;
    state->max_descriptors = CONFIG_ESP32_MQUICKJS_BLE_MAX_DESCRIPTORS;
    atomic_init(&state->completed, false);
    if (!JS_IsUndefined(options)) {
        if (!ble_get_bool(ctx, options, "includeDescriptors", false,
                          &state->include_descriptors) ||
            !ble_parse_timeout_option(ctx, options, "discover",
                                      &state->timeout_ms)) goto fail;
#define BLE_PARSE_DISCOVERY_LIMIT(name, field, maximum)                         \
        *property = JS_GetPropertyStr(ctx, options, name);                      \
        if (JS_IsException(*property)) goto fail;                               \
        if (!JS_IsUndefined(*property)) {                                       \
            if (!ble_to_u32(ctx, *property, &raw) || raw == 0 ||                \
                raw > (maximum)) {                                              \
                JS_ThrowRangeError(ctx, name " exceeds the compiled limit");   \
                goto fail;                                                      \
            }                                                                   \
            state->field = raw;                                                 \
        }
        BLE_PARSE_DISCOVERY_LIMIT("maxServices", max_services,
                                  CONFIG_ESP32_MQUICKJS_BLE_MAX_SERVICES)
        BLE_PARSE_DISCOVERY_LIMIT("maxCharacteristics", max_characteristics,
                                  CONFIG_ESP32_MQUICKJS_BLE_MAX_CHARACTERISTICS)
        BLE_PARSE_DISCOVERY_LIMIT("maxDescriptors", max_descriptors,
                                  CONFIG_ESP32_MQUICKJS_BLE_MAX_DESCRIPTORS)
#undef BLE_PARSE_DISCOVERY_LIMIT
    }
    ble_clear_remote_discovery(slot);
    slot->services = heap_caps_calloc(state->max_services,
                                      sizeof(*slot->services), MALLOC_CAP_8BIT);
    slot->characteristics = heap_caps_calloc(
        state->max_characteristics, sizeof(*slot->characteristics), MALLOC_CAP_8BIT);
    if (state->include_descriptors)
        slot->descriptors = heap_caps_calloc(state->max_descriptors,
                                             sizeof(*slot->descriptors), MALLOC_CAP_8BIT);
    if (slot->services == NULL || slot->characteristics == NULL ||
        (state->include_descriptors && slot->descriptors == NULL)) {
        JS_ThrowOutOfMemory(ctx);
        ble_clear_remote_discovery(slot);
        goto fail;
    }
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    JS_PopGCRef(ctx, &property_ref);
    return true;
fail:
    heap_caps_free(state);
    JS_PopGCRef(ctx, &property_ref);
    return false;
}

static bool ble_discover_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot = &s_ble.connections[state->connection_index];
    int rc;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    ble_gatt_state_bind(slot, state);
    rc = ble_gattc_disc_all_svcs(slot->conn_handle,
                                 ble_discover_service_callback, slot);
    if (rc != 0) {
        ble_active_state_bind(&slot->active_gatt_state, NULL);
        state->host_code = rc;
        ble_throw_error(ctx, "BLE_GATT_ERROR", rc, -1,
                        state->connection_index, -1);
        return false;
    }
    return true;
}

static JSValue ble_new_service_handle(JSContext *ctx,
                                      uint16_t connection_index,
                                      uint16_t service_index)
{
    ble_connection_slot_t *slot = &s_ble.connections[connection_index];
    ble_remote_service_t *service = &slot->services[service_index];
    ble_attribute_ref_t *ref;
    char uuid[BLE_UUID_TEXT_MAX];
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BLE_SERVICE);
    if (JS_IsException(*object)) goto fail;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ref->adapter_generation = s_ble.generation;
    ref->connection_index = connection_index;
    ref->connection_generation = slot->generation;
    ref->discovery_generation = slot->discovery_generation;
    ref->index = service_index;
    JS_SetOpaque(ctx, *object, ref);
    ble_uuid_to_text(&service->uuid, uuid);
    if (!esp32_mquickjs_set_property_ref(ctx, object, "uuid",
                                         JS_NewString(ctx, uuid)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "startHandle",
                                         JS_NewUint32(ctx, service->start_handle)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "endHandle",
                                         JS_NewUint32(ctx, service->end_handle))) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
        goto fail;
    }
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static JSValue ble_discover_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot = &s_ble.connections[state->connection_index];
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    uint16_t index;
    if (state->host_code != 0) {
        JS_PopGCRef(ctx, &item_ref);
        JS_PopGCRef(ctx, &array_ref);
        return ble_throw_error(ctx,
            state->host_code == BLE_HS_ENOMEM ? "BLE_SERVER_LIMIT"
                                               : "BLE_GATT_ERROR",
            state->host_code, -1, state->connection_index, -1);
    }
    *array = JS_NewArray(ctx, slot->service_count);
    for (index = 0; !JS_IsException(*array) && index < slot->service_count;
         ++index) {
        *item = ble_new_service_handle(ctx, state->connection_index, index);
        if (JS_IsException(*item) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *array, index, *item))) {
            *array = JS_EXCEPTION;
            break;
        }
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);
}

static const esp32_mquickjs_future_driver_t s_ble_discover_driver = {
    .capture = ble_discover_capture,
    .start = ble_discover_start,
    .poll = ble_future_poll,
    .finish = ble_discover_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gatt_resource_key,
};

static ble_connection_slot_t *ble_attribute_from_value(
    JSContext *ctx, JSValue value, int expected_class,
    ble_attribute_ref_t **out_ref, bool throw_if_stale)
{
    ble_attribute_ref_t *ref;
    ble_connection_slot_t *slot = NULL;
    if (JS_GetClassID(ctx, value) != expected_class ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "expected a BLE attribute handle");
        return NULL;
    }
    if (ref->adapter_generation == s_ble.generation &&
        ref->connection_index < s_ble.max_connections) {
        slot = &s_ble.connections[ref->connection_index];
        if (!slot->allocated || !slot->open ||
            slot->generation != ref->connection_generation ||
            slot->discovery_generation != ref->discovery_generation) slot = NULL;
    }
    if (slot == NULL && throw_if_stale)
        JS_ThrowReferenceError(ctx, "BLE_STALE_ATTRIBUTE: attribute is stale");
    if (out_ref != NULL) *out_ref = ref;
    return slot;
}

static uint16_t ble_attribute_handle(ble_connection_slot_t *slot,
                                     ble_attribute_ref_t *ref,
                                     int class_id)
{
    if (class_id == JS_CLASS_BLE_CHARACTERISTIC &&
        ref->index < slot->characteristic_count)
        return slot->characteristics[ref->index].value_handle;
    if (class_id == JS_CLASS_BLE_DESCRIPTOR &&
        ref->index < slot->descriptor_count)
        return slot->descriptors[ref->index].handle;
    return 0;
}

static int ble_gatt_attr_callback(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attribute, void *arg)
{
    ble_connection_slot_t *slot = arg;
    esp32_mquickjs_future_driver_state_t *state =
        slot != NULL
            ? ble_active_state_acquire(&slot->active_gatt_state) : NULL;
    uint16_t length = 0;
    (void)conn_handle;
    if (state == NULL) return 0;
    state->host_code = error != NULL ? error->status : BLE_HS_EUNKNOWN;
    state->att_code = error != NULL ? error->att_handle : 0;
    if (state->host_code == 0 && state->operation == BLE_OP_GATT_READ &&
        attribute != NULL && attribute->om != NULL) {
        length = OS_MBUF_PKTLEN(attribute->om);
        if (length > state->max_bytes) {
            state->host_code = BLE_HS_EMSGSIZE;
        } else if (os_mbuf_copydata(attribute->om, 0, length,
                                    state->payload) != 0) {
            state->host_code = BLE_HS_EUNKNOWN;
        } else {
            state->payload_length = length;
        }
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    ble_future_state_drop(state);
    return 0;
}

static bool ble_gatt_read_capture_common(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state, int class_id)
{
    static const char *const allowed[] = {"timeoutMs", "maxBytes"};
    esp32_mquickjs_future_driver_state_t *state;
    ble_attribute_ref_t *ref = NULL;
    ble_connection_slot_t *slot;
    JSValue options = argc == 1 ? argv[0].val : JS_UNDEFINED;
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    uint32_t raw;
    if (out_state == NULL || argc > 1 ||
        (slot = ble_attribute_from_value(ctx, this_ref->val, class_id,
                                         &ref, true)) == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    if (!JS_IsUndefined(options) &&
        (!ble_is_object(ctx, options) ||
         !ble_validate_option_keys(ctx, options, "BLE GATT read", allowed, 2))) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_GATT_READ;
    state->adapter_generation = ref->adapter_generation;
    state->connection_index = ref->connection_index;
    state->connection_generation = ref->connection_generation;
    state->attribute_index = ref->index;
    state->attribute_handle = ble_attribute_handle(slot, ref, class_id);
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    state->max_bytes = CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES;
    atomic_init(&state->completed, false);
    if (state->attribute_handle == 0) {
        ble_throw_error(ctx, "BLE_STALE_ATTRIBUTE", BLE_HS_EINVAL, -1,
                        ref->connection_index, -1);
        goto fail;
    }
    if (!JS_IsUndefined(options)) {
        if (!ble_parse_timeout_option(ctx, options, "GATT read",
                                      &state->timeout_ms)) goto fail;
        *property = JS_GetPropertyStr(ctx, options, "maxBytes");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            if (!ble_to_u32(ctx, *property, &raw) || raw == 0 ||
                raw > CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES) {
                JS_ThrowRangeError(ctx, "maxBytes exceeds the compiled limit");
                goto fail;
            }
            state->max_bytes = raw;
        }
    }
    state->payload = heap_caps_malloc(state->max_bytes, MALLOC_CAP_8BIT);
    if (state->payload == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    JS_PopGCRef(ctx, &property_ref);
    return true;
fail:
    heap_caps_free(state->payload);
    heap_caps_free(state);
    JS_PopGCRef(ctx, &property_ref);
    return false;
}

static bool ble_characteristic_read_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return ble_gatt_read_capture_common(ctx, this_ref, argc, argv, out_state,
                                       JS_CLASS_BLE_CHARACTERISTIC);
}

static bool ble_descriptor_read_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return ble_gatt_read_capture_common(ctx, this_ref, argc, argv, out_state,
                                       JS_CLASS_BLE_DESCRIPTOR);
}

static bool ble_gatt_read_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot = &s_ble.connections[state->connection_index];
    int rc;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    ble_gatt_state_bind(slot, state);
    rc = ble_gattc_read(slot->conn_handle, state->attribute_handle,
                        ble_gatt_attr_callback, slot);
    if (rc != 0) {
        ble_active_state_bind(&slot->active_gatt_state, NULL);
        state->host_code = rc;
        ble_throw_error(ctx, "BLE_GATT_ERROR", rc, -1,
                        state->connection_index, state->attribute_handle);
        return false;
    }
    return true;
}

static JSValue ble_gatt_read_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    uint8_t *payload;
    JSValue result;
    if (state->host_code != 0) {
        return ble_throw_error(ctx,
            state->host_code == BLE_HS_EMSGSIZE ? "BLE_PAYLOAD_TOO_LARGE"
                                                : "BLE_GATT_ERROR",
            state->host_code, state->att_code, state->connection_index,
            state->attribute_handle);
    }
    payload = state->payload;
    state->payload = NULL;
    result = esp32_mquickjs_new_owned_byte_view(ctx, payload,
                                                state->payload_length);
    if (JS_IsException(result)) heap_caps_free(payload);
    return result;
}

static bool ble_gatt_write_capture_common(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state, int class_id)
{
    static const char *const allowed[] = {"response", "timeoutMs"};
    esp32_mquickjs_future_driver_state_t *state;
    ble_attribute_ref_t *ref = NULL;
    ble_connection_slot_t *slot;
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue error = JS_UNDEFINED;
    JSValue options = argc == 2 ? argv[1].val : JS_UNDEFINED;
    if (out_state == NULL || argc < 1 || argc > 2 ||
        (slot = ble_attribute_from_value(ctx, this_ref->val, class_id,
                                         &ref, true)) == NULL) return false;
    if (!JS_IsUndefined(options) &&
        (!ble_is_object(ctx, options) ||
         !ble_validate_option_keys(ctx, options, "BLE GATT write", allowed, 2)))
        return false;
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_GATT_WRITE;
    state->adapter_generation = ref->adapter_generation;
    state->connection_index = ref->connection_index;
    state->connection_generation = ref->connection_generation;
    state->attribute_index = ref->index;
    state->attribute_handle = ble_attribute_handle(slot, ref, class_id);
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    state->response = true;
    atomic_init(&state->completed, false);
    if (!JS_IsUndefined(options) &&
        (!ble_get_bool(ctx, options, "response", true, &state->response) ||
         !ble_parse_timeout_option(ctx, options, "GATT write",
                                   &state->timeout_ms))) goto fail;
    if (!esp32_mquickjs_get_byte_source(ctx, argv[0].val, "BLE GATT write",
                                        &source, &owned, &error)) goto fail;
    if (source.length > CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES) {
        esp32_mquickjs_release_byte_source(owned);
        owned = NULL;
        ble_throw_error(ctx, "BLE_PAYLOAD_TOO_LARGE", BLE_HS_EMSGSIZE,
                        -1, state->connection_index, state->attribute_handle);
        goto fail;
    }
    state->payload = heap_caps_malloc(source.length > 0 ? source.length : 1,
                                      MALLOC_CAP_8BIT);
    if (state->payload == NULL) {
        esp32_mquickjs_release_byte_source(owned);
        owned = NULL;
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    state->payload_length = source.length;
    if (source.length > 0) memcpy(state->payload, source.data, source.length);
    esp32_mquickjs_release_byte_source(owned);
    owned = NULL;
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
fail:
    esp32_mquickjs_release_byte_source(owned);
    heap_caps_free(state->payload);
    heap_caps_free(state);
    return false;
}

static bool ble_characteristic_write_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return ble_gatt_write_capture_common(ctx, this_ref, argc, argv, out_state,
                                        JS_CLASS_BLE_CHARACTERISTIC);
}

static bool ble_descriptor_write_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return ble_gatt_write_capture_common(ctx, this_ref, argc, argv, out_state,
                                        JS_CLASS_BLE_DESCRIPTOR);
}

static bool ble_gatt_write_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot = &s_ble.connections[state->connection_index];
    int rc;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    ble_gatt_state_bind(slot, state);
    if (state->response) {
        rc = ble_gattc_write_flat(slot->conn_handle, state->attribute_handle,
                                  state->payload, state->payload_length,
                                  ble_gatt_attr_callback, slot);
    } else {
        rc = ble_gattc_write_no_rsp_flat(slot->conn_handle,
                                         state->attribute_handle,
                                         state->payload,
                                         state->payload_length);
        if (rc == 0) {
            state->host_code = 0;
            atomic_store_explicit(&state->completed, true, memory_order_release);
            (void)esp32_mquickjs_future_wake(runtime, token);
        }
    }
    if (rc != 0) {
        ble_active_state_bind(&slot->active_gatt_state, NULL);
        state->host_code = rc;
        ble_throw_error(ctx, "BLE_GATT_ERROR", rc, -1,
                        state->connection_index, state->attribute_handle);
        return false;
    }
    return true;
}

static JSValue ble_gatt_write_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->host_code != 0)
        return ble_throw_error(ctx, "BLE_GATT_ERROR", state->host_code,
                               state->att_code, state->connection_index,
                               state->attribute_handle);
    return JS_NewInt64(ctx, state->payload_length);
}

static const esp32_mquickjs_future_driver_t s_ble_gatt_read_driver = {
    .capture = ble_characteristic_read_capture,
    .start = ble_gatt_read_start,
    .poll = ble_future_poll,
    .finish = ble_gatt_read_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gatt_resource_key,
};

static const esp32_mquickjs_future_driver_t s_ble_descriptor_read_driver = {
    .capture = ble_descriptor_read_capture,
    .start = ble_gatt_read_start,
    .poll = ble_future_poll,
    .finish = ble_gatt_read_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gatt_resource_key,
};

static const esp32_mquickjs_future_driver_t s_ble_gatt_write_driver = {
    .capture = ble_characteristic_write_capture,
    .start = ble_gatt_write_start,
    .poll = ble_future_poll,
    .finish = ble_gatt_write_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gatt_resource_key,
};

static const esp32_mquickjs_future_driver_t s_ble_descriptor_write_driver = {
    .capture = ble_descriptor_write_capture,
    .start = ble_gatt_write_start,
    .poll = ble_future_poll,
    .finish = ble_gatt_write_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gatt_resource_key,
};

static uint16_t ble_find_cccd(ble_connection_slot_t *slot,
                              ble_remote_characteristic_t *characteristic)
{
    uint16_t offset;
    for (offset = 0; offset < characteristic->descriptor_count; ++offset) {
        ble_remote_descriptor_t *descriptor =
            &slot->descriptors[characteristic->first_descriptor + offset];
        if (ble_uuid_u16(&descriptor->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16)
            return descriptor->handle;
    }
    return characteristic->value_handle + 1U;
}

static bool ble_subscribe_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    static const char *const allowed[] = {"mode", "capacity", "timeoutMs"};
    esp32_mquickjs_future_driver_state_t *state;
    ble_attribute_ref_t *ref = NULL;
    ble_connection_slot_t *slot;
    ble_remote_characteristic_t *characteristic;
    ble_subscription_t *subscription = NULL;
    JSValue options = argc == 1 ? argv[0].val : JS_UNDEFINED;
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    uint32_t capacity = CONFIG_ESP32_MQUICKJS_BLE_NOTIFICATION_QUEUE_LEN;
    uint32_t raw;
    uint16_t index;
    bool indication;
    JSValue queue;

    if (out_state == NULL || argc > 1 ||
        (slot = ble_attribute_from_value(ctx, this_ref->val,
                                         JS_CLASS_BLE_CHARACTERISTIC,
                                         &ref, true)) == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    characteristic = ref->index < slot->characteristic_count
                         ? &slot->characteristics[ref->index] : NULL;
    if (characteristic == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        ble_throw_error(ctx, "BLE_STALE_ATTRIBUTE", BLE_HS_EINVAL, -1,
                        ref->connection_index, -1);
        return false;
    }
    indication = (characteristic->properties & BLE_GATT_CHR_PROP_NOTIFY) == 0 &&
                 (characteristic->properties & BLE_GATT_CHR_PROP_INDICATE) != 0;
    if ((characteristic->properties & (BLE_GATT_CHR_PROP_NOTIFY |
                                       BLE_GATT_CHR_PROP_INDICATE)) == 0) {
        JS_PopGCRef(ctx, &property_ref);
        ble_throw_error(ctx, "BLE_NOT_SUPPORTED", BLE_HS_ENOTSUP, -1,
                        ref->connection_index, characteristic->value_handle);
        return false;
    }
    if (!JS_IsUndefined(options)) {
        if (!ble_is_object(ctx, options) ||
            !ble_validate_option_keys(ctx, options, "subscribe", allowed, 3)) {
            JS_PopGCRef(ctx, &property_ref);
            return false;
        }
        *property = JS_GetPropertyStr(ctx, options, "mode");
        if (JS_IsException(*property)) goto fail_early;
        if (!JS_IsUndefined(*property)) {
            if (ble_string_equals(ctx, *property, "notify")) indication = false;
            else if (ble_string_equals(ctx, *property, "indicate")) indication = true;
            else if (!ble_string_equals(ctx, *property, "auto")) {
                JS_ThrowTypeError(ctx, "subscription mode must be notify, indicate, or auto");
                goto fail_early;
            }
        }
        if ((!indication && !(characteristic->properties & BLE_GATT_CHR_PROP_NOTIFY)) ||
            (indication && !(characteristic->properties & BLE_GATT_CHR_PROP_INDICATE))) {
            ble_throw_error(ctx, "BLE_NOT_SUPPORTED", BLE_HS_ENOTSUP, -1,
                            ref->connection_index, characteristic->value_handle);
            goto fail_early;
        }
        *property = JS_GetPropertyStr(ctx, options, "capacity");
        if (JS_IsException(*property)) goto fail_early;
        if (!JS_IsUndefined(*property)) {
            if (!ble_to_u32(ctx, *property, &raw) || raw == 0 || raw > 32) {
                JS_ThrowRangeError(ctx, "subscription capacity must be in 1..32");
                goto fail_early;
            }
            capacity = raw;
        }
    }
    for (index = 0; index < BLE_MAX_SUBSCRIPTIONS; ++index) {
        if (!slot->subscriptions[index].allocated) {
            subscription = &slot->subscriptions[index];
            break;
        }
    }
    if (subscription == NULL) {
        ble_throw_error(ctx, "BLE_QUEUE_FULL", BLE_HS_ENOMEM, -1,
                        ref->connection_index, characteristic->value_handle);
        goto fail_early;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail_early;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_SUBSCRIBE;
    state->adapter_generation = ref->adapter_generation;
    state->connection_index = ref->connection_index;
    state->connection_generation = ref->connection_generation;
    state->attribute_index = ref->index;
    state->attribute_handle = characteristic->value_handle;
    state->cccd_handle = ble_find_cccd(slot, characteristic);
    state->subscription_index = index;
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    state->indication = indication;
    state->payload_length = 2;
    state->payload = heap_caps_calloc(1, 2, MALLOC_CAP_8BIT);
    atomic_init(&state->completed, false);
    if (state->payload == NULL) {
        JS_ThrowOutOfMemory(ctx);
        heap_caps_free(state);
        goto fail_early;
    }
    if (!JS_IsUndefined(options) &&
        !ble_parse_timeout_option(ctx, options, "subscribe", &state->timeout_ms))
        goto fail_state;
    state->payload[0] = indication ? 2U : 1U;
    memset(subscription, 0, sizeof(*subscription));
    subscription->allocated = true;
    subscription->generation = ble_next_generation(&s_ble_next_subscription_generation);
    subscription->value_handle = characteristic->value_handle;
    subscription->cccd_handle = state->cccd_handle;
    subscription->indication = indication;
    subscription->capacity = capacity;
    subscription->payloads = heap_caps_calloc(
        capacity, CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES,
        MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    subscription->lengths = heap_caps_calloc(capacity, sizeof(uint16_t),
                                              MALLOC_CAP_8BIT);
    atomic_init(&subscription->sequence, 0);
    atomic_init(&subscription->received, 0);
    atomic_init(&subscription->dropped, 0);
    if (!esp32_mquickjs_wireless_pool_init(&subscription->free_slots,
                                            capacity) ||
        subscription->payloads == NULL ||
        subscription->lengths == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail_subscription;
    }
    queue = esp32_mquickjs_event_queue_new(
        ctx, s_ble.runtime, sizeof(ble_notification_event_t), capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
        ble_notification_event_to_js, ble_notification_event_drop,
        NULL, subscription);
    if (JS_IsException(queue)) goto fail_subscription;
    *JS_AddGCRef(ctx, &subscription->queue_ref) = queue;
    subscription->queue_rooted = true;
    subscription->queue = esp32_mquickjs_event_queue_from_value(ctx, queue);
    if (subscription->queue == NULL) goto fail_subscription;
    state->subscription_generation = subscription->generation;
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    JS_PopGCRef(ctx, &property_ref);
    return true;
fail_subscription:
    if (subscription->queue_rooted)
        JS_DeleteGCRef(ctx, &subscription->queue_ref);
    heap_caps_free(subscription->payloads);
    heap_caps_free(subscription->lengths);
    memset(subscription, 0, sizeof(*subscription));
fail_state:
    heap_caps_free(state->payload);
    heap_caps_free(state);
fail_early:
    JS_PopGCRef(ctx, &property_ref);
    return false;
}

static bool ble_subscribe_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot = &s_ble.connections[state->connection_index];
    int rc;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    ble_gatt_state_bind(slot, state);
    rc = ble_gattc_write_flat(slot->conn_handle, state->cccd_handle,
                              state->payload, state->payload_length,
                              ble_gatt_attr_callback, slot);
    if (rc != 0) {
        ble_active_state_bind(&slot->active_gatt_state, NULL);
        state->host_code = rc;
        ble_throw_error(ctx, "BLE_GATT_ERROR", rc, -1,
                        state->connection_index, state->cccd_handle);
        return false;
    }
    return true;
}

static JSValue ble_subscribe_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    ble_connection_slot_t *slot = &s_ble.connections[state->connection_index];
    ble_subscription_t *subscription =
        &slot->subscriptions[state->subscription_index];
    ble_subscription_ref_t *ref;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    if (state->host_code != 0) {
        JS_PopGCRef(ctx, &object_ref);
        return ble_throw_error(ctx, "BLE_GATT_ERROR", state->host_code,
                               state->att_code, state->connection_index,
                               state->cccd_handle);
    }
    subscription->open = true;
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BLE_NOTIFICATION_STREAM);
    if (JS_IsException(*object)) goto fail;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ref->adapter_generation = s_ble.generation;
    ref->connection_index = state->connection_index;
    ref->connection_generation = state->connection_generation;
    ref->subscription_index = state->subscription_index;
    ref->subscription_generation = subscription->generation;
    JS_SetOpaque(ctx, *object, ref);
    if (!esp32_mquickjs_set_property_ref(ctx, object, "_eventQueue",
                                         subscription->queue_ref.val)) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
        goto fail;
    }
    state->transferred = true;
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

static void ble_subscribe_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL && !state->transferred &&
        state->connection_index < s_ble.max_connections &&
        state->subscription_index < BLE_MAX_SUBSCRIPTIONS) {
        ble_subscription_t *subscription =
            &s_ble.connections[state->connection_index]
                 .subscriptions[state->subscription_index];
        if (subscription->generation == state->subscription_generation)
            ble_close_subscription(subscription);
    }
    ble_future_state_release(state);
}

static ble_subscription_t *ble_subscription_from_value(
    JSContext *ctx, JSValue value, ble_subscription_ref_t **out_ref,
    bool throw_if_stale)
{
    ble_subscription_ref_t *ref;
    ble_subscription_t *subscription = NULL;
    if (JS_GetClassID(ctx, value) != JS_CLASS_BLE_NOTIFICATION_STREAM ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "expected a BLENotificationStream instance");
        return NULL;
    }
    if (ref->adapter_generation == s_ble.generation &&
        ref->connection_index < s_ble.max_connections &&
        ref->subscription_index < BLE_MAX_SUBSCRIPTIONS) {
        ble_connection_slot_t *slot = &s_ble.connections[ref->connection_index];
        if (slot->generation == ref->connection_generation) {
            subscription = &slot->subscriptions[ref->subscription_index];
            if (!subscription->allocated ||
                subscription->generation != ref->subscription_generation)
                subscription = NULL;
        }
    }
    if (subscription == NULL && throw_if_stale)
        JS_ThrowReferenceError(ctx,
            "BLE_STALE_SUBSCRIPTION: subscription is closed");
    if (out_ref != NULL) *out_ref = ref;
    return subscription;
}

static bool ble_subscription_close_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    ble_subscription_ref_t *ref = NULL;
    ble_subscription_t *subscription;
    (void)argv;
    if (out_state == NULL || argc != 0 ||
        (subscription = ble_subscription_from_value(
             ctx, this_ref->val, &ref, true)) == NULL) return false;
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_SUBSCRIPTION_CLOSE;
    state->adapter_generation = ref->adapter_generation;
    state->connection_index = ref->connection_index;
    state->connection_generation = ref->connection_generation;
    state->subscription_index = ref->subscription_index;
    state->subscription_generation = ref->subscription_generation;
    state->attribute_handle = subscription->value_handle;
    state->cccd_handle = subscription->cccd_handle;
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    state->payload_length = 2;
    state->payload = heap_caps_calloc(1, 2, MALLOC_CAP_8BIT);
    atomic_init(&state->completed, false);
    if (state->payload == NULL) {
        heap_caps_free(state);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
}

static JSValue ble_subscription_close_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->host_code != 0)
        return ble_throw_error(ctx, "BLE_GATT_ERROR", state->host_code,
                               state->att_code, state->connection_index,
                               state->cccd_handle);
    if (state->connection_index < s_ble.max_connections &&
        state->subscription_index < BLE_MAX_SUBSCRIPTIONS) {
        ble_subscription_t *subscription =
            &s_ble.connections[state->connection_index]
                 .subscriptions[state->subscription_index];
        if (subscription->generation == state->subscription_generation)
            ble_close_subscription(subscription);
    }
    return JS_TRUE;
}

static const esp32_mquickjs_future_driver_t s_ble_subscribe_driver = {
    .capture = ble_subscribe_capture,
    .start = ble_subscribe_start,
    .poll = ble_future_poll,
    .finish = ble_subscribe_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_subscribe_destroy,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gatt_resource_key,
};

static const esp32_mquickjs_future_driver_t s_ble_subscription_close_driver = {
    .capture = ble_subscription_close_capture,
    .start = ble_subscribe_start,
    .poll = ble_future_poll,
    .finish = ble_subscription_close_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_gatt_resource_key,
};

static bool ble_bond_operation_capture_common(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state,
    ble_operation_t operation)
{
    esp32_mquickjs_future_driver_state_t *state;
    if (out_state == NULL || ble_adapter_from_value(ctx, this_ref->val, true) == NULL)
        return false;
    if ((operation == BLE_OP_REMOVE_BOND && argc != 1) ||
        (operation == BLE_OP_CLEAR_BONDS && argc != 0)) {
        JS_ThrowTypeError(ctx, "invalid BLE bond operation arguments");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = operation;
    state->adapter_generation = s_ble.generation;
    state->timeout_ms = 5000;
    atomic_init(&state->completed, false);
    if (operation == BLE_OP_REMOVE_BOND &&
        !ble_parse_address(ctx, argv[0].val, &state->peer)) {
        heap_caps_free(state);
        return false;
    }
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
}

static bool ble_remove_bond_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return ble_bond_operation_capture_common(ctx, this_ref, argc, argv,
                                             out_state, BLE_OP_REMOVE_BOND);
}

static bool ble_clear_bonds_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return ble_bond_operation_capture_common(ctx, this_ref, argc, argv,
                                             out_state, BLE_OP_CLEAR_BONDS);
}

static bool ble_bond_operation_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    int count = 0;
    int rc;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (state->operation == BLE_OP_REMOVE_BOND) {
        rc = ble_store_util_delete_peer(&state->peer);
        state->result_bool = rc == 0;
        if (rc == BLE_HS_ENOENT) rc = 0;
    } else {
        rc = ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count);
        if (rc == 0) rc = ble_store_clear();
        state->result_count = rc == 0 ? count : 0;
    }
    state->host_code = rc;
    if (rc != 0) {
        ble_throw_error(ctx, "BLE_SECURITY_ERROR", rc, -1, -1, -1);
        return false;
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static JSValue ble_bond_operation_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->host_code != 0)
        return ble_throw_error(ctx, "BLE_SECURITY_ERROR", state->host_code,
                               -1, -1, -1);
    return state->operation == BLE_OP_REMOVE_BOND
               ? JS_NewBool(state->result_bool)
               : JS_NewInt32(ctx, state->result_count);
}

static const esp32_mquickjs_future_driver_t s_ble_remove_bond_driver = {
    .capture = ble_remove_bond_capture,
    .start = ble_bond_operation_start,
    .poll = ble_future_poll,
    .finish = ble_bond_operation_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_adapter_resource_key,
};

static const esp32_mquickjs_future_driver_t s_ble_clear_bonds_driver = {
    .capture = ble_clear_bonds_capture,
    .start = ble_bond_operation_start,
    .poll = ble_future_poll,
    .finish = ble_bond_operation_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_adapter_resource_key,
};

static void ble_close_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    int rc = nimble_port_stop();
    esp_err_t deinit_rc = nimble_port_deinit();
    if (rc == 0 && deinit_rc != ESP_OK) rc = deinit_rc;
    state->host_code = rc;
    s_ble.host_started = false;
    s_ble.synchronized = false;
    atomic_store_explicit(&state->completed, true, memory_order_release);
    /* The generic Future worker owns the sole runtime wake after return. */
    ble_future_state_drop(state);
}

static bool ble_adapter_close_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    (void)argv;
    if (out_state == NULL || argc != 0 ||
        ble_adapter_from_value(ctx, this_ref->val, true) == NULL) return false;
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_ADAPTER_CLOSE;
    state->adapter_generation = s_ble.generation;
    state->timeout_ms = 10000;
    atomic_init(&state->completed, false);
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    return true;
}

static bool ble_adapter_close_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    uint16_t index;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    s_ble.lifecycle = BLE_LIFECYCLE_CLOSING;
    if (s_ble.scanner.active) (void)ble_gap_disc_cancel();
    if (s_ble.advertiser.active) (void)ble_gap_adv_stop();
    for (index = 0; index < s_ble.max_connections; ++index) {
        if (s_ble.connections[index].open)
            (void)ble_gap_terminate(s_ble.connections[index].conn_handle,
                                    BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_future_state_hold(state);
    if (!esp32_mquickjs_future_submit_worker(runtime, token,
                                              ble_close_worker, state)) {
        ble_future_state_drop(state);
        s_ble.lifecycle = BLE_LIFECYCLE_FAILED;
        ble_throw_error(ctx, "BLE_CLOSING", BLE_HS_ENOMEM, -1, -1, -1);
        return false;
    }
    return true;
}

static JSValue ble_adapter_close_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    int host_code = state != NULL ? state->host_code : BLE_HS_EUNKNOWN;
    ble_free_pools(&s_ble);
    s_ble.lifecycle = BLE_LIFECYCLE_CLOSED;
    s_ble.runtime = NULL;
    if (host_code != 0)
        return ble_throw_error(ctx, "BLE_CLOSING", host_code, -1, -1, -1);
    return JS_TRUE;
}

static esp32_mquickjs_resource_key_t ble_adapter_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    (void)state;
    return &s_ble_adapter_lane_key;
}

static const esp32_mquickjs_future_driver_t s_ble_adapter_close_driver = {
    .capture = ble_adapter_close_capture,
    .start = ble_adapter_close_start,
    .poll = ble_future_poll,
    .finish = ble_adapter_close_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_future_state_release,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_adapter_resource_key,
};

static ble_local_characteristic_t *ble_local_characteristic_from_value(
    JSContext *ctx, JSValue value, ble_local_characteristic_ref_t **out_ref,
    bool throw_if_stale)
{
    ble_local_characteristic_ref_t *ref;
    ble_local_characteristic_t *local = NULL;
    if (JS_GetClassID(ctx, value) != JS_CLASS_BLE_LOCAL_CHARACTERISTIC ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "expected a BLELocalCharacteristic instance");
        return NULL;
    }
    if (ref->adapter_generation == s_ble.generation &&
        s_ble.server.open && ref->index < s_ble.server.characteristic_count)
        local = &s_ble.server.characteristics[ref->index];
    if (local == NULL && throw_if_stale)
        JS_ThrowReferenceError(ctx, "BLE_STALE_ATTRIBUTE: local characteristic is stale");
    if (out_ref != NULL) *out_ref = ref;
    return local;
}

static bool ble_server_notify_capture(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    static const char *const allowed[] = {"connection", "indication", "timeoutMs"};
    esp32_mquickjs_future_driver_state_t *state;
    ble_local_characteristic_ref_t *ref = NULL;
    ble_local_characteristic_t *local;
    JSValue data = argc >= 1 ? argv[0].val : JS_UNDEFINED;
    JSValue options = argc == 2 ? argv[1].val : JS_UNDEFINED;
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue error = JS_UNDEFINED;
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool indication = false;
    size_t length;
    if (out_state == NULL || argc > 2 ||
        (local = ble_local_characteristic_from_value(
             ctx, this_ref->val, &ref, true)) == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    if (!JS_IsUndefined(options) &&
        (!ble_is_object(ctx, options) ||
         !ble_validate_option_keys(ctx, options,
                                   "BLELocalCharacteristic.notify()",
                                   allowed, 3))) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_PopGCRef(ctx, &property_ref);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->operation = BLE_OP_SERVER_NOTIFY;
    state->adapter_generation = ref->adapter_generation;
    state->attribute_index = ref->index;
    state->attribute_handle = local->value_handle;
    state->connection_index = UINT16_MAX;
    state->timeout_ms = BLE_DEFAULT_TIMEOUT_MS;
    atomic_init(&state->completed, false);
    atomic_init(&state->pending_confirmations, 0);
    if (!JS_IsUndefined(options)) {
        if (!ble_get_bool(ctx, options, "indication", false, &indication) ||
            !ble_parse_timeout_option(ctx, options, "server notify",
                                      &state->timeout_ms)) goto fail;
        *property = JS_GetPropertyStr(ctx, options, "connection");
        if (JS_IsException(*property)) goto fail;
        if (!JS_IsUndefined(*property)) {
            ble_connection_ref_t *connection_ref = NULL;
            if (ble_connection_from_value(ctx, *property, &connection_ref, true) == NULL)
                goto fail;
            state->connection_index = connection_ref->index;
            state->connection_generation = connection_ref->generation;
        }
    }
    state->indication = indication;
    if ((!indication && !(local->properties & BLE_GATT_CHR_F_NOTIFY)) ||
        (indication && !(local->properties & BLE_GATT_CHR_F_INDICATE))) {
        ble_throw_error(ctx, "BLE_NOT_SUPPORTED", BLE_HS_ENOTSUP, -1,
                        -1, local->value_handle);
        goto fail;
    }
    if (JS_IsUndefined(data)) {
        taskENTER_CRITICAL(&local->lock);
        length = local->length;
        state->payload = heap_caps_malloc(length > 0 ? length : 1,
                                           MALLOC_CAP_8BIT);
        if (state->payload != NULL && length > 0)
            memcpy(state->payload, local->value, length);
        taskEXIT_CRITICAL(&local->lock);
        if (state->payload == NULL) {
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
        state->payload_length = length;
    } else {
        if (!esp32_mquickjs_get_byte_source(ctx, data, "server notify data",
                                            &source, &owned, &error)) goto fail;
        if (source.length > local->max_length) {
            esp32_mquickjs_release_byte_source(owned);
            owned = NULL;
            ble_throw_error(ctx, "BLE_PAYLOAD_TOO_LARGE", BLE_HS_EMSGSIZE,
                            -1, -1, local->value_handle);
            goto fail;
        }
        state->payload = heap_caps_malloc(source.length > 0 ? source.length : 1,
                                           MALLOC_CAP_8BIT);
        if (state->payload == NULL) {
            esp32_mquickjs_release_byte_source(owned);
            owned = NULL;
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
        state->payload_length = source.length;
        if (source.length > 0) memcpy(state->payload, source.data, source.length);
        esp32_mquickjs_release_byte_source(owned);
        owned = NULL;
    }
    ble_retain_owner(ctx, this_ref->val, state);
    *out_state = state;
    JS_PopGCRef(ctx, &property_ref);
    return true;
fail:
    esp32_mquickjs_release_byte_source(owned);
    heap_caps_free(state->payload);
    heap_caps_free(state);
    JS_PopGCRef(ctx, &property_ref);
    return false;
}

static bool ble_server_notify_start(
    JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    uint16_t index;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (state->indication)
        ble_active_state_bind(&s_ble_server_notify_state, state);
    for (index = 0; index < s_ble.max_connections; ++index) {
        ble_connection_slot_t *slot = &s_ble.connections[index];
        struct os_mbuf *mbuf;
        int rc;
        if (!slot->open ||
            (state->connection_index != UINT16_MAX &&
             (index != state->connection_index ||
              slot->generation != state->connection_generation))) continue;
        state->attempted_connections++;
        mbuf = ble_hs_mbuf_from_flat(state->payload, state->payload_length);
        if (mbuf == NULL) {
            if (state->host_code == 0) state->host_code = BLE_HS_ENOMEM;
            continue;
        }
        if (state->indication) {
            atomic_fetch_add_explicit(&state->pending_confirmations, 1,
                                      memory_order_relaxed);
            rc = ble_gatts_indicate_custom(slot->conn_handle,
                                            state->attribute_handle, mbuf);
            if (rc != 0)
                atomic_fetch_sub_explicit(&state->pending_confirmations, 1,
                                          memory_order_relaxed);
        } else {
            rc = ble_gatts_notify_custom(slot->conn_handle,
                                          state->attribute_handle, mbuf);
        }
        if (rc == 0) state->submitted_connections++;
        else if (state->host_code == 0) state->host_code = rc;
    }
    if (state->indication)
        atomic_fetch_add_explicit(&s_ble.server.indications,
                                  state->submitted_connections,
                                  memory_order_relaxed);
    else
        atomic_fetch_add_explicit(&s_ble.server.notifications,
                                  state->submitted_connections,
                                  memory_order_relaxed);
    if (!state->indication ||
        atomic_load_explicit(&state->pending_confirmations,
                             memory_order_acquire) == 0) {
        ble_active_state_bind(&s_ble_server_notify_state, NULL);
        atomic_store_explicit(&state->completed, true, memory_order_release);
        (void)esp32_mquickjs_future_wake(runtime, token);
    }
    if (state->attempted_connections == 0 && state->connection_index != UINT16_MAX) {
        state->host_code = BLE_HS_ENOTCONN;
    }
    (void)ctx;
    return true;
}

static JSValue ble_server_notify_finish(
    JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "attemptedConnections",
            JS_NewUint32(ctx, state->attempted_connections)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "submittedConnections",
            JS_NewUint32(ctx, state->submitted_connections)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "bytes",
            JS_NewInt64(ctx, state->payload_length)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "indication",
            JS_NewBool(state->indication))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

static void ble_server_notify_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    ble_future_state_release(state);
}

static esp32_mquickjs_resource_key_t ble_server_notify_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    (void)state;
    return &s_ble_server_lane_key;
}

static const esp32_mquickjs_future_driver_t s_ble_server_notify_driver = {
    .capture = ble_server_notify_capture,
    .start = ble_server_notify_start,
    .poll = ble_future_poll,
    .finish = ble_server_notify_finish,
    .cancel = ble_future_cancel,
    .destroy = ble_server_notify_destroy,
    .timeout_ms = ble_future_timeout_ms,
    .on_timeout = ble_future_on_timeout,
    .resource_key = ble_server_notify_resource_key,
};

static const char *ble_stop_reason_name(ble_stop_reason_t reason)
{
    switch (reason) {
    case BLE_STOP_RUNNING: return "running";
    case BLE_STOP_COMPLETED: return "completed";
    case BLE_STOP_CONNECTED: return "connected";
    case BLE_STOP_CLOSED: return "closed";
    default: return "error";
    }
}

static JSValue ble_properties_to_js(JSContext *ctx, uint32_t properties)
{
    static const struct {
        uint32_t flag;
        const char *name;
    } names[] = {
        {BLE_GATT_CHR_PROP_BROADCAST, "broadcast"},
        {BLE_GATT_CHR_PROP_READ, "read"},
        {BLE_GATT_CHR_PROP_WRITE, "write"},
        {BLE_GATT_CHR_PROP_WRITE_NO_RSP, "write-without-response"},
        {BLE_GATT_CHR_PROP_NOTIFY, "notify"},
        {BLE_GATT_CHR_PROP_INDICATE, "indicate"},
        {BLE_GATT_CHR_PROP_AUTH_SIGN_WRITE, "authenticated-signed-write"},
    };
    JSGCRef array_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    uint32_t input;
    uint32_t output = 0;
    *array = JS_NewArray(ctx, 0);
    for (input = 0; !JS_IsException(*array) &&
                    input < sizeof(names) / sizeof(names[0]); ++input) {
        if ((properties & names[input].flag) != 0 &&
            JS_IsException(JS_SetPropertyUint32(
                ctx, *array, output++, JS_NewString(ctx, names[input].name))))
            *array = JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &array_ref);
}

static JSValue ble_adapter_status_to_js(JSContext *ctx)
{
    JSGCRef object_ref, roles_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *roles = JS_PushGCRef(ctx, &roles_ref);
    char address[18];
    int bonds = 0;
    uint16_t index;
    uint32_t connections = 0;
    uint32_t role_index = 0;
    ble_format_address(s_ble.address, address);
    (void)ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &bonds);
    for (index = 0; index < s_ble.max_connections; ++index)
        if (s_ble.connections[index].open) connections++;
    *roles = JS_NewArray(ctx, 0);
    if (s_ble.role_central)
        (void)JS_SetPropertyUint32(ctx, *roles, role_index++,
                                   JS_NewString(ctx, "central"));
    if (s_ble.role_peripheral)
        (void)JS_SetPropertyUint32(ctx, *roles, role_index++,
                                   JS_NewString(ctx, "peripheral"));
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) || JS_IsException(*roles) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "open",
            JS_NewBool(s_ble.lifecycle == BLE_LIFECYCLE_ACTIVE)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "synchronized",
            JS_NewBool(s_ble.synchronized)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "address",
            JS_NewString(ctx, address)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "addressType",
            JS_NewString(ctx, ble_address_type_name(s_ble.own_addr_type))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "deviceName",
            JS_NewString(ctx, s_ble.device_name)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "roles", *roles) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "connections",
            JS_NewUint32(ctx, connections)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "maxConnections",
            JS_NewUint32(ctx, s_ble.max_connections)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "scanning",
            JS_NewBool(s_ble.scanner.active)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "advertising",
            JS_NewBool(s_ble.advertiser.active)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "bondedDevices",
            JS_NewInt32(ctx, bonds)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "resetCount",
            JS_NewUint32(ctx, atomic_load_explicit(&s_ble.reset_count,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "droppedScanReports",
            JS_NewUint32(ctx, atomic_load_explicit(&s_ble.scanner.dropped,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "droppedConnectionEvents",
            JS_NewUint32(ctx, atomic_load_explicit(
                &s_ble.dropped_connection_events, memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "droppedServerEvents",
            JS_NewUint32(ctx, atomic_load_explicit(&s_ble.server.dropped,
                                                   memory_order_relaxed)))) {
        JS_PopGCRef(ctx, &roles_ref);
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    JS_PopGCRef(ctx, &roles_ref);
    return JS_PopGCRef(ctx, &object_ref);
}

static JSValue ble_connection_status_to_js(JSContext *ctx,
                                           ble_connection_slot_t *slot,
                                           uint16_t connection_index)
{
    JSGCRef object_ref, peer_ref, security_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *peer = JS_PushGCRef(ctx, &peer_ref);
    JSValue *security = JS_PushGCRef(ctx, &security_ref);
    *object = JS_NewObject(ctx);
    *peer = ble_address_to_js(ctx, &slot->peer);
    *security = ble_security_to_js(ctx, &slot->security, &slot->peer);
    if (JS_IsException(*object) || JS_IsException(*peer) ||
        JS_IsException(*security) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "open",
                                         JS_NewBool(slot->open)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "connectionId",
                                         JS_NewUint32(ctx, connection_index)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "peer", *peer) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "role",
            JS_NewString(ctx, slot->central ? "central" : "peripheral")) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "mtu",
                                         JS_NewUint32(ctx, slot->mtu)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "rssi",
            slot->rssi_valid ? JS_NewInt32(ctx, slot->rssi) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "security", *security) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "gattQueued",
            JS_NewUint32(ctx, atomic_load_explicit(&slot->gatt_pending,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "gattActive",
            JS_NewBool(atomic_load_explicit(&slot->active_gatt_state,
                                            memory_order_acquire) != NULL))) {
        JS_PopGCRef(ctx, &security_ref);
        JS_PopGCRef(ctx, &peer_ref);
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    JS_PopGCRef(ctx, &security_ref);
    JS_PopGCRef(ctx, &peer_ref);
    return JS_PopGCRef(ctx, &object_ref);
}

static JSValue ble_throw_constructor(JSContext *ctx, const char *name)
{
    return JS_ThrowTypeError(ctx, "%s cannot be constructed directly", name);
}

JSValue js_ble_adapter_constructor(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ble_throw_constructor(ctx, "BLEAdapter");
}

void js_ble_adapter_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

JSValue js_ble_adapter_status(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    (void)argv;
    if (this_val == NULL || argc != 0 ||
        ble_adapter_from_value(ctx, *this_val, true) == NULL) return JS_EXCEPTION;
    return ble_adapter_status_to_js(ctx);
}

#define BLE_FUTURE_WRAPPER(function_name, method_name)                         \
    JSValue function_name(JSContext *ctx, JSValue *this_val,                  \
                          int argc, JSValue *argv)                             \
    {                                                                          \
        if (this_val == NULL)                                                   \
            return JS_ThrowTypeError(ctx, method_name " has no receiver");    \
        return ble_future_call_and_wait(ctx, *this_val, method_name, argc,     \
                                        argv);                                  \
    }

BLE_FUTURE_WRAPPER(js_ble_adapter_scan, "scan")
BLE_FUTURE_WRAPPER(js_ble_adapter_connect, "connect")
BLE_FUTURE_WRAPPER(js_ble_adapter_advertise, "advertise")
BLE_FUTURE_WRAPPER(js_ble_adapter_remove_bond, "removeBond")
BLE_FUTURE_WRAPPER(js_ble_adapter_clear_bonds, "clearBonds")
BLE_FUTURE_WRAPPER(js_ble_adapter_close, "close")

JSValue js_ble_adapter_server(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    ble_server_ref_t *ref;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    (void)argv;
    if (this_val == NULL || argc != 0 ||
        ble_adapter_from_value(ctx, *this_val, true) == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    if (!s_ble.server.open) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_NULL;
    }
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BLE_GATT_SERVER);
    if (JS_IsException(*object)) goto fail;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ref->adapter_generation = s_ble.generation;
    JS_SetOpaque(ctx, *object, ref);
    if (!esp32_mquickjs_set_property_ref(ctx, object, "_eventQueue",
                                         s_ble.server.event_queue_ref.val)) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
        goto fail;
    }
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

JSValue js_ble_adapter_bonds(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv)
{
    ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int count = 0;
    int index;
    JSGCRef array_ref, item_ref, peer_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    JSValue *peer = JS_PushGCRef(ctx, &peer_ref);
    (void)argv;
    if (this_val == NULL || argc != 0 ||
        ble_adapter_from_value(ctx, *this_val, true) == NULL) goto fail;
    if (ble_store_util_bonded_peers(peers, &count,
                                    CONFIG_BT_NIMBLE_MAX_BONDS) != 0) {
        ble_throw_error(ctx, "BLE_SECURITY_ERROR", BLE_HS_ESTORE_FAIL,
                        -1, -1, -1);
        goto fail;
    }
    *array = JS_NewArray(ctx, count);
    for (index = 0; !JS_IsException(*array) && index < count; ++index) {
        struct ble_store_key_sec key = {.peer_addr = peers[index]};
        struct ble_store_value_sec value = {0};
        bool authenticated = false, secure_connections = false;
        if (ble_store_read_peer_sec(&key, &value) == 0) {
            authenticated = value.authenticated;
            secure_connections = value.sc;
        }
        *peer = ble_address_to_js(ctx, &peers[index]);
        *item = JS_NewObject(ctx);
        if (JS_IsException(*peer) || JS_IsException(*item) ||
            !esp32_mquickjs_set_property_ref(ctx, item, "peer", *peer) ||
            !esp32_mquickjs_set_property_ref(ctx, item, "authenticated",
                                             JS_NewBool(authenticated)) ||
            !esp32_mquickjs_set_property_ref(ctx, item, "secureConnections",
                                             JS_NewBool(secure_connections)) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *array, index, *item)))
            goto fail;
    }
    JS_PopGCRef(ctx, &peer_ref);
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &peer_ref);
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

JSValue js_ble_scanner_constructor(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ble_throw_constructor(ctx, "BLEScanner");
}

void js_ble_scanner_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

JSValue js_ble_scanner_receive(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv)
{
    if (this_val == NULL ||
        ble_scanner_from_value(ctx, *this_val, true) == NULL) return JS_EXCEPTION;
    return ble_future_call_and_wait(ctx, *this_val, "receive", argc, argv);
}

JSValue js_ble_scanner_stats(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv)
{
    if (this_val == NULL ||
        ble_scanner_from_value(ctx, *this_val, true) == NULL) return JS_EXCEPTION;
    return js_event_queue_stats(ctx, this_val, argc, argv);
}

JSValue js_ble_scanner_status(JSContext *ctx, JSValue *this_val,
                              int argc, JSValue *argv)
{
    ble_scanner_t *scanner;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    (void)argv;
    if (this_val == NULL || argc != 0 ||
        (scanner = ble_scanner_from_value(ctx, *this_val, true)) == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "open",
                                         JS_NewBool(scanner->open)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "active",
                                         JS_NewBool(scanner->active)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "startedAtUs",
                                         JS_NewInt64(ctx, scanner->started_at_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "reports",
            JS_NewUint32(ctx, atomic_load_explicit(&scanner->reports,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "dropped",
            JS_NewUint32(ctx, atomic_load_explicit(&scanner->dropped,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "malformed",
            JS_NewUint32(ctx, atomic_load_explicit(&scanner->malformed,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "stopReason",
            JS_NewString(ctx, ble_stop_reason_name(scanner->stop_reason)))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

BLE_FUTURE_WRAPPER(js_ble_scanner_close, "close")

JSValue js_ble_advertiser_constructor(JSContext *ctx, JSValue *this_val,
                                      int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ble_throw_constructor(ctx, "BLEAdvertiser");
}

void js_ble_advertiser_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

JSValue js_ble_advertiser_receive(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
{
    if (this_val == NULL ||
        ble_advertiser_from_value(ctx, *this_val, true) == NULL) return JS_EXCEPTION;
    return ble_future_call_and_wait(ctx, *this_val, "receive", argc, argv);
}

JSValue js_ble_advertiser_stats(JSContext *ctx, JSValue *this_val,
                                int argc, JSValue *argv)
{
    if (this_val == NULL ||
        ble_advertiser_from_value(ctx, *this_val, true) == NULL) return JS_EXCEPTION;
    return js_event_queue_stats(ctx, this_val, argc, argv);
}

JSValue js_ble_advertiser_status(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    ble_advertiser_t *advertiser;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    (void)argv;
    if (this_val == NULL || argc != 0 ||
        (advertiser = ble_advertiser_from_value(ctx, *this_val, true)) == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "open",
                                         JS_NewBool(advertiser->open)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "active",
                                         JS_NewBool(advertiser->active)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "connectable",
                                         JS_NewBool(advertiser->connectable)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "scannable",
                                         JS_NewBool(advertiser->scannable)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "incomingConnections",
            JS_NewUint32(ctx, atomic_load_explicit(&advertiser->incoming,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "dropped",
            JS_NewUint32(ctx, atomic_load_explicit(&advertiser->dropped,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "stopReason",
            JS_NewString(ctx, ble_stop_reason_name(advertiser->stop_reason)))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

BLE_FUTURE_WRAPPER(js_ble_advertiser_close, "close")

JSValue js_ble_connection_constructor(JSContext *ctx, JSValue *this_val,
                                      int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ble_throw_constructor(ctx, "BLEConnection");
}

void js_ble_connection_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

JSValue js_ble_connection_receive(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    if (this_val == NULL ||
        ble_connection_from_value(ctx, *this_val, NULL, true) == NULL)
        return JS_EXCEPTION;
    return ble_future_call_and_wait(ctx, *this_val, "receive", argc, argv);
}

JSValue js_ble_connection_stats(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    if (this_val == NULL ||
        ble_connection_from_value(ctx, *this_val, NULL, true) == NULL)
        return JS_EXCEPTION;
    return js_event_queue_stats(ctx, this_val, argc, argv);
}

JSValue js_ble_connection_status(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
{
    ble_connection_ref_t *ref = NULL;
    ble_connection_slot_t *slot;
    (void)argv;
    if (this_val == NULL || argc != 0 ||
        (slot = ble_connection_from_value(ctx, *this_val, &ref, true)) == NULL)
        return JS_EXCEPTION;
    return ble_connection_status_to_js(ctx, slot, ref->index);
}

BLE_FUTURE_WRAPPER(js_ble_connection_pair, "pair")
BLE_FUTURE_WRAPPER(js_ble_connection_exchange_mtu, "exchangeMtu")
BLE_FUTURE_WRAPPER(js_ble_connection_read_rssi, "readRssi")
BLE_FUTURE_WRAPPER(js_ble_connection_discover, "discover")
BLE_FUTURE_WRAPPER(js_ble_connection_close, "close")

JSValue js_ble_connection_respond_pairing(JSContext *ctx, JSValue *this_val,
                                          int argc, JSValue *argv)
{
    ble_connection_slot_t *slot;
    uint32_t request_id;
    struct ble_sm_io response = {0};
    if (this_val == NULL || argc != 2 ||
        (slot = ble_connection_from_value(ctx, *this_val, NULL, true)) == NULL ||
        !ble_to_u32(ctx, argv[0], &request_id)) {
        if (!JS_HasException(ctx))
            JS_ThrowTypeError(ctx,
                "respondPairing(requestId, response) expects two arguments");
        return JS_EXCEPTION;
    }
    if (request_id == 0 || request_id != slot->pairing_request_id ||
        esp_timer_get_time() > slot->pairing_expires_at_us) {
        return ble_throw_error(ctx, "BLE_PAIRING_EXPIRED", BLE_HS_ETIMEOUT,
                               -1, -1, -1);
    }
    response.action = slot->pairing_action;
    if (response.action == BLE_SM_IOACT_NUMCMP) {
        if (!JS_IsBool(argv[1]))
            return JS_ThrowTypeError(ctx,
                                     "numeric comparison response must be boolean");
        response.numcmp_accept = JS_VALUE_GET_SPECIAL_VALUE(argv[1]) != 0;
    } else if (response.action == BLE_SM_IOACT_INPUT) {
        uint32_t passkey;
        if (!ble_to_u32(ctx, argv[1], &passkey) || passkey > 999999)
            return JS_ThrowRangeError(ctx, "pairing passkey must be 0..999999");
        response.passkey = passkey;
    } else {
        if (!JS_IsBool(argv[1]))
            return JS_ThrowTypeError(ctx, "pairing response must be boolean");
        if (JS_VALUE_GET_SPECIAL_VALUE(argv[1]) == 0) return JS_FALSE;
        response.passkey = slot->pairing_passkey;
    }
    if (ble_sm_inject_io(slot->conn_handle, &response) != 0)
        return ble_throw_error(ctx, "BLE_SECURITY_ERROR", BLE_HS_EINVAL,
                               -1, -1, -1);
    slot->pairing_request_id = 0;
    return JS_TRUE;
}

JSValue js_ble_service_constructor(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ble_throw_constructor(ctx, "BLEService");
}

void js_ble_service_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

static JSValue ble_new_characteristic_handle(JSContext *ctx,
                                             uint16_t connection_index,
                                             uint16_t characteristic_index)
{
    ble_connection_slot_t *slot = &s_ble.connections[connection_index];
    ble_remote_characteristic_t *characteristic =
        &slot->characteristics[characteristic_index];
    ble_attribute_ref_t *ref;
    char uuid[BLE_UUID_TEXT_MAX];
    JSGCRef object_ref, properties_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *properties = JS_PushGCRef(ctx, &properties_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BLE_CHARACTERISTIC);
    *properties = ble_properties_to_js(ctx, characteristic->properties);
    if (JS_IsException(*object) || JS_IsException(*properties)) goto fail;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ref->adapter_generation = s_ble.generation;
    ref->connection_index = connection_index;
    ref->connection_generation = slot->generation;
    ref->discovery_generation = slot->discovery_generation;
    ref->index = characteristic_index;
    JS_SetOpaque(ctx, *object, ref);
    ble_uuid_to_text(&characteristic->uuid, uuid);
    if (!esp32_mquickjs_set_property_ref(ctx, object, "uuid",
                                         JS_NewString(ctx, uuid)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "declarationHandle",
            JS_NewUint32(ctx, characteristic->declaration_handle)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "valueHandle",
            JS_NewUint32(ctx, characteristic->value_handle)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "properties", *properties)) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
        goto fail;
    }
    JS_PopGCRef(ctx, &properties_ref);
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &properties_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

JSValue js_ble_service_characteristics(JSContext *ctx, JSValue *this_val,
                                        int argc, JSValue *argv)
{
    ble_attribute_ref_t *ref = NULL;
    ble_connection_slot_t *slot;
    ble_remote_service_t *service;
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    uint16_t offset;
    (void)argv;
    if (this_val == NULL || argc != 0 ||
        (slot = ble_attribute_from_value(ctx, *this_val,
                                         JS_CLASS_BLE_SERVICE,
                                         &ref, true)) == NULL ||
        ref->index >= slot->service_count) goto fail;
    service = &slot->services[ref->index];
    *array = JS_NewArray(ctx, service->characteristic_count);
    for (offset = 0; !JS_IsException(*array) &&
                     offset < service->characteristic_count; ++offset) {
        *item = ble_new_characteristic_handle(
            ctx, ref->connection_index, service->first_characteristic + offset);
        if (JS_IsException(*item) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *array, offset, *item)))
            goto fail;
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

JSValue js_ble_characteristic_constructor(JSContext *ctx, JSValue *this_val,
                                           int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ble_throw_constructor(ctx, "BLECharacteristic");
}

void js_ble_characteristic_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

static JSValue ble_new_descriptor_handle(JSContext *ctx,
                                         uint16_t connection_index,
                                         uint16_t descriptor_index)
{
    ble_connection_slot_t *slot = &s_ble.connections[connection_index];
    ble_remote_descriptor_t *descriptor = &slot->descriptors[descriptor_index];
    ble_attribute_ref_t *ref;
    char uuid[BLE_UUID_TEXT_MAX];
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BLE_DESCRIPTOR);
    if (JS_IsException(*object)) goto fail;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ref->adapter_generation = s_ble.generation;
    ref->connection_index = connection_index;
    ref->connection_generation = slot->generation;
    ref->discovery_generation = slot->discovery_generation;
    ref->index = descriptor_index;
    JS_SetOpaque(ctx, *object, ref);
    ble_uuid_to_text(&descriptor->uuid, uuid);
    if (!esp32_mquickjs_set_property_ref(ctx, object, "uuid",
                                         JS_NewString(ctx, uuid)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "handle",
                                         JS_NewUint32(ctx, descriptor->handle))) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
        goto fail;
    }
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

JSValue js_ble_characteristic_descriptors(JSContext *ctx, JSValue *this_val,
                                           int argc, JSValue *argv)
{
    ble_attribute_ref_t *ref = NULL;
    ble_connection_slot_t *slot;
    ble_remote_characteristic_t *characteristic;
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    uint16_t offset;
    (void)argv;
    if (this_val == NULL || argc != 0 ||
        (slot = ble_attribute_from_value(ctx, *this_val,
                                         JS_CLASS_BLE_CHARACTERISTIC,
                                         &ref, true)) == NULL ||
        ref->index >= slot->characteristic_count) goto fail;
    characteristic = &slot->characteristics[ref->index];
    *array = JS_NewArray(ctx, characteristic->descriptor_count);
    for (offset = 0; !JS_IsException(*array) &&
                     offset < characteristic->descriptor_count; ++offset) {
        *item = ble_new_descriptor_handle(
            ctx, ref->connection_index, characteristic->first_descriptor + offset);
        if (JS_IsException(*item) ||
            JS_IsException(JS_SetPropertyUint32(ctx, *array, offset, *item)))
            goto fail;
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}

BLE_FUTURE_WRAPPER(js_ble_characteristic_read, "read")
BLE_FUTURE_WRAPPER(js_ble_characteristic_write, "write")
BLE_FUTURE_WRAPPER(js_ble_characteristic_subscribe, "subscribe")

JSValue js_ble_descriptor_constructor(JSContext *ctx, JSValue *this_val,
                                      int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ble_throw_constructor(ctx, "BLEDescriptor");
}

void js_ble_descriptor_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

BLE_FUTURE_WRAPPER(js_ble_descriptor_read, "read")
BLE_FUTURE_WRAPPER(js_ble_descriptor_write, "write")

JSValue js_ble_notification_constructor(JSContext *ctx, JSValue *this_val,
                                        int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ble_throw_constructor(ctx, "BLENotificationStream");
}

void js_ble_notification_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

JSValue js_ble_notification_receive(JSContext *ctx, JSValue *this_val,
                                     int argc, JSValue *argv)
{
    if (this_val == NULL ||
        ble_subscription_from_value(ctx, *this_val, NULL, true) == NULL)
        return JS_EXCEPTION;
    return ble_future_call_and_wait(ctx, *this_val, "receive", argc, argv);
}

JSValue js_ble_notification_stats(JSContext *ctx, JSValue *this_val,
                                   int argc, JSValue *argv)
{
    if (this_val == NULL ||
        ble_subscription_from_value(ctx, *this_val, NULL, true) == NULL)
        return JS_EXCEPTION;
    return js_event_queue_stats(ctx, this_val, argc, argv);
}

JSValue js_ble_notification_status(JSContext *ctx, JSValue *this_val,
                                    int argc, JSValue *argv)
{
    ble_subscription_t *subscription;
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    (void)argv;
    if (this_val == NULL || argc != 0 ||
        (subscription = ble_subscription_from_value(
             ctx, *this_val, NULL, true)) == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "open",
                                         JS_NewBool(subscription->open)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "mode",
            JS_NewString(ctx, subscription->indication ? "indicate" : "notify")) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "received",
            JS_NewUint32(ctx, atomic_load_explicit(&subscription->received,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "dropped",
            JS_NewUint32(ctx, atomic_load_explicit(&subscription->dropped,
                                                   memory_order_relaxed)))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

BLE_FUTURE_WRAPPER(js_ble_notification_close, "close")

JSValue js_ble_gatt_server_constructor(JSContext *ctx, JSValue *this_val,
                                       int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ble_throw_constructor(ctx, "BLEGattServer");
}

void js_ble_gatt_server_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

static bool ble_server_from_value(JSContext *ctx, JSValue value)
{
    ble_server_ref_t *ref;
    if (JS_GetClassID(ctx, value) != JS_CLASS_BLE_GATT_SERVER ||
        (ref = JS_GetOpaque(ctx, value)) == NULL) {
        JS_ThrowTypeError(ctx, "expected a BLEGattServer instance");
        return false;
    }
    if (ref->adapter_generation != s_ble.generation || !s_ble.server.open) {
        JS_ThrowReferenceError(ctx, "BLE_STALE_ADAPTER: GATT server is closed");
        return false;
    }
    return true;
}

JSValue js_ble_gatt_server_status(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    uint16_t index;
    uint32_t active = 0;
    (void)argv;
    if (this_val == NULL || argc != 0 ||
        !ble_server_from_value(ctx, *this_val)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    for (index = 0; index < s_ble.max_connections; ++index)
        if (s_ble.connections[index].open) active++;
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "open", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "services",
            JS_NewUint32(ctx, s_ble.server.service_count)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "characteristics",
            JS_NewUint32(ctx, s_ble.server.characteristic_count)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "activeConnections",
            JS_NewUint32(ctx, active)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "writes",
            JS_NewUint32(ctx, atomic_load_explicit(&s_ble.server.writes,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "notifications",
            JS_NewUint32(ctx, atomic_load_explicit(&s_ble.server.notifications,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "indications",
            JS_NewUint32(ctx, atomic_load_explicit(&s_ble.server.indications,
                                                   memory_order_relaxed))) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "droppedEvents",
            JS_NewUint32(ctx, atomic_load_explicit(&s_ble.server.dropped,
                                                   memory_order_relaxed)))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_ble_gatt_server_watch(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    static const char *const allowed[] = {"capacity"};
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    uint32_t capacity;
    (void)argv;
    if (this_val == NULL || argc > 1 ||
        !ble_server_from_value(ctx, *this_val)) {
        JS_PopGCRef(ctx, &property_ref);
        return JS_EXCEPTION;
    }
    if (argc == 1 && !JS_IsUndefined(argv[0])) {
        if (!ble_is_object(ctx, argv[0]) ||
            !ble_validate_option_keys(ctx, argv[0], "BLEGattServer.watch()",
                                      allowed, 1)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "capacity");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) &&
            (!ble_to_u32(ctx, *property, &capacity) ||
             capacity != s_ble.server.event_capacity)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowRangeError(
                ctx, "watch capacity is fixed when ble.open() captures the server");
        }
    }
    JS_PopGCRef(ctx, &property_ref);
    return JS_GetPropertyStr(ctx, *this_val, "_eventQueue");
}

JSValue js_ble_gatt_server_characteristic(JSContext *ctx, JSValue *this_val,
                                           int argc, JSValue *argv)
{
    JSCStringBuf buffer;
    const char *id;
    uint16_t index;
    ble_local_characteristic_ref_t *ref;
    JSGCRef object_ref, properties_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *properties = JS_PushGCRef(ctx, &properties_ref);
    char uuid[BLE_UUID_TEXT_MAX];
    if (this_val == NULL || argc != 1 ||
        !ble_server_from_value(ctx, *this_val) ||
        !JS_IsString(ctx, argv[0]) ||
        (id = JS_ToCString(ctx, argv[0], &buffer)) == NULL) goto fail;
    for (index = 0; index < s_ble.server.characteristic_count; ++index)
        if (strcmp(id, s_ble.server.characteristics[index].id) == 0) break;
    if (index == s_ble.server.characteristic_count) {
        JS_ThrowReferenceError(ctx, "BLE_STALE_ATTRIBUTE: unknown characteristic id");
        goto fail;
    }
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BLE_LOCAL_CHARACTERISTIC);
    *properties = ble_properties_to_js(
        ctx, s_ble.server.characteristics[index].properties);
    if (JS_IsException(*object) || JS_IsException(*properties)) goto fail;
    ref = heap_caps_calloc(1, sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    ref->adapter_generation = s_ble.generation;
    ref->index = index;
    JS_SetOpaque(ctx, *object, ref);
    ble_uuid_to_text(&s_ble.server.characteristics[index].uuid, uuid);
    if (!esp32_mquickjs_set_property_ref(ctx, object, "id",
            JS_NewString(ctx, s_ble.server.characteristics[index].id)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "uuid",
            JS_NewString(ctx, uuid)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "maxLength",
            JS_NewUint32(ctx, s_ble.server.characteristics[index].max_length)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "properties", *properties)) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(ref);
        goto fail;
    }
    JS_PopGCRef(ctx, &properties_ref);
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &properties_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

JSValue js_ble_local_characteristic_constructor(JSContext *ctx,
                                                JSValue *this_val,
                                                int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return ble_throw_constructor(ctx, "BLELocalCharacteristic");
}

void js_ble_local_characteristic_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    heap_caps_free(opaque);
}

JSValue js_ble_local_characteristic_value(JSContext *ctx, JSValue *this_val,
                                          int argc, JSValue *argv)
{
    ble_local_characteristic_t *local;
    uint8_t *payload;
    uint16_t length;
    (void)argv;
    if (this_val == NULL || argc != 0 ||
        (local = ble_local_characteristic_from_value(
             ctx, *this_val, NULL, true)) == NULL) return JS_EXCEPTION;
    taskENTER_CRITICAL(&local->lock);
    length = local->length;
    payload = heap_caps_malloc(length > 0 ? length : 1, MALLOC_CAP_8BIT);
    if (payload != NULL && length > 0) memcpy(payload, local->value, length);
    taskEXIT_CRITICAL(&local->lock);
    if (payload == NULL) return JS_ThrowOutOfMemory(ctx);
    return esp32_mquickjs_new_owned_byte_view(ctx, payload, length);
}

JSValue js_ble_local_characteristic_set_value(JSContext *ctx,
                                              JSValue *this_val,
                                              int argc, JSValue *argv)
{
    ble_local_characteristic_t *local;
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue error = JS_UNDEFINED;
    if (this_val == NULL || argc != 1 ||
        (local = ble_local_characteristic_from_value(
             ctx, *this_val, NULL, true)) == NULL) return JS_EXCEPTION;
    if (!esp32_mquickjs_get_byte_source(ctx, argv[0], "setValue",
                                        &source, &owned, &error)) return JS_EXCEPTION;
    if (source.length > local->max_length) {
        esp32_mquickjs_release_byte_source(owned);
        return ble_throw_error(ctx, "BLE_PAYLOAD_TOO_LARGE", BLE_HS_EMSGSIZE,
                               -1, -1, local->value_handle);
    }
    taskENTER_CRITICAL(&local->lock);
    if (source.length > 0) memcpy(local->value, source.data, source.length);
    local->length = source.length;
    taskEXIT_CRITICAL(&local->lock);
    esp32_mquickjs_release_byte_source(owned);
    return JS_NewInt64(ctx, source.length);
}

BLE_FUTURE_WRAPPER(js_ble_local_characteristic_notify, "notify")

#undef BLE_FUTURE_WRAPPER

JSValue js_ble_capabilities(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    (void)this_val; (void)argc; (void)argv;
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "classic", JS_FALSE) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "central", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "peripheral", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "observer", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "broadcaster", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "legacyAdvertising", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "extendedAdvertising", JS_FALSE) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "maxConnections",
            JS_NewUint32(ctx, CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "maxMtu",
            JS_NewUint32(ctx, CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "bonding",
            JS_NewBool(CONFIG_ESP32_MQUICKJS_BLE_BONDING)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "privacy", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(ctx, object,
                                         "concurrentScanAdvertising", JS_FALSE)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_ble_open(JSContext *ctx, JSValue *this_val,
                    int argc, JSValue *argv)
{
    if (this_val == NULL)
        return JS_ThrowTypeError(ctx, "ble.open() has no receiver");
    return ble_future_call_and_wait(ctx, *this_val, "open", argc, argv);
}

static bool ble_register_future_drivers(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, module_ref, adapter_ref, scanner_ref, advertiser_ref,
        connection_ref, characteristic_ref, descriptor_ref, notification_ref,
        local_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref);
    JSValue *adapter = JS_PushGCRef(ctx, &adapter_ref);
    JSValue *scanner = JS_PushGCRef(ctx, &scanner_ref);
    JSValue *advertiser = JS_PushGCRef(ctx, &advertiser_ref);
    JSValue *connection = JS_PushGCRef(ctx, &connection_ref);
    JSValue *characteristic = JS_PushGCRef(ctx, &characteristic_ref);
    JSValue *descriptor = JS_PushGCRef(ctx, &descriptor_ref);
    JSValue *notification = JS_PushGCRef(ctx, &notification_ref);
    JSValue *local = JS_PushGCRef(ctx, &local_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    bool result;

#define BLE_REGISTER_DRIVER(object_value, method_name, driver_value)          \
    do {                                                                       \
        *method = result                                                       \
                      ? JS_GetPropertyStr(ctx, (object_value), (method_name))   \
                      : JS_EXCEPTION;                                          \
        result = result && !JS_IsException(*method) &&                         \
                 esp32_mquickjs_future_register_driver(                        \
                     ctx, runtime, *method, (driver_value));                   \
    } while (0)

#define BLE_REGISTER_RECEIVE(object_value)                                    \
    do {                                                                       \
        *method = result                                                       \
                      ? JS_GetPropertyStr(ctx, (object_value), "receive")      \
                      : JS_EXCEPTION;                                          \
        result = result && !JS_IsException(*method) &&                         \
                 esp32_mquickjs_event_queue_register_receive_alias(            \
                     ctx, runtime, *method);                                   \
    } while (0)

    *global = JS_GetGlobalObject(ctx);
    *module = JS_IsException(*global) ? JS_EXCEPTION
                                     : JS_GetPropertyStr(ctx, *global, "ble");
    result = !JS_IsException(*module);
    BLE_REGISTER_DRIVER(*module, "open", &s_ble_open_driver);
    *adapter = result ? JS_NewObjectClassUser(ctx, JS_CLASS_BLE_ADAPTER)
                      : JS_EXCEPTION;
    result = result && !JS_IsException(*adapter);
    BLE_REGISTER_DRIVER(*adapter, "scan", &s_ble_scan_driver);
    BLE_REGISTER_DRIVER(*adapter, "connect", &s_ble_connect_driver);
    BLE_REGISTER_DRIVER(*adapter, "advertise", &s_ble_advertise_driver);
    BLE_REGISTER_DRIVER(*adapter, "removeBond", &s_ble_remove_bond_driver);
    BLE_REGISTER_DRIVER(*adapter, "clearBonds", &s_ble_clear_bonds_driver);
    BLE_REGISTER_DRIVER(*adapter, "close", &s_ble_adapter_close_driver);
    *scanner = result ? JS_NewObjectClassUser(ctx, JS_CLASS_BLE_SCANNER)
                      : JS_EXCEPTION;
    result = result && !JS_IsException(*scanner);
    BLE_REGISTER_RECEIVE(*scanner);
    BLE_REGISTER_DRIVER(*scanner, "close", &s_ble_scanner_close_driver);
    *advertiser = result ? JS_NewObjectClassUser(ctx, JS_CLASS_BLE_ADVERTISER)
                         : JS_EXCEPTION;
    result = result && !JS_IsException(*advertiser);
    BLE_REGISTER_RECEIVE(*advertiser);
    BLE_REGISTER_DRIVER(*advertiser, "close", &s_ble_advertiser_close_driver);
    *connection = result ? JS_NewObjectClassUser(ctx, JS_CLASS_BLE_CONNECTION)
                         : JS_EXCEPTION;
    result = result && !JS_IsException(*connection);
    BLE_REGISTER_RECEIVE(*connection);
    BLE_REGISTER_DRIVER(*connection, "pair", &s_ble_pair_driver);
    BLE_REGISTER_DRIVER(*connection, "exchangeMtu", &s_ble_exchange_mtu_driver);
    BLE_REGISTER_DRIVER(*connection, "readRssi", &s_ble_read_rssi_driver);
    BLE_REGISTER_DRIVER(*connection, "discover", &s_ble_discover_driver);
    BLE_REGISTER_DRIVER(*connection, "close", &s_ble_connection_close_driver);
    *characteristic = result
                          ? JS_NewObjectClassUser(ctx, JS_CLASS_BLE_CHARACTERISTIC)
                          : JS_EXCEPTION;
    result = result && !JS_IsException(*characteristic);
    BLE_REGISTER_DRIVER(*characteristic, "read", &s_ble_gatt_read_driver);
    BLE_REGISTER_DRIVER(*characteristic, "write", &s_ble_gatt_write_driver);
    BLE_REGISTER_DRIVER(*characteristic, "subscribe", &s_ble_subscribe_driver);
    *descriptor = result ? JS_NewObjectClassUser(ctx, JS_CLASS_BLE_DESCRIPTOR)
                         : JS_EXCEPTION;
    result = result && !JS_IsException(*descriptor);
    BLE_REGISTER_DRIVER(*descriptor, "read", &s_ble_descriptor_read_driver);
    BLE_REGISTER_DRIVER(*descriptor, "write", &s_ble_descriptor_write_driver);
    *notification = result
                        ? JS_NewObjectClassUser(ctx,
                              JS_CLASS_BLE_NOTIFICATION_STREAM)
                        : JS_EXCEPTION;
    result = result && !JS_IsException(*notification);
    BLE_REGISTER_RECEIVE(*notification);
    BLE_REGISTER_DRIVER(*notification, "close", &s_ble_subscription_close_driver);
    *local = result
                 ? JS_NewObjectClassUser(ctx, JS_CLASS_BLE_LOCAL_CHARACTERISTIC)
                 : JS_EXCEPTION;
    result = result && !JS_IsException(*local);
    BLE_REGISTER_DRIVER(*local, "notify", &s_ble_server_notify_driver);
    if (!result && !JS_HasException(ctx))
        JS_ThrowInternalError(ctx, "failed to register BLE Future drivers");
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &local_ref);
    JS_PopGCRef(ctx, &notification_ref);
    JS_PopGCRef(ctx, &descriptor_ref);
    JS_PopGCRef(ctx, &characteristic_ref);
    JS_PopGCRef(ctx, &connection_ref);
    JS_PopGCRef(ctx, &advertiser_ref);
    JS_PopGCRef(ctx, &scanner_ref);
    JS_PopGCRef(ctx, &adapter_ref);
    JS_PopGCRef(ctx, &module_ref);
    JS_PopGCRef(ctx, &global_ref);
#undef BLE_REGISTER_RECEIVE
#undef BLE_REGISTER_DRIVER
    return result;
}

bool esp32_mquickjs_init_ble_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime)
{
    if (ctx == NULL || runtime == NULL ||
        s_ble.lifecycle != BLE_LIFECYCLE_CLOSED) return false;
    s_ble.ctx = ctx;
    s_ble.runtime = runtime;
    atomic_init(&s_ble_open_state, NULL);
    atomic_init(&s_ble_server_notify_state, NULL);
    return ble_register_future_drivers(ctx, runtime);
}

void esp32_mquickjs_deinit_ble_runtime(JSContext *ctx)
{
    (void)ctx;
    if (s_ble.lifecycle != BLE_LIFECYCLE_CLOSED) {
        if (s_ble.scanner.active) (void)ble_gap_disc_cancel();
        if (s_ble.advertiser.active) (void)ble_gap_adv_stop();
        if (s_ble.host_started) {
            (void)nimble_port_stop();
            (void)nimble_port_deinit();
        }
        ble_free_pools(&s_ble);
        s_ble.lifecycle = BLE_LIFECYCLE_CLOSED;
        s_ble.runtime = NULL;
    }
}

#endif
