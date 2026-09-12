static esp32_mquickjs_wifi_nan_discovery_t *nan_service_receiver(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_NAN_SERVICE) {
        JS_ThrowTypeError(ctx, "expected WiFiNanService"); return NULL;
    }
    esp32_mquickjs_wifi_nan_discovery_t *service = JS_GetOpaque(ctx, value);
    if (!service) JS_ThrowReferenceError(ctx, "invalid WiFiNanService");
    return service;
}

static JSValue nan_service_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_nan_discovery_status_t *s)
{
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "identity", JS_NewUint32(ctx, s->identity));
    SET(result, "sessionIdentity", JS_NewUint32(ctx, s->session_identity));
    SET(result, "serviceId", s->native_id ? JS_NewInt32(ctx, s->native_id) : JS_NULL);
    SET(result, "kind", JS_NewString(ctx, s->publish ? "publish" : "subscribe"));
    SET(result, "state", JS_NewString(ctx, s->retired ? "closed" : s->closing ? "closing" : s->ready ? "ready" : "opening"));
    SET(result, "ready", JS_NewBool(s->ready));
    SET(result, "creating", JS_NewBool(s->creating));
    SET(result, "closeRequested", JS_NewBool(s->closing));
    SET(result, "nativeCancelled", JS_NewBool(s->cancelled));
    SET(result, "cleanupPending", JS_NewBool(s->closing && !s->retired));
    SET(result, "timedOut", JS_NewBool(s->timed_out));
    SET(result, "droppedBeforeQueue", JS_NewUint32(ctx, s->dropped));
    SET(result, "sendIdentity", s->send_identity ? JS_NewUint32(ctx, s->send_identity) : JS_NULL);
    SET(result, "sendPending", JS_NewBool(s->send_pending));
    SET(result, "sendCleanupPending", JS_NewBool(s->send_cleanup_pending));
    SET(result, "reservedBytes", JS_NewUint32(ctx, s->reserved_bytes));
    SET(result, "pairingEnabled", JS_NewBool(s->pairing_enabled));
    SET(result, "pairingRequestPending", JS_NewBool(s->pairing_request_pending));
    SET(result, "pairingRequestsDropped", JS_NewUint32(ctx, s->pairing_requests_dropped));
    SET(result, "securityRequired", JS_NewBool(s->security_required));
    SET(result, "credentialCount", JS_NewInt32(ctx, s->credential_count));
    SET(result, "groupDataProtection", JS_NewBool(s->group_data_protection));
    SET(result, "groupManagementProtection", JS_NewBool(s->group_management_protection));
    SET(result, "vendorBytes", JS_NewInt32(ctx, s->vendor_bytes));
    SET(result, "error", s->error ? JS_NewInt32(ctx, s->error) : JS_NULL);
    SET(result, "cleanupError", s->cleanup_error ? JS_NewInt32(ctx, s->cleanup_error) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue nan_service_error(JSContext *ctx, const char *operation,
    esp32_mquickjs_wifi_nan_discovery_t *service, esp_err_t error, bool timeout)
{
    esp32_mquickjs_wifi_nan_discovery_status_t status = {0};
    if (service) esp32_mquickjs_wifi_nan_discovery_status(service, &status);
    if (!error) error = status.error ? status.error : ESP_ERR_INVALID_STATE;
    JSGCRef ref; JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = nan_service_status_to_js(ctx, &status);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "waitTimedOut", JS_NewBool(timeout));
    (void)esp32_mquickjs_throw_native_error(ctx,
        timeout || status.timed_out ? "WIFI_NAN_TIMEOUT" : status.closing && !status.error ? "WIFI_NAN_CLOSED" : "WIFI_NAN_FAILED",
        operation, "NAN service operation did not complete; inspect service status", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue nan_event_bytes(JSContext *ctx, const uint8_t *bytes, size_t length)
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

static JSValue nan_service_event_to_js(JSContext *ctx, const void *data, void *opaque)
{
    (void)opaque;
    const esp32_mquickjs_wifi_nan_discovery_event_t *e = data;
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "kind", JS_NewString(ctx, e->kind == 1 ? "match" : e->kind == 2 ? "replied" : "message"));
    SET(result, "identity", JS_NewUint32(ctx, e->identity));
    SET(result, "sequence", JS_NewUint32(ctx, e->sequence));
    SET(result, "serviceId", JS_NewInt32(ctx, e->native_id));
    SET(result, "peerServiceId", JS_NewInt32(ctx, e->peer_id));
    SET(result, "peerMac", nan_event_bytes(ctx, e->peer, 6));
    SET(result, "ssi", nan_event_bytes(ctx, e->ssi, e->ssi_len));
    SET(result, "ssiVersion", e->kind == 1 ? JS_NewInt32(ctx, e->ssi_version) : JS_NULL);
    SET(result, "datapathRequired", e->kind == 1 ? JS_NewBool(e->datapath_required) : JS_NULL);
    SET(result, "securityRequired", e->kind == 1 ? JS_NewBool(e->security_required) : JS_NULL);
    SET(result, "furtherDiscovery", e->kind == 1 ? JS_NewBool(e->further_discovery) : JS_NULL);
    SET(result, "gas", e->kind == 1 ? JS_NewBool(e->gas) : JS_NULL);
    SET(result, "ndpe", e->kind == 1 ? JS_NewBool(e->ndpe) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

typedef struct { esp32_mquickjs_wifi_nan_discovery_t *service; bool bound; } nan_queue_context_t;
static void nan_service_queue_destroyed(void *opaque)
{
    nan_queue_context_t *source = opaque;
    esp32_mquickjs_wifi_nan_discovery_release(source->service);
    esp32_mquickjs_memory_payload_free(source);
}
static void nan_service_queue_closed(void *opaque)
{
    nan_queue_context_t *source = opaque;
    esp32_mquickjs_wifi_nan_discovery_queue_closed(source->service);
    if (!source->bound) nan_service_queue_destroyed(source);
}

static bool nan_service_string(JSContext *ctx, JSValue value, char *out, size_t capacity)
{
    if (!JS_IsString(ctx, value)) return false;
    JSCStringBuf buffer; size_t length = 0;
    const char *text = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (!text || length >= capacity || memchr(text, 0, length)) return false;
    memcpy(out, text, length); out[length] = 0; return true;
}

#include "esp32_mquickjs_wifi_nan_service_options_public.inc"

typedef struct {
    esp32_mquickjs_wifi_nan_service_config_t config;
    uint8_t ssi[ESP_WIFI_MAX_SVC_SSI_LEN];
    wifi_nan_discovery_security_params_t security;
    wifi_nan_pairing_cfg_t pairing;
    nan_vendor_ie_t vendor;
    uint8_t vendor_body[NAN_VENDOR_IE_MAX_BODY_LEN];
    uint32_t timeout_ms, capacity;
} nan_service_options_t;
static bool nan_service_options(JSContext *ctx, JSGCRef *input, bool publish, bool usd, nan_service_options_t *out)
{
    static const char *const keys[] = {"name", "type", "matchingFilter", "singleEvent", "ssi", "timeoutMs", "queueCapacity", "dataPath", "security", "vendor", "ttlSeconds", "channel", "channels", "dwell", "pairing"};
    if (!esp32_mquickjs_validate_plain_options(ctx, input->val, "NAN service", keys, 15)) return false;
    JSGCRef field_ref; JSValue *field = JS_PushGCRef(ctx, &field_ref);
    bool ok = false, single = false, datapath = false; size_t type = 0;
    const char *const choices_publish[] = {"unsolicited", "solicited"};
    const char *const choices_subscribe[] = {"active", "passive"};
    out->timeout_ms = 10000; out->capacity = 4;
    if (!nan_service_usd_options(ctx, input, publish, usd, &out->config)) goto done;
    char *name = publish ? out->config.publish.service_name : out->config.subscribe.service_name;
    char *filter = publish ? out->config.publish.matching_filter : out->config.subscribe.matching_filter;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, input->val, name); if (JS_IsException(*field)) goto done; } while (0)
    FIELD("name"); if (!nan_service_string(ctx, *field, name, ESP_WIFI_MAX_SVC_NAME_LEN) || !name[0]) goto invalid;
    FIELD("matchingFilter");
    if (!JS_IsUndefined(*field) && !nan_service_string(ctx, *field, filter, ESP_WIFI_MAX_FILTER_LEN)) goto invalid;
    FIELD("type");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_enum(ctx, *field,
        publish ? choices_publish : choices_subscribe, 2, &type)) goto invalid;
    FIELD("singleEvent");
    if (!JS_IsUndefined(*field)) { if (!JS_IsBool(*field)) goto invalid; single = *field == JS_TRUE; }
    FIELD("dataPath");
    if (!JS_IsUndefined(*field)) { if (!JS_IsBool(*field)) goto invalid; datapath = *field == JS_TRUE; }
    FIELD("timeoutMs");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 120000, &out->timeout_ms)) goto invalid;
    FIELD("queueCapacity");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 16, &out->capacity)) goto invalid;
    FIELD("security");
    if (!JS_IsUndefined(*field)) {
        if (!nan_security_options(ctx, &field_ref, &out->security)) goto done;
        if (publish) { out->config.publish.security_reqd = true; out->config.publish.security_cfg = &out->security; }
        else { out->config.subscribe.security_reqd = true; out->config.subscribe.security_cfg = &out->security; }
    }
    FIELD("pairing");
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        if (*field == JS_TRUE) {
            out->pairing.pairing_setup = true;
            out->pairing.pairing_verification = true;
            out->pairing.npk_nik_caching = true;
            out->pairing.bootstrapping_methods = publish ? WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY : WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD;
            if (publish) out->config.publish.pairing = &out->pairing;
            else out->config.subscribe.pairing = &out->pairing;
        }
    }
    FIELD("vendor");
    if (!JS_IsUndefined(*field)) {
        if (!nan_vendor_options(ctx, &field_ref, &out->vendor, out->vendor_body)) goto done;
        if (publish) out->config.publish.vendor_ie = &out->vendor;
        else out->config.subscribe.vendor_ie = &out->vendor;
    }
    FIELD("ssi");
    uint16_t length = 0;
    if (!JS_IsUndefined(*field)) {
        esp32_mquickjs_byte_source_t source = {0}; uint8_t *owned = NULL; JSValue error = JS_UNDEFINED;
        if (!esp32_mquickjs_get_wireless_byte_source("wifi.nan", ctx, *field, "NAN service SSI", &source, &owned, &error)) {
            if (!JS_HasException(ctx)) JS_Throw(ctx, error);
            goto done;
        }
        bool bounded = source.length <= sizeof(out->ssi);
        if (bounded) { length = source.length; if (length) memcpy(out->ssi, source.data, length); }
        esp32_mquickjs_release_byte_source(owned);
        if (!bounded) goto invalid;
    }
    if (publish) {
        out->config.publish.type = type ? NAN_PUBLISH_SOLICITED : NAN_PUBLISH_UNSOLICITED;
        out->config.publish.single_replied_event = single;
        out->config.publish.ssi = length ? out->ssi : NULL; out->config.publish.ssi_len = length;
        out->config.publish.ndp_resp_needed = true;
        out->config.publish.datapath_reqd = datapath;
    } else {
        out->config.subscribe.type = type ? NAN_SUBSCRIBE_PASSIVE : NAN_SUBSCRIBE_ACTIVE;
        out->config.subscribe.single_match_event = single;
        out->config.subscribe.datapath_reqd = datapath;
        out->config.subscribe.ssi = length ? out->ssi : NULL; out->config.subscribe.ssi_len = length;
    }
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid NAN service options");
done:
#undef FIELD
    JS_PopGCRef(ctx, &field_ref); return ok;
}

