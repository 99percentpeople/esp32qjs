static esp32_mquickjs_wifi_nan_path_t *nan_path_receiver(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_NAN_PATH) {
        JS_ThrowTypeError(ctx, "expected WiFiNanDataPath"); return NULL;
    }
    esp32_mquickjs_wifi_nan_path_t *p = JS_GetOpaque(ctx, value);
    if (!p) JS_ThrowReferenceError(ctx, "invalid WiFiNanDataPath");
    return p;
}

static JSValue nan_path_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_nan_path_status_t *s)
{
    const esp32_mquickjs_wifi_nan_ndp_status_t *n = &s->native;
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    SET(result, "identity", JS_NewUint32(ctx, s->identity));
    SET(result, "sessionIdentity", JS_NewUint32(ctx, s->session_identity));
    SET(result, "serviceIdentity", JS_NewUint32(ctx, s->service_identity));
    SET(result, "nativeIdentity", n->identity ? JS_NewUint32(ctx, n->identity) : JS_NULL);
    SET(result, "dataPathId", n->ndp_id ? JS_NewInt32(ctx, n->ndp_id) : JS_NULL);
    SET(result, "publisherId", JS_NewInt32(ctx, n->publisher_id));
    SET(result, "direction", JS_NewString(ctx, s->outgoing ? "outgoing" : "incoming"));
    SET(result, "state", JS_NewString(ctx, s->retired ? "closed" : s->closing ? "closing" :
        s->ready ? "connected" : !s->outgoing && !s->response_requested ? "pending" : "connecting"));
    SET(result, "peerMac", nan_event_bytes(ctx, n->peer, 6));
    SET(result, "peerDataMac", nan_event_bytes(ctx, n->peer_ndi, 6));
    SET(result, "localDataMac", nan_event_bytes(ctx, n->own_ndi, 6));
    SET(result, "ipv6Identifier", nan_event_bytes(ctx, n->ipv6_identifier, 8));
    SET(result, "ssi", nan_event_bytes(ctx, s->ssi, s->ssi_len));
    SET(result, "ready", JS_NewBool(s->ready));
    SET(result, "closeRequested", JS_NewBool(s->closing));
    SET(result, "cleanupPending", JS_NewBool(s->closing && !s->retired));
    SET(result, "timedOut", JS_NewBool(s->timed_out));
    SET(result, "responseRequested", JS_NewBool(s->response_requested));
    SET(result, "acceptRequested", s->response_requested ? JS_NewBool(s->accept) : JS_NULL);
    SET(result, "requestSubmitted", JS_NewBool(n->submitted));
    SET(result, "responseAttempted", JS_NewBool(n->response_attempted));
    SET(result, "endAttempted", JS_NewBool(n->end_attempted));
    SET(result, "endSubmitted", JS_NewBool(n->end_submitted));
    SET(result, "nativeDeleted", JS_NewBool(n->native_deleted));
    SET(result, "framesRetired", JS_NewBool(n->frames_retired));
    SET(result, "nativeStatus", JS_NewInt32(ctx, n->native_status));
    SET(result, "error", s->error ? JS_NewInt32(ctx, s->error) : JS_NULL);
    SET(result, "cleanupError", s->cleanup_error ? JS_NewInt32(ctx, s->cleanup_error) : JS_NULL);
    SET(result, "reservedBytes", JS_NewUint32(ctx, s->reserved_bytes));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue nan_path_error(JSContext *ctx, esp32_mquickjs_wifi_nan_path_t *path,
    const char *operation, esp_err_t error, bool timeout)
{
    esp32_mquickjs_wifi_nan_path_status_t status = {0};
    if (path) esp32_mquickjs_wifi_nan_path_status(path, &status);
    if (!error) error = status.error ? status.error : status.cleanup_error ? status.cleanup_error : ESP_ERR_INVALID_STATE;
    JSGCRef ref; JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = nan_path_status_to_js(ctx, &status); if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "waitTimedOut", JS_NewBool(timeout));
    (void)esp32_mquickjs_throw_native_error(ctx, timeout || status.timed_out ? "WIFI_NAN_TIMEOUT" :
        status.closing && !status.error ? "WIFI_NAN_CLOSED" : "WIFI_NAN_FAILED", operation,
        "NAN data path did not complete; inspect connection and Session cleanup", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

void js_wifi_nan_path_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    esp32_mquickjs_wifi_nan_path_close(opaque, false);
    esp32_mquickjs_wifi_nan_path_release(opaque);
}
JSValue js_wifi_nan_path_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use NAN service.requestDataPath() or receiveDataPath()");
}
JSValue js_wifi_nan_path_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "NAN data path status expects no arguments");
    esp32_mquickjs_wifi_nan_path_t *path = nan_path_receiver(ctx, *self);
    if (!path) return JS_EXCEPTION;
    esp32_mquickjs_wifi_nan_path_status_t status;
    esp32_mquickjs_wifi_nan_path_status(path, &status); return nan_path_status_to_js(ctx, &status);
}
JSValue js_wifi_nan_path_cancel(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "NAN data path cancel expects no arguments");
    esp32_mquickjs_wifi_nan_path_t *path = nan_path_receiver(ctx, *self);
    if (!path) return JS_EXCEPTION;
    esp32_mquickjs_wifi_nan_path_close(path, false); (void)esp32_mquickjs_wifi_nan_service(); return JS_UNDEFINED;
}

