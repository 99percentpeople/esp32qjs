#pragma once
#include "sdkconfig.h"
#include "esp32_mquickjs_wifi_mesh_feature.h"

#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_mesh.h"

#define ESP32_MQUICKJS_MESH_EVENT_SLOTS 8U
#define ESP32_MQUICKJS_MESH_MAX_GROUPS 64U
#define ESP32_MQUICKJS_MESH_MAX_NODES 1000U
#define ESP32_MQUICKJS_MESH_SCAN_IE_BYTES 257U

typedef struct {
    uint32_t identity;
    uint16_t total, remaining;
    esp_err_t error;
    bool running, completed, uncertain, retained;
} esp32_mquickjs_wifi_mesh_scan_status_t;
typedef struct {
    wifi_scan_config_t config;
    uint8_t ssid[33], bssid[6];
    bool has_ssid, has_bssid;
} esp32_mquickjs_wifi_mesh_scan_config_t;
typedef struct {
    wifi_ap_record_t ap;
    uint32_t identity, sequence;
    uint16_t ie_length;
    uint8_t ie[ESP32_MQUICKJS_MESH_SCAN_IE_BYTES];
} esp32_mquickjs_wifi_mesh_scan_record_t;

typedef struct { uint32_t generation, identity; } esp32_mquickjs_wifi_mesh_token_t;
typedef struct {
    mesh_cfg_t network;
    esp_mesh_topology_t topology;
    mesh_type_t type;
    wifi_auth_mode_t ap_authmode;
    uint16_t max_layer, capacity, receive_queue;
    bool fixed_root, self_organized, power_save, encrypt_ie;
    uint8_t ie_key_length;
    char ie_key[64];
    uint32_t send_block_ms;
    float vote_percentage;
} esp32_mquickjs_wifi_mesh_config_t;

enum {
    ESP32_MQUICKJS_MESH_EVENT_MAC = 1U,
    ESP32_MQUICKJS_MESH_EVENT_CHANNEL = 2U,
    ESP32_MQUICKJS_MESH_EVENT_LAYER = 4U,
    ESP32_MQUICKJS_MESH_EVENT_REASON = 8U,
    ESP32_MQUICKJS_MESH_EVENT_TABLE = 16U,
    ESP32_MQUICKJS_MESH_EVENT_BOOLEAN = 32U,
    ESP32_MQUICKJS_MESH_EVENT_DUTY = 64U,
};
/* Whitelisted non-secret fields. No SDK union, callback pointer or SSID is
 * exposed. Sequence is exact within the non-reused native owner identity. */
typedef struct {
    uint32_t sequence, fields;
    int32_t id, reason;
    uint16_t layer, table_size, table_change;
    uint8_t mac[6], channel, duty;
    bool value;
} esp32_mquickjs_wifi_mesh_notice_t;
typedef struct {
    esp32_mquickjs_wifi_mesh_token_t token;
    const char *stage, *cleanup_stage;
    esp_err_t error, cleanup_error, query_error;
    uint32_t events, dropped_events, malformed_events, queued_events;
    uint32_t sends, receives, reserved_bytes;
    bool busy, closing, init_attempted, initialized, start_attempted, started;
    bool native_retired, restart_required, parent_known, parent_connected;
    bool voting, to_ds_reachable, native_snapshot_valid, root, type_known, native_start_seen, native_stop_seen;
    uint16_t layer;
    uint8_t parent[6], channel;
    mesh_type_t type;
    int nodes, routes;
    mesh_tx_pending_t tx_pending;
    mesh_rx_pending_t rx_pending;
    uint8_t router_bssid[6];
    esp32_mquickjs_wifi_mesh_scan_status_t scan;
} esp32_mquickjs_wifi_mesh_status_t;

