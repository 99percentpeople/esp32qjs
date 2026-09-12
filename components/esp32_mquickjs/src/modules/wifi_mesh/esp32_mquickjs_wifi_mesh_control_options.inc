/* Typed capture for the extended Mesh commands. Caller already retains the
 * Session; property getters may allocate, collect or close that same Session. */
static bool mesh_control_options(JSContext *ctx, JSGCRef *input, esp32_mquickjs_future_driver_state_t *state)
{
    mesh_operation_t op = state->operation;
    if (op >= MESH_SET_PARENT) return mesh_manual_options(ctx, input, state);
    static const char *const config_keys[] = {"includeSecrets", "timeoutMs"};
    static const char *const router_keys[] = {"ssid", "password", "bssid", "allowRouterSwitch", "timeoutMs"};
    static const char *const address_keys[] = {"address", "timeoutMs"};
    static const char *const type_keys[] = {"type", "timeoutMs"};
    static const char *const self_keys[] = {"enabled", "selectParent", "timeoutMs"};
    static const char *const bool_keys[] = {"enabled", "timeoutMs"};
    static const char *const expiry_keys[] = {"seconds", "timeoutMs"};
    static const char *const healing_keys[] = {"milliseconds", "timeoutMs"};
    static const char *const ie_keys[] = {"enabled", "key", "timeoutMs"};
    static const char *const vote_keys[] = {"attempts", "percentage", "timeoutMs"};
    static const char *const channel_keys[] = {"channel", "beaconCount", "routerBssid", "timeoutMs"};
    static const char *const device_keys[] = {"duty", "type", "timeoutMs"};
    static const char *const network_keys[] = {"duty", "durationMinutes", "timeoutMs"};
    static const char *const signal_keys[] = {"forwardCount", "timeoutMs"};
    static const char *const wait_keys[] = {"timeoutMs"};
    const char *const *keys = wait_keys; size_t key_count = 1;
#define KEYS(name) do { keys = name; key_count = sizeof(name)/sizeof(*name); } while (0)
    switch (op) {
    case MESH_CONFIGURATION: KEYS(config_keys); break;
    case MESH_SET_ROUTER: KEYS(router_keys); break;
    case MESH_SET_ID: case MESH_SUBNET: case MESH_HAS_GROUP: case MESH_UPSTREAM_CAPACITY: KEYS(address_keys); break;
    case MESH_SET_TYPE: KEYS(type_keys); break;
    case MESH_SELF_ORGANIZED: KEYS(self_keys); break;
    case MESH_FIXED_ROOT: case MESH_ROOT_CONFLICTS: KEYS(bool_keys); break;
    case MESH_ASSOC_EXPIRY: KEYS(expiry_keys); break;
    case MESH_ROOT_HEALING: KEYS(healing_keys); break;
    case MESH_IE_ENCRYPTION: KEYS(ie_keys); break;
    case MESH_WAIVE_ROOT: KEYS(vote_keys); break;
    case MESH_SWITCH_CHANNEL: KEYS(channel_keys); break;
    case MESH_DEVICE_DUTY: KEYS(device_keys); break;
    case MESH_NETWORK_DUTY: KEYS(network_keys); break;
    case MESH_SIGNAL_DUTY: KEYS(signal_keys); break;
    default: break;
    }
#undef KEYS
    bool optional = op == MESH_CONFIGURATION || op == MESH_WAIVE_ROOT || op == MESH_POWER_STATUS || op == MESH_TSF_TIME;
    bool absent = JS_IsUndefined(input->val);
    if ((!absent || !optional) && !esp32_mquickjs_validate_plain_options(ctx, input->val, mesh_methods[op], keys, key_count)) return false;
    esp32_mquickjs_wifi_mesh_control_data_t *d = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, sizeof(*d),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!d) { JS_ThrowOutOfMemory(ctx); return false; }
    esp32_mquickjs_wifi_mesh_control_t control = {.detail = d};
    /* Public and native extended operations have the same contiguous order. */
    _Static_assert(MESH_TSF_TIME - MESH_CONFIGURATION ==
        ESP32_MQUICKJS_MESH_TSF_TIME - ESP32_MQUICKJS_MESH_CONFIGURATION, "Mesh command ordering");
    control.kind = ESP32_MQUICKJS_MESH_CONFIGURATION + op - MESH_CONFIGURATION;
    JSGCRef field_ref; JSValue *field = JS_PushGCRef(ctx, &field_ref);
    bool ok = false; uint32_t number; uint16_t length; size_t size, choice;
