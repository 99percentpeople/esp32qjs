#include "esp32_mquickjs_wifi_nan.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
#include "esp32_mquickjs_wifi_nan_session.h"
#include "esp32_mquickjs_wifi_nan_tx.h"
#include "esp32_mquickjs_wifi_nan_discovery.h"
#include "esp32_mquickjs_wifi_nan_message.h"
#include "esp32_mquickjs_wifi_nan_path.h"
#include "esp32_mquickjs_wifi_nan_pairing.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_native_status.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp_nan.h"

typedef enum { NAN_READY, NAN_CLOSE, NAN_SERVICE_READY, NAN_SERVICE_CLOSE, NAN_SERVICE_SEND,
    NAN_PATH_RECEIVE, NAN_PATH_READY, NAN_PATH_CLOSE, NAN_PATH_RESPOND, NAN_PAIRING_READY, NAN_PAIRING_CLOSE, NAN_PAIRING_CREDENTIALS, NAN_PAIRING_RECEIVE } nan_operation_t;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
typedef struct { bool accept; uint16_t length; uint8_t ssi[ESP_WIFI_MAX_SVC_SSI_LEN]; } nan_path_response_t;
#endif

struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_nan_session_t *session;
    esp32_mquickjs_wifi_nan_discovery_t *discovery;
    esp32_mquickjs_wifi_nan_message_t *message;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    esp32_mquickjs_wifi_nan_path_t *path;
    nan_path_response_t *response;

#endif
#if CONFIG_ESP_WIFI_NAN_PAIRING
    esp32_mquickjs_wifi_nan_pairing_t *pairing;
    esp32_mquickjs_wifi_nan_credentials_t credentials;
    uint32_t credentials_revision;
    esp_err_t credentials_error;
    bool credentials_ready;
#endif
    nan_operation_t operation;
    uint32_t timeout_ms;
    bool cancelled;
};
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)

static const char *nan_operation_name(nan_operation_t operation)
{
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (operation == NAN_PATH_RECEIVE) return "WiFiNanService.receiveDataPath";
    if (operation == NAN_PATH_READY) return "WiFiNanDataPath.ready";
    if (operation == NAN_PATH_CLOSE) return "WiFiNanDataPath.close";
    if (operation == NAN_PATH_RESPOND) return "WiFiNanDataPath.respond";

#endif
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (operation == NAN_PAIRING_RECEIVE) return "WiFiNanService.receivePairing";
    if (operation == NAN_PAIRING_CREDENTIALS) return "WiFiNanService.pairingCredentials";
    if (operation == NAN_PAIRING_READY) return "WiFiNanPairing.ready";
    if (operation == NAN_PAIRING_CLOSE) return "WiFiNanPairing.close";
#endif
    if (operation == NAN_SERVICE_READY) return "WiFiNanService.ready";
    if (operation == NAN_SERVICE_CLOSE) return "WiFiNanService.close";
    if (operation == NAN_SERVICE_SEND) return "WiFiNanService.send";
    return operation == NAN_CLOSE ? "WiFiNanSession.close" : "WiFiNanSession.ready";
}

static esp32_mquickjs_wifi_nan_session_t *nan_receiver(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_NAN_SESSION) {
        JS_ThrowTypeError(ctx, "expected WiFiNanSession");
        return NULL;
    }
    esp32_mquickjs_wifi_nan_session_t *session = JS_GetOpaque(ctx, value);
    if (!session) JS_ThrowReferenceError(ctx, "invalid WiFiNanSession");
    return session;
}

void js_wifi_nan_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    /* Futures are independent native owners. The last external release asks
     * the registry to close; no JS root is needed by the worker or observer. */
    esp32_mquickjs_wifi_nan_session_release(opaque);
}

JSValue js_wifi_nan_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use wifi.nan.open()");
}

static JSValue nan_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_nan_session_status_t *s)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "identity", JS_NewUint32(ctx, s->identity));
    SET(result, "mode", JS_NewString(ctx, s->usd ? "unsynchronized" : "synchronized"));
    SET(result, "state", JS_NewString(ctx, s->retired ? "closed" : s->closing ? "closing" : s->ready ? "ready" : "opening"));
    SET(result, "operation", s->native.operation.identity ? JS_NewUint32(ctx, s->native.operation.identity) : JS_NULL);
    SET(result, "radioGeneration", s->native.operation.identity ? JS_NewUint32(ctx, s->native.operation.generation) : JS_NULL);
    SET(result, "activated", JS_NewBool(s->activated));
    SET(result, "workerBusy", JS_NewBool(s->worker_busy));
    SET(result, "ready", JS_NewBool(s->ready));
    SET(result, "closeRequested", JS_NewBool(s->closing));
    SET(result, "cleanupPending", JS_NewBool(s->closing && !s->retired));
    SET(result, "timedOut", JS_NewBool(s->timed_out));
    SET(result, "nativeStartSeen", JS_NewBool(s->native_start_seen));
    SET(result, "nativeStopSeen", JS_NewBool(s->native_stop_seen));
    SET(result, "groupManagementProtection", JS_NewBool(s->group_management_protection));
    SET(result, "startAttempted", JS_NewBool(s->native.start_attempted));
    SET(result, "startAccepted", JS_NewBool(s->native.start_accepted));
    SET(result, "stopped", JS_NewBool(s->native.stopped));
    SET(result, "nativeReset", JS_NewBool(s->native.native_reset));
    SET(result, "netifRetired", JS_NewBool(s->native.netif_retired));
    SET(result, "observerRetired", JS_NewBool(s->native.observer_retired));
    SET(result, "modeRestored", JS_NewBool(s->native.mode_restored));
    SET(result, "storageRestored", JS_NewBool(s->native.storage_restored));
    SET(result, "reservedBytes", JS_NewUint32(ctx, s->reserved_bytes));
    SET(result, "error", s->error ? JS_NewInt32(ctx, s->error) : JS_NULL);
    SET(result, "stage", s->stage ? JS_NewString(ctx, s->stage) : JS_NULL);
    SET(result, "nativeStage", s->native.stage ? JS_NewString(ctx, s->native.stage) : JS_NULL);
    SET(result, "cleanupError", s->cleanup_error ? JS_NewInt32(ctx, s->cleanup_error) : JS_NULL);
    SET(result, "cleanupStage", s->cleanup_stage ? JS_NewString(ctx, s->cleanup_stage) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}