typedef enum {
    ESP32_MQUICKJS_MESH_TO_ROOT,
    ESP32_MQUICKJS_MESH_TO_PEER,
    ESP32_MQUICKJS_MESH_TO_DS,
    ESP32_MQUICKJS_MESH_FROM_DS,
    ESP32_MQUICKJS_MESH_TO_GROUP,
    ESP32_MQUICKJS_MESH_BROADCAST,
} esp32_mquickjs_wifi_mesh_destination_t;
typedef struct {
    esp32_mquickjs_wifi_mesh_destination_t destination;
    mesh_addr_t address;
    mesh_proto_t protocol;
    bool reliable, drop_on_root_change;
    const uint8_t *bytes;
    size_t length;
} esp32_mquickjs_wifi_mesh_send_t;
typedef struct {
    uint32_t sequence;
    mesh_addr_t from, to;
    uint16_t length;
    int flags;
    mesh_proto_t protocol;
    mesh_tos_t service;
    bool to_ds, has_ds_address;
} esp32_mquickjs_wifi_mesh_message_t;

typedef enum {
    ESP32_MQUICKJS_MESH_ROUTING_TABLE, ESP32_MQUICKJS_MESH_GROUP_LIST,
    ESP32_MQUICKJS_MESH_GROUP_ADD, ESP32_MQUICKJS_MESH_GROUP_DELETE,
    ESP32_MQUICKJS_MESH_TODS_STATE, ESP32_MQUICKJS_MESH_CONNECT,
    ESP32_MQUICKJS_MESH_DISCONNECT, ESP32_MQUICKJS_MESH_FLUSH_UPSTREAM,
    ESP32_MQUICKJS_MESH_CONFIGURATION, ESP32_MQUICKJS_MESH_SET_ROUTER,
    ESP32_MQUICKJS_MESH_SET_ID, ESP32_MQUICKJS_MESH_SET_TYPE,
    ESP32_MQUICKJS_MESH_SELF_ORGANIZED, ESP32_MQUICKJS_MESH_FIXED_ROOT,
    ESP32_MQUICKJS_MESH_ROOT_CONFLICTS, ESP32_MQUICKJS_MESH_ASSOC_EXPIRY,
    ESP32_MQUICKJS_MESH_ROOT_HEALING, ESP32_MQUICKJS_MESH_IE_ENCRYPTION,
    ESP32_MQUICKJS_MESH_WAIVE_ROOT, ESP32_MQUICKJS_MESH_SWITCH_CHANNEL,
    ESP32_MQUICKJS_MESH_DEVICE_DUTY, ESP32_MQUICKJS_MESH_NETWORK_DUTY,
    ESP32_MQUICKJS_MESH_SIGNAL_DUTY, ESP32_MQUICKJS_MESH_SUBNET,
    ESP32_MQUICKJS_MESH_HAS_GROUP, ESP32_MQUICKJS_MESH_UPSTREAM_CAPACITY,
    ESP32_MQUICKJS_MESH_POWER_STATUS, ESP32_MQUICKJS_MESH_TSF_TIME,
    ESP32_MQUICKJS_MESH_SET_PARENT, ESP32_MQUICKJS_MESH_SCAN,
    ESP32_MQUICKJS_MESH_SCAN_NEXT, ESP32_MQUICKJS_MESH_SCAN_FLUSH,
    ESP32_MQUICKJS_MESH_CONTROL_COUNT,
} esp32_mquickjs_wifi_mesh_control_kind_t;
/* Only extended commands allocate this storage. Owned by the job, never a JS
 * pointer; result remains immutable after publication until its last release. */
