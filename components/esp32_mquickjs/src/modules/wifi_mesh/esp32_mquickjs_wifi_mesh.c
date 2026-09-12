#include "esp32_mquickjs_wifi_mesh.h"
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
#include "esp32_mquickjs_wifi_mesh_session.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_wireless_core.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
#include <limits.h>
#include <math.h>
#include "esp32_mquickjs_wifi.h"
#include "esp_mesh_internal.h"

typedef enum {
    MESH_READY, MESH_CLOSE, MESH_RECOVER, MESH_RECEIVE, MESH_SEND,
    MESH_ROUTES, MESH_GROUPS, MESH_ADD_GROUPS, MESH_REMOVE_GROUPS,
    MESH_SET_TODS, MESH_CONNECT, MESH_DISCONNECT, MESH_FLUSH,
    MESH_CONFIGURATION, MESH_SET_ROUTER, MESH_SET_ID, MESH_SET_TYPE, MESH_SELF_ORGANIZED, MESH_FIXED_ROOT, MESH_ROOT_CONFLICTS, MESH_ASSOC_EXPIRY, MESH_ROOT_HEALING, MESH_IE_ENCRYPTION, MESH_WAIVE_ROOT, MESH_SWITCH_CHANNEL, MESH_DEVICE_DUTY, MESH_NETWORK_DUTY, MESH_SIGNAL_DUTY, MESH_SUBNET, MESH_HAS_GROUP, MESH_UPSTREAM_CAPACITY, MESH_POWER_STATUS, MESH_TSF_TIME, MESH_SET_PARENT, MESH_SCAN, MESH_SCAN_NEXT, MESH_SCAN_FLUSH,
} mesh_operation_t;
static const char *const mesh_methods[] = {"WiFiMeshSession.ready", "WiFiMeshSession.close", "WiFiMeshSession.recover",
    "WiFiMeshSession.receive", "WiFiMeshSession.send", "WiFiMeshSession.routingTable", "WiFiMeshSession.groups",
    "WiFiMeshSession.addGroups", "WiFiMeshSession.removeGroups", "WiFiMeshSession.setToDSState",
    "WiFiMeshSession.connect", "WiFiMeshSession.disconnect", "WiFiMeshSession.flushUpstream",
    "WiFiMeshSession.configuration", "WiFiMeshSession.setRouter", "WiFiMeshSession.setMeshId", "WiFiMeshSession.setType", "WiFiMeshSession.setSelfOrganized", "WiFiMeshSession.setFixedRoot", "WiFiMeshSession.setRootConflicts", "WiFiMeshSession.setAssociationExpiry", "WiFiMeshSession.setRootHealingDelay", "WiFiMeshSession.setIEEncryption", "WiFiMeshSession.waiveRoot", "WiFiMeshSession.switchChannel", "WiFiMeshSession.setDeviceDuty", "WiFiMeshSession.setNetworkDuty", "WiFiMeshSession.signalDuty", "WiFiMeshSession.subnet", "WiFiMeshSession.hasGroup", "WiFiMeshSession.upstreamCapacity", "WiFiMeshSession.powerStatus", "WiFiMeshSession.tsfTime", "WiFiMeshSession.setParent", "WiFiMeshSession.scan", "WiFiMeshSession.receiveScan", "WiFiMeshSession.flushScan"};
struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_mesh_session_t *session;
    esp32_mquickjs_wifi_mesh_job_t *job;
    esp32_mquickjs_wifi_mesh_message_t message;
    uint8_t *bytes;
    uint32_t timeout_ms, read_identity;
    esp_err_t error;
    mesh_operation_t operation;
    bool cancelled, to_ds;
};
#define SET(object, key, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, key, value)) goto fail; } while (0)