static JSValue nan_error(JSContext *ctx, const char *operation,
    esp32_mquickjs_wifi_nan_session_t *session, esp_err_t error, bool wait_timeout)
{
    esp32_mquickjs_wifi_nan_session_status_t status = {0};
    if (session) (void)esp32_mquickjs_wifi_nan_session_status(session, &status);
    if (error == ESP_OK) error = status.error ? status.error : ESP_ERR_INVALID_STATE;
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = nan_status_to_js(ctx, &status);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "waitTimedOut", JS_NewBool(wait_timeout));
    (void)esp32_mquickjs_throw_native_error(ctx,
        wait_timeout || status.timed_out || error == ESP_ERR_TIMEOUT ? "WIFI_NAN_TIMEOUT" :
        status.closing && !status.error ? "WIFI_NAN_CLOSED" : "WIFI_NAN_FAILED",
        operation, "NAN operation did not complete; inspect session status", *details);
fail:
    JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}

static bool nan_open_options(JSContext *ctx, JSGCRef *input,
    wifi_nan_sync_config_t *config, uint32_t *timeout_ms, bool *usd)
{
    static const char *const keys[] = {
        "channel", "masterPreference", "scanTimeSeconds", "warmUpSeconds", "randomizeMac", "timeoutMs", "groupManagementProtection", "mode"
    };
    if (JS_IsUndefined(input->val)) return true;
    if (!esp32_mquickjs_validate_plain_options(ctx, input->val, "wifi.nan.open", keys, 8)) return false;
    JSGCRef ref;
    JSValue *field = JS_PushGCRef(ctx, &ref);
    uint32_t number;
    bool valid = false, sync_options = false;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, input->val, name); if (JS_IsException(*field)) goto done; } while (0)
#define INTEGER(name, minimum, maximum, target) do { \
    FIELD(name); \
    if (!JS_IsUndefined(*field)) { \
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, minimum, maximum, &number)) goto invalid; \
        target = number; \
        if (strcmp(name, "timeoutMs")) sync_options = true; \
    } \
} while (0)
    FIELD("mode");
    if (!JS_IsUndefined(*field)) {
        static const char *const modes[] = {"synchronized", "unsynchronized"};
        size_t mode = 0;
        if (!esp32_mquickjs_value_to_enum(ctx, *field, modes, 2, &mode)) goto invalid;
        *usd = mode == 1;
    }
    INTEGER("channel", 1, UINT8_MAX, config->op_channel);
    INTEGER("masterPreference", 0, UINT8_MAX, config->master_pref);
    INTEGER("scanTimeSeconds", 0, UINT8_MAX, config->scan_time);
    INTEGER("warmUpSeconds", 0, UINT16_MAX, config->warm_up_sec);
    INTEGER("timeoutMs", 1, ESP32_MQUICKJS_NAN_MAX_STARTUP_MS, *timeout_ms);
    FIELD("randomizeMac");
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        config->disable_random_mac = *field == JS_FALSE;
        sync_options = true;
    }
    FIELD("groupManagementProtection");
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        config->group_mgmt_prot = *field == JS_TRUE;
        sync_options = true;
    }
    if (*usd && sync_options) goto invalid;
    valid = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid NAN options");
done:
    JS_PopGCRef(ctx, &ref);
    return valid;
#undef INTEGER
#undef FIELD
}

