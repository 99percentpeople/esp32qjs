static bool mesh_manual_ssid(JSContext *ctx, JSValue value, uint8_t *out)
{
    if (JS_IsString(ctx, value)) return mesh_string(ctx, value, out, 1, 32, NULL);
    uint16_t length;
    return mesh_bytes(ctx, value, out, 32, &length) && length && !memchr(out, 0, length);
}

static bool mesh_manual_options(JSContext *ctx, JSGCRef *input, esp32_mquickjs_future_driver_state_t *state)
{
    mesh_operation_t op = state->operation;
    static const char *const parent_keys[] = {"ssid", "password", "bssid", "channel", "meshId", "type", "layer", "timeoutMs"};
    static const char *const scan_keys[] = {"ssid", "bssid", "channel", "channels", "showHidden", "mode", "activeMinMs",
        "activeMaxMs", "passiveMs", "homeChannelDwellMs", "coexistenceBackgroundScan", "timeoutMs"};
    static const char *const result_keys[] = {"scanId", "timeoutMs"};
    const char *const *keys = op == MESH_SET_PARENT ? parent_keys : op == MESH_SCAN ? scan_keys : result_keys;
    size_t count = op == MESH_SET_PARENT ? sizeof(parent_keys)/sizeof(*parent_keys) :
        op == MESH_SCAN ? sizeof(scan_keys)/sizeof(*scan_keys) : sizeof(result_keys)/sizeof(*result_keys);
    bool absent = JS_IsUndefined(input->val);
    if ((!absent || op != MESH_SCAN) && !esp32_mquickjs_validate_plain_options(ctx, input->val, mesh_methods[op], keys, count)) return false;
    esp32_mquickjs_wifi_mesh_control_data_t *d = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, sizeof(*d),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!d) { JS_ThrowOutOfMemory(ctx); return false; }
    esp32_mquickjs_wifi_mesh_control_t control = {.detail = d};
    control.kind = op == MESH_SET_PARENT ? ESP32_MQUICKJS_MESH_SET_PARENT : op == MESH_SCAN ?
        ESP32_MQUICKJS_MESH_SCAN : op == MESH_SCAN_NEXT ? ESP32_MQUICKJS_MESH_SCAN_NEXT : ESP32_MQUICKJS_MESH_SCAN_FLUSH;
    JSGCRef field_ref, array_ref;
    JSValue *field = JS_PushGCRef(ctx, &field_ref), *array = JS_PushGCRef(ctx, &array_ref);
    uint32_t number; uint16_t length; size_t size, choice; bool ok = false;