static JSValue mesh_address(JSContext *ctx, const uint8_t *bytes, size_t length)
{
    JSGCRef ref; JSValue *array = JS_PushGCRef(ctx, &ref);
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    for (size_t i = 0; i < length; ++i)
        if (JS_IsException(JS_SetPropertyUint32(ctx, *array, i, JS_NewInt32(ctx, bytes[i])))) goto fail;
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static bool mesh_bytes(JSContext *ctx, JSValue value, uint8_t *bytes, size_t maximum, uint16_t *length)
{
    esp32_mquickjs_byte_source_t source = {0}; uint8_t *owned = NULL; JSValue error = JS_UNDEFINED;
    if (!esp32_mquickjs_get_wireless_byte_source("wifi.mesh", ctx, value, "Mesh bytes", &source, &owned, &error)) {
        if (!JS_HasException(ctx)) JS_Throw(ctx, error);
        return false;
    }
    bool valid = source.length <= maximum;
    if (valid) { *length = source.length; if (*length) memcpy(bytes, source.data, *length); }
    esp32_mquickjs_release_byte_source(owned);
    if (!valid) JS_ThrowTypeError(ctx, "Mesh byte length exceeds limit");
    return valid;
}
static bool mesh_string(JSContext *ctx, JSValue value, void *output, size_t minimum, size_t maximum, size_t *length)
{
    if (!JS_IsString(ctx, value)) return false;
    JSCStringBuf buffer;
    size_t size = 0;
    const char *bytes = JS_ToCStringLen(ctx, &size, value, &buffer);
    if (!bytes || size < minimum || size > maximum || memchr(bytes, 0, size)) return false;
    if (size) memcpy(output, bytes, size);
    if (length) *length = size;
    return true;
}
static bool mesh_percentage(JSContext *ctx, JSValue value, float *out)
{
    double number;
    if (!JS_IsNumber(ctx, value) || JS_ToNumber(ctx, &number, value) || !isfinite(number) || number <= 0 || number > 1)
        return false;
    *out = (float)number;
    return *out > 0;
}
static esp32_mquickjs_wifi_mesh_session_t *mesh_receiver(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_MESH_SESSION) {
        JS_ThrowTypeError(ctx, "expected WiFiMeshSession"); return NULL;
    }
    esp32_mquickjs_wifi_mesh_session_t *session = JS_GetOpaque(ctx, value);
    if (!session) JS_ThrowReferenceError(ctx, "invalid WiFiMeshSession");
    return session;
}
void js_wifi_mesh_finalizer(JSContext *ctx, void *opaque)
{ (void)ctx; esp32_mquickjs_wifi_mesh_session_release(opaque); }
JSValue js_wifi_mesh_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ (void)self; (void)argc; (void)argv; return JS_ThrowTypeError(ctx, "use wifi.mesh.open()"); }

