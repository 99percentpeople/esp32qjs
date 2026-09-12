static JSValue nan_credentials_to_js(JSContext *ctx, const esp32_mquickjs_wifi_nan_credentials_t *snapshot)
{
    JSGCRef array_ref, row_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref), *row = JS_PushGCRef(ctx, &row_ref);
    *array = JS_NewArray(ctx, 0); if (JS_IsException(*array)) goto fail;
    for (unsigned i = 0; i < snapshot->count; ++i) {
        const esp32_mquickjs_wifi_nan_credential_info_t *entry = &snapshot->entries[i];
        *row = JS_NewObject(ctx); if (JS_IsException(*row)) goto fail;
        SET(row, "credentialId", JS_NewUint32(ctx, entry->identity));
        SET(row, "peerMac", nan_event_bytes(ctx, entry->peer, 6));
        SET(row, "expiresAtUs", entry->expires_us ? JS_NewFloat64(ctx, entry->expires_us) : JS_NULL);
        if (JS_IsException(JS_SetPropertyUint32(ctx, *array, i, *row))) goto fail;
    }
    JS_PopGCRef(ctx, &row_ref); return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &row_ref); JS_PopGCRef(ctx, &array_ref); return JS_EXCEPTION;
}

static esp32_mquickjs_wifi_nan_pairing_t *nan_pairing_receiver(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_NAN_PAIRING) {
        JS_ThrowTypeError(ctx, "expected WiFiNanPairing"); return NULL;
    }
    esp32_mquickjs_wifi_nan_pairing_t *p = JS_GetOpaque(ctx, value);
    if (!p) JS_ThrowReferenceError(ctx, "invalid WiFiNanPairing");
    return p;
}

static JSValue nan_pairing_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_nan_pairing_status_t *s)
{
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    SET(result, "identity", JS_NewUint32(ctx, s->identity));
    SET(result, "sessionIdentity", JS_NewUint32(ctx, s->session_identity));
    SET(result, "serviceIdentity", JS_NewUint32(ctx, s->service_identity));
    SET(result, "serviceId", JS_NewInt32(ctx, s->service_id));
    SET(result, "peerServiceId", JS_NewInt32(ctx, s->peer_service_id));
    SET(result, "peerMac", nan_event_bytes(ctx, s->peer, 6));
    SET(result, "role", JS_NewString(ctx, s->initiator ? "initiator" : "responder"));
    SET(result, "state", JS_NewString(ctx, s->retired ? "closed" : s->closing ? "closing" :
        s->ready ? "paired" : !s->confirmed ? "confirmation-pending" : "pairing"));
    SET(result, "mode", JS_NewString(ctx, s->verification ? "verification" : "pin"));
    SET(result, "credentialId", s->credential_id ? JS_NewUint32(ctx, s->credential_id) : JS_NULL);
    SET(result, "bootstrap", JS_NewBool(s->bootstrap));
    SET(result, "incoming", JS_NewBool(s->incoming));
    SET(result, "delivered", JS_NewBool(s->delivered));
    SET(result, "bootstrapAttempted", JS_NewBool(s->bootstrap_attempted));
    SET(result, "bootstrapSent", JS_NewBool(s->bootstrap_sent));
    SET(result, "bootstrapTxPending", JS_NewBool(s->bootstrap_tx_pending));
    SET(result, "peerResponded", JS_NewBool(s->peer_responded));
    SET(result, "peerAccepted", JS_NewBool(s->peer_accepted));
    SET(result, "confirmed", JS_NewBool(s->confirmed));
    SET(result, "rejected", JS_NewBool(s->rejected));
    SET(result, "startAttempted", JS_NewBool(s->start_attempted));
    SET(result, "ready", JS_NewBool(s->ready));
    SET(result, "closeRequested", JS_NewBool(s->closing));
    SET(result, "cleanupPending", JS_NewBool(s->closing && !s->retired));
    SET(result, "timedOut", JS_NewBool(s->timed_out));
    SET(result, "nativeIdentity", s->native.identity ? JS_NewUint32(ctx, s->native.identity) : JS_NULL);
    SET(result, "nativeActive", JS_NewBool(s->native.active));
    SET(result, "authenticated", JS_NewBool(s->native.authenticated));
    SET(result, "paired", JS_NewBool(s->native.paired));
    SET(result, "trafficPending", JS_NewBool(s->native.traffic_pending));
    SET(result, "nativeRetired", JS_NewBool(s->native.retired));
    SET(result, "error", s->error ? JS_NewInt32(ctx, s->error) : JS_NULL);
    SET(result, "cleanupError", s->cleanup_error ? JS_NewInt32(ctx, s->cleanup_error) : JS_NULL);
    SET(result, "reservedBytes", JS_NewUint32(ctx, s->reserved_bytes));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue nan_pairing_error(JSContext *ctx, esp32_mquickjs_wifi_nan_pairing_t *p,
    const char *operation, esp_err_t error, bool timeout)
{
    esp32_mquickjs_wifi_nan_pairing_status_t status = {0};
    if (p) esp32_mquickjs_wifi_nan_pairing_status(p, &status);
    if (!error) error = status.error ? status.error : status.cleanup_error ? status.cleanup_error : ESP_ERR_INVALID_STATE;
    JSGCRef ref; JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = nan_pairing_status_to_js(ctx, &status); if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "waitTimedOut", JS_NewBool(timeout));
    (void)esp32_mquickjs_throw_native_error(ctx, timeout || status.timed_out ? "WIFI_NAN_TIMEOUT" :
        status.closing && !status.error ? "WIFI_NAN_CLOSED" : "WIFI_NAN_FAILED", operation,
        "NAN pairing did not complete; inspect pairing and Session cleanup", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

void js_wifi_nan_pairing_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    esp32_mquickjs_wifi_nan_pairing_close(opaque, false);
    esp32_mquickjs_wifi_nan_pairing_release(opaque);
}
JSValue js_wifi_nan_pairing_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use NAN service.preparePairing()");
}
JSValue js_wifi_nan_pairing_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "NAN pairing status expects no arguments");
    esp32_mquickjs_wifi_nan_pairing_t *p = nan_pairing_receiver(ctx, *self);
    if (!p) return JS_EXCEPTION;
    esp32_mquickjs_wifi_nan_pairing_status_t status;
    esp32_mquickjs_wifi_nan_pairing_status(p, &status); return nan_pairing_status_to_js(ctx, &status);
}
JSValue js_wifi_nan_pairing_cancel(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "NAN pairing cancel expects no arguments");
    esp32_mquickjs_wifi_nan_pairing_t *p = nan_pairing_receiver(ctx, *self);
    if (!p) return JS_EXCEPTION;
    esp32_mquickjs_wifi_nan_pairing_close(p, false); (void)esp32_mquickjs_wifi_nan_service(); return JS_UNDEFINED;
}