#define FIELD(key) do { *field = absent ? JS_UNDEFINED : JS_GetPropertyStr(ctx, input->val, key); if (JS_IsException(*field)) goto done; } while (0)
#define NUMBER(key, min, max, target, required) do { FIELD(key); if ((required) || !JS_IsUndefined(*field)) { \
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, min, max, &number)) { goto invalid; } target = number; } } while (0)
#define BOOLEAN(key, target) do { FIELD(key); if (!JS_IsUndefined(*field)) { \
    if (!JS_IsBool(*field)) { goto invalid; } target = *field == JS_TRUE; } } while (0)
    NUMBER("timeoutMs", 1, ESP32_MQUICKJS_MESH_MAX_WAIT_MS, state->timeout_ms, false);
    if (op == MESH_SET_PARENT) {
        wifi_sta_config_t *p = &d->data.parent.sta;
        FIELD("ssid"); if (!mesh_manual_ssid(ctx, *field, p->ssid)) goto invalid;
        FIELD("password"); if (!JS_IsUndefined(*field) &&
            (!mesh_string(ctx, *field, p->password, 0, 64, &size) || (size && size < 8))) goto invalid;
        FIELD("bssid"); if (!JS_IsUndefined(*field)) {
            if (!mesh_bytes(ctx, *field, p->bssid, 6, &length) || length != 6 || (p->bssid[0] & 1U)) goto invalid;
            uint8_t any = 0; for (unsigned i = 0; i < 6; ++i) any |= p->bssid[i];
            if (!any) goto invalid;
            p->bssid_set = true;
        }
        FIELD("meshId"); if (!JS_IsUndefined(*field)) {
            if (!mesh_bytes(ctx, *field, d->address.addr, 6, &length) || length != 6) goto invalid;
            uint8_t any = 0; for (unsigned i = 0; i < 6; ++i) any |= d->address.addr[i];
            if (!any) goto invalid;
            d->has_address = true;
        }
        NUMBER("channel", 1, 14, p->channel, true);
        static const char *const types[] = {"root", "node", "leaf"};
        FIELD("type"); if (!esp32_mquickjs_value_to_enum(ctx, *field, types, 3, &choice)) goto invalid;
        d->number = choice == 0 ? MESH_ROOT : choice == 1 ? MESH_NODE : MESH_LEAF;
        NUMBER("layer", 1, ESP32_MQUICKJS_MESH_MAX_NODES, d->duration, true);
        if ((d->number == MESH_ROOT && d->duration != MESH_ROOT_LAYER) ||
            (d->number == MESH_NODE && d->duration <= MESH_ROOT_LAYER)) goto invalid;
    } else if (op == MESH_SCAN) {
        esp32_mquickjs_wifi_mesh_scan_config_t *s = &d->data.scan;
        s->config.show_hidden = true; s->config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        s->config.home_chan_dwell_time = 30; s->config.channel_bitmap.ghz_5_channels = 1;
        FIELD("mode"); if (!JS_IsUndefined(*field)) {
            static const char *const modes[] = {"active", "passive"};
            if (!esp32_mquickjs_value_to_enum(ctx, *field, modes, 2, &choice)) goto invalid;
            s->config.scan_type = choice ? WIFI_SCAN_TYPE_PASSIVE : WIFI_SCAN_TYPE_ACTIVE;
        }
        FIELD("ssid"); if (!JS_IsUndefined(*field)) {
            if (!mesh_manual_ssid(ctx, *field, s->ssid)) goto invalid;
            s->has_ssid = true;
        }
        FIELD("bssid"); if (!JS_IsUndefined(*field)) {
            if (!mesh_bytes(ctx, *field, s->bssid, 6, &length) || length != 6 || (s->bssid[0] & 1U)) goto invalid;
            uint8_t any = 0; for (unsigned i = 0; i < 6; ++i) any |= s->bssid[i];
            if (!any) goto invalid;
            s->has_bssid = true;
        }
        NUMBER("channel", 0, 14, s->config.channel, false);
        FIELD("channels"); if (!JS_IsUndefined(*field)) {
            if (s->config.channel || !JS_IsArray(ctx, *field)) goto invalid;
            *array = *field; *field = JS_GetPropertyStr(ctx, *array, "length");
            if (JS_IsException(*field) || !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 14, &number)) goto invalid;
            uint32_t channels = number;
            for (uint32_t i = 0; i < channels; ++i) {
                *field = JS_GetPropertyUint32(ctx, *array, i);
                if (JS_IsException(*field) || !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 14, &number) ||
                    (s->config.channel_bitmap.ghz_2_channels & (1U << number))) goto invalid;
                s->config.channel_bitmap.ghz_2_channels |= 1U << number;
            }
        }
        BOOLEAN("showHidden", s->config.show_hidden);
        BOOLEAN("coexistenceBackgroundScan", s->config.coex_background_scan);
        NUMBER("homeChannelDwellMs", 30, 150, s->config.home_chan_dwell_time, false);
        if (s->config.scan_type == WIFI_SCAN_TYPE_ACTIVE) {
            s->config.scan_time.active.max = 120;
            NUMBER("activeMinMs", 0, 1500, s->config.scan_time.active.min, false);
            NUMBER("activeMaxMs", 1, 1500, s->config.scan_time.active.max, false);
            if (s->config.scan_time.active.min > s->config.scan_time.active.max) goto invalid;
            FIELD("passiveMs"); if (!JS_IsUndefined(*field)) goto invalid;
        } else {
            s->config.scan_time.passive = 360;
            NUMBER("passiveMs", 1, 1500, s->config.scan_time.passive, false);
            FIELD("activeMinMs"); if (!JS_IsUndefined(*field)) goto invalid;
            FIELD("activeMaxMs"); if (!JS_IsUndefined(*field)) goto invalid;
        }
    } else NUMBER("scanId", 1, UINT32_MAX, d->scan_identity, true);
    state->error = esp32_mquickjs_wifi_mesh_job_create(state->session, NULL, &control, state->timeout_ms, &state->job);
    if (state->error) { mesh_error(ctx, mesh_methods[op], state->session, state->error, false); goto done; }
    ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid Mesh parent/scan options");
done:
    esp32_mquickjs_wireless_secure_zero(d, sizeof(*d)); esp32_mquickjs_memory_payload_free(d);
    JS_PopGCRef(ctx, &array_ref); JS_PopGCRef(ctx, &field_ref); return ok;
#undef FIELD
#undef NUMBER
#undef BOOLEAN
}