static JSValue mesh_pending_to_js(JSContext *ctx, const esp32_mquickjs_wifi_mesh_status_t *n, bool tx)
{
    if (!n->native_snapshot_valid) return JS_NULL;
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    if (tx) {
        SET(result, "toParent", JS_NewInt32(ctx, n->tx_pending.to_parent));
        SET(result, "toParentP2P", JS_NewInt32(ctx, n->tx_pending.to_parent_p2p));
        SET(result, "toChild", JS_NewInt32(ctx, n->tx_pending.to_child));
        SET(result, "toChildP2P", JS_NewInt32(ctx, n->tx_pending.to_child_p2p));
        SET(result, "management", JS_NewInt32(ctx, n->tx_pending.mgmt));
        SET(result, "broadcast", JS_NewInt32(ctx, n->tx_pending.broadcast));
    } else {
        SET(result, "toDS", JS_NewInt32(ctx, n->rx_pending.toDS));
        SET(result, "toSelf", JS_NewInt32(ctx, n->rx_pending.toSelf));
    }
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static JSValue mesh_scan_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_mesh_scan_status_t *s)
{
    if (!s->identity) return JS_NULL;
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref); *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "identity", JS_NewUint32(ctx, s->identity));
    SET(result, "running", JS_NewBool(s->running)); SET(result, "completed", JS_NewBool(s->completed));
    SET(result, "uncertain", JS_NewBool(s->uncertain)); SET(result, "retained", JS_NewBool(s->retained));
    SET(result, "total", JS_NewInt32(ctx, s->total)); SET(result, "remaining", JS_NewInt32(ctx, s->remaining));
    SET(result, "error", s->error ? JS_NewInt32(ctx, s->error) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static JSValue mesh_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_mesh_session_status_t *s)
{
    const esp32_mquickjs_wifi_mesh_status_t *n = &s->native.native;
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    SET(result, "identity", JS_NewUint32(ctx, s->identity));
    SET(result, "state", JS_NewString(ctx, s->retired ? "closed" : s->closing ? "closing" : s->ready ? "ready" : "opening"));
    SET(result, "ready", JS_NewBool(s->ready));
    SET(result, "workerBusy", JS_NewBool(s->worker_busy));
    SET(result, "cleanupPending", JS_NewBool(s->closing && !s->retired));
    SET(result, "radioGeneration", s->native.token.identity ? JS_NewUint32(ctx, s->native.token.generation) : JS_NULL);
    SET(result, "nativeIdentity", n->token.identity ? JS_NewUint32(ctx, n->token.identity) : JS_NULL);
    SET(result, "parentKnown", JS_NewBool(n->parent_known));
    SET(result, "parentConnected", JS_NewBool(n->parent_known && n->parent_connected && !s->closing));
    SET(result, "parent", n->parent_known && n->parent_connected ? mesh_address(ctx, n->parent, 6) : JS_NULL);
    SET(result, "channel", n->channel ? JS_NewInt32(ctx, n->channel) : JS_NULL);
    SET(result, "layer", n->parent_known && n->parent_connected ? JS_NewInt32(ctx, n->layer) : JS_NULL);
    SET(result, "routerBssid", n->native_snapshot_valid ? mesh_address(ctx, n->router_bssid, 6) : JS_NULL);
    SET(result, "queryError", n->query_error ? JS_NewInt32(ctx, n->query_error) : JS_NULL);
    SET(result, "scan", s->retired ? JS_NULL : mesh_scan_status_to_js(ctx, &n->scan));
    SET(result, "snapshotValid", JS_NewBool(n->native_snapshot_valid));
    SET(result, "root", n->native_snapshot_valid ? JS_NewBool(n->root) : JS_NULL);
    static const char *const types[] = {"idle", "root", "node", "leaf", "station"};
    SET(result, "type", n->native_snapshot_valid && n->type_known && (unsigned)n->type < 5 ?
        JS_NewString(ctx, types[n->type]) : JS_NULL);
    SET(result, "nodes", n->native_snapshot_valid ? JS_NewInt32(ctx, n->nodes) : JS_NULL);
    SET(result, "routes", n->native_snapshot_valid ? JS_NewInt32(ctx, n->routes) : JS_NULL);
    SET(result, "txPending", mesh_pending_to_js(ctx, n, true));
    SET(result, "rxPending", mesh_pending_to_js(ctx, n, false));
    SET(result, "voting", JS_NewBool(n->voting));
    SET(result, "toDSReachable", JS_NewBool(n->to_ds_reachable && !s->closing));
    SET(result, "dhcpStarted", JS_NewBool(s->native.dhcp_started));
    SET(result, "ipReady", JS_NewBool(s->native.ip_ready && !s->closing));
    SET(result, "ipv4", s->native.ip_ready && !s->closing ?
        mesh_address(ctx, (const uint8_t *)&s->native.ip.ip.addr, 4) : JS_NULL);
    SET(result, "nativeRetired", JS_NewBool(n->native_retired));
    SET(result, "stopped", JS_NewBool(s->native.stopped));
    SET(result, "netifsRetired", JS_NewBool(s->native.netifs_retired));
    SET(result, "restored", JS_NewBool(s->native.restored));
    SET(result, "restartRequired", JS_NewBool(s->native.restart_required));
    SET(result, "recoveryPending", JS_NewBool(s->recovery_pending));
    SET(result, "recoveryAttempts", JS_NewUint32(ctx, s->recovery_attempts));
    SET(result, "timedOut", JS_NewBool(s->timed_out));
    SET(result, "sends", JS_NewUint32(ctx, n->sends));
    SET(result, "receives", JS_NewUint32(ctx, n->receives));
    SET(result, "events", JS_NewUint32(ctx, n->events));
    SET(result, "droppedEvents", JS_NewUint32(ctx, n->dropped_events));
    SET(result, "malformedEvents", JS_NewUint32(ctx, n->malformed_events));
    SET(result, "reservedBytes", JS_NewUint32(ctx, s->reserved_bytes));
    SET(result, "error", s->error ? JS_NewInt32(ctx, s->error) : JS_NULL);
    SET(result, "cleanupError", s->cleanup_error ? JS_NewInt32(ctx, s->cleanup_error) : JS_NULL);
    SET(result, "networkError", s->native.network_error ? JS_NewInt32(ctx, s->native.network_error) : JS_NULL);
    SET(result, "stage", s->stage ? JS_NewString(ctx, s->stage) : JS_NULL);
    SET(result, "nativeStage", n->stage ? JS_NewString(ctx, n->stage) : JS_NULL);
    SET(result, "cleanupStage", s->native.cleanup_stage ? JS_NewString(ctx, s->native.cleanup_stage) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static JSValue mesh_error(JSContext *ctx, const char *operation, esp32_mquickjs_wifi_mesh_session_t *session,
    esp_err_t error, bool timeout)
{
    esp32_mquickjs_wifi_mesh_session_status_t status = {0};
    if (session) esp32_mquickjs_wifi_mesh_session_status(session, &status);
    if (!error) error = status.error ? status.error : status.cleanup_error ? status.cleanup_error : ESP_ERR_INVALID_STATE;
    JSGCRef ref; JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = mesh_status_to_js(ctx, &status); if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "waitTimedOut", JS_NewBool(timeout));
    (void)esp32_mquickjs_throw_native_error(ctx, timeout || error == ESP_ERR_TIMEOUT ? "WIFI_MESH_TIMEOUT" :
        status.closing ? "WIFI_MESH_CLOSED" : "WIFI_MESH_FAILED", operation,
        "Mesh operation did not complete; inspect session status", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

typedef struct { esp32_mquickjs_wifi_mesh_config_t config; uint32_t timeout_ms; bool allow_ap_restart; } mesh_open_options_t;
static bool mesh_open_options(JSContext *ctx, JSGCRef *input, mesh_open_options_t *out)
{
    static const char *const keys[] = {"meshId", "routerSsid", "routerPassword", "routerBssid", "apPassword", "apAuthentication",
        "channel", "topology", "type", "maxLayer", "capacity", "receiveQueue", "sendBlockMs", "maxConnections",
        "nonMeshConnections", "fixedRoot", "selfOrganized", "powerSave", "encryptIE", "ieKey", "allowApRestart", "timeoutMs", "allowChannelSwitch", "allowRouterSwitch", "votePercentage"};
    if (!esp32_mquickjs_validate_plain_options(ctx, input->val, "wifi.mesh.open", keys, sizeof(keys)/sizeof(*keys))) return false;
    out->config = (esp32_mquickjs_wifi_mesh_config_t){.network = MESH_INIT_CONFIG_DEFAULT(),
        .topology = MESH_TOPO_TREE, .type = MESH_IDLE, .ap_authmode = WIFI_AUTH_WPA2_PSK,
        .max_layer = 6, .capacity = 32, .receive_queue = 16, .send_block_ms = 1000, .self_organized = true, .vote_percentage = 0.9f};
    out->config.network.mesh_ap.max_connection = 4;
    out->timeout_ms = 30000;
    JSGCRef ref; JSValue *field = JS_PushGCRef(ctx, &ref);
    uint32_t number; uint16_t length; size_t string_length, choice;
    bool ok = false;
#define FIELD(key) do { *field = JS_GetPropertyStr(ctx, input->val, key); if (JS_IsException(*field)) goto fail; } while (0)
#define NUMBER(key, min, max, target) do { FIELD(key); if (!JS_IsUndefined(*field)) { \
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, min, max, &number)) { goto invalid; } target = number; } } while (0)
#define BOOLEAN(key, target) do { FIELD(key); if (!JS_IsUndefined(*field)) { if (!JS_IsBool(*field)) goto invalid; target = *field == JS_TRUE; } } while (0)
    FIELD("meshId"); if (!mesh_bytes(ctx, *field, out->config.network.mesh_id.addr, 6, &length) || length != 6) goto invalid;
    FIELD("routerSsid"); if (!mesh_string(ctx, *field, out->config.network.router.ssid, 1, 32, &string_length)) goto invalid;
    out->config.network.router.ssid_len = string_length;
    FIELD("routerPassword"); if (!JS_IsUndefined(*field) && !mesh_string(ctx, *field, out->config.network.router.password, 0, 64, NULL)) goto invalid;
    FIELD("routerBssid"); if (!JS_IsUndefined(*field) && (!mesh_bytes(ctx, *field, out->config.network.router.bssid, 6, &length) || length != 6)) goto invalid;
    FIELD("apPassword"); if (!mesh_string(ctx, *field, out->config.network.mesh_ap.password, 0, 64, NULL)) goto invalid;
    FIELD("apAuthentication"); if (!JS_IsUndefined(*field)) {
        static const char *const values[] = {"open", "wpa-psk", "wpa2-psk", "wpa-wpa2-psk"};
        static const wifi_auth_mode_t modes[] = {WIFI_AUTH_OPEN, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK};
        if (!esp32_mquickjs_value_to_enum(ctx, *field, values, 4, &choice)) goto invalid;
        out->config.ap_authmode = modes[choice];
    }
    FIELD("topology"); if (!JS_IsUndefined(*field)) {
        static const char *const values[] = {"tree", "chain"};
        if (!esp32_mquickjs_value_to_enum(ctx, *field, values, 2, &choice)) goto invalid;
        out->config.topology = choice ? MESH_TOPO_CHAIN : MESH_TOPO_TREE;
    }
    FIELD("type"); if (!JS_IsUndefined(*field)) {
        static const char *const values[] = {"idle", "root", "node", "leaf", "station"};
        if (!esp32_mquickjs_value_to_enum(ctx, *field, values, 5, &choice)) goto invalid;
        out->config.type = (mesh_type_t)choice;
    }
    NUMBER("channel", 0, 14, out->config.network.channel);
    NUMBER("maxLayer", 1, 1000, out->config.max_layer);
    NUMBER("capacity", 1, 1000, out->config.capacity);
    NUMBER("receiveQueue", 16, 128, out->config.receive_queue);
    NUMBER("sendBlockMs", 1, 60000, out->config.send_block_ms);
    NUMBER("maxConnections", 1, 10, out->config.network.mesh_ap.max_connection);
    NUMBER("nonMeshConnections", 0, 9, out->config.network.mesh_ap.nonmesh_max_connection);
    NUMBER("timeoutMs", 1, ESP32_MQUICKJS_MESH_MAX_WAIT_MS, out->timeout_ms);
    BOOLEAN("allowChannelSwitch", out->config.network.allow_channel_switch);
    BOOLEAN("allowRouterSwitch", out->config.network.router.allow_router_switch);
    FIELD("votePercentage"); if (!JS_IsUndefined(*field) && !mesh_percentage(ctx, *field, &out->config.vote_percentage)) goto invalid;
    BOOLEAN("fixedRoot", out->config.fixed_root);
    BOOLEAN("selfOrganized", out->config.self_organized);
    BOOLEAN("powerSave", out->config.power_save);
    BOOLEAN("allowApRestart", out->allow_ap_restart);
    FIELD("encryptIE"); if (!JS_IsBool(*field)) goto invalid;
    out->config.encrypt_ie = *field == JS_TRUE;
    FIELD("ieKey");
    if (out->config.encrypt_ie) {
        if (!mesh_string(ctx, *field, out->config.ie_key, 8, 64, &string_length)) goto invalid;
        out->config.ie_key_length = string_length;
    } else if (!JS_IsUndefined(*field)) goto invalid;
    if (esp32_mquickjs_wifi_mesh_validate_config(&out->config)) goto invalid;
    ok = true; goto fail;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid Mesh options; encryption policy must be explicit");
