#pragma once
#include <stdbool.h>
#include <string.h>

/* VM-thread only. Include after esp32_mquickjs_core.h. Native callbacks retain
 * scalar observations; they must not allocate JS objects or format names. */
static inline JSValue esp32_mquickjs_native_code_to_js(JSContext *ctx,
    const char *domain, int code, const char *name)
{
    if (name && !strcmp(name, "UNKNOWN ERROR")) name = NULL;
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "domain", JS_NewString(ctx, domain)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "code", JS_NewInt32(ctx, code)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "name", name ? JS_NewString(ctx, name) : JS_NULL)) {
        JS_PopGCRef(ctx, &ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &ref);
}

static inline JSValue esp32_mquickjs_tx_completion_to_js(JSContext *ctx,
    bool observed, const char *status, const char *domain, int code, const char *name)
{
    if (!observed) return JS_NULL;
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "status", JS_NewString(ctx, status)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "native", domain ?
            esp32_mquickjs_native_code_to_js(ctx, domain, code, name) : JS_NULL)) {
        JS_PopGCRef(ctx, &ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &ref);
}

static inline JSValue esp32_mquickjs_tx_enum_completion_to_js(JSContext *ctx,
    bool observed, const char *domain, int code, int success, int failed,
    const char *success_name, const char *failed_name)
{
    return esp32_mquickjs_tx_completion_to_js(ctx, observed,
        code == success ? "success" : code == failed ? "failed" : "unknown",
        domain, code, code == success ? success_name : code == failed ? failed_name : NULL);
}
