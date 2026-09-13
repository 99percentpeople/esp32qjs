/* Included with the ordinary Wi-Fi configuration/runtime coordinator. */
#define WAPI_SET(key, value) do { if (!esp32_mquickjs_set_property_ref(ctx, result, key, value)) goto fail; } while (0)
static JSValue wifi_wapi_status(JSContext *ctx)
{
    esp32_mquickjs_wifi_wapi_status_t status;
    esp32_mquickjs_wifi_wapi_sdk_status(&status);
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    WAPI_SET("requestedEnabled", JS_NewBool(status.requested_enabled));
    WAPI_SET("enabled", status.busy || status.uncertain ? JS_NULL : JS_NewBool(status.enabled));
    WAPI_SET("supplicantActive", JS_NewBool(status.supplicant_active));
    WAPI_SET("policyApplied", JS_NewBool(status.supplicant_active && !status.busy && !status.uncertain &&
        status.requested_enabled == status.enabled));
    WAPI_SET("busy", JS_NewBool(status.busy));
    WAPI_SET("policyRevision", JS_NewUint32(ctx, status.revision));
    WAPI_SET("nativeGeneration", JS_NewUint32(ctx, status.generation));
    WAPI_SET("restartRequired", JS_NewBool(status.uncertain));
    WAPI_SET("runtimeCleanupPending", JS_NewBool(s_wifi_configuration_cleanup || s_wifi_state.runtime_cleanup_pending));
    WAPI_SET("error", status.error ? JS_NewInt32(ctx, status.error) : JS_NULL);
    WAPI_SET("cleanupError", status.cleanup_error ? JS_NewInt32(ctx, status.cleanup_error) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

JSValue js_wifi_wapi_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    return argc ? JS_ThrowTypeError(ctx, "wifi.wapi.status expects no arguments") : wifi_wapi_status(ctx);
}

JSValue js_wifi_wapi_capabilities(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "wifi.wapi.capabilities expects no arguments");
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    WAPI_SET("apiVersion", JS_NewString(ctx, "wifi-wapi/1"));
    WAPI_SET("stability", JS_NewString(ctx, "candidate"));
    WAPI_SET("psk", JS_TRUE);
    WAPI_SET("certificate", JS_FALSE);
    WAPI_SET("softAp", JS_FALSE);
    WAPI_SET("exactAuthSelection", JS_FALSE);
    WAPI_SET("nativeOwner", JS_NewString(ctx, "supplicant"));
    WAPI_SET("rebuildsInitializedDriver", JS_TRUE);
    WAPI_SET("maxTimeoutMs", JS_NewInt32(ctx, INT32_MAX));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
#undef WAPI_SET

static JSValue wifi_wapi_control(JSContext *ctx, int argc, JSValue *argv, bool enabled)
{
    const char *operation = enabled ? "wifi.wapi.enable" : "wifi.wapi.disable";
    if (argc > 1) return JS_ThrowTypeError(ctx, "%s expects at most one options object", operation);
    uint32_t timeout_ms;
    if (!esp32_mquickjs_wifi_capture_wapi_control(ctx, argc ? argv[0] : JS_UNDEFINED, enabled, &timeout_ms)) return JS_EXCEPTION;
    const char *stage = "wapi-admission";
    bool rebuild = false;
    esp_err_t error = wifi_driver_helpers_ready() ?
        esp32_mquickjs_wifi_radio_wapi_prepare(enabled, &rebuild) : ESP_ERR_INVALID_STATE;
    if (!error && rebuild) {
        stage = "wapi-wait-admission";
        error = esp32_mquickjs_wifi_wait_begin(timeout_ms);
        if (!error) {
            esp32_mquickjs_wifi_configuration_execution_t execution;
            error = wifi_restart_interfaces_inner(WIFI_MODE_NULL, true, &execution, &enabled, false);
            stage = execution.stage;
            esp32_mquickjs_wifi_wait_end();
        }
    }
    if (!error) return wifi_wapi_status(ctx);
    JSGCRef ref; JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = wifi_wapi_status(ctx);
    if (!JS_IsException(*details) &&
        esp32_mquickjs_set_property_ref(ctx, details, "stage", JS_NewString(ctx, stage ? stage : "wapi-control")) &&
        esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, error)))
        (void)esp32_mquickjs_throw_native_error(ctx, "WIFI_WAPI_FAILED", operation, "WAPI control failed", *details);
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

JSValue js_wifi_wapi_enable(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ (void)self; return wifi_wapi_control(ctx, argc, argv, true); }
JSValue js_wifi_wapi_disable(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ (void)self; return wifi_wapi_control(ctx, argc, argv, false); }