fail:
    JS_PopGCRef(ctx, &ref); return ok;
#undef FIELD
#undef NUMBER
#undef BOOLEAN
}

JSValue js_wifi_mesh_open(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.mesh.open requires options");
    JSGCRef input_ref, result_ref; JSValue *input = JS_PushGCRef(ctx, &input_ref), *result = JS_PushGCRef(ctx, &result_ref);
    *input = argv[0];
    mesh_open_options_t *options = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, sizeof(*options),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    esp32_mquickjs_wifi_mesh_session_t *session = NULL;
    if (!options) { JS_ThrowOutOfMemory(ctx); goto fail; }
    if (!mesh_open_options(ctx, &input_ref, options)) goto fail;
    esp_err_t error = esp32_mquickjs_wifi_mesh_session_create(&options->config, options->allow_ap_restart, options->timeout_ms, &session);
    if (error) { mesh_error(ctx, "wifi.mesh.open", NULL, error, false); goto fail; }
    *result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_MESH_SESSION);
    if (JS_IsException(*result)) goto fail;
    JS_SetOpaque(ctx, *result, session);
    error = esp32_mquickjs_wifi_mesh_session_activate(session);
    if (error) { JS_SetOpaque(ctx, *result, NULL); mesh_error(ctx, "wifi.mesh.open", session, error, false); goto fail; }
    esp32_mquickjs_wireless_secure_zero(options, sizeof(*options)); esp32_mquickjs_memory_payload_free(options);
    (void)esp32_mquickjs_wifi_mesh_service();
    JSValue value = JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &input_ref); return value;
