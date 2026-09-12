/* All values remain rooted while getters/ByteSource conversion may collect.
 * Only native copies survive capture; diagnostics never receive credentials. */
static bool nan_option_bytes(JSContext *ctx, JSValue value, uint8_t *out,
    size_t capacity, uint16_t *length, bool secret)
{
    esp32_mquickjs_byte_source_t source = {0}; uint8_t *owned = NULL; JSValue error = JS_UNDEFINED;
    if (!esp32_mquickjs_get_wireless_byte_source("wifi.nan", ctx, value, "NAN bytes", &source, &owned, &error)) {
        if (!JS_HasException(ctx)) JS_Throw(ctx, error);
        return false;
    }
    bool valid = source.length <= capacity;
    if (valid) { *length = source.length; if (*length) memcpy(out, source.data, *length); }
    if (secret && owned) esp32_mquickjs_wireless_secure_zero(owned, source.length);
    esp32_mquickjs_release_byte_source(owned);
    if (!valid) JS_ThrowTypeError(ctx, "NAN byte length exceeds limit");
    return valid;
}

static bool nan_vendor_options(JSContext *ctx, JSGCRef *input, nan_vendor_ie_t *vendor, uint8_t *body)
{
    static const char *const keys[] = {"oui", "body"};
    if (!esp32_mquickjs_validate_plain_options(ctx, input->val, "NAN vendor attribute", keys, 2)) return false;
    JSGCRef ref; JSValue *field = JS_PushGCRef(ctx, &ref);
    bool ok = false; uint16_t length = 0;
    *field = JS_GetPropertyStr(ctx, input->val, "oui");
    if (JS_IsException(*field) || !nan_option_bytes(ctx, *field, vendor->vendor_oui, 3, &length, false)) goto done;
    if (length != 3) { JS_ThrowTypeError(ctx, "NAN vendor OUI requires 3 bytes"); goto done; }
    *field = JS_GetPropertyStr(ctx, input->val, "body");
    if (JS_IsException(*field) || !nan_option_bytes(ctx, *field, body, NAN_VENDOR_IE_MAX_BODY_LEN, &vendor->body_len, false)) goto done;
    vendor->body = body; ok = true;
done:
    JS_PopGCRef(ctx, &ref); return ok;
}

static bool nan_security_options(JSContext *ctx, JSGCRef *input, wifi_nan_discovery_security_params_t *security)
{
    static const char *const keys[] = {"credentials", "groupDataProtection"};
    static const char *const credential_keys[] = {"cipher", "passphrase", "pmk"};
    static const char *const ciphers[] = {"ncs-sk-128"};
    if (!esp32_mquickjs_validate_plain_options(ctx, input->val, "NAN security", keys, 2)) return false;
    JSGCRef array_ref, credential_ref, field_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref), *credential = JS_PushGCRef(ctx, &credential_ref),
        *field = JS_PushGCRef(ctx, &field_ref);
    bool ok = false; uint32_t count = 0;
    *field = JS_GetPropertyStr(ctx, input->val, "groupDataProtection");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        security->group_data_prot = *field == JS_TRUE;
    }
    *array = JS_GetPropertyStr(ctx, input->val, "credentials");
    if (JS_IsException(*array)) goto done;
    if (!JS_IsArray(ctx, *array)) goto invalid;
    *field = JS_GetPropertyStr(ctx, *array, "length");
    if (JS_IsException(*field)) goto done;
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, ESP_WIFI_NAN_MAX_CREDS_PER_SVC, &count)) goto invalid;
    for (uint32_t i = 0; i < count; ++i) {
        *credential = JS_GetPropertyUint32(ctx, *array, i);
        if (JS_IsException(*credential)) goto done;
        if (!esp32_mquickjs_validate_plain_options(ctx, *credential, "NAN credential", credential_keys, 3)) goto done;
        wifi_nan_credential_t *native = &security->creds[i];
        *field = JS_GetPropertyStr(ctx, *credential, "cipher");
        if (JS_IsException(*field)) goto done;
        size_t cipher = 0;
        if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_enum(ctx, *field, ciphers, 1, &cipher)) goto invalid;
        native->csid = WIFI_NAN_CSID_NCS_SK_128;
        *field = JS_GetPropertyStr(ctx, *credential, "passphrase");
        if (JS_IsException(*field)) goto done;
        bool passphrase = !JS_IsUndefined(*field);
        if (passphrase && (!nan_service_string(ctx, *field, native->passphrase, sizeof(native->passphrase)) ||
            strlen(native->passphrase) < 8)) goto invalid;
        *field = JS_GetPropertyStr(ctx, *credential, "pmk");
        if (JS_IsException(*field)) goto done;
        if (passphrase == !JS_IsUndefined(*field)) goto invalid;
        if (!passphrase) {
            uint16_t length = 0;
            if (!nan_option_bytes(ctx, *field, native->pmk, sizeof(native->pmk), &length, true)) goto done;
            if (length != sizeof(native->pmk)) goto invalid;
            native->use_pmk = true;
        }
    }
    security->num_credentials = count; ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid NAN security options");