static JSValue nan_create_service(JSContext *ctx, JSValue *self, int argc, JSValue *argv, bool publish)
{
    if (argc != 1) return JS_ThrowTypeError(ctx, "NAN service requires one options object");
    esp32_mquickjs_wifi_nan_session_t *parent = nan_receiver(ctx, *self);
    if (!parent) return JS_EXCEPTION;
    if (!esp32_mquickjs_wifi_nan_session_retain(parent)) return JS_ThrowInternalError(ctx, "NAN reference exhausted");
    JSGCRef input_ref, result_ref, queue_ref;
    JSValue *input = JS_PushGCRef(ctx, &input_ref), *result = JS_PushGCRef(ctx, &result_ref), *queue_value = JS_PushGCRef(ctx, &queue_ref);
    *input = argv[0];
    esp32_mquickjs_wifi_nan_discovery_t *service = NULL;
    nan_service_options_t *options = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*options),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!options) { JS_ThrowOutOfMemory(ctx); goto fail; }
    esp32_mquickjs_wifi_nan_session_status_t parent_status;
    esp32_mquickjs_wifi_nan_session_status(parent, &parent_status);
    if (!nan_service_options(ctx, &input_ref, publish, parent_status.usd, options)) goto fail;
    esp_err_t error = esp32_mquickjs_wifi_nan_discovery_create(parent, publish, &options->config, options->timeout_ms, &service);
    if (error) { (void)nan_service_error(ctx, publish ? "WiFiNanSession.publish" : "WiFiNanSession.subscribe", NULL, error, false); goto fail; }
    *result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_NAN_SERVICE);
    if (JS_IsException(*result)) goto fail;
    nan_queue_context_t *source = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*source),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!source) { JS_ThrowOutOfMemory(ctx); goto fail; }
    source->service = service;
    if (!esp32_mquickjs_wifi_nan_discovery_retain(service)) {
        esp32_mquickjs_memory_payload_free(source); JS_ThrowInternalError(ctx, "NAN reference exhausted"); goto fail;
    }
    *queue_value = esp32_mquickjs_event_queue_new_wireless("wifi.nan", ctx, esp32_mquickjs_get_active_runtime(),
        sizeof(esp32_mquickjs_wifi_nan_discovery_event_t), options->capacity, ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,
        nan_service_event_to_js, NULL, nan_service_queue_closed, source);
    if (JS_IsException(*queue_value)) { nan_service_queue_closed(source); goto fail; }
    esp32_mquickjs_event_queue_t *queue = esp32_mquickjs_event_queue_from_value(ctx, *queue_value);
    source->bound = esp32_mquickjs_event_queue_bind_context_release(queue, nan_service_queue_destroyed);
    if (!source->bound) { JS_ThrowInternalError(ctx, "NAN queue lifetime binding failed"); goto fail; }
    SET(result, "events", *queue_value);
    error = esp32_mquickjs_wifi_nan_discovery_activate(service, queue);
    if (error) { (void)nan_service_error(ctx, publish ? "WiFiNanSession.publish" : "WiFiNanSession.subscribe", service, error, false); goto fail; }
    JS_SetOpaque(ctx, *result, service); service = NULL;
    esp32_mquickjs_wireless_secure_zero(options, sizeof(*options)); esp32_mquickjs_memory_payload_free(options);
    esp32_mquickjs_wifi_nan_session_release(parent);
    (void)esp32_mquickjs_wifi_nan_service();
    JSValue value = *result;
    JS_PopGCRef(ctx, &queue_ref); JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &input_ref);
    return value;