JSValue js_wifi_nan_open(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    if (argc > 1) return JS_ThrowTypeError(ctx, "wifi.nan.open expects optional options");
    JSGCRef options_ref, result_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *result = JS_PushGCRef(ctx, &result_ref);
    *options = argc ? argv[0] : JS_UNDEFINED;
    uint32_t timeout_ms = 10000;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    wifi_nan_sync_config_t config = WIFI_NAN_SYNC_CONFIG_DEFAULT();
    bool usd = false;
#else
    wifi_nan_sync_config_t config = {.op_channel = 6};
    bool usd = true;
#endif
    esp32_mquickjs_wifi_nan_session_t *session = NULL;
    if (!nan_open_options(ctx, &options_ref, &config, &timeout_ms, &usd)) goto fail;
    esp_err_t error = usd ? esp32_mquickjs_wifi_nan_session_create_usd(timeout_ms, &session) :
        esp32_mquickjs_wifi_nan_session_create(&config, timeout_ms, &session);
    if (error != ESP_OK) { (void)nan_error(ctx, "wifi.nan.open", session, error, false); goto fail; }
    *result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_NAN_SESSION);
    if (JS_IsException(*result)) goto fail;
    JS_SetOpaque(ctx, *result, session);
    error = esp32_mquickjs_wifi_nan_session_activate(session);
    if (error != ESP_OK) {
        JS_SetOpaque(ctx, *result, NULL);
        (void)nan_error(ctx, "wifi.nan.open", session, error, false);
        goto fail;
    }
    /* Complete all fallible JS construction before native activation. */
    esp32_mquickjs_wireless_secure_zero(&config, sizeof(config));
    (void)esp32_mquickjs_wifi_nan_service();
    JSValue value = JS_PopGCRef(ctx, &result_ref);
    JS_PopGCRef(ctx, &options_ref);
    return value;
fail:
    esp32_mquickjs_wireless_secure_zero(&config, sizeof(config));
    esp32_mquickjs_wifi_nan_session_release(session);
    JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &options_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_nan_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "NAN status expects no arguments");
    esp32_mquickjs_wifi_nan_session_t *session = nan_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    esp32_mquickjs_wifi_nan_session_status_t status;
    (void)esp32_mquickjs_wifi_nan_session_status(session, &status);
    return nan_status_to_js(ctx, &status);
}

#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
static JSValue nan_tx_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_nan_tx_status_t *s)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "capacity", JS_NewUint32(ctx, ESP32_MQUICKJS_NAN_TX_CAPACITY));
    SET(result, "tracked", JS_NewUint32(ctx, s->tracked));
    SET(result, "unidentified", JS_NewUint32(ctx, s->unidentified));
    SET(result, "submissions", JS_NewUint32(ctx, s->submissions));
    SET(result, "rejected", JS_NewUint32(ctx, s->rejected));
    SET(result, "reservedBytes", JS_NewUint32(ctx, s->reserved_bytes));
    SET(result, "closing", JS_NewBool(s->closing));
    SET(result, "identityExhausted", JS_NewBool(s->identity_exhausted));
    SET(result, "error", s->error ? JS_NewInt32(ctx, s->error) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}

#endif

JSValue js_wifi_nan_global_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "NAN status expects no arguments");
    esp32_mquickjs_wifi_nan_global_status_t status;
    esp32_mquickjs_wifi_nan_global_status(&status);
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    esp32_mquickjs_wifi_nan_tx_status_t tx;
    esp32_mquickjs_wifi_nan_tx_status(&tx);
#endif
    esp32_mquickjs_wifi_nan_sdk_observer_status_t observer;
    esp32_mquickjs_wifi_nan_sdk_observer_status(&observer);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "active", JS_NewBool(status.active));
    SET(result, "handles", JS_NewUint32(ctx, status.handles));
    SET(result, "workers", JS_NewUint32(ctx, status.workers));
    SET(result, "serviceHandles", JS_NewUint32(ctx, status.service_handles));
    SET(result, "activeServices", JS_NewUint32(ctx, status.active_services));
    SET(result, "messageHandles", JS_NewUint32(ctx, status.message_handles));
    SET(result, "dataPathHandles", JS_NewUint32(ctx, status.path_handles));
    SET(result, "activeDataPaths", JS_NewUint32(ctx, status.active_paths));
    SET(result, "pairingHandles", JS_NewUint32(ctx, status.pairing_handles));
    SET(result, "activePairings", JS_NewUint32(ctx, status.active_pairings));
    SET(result, "runtimeClosing", JS_NewBool(status.runtime_closing));
    SET(result, "session", status.active ? nan_status_to_js(ctx, &status.session) : JS_NULL);
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    SET(result, "tx", status.active && status.session.usd ? JS_NULL : nan_tx_status_to_js(ctx, &tx));
#else
    SET(result, "tx", JS_NULL);
#endif
    SET(result, "serviceCallbacks", JS_NewUint32(ctx, observer.service_callbacks));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_nan_capabilities(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "NAN capabilities expects no arguments");
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "apiVersion", JS_NewString(ctx, "wifi-nan/1"));
    SET(result, "stability", JS_NewString(ctx, "candidate"));
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    SET(result, "synchronized", JS_TRUE);
    SET(result, "peerQueries", JS_TRUE);
    SET(result, "maxPeerRecordsPerService", JS_NewInt32(ctx, NAN_MAX_PEERS_RECORD));
#else
    SET(result, "synchronized", JS_FALSE);
    SET(result, "peerQueries", JS_FALSE);
    SET(result, "maxPeerRecordsPerService", JS_NewInt32(ctx, 0));
#endif
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    SET(result, "unsynchronized", JS_TRUE);
#else
    SET(result, "unsynchronized", JS_FALSE);