static JSValue nan_pairing_prepare(JSContext *ctx, JSValue *self, int argc, JSValue *argv, bool bootstrap)
{
    if (argc != 1) return JS_ThrowTypeError(ctx, "pairing requires options");
    const char *operation = bootstrap ? "WiFiNanService.requestPairing" : "WiFiNanService.preparePairing";
    esp32_mquickjs_wifi_nan_discovery_t *service = nan_service_receiver(ctx, *self);
    if (!service) return JS_EXCEPTION;
    if (!esp32_mquickjs_wifi_nan_discovery_retain(service)) return JS_ThrowInternalError(ctx, "NAN reference exhausted");
    JSGCRef input_ref, field_ref, result_ref;
    JSValue *input = JS_PushGCRef(ctx, &input_ref), *field = JS_PushGCRef(ctx, &field_ref), *result = JS_PushGCRef(ctx, &result_ref);
    *input = argv[0];
    uint32_t peer_service = 0, timeout_ms = 30000, credential_id = 0; uint16_t length = 0; uint8_t peer[6] = {0};
    esp32_mquickjs_wifi_nan_pairing_t *p = NULL;
    static const char *const keys[] = {"peerServiceId", "peerMac", "timeoutMs", "credentialId"};
    if (!esp32_mquickjs_validate_plain_options(ctx, *input, operation, keys, bootstrap ? 3 : 4)) goto fail;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, *input, name); if (JS_IsException(*field)) goto fail; } while (0)
    FIELD("peerServiceId"); if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 255, &peer_service)) goto invalid;
    FIELD("peerMac"); if (!nan_message_bytes(ctx, *field, peer, 6, &length)) goto fail;
    if (length != 6 || (peer[0] & 1) || !memcmp(peer, (uint8_t[6]){0}, 6)) goto invalid;
    FIELD("timeoutMs"); if (!JS_IsUndefined(*field) &&
        !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 120000, &timeout_ms)) goto invalid;
    if (!bootstrap) {
        FIELD("credentialId"); if (!JS_IsUndefined(*field) &&
            !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, UINT32_MAX, &credential_id)) goto invalid;
    }
    esp32_mquickjs_wifi_nan_discovery_status_t status;
    esp32_mquickjs_wifi_nan_discovery_status(service, &status);
    esp_err_t error = esp32_mquickjs_wifi_nan_pairing_create(service, peer_service, peer, !status.publish, timeout_ms, &p);
    if (error) { (void)nan_service_error(ctx, operation, service, error, false); goto fail; }
    *result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_NAN_PAIRING); if (JS_IsException(*result)) goto fail;
    if (credential_id) {
        error = esp32_mquickjs_wifi_nan_pairing_select(p, credential_id);
        if (error) { (void)nan_pairing_error(ctx, p, operation, error, false); goto fail; }
    }
    if (bootstrap) {
        error = esp32_mquickjs_wifi_nan_pairing_request(p);
        if (error) { (void)nan_pairing_error(ctx, p, operation, error, false); goto fail; }
    }
    error = esp32_mquickjs_wifi_nan_pairing_activate(p);
    if (error) { (void)nan_pairing_error(ctx, p, operation, error, false); goto fail; }
    JS_SetOpaque(ctx, *result, p); p = NULL;
    (void)esp32_mquickjs_wifi_nan_service();
    esp32_mquickjs_wifi_nan_discovery_release(service);
    JSValue value = JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &input_ref);
    return value;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid NAN pairing options");