typedef struct {
    const char *stage;
    uint32_t completed_steps;
    bool include_secrets, enabled, select_parent, has_address, result;
    mesh_addr_t address;
    int number, duration, rule, duty_type;
    uint32_t sequence;
    int64_t time_us;
    uint32_t scan_identity;
    esp32_mquickjs_wifi_mesh_scan_record_t *scan_record;
    union {
        struct {
            esp32_mquickjs_wifi_mesh_config_t config;
            int assoc_expiry, root_healing;
            bool root_conflicts;
        } configuration;
        mesh_router_t router;
        mesh_vote_t vote;
        struct { bool enabled, active; int device, device_type, network, duration, type, rule, running; } power;
        struct { char key[64]; uint8_t length; } encryption;
        wifi_config_t parent;
        esp32_mquickjs_wifi_mesh_scan_config_t scan;
        esp32_mquickjs_wifi_mesh_scan_status_t scan_result;
    } data;
} esp32_mquickjs_wifi_mesh_control_data_t;
typedef struct {
    esp32_mquickjs_wifi_mesh_control_kind_t kind;
    mesh_addr_t *addresses;
    uint16_t capacity, count;
    bool value;
    esp32_mquickjs_wifi_mesh_control_data_t *detail;
} esp32_mquickjs_wifi_mesh_control_t;

/* Native operation core, not a second Radio. The caller must reserve exclusive
 * Radio ownership before reserve/start, provide started APSTA in RAM storage,
 * and retain that exclusion through native close, physical STOP, netif retirement
 * and config restoration before release. No call here changes Radio ownership.
 * Blocking SDK work runs on a native worker, never on the JS/event-loop task.
 * A Future ending does not cancel an in-flight send or release its input span. */
esp_err_t esp32_mquickjs_wifi_mesh_validate_config(const esp32_mquickjs_wifi_mesh_config_t *config);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_reserve(uint32_t generation,
    const esp32_mquickjs_wifi_mesh_config_t *config, esp32_mquickjs_wifi_mesh_token_t *token);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_start(const esp32_mquickjs_wifi_mesh_token_t *token);
/* Seal admission without deinitializing. Safe during a worker's SDK call;
 * native input remains pinned until that call returns and close retires it. */
esp_err_t esp32_mquickjs_wifi_mesh_sdk_seal(const esp32_mquickjs_wifi_mesh_token_t *token);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_close(const esp32_mquickjs_wifi_mesh_token_t *token);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_release(esp32_mquickjs_wifi_mesh_token_t *token);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_status(const esp32_mquickjs_wifi_mesh_token_t *token,
    esp32_mquickjs_wifi_mesh_status_t *status);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_inspect(const esp32_mquickjs_wifi_mesh_token_t *token);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_send(const esp32_mquickjs_wifi_mesh_token_t *token,
    const esp32_mquickjs_wifi_mesh_send_t *send);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_control(const esp32_mquickjs_wifi_mesh_token_t *token,
    esp32_mquickjs_wifi_mesh_control_t *control);
/* Nonblocking SDK receive into two bounded retained slots (self and ToDS).
 * Copy does not consume. Only commit after complete public conversion; an OOM
 * or cancelled waiter leaves the same packet available. No borrowed SDK bytes. */
esp_err_t esp32_mquickjs_wifi_mesh_sdk_receive(const esp32_mquickjs_wifi_mesh_token_t *token, bool to_ds);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_message_copy(const esp32_mquickjs_wifi_mesh_token_t *token,
    bool to_ds, esp32_mquickjs_wifi_mesh_message_t *message, uint8_t *bytes, size_t capacity);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_message_commit(const esp32_mquickjs_wifi_mesh_token_t *token,
    bool to_ds, uint32_t sequence);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_event_copy(const esp32_mquickjs_wifi_mesh_token_t *token,
    esp32_mquickjs_wifi_mesh_notice_t *notice);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_event_commit(const esp32_mquickjs_wifi_mesh_token_t *token,
    uint32_t sequence);
esp_err_t esp32_mquickjs_wifi_mesh_sdk_scan_commit(const esp32_mquickjs_wifi_mesh_token_t *token,
    uint32_t scan_identity, uint32_t sequence);
/* Captures terminal/control state before the SDK posts its lossy observation
 * event. No heap, SDK calls or user callback while holding the record lock. */
void esp32_mquickjs_wifi_mesh_sdk_capture(int32_t id, const void *data, size_t length);
#endif