done:
    if (!ok) esp32_mquickjs_wireless_secure_zero(security, sizeof(*security));
    JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &credential_ref); JS_PopGCRef(ctx, &array_ref);
    return ok;
}

/* USD-only fields are captured once before admission; synchronized sessions
 * reject them instead of silently ignoring their channel/lifetime policy. */
static bool nan_service_usd_options(JSContext *ctx, JSGCRef *input, bool publish, bool usd,
    esp32_mquickjs_wifi_nan_service_config_t *config)
{
    wifi_nan_usd_config_t *native = publish ? &config->publish.usd_publish_config : &config->subscribe.usd_subscribe_config;
    JSGCRef field_ref, item_ref;
    JSValue *field = JS_PushGCRef(ctx, &field_ref), *item = JS_PushGCRef(ctx, &item_ref);
    bool ok = false; uint32_t value, ttl = 60;
    native->usd_default_channel = 6;
    native->n_min = native->m_min = 5; native->n_max = native->m_max = 10;
    *field = JS_GetPropertyStr(ctx, input->val, "ttlSeconds");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field) && (!usd || !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 0, INT32_MAX, &ttl))) goto invalid;
    *field = JS_GetPropertyStr(ctx, input->val, "channel");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        if (!usd || !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 177, &value)) goto invalid;
        if (value > 14 && !esp32_mquickjs_wifi_radio_5ghz_channel_bit(value)) goto invalid;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
        if (value > 14) goto invalid;
#endif
        native->usd_default_channel = value;
    }
    *field = JS_GetPropertyStr(ctx, input->val, "channels");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        if (!usd || !JS_IsArray(ctx, *field)) goto invalid;
        *item = JS_GetPropertyStr(ctx, *field, "length");
        if (JS_IsException(*item)) goto done;
        uint32_t length;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *item, 1, 42, &length)) goto invalid;
        for (uint32_t i = 0; i < length; ++i) {
            *item = JS_GetPropertyUint32(ctx, *field, i);
            if (JS_IsException(*item)) goto done;
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *item, 1, 177, &value)) goto invalid;
            uint32_t bit = value <= 14 ? 1U << value : esp32_mquickjs_wifi_radio_5ghz_channel_bit(value);
            if (!bit) goto invalid;
#if !CONFIG_SOC_WIFI_SUPPORT_5G
            if (value > 14) goto invalid;
#endif
            if (value <= 14) {
                if (native->usd_chan_bitmap.ghz_2_channels & bit) goto invalid;
                native->usd_chan_bitmap.ghz_2_channels |= bit;
            } else {
                if (native->usd_chan_bitmap.ghz_5_channels & bit) goto invalid;
                native->usd_chan_bitmap.ghz_5_channels |= bit;
            }
        }
    }
    *field = JS_GetPropertyStr(ctx, input->val, "dwell");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        static const char *const keys[] = {"nMin", "nMax", "mMin", "mMax"};
        if (!usd || !publish) goto invalid;
        if (!esp32_mquickjs_validate_plain_options(ctx, *field, "USD publish dwell", keys, 4)) goto done;
        uint8_t *destinations[] = {&native->n_min, &native->n_max, &native->m_min, &native->m_max};
        for (unsigned i = 0; i < 4; ++i) {
            *item = JS_GetPropertyStr(ctx, *field, keys[i]);
            if (JS_IsException(*item)) goto done;
            if (JS_IsUndefined(*item)) continue;
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *item, 1, 255, &value)) goto invalid;
            *destinations[i] = value;
        }
        if (native->n_min > native->n_max || native->m_min > native->m_max) goto invalid;
    }
    if (publish) { config->publish.usd_discovery_flag = usd; config->publish.ttl = usd ? ttl : 0; }
    else { config->subscribe.usd_discovery_flag = usd; config->subscribe.ttl = usd ? ttl : 0; }
    ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid NAN USD discovery options");
done:
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &field_ref); return ok;
}