fail:
    if (options) { esp32_mquickjs_wireless_secure_zero(options, sizeof(*options)); esp32_mquickjs_memory_payload_free(options); }
    esp32_mquickjs_wifi_mesh_session_release(session);
    JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &input_ref); return JS_EXCEPTION;
}
JSValue js_wifi_mesh_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv; if (argc) return JS_ThrowTypeError(ctx, "Mesh status takes no arguments");
    esp32_mquickjs_wifi_mesh_session_t *session = mesh_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    esp32_mquickjs_wifi_mesh_session_status_t status;
    esp32_mquickjs_wifi_mesh_session_status(session, &status); return mesh_status_to_js(ctx, &status);
}
JSValue js_wifi_mesh_cancel(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv; if (argc) return JS_ThrowTypeError(ctx, "Mesh cancel takes no arguments");
    esp32_mquickjs_wifi_mesh_session_t *session = mesh_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    esp32_mquickjs_wifi_mesh_session_close(session, false);
    (void)esp32_mquickjs_wifi_mesh_service(); return JS_UNDEFINED;
}
JSValue js_wifi_mesh_capabilities(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv; if (argc) return JS_ThrowTypeError(ctx, "Mesh capabilities takes no arguments");
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref); *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "apiVersion", JS_NewString(ctx, "wifi-mesh/1"));
    SET(result, "stability", JS_NewString(ctx, "candidate"));
    SET(result, "radioOwnership", JS_NewString(ctx, "exclusive"));
    SET(result, "band", JS_NewString(ctx, "2.4ghz"));
    SET(result, "maxSessions", JS_NewInt32(ctx, ESP32_MQUICKJS_MESH_HANDLES));
    SET(result, "maxActiveSessions", JS_NewInt32(ctx, 1));
    SET(result, "maxPendingCommands", JS_NewInt32(ctx, 1));
    SET(result, "maxCommandHandles", JS_NewInt32(ctx, ESP32_MQUICKJS_MESH_JOBS));
    SET(result, "maxPayloadBytes", JS_NewInt32(ctx, MESH_MPS));
    SET(result, "maxRoutingEntries", JS_NewInt32(ctx, ESP32_MQUICKJS_MESH_MAX_NODES));
    SET(result, "maxGroupEntries", JS_NewInt32(ctx, ESP32_MQUICKJS_MESH_MAX_GROUPS));
    SET(result, "maxScanIEBytes", JS_NewInt32(ctx, ESP32_MQUICKJS_MESH_SCAN_IE_BYTES));
    SET(result, "maxTimeoutMs", JS_NewInt32(ctx, ESP32_MQUICKJS_MESH_MAX_WAIT_MS));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