fail:
#undef FIELD
    esp32_mquickjs_wifi_nan_pairing_release(p); esp32_mquickjs_wifi_nan_discovery_release(service);
    JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &input_ref); return JS_EXCEPTION;
}

JSValue js_wifi_nan_pairing_prepare(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_pairing_prepare(ctx, self, argc, argv, false); }
JSValue js_wifi_nan_pairing_request(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return nan_pairing_prepare(ctx, self, argc, argv, true); }

static JSValue nan_pairing_receive_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (!state->pairing) return nan_service_error(ctx, "WiFiNanService.receivePairing", state->discovery, ESP_ERR_INVALID_STATE, false);
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_NAN_PAIRING);
    if (JS_IsException(*result)) { JS_PopGCRef(ctx, &ref); return JS_EXCEPTION; }
    if (!esp32_mquickjs_wifi_nan_pairing_deliver(state->pairing)) {
        JS_PopGCRef(ctx, &ref);
        return nan_pairing_error(ctx, state->pairing, "WiFiNanService.receivePairing", ESP_ERR_INVALID_STATE, false);
    }
    JS_SetOpaque(ctx, *result, state->pairing); state->pairing = NULL;
    return JS_PopGCRef(ctx, &ref);
}

static bool nan_pairing_pin(JSContext *ctx, JSValue value, uint32_t *pin)
{
    if (!JS_IsString(ctx, value)) return false;
    JSCStringBuf buffer; size_t length = 0;
    const char *text = JS_ToCStringLen(ctx, &length, value, &buffer);
    bool valid = text && length == 6;
    uint32_t number = 0;
    for (size_t i = 0; valid && i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') valid = false;
        else number = number * 10 + (uint32_t)(text[i] - '0');
    }
    if (valid) *pin = number;
    esp32_mquickjs_wireless_secure_zero(&number, sizeof(number));
    esp32_mquickjs_wireless_secure_zero(&buffer, sizeof(buffer));
    return valid;
}

JSValue js_wifi_nan_pairing_confirm(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    if (argc != 1) return JS_ThrowTypeError(ctx, "confirm requires explicit accept options");
    esp32_mquickjs_wifi_nan_pairing_t *p = nan_pairing_receiver(ctx, *self);
    if (!p) return JS_EXCEPTION;
    if (!esp32_mquickjs_wifi_nan_pairing_retain(p)) return JS_ThrowInternalError(ctx, "NAN reference exhausted");
    JSGCRef input_ref, field_ref;
    JSValue *input = JS_PushGCRef(ctx, &input_ref), *field = JS_PushGCRef(ctx, &field_ref);
    *input = argv[0]; bool accept = false; uint32_t pin = 0; bool ok = false;
    static const char *const keys[] = {"accept", "pin"};
    if (!esp32_mquickjs_validate_plain_options(ctx, *input, "NAN pairing confirm", keys, 2)) goto done;
    *field = JS_GetPropertyStr(ctx, *input, "accept"); if (JS_IsException(*field)) goto done;
    if (!JS_IsBool(*field)) goto invalid;
    accept = *field == JS_TRUE;
    *field = JS_GetPropertyStr(ctx, *input, "pin"); if (JS_IsException(*field)) goto done;
    esp32_mquickjs_wifi_nan_pairing_status_t status;
    esp32_mquickjs_wifi_nan_pairing_status(p, &status);
    if (accept && !status.verification ? !nan_pairing_pin(ctx, *field, &pin) : !JS_IsUndefined(*field)) goto invalid;
    esp_err_t error = esp32_mquickjs_wifi_nan_pairing_confirm(p, accept, pin);
    esp32_mquickjs_wireless_secure_zero(&pin, sizeof(pin));
    if (error) { (void)nan_pairing_error(ctx, p, "WiFiNanPairing.confirm", error, false); goto done; }
    (void)esp32_mquickjs_wifi_nan_service(); ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "PIN pairing requires six digits; verification and rejection must omit PIN");
done:
    esp32_mquickjs_wireless_secure_zero(&pin, sizeof(pin));
    esp32_mquickjs_wifi_nan_pairing_release(p);
    JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &input_ref);
    return ok ? JS_UNDEFINED : JS_EXCEPTION;
}