#endif
#if CONFIG_ESP_WIFI_NAN_PAIRING
    SET(result, "pinPairing", JS_TRUE);
    SET(result, "pinBootstrap", JS_TRUE);
    SET(result, "maxPendingPairingRequests", JS_NewInt32(ctx, 1));
    SET(result, "maxBootstrapPeersPerService", JS_NewInt32(ctx, ESP32_MQUICKJS_NAN_BOOTSTRAP_PEERS));
    SET(result, "cachedVerification", JS_TRUE);
    SET(result, "maxCachedPairings", JS_NewInt32(ctx, ESP32_MQUICKJS_NAN_CACHED_PAIRINGS));
    SET(result, "maxActivePairings", JS_NewInt32(ctx, 1));
    SET(result, "maxPairingHandles", JS_NewInt32(ctx, ESP32_MQUICKJS_NAN_PAIRING_HANDLES));
#else
    SET(result, "pinPairing", JS_FALSE);
    SET(result, "pinBootstrap", JS_FALSE);
    SET(result, "maxPendingPairingRequests", JS_NewInt32(ctx, 0));
    SET(result, "maxBootstrapPeersPerService", JS_NewInt32(ctx, 0));
    SET(result, "cachedVerification", JS_FALSE);
    SET(result, "maxCachedPairings", JS_NewInt32(ctx, 0));
    SET(result, "maxActivePairings", JS_NewInt32(ctx, 0));
    SET(result, "maxPairingHandles", JS_NewInt32(ctx, 0));
#endif
    SET(result, "radioOwnership", JS_NewString(ctx, "exclusive"));
    SET(result, "maxSessions", JS_NewUint32(ctx, ESP32_MQUICKJS_NAN_MAX_HANDLES));
    SET(result, "maxActiveSessions", JS_NewInt32(ctx, 1));
    SET(result, "maxStartupTimeoutMs", JS_NewUint32(ctx, ESP32_MQUICKJS_NAN_MAX_STARTUP_MS));
    SET(result, "maxServiceHandles", JS_NewUint32(ctx, ESP32_MQUICKJS_NAN_SERVICE_HANDLES));
    SET(result, "maxActiveServices", JS_NewInt32(ctx, ESP_WIFI_NAN_MAX_SVC_SUPPORTED));
    SET(result, "maxServiceSsiBytes", JS_NewInt32(ctx, ESP_WIFI_MAX_SVC_SSI_LEN));
    SET(result, "maxReceivedSsiBytes", JS_NewInt32(ctx, ESP_WIFI_MAX_FUP_SSI_LEN));
    SET(result, "maxMessageHandles", JS_NewInt32(ctx, ESP32_MQUICKJS_NAN_MESSAGE_HANDLES));
    SET(result, "maxActiveMessages", JS_NewInt32(ctx, 1));
    SET(result, "maxSentSsiBytes", JS_NewInt32(ctx, ESP_WIFI_MAX_FUP_SSI_LEN));
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    SET(result, "dataPaths", JS_TRUE);
    SET(result, "maxActiveDataPaths", JS_NewInt32(ctx, ESP_WIFI_NAN_DATAPATH_MAX_PEERS));
    SET(result, "maxDataPathHandles", JS_NewInt32(ctx, ESP32_MQUICKJS_NAN_PATH_HANDLES));
    SET(result, "incomingDataPathTimeoutMs", JS_NewInt32(ctx, 10000));
    SET(result, "vendorAttributes", JS_TRUE);
    SET(result, "maxVendorBodyBytes", JS_NewInt32(ctx, NAN_VENDOR_IE_MAX_BODY_LEN));
#else
    SET(result, "dataPaths", JS_FALSE);
    SET(result, "maxActiveDataPaths", JS_NewInt32(ctx, 0));
    SET(result, "maxDataPathHandles", JS_NewInt32(ctx, 0));
    SET(result, "incomingDataPathTimeoutMs", JS_NewInt32(ctx, 0));
    SET(result, "vendorAttributes", JS_FALSE);
    SET(result, "maxVendorBodyBytes", JS_NewInt32(ctx, 0));
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE && CONFIG_ESP_WIFI_NAN_SECURITY
    SET(result, "security", JS_TRUE);
    SET(result, "securityReason", JS_NULL);
#else
    SET(result, "security", JS_FALSE);
    SET(result, "securityReason", JS_NewString(ctx, "CONFIG_ESP_WIFI_NAN_SECURITY disabled"));
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE && CONFIG_ESP_WIFI_NAN_SECURITY
    SET(result, "maxCredentials", JS_NewInt32(ctx, ESP_WIFI_NAN_MAX_CREDS_PER_SVC));
#else
    SET(result, "maxCredentials", JS_NewInt32(ctx, 0));
#endif
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}

#include "esp32_mquickjs_wifi_nan_service_public.inc"
#include "esp32_mquickjs_wifi_nan_query_public.inc"
#include "esp32_mquickjs_wifi_nan_message_public.inc"
#if CONFIG_ESP_WIFI_NAN_PAIRING
#include "esp32_mquickjs_wifi_nan_pairing_public.inc"
#endif
static void nan_destroy(esp32_mquickjs_future_driver_state_t *state);
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
#include "esp32_mquickjs_wifi_nan_path_public.inc"
#endif

