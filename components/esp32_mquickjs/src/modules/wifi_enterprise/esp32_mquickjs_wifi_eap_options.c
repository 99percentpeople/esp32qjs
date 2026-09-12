#include "esp32_mquickjs_wifi_eap_options.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_wireless_core.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "esp_eap_client.h"
#include <string.h>

static const char *const s_eap_fields[] = {"anonymousIdentity", "username", "password", "newPassword",
    "caCertificate", "clientCertificate", "privateKey", "privateKeyPassword", "pac", "domain"};
_Static_assert(sizeof(s_eap_fields) / sizeof(s_eap_fields[0]) == ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT,
    "EAP capture field table must cover the native profile");

/* No pointer borrowed from JS survives another JS call. Strings are converted
 * only for immediate length/copy; ByteView read leases bracket immediate use. */
static bool eap_capture_span(JSContext *ctx, JSGCRef *source, JSGCRef *scratch,
    size_t limit, size_t *length, uint8_t *destination)
{
    if (JS_IsUndefined(source->val)) { *length = 0; return destination == NULL; }
    if (JS_IsString(ctx, source->val)) {
        JSCStringBuf buffer = {0}; size_t count = 0;
        const char *text = JS_ToCStringLen(ctx, &count, source->val, &buffer);
        bool ok = text && count <= limit && (!destination || count == *length);
        if (ok && destination && count) memcpy(destination, text, count);
        esp32_mquickjs_wireless_secure_zero(&buffer, sizeof(buffer));
        if (ok) *length = count;
        return ok;
    }
    if (JS_GetClassID(ctx, source->val) == JS_CLASS_BYTE_VIEW) {
        const uint8_t *bytes; size_t count;
        if (!esp32_mquickjs_byte_view_acquire_read(ctx, source->val, "wifi.enterprise.configure", &bytes, &count)) return false;
        bool ok = count <= limit && (!destination || count == *length);
        if (ok && destination && count) memcpy(destination, bytes, count);
        esp32_mquickjs_byte_view_release_read(ctx, source->val);
        if (ok) *length = count;
        return ok;
    }
    if (JS_GetClassID(ctx, source->val) < 0) return false;
    if (!destination) {
        scratch->val = JS_GetPropertyStr(ctx, source->val, "length");
        if (JS_IsException(scratch->val)) return false;
        uint32_t count;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, scratch->val, 0, limit, &count)) return false;
        *length = count;
        return true;
    }
    /* The preflight length is the immutable capture boundary. ArrayLike getters
     * are read once per byte; growth cannot extend the native allocation and
     * holes/shrinkage fail the byte check. No arbitrary coercion is accepted. */
    for (uint32_t i = 0; i < *length; ++i) {
        scratch->val = JS_GetPropertyUint32(ctx, source->val, i);
        if (JS_IsException(scratch->val)) return false;
        uint32_t byte;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, scratch->val, 0, 255, &byte)) return false;
        destination[i] = (uint8_t)byte;
    }
    return true;
}

bool esp32_mquickjs_wifi_eap_capture(JSContext *ctx, JSValue input,
    esp32_mquickjs_wifi_eap_profile_t **output)
{
    if (!output) { JS_ThrowInternalError(ctx, "invalid enterprise capture output"); return false; }
    *output = NULL;
    static const char *const keys[] = {"methods", "anonymousIdentity", "username", "password", "newPassword",
        "caCertificate", "clientCertificate", "privateKey", "privateKeyPassword", "pac", "domain",
        "ttlsPhase2", "checkCertificateTime", "suiteB192", "defaultCertificateBundle", "okc", "fast"};
    static const char *const methods[] = {"tls", "ttls", "peap", "fast"};
    static const uint8_t method_bits[] = {ESP_EAP_TYPE_TLS, ESP_EAP_TYPE_TTLS, ESP_EAP_TYPE_PEAP, ESP_EAP_TYPE_FAST};
    static const char *const phase2[] = {"eap", "mschapv2", "mschap", "pap", "chap"};
    static const char *const fast_keys[] = {"provisioning", "maximumPacEntries", "binaryPac"};
    static const char *const provisioning[] = {"disabled", "unauthenticated", "authenticated"};
    JSGCRef options_ref, list_ref, value_ref, fields[ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT];
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *list = JS_PushGCRef(ctx, &list_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT; ++i) JS_PushGCRef(ctx, &fields[i]);
    *options = input;
    bool ok = false;
    esp32_mquickjs_wifi_eap_profile_t *profile = NULL;
    esp32_mquickjs_wifi_eap_policy_t policy = {.ttls_phase2 = ESP_EAP_TTLS_PHASE2_MSCHAPV2};
    size_t lengths[ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT] = {0};
    const char *stage = "options";
    if (!esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.enterprise.configure", keys, sizeof(keys)/sizeof(keys[0]))) goto done;
    stage = "methods";
    *list = JS_GetPropertyStr(ctx, *options, "methods");
    if (JS_IsException(*list)) goto done;
    if (!JS_IsArray(ctx, *list)) goto invalid;
    *value = JS_GetPropertyStr(ctx, *list, "length");
    if (JS_IsException(*value)) goto done;
    uint32_t count;
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, 4, &count)) goto invalid;
    for (uint32_t i = 0; i < count; ++i) {
        *value = JS_GetPropertyUint32(ctx, *list, i);
        if (JS_IsException(*value)) goto done;
        size_t index;
        if (!esp32_mquickjs_value_to_enum(ctx, *value, methods, 4, &index) || (policy.methods & method_bits[index])) goto invalid;
        policy.methods |= method_bits[index];
    }