#define FIELD(key) do { *field = absent ? JS_UNDEFINED : JS_GetPropertyStr(ctx, input->val, key); if (JS_IsException(*field)) goto done; } while (0)
#define NUMBER(key, min, max, target, required) do { FIELD(key); if ((required) || !JS_IsUndefined(*field)) { \
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, min, max, &number)) { goto invalid; } target = number; } } while (0)
#define BOOLEAN(key, target, required) do { FIELD(key); if ((required) || !JS_IsUndefined(*field)) { \
    if (!JS_IsBool(*field)) { goto invalid; } target = *field == JS_TRUE; } } while (0)
    NUMBER("timeoutMs", 1, ESP32_MQUICKJS_MESH_MAX_WAIT_MS, state->timeout_ms, false);
    switch (op) {
    case MESH_CONFIGURATION: BOOLEAN("includeSecrets", d->include_secrets, false); break;
    case MESH_SET_ROUTER:
        FIELD("ssid"); if (!mesh_string(ctx, *field, d->data.router.ssid, 1, 32, &size)) goto invalid;
        d->data.router.ssid_len = size;
        FIELD("password"); if (!JS_IsUndefined(*field)) {
            if (!mesh_string(ctx, *field, d->data.router.password, 0, 64, &size) || (size && size < 8)) goto invalid;
        }
        FIELD("bssid"); if (!JS_IsUndefined(*field) &&
            (!mesh_bytes(ctx, *field, d->data.router.bssid, 6, &length) || length != 6)) goto invalid;
        BOOLEAN("allowRouterSwitch", d->data.router.allow_router_switch, false); break;
    case MESH_SET_ID: case MESH_SUBNET: case MESH_HAS_GROUP: case MESH_UPSTREAM_CAPACITY: {
        FIELD("address"); if (!mesh_bytes(ctx, *field, d->address.addr, 6, &length) || length != 6) goto invalid;
        uint8_t any = 0; for (unsigned i = 0; i < 6; ++i) any |= d->address.addr[i];
        if (!any) goto invalid;
        if (op == MESH_SUBNET) control.capacity = ESP32_MQUICKJS_MESH_MAX_NODES;
        break;
    }
    case MESH_SET_TYPE: {
        static const char *const values[] = {"idle", "root", "node", "leaf", "station"};
        FIELD("type"); if (!esp32_mquickjs_value_to_enum(ctx, *field, values, 5, &choice)) goto invalid;
        d->number = choice; break;
    }
    case MESH_SELF_ORGANIZED:
        BOOLEAN("enabled", d->enabled, true); BOOLEAN("selectParent", d->select_parent, false);
        if (!d->enabled && d->select_parent) goto invalid;
        break;
    case MESH_FIXED_ROOT: case MESH_ROOT_CONFLICTS: BOOLEAN("enabled", d->enabled, true); break;
    case MESH_ASSOC_EXPIRY: NUMBER("seconds", 10, INT_MAX / 1000, d->number, true); break;
    case MESH_ROOT_HEALING: NUMBER("milliseconds", 0, INT_MAX, d->number, true); break;
    case MESH_IE_ENCRYPTION:
        BOOLEAN("enabled", d->enabled, true);
        FIELD("key"); if (d->enabled) {
            if (!mesh_string(ctx, *field, d->data.encryption.key, 8, 64, &size)) goto invalid;
            d->data.encryption.length = size;
            for (unsigned i = 0; i < size; ++i) if ((unsigned char)d->data.encryption.key[i] < 32 ||
                (unsigned char)d->data.encryption.key[i] > 126) goto invalid;
        } else if (!JS_IsUndefined(*field)) goto invalid;
        break;
    case MESH_WAIVE_ROOT:
        d->data.vote.config.attempts = 15; d->data.vote.percentage = 0.9f;
        NUMBER("attempts", 15, INT_MAX, d->data.vote.config.attempts, false);
        FIELD("percentage"); if (!JS_IsUndefined(*field) && !mesh_percentage(ctx, *field, &d->data.vote.percentage)) goto invalid;
        break;
    case MESH_SWITCH_CHANNEL:
        d->duration = 15;
        NUMBER("channel", 1, 14, d->number, true); NUMBER("beaconCount", 1, UINT8_MAX, d->duration, false);
        FIELD("routerBssid"); if (!JS_IsUndefined(*field)) {
            if (!mesh_bytes(ctx, *field, d->address.addr, 6, &length) || length != 6) goto invalid;
            uint8_t any = 0; for (unsigned i = 0; i < 6; ++i) any |= d->address.addr[i];
            if (!any) goto invalid;
            d->has_address = true;
        }
        break;
    case MESH_DEVICE_DUTY: {
        static const char *const values[] = {"request", "demand"};
        NUMBER("duty", 1, 100, d->number, true);
        FIELD("type"); if (!esp32_mquickjs_value_to_enum(ctx, *field, values, 2, &choice)) goto invalid;
        d->duty_type = choice ? MESH_PS_DEVICE_DUTY_DEMAND : MESH_PS_DEVICE_DUTY_REQUEST; break;
    }
    case MESH_NETWORK_DUTY: {
        NUMBER("duty", 1, 100, d->number, true);
        int32_t duration;
        FIELD("durationMinutes"); if (!esp32_mquickjs_value_to_bounded_i32(ctx, *field, -1, INT_MAX, &duration) || !duration) goto invalid;
        d->duration = duration; d->rule = MESH_PS_NETWORK_DUTY_APPLIED_ENTIRE; break;
    }
    case MESH_SIGNAL_DUTY: NUMBER("forwardCount", 0, UINT8_MAX - 1, d->number, true); break;
    default: break;
    }
    state->error = esp32_mquickjs_wifi_mesh_job_create(state->session, NULL, &control, state->timeout_ms, &state->job);
    if (state->error) { mesh_error(ctx, mesh_methods[op], state->session, state->error, false); goto done; }
    ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid Mesh control options");
