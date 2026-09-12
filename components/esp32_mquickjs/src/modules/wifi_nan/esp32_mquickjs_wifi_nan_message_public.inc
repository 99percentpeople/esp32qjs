static JSValue nan_message_result_to_js(JSContext *ctx, const esp32_mquickjs_wifi_nan_message_status_t *s)
{
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    SET(result, "identity", JS_NewUint32(ctx, s->identity));
    SET(result, "serviceIdentity", JS_NewUint32(ctx, s->service_identity));
    SET(result, "serviceId", JS_NewInt32(ctx, s->service_id));
    SET(result, "peerServiceId", JS_NewInt32(ctx, s->peer_id));
    SET(result, "peerMac", nan_event_bytes(ctx, s->peer, 6));
    SET(result, "bytes", JS_NewInt32(ctx, s->bytes));
    SET(result, "submitStarted", JS_NewBool(s->submit_started));
    SET(result, "txSubmitted", JS_NewBool(s->submitted));
    SET(result, "txCompleted", JS_NewBool(s->tx.tx_done));
    SET(result, "txSucceeded", JS_NewBool(s->tx.tx_succeeded));
    SET(result, "bufferRetired", JS_NewBool(s->tx.buffer_retired));
    SET(result, "cancelRequested", JS_NewBool(s->cancelled));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue nan_message_error(JSContext *ctx, esp32_mquickjs_wifi_nan_message_t *message, esp_err_t error, bool timeout)
{
    esp32_mquickjs_wifi_nan_message_status_t status;
    esp32_mquickjs_wifi_nan_message_status(message, &status);
    if (!error) error = status.error ? status.error : ESP_ERR_INVALID_STATE;
    JSGCRef ref; JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = nan_message_result_to_js(ctx, &status); if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "waitTimedOut", JS_NewBool(timeout));
    (void)esp32_mquickjs_throw_native_error(ctx, timeout || status.timed_out ? "WIFI_NAN_TIMEOUT" :
        status.cancelled ? "WIFI_NAN_CLOSED" : "WIFI_NAN_FAILED", "WiFiNanService.send",
        "NAN message did not complete; inspect send ownership before submitting another message", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static bool nan_message_bytes(JSContext *ctx, JSValue value, uint8_t *out, size_t capacity, uint16_t *length)
{
    return nan_option_bytes(ctx, value, out, capacity, length, false);
}

typedef struct {
    wifi_nan_followup_params_t params;
    uint8_t ssi[ESP_WIFI_MAX_FUP_SSI_LEN];
    nan_vendor_ie_t vendor;
    uint8_t vendor_body[NAN_VENDOR_IE_MAX_BODY_LEN];
} nan_message_options_t;
static bool nan_send_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out)
{
    *out = NULL;
    if (argc != 1) { JS_ThrowTypeError(ctx, "NAN send requires one options object"); return false; }
    esp32_mquickjs_wifi_nan_discovery_t *service = nan_service_receiver(ctx, self->val);
    if (!service) return false;
    if (!esp32_mquickjs_wifi_nan_discovery_retain(service)) { JS_ThrowInternalError(ctx, "NAN reference exhausted"); return false; }
    nan_message_options_t *options = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*options),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    esp32_mquickjs_future_driver_state_t *state = NULL;
    JSGCRef field_ref; JSValue *field = JS_PushGCRef(ctx, &field_ref);
    bool ok = false; uint32_t peer_id = 0, timeout_ms = 10000; uint16_t mac_length = 0;
    if (!options) { JS_ThrowOutOfMemory(ctx); goto done; }
    static const char *const keys[] = {"peerServiceId", "peerMac", "ssi", "timeoutMs", "vendor"};
    if (!esp32_mquickjs_validate_plain_options(ctx, argv[0].val, "NAN send", keys, 5)) goto done;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, argv[0].val, name); if (JS_IsException(*field)) goto done; } while (0)
    FIELD("peerServiceId"); if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 255, &peer_id)) goto invalid;
    options->params.peer_inst_id = peer_id;
    FIELD("timeoutMs"); if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 120000, &timeout_ms)) goto invalid;
    FIELD("peerMac"); if (!nan_message_bytes(ctx, *field, options->params.peer_mac, 6, &mac_length)) goto done;
    if (mac_length != 6 || (options->params.peer_mac[0] & 1) || !memcmp(options->params.peer_mac, (uint8_t[6]){0}, 6)) goto invalid;
    FIELD("ssi");
    if (!JS_IsUndefined(*field) && !nan_message_bytes(ctx, *field, options->ssi, sizeof(options->ssi), &options->params.ssi_len)) goto done;
    options->params.ssi = options->params.ssi_len ? options->ssi : NULL;
    FIELD("vendor");
    if (!JS_IsUndefined(*field)) {
        if (!nan_vendor_options(ctx, &field_ref, &options->vendor, options->vendor_body)) goto done;
        options->params.vendor_ie = &options->vendor;
    }
    state = esp32_mquickjs_memory_wireless_calloc("wireless.future", 1, sizeof(*state),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!state) { JS_ThrowOutOfMemory(ctx); goto done; }
    esp_err_t error = esp32_mquickjs_wifi_nan_message_create(service, &options->params, timeout_ms, &state->message);
    if (error) { (void)nan_service_error(ctx, "WiFiNanService.send", service, error, false); goto done; }
    state->operation = NAN_SERVICE_SEND; state->timeout_ms = timeout_ms; *out = state; state = NULL; ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid NAN send options");
done:
#undef FIELD
    if (state) { esp32_mquickjs_wifi_nan_message_release(state->message); esp32_mquickjs_memory_payload_free(state); }
    if (options) { esp32_mquickjs_wireless_secure_zero(options, sizeof(*options)); esp32_mquickjs_memory_payload_free(options); }
    esp32_mquickjs_wifi_nan_discovery_release(service); JS_PopGCRef(ctx, &field_ref);
    return ok;
}