static void nan_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (!state) return;
    esp32_mquickjs_wifi_nan_session_release(state->session);
    esp32_mquickjs_wifi_nan_discovery_release(state->discovery);
    esp32_mquickjs_wifi_nan_message_release(state->message);
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (state->operation == NAN_PAIRING_RECEIVE) esp32_mquickjs_wifi_nan_pairing_unreserve(state->pairing);
    esp32_mquickjs_wifi_nan_pairing_release(state->pairing);
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (state->operation == NAN_PATH_RECEIVE) esp32_mquickjs_wifi_nan_path_unreserve(state->path);
    esp32_mquickjs_wifi_nan_path_release(state->path);
    if (state->response) {
        esp32_mquickjs_wireless_secure_zero(state->response, sizeof(*state->response));
        esp32_mquickjs_memory_payload_free(state->response);
    }

#endif
    esp32_mquickjs_memory_payload_free(state);
}

static bool nan_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out, nan_operation_t operation)
{
    *out = NULL;
    if (argc > 1) { JS_ThrowTypeError(ctx, "NAN method expects optional timeout options"); return false; }
    bool child = operation == NAN_SERVICE_READY || operation == NAN_SERVICE_CLOSE;
    bool connection = false, pairing = false;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    child = child || operation == NAN_PAIRING_CREDENTIALS || operation == NAN_PAIRING_RECEIVE;
    pairing = operation == NAN_PAIRING_READY || operation == NAN_PAIRING_CLOSE;
    esp32_mquickjs_wifi_nan_pairing_t *pair = pairing ? nan_pairing_receiver(ctx, self->val) : NULL;
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    child = child || operation == NAN_PATH_RECEIVE;
    connection = operation == NAN_PATH_READY || operation == NAN_PATH_CLOSE;
    esp32_mquickjs_wifi_nan_path_t *path = connection ? nan_path_receiver(ctx, self->val) : NULL;
#endif
    esp32_mquickjs_wifi_nan_session_t *session = child || connection || pairing ? NULL : nan_receiver(ctx, self->val);
    esp32_mquickjs_wifi_nan_discovery_t *discovery = child ? nan_service_receiver(ctx, self->val) : NULL;
    if (!session && !discovery
#if CONFIG_ESP_WIFI_NAN_PAIRING
        && !pair
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
        && !path
#endif
    ) return false;
    /* Retain before any option getter can run JS or trigger GC. */
    bool retained;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (pairing) retained = esp32_mquickjs_wifi_nan_pairing_retain(pair);
    else
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (connection) retained = esp32_mquickjs_wifi_nan_path_retain(path);
    else
#endif
        retained = child ? esp32_mquickjs_wifi_nan_discovery_retain(discovery) : esp32_mquickjs_wifi_nan_session_retain(session);
    if (!retained) {
        JS_ThrowInternalError(ctx, "NAN reference exhausted"); return false;
    }
    uint32_t timeout_ms = 10000;
    bool valid = true;
    if (argc && !JS_IsUndefined(argv[0].val)) {
        static const char *const keys[] = {"timeoutMs"};
        valid = esp32_mquickjs_validate_plain_options(ctx, argv[0].val, nan_operation_name(operation), keys, 1);
        if (valid) {
            JSGCRef ref;
            JSValue *field = JS_PushGCRef(ctx, &ref);
            *field = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
            valid = !JS_IsException(*field) && (JS_IsUndefined(*field) ||
                esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 120000, &timeout_ms));
            JS_PopGCRef(ctx, &ref);
        }
    }
    if (!valid) {
        esp32_mquickjs_wifi_nan_session_release(session);
        esp32_mquickjs_wifi_nan_discovery_release(discovery);
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
        esp32_mquickjs_wifi_nan_path_release(path);
#endif
#if CONFIG_ESP_WIFI_NAN_PAIRING
        esp32_mquickjs_wifi_nan_pairing_release(pair);
#endif
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid NAN timeout options");
        return false;
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!state) {
        esp32_mquickjs_wifi_nan_session_release(session); esp32_mquickjs_wifi_nan_discovery_release(discovery);
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
        esp32_mquickjs_wifi_nan_path_release(path);
#endif
#if CONFIG_ESP_WIFI_NAN_PAIRING
        esp32_mquickjs_wifi_nan_pairing_release(pair);
#endif
        JS_ThrowOutOfMemory(ctx); return false;
    }
    state->session = session; state->operation = operation; state->timeout_ms = timeout_ms;
    state->discovery = discovery;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    state->pairing = pair;
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    state->path = path;
#endif
    *out = state;
    return true;
}

static bool nan_ready_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out)
{ return nan_capture(ctx, self, argc, argv, out, NAN_READY); }
static bool nan_close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out)
{ return nan_capture(ctx, self, argc, argv, out, NAN_CLOSE); }
static bool nan_service_ready_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out)
{ return nan_capture(ctx, self, argc, argv, out, NAN_SERVICE_READY); }
static bool nan_service_close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out)
{ return nan_capture(ctx, self, argc, argv, out, NAN_SERVICE_CLOSE); }