static JSValue mesh_association_to_js(JSContext *ctx, const esp32_mquickjs_wifi_mesh_scan_record_t *record)
{
    _Static_assert(sizeof(mesh_chain_assoc_t) <= ESP32_MQUICKJS_MESH_SCAN_IE_BYTES, "Mesh scan IE storage");
    bool chain = record->ie_length == sizeof(mesh_chain_assoc_t);
    if (!chain && record->ie_length != sizeof(mesh_assoc_t)) return JS_NULL;
    mesh_chain_assoc_t copied = {0}; memcpy(&copied, record->ie, record->ie_length);
    const mesh_assoc_t *a = &copied.tree;
    if (a->eid != 221 || memcmp(a->oui, "\x18\xfe\x34", 3) || (a->version != 1 && a->version != 2)) return JS_NULL;
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref); *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "topology", JS_NewString(ctx, chain ? "chain" : "tree"));
    SET(result, "ieVersion", JS_NewInt32(ctx, a->version)); SET(result, "ieType", JS_NewInt32(ctx, a->type));
    SET(result, "encrypted", JS_NewBool(a->encrypted)); SET(result, "type", JS_NewInt32(ctx, a->mesh_type));
    SET(result, "meshId", mesh_address(ctx, a->mesh_id, 6));
    SET(result, "layer", JS_NewInt32(ctx, chain ? copied.chain.layer : a->layer));
    SET(result, "layerCapacity", JS_NewInt32(ctx, chain ? copied.chain.layer_cap : a->layer_cap));
    SET(result, "connections", JS_NewInt32(ctx, a->assoc)); SET(result, "connectionCapacity", JS_NewInt32(ctx, a->assoc_cap));
    SET(result, "leaves", JS_NewInt32(ctx, a->leaf_assoc)); SET(result, "leafCapacity", JS_NewInt32(ctx, a->leaf_cap));
    SET(result, "rootCapacity", JS_NewInt32(ctx, a->root_cap)); SET(result, "selfCapacity", JS_NewInt32(ctx, a->self_cap));
    SET(result, "layer2Capacity", JS_NewInt32(ctx, a->layer2_cap)); SET(result, "scanAPs", JS_NewInt32(ctx, a->scan_ap_num));
    SET(result, "parentRssi", JS_NewInt32(ctx, a->rssi)); SET(result, "routerRssi", JS_NewInt32(ctx, a->router_rssi));
    SET(result, "flags", JS_NewInt32(ctx, a->flag)); SET(result, "rootCandidate", mesh_address(ctx, a->rc_addr, 6));
    SET(result, "rootCandidateRssi", JS_NewInt32(ctx, a->rc_rssi)); SET(result, "votedAddress", mesh_address(ctx, a->vote_addr, 6));
    SET(result, "votedRssi", JS_NewInt32(ctx, a->vote_rssi)); SET(result, "voteTTL", JS_NewInt32(ctx, a->vote_ttl));
    SET(result, "votes", JS_NewInt32(ctx, a->votes)); SET(result, "myVotes", JS_NewInt32(ctx, a->my_votes));
    SET(result, "voteReason", JS_NewInt32(ctx, a->reason)); SET(result, "child", mesh_address(ctx, a->child, 6));
    SET(result, "toDS", JS_NewInt32(ctx, a->toDS));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue mesh_scan_record_to_js(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state,
    const esp32_mquickjs_wifi_mesh_scan_record_t *record)
{
    if (!record || !record->sequence) return JS_NULL;
    JSGCRef result_ref, view_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref), *view = JS_PushGCRef(ctx, &view_ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    SET(result, "scanId", JS_NewUint32(ctx, record->identity)); SET(result, "sequence", JS_NewUint32(ctx, record->sequence));
    SET(result, "accessPoint", esp32_mquickjs_wifi_scan_record_to_js(ctx, &record->ap));
    SET(result, "bssidBytes", mesh_address(ctx, record->ap.bssid, 6));
    SET(result, "association", mesh_association_to_js(ctx, record));
    uint8_t *bytes = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, record->ie_length ? record->ie_length : 1,
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
    if (!bytes) { JS_ThrowOutOfMemory(ctx); goto fail; }
    memcpy(bytes, record->ie, record->ie_length);
    *view = esp32_mquickjs_new_wireless_owned_byte_view("wifi.mesh", ctx, bytes, record->ie_length);
    if (JS_IsException(*view)) goto fail;
    SET(result, "meshIE", *view);
    esp_err_t error = esp32_mquickjs_wifi_mesh_scan_commit(state->session, state->read_identity, record->identity, record->sequence);
    if (error) { mesh_error(ctx, "WiFiMeshSession.receiveScan", state->session, error, false); goto fail; }
    JS_PopGCRef(ctx, &view_ref); return JS_PopGCRef(ctx, &result_ref);
fail:
    if (!JS_IsUndefined(*view) && !JS_IsException(*view)) (void)js_byte_view_close(ctx, view, 0, NULL);
    JS_PopGCRef(ctx, &view_ref); JS_PopGCRef(ctx, &result_ref); return JS_EXCEPTION;
}
