#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
static JSValue nan_query_peer_to_js(JSContext *ctx, const struct nan_peer_record *peer)
{
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "serviceId", JS_NewInt32(ctx, peer->own_svc_id));
    SET(result, "peerServiceId", JS_NewInt32(ctx, peer->peer_svc_id));
    SET(result, "peerType", JS_NewString(ctx, peer->peer_svc_type == ESP_NAN_PUBLISH ? "publish" : "subscribe"));
    SET(result, "peerMac", nan_event_bytes(ctx, peer->peer_nmi, 6));
    SET(result, "ndpId", peer->ndp_id ? JS_NewInt32(ctx, peer->ndp_id) : JS_NULL);
    SET(result, "peerDataMac", peer->ndp_id ? nan_event_bytes(ctx, peer->peer_ndi, 6) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue nan_query_to_js(JSContext *ctx, esp32_mquickjs_wifi_nan_query_kind_t kind,
    const esp32_mquickjs_wifi_nan_query_result_t *snapshot)
{
    if (kind == ESP32_MQUICKJS_NAN_QUERY_PEER) return nan_query_peer_to_js(ctx, &snapshot->peers[0]);
    JSGCRef ref, array_ref, peer_ref;
    JSValue *result = JS_PushGCRef(ctx, &ref), *array = JS_PushGCRef(ctx, &array_ref),
        *peer = JS_PushGCRef(ctx, &peer_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "serviceId", JS_NewInt32(ctx, snapshot->service_id));
    SET(result, "name", JS_NewString(ctx, snapshot->service_name));
    SET(result, "peerCount", JS_NewInt32(ctx, snapshot->peer_count));
    if (kind == ESP32_MQUICKJS_NAN_QUERY_PEERS) {
        *array = JS_NewArray(ctx, 0);
        if (JS_IsException(*array)) goto fail;
        for (unsigned i = 0; i < snapshot->peer_count; ++i) {
            *peer = nan_query_peer_to_js(ctx, &snapshot->peers[i]);
            if (JS_IsException(*peer) || JS_IsException(JS_SetPropertyUint32(ctx, *array, i, *peer))) goto fail;
        }
        SET(result, "peers", *array);
    }
    JS_PopGCRef(ctx, &peer_ref); JS_PopGCRef(ctx, &array_ref);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &peer_ref); JS_PopGCRef(ctx, &array_ref); JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}

static JSValue nan_query(JSContext *ctx, JSValue *self, int argc, JSValue *argv,
    esp32_mquickjs_wifi_nan_query_kind_t kind, const char *operation)
{
    bool one_peer = kind == ESP32_MQUICKJS_NAN_QUERY_PEER;
    if (argc < 1 || argc > (one_peer ? 2 : 1)) return JS_ThrowTypeError(ctx, "invalid NAN query arguments");
    esp32_mquickjs_wifi_nan_session_t *session = nan_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    if (!esp32_mquickjs_wifi_nan_session_retain(session)) return JS_ThrowInternalError(ctx, "NAN reference exhausted");
    JSGCRef selector_ref, peer_ref;
    JSValue *selector = JS_PushGCRef(ctx, &selector_ref), *peer = JS_PushGCRef(ctx, &peer_ref);
    *selector = one_peer ? argc == 2 ? argv[1] : JS_UNDEFINED : argv[0];
    *peer = one_peer ? argv[0] : JS_UNDEFINED;
    /* Fixed-size copies on the existing runtime task stack. No native storage
     * or Service pointer survives this synchronous metadata query. */
    char name[ESP_WIFI_MAX_SVC_NAME_LEN] = {0};
    esp32_mquickjs_wifi_nan_query_t query = {.kind = kind};
    esp32_mquickjs_wifi_nan_query_result_t snapshot;
    JSValue result = JS_EXCEPTION;
    if (JS_IsString(ctx, *selector)) {
        if (!nan_service_string(ctx, *selector, name, sizeof(name)) || !name[0]) goto invalid;
        query.service_name = name;
    } else if (!(one_peer && JS_IsUndefined(*selector))) {
        uint32_t id;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *selector, 1, UINT8_MAX, &id)) goto invalid;
        query.service_id = id;
    }
    if (one_peer) {
        uint16_t length = 0;
        if (!nan_option_bytes(ctx, *peer, query.peer, sizeof(query.peer), &length, false)) goto done;
        if (length != 6 || (query.peer[0] & 1) || !memcmp(query.peer, (uint8_t[6]){0}, 6)) goto invalid;
    }
    esp_err_t error = esp32_mquickjs_wifi_nan_session_query(session, &query, &snapshot);
    if (error == ESP_ERR_NOT_FOUND) result = JS_NULL;
    else if (error != ESP_OK) result = nan_error(ctx, operation, session, error, false);
    else result = nan_query_to_js(ctx, kind, &snapshot);
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "NAN query expects a service ID 1..255 or nonempty service name, and a unicast peer MAC");
done:
    JS_PopGCRef(ctx, &peer_ref); JS_PopGCRef(ctx, &selector_ref);
    esp32_mquickjs_wifi_nan_session_release(session);
    return result;
}

JSValue js_wifi_nan_get_service_info(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    return nan_query(ctx, self, argc, argv, ESP32_MQUICKJS_NAN_QUERY_SERVICE, "WiFiNanSession.getServiceInfo");
}
JSValue js_wifi_nan_get_peer_info(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    return nan_query(ctx, self, argc, argv, ESP32_MQUICKJS_NAN_QUERY_PEER, "WiFiNanSession.getPeerInfo");
}
JSValue js_wifi_nan_get_peer_records(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    return nan_query(ctx, self, argc, argv, ESP32_MQUICKJS_NAN_QUERY_PEERS, "WiFiNanSession.getPeerRecords");
}
#endif