#define EAP_OPTION(name) do { stage = name; *value = JS_GetPropertyStr(ctx, *options, name); if (JS_IsException(*value)) goto done; } while (0)
#define EAP_BOOL(name, target) do { EAP_OPTION(name); if (!JS_IsUndefined(*value)) { if (!JS_IsBool(*value)) goto invalid; target = *value == JS_TRUE; } } while (0)
    bool check_time = true;
    EAP_BOOL("checkCertificateTime", check_time);
    policy.disable_time_check = !check_time;
    EAP_BOOL("suiteB192", policy.suiteb);
    EAP_BOOL("defaultCertificateBundle", policy.default_bundle);
    EAP_BOOL("okc", policy.okc);
    EAP_OPTION("ttlsPhase2");
    if (!JS_IsUndefined(*value)) {
        size_t index;
        if (!esp32_mquickjs_value_to_enum(ctx, *value, phase2, 5, &index)) goto invalid;
        policy.ttls_phase2 = index;
    }
    EAP_OPTION("fast");
    if (!JS_IsUndefined(*value)) {
        if (!(policy.methods & ESP_EAP_TYPE_FAST)) goto invalid;
        *list = *value;
        if (!esp32_mquickjs_validate_plain_options(ctx, *list, "enterprise FAST", fast_keys, 3)) goto done;
        *value = JS_GetPropertyStr(ctx, *list, "provisioning");
        if (JS_IsException(*value)) goto done;
        if (!JS_IsUndefined(*value)) {
            size_t index;
            if (!esp32_mquickjs_value_to_enum(ctx, *value, provisioning, 3, &index)) goto invalid;
            policy.fast_provisioning = index;
        }
        *value = JS_GetPropertyStr(ctx, *list, "maximumPacEntries");
        if (JS_IsException(*value)) goto done;
        if (!JS_IsUndefined(*value)) {
            uint32_t entries;
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, 99, &entries)) goto invalid;
            policy.fast_max_pac_list = entries;
        }
        *value = JS_GetPropertyStr(ctx, *list, "binaryPac");
        if (JS_IsException(*value)) goto done;
        if (!JS_IsUndefined(*value)) {
            if (!JS_IsBool(*value)) goto invalid;
            policy.fast_binary = *value == JS_TRUE;
        }
    }
#undef EAP_BOOL
#undef EAP_OPTION
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT; ++i) {
        fields[i].val = JS_GetPropertyStr(ctx, *options, s_eap_fields[i]);
        if (JS_IsException(fields[i].val)) goto done;
    }
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT; ++i) {
        stage = s_eap_fields[i];
        if (!eap_capture_span(ctx, &fields[i], &value_ref, esp32_mquickjs_wifi_eap_field_limit(i), &lengths[i], NULL)) goto invalid;
    }
    stage = "server-trust";
    if ((policy.methods & (ESP_EAP_TYPE_TLS | ESP_EAP_TYPE_TTLS | ESP_EAP_TYPE_PEAP)) &&
        !policy.default_bundle && !lengths[ESP32_MQUICKJS_WIFI_EAP_CA_CERT]) goto invalid;
    stage = "profile";
    esp_err_t error = esp32_mquickjs_wifi_eap_profile_begin(&policy, lengths, &profile);
    if (error == ESP_ERR_NO_MEM) { JS_ThrowOutOfMemory(ctx); goto done; }
    if (error != ESP_OK) goto invalid;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT; ++i) {
        if (!lengths[i]) continue;
        stage = s_eap_fields[i];
        uint8_t *bytes = esp32_mquickjs_wifi_eap_profile_field(profile, i);
        if (!bytes || !eap_capture_span(ctx, &fields[i], &value_ref, esp32_mquickjs_wifi_eap_field_limit(i), &lengths[i], bytes)) goto invalid;
    }
    stage = "profile-validation";
    if (esp32_mquickjs_wifi_eap_profile_seal(profile) != ESP_OK) goto invalid;
    *output = profile;
    profile = NULL;
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid or unsupported enterprise %s", stage);
done:
    esp32_mquickjs_wifi_eap_profile_release(profile);
    for (unsigned i = ESP32_MQUICKJS_WIFI_EAP_FIELD_COUNT; i > 0; --i) JS_PopGCRef(ctx, &fields[i - 1]);
    JS_PopGCRef(ctx, &value_ref); JS_PopGCRef(ctx, &list_ref); JS_PopGCRef(ctx, &options_ref);
    return ok;
}
#endif