static bool nan_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (state->operation == NAN_PAIRING_CREDENTIALS) {
        esp_err_t error = esp32_mquickjs_wifi_nan_discovery_credentials_request(state->discovery, &state->credentials_revision);
        if (error) { (void)nan_service_error(ctx, nan_operation_name(state->operation), state->discovery, error, false); return false; }
    }
    if (state->operation == NAN_PAIRING_RECEIVE) {
        esp_err_t error = esp32_mquickjs_wifi_nan_pairing_receive(state->discovery, &state->pairing);
        if (error) { (void)nan_service_error(ctx, nan_operation_name(state->operation), state->discovery, error, false); return false; }
    }
    if (state->operation == NAN_PAIRING_CLOSE) esp32_mquickjs_wifi_nan_pairing_close(state->pairing, false);
#endif
    if (state->operation == NAN_CLOSE) esp32_mquickjs_wifi_nan_session_close(state->session, false);
    if (state->operation == NAN_SERVICE_CLOSE) esp32_mquickjs_wifi_nan_discovery_close(state->discovery);
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (state->operation == NAN_PATH_CLOSE) esp32_mquickjs_wifi_nan_path_close(state->path, false);
    if (state->operation == NAN_PATH_RESPOND) {
        esp_err_t error = esp32_mquickjs_wifi_nan_path_respond(state->path, state->response->accept,
            state->response->ssi, state->response->length);
        if (error) { (void)nan_path_error(ctx, state->path, nan_operation_name(state->operation), error, false); return false; }
    }
    if (state->operation == NAN_PATH_RECEIVE) {
        esp_err_t error = esp32_mquickjs_wifi_nan_path_receive(state->discovery, &state->path);
        if (error) { (void)nan_service_error(ctx, nan_operation_name(state->operation), state->discovery, error, false); return false; }
    }

#endif
    if (state->message) {
        esp_err_t error = esp32_mquickjs_wifi_nan_message_activate(state->message);
        if (error) { (void)nan_message_error(ctx, state->message, error, false); return false; }
    }
    (void)esp32_mquickjs_wifi_nan_service();
    return true;
}

static esp32_mquickjs_future_poll_t nan_poll(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->cancelled) return ESP32_MQUICKJS_FUTURE_READY;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (state->operation == NAN_PAIRING_CREDENTIALS) {
        (void)esp32_mquickjs_wifi_nan_service();
        state->credentials_error = esp32_mquickjs_wifi_nan_discovery_credentials_read(state->discovery,
            state->credentials_revision, &state->credentials_ready, &state->credentials);
        return state->credentials_ready || state->credentials_error ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
    }
    if (state->operation == NAN_PAIRING_RECEIVE) {
        (void)esp32_mquickjs_wifi_nan_service();
        if (state->pairing) return ESP32_MQUICKJS_FUTURE_READY;
        esp_err_t error = esp32_mquickjs_wifi_nan_pairing_receive(state->discovery, &state->pairing);
        return error || state->pairing ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
    }
    if (state->pairing) {
        (void)esp32_mquickjs_wifi_nan_service();
        esp32_mquickjs_wifi_nan_pairing_status_t status;
        esp32_mquickjs_wifi_nan_pairing_status(state->pairing, &status);
        return (state->operation == NAN_PAIRING_CLOSE ? status.retired : status.ready || status.closing || status.error) ?
            ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
    }
#endif
    (void)esp32_mquickjs_wifi_nan_service();
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (state->operation == NAN_PATH_RECEIVE) {
        if (state->path) return ESP32_MQUICKJS_FUTURE_READY;
        esp_err_t error = esp32_mquickjs_wifi_nan_path_receive(state->discovery, &state->path);
        return error || state->path ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
    }
    if (state->path) {
        esp32_mquickjs_wifi_nan_path_status_t status;
        esp32_mquickjs_wifi_nan_path_status(state->path, &status);
        bool close = state->operation == NAN_PATH_CLOSE ||
            (state->operation == NAN_PATH_RESPOND && !state->response->accept);
        return (close ? status.retired : status.ready || status.closing || status.error) ?
            ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
    }
#endif
    if (state->message) {
        esp32_mquickjs_wifi_nan_message_status_t status;
        esp32_mquickjs_wifi_nan_message_status(state->message, &status);
        return status.done ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
    }
    if (state->discovery) {
        esp32_mquickjs_wifi_nan_discovery_status_t status;
        esp32_mquickjs_wifi_nan_discovery_status(state->discovery, &status);
        bool ready = state->operation == NAN_SERVICE_CLOSE ? status.retired : status.ready || status.error || status.closing;
        return ready ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
    }
    esp32_mquickjs_wifi_nan_session_status_t status;
    (void)esp32_mquickjs_wifi_nan_session_status(state->session, &status);
    bool ready = state->operation == NAN_CLOSE ? status.retired : status.ready || status.error || status.closing;
    return ready ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue nan_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (state->operation == NAN_PAIRING_CREDENTIALS) {
        if (state->credentials_error || !state->credentials_ready)
            return nan_service_error(ctx, nan_operation_name(state->operation), state->discovery,
                state->credentials_error, false);
        return nan_credentials_to_js(ctx, &state->credentials);
    }
    if (state->operation == NAN_PAIRING_RECEIVE) return nan_pairing_receive_finish(ctx, state);
    if (state->pairing) {
        esp32_mquickjs_wifi_nan_pairing_status_t status;
        esp32_mquickjs_wifi_nan_pairing_status(state->pairing, &status);
        if (state->operation == NAN_PAIRING_CLOSE && status.retired) return JS_UNDEFINED;
        if (state->operation == NAN_PAIRING_READY && status.ready && !status.closing && !status.error)
            return nan_pairing_status_to_js(ctx, &status);
        return nan_pairing_error(ctx, state->pairing, nan_operation_name(state->operation), ESP_OK, false);
    }
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (state->operation == NAN_PATH_RECEIVE) return nan_path_receive_finish(ctx, state);
    if (state->path) {
        esp32_mquickjs_wifi_nan_path_status_t status;
        esp32_mquickjs_wifi_nan_path_status(state->path, &status);
        if (state->operation == NAN_PATH_CLOSE && status.retired) return JS_UNDEFINED;
        if (state->operation == NAN_PATH_RESPOND && !state->response->accept && status.retired && !status.error)
            return nan_path_status_to_js(ctx, &status);
        if (state->operation != NAN_PATH_CLOSE && status.ready && !status.error)
            return nan_path_status_to_js(ctx, &status);
        return nan_path_error(ctx, state->path, nan_operation_name(state->operation), ESP_OK, false);
    }