#include "esp32_mquickjs_wifi_mesh_manual_options.inc"
#include "esp32_mquickjs_wifi_mesh_control_options.inc"
#include "esp32_mquickjs_wifi_mesh_future.inc"
#include "esp32_mquickjs_wifi_mesh_watch.inc"

bool esp32_mquickjs_init_wifi_mesh_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, object_ref, proto_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *proto = JS_PushGCRef(ctx, &proto_ref), *method = JS_PushGCRef(ctx, &method_ref);
    bool ok = false;
    *global = JS_GetGlobalObject(ctx); if (JS_IsException(*global)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "WiFiMeshSession"); if (JS_IsException(*object)) goto done;
    *proto = JS_GetPropertyStr(ctx, *object, "prototype"); if (JS_IsException(*proto)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "ready");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_ready_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "close");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_close_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "recover");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_recover_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "receive");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_receive_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "send");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_send_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "routingTable");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_routing_table_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "groups");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_groups_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "addGroups");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_add_groups_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "removeGroups");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_remove_groups_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setToDSState");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_tods_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "connect");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_connect_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "disconnect");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_disconnect_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "flushUpstream");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_flush_upstream_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "configuration");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_configuration_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setRouter");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_router_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setMeshId");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_mesh_id_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setType");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_type_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setSelfOrganized");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_self_organized_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setFixedRoot");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_fixed_root_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setRootConflicts");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_root_conflicts_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setAssociationExpiry");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_association_expiry_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setRootHealingDelay");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_root_healing_delay_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setIEEncryption");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_ie_encryption_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "waiveRoot");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_waive_root_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "switchChannel");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_switch_channel_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setDeviceDuty");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_device_duty_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setNetworkDuty");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_network_duty_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "signalDuty");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_signal_duty_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "subnet");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_subnet_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "hasGroup");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_has_group_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "upstreamCapacity");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_upstream_capacity_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "powerStatus");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_power_status_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "tsfTime");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_tsf_time_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "setParent");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_set_parent_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "scan");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_scan_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "receiveScan");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_receive_scan_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "flushScan");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_mesh_flush_scan_driver)) goto done;
    ok = true;
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register Mesh runtime");
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &proto_ref);
    JS_PopGCRef(ctx, &object_ref); JS_PopGCRef(ctx, &global_ref); return ok;
}
#endif