JSValue js_wifi_nan_path_request(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    if (argc != 1) return JS_ThrowTypeError(ctx, "requestDataPath requires one options object");
    esp32_mquickjs_wifi_nan_discovery_t *service = nan_service_receiver(ctx, *self);
    if (!service) return JS_EXCEPTION;
    if (!esp32_mquickjs_wifi_nan_discovery_retain(service)) return JS_ThrowInternalError(ctx, "NAN reference exhausted");
    JSGCRef input_ref, field_ref, result_ref;
    JSValue *input = JS_PushGCRef(ctx, &input_ref), *field = JS_PushGCRef(ctx, &field_ref), *result = JS_PushGCRef(ctx, &result_ref);
    *input = argv[0]; wifi_nan_datapath_req_t request = {.confirm_required = true};
    uint32_t peer = 0, timeout_ms = 10000; uint16_t length = 0;
    esp32_mquickjs_wifi_nan_path_t *path = NULL;
    static const char *const keys[] = {"peerServiceId", "peerMac", "confirmRequired", "timeoutMs"};
    if (!esp32_mquickjs_validate_plain_options(ctx, *input, "requestDataPath", keys, 4)) goto fail;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, *input, name); if (JS_IsException(*field)) goto fail; } while (0)
    FIELD("peerServiceId"); if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 255, &peer)) goto invalid;
    request.pub_id = peer;
    FIELD("peerMac"); if (!nan_message_bytes(ctx, *field, request.peer_mac, 6, &length)) goto fail;
    if (length != 6 || (request.peer_mac[0] & 1) || !memcmp(request.peer_mac, (uint8_t[6]){0}, 6)) goto invalid;
    FIELD("confirmRequired");
    if (!JS_IsUndefined(*field)) { if (!JS_IsBool(*field)) goto invalid; request.confirm_required = *field == JS_TRUE; }
    FIELD("timeoutMs");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 120000, &timeout_ms)) goto invalid;
    esp_err_t error = esp32_mquickjs_wifi_nan_path_create(service, &request, timeout_ms, &path);
    if (error) { (void)nan_service_error(ctx, "WiFiNanService.requestDataPath", service, error, false); goto fail; }
    *result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_NAN_PATH); if (JS_IsException(*result)) goto fail;
    /* No fallible JS construction remains when native activation begins. */
    error = esp32_mquickjs_wifi_nan_path_activate(path);
    if (error) { (void)nan_path_error(ctx, path, "WiFiNanService.requestDataPath", error, false); goto fail; }
    JS_SetOpaque(ctx, *result, path); path = NULL;
    (void)esp32_mquickjs_wifi_nan_service();
    esp32_mquickjs_wifi_nan_discovery_release(service);
    JSValue value = JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &input_ref);
    return value;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid NAN data path options");
fail:
#undef FIELD
    esp32_mquickjs_wifi_nan_path_release(path); esp32_mquickjs_wifi_nan_discovery_release(service);
    JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &input_ref); return JS_EXCEPTION;
}

static JSValue nan_path_receive_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (!state->path) return nan_service_error(ctx, "WiFiNanService.receiveDataPath", state->discovery, ESP_ERR_INVALID_STATE, false);
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_NAN_PATH);
    if (JS_IsException(*result)) { JS_PopGCRef(ctx, &ref); return JS_EXCEPTION; }
    if (!esp32_mquickjs_wifi_nan_path_deliver(state->path)) {
        JS_PopGCRef(ctx, &ref);
        return nan_path_error(ctx, state->path, "WiFiNanService.receiveDataPath", ESP_ERR_INVALID_STATE, false);
    }
    JS_SetOpaque(ctx, *result, state->path); state->path = NULL; /* Transfer the reserved reference. */
    return JS_PopGCRef(ctx, &ref);
}

static bool nan_path_respond_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out)
{
    *out = NULL;
    if (argc != 1) { JS_ThrowTypeError(ctx, "respond requires explicit accept options"); return false; }
    esp32_mquickjs_wifi_nan_path_t *path = nan_path_receiver(ctx, self->val);
    if (!path) return false;
    if (!esp32_mquickjs_wifi_nan_path_retain(path)) { JS_ThrowInternalError(ctx, "NAN reference exhausted"); return false; }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc("wireless.future", 1,
        sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!state) { esp32_mquickjs_wifi_nan_path_release(path); JS_ThrowOutOfMemory(ctx); return false; }
    state->path = path; state->operation = NAN_PATH_RESPOND; state->timeout_ms = 10000;
    state->response = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*state->response),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    JSGCRef ref; JSValue *field = JS_PushGCRef(ctx, &ref);
    bool ok = false;
    if (!state->response) { JS_ThrowOutOfMemory(ctx); goto done; }
    static const char *const keys[] = {"accept", "ssi", "timeoutMs"};
    if (!esp32_mquickjs_validate_plain_options(ctx, argv[0].val, "NAN respond", keys, 3)) goto done;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, argv[0].val, name); if (JS_IsException(*field)) goto done; } while (0)
    FIELD("accept"); if (!JS_IsBool(*field)) goto invalid;
    state->response->accept = *field == JS_TRUE;
    FIELD("timeoutMs"); if (!JS_IsUndefined(*field) &&
        !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 120000, &state->timeout_ms)) goto invalid;
    FIELD("ssi"); if (!JS_IsUndefined(*field) && !nan_message_bytes(ctx, *field, state->response->ssi,
        sizeof(state->response->ssi), &state->response->length)) goto done;
    *out = state; state = NULL; ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid NAN response options");
done:
#undef FIELD
    nan_destroy(state); JS_PopGCRef(ctx, &ref); return ok;
}