#endif
    if (state->message) return nan_message_finish(ctx, state->message);
    if (state->discovery) {
        esp32_mquickjs_wifi_nan_discovery_status_t status;
        esp32_mquickjs_wifi_nan_discovery_status(state->discovery, &status);
        if (state->operation == NAN_SERVICE_CLOSE && status.retired) return JS_UNDEFINED;
        if (state->operation == NAN_SERVICE_READY && status.ready && !status.error && !status.closing)
            return nan_service_status_to_js(ctx, &status);
        return nan_service_error(ctx, nan_operation_name(state->operation), state->discovery, ESP_OK, false);
    }
    esp32_mquickjs_wifi_nan_session_status_t status;
    (void)esp32_mquickjs_wifi_nan_session_status(state->session, &status);
    if (state->operation == NAN_CLOSE) {
        if (status.retired) return JS_UNDEFINED;
    } else if (status.ready && !status.error && !status.closing) {
        /* Snapshot conversion never consumes the ready state or its owner. */
        return nan_status_to_js(ctx, &status);
    }
    return nan_error(ctx, nan_operation_name(state->operation), state->session, ESP_OK, false);
}

static esp32_mquickjs_cancel_result_t nan_cancel_wait(esp32_mquickjs_future_driver_state_t *state)
{
    state->cancelled = true;
    if (state->message) esp32_mquickjs_wifi_nan_message_cancel(state->message, false);
    /* Cancellation before start has no native effect. A started close already
     * belongs to the registry; cancelling a ready wait does not stop discovery. */
    return ESP32_MQUICKJS_CANCELLED;
}
static uint32_t nan_timeout(const esp32_mquickjs_future_driver_state_t *state)
{ return state->timeout_ms; }
static JSValue nan_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (state->operation == NAN_PAIRING_RECEIVE) return JS_NULL;
    if (state->pairing) return nan_pairing_error(ctx, state->pairing, nan_operation_name(state->operation), ESP_ERR_TIMEOUT, true);
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (state->operation == NAN_PATH_RECEIVE) return JS_NULL;
    if (state->path) return nan_path_error(ctx, state->path, nan_operation_name(state->operation), ESP_ERR_TIMEOUT, true);

#endif
    if (state->message) {
        state->cancelled = true;
        esp32_mquickjs_wifi_nan_message_cancel(state->message, true);
        return nan_message_error(ctx, state->message, ESP_ERR_TIMEOUT, true);
    }
    (void)nan_cancel_wait(state);
    if (state->discovery) return nan_service_error(ctx, nan_operation_name(state->operation), state->discovery, ESP_ERR_TIMEOUT, true);
    return nan_error(ctx, nan_operation_name(state->operation), state->session, ESP_ERR_TIMEOUT, true);
}
#define NAN_DRIVER(capture_fn) { .memory_owner = "wireless.future", .capture = capture_fn, .start = nan_start, \
    .poll = nan_poll, .finish = nan_finish, .cancel = nan_cancel_wait, .destroy = nan_destroy, \
    .timeout_ms = nan_timeout, .on_timeout = nan_on_timeout }