done:
    esp32_mquickjs_wireless_secure_zero(d, sizeof(*d)); esp32_mquickjs_memory_payload_free(d);
    JS_PopGCRef(ctx, &field_ref); return ok;
#undef FIELD
#undef NUMBER
#undef BOOLEAN
}

static JSValue mesh_control_to_js(JSContext *ctx, mesh_operation_t op, const esp32_mquickjs_wifi_mesh_control_data_t *d)
{
    if (op == MESH_HAS_GROUP) return JS_NewBool(d->result);
    if (op == MESH_TSF_TIME) return JS_NewFloat64(ctx, d->time_us);
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref); *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    if (op == MESH_UPSTREAM_CAPACITY) {
        SET(result, "available", JS_NewInt32(ctx, d->number)); SET(result, "lastSequence", JS_NewUint32(ctx, d->sequence));
    } else if (op == MESH_POWER_STATUS) {
        SET(result, "enabled", JS_NewBool(d->data.power.enabled)); SET(result, "active", JS_NewBool(d->data.power.active));
        SET(result, "deviceDuty", JS_NewInt32(ctx, d->data.power.device)); SET(result, "deviceType", JS_NewInt32(ctx, d->data.power.device_type));
        SET(result, "networkDuty", JS_NewInt32(ctx, d->data.power.network)); SET(result, "durationMinutes", JS_NewInt32(ctx, d->data.power.duration));
        SET(result, "networkType", JS_NewInt32(ctx, d->data.power.type)); SET(result, "appliedRule", JS_NewInt32(ctx, d->data.power.rule));
        SET(result, "runningDuty", JS_NewInt32(ctx, d->data.power.running));
    } else if (op == MESH_CONFIGURATION) {
        const esp32_mquickjs_wifi_mesh_config_t *c = &d->data.configuration.config;
        SET(result, "meshId", mesh_address(ctx, c->network.mesh_id.addr, 6));
        SET(result, "routerSsidBytes", mesh_address(ctx, c->network.router.ssid, c->network.router.ssid_len));
        SET(result, "routerSsid", esp32_mquickjs_wifi_ssid_text(ctx, c->network.router.ssid, c->network.router.ssid_len));
        SET(result, "routerBssid", mesh_address(ctx, c->network.router.bssid, 6));
        SET(result, "allowRouterSwitch", JS_NewBool(c->network.router.allow_router_switch));
        SET(result, "allowChannelSwitch", JS_NewBool(c->network.allow_channel_switch));
        SET(result, "channel", JS_NewInt32(ctx, c->network.channel));
        SET(result, "topology", c->topology == MESH_TOPO_TREE ? JS_NewString(ctx, "tree") :
            c->topology == MESH_TOPO_CHAIN ? JS_NewString(ctx, "chain") : JS_NULL);
        /* Authentication is an SDK value, so unknown future driver values are
         * preserved without inventing a public authentication string. */
        SET(result, "apAuthentication", JS_NewInt32(ctx, c->ap_authmode));
        SET(result, "maxLayer", JS_NewInt32(ctx, c->max_layer)); SET(result, "capacity", JS_NewInt32(ctx, c->capacity));
        SET(result, "receiveQueue", JS_NewInt32(ctx, c->receive_queue)); SET(result, "sendBlockMs", JS_NewUint32(ctx, c->send_block_ms));
        SET(result, "maxConnections", JS_NewInt32(ctx, c->network.mesh_ap.max_connection));
        SET(result, "nonMeshConnections", JS_NewInt32(ctx, c->network.mesh_ap.nonmesh_max_connection));
        SET(result, "fixedRoot", JS_NewBool(c->fixed_root)); SET(result, "selfOrganized", JS_NewBool(c->self_organized));
        SET(result, "powerSave", JS_NewBool(c->power_save)); SET(result, "votePercentage", JS_NewFloat64(ctx, c->vote_percentage));
        SET(result, "rootConflicts", JS_NewBool(d->data.configuration.root_conflicts));
        SET(result, "associationExpirySeconds", JS_NewInt32(ctx, d->data.configuration.assoc_expiry));
        SET(result, "rootHealingDelayMs", JS_NewInt32(ctx, d->data.configuration.root_healing));
        SET(result, "encryptIE", JS_NewBool(c->encrypt_ie)); SET(result, "ieKeyLength", JS_NewInt32(ctx, c->ie_key_length));
        SET(result, "secretsIncluded", JS_NewBool(d->include_secrets));
        SET(result, "routerPassword", d->include_secrets ? JS_NewStringLen(ctx, (const char *)c->network.router.password,
            strnlen((const char *)c->network.router.password, 64)) : JS_NULL);
        SET(result, "apPassword", d->include_secrets ? JS_NewStringLen(ctx, (const char *)c->network.mesh_ap.password,
            strnlen((const char *)c->network.mesh_ap.password, 64)) : JS_NULL);
        SET(result, "ieKey", d->include_secrets && c->ie_key_length ? JS_NewStringLen(ctx, c->ie_key, c->ie_key_length) : JS_NULL);
    }
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