fail:
    if (!JS_IsUndefined(*queue_value) && !JS_IsException(*queue_value)) (void)esp32_mquickjs_event_queue_dispose(ctx, *queue_value);
    if (options) { esp32_mquickjs_wireless_secure_zero(options, sizeof(*options)); esp32_mquickjs_memory_payload_free(options); }
    esp32_mquickjs_wifi_nan_discovery_release(service);
    esp32_mquickjs_wifi_nan_session_release(parent);
    JS_PopGCRef(ctx, &queue_ref); JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &input_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_nan_publish(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_create_service(ctx, self, argc, argv, true); }
JSValue js_wifi_nan_subscribe(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_create_service(ctx, self, argc, argv, false); }
JSValue js_wifi_nan_service_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ (void)self; (void)argc; (void)argv; return JS_ThrowTypeError(ctx, "use NAN session publish/subscribe"); }
void js_wifi_nan_service_finalizer(JSContext *ctx, void *opaque)
{ (void)ctx; esp32_mquickjs_wifi_nan_discovery_close(opaque); esp32_mquickjs_wifi_nan_discovery_release(opaque); }
JSValue js_wifi_nan_service_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "NAN service status expects no arguments");
    esp32_mquickjs_wifi_nan_discovery_t *service = nan_service_receiver(ctx, *self);
    if (!service) return JS_EXCEPTION;
    esp32_mquickjs_wifi_nan_discovery_status_t status;
    esp32_mquickjs_wifi_nan_discovery_status(service, &status);
    return nan_service_status_to_js(ctx, &status);
}
JSValue js_wifi_nan_service_cancel(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "NAN service cancel expects no arguments");
    esp32_mquickjs_wifi_nan_discovery_t *service = nan_service_receiver(ctx, *self);
    if (!service) return JS_EXCEPTION;
    esp32_mquickjs_wifi_nan_discovery_close(service);
    (void)esp32_mquickjs_wifi_nan_service();
    return JS_UNDEFINED;
}