static const esp32_mquickjs_future_driver_t s_nan_ready_driver = NAN_DRIVER(nan_ready_capture);
static const esp32_mquickjs_future_driver_t s_nan_close_driver = NAN_DRIVER(nan_close_capture);
static const esp32_mquickjs_future_driver_t s_nan_service_ready_driver = NAN_DRIVER(nan_service_ready_capture);
static const esp32_mquickjs_future_driver_t s_nan_service_close_driver = NAN_DRIVER(nan_service_close_capture);
static const esp32_mquickjs_future_driver_t s_nan_service_send_driver = NAN_DRIVER(nan_send_capture);
#if CONFIG_ESP_WIFI_NAN_PAIRING
static bool nan_pairing_receive_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return nan_capture(ctx, self, argc, argv, out, NAN_PAIRING_RECEIVE); }
static const esp32_mquickjs_future_driver_t s_nan_pairing_receive_driver = NAN_DRIVER(nan_pairing_receive_capture);
static bool nan_pairing_credentials_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return nan_capture(ctx, self, argc, argv, out, NAN_PAIRING_CREDENTIALS); }
static const esp32_mquickjs_future_driver_t s_nan_pairing_credentials_driver = NAN_DRIVER(nan_pairing_credentials_capture);
static bool nan_pairing_ready_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return nan_capture(ctx, self, argc, argv, out, NAN_PAIRING_READY); }
static bool nan_pairing_close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return nan_capture(ctx, self, argc, argv, out, NAN_PAIRING_CLOSE); }
static const esp32_mquickjs_future_driver_t s_nan_pairing_ready_driver = NAN_DRIVER(nan_pairing_ready_capture);
static const esp32_mquickjs_future_driver_t s_nan_pairing_close_driver = NAN_DRIVER(nan_pairing_close_capture);
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
static bool nan_path_ready_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return nan_capture(ctx, self, argc, argv, out, NAN_PATH_READY); }
static bool nan_path_close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return nan_capture(ctx, self, argc, argv, out, NAN_PATH_CLOSE); }
static bool nan_path_receive_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return nan_capture(ctx, self, argc, argv, out, NAN_PATH_RECEIVE); }
static const esp32_mquickjs_future_driver_t s_nan_path_ready_driver = NAN_DRIVER(nan_path_ready_capture);
static const esp32_mquickjs_future_driver_t s_nan_path_close_driver = NAN_DRIVER(nan_path_close_capture);
static const esp32_mquickjs_future_driver_t s_nan_path_receive_driver = NAN_DRIVER(nan_path_receive_capture);
static const esp32_mquickjs_future_driver_t s_nan_path_respond_driver = NAN_DRIVER(nan_path_respond_capture);

#endif

static JSValue nan_call(JSContext *ctx, JSValue *receiver, int argc, JSValue *argv, const char *name)
{
    JSGCRef self_ref, method_ref;
    JSValue *self = JS_PushGCRef(ctx, &self_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *self = *receiver;
    *method = JS_GetPropertyStr(ctx, *self, name);
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *self, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &self_ref);
    return result;
}

JSValue js_wifi_nan_ready(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "ready"); }
JSValue js_wifi_nan_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "close"); }
JSValue js_wifi_nan_service_ready(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "ready"); }
JSValue js_wifi_nan_service_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "close"); }
JSValue js_wifi_nan_service_send(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "send"); }
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
JSValue js_wifi_nan_path_ready(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "ready"); }
JSValue js_wifi_nan_path_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "close"); }
JSValue js_wifi_nan_path_respond(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "respond"); }
JSValue js_wifi_nan_path_receive(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "receiveDataPath"); }

#endif
#if CONFIG_ESP_WIFI_NAN_PAIRING
JSValue js_wifi_nan_pairing_receive(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "receivePairing"); }
JSValue js_wifi_nan_pairing_credentials(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "pairingCredentials"); }
JSValue js_wifi_nan_pairing_ready(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "ready"); }
JSValue js_wifi_nan_pairing_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_call(ctx, self, argc, argv, "close"); }
#endif
JSValue js_wifi_nan_cancel(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "NAN cancel expects no arguments");
    esp32_mquickjs_wifi_nan_session_t *session = nan_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    esp32_mquickjs_wifi_nan_session_close(session, false);
    (void)esp32_mquickjs_wifi_nan_service();
    return JS_UNDEFINED;
}

bool esp32_mquickjs_init_wifi_nan_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, object_ref, proto_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *proto = JS_PushGCRef(ctx, &proto_ref), *method = JS_PushGCRef(ctx, &method_ref);
    bool ok = false;
    *global = JS_GetGlobalObject(ctx);
    if (JS_IsException(*global)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "WiFiNanSession");
    if (JS_IsException(*object)) goto done;
    *proto = JS_GetPropertyStr(ctx, *object, "prototype");
    if (JS_IsException(*proto)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "ready");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_ready_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "close");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_close_driver)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "WiFiNanService");
    if (JS_IsException(*object)) goto done;
    *proto = JS_GetPropertyStr(ctx, *object, "prototype");
    if (JS_IsException(*proto)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "ready");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_service_ready_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "close");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_service_close_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "send");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_service_send_driver)) goto done;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    *method = JS_GetPropertyStr(ctx, *proto, "receivePairing");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_pairing_receive_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "pairingCredentials");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_pairing_credentials_driver)) goto done;
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    *method = JS_GetPropertyStr(ctx, *proto, "receiveDataPath");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_path_receive_driver)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "WiFiNanDataPath");
    if (JS_IsException(*object)) goto done;
    *proto = JS_GetPropertyStr(ctx, *object, "prototype");
    if (JS_IsException(*proto)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "ready");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_path_ready_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "close");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_path_close_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "respond");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_path_respond_driver)) goto done;

#endif
#if CONFIG_ESP_WIFI_NAN_PAIRING
    *object = JS_GetPropertyStr(ctx, *global, "WiFiNanPairing");
    if (JS_IsException(*object)) goto done;
    *proto = JS_GetPropertyStr(ctx, *object, "prototype");
    if (JS_IsException(*proto)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "ready");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_pairing_ready_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "close");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_nan_pairing_close_driver)) goto done;
#endif
    ok = true;
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register NAN runtime");
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &proto_ref);
    JS_PopGCRef(ctx, &object_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#endif
