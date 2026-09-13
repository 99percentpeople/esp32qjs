/* cutils defines branch hints; IDF guards its own definitions when included later. */
#include "esp32_mquickjs_memory.h"
#include "cutils.h"
#include "esp32_mquickjs_wifi_action_radio.h"
#include "esp32_mquickjs_wifi_raw_tx_ap.h"
#include "esp32_mquickjs_wifi.h"
#include "esp32_mquickjs_wifi_wait.h"
#include "esp32_mquickjs_wifi_wps_ap_radio.h"
#include "esp32_mquickjs_wifi_eap_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_net.h"
#include "esp32_mquickjs_wifi_netif.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp_wifi_default.h"
#include "esp_mac.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* No callback or JS pointer is retained here. startAP uses exclusive startup;
 * configure can share with Station. Default netif handlers own DHCP/IP readiness. */
static esp_netif_t *s_ap_netif;
static esp32_mquickjs_wifi_radio_lease_t s_ap_lease;
static esp32_mquickjs_wifi_radio_lifecycle_t s_ap_lifecycle;
/* Borrowed identity only: the Station/configuration coordinator owns this
 * reservation. Keep it across failed native retirement and runtime teardown. */
static esp32_mquickjs_wifi_radio_lifecycle_t s_ap_coordinator;
/* Identity-only observer of a Session-owned lease; no release authority. */
static struct { uint32_t generation, identity; } s_ap_raw_owner;
static bool s_ap_cleanup_pending;
static bool s_ap_partial_stop;
static const char *s_ap_stage;
static esp_err_t s_ap_detach_error;

esp_err_t esp32_mquickjs_wifi_ap_netif_cleanup_error(void) { return s_ap_detach_error; }

const esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_control_lease(void)
{
    return s_ap_lease.acquired && !s_ap_cleanup_pending ? &s_ap_lease : NULL;
}

static esp_err_t wifi_ap_prepare_netif(void)
{
    if (s_ap_netif != NULL || s_ap_detach_error != ESP_OK) return ESP_ERR_INVALID_STATE;
    s_ap_stage = "net-init";
    esp_err_t err = esp32_mquickjs_net_ensure_initialized();
    if (err != ESP_OK) return err;
    s_ap_stage = "netif-create";
    esp_netif_config_t config = ESP_NETIF_DEFAULT_WIFI_AP();
    s_ap_netif = esp_netif_new(&config);
    if (s_ap_netif == NULL) return ESP_ERR_NO_MEM;
    s_ap_stage = "netif-attach";
    err = esp_netif_attach_wifi_ap(s_ap_netif);
    if (err != ESP_OK) return err;
    s_ap_stage = "netif-handlers";
    return esp_wifi_set_default_wifi_ap_handlers();
}

static esp_err_t wifi_ap_retire_netif(void)
{
    if (s_ap_detach_error != ESP_OK) return s_ap_detach_error;
    if (s_ap_netif == NULL) return ESP_OK;
    s_ap_stage = "netif-detach";
    return esp32_mquickjs_wifi_netif_retire(&s_ap_netif, &s_ap_detach_error);
}

esp_err_t esp32_mquickjs_wifi_ap_open_raw_tx_rate(
    esp32_mquickjs_wifi_raw_tx_ap_context_t *context, const wifi_tx_rate_config_t *rate,
    uint8_t channel, esp32_mquickjs_wifi_radio_lease_t *lease, uint8_t *actual_channel,
    const char **stage)
{
    if (stage == NULL) return ESP_ERR_INVALID_ARG;
    *stage = "ap-rate-admission";
    if (context == NULL || context->owned || context->lifecycle.identity != 0U ||
        context->lifecycle.generation != 0U || context->mode != 0U || context->station_cleanup_pending ||
        lease == NULL || lease->acquired ||
        lease->identity != 0U || lease->generation != 0U || actual_channel == NULL ||
        !esp32_mquickjs_wifi_tx_rate_valid(rate)) return ESP_ERR_INVALID_ARG;
    if (!esp32_mquickjs_wifi_raw_tx_ap_ready() || s_ap_netif != NULL || s_ap_lease.acquired ||
        s_ap_raw_owner.identity != 0U || s_ap_lifecycle.identity != 0U || s_ap_coordinator.identity != 0U ||
        s_ap_cleanup_pending || s_ap_detach_error != ESP_OK) return ESP_ERR_INVALID_STATE;
    *stage = "ap-rate-config-allocate";
    wifi_config_t *saved = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*saved), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (saved == NULL) return ESP_ERR_NO_MEM;
    *stage = "ap-rate-admission";
    wifi_mode_t mode;
    esp_err_t err = esp32_mquickjs_wifi_radio_begin_raw_tx_ap_rate(rate, &context->lifecycle, &mode);
    if (err != ESP_OK) goto done;
    context->owned = true;
    context->mode = (uint8_t)mode;
    s_ap_coordinator = context->lifecycle;
    s_ap_cleanup_pending = true;
    *stage = "ap-rate-config";
    err = esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration(&context->lifecycle, saved);
    if (err != ESP_OK) goto done;
    /* No implicit AP channel rewrite or post-start CSA for a fixed request. */
    if (channel != 0U && channel != saved->ap.channel) { err = ESP_ERR_INVALID_ARG; goto done; }
    *stage = "ap-rate-station-retire";
    context->station_cleanup_pending = true;
    err = esp32_mquickjs_wifi_retire_for_configuration(&context->lifecycle);
    if (err != ESP_OK) goto done;
    context->station_cleanup_pending = false;
    if (mode & WIFI_MODE_STA) {
        *stage = "ap-rate-station-prepare";
        context->station_cleanup_pending = true;
        err = esp32_mquickjs_wifi_prepare_for_configuration(&context->lifecycle);
        if (err != ESP_OK) goto done;
    }
    *stage = "ap-rate-helper-prepare";
    err = esp32_mquickjs_wifi_radio_check_stopped_lifecycle(&context->lifecycle, false);
    if (err == ESP_OK) err = wifi_ap_prepare_netif();
    if (err != ESP_OK) goto done;
    *stage = "ap-rate-start";
    err = esp32_mquickjs_wifi_radio_start_raw_tx_ap_rate(&context->lifecycle, mode, saved, rate, lease, actual_channel);
    s_ap_coordinator = context->lifecycle;
    if (lease->acquired) {
        s_ap_raw_owner.generation = lease->generation;
        s_ap_raw_owner.identity = lease->identity;
    }
    if (err == ESP_OK) { s_ap_cleanup_pending = false; s_ap_stage = NULL; *stage = NULL; }
done:
    if (context->owned && err != ESP_OK) s_ap_stage = *stage;
    esp32_mquickjs_wireless_secure_zero(saved, sizeof(*saved));
    esp32_mquickjs_memory_payload_free(saved);
    return err;
}

esp_err_t esp32_mquickjs_wifi_ap_close_raw_tx_rate(
    esp32_mquickjs_wifi_raw_tx_ap_context_t *context, esp32_mquickjs_wifi_radio_lease_t *lease,
    bool physically_terminated, const char **stage)
{
    if (context == NULL || lease == NULL || stage == NULL) return ESP_ERR_INVALID_ARG;
    *stage = "ap-rate-cleanup-admission";
    if (!context->owned) return ESP_OK;
    if (physically_terminated) {
        /* The admitted central recovery already retired this AP helper before
         * deinit. Never touch a new helper it may have subsequently rebuilt. */
        if (lease->acquired || context->lifecycle.identity != 0U || s_ap_raw_owner.identity != 0U)
            return ESP_ERR_INVALID_STATE;
        memset(context, 0, sizeof(*context));
        *stage = NULL;
        return ESP_OK;
    }
    if (context->lifecycle.identity != 0U) {
        if (s_ap_coordinator.identity != context->lifecycle.identity ||
            s_ap_coordinator.generation != context->lifecycle.generation) return ESP_ERR_INVALID_STATE;
    } else if (!lease->acquired || s_ap_raw_owner.identity != lease->identity ||
               s_ap_raw_owner.generation != lease->generation || s_ap_coordinator.identity != 0U)
        return ESP_ERR_INVALID_STATE;
    s_ap_cleanup_pending = true;
    *stage = s_ap_stage = "ap-rate-stop-restore";
    esp_err_t err = esp32_mquickjs_wifi_radio_quiesce_raw_tx_ap_rate(lease, &context->lifecycle);
    if (context->lifecycle.identity != 0U) s_ap_coordinator = context->lifecycle;
    if (err != ESP_OK) return err;
    *stage = s_ap_stage = "ap-rate-helper-retire";
    err = esp32_mquickjs_wifi_radio_check_stopped_lifecycle(&context->lifecycle, true);
    if (err == ESP_OK) err = wifi_ap_retire_netif();
    if (err != ESP_OK) return err;
    if (context->station_cleanup_pending) {
        *stage = s_ap_stage = "ap-rate-station-retire";
        err = esp32_mquickjs_wifi_retire_for_configuration(&context->lifecycle);
        if (err != ESP_OK) return err;
        context->station_cleanup_pending = false;
    }
    *stage = s_ap_stage = "ap-rate-finish";
    err = esp32_mquickjs_wifi_radio_finish_lifecycle(&context->lifecycle, false);
    if (err != ESP_OK) return err;
    memset(&s_ap_raw_owner, 0, sizeof(s_ap_raw_owner));
    memset(&s_ap_coordinator, 0, sizeof(s_ap_coordinator));
    memset(context, 0, sizeof(*context));
    s_ap_cleanup_pending = false;
    s_ap_stage = *stage = NULL;
    return ESP_OK;
}

/* Runtime task only: the borrowed token excludes driver mutation through
 * allocation/handler setup and native AP_START. No JS is retained or polled. */
esp_err_t esp32_mquickjs_wifi_ap_reopen(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station, wifi_config_t *config,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (s_ap_netif != NULL || s_ap_lease.acquired || s_ap_cleanup_pending ||
        s_ap_lifecycle.identity != 0U || s_ap_coordinator.identity != 0U || s_ap_detach_error != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    s_ap_stage = "ap-reopen-admission";
    esp_err_t err = esp32_mquickjs_wifi_radio_begin_ap_reopen(application, station, config, &s_ap_lease, token);
    if (err != ESP_OK) return err;
    s_ap_coordinator = *token;
    s_ap_cleanup_pending = true;
    err = esp32_mquickjs_wifi_radio_check_ap_reopen(token);
    if (err == ESP_OK) err = wifi_ap_prepare_netif();
    if (err == ESP_OK) {
        s_ap_stage = "ap-reopen-start";
        err = esp32_mquickjs_wifi_radio_finish_ap_reopen(token, &config->ap.channel);
    }
    if (err == ESP_OK) {
        memset(&s_ap_coordinator, 0, sizeof(s_ap_coordinator));
        s_ap_cleanup_pending = false;
        s_ap_stage = NULL;
        return ESP_OK;
    }
    /* Keep netif and lease on failure. stopAP can retire a never-started AP
     * immediately; an attempted native start must pass AP_STOP first. */
    esp_err_t abort_error = esp32_mquickjs_wifi_radio_abort_ap_reopen(token);
    if (abort_error != ESP_OK) return abort_error;
    s_ap_partial_stop = true;
    return err;
}

static bool wifi_ap_partial_token(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    return token != NULL && token->identity != 0U && s_ap_partial_stop &&
        s_ap_coordinator.identity == token->identity && s_ap_coordinator.generation == token->generation;
}

esp_err_t esp32_mquickjs_wifi_ap_begin_partial_stop(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (s_ap_netif == NULL || !s_ap_lease.acquired || s_ap_cleanup_pending ||
        s_ap_lifecycle.identity != 0U || s_ap_coordinator.identity != 0U || s_ap_detach_error != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    s_ap_stage = "admission";
    esp_err_t err = esp32_mquickjs_wifi_radio_begin_ap_stop(application, station, &s_ap_lease, token);
    if (err != ESP_OK) return err;
    s_ap_coordinator = *token;
    s_ap_cleanup_pending = true;
    s_ap_partial_stop = true;
    s_ap_stage = "ap-stop";
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_ap_retire_partial_stop(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (!wifi_ap_partial_token(token)) return ESP_ERR_INVALID_STATE;
    s_ap_stage = "ap-stop";
    esp_err_t err = esp32_mquickjs_wifi_radio_check_ap_stopped_lifecycle(token);
    if (err != ESP_OK) return err;
    return wifi_ap_retire_netif();
}

esp_err_t esp32_mquickjs_wifi_ap_finish_partial_stop(esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (!wifi_ap_partial_token(token) || s_ap_netif != NULL || s_ap_detach_error != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    s_ap_stage = "ap-stop-finish";
    esp_err_t err = esp32_mquickjs_wifi_radio_finish_ap_stop(token, &s_ap_lease);
    if (err != ESP_OK) return err;
    memset(&s_ap_coordinator, 0, sizeof(s_ap_coordinator));
    s_ap_partial_stop = false;
    s_ap_cleanup_pending = false;
    s_ap_stage = NULL;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_ap_adopt_partial_stop(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (!wifi_ap_partial_token(token)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp32_mquickjs_wifi_radio_adopt_ap_stop(token);
    if (err != ESP_OK) return err;
    esp32_mquickjs_wifi_radio_release(&s_ap_lease);
    s_ap_partial_stop = false;
    s_ap_stage = "configuration-handoff";
    return ESP_OK;
}

static esp_err_t wifi_ap_cleanup(void)
{
    bool deauth_pending;
    (void)esp32_mquickjs_wifi_radio_ap_deauth_poll(&deauth_pending);
    if (deauth_pending) return ESP_ERR_INVALID_STATE;
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (esp32_mquickjs_wifi_wps_ap_helper_held()) return ESP_ERR_INVALID_STATE;
#endif
    esp_err_t err;
    if (esp32_mquickjs_wifi_configuration_pending())
        return esp32_mquickjs_wifi_cleanup_ap_configuration();
    if (esp32_mquickjs_wifi_ap_stop_pending() ||
        (s_ap_lease.acquired && s_ap_lifecycle.identity == 0U && s_ap_coordinator.identity == 0U)) {
        bool handled = false;
        err = esp32_mquickjs_wifi_stop_ap_shared(&handled);
        if (handled) return err;
    }
    if (s_ap_coordinator.identity != 0U || s_ap_raw_owner.identity != 0U) return ESP_ERR_INVALID_STATE;
    s_ap_cleanup_pending = true;
    if (s_ap_detach_error != ESP_OK) return s_ap_detach_error;
    if (s_ap_lease.acquired && s_ap_lifecycle.identity == 0U) {
        s_ap_stage = "admission";
        err = esp32_mquickjs_wifi_radio_begin_lifecycle(NULL, NULL, &s_ap_lease, &s_ap_lifecycle);
        if (err != ESP_OK) return err;
    }
    if (s_ap_lease.acquired && s_ap_lifecycle.identity != 0U)
        esp32_mquickjs_wifi_radio_release(&s_ap_lease);
    if (s_ap_lifecycle.identity != 0U) {
        s_ap_stage = "stop";
        err = esp32_mquickjs_wifi_radio_quiesce_lifecycle(&s_ap_lifecycle);
        if (err != ESP_OK) return err;
    }
    err = wifi_ap_retire_netif();
    if (err != ESP_OK) return err;
    if (s_ap_lifecycle.identity != 0U) {
        s_ap_stage = "shutdown";
        err = esp32_mquickjs_wifi_radio_finish_lifecycle(&s_ap_lifecycle, true);
        if (err != ESP_OK) return err;
    }
    s_ap_cleanup_pending = false;
    s_ap_stage = NULL;
    return ESP_OK;
}

bool esp32_mquickjs_wifi_parse_ap_config_for_operation(JSContext *ctx, JSValue options,
    wifi_config_t *config, const char *operation)
{
    static const char *const keys[] = {"ssid", "password", "channel", "hidden", "authMode", "maxConnections",
        "beaconIntervalMs", "dtimPeriod", "csaCount", "pairwiseCipher", "pmf", "ftmResponder", "saePwe",
        "gtkRekeyIntervalSeconds", "transitionDisable", "saeExt", "wpa3CompatibleMode",
        "bssMaxIdlePeriod", "bssMaxIdleProtectedKeepAlive"};
    static const char *const auth_modes[] = {"open", "wpa", "wpa2", "wpa/wpa2", "wpa3", "wpa2/wpa3", "owe"};
    static const wifi_auth_mode_t auth_values[] = {WIFI_AUTH_OPEN, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK,
        WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK, WIFI_AUTH_OWE};
    static const char *const cipher_names[] = {"tkip", "ccmp", "tkip/ccmp", "gcmp", "gcmp-256"};
    static const wifi_cipher_type_t cipher_values[] = {WIFI_CIPHER_TYPE_TKIP, WIFI_CIPHER_TYPE_CCMP,
        WIFI_CIPHER_TYPE_TKIP_CCMP, WIFI_CIPHER_TYPE_GCMP, WIFI_CIPHER_TYPE_GCMP256};
    static const char *const pmf_names[] = {"optional", "required", "disabled"};
    static const char *const pwe_names[] = {"hunting-and-pecking", "hash-to-element", "both"};
    static const wifi_sae_pwe_method_t pwe_values[] = {WPA3_SAE_PWE_HUNT_AND_PECK, WPA3_SAE_PWE_HASH_TO_ELEMENT, WPA3_SAE_PWE_BOTH};
    JSGCRef options_ref, property_ref;
    JSValue *root = JS_PushGCRef(ctx, &options_ref);
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    JSCStringBuf buffer;
    bool ok = false, auth_explicit = false;
    size_t length, auth;
    uint32_t number;
    const char *text;
    *root = options;
    memset(config, 0, sizeof(*config));
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, "wifi.startAP", keys, sizeof(keys)/sizeof(*keys))) goto done;
    *property = JS_GetPropertyStr(ctx, *root, "ssid");
    if (!JS_IsString(ctx, *property) ||
        (text = JS_ToCStringLen(ctx, &length, *property, &buffer)) == NULL ||
        length == 0 || length > 32 || memchr(text, 0, length) != NULL) goto invalid;
    memcpy(config->ap.ssid, text, length);
    config->ap.ssid_len = length;
    *property = JS_GetPropertyStr(ctx, *root, "password");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!JS_IsString(ctx, *property) ||
            (text = JS_ToCStringLen(ctx, &length, *property, &buffer)) == NULL ||
            length < 1 || length > 63 || memchr(text, 0, length) != NULL) goto invalid;
        memcpy(config->ap.password, text, length);
    }
    config->ap.authmode = config->ap.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    *property = JS_GetPropertyStr(ctx, *root, "authMode");
    if (JS_IsException(*property)) goto done;
    auth_explicit = !JS_IsUndefined(*property);
    if (!JS_IsUndefined(*property)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *property, auth_modes, sizeof(auth_modes) / sizeof(auth_modes[0]), &auth)) goto invalid;
        config->ap.authmode = auth_values[auth];
    }
    *property = JS_GetPropertyStr(ctx, *root, "saeExt");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!JS_IsBool(*property)) goto invalid;
        config->ap.sae_ext = *property == JS_TRUE;
    }
    if (config->ap.sae_ext && !auth_explicit) config->ap.authmode = WIFI_AUTH_WPA3_PSK;
    *property = JS_GetPropertyStr(ctx, *root, "wpa3CompatibleMode");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!JS_IsBool(*property)) goto invalid;
        config->ap.wpa3_compatible_mode = *property == JS_TRUE;
    }
    config->ap.channel = 1;
    *property = JS_GetPropertyStr(ctx, *root, "channel");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *property, 1, 11, &number)) goto invalid;
        config->ap.channel = number;
    }
    *property = JS_GetPropertyStr(ctx, *root, "hidden");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!JS_IsBool(*property)) goto invalid;
        config->ap.ssid_hidden = *property == JS_TRUE;
    }
    config->ap.max_connection = ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS;
    *property = JS_GetPropertyStr(ctx, *root, "maxConnections");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *property, 1, ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS, &number)) goto invalid;
        config->ap.max_connection = number;
    }
    config->ap.beacon_interval = ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU; /* SDK TU, not milliseconds. */
    config->ap.dtim_period = 1;
    config->ap.csa_count = 3;
    *property = JS_GetPropertyStr(ctx, *root, "beaconIntervalMs");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        double ms;
        if (!JS_IsNumber(ctx, *property) || JS_ToNumber(ctx, &ms, *property) != 0 || !isfinite(ms) || ms <= 0 || ms > ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU * 1.024) goto invalid;
        /* Round upward to the next SDK-supported 100 TU quantum. */
        config->ap.beacon_interval = (uint16_t)(ceil(ms / (ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU * 1.024)) * ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU);
    }
    *property = JS_GetPropertyStr(ctx, *root, "dtimPeriod");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *property, 1, ESP32_MQUICKJS_WIFI_AP_DTIM_MAX, &number)) goto invalid;
        config->ap.dtim_period = number;
    }
    *property = JS_GetPropertyStr(ctx, *root, "csaCount");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *property, 1, 255, &number)) goto invalid;
        config->ap.csa_count = number;
    }
    config->ap.pairwise_cipher = config->ap.authmode == WIFI_AUTH_WPA_PSK || config->ap.authmode == WIFI_AUTH_WPA_WPA2_PSK
        ? WIFI_CIPHER_TYPE_TKIP_CCMP : WIFI_CIPHER_TYPE_CCMP;
    if (config->ap.sae_ext) config->ap.pairwise_cipher = WIFI_CIPHER_TYPE_GCMP256;
    *property = JS_GetPropertyStr(ctx, *root, "pairwiseCipher");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (config->ap.authmode == WIFI_AUTH_OPEN || !esp32_mquickjs_value_to_enum(ctx, *property, cipher_names, 5, &auth)) goto invalid;
        config->ap.pairwise_cipher = cipher_values[auth];
    }
    config->ap.pmf_cfg.capable = true;
    config->ap.pmf_cfg.required = config->ap.authmode == WIFI_AUTH_WPA3_PSK || config->ap.authmode == WIFI_AUTH_OWE;
    *property = JS_GetPropertyStr(ctx, *root, "pmf");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if ((config->ap.authmode == WIFI_AUTH_OPEN || config->ap.authmode == WIFI_AUTH_WPA_PSK) || !esp32_mquickjs_value_to_enum(ctx, *property, pmf_names, 3, &auth)) goto invalid;
        config->ap.pmf_cfg.capable = auth != 2;
        config->ap.pmf_cfg.required = auth == 1;
    }
    *property = JS_GetPropertyStr(ctx, *root, "saePwe");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *property, pwe_names, 3, &auth)) goto invalid;
        config->ap.sae_pwe_h2e = pwe_values[auth];
    }
    if (config->ap.sae_ext && config->ap.sae_pwe_h2e == WPA3_SAE_PWE_UNSPECIFIED)
        config->ap.sae_pwe_h2e = WPA3_SAE_PWE_HASH_TO_ELEMENT;
    *property = JS_GetPropertyStr(ctx, *root, "ftmResponder");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!JS_IsBool(*property)) goto invalid;
        config->ap.ftm_responder = *property == JS_TRUE;
    }
    *property = JS_GetPropertyStr(ctx, *root, "transitionDisable");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!JS_IsBool(*property)) goto invalid;
        config->ap.transition_disable = *property == JS_TRUE;
    }
    *property = JS_GetPropertyStr(ctx, *root, "gtkRekeyIntervalSeconds");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *property, 0, UINT16_MAX, &number)) goto invalid;
        config->ap.gtk_rekey_interval = number;
    }
    *property = JS_GetPropertyStr(ctx, *root, "bssMaxIdlePeriod");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *property, 0, UINT16_MAX, &number)) goto invalid;
        config->ap.bss_max_idle_cfg.period = number;
    }
    *property = JS_GetPropertyStr(ctx, *root, "bssMaxIdleProtectedKeepAlive");
    if (JS_IsException(*property)) goto done;
    if (!JS_IsUndefined(*property)) {
        if (!JS_IsBool(*property)) goto invalid;
        config->ap.bss_max_idle_cfg.protected_keep_alive = *property == JS_TRUE;
    }
    esp_err_t validation = esp32_mquickjs_wifi_radio_validate_ap_config(config);
    if (validation == ESP_ERR_NOT_SUPPORTED) {
        if (strcmp(operation, "wifi.configure") == 0)
            (void)esp32_mquickjs_wifi_throw_configuration_error(ctx, validation, NULL, NULL);
        else
            (void)esp32_mquickjs_wifi_throw_operation_error(ctx, "WIFI_AP_UNSUPPORTED", operation, validation, -1, UINT32_MAX);
        goto done;
    }
    if (validation != ESP_OK) goto invalid;
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid or conflicting wifi.startAP options");
done:
    esp32_mquickjs_wireless_secure_zero(&buffer, sizeof(buffer));
    if (!ok) esp32_mquickjs_wireless_secure_zero(config, sizeof(*config));
    JS_PopGCRef(ctx, &property_ref);
    JS_PopGCRef(ctx, &options_ref);
    return ok;
}

bool esp32_mquickjs_wifi_parse_ap_config(JSContext *ctx, JSValue options,
    wifi_config_t *config, bool *allow_disconnect)
{
    return esp32_mquickjs_wifi_parse_start_ap_config(ctx, options, config, allow_disconnect);
}

static JSValue wifi_ap_error(JSContext *ctx, const char *operation, esp_err_t err)
{
    esp32_mquickjs_wifi_radio_status_t radio = {0};
    (void)esp32_mquickjs_wifi_radio_get_status(&radio);
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stage", s_ap_stage ? JS_NewString(ctx, s_ap_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, err)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "restartRequired", JS_NewBool(s_ap_detach_error != ESP_OK || radio.restart_required))) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    JSValue result = esp32_mquickjs_throw_native_error(ctx, "WIFI_AP_FAILED", operation, "Wi-Fi AP operation failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

static const char *wifi_ap_auth_name(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
    case WIFI_AUTH_OWE: return "owe";
    default: return "unknown";
    }
}

/* JS_NewStringLen requires valid WTF-8. Binary SDK SSIDs need validation;
 * retain exact bytes separately instead of passing malformed text to the VM. */
JSValue esp32_mquickjs_wifi_ap_status(JSContext *ctx)
{
    /* Runtime-task local resources cannot retire while this synchronous query
     * constructs JS. Pending cleanup remains observable without SDK reads. */
    if (s_ap_netif == NULL && !s_ap_lease.acquired && !s_ap_cleanup_pending) return JS_NULL;
    esp32_mquickjs_wifi_ap_snapshot_t snapshot;
    const char *query_stage = NULL;
    const esp32_mquickjs_wifi_radio_lease_t raw_observer = {
        .generation = s_ap_raw_owner.generation, .identity = s_ap_raw_owner.identity,
        .client = ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX, .acquired = s_ap_raw_owner.identity != 0U};
    esp_err_t err = esp32_mquickjs_wifi_radio_sample_ap(
        s_ap_cleanup_pending ? NULL : s_ap_raw_owner.identity != 0U ? &raw_observer : &s_ap_lease,
        &snapshot, &query_stage);
    bool available = err == ESP_OK;
    char mac[18];
    if (available) snprintf(mac, sizeof(mac), MACSTR, MAC2STR(snapshot.mac));
    JSGCRef result_ref, bytes_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *bytes = JS_PushGCRef(ctx, &bytes_ref);
    *result = JS_NewObject(ctx);
    *bytes = JS_NULL;
    if (JS_IsException(*result)) goto fail;
    if (available) {
        *bytes = JS_NewArray(ctx, 0);
        if (JS_IsException(*bytes)) goto fail;
        for (uint32_t i = 0; i < snapshot.ssid_len; ++i) {
            if (JS_IsException(JS_SetPropertyUint32(ctx, *bytes, i,
                    JS_NewUint32(ctx, snapshot.ssid[i])))) goto fail;
        }
    }
    if (!esp32_mquickjs_set_property_ref(ctx, result, "started", JS_NewBool(snapshot.started)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "cleanupPending", JS_NewBool(s_ap_cleanup_pending)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "cleanupStage",
            s_ap_cleanup_pending && s_ap_stage != NULL ? JS_NewString(ctx, s_ap_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "queryError", available ? JS_NULL : JS_NewInt32(ctx, err)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "queryStage",
            query_stage != NULL ? JS_NewString(ctx, query_stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ssid",
            available ? esp32_mquickjs_wifi_ssid_text(ctx, snapshot.ssid, snapshot.ssid_len) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ssidBytes", *bytes) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "hidden", available ? JS_NewBool(snapshot.hidden) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "channel", available ? JS_NewUint32(ctx, snapshot.channel) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "authMode", available ? JS_NewString(ctx, wifi_ap_auth_name(snapshot.authmode)) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "maxConnections", available ? JS_NewUint32(ctx, snapshot.max_connections) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "clientCount", available ? JS_NewUint32(ctx, snapshot.client_count) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "mac", available ? JS_NewString(ctx, mac) : JS_NULL)) goto fail;
    JS_PopGCRef(ctx, &bytes_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &bytes_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static const char *wifi_ap_cipher_name(wifi_cipher_type_t cipher)
{
    switch (cipher) {
    case WIFI_CIPHER_TYPE_TKIP: return "tkip";
    case WIFI_CIPHER_TYPE_CCMP: return "ccmp";
    case WIFI_CIPHER_TYPE_TKIP_CCMP: return "tkip/ccmp";
    case WIFI_CIPHER_TYPE_GCMP: return "gcmp";
    case WIFI_CIPHER_TYPE_GCMP256: return "gcmp-256";
    default: return "unknown";
    }
}

static const char *wifi_ap_pwe_name(wifi_sae_pwe_method_t pwe)
{
    switch (pwe) {
    case WPA3_SAE_PWE_HUNT_AND_PECK: return "hunting-and-pecking";
    case WPA3_SAE_PWE_HASH_TO_ELEMENT: return "hash-to-element";
    case WPA3_SAE_PWE_BOTH: return "both";
    default: return NULL;
    }
}

static JSValue wifi_ap_result(JSContext *ctx, const wifi_config_t *config)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    bool sae = config->ap.authmode == WIFI_AUTH_WPA3_PSK || config->ap.authmode == WIFI_AUTH_WPA2_WPA3_PSK ||
        config->ap.wpa3_compatible_mode;
    const char *pwe = sae ? wifi_ap_pwe_name(config->ap.sae_pwe_h2e) : NULL;
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "started", JS_NewBool(true)) ||
        !esp32_mquickjs_wifi_set_ssid_properties(ctx, result, config->ap.ssid, config->ap.ssid_len) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "channel", JS_NewUint32(ctx, config->ap.channel)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "hidden", JS_NewBool(config->ap.ssid_hidden)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "authMode", JS_NewString(ctx, wifi_ap_auth_name(config->ap.authmode))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "maxConnections", JS_NewUint32(ctx, config->ap.max_connection)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "beaconIntervalMs", JS_NewFloat64(ctx, config->ap.beacon_interval * 1.024)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "dtimPeriod", JS_NewUint32(ctx, config->ap.dtim_period)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "csaCount", JS_NewUint32(ctx, config->ap.csa_count)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "pairwiseCipher", config->ap.authmode == WIFI_AUTH_OPEN ? JS_NULL : JS_NewString(ctx, wifi_ap_cipher_name(config->ap.pairwise_cipher))) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "pmf", config->ap.authmode == WIFI_AUTH_OPEN || config->ap.authmode == WIFI_AUTH_WPA_PSK ? JS_NULL : JS_NewString(ctx, config->ap.pmf_cfg.required ? "required" : config->ap.pmf_cfg.capable ? "optional" : "disabled")) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "saePwe", pwe == NULL ? JS_NULL : JS_NewString(ctx, pwe)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "ftmResponder", JS_NewBool(config->ap.ftm_responder)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "transitionDisable", JS_NewBool(config->ap.transition_disable)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "saeExt", JS_NewBool(config->ap.sae_ext)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "wpa3CompatibleMode", JS_NewBool(config->ap.wpa3_compatible_mode)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "bssMaxIdlePeriod", JS_NewUint32(ctx, config->ap.bss_max_idle_cfg.period)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "bssMaxIdleProtectedKeepAlive", JS_NewBool(config->ap.bss_max_idle_cfg.protected_keep_alive)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "gtkRekeyIntervalSeconds", JS_NewUint32(ctx, config->ap.gtk_rekey_interval))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

JSValue js_wifi_start_ap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    wifi_config_t config = {0};
    bool allow_disconnect = false;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.startAP(options) expects one argument");
    if (!esp32_mquickjs_wifi_parse_ap_config(ctx, argv[0], &config, &allow_disconnect)) return JS_EXCEPTION;
    JSValue result;
    bool preparing = false;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (allow_disconnect) {
        s_ap_stage = "ap-configuration-admission";
        err = esp32_mquickjs_wifi_activate_ap(&config);
        if (err != ESP_OK) {
            const char *stage = esp32_mquickjs_wifi_state()->cleanup_stage;
            if (stage != NULL) s_ap_stage = stage;
            goto fail;
        }
        s_ap_stage = NULL;
        result = wifi_ap_result(ctx, &config);
        goto done;
    }
    if (s_ap_netif != NULL || s_ap_cleanup_pending || s_ap_lease.acquired) {
        s_ap_stage = "admission";
        goto fail;
    }
    bool shared = false;
    s_ap_stage = "ap-reopen-admission";
    err = esp32_mquickjs_wifi_reopen_ap_shared(&config, &shared);
    if (shared) {
        if (err != ESP_OK) goto fail;
        result = wifi_ap_result(ctx, &config);
        goto done;
    }
    s_ap_stage = "admission";
    err = esp32_mquickjs_wifi_radio_reserve_ap(&s_ap_lease);
    if (err != ESP_OK) goto fail;
    preparing = true;
    s_ap_stage = "configuration";
    err = esp32_mquickjs_wifi_configure_interfaces(WIFI_MODE_AP, WIFI_STORAGE_RAM,
        true, NULL, NULL, &config, esp32_mquickjs_wifi_radio_accept_ap_config, NULL, NULL, false);
    if (esp32_mquickjs_wifi_configuration_pending()) preparing = false;
    if (err != ESP_OK) {
        const char *stage = esp32_mquickjs_wifi_state()->cleanup_stage;
        if (stage != NULL) s_ap_stage = stage;
        goto fail;
    }
    preparing = false;
    s_ap_stage = NULL;
    result = wifi_ap_result(ctx, &config);
    goto done;
fail:
    if (preparing) {
        const char *stage = s_ap_stage;
        esp_err_t cleanup_error = wifi_ap_cleanup();
        if (cleanup_error != ESP_OK) err = cleanup_error;
        else s_ap_stage = stage;
    }
    result = wifi_ap_error(ctx, "wifi.startAP", err);
done:
    esp32_mquickjs_wireless_secure_zero(&config, sizeof(config));
    return result;
}

JSValue js_wifi_stop_ap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc > 1) return JS_ThrowTypeError(ctx, "wifi.stopAP(options?) expects at most one argument");
    uint32_t timeout_ms;
    if (!esp32_mquickjs_wifi_capture_stop_ap_timeout(ctx, argc ? argv[0] : JS_UNDEFINED, &timeout_ms)) return JS_EXCEPTION;
    esp_err_t err = esp32_mquickjs_wifi_wait_begin(timeout_ms);
    if (err == ESP_OK) {
        err = wifi_ap_cleanup();
        esp32_mquickjs_wifi_wait_end();
    }
    if (err != ESP_OK) return wifi_ap_error(ctx, "wifi.stopAP", err);
    return esp32_mquickjs_wifi_make_status_object(ctx);
}

static JSValue wifi_ap_client_result(JSContext *ctx, const wifi_sta_info_t *station, uint16_t aid)
{
    static const char *const names[] = {"11b", "11g", "11n", "11a", "11ac", "11ax", "lr"};
    const bool flags[] = {station->phy_11b, station->phy_11g, station->phy_11n,
                         station->phy_11a, station->phy_11ac, station->phy_11ax, station->phy_lr};
    JSGCRef result_ref, phy_ref, value_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *phy = JS_PushGCRef(ctx, &phy_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    char address[18];
    snprintf(address, sizeof(address), MACSTR, MAC2STR(station->mac));
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "address", JS_NewString(ctx, address)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "aid", aid ? JS_NewUint32(ctx, aid) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "rssi", JS_NewInt32(ctx, station->rssi))) goto fail;
    *phy = JS_NewArray(ctx, 0);
    if (JS_IsException(*phy)) goto fail;
    uint32_t count = 0;
    for (size_t i = 0; i < sizeof(flags) / sizeof(*flags); ++i) {
        if (!flags[i]) continue;
        *value = JS_NewString(ctx, names[i]);
        if (JS_IsException(*value) || JS_IsException(JS_SetPropertyUint32(ctx, *phy, count++, *value))) goto fail;
    }
    if (!esp32_mquickjs_set_property_ref(ctx, result, "phy", *phy)) goto fail;
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &phy_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &phy_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static bool wifi_ap_capture_clients_options(JSContext *ctx, JSValue options, bool *include_ip)
{
    static const char *const keys[] = {"includeIp"};
    *include_ip = false;
    if (JS_IsUndefined(options)) return true;
    JSGCRef options_ref, value_ref;
    JSValue *root = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *root = options;
    bool ok = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, "wifi.apClients", keys, 1)) goto done;
    *value = JS_GetPropertyStr(ctx, *root, "includeIp");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!JS_IsBool(*value)) {
            JS_ThrowTypeError(ctx, "wifi.apClients includeIp must be boolean");
            goto done;
        }
        *include_ip = *value == JS_TRUE;
    }
    ok = true;
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &options_ref);
    return ok;
}

/* Runtime task owns the AP netif. SDK copies DHCP observations on the lwIP
 * task; no JS/poller runs between the association snapshot and this lookup.
 * Missing MACs leave outputs unchanged, so zero every IP before querying. */
static esp_err_t wifi_ap_client_ips(const esp32_mquickjs_wifi_ap_clients_t *snapshot,
                                    esp_netif_pair_mac_ip_t *pairs)
{
    if (s_ap_cleanup_pending || s_ap_netif == NULL || snapshot == NULL || pairs == NULL ||
        snapshot->stations.num < 0 || snapshot->stations.num > ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS)
        return ESP_ERR_INVALID_STATE;
    memset(pairs, 0, sizeof(*pairs) * ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS);
    if (snapshot->stations.num == 0) return ESP_OK;
    for (int i = 0; i < snapshot->stations.num; ++i)
        memcpy(pairs[i].mac, snapshot->stations.sta[i].mac, sizeof(pairs[i].mac));
    return esp_netif_dhcps_get_clients_by_mac(s_ap_netif, snapshot->stations.num, pairs);
}

JSValue js_wifi_deauth_client(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1 || !JS_IsString(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "wifi.deauthClient(address) expects one MAC string");
    JSCStringBuf buffer;
    size_t length;
    const char *text = JS_ToCStringLen(ctx, &length, argv[0], &buffer);
    if (!text) return JS_EXCEPTION;
    uint8_t address[6];
    if (length != 17 || !esp32_mquickjs_wireless_parse_address(text, address) ||
        (address[0] & 1U) || !memcmp(address, "\0\0\0\0\0\0", 6))
        return JS_ThrowTypeError(ctx, "wifi.deauthClient expects a nonzero unicast MAC in xx:xx:xx:xx:xx:xx form");
    bool requested = false, unknown = false;
    const char *stage = s_ap_cleanup_pending ? s_ap_stage : "deauth-admission";
    esp_err_t error = s_ap_cleanup_pending ? ESP_ERR_INVALID_STATE :
        esp32_mquickjs_wifi_radio_ap_deauth(&s_ap_lease, address, &requested, &unknown, &stage);
    if (error == ESP_OK) return JS_NewBool(requested);
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, error)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stage", stage ? JS_NewString(ctx, stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "requestAccepted", unknown ? JS_NULL : JS_NewBool(requested)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "handoffUnknown", JS_NewBool(unknown))) goto fail;
    (void)esp32_mquickjs_throw_native_error(ctx, "WIFI_AP_FAILED", "wifi.deauthClient",
        "AP client deauthentication request failed", *details);
fail:
    JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_ap_clients(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc > 1) return JS_ThrowTypeError(ctx, "wifi.apClients(options?) expects at most one argument");
    bool include_ip;
    if (!wifi_ap_capture_clients_options(ctx, argc ? argv[0] : JS_UNDEFINED, &include_ip)) return JS_EXCEPTION;
#if !CONFIG_LWIP_DHCPS
    if (include_ip) {
        if (!s_ap_cleanup_pending) s_ap_stage = "clients-ip";
        return wifi_ap_error(ctx, "wifi.apClients", ESP_ERR_NOT_SUPPORTED);
    }
#endif
    esp32_mquickjs_wifi_ap_clients_t snapshot;
    esp_netif_pair_mac_ip_t pairs[ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS];
    esp_err_t err = s_ap_cleanup_pending ? ESP_ERR_INVALID_STATE :
        esp32_mquickjs_wifi_radio_ap_clients(&s_ap_lease, &snapshot);
    const char *stage = "clients";
    if (err == ESP_OK && include_ip) {
        stage = "clients-ip";
        err = wifi_ap_client_ips(&snapshot, pairs);
    }
    if (err != ESP_OK) {
        /* Do not overwrite the cleanup stage retained by a failed stopAP. */
        if (!s_ap_cleanup_pending) s_ap_stage = stage;
        return wifi_ap_error(ctx, "wifi.apClients", err);
    }
    JSGCRef result_ref, entry_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *entry = JS_PushGCRef(ctx, &entry_ref);
    *result = JS_NewArray(ctx, 0);
    if (JS_IsException(*result)) goto fail;
    for (int i = 0; i < snapshot.stations.num; ++i) {
        *entry = wifi_ap_client_result(ctx, &snapshot.stations.sta[i], snapshot.aid[i]);
        if (JS_IsException(*entry)) goto fail;
        if (include_ip) {
            char address[16];
            JSValue ip = JS_NULL;
            if (pairs[i].ip.addr != 0) {
                if (esp_ip4addr_ntoa(&pairs[i].ip, address, sizeof(address)) == NULL) {
                    JS_ThrowInternalError(ctx, "failed to format AP client IPv4 address");
                    goto fail;
                }
                ip = JS_NewString(ctx, address);
            }
            if (!esp32_mquickjs_set_property_ref(ctx, entry, "ip", ip)) goto fail;
        }
        if (JS_IsException(JS_SetPropertyUint32(ctx, *result, i, *entry))) goto fail;
    }
    JS_PopGCRef(ctx, &entry_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &entry_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

esp_err_t esp32_mquickjs_wifi_ap_begin_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_recovery_request_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode)
{
    if (s_ap_lifecycle.identity != 0U || s_ap_coordinator.identity != 0U ||
        s_ap_cleanup_pending || s_ap_detach_error != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (s_ap_raw_owner.identity != 0U && (operation == NULL ||
        operation->kind != ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX ||
        operation->generation != s_ap_raw_owner.generation)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp32_mquickjs_wifi_radio_begin_recovery(
        application, station, &s_ap_lease, operation, token, mode);
    if (err != ESP_OK) return err;
    s_ap_coordinator = *token;
    s_ap_cleanup_pending = true;
    s_ap_stage = "recovery-handoff";
    esp32_mquickjs_wifi_radio_release(&s_ap_lease);
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_ap_begin_stopped_restart(
    esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_radio_restart_selection_t *selection)
{
    if (s_ap_raw_owner.identity != 0U || s_ap_lifecycle.identity != 0U || s_ap_coordinator.identity != 0U ||
        s_ap_lease.acquired || s_ap_cleanup_pending || s_ap_detach_error != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp32_mquickjs_wifi_radio_begin_stopped_restart(token, selection);
    if (err != ESP_OK) return err;
    s_ap_coordinator = *token;
    s_ap_cleanup_pending = true;
    s_ap_stage = "configuration-handoff";
    return ESP_OK;
}

#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
esp_err_t esp32_mquickjs_wifi_ap_begin_enterprise_restart(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_eap_profile_t *profile, bool allow_ap_restart,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode)
{
    if (s_ap_raw_owner.identity || s_ap_lifecycle.identity || s_ap_coordinator.identity ||
        s_ap_cleanup_pending || s_ap_detach_error != ESP_OK) return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp32_mquickjs_wifi_radio_eap_begin_restart(identity, application, station,
        &s_ap_lease, profile, allow_ap_restart, token, mode);
    if (error == ESP_OK) {
        s_ap_coordinator = *token;
        s_ap_cleanup_pending = true;
        s_ap_stage = "configuration-handoff";
    }
    return error;
}

esp_err_t esp32_mquickjs_wifi_ap_begin_enterprise_stop(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (s_ap_raw_owner.identity != 0U || s_ap_lifecycle.identity != 0U || s_ap_coordinator.identity != 0U ||
        s_ap_cleanup_pending || s_ap_detach_error != ESP_OK) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp32_mquickjs_wifi_radio_eap_begin_stop(identity,
        application, station, &s_ap_lease, token);
    if (err != ESP_OK) return err;
    s_ap_coordinator = *token;
    s_ap_cleanup_pending = true;
    s_ap_stage = "configuration-handoff";
    /* SDK still pins this lease; release only after exact EAP retirement. */
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_ap_release_enterprise_stop(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (!token || !token->identity || token->identity != s_ap_coordinator.identity ||
        token->generation != s_ap_coordinator.generation) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_radio_release(&s_ap_lease);
    return s_ap_lease.acquired ? ESP_ERR_INVALID_STATE : ESP_OK;
}

#endif

esp_err_t esp32_mquickjs_wifi_ap_begin_configuration(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (s_ap_raw_owner.identity != 0U || s_ap_lifecycle.identity != 0U || s_ap_coordinator.identity != 0U ||
        s_ap_cleanup_pending || s_ap_detach_error != ESP_OK) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp32_mquickjs_wifi_radio_begin_lifecycle(
        application, station, &s_ap_lease, token);
    if (err != ESP_OK) return err;
    s_ap_coordinator = *token;
    s_ap_cleanup_pending = true;
    s_ap_stage = "configuration-handoff";
    esp32_mquickjs_wifi_radio_release(&s_ap_lease);
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_ap_begin_start_configuration(
    esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, bool *already_started)
{
    if (s_ap_raw_owner.identity != 0U || s_ap_lifecycle.identity != 0U || s_ap_coordinator.identity != 0U ||
        s_ap_cleanup_pending || s_ap_detach_error != ESP_OK) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp32_mquickjs_wifi_radio_begin_start_lifecycle(
        application, station, &s_ap_lease, selection, token, already_started);
    if (err != ESP_OK || *already_started) return err;
    s_ap_coordinator = *token;
    s_ap_cleanup_pending = true;
    s_ap_stage = "configuration-handoff";
    esp32_mquickjs_wifi_radio_release(&s_ap_lease);
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_ap_begin_selected_configuration(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    const esp32_mquickjs_wifi_radio_start_controls_t *start_controls,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (s_ap_raw_owner.identity != 0U || s_ap_lifecycle.identity != 0U || s_ap_coordinator.identity != 0U ||
        s_ap_cleanup_pending || s_ap_detach_error != ESP_OK) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp32_mquickjs_wifi_radio_begin_configuration_lifecycle(
        application, station, &s_ap_lease, selection, controls, start_controls, token);
    if (err != ESP_OK) return err;
    s_ap_coordinator = *token;
    s_ap_cleanup_pending = true;
    s_ap_stage = "configuration-handoff";
    esp32_mquickjs_wifi_radio_release(&s_ap_lease);
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_ap_prepare_for_configuration(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    esp_err_t err = esp32_mquickjs_wifi_radio_check_stopped_lifecycle(token, false);
    if (err != ESP_OK) return err;
    if (s_ap_raw_owner.identity != 0U || s_ap_lifecycle.identity != 0U || s_ap_coordinator.identity != 0U ||
        s_ap_lease.acquired || s_ap_cleanup_pending || s_ap_detach_error != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (s_ap_netif != NULL) return ESP_OK;
    s_ap_cleanup_pending = true;
    err = wifi_ap_prepare_netif();
    if (err == ESP_OK) { s_ap_cleanup_pending = false; s_ap_stage = NULL; }
    return err;
}

static esp_err_t wifi_ap_retire_for_token(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (s_ap_lifecycle.identity != 0U || s_ap_lease.acquired ||
        (s_ap_coordinator.identity != 0U &&
         (s_ap_coordinator.identity != token->identity ||
          s_ap_coordinator.generation != token->generation))) return ESP_ERR_INVALID_STATE;
    s_ap_cleanup_pending = true;
    esp_err_t err = wifi_ap_retire_netif();
    if (err == ESP_OK) {
        memset(&s_ap_raw_owner, 0, sizeof(s_ap_raw_owner));
        memset(&s_ap_coordinator, 0, sizeof(s_ap_coordinator));
        s_ap_cleanup_pending = false;
        s_ap_stage = NULL;
    }
    return err;
}

esp_err_t esp32_mquickjs_wifi_ap_retire_for_configuration(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    esp_err_t err = esp32_mquickjs_wifi_radio_check_stopped_lifecycle(token, true);
    return err == ESP_OK ? wifi_ap_retire_for_token(token) : err;
}

esp_err_t esp32_mquickjs_wifi_ap_retire_for_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    esp_err_t err = esp32_mquickjs_wifi_radio_check_stopped_recovery(token);
    return err == ESP_OK ? wifi_ap_retire_for_token(token) : err;
}

esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_configuration_slot(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{
    if (esp32_mquickjs_wifi_radio_check_stopped_lifecycle(token, false) != ESP_OK ||
        s_ap_lifecycle.identity != 0U || s_ap_coordinator.identity != 0U ||
        s_ap_lease.acquired || s_ap_netif == NULL ||
        s_ap_cleanup_pending || s_ap_detach_error != ESP_OK) return NULL;
    return &s_ap_lease;
}

bool esp32_mquickjs_init_wifi_ap_runtime(JSContext *ctx)
{
    if (!s_ap_cleanup_pending) return true;
    esp_err_t err = wifi_ap_cleanup();
    if (err == ESP_OK) return true;
    (void)wifi_ap_error(ctx, "wifi.stopAP", err);
    return false;
}

void esp32_mquickjs_deinit_wifi_ap_runtime(void)
{
    (void)wifi_ap_cleanup();
}
#elif CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
esp_err_t esp32_mquickjs_wifi_ap_open_raw_tx_rate(
    esp32_mquickjs_wifi_raw_tx_ap_context_t *context, const wifi_tx_rate_config_t *rate,
    uint8_t channel, esp32_mquickjs_wifi_radio_lease_t *lease, uint8_t *actual_channel, const char **stage)
{ (void)context; (void)rate; (void)channel; (void)lease; (void)actual_channel; if (stage) *stage = "ap-rate-disabled"; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp32_mquickjs_wifi_ap_close_raw_tx_rate(
    esp32_mquickjs_wifi_raw_tx_ap_context_t *context, esp32_mquickjs_wifi_radio_lease_t *lease,
    bool physically_terminated, const char **stage)
{ (void)context; (void)lease; (void)physically_terminated; if (stage) *stage = "ap-rate-disabled"; return ESP_ERR_NOT_SUPPORTED; }
JSValue esp32_mquickjs_wifi_ap_status(JSContext *ctx) { (void)ctx; return JS_NULL; }
esp_err_t esp32_mquickjs_wifi_ap_begin_start_configuration(
    esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, bool *already_started)
{ return esp32_mquickjs_wifi_radio_begin_start_lifecycle(application, station, NULL, selection, token, already_started); }

esp_err_t esp32_mquickjs_wifi_ap_reopen(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station, wifi_config_t *config,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ (void)application; (void)station; (void)config; (void)token; return ESP_ERR_NOT_SUPPORTED; }

esp_err_t esp32_mquickjs_wifi_ap_begin_partial_stop(
    const esp32_mquickjs_wifi_radio_lease_t *application, const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ (void)application; (void)station; (void)token; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp32_mquickjs_wifi_ap_retire_partial_stop(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ (void)token; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp32_mquickjs_wifi_ap_finish_partial_stop(esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ (void)token; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp32_mquickjs_wifi_ap_adopt_partial_stop(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ (void)token; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp32_mquickjs_wifi_ap_netif_cleanup_error(void) { return ESP_OK; }
esp_err_t esp32_mquickjs_wifi_ap_begin_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_recovery_request_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode)
{ return esp32_mquickjs_wifi_radio_begin_recovery(application, station, NULL, operation, token, mode); }
esp_err_t esp32_mquickjs_wifi_ap_begin_stopped_restart(
    esp32_mquickjs_wifi_radio_lifecycle_t *token,
    esp32_mquickjs_wifi_radio_restart_selection_t *selection)
{ return esp32_mquickjs_wifi_radio_begin_stopped_restart(token, selection); }
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
esp_err_t esp32_mquickjs_wifi_ap_begin_enterprise_stop(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ return esp32_mquickjs_wifi_radio_eap_begin_stop(identity, application, station, NULL, token); }
esp_err_t esp32_mquickjs_wifi_ap_begin_enterprise_restart(uint64_t identity,
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_eap_profile_t *profile, bool allow_ap_restart,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode)
{ return esp32_mquickjs_wifi_radio_eap_begin_restart(identity, application, station, NULL, profile, allow_ap_restart, token, mode); }
esp_err_t esp32_mquickjs_wifi_ap_release_enterprise_stop(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ (void)token; return ESP_OK; }

#endif
esp_err_t esp32_mquickjs_wifi_ap_begin_configuration(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ return esp32_mquickjs_wifi_radio_begin_lifecycle(application, station, NULL, token); }
esp_err_t esp32_mquickjs_wifi_ap_begin_selected_configuration(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    esp32_mquickjs_wifi_radio_configuration_selection_t *selection,
    const esp32_mquickjs_wifi_radio_config_controls_t *controls,
    const esp32_mquickjs_wifi_radio_start_controls_t *start_controls,
    esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ return esp32_mquickjs_wifi_radio_begin_configuration_lifecycle(
    application, station, NULL, selection, controls, start_controls, token); }
esp_err_t esp32_mquickjs_wifi_ap_prepare_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ (void)token; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp32_mquickjs_wifi_ap_retire_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ return esp32_mquickjs_wifi_radio_check_stopped_lifecycle(token, true); }
esp_err_t esp32_mquickjs_wifi_ap_retire_for_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ return esp32_mquickjs_wifi_radio_check_stopped_recovery(token); }
esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_configuration_slot(const esp32_mquickjs_wifi_radio_lifecycle_t *token)
{ (void)token; return NULL; }

const esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_control_lease(void) { return NULL; }
JSValue js_wifi_deauth_client(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.deauthClient(address) expects one MAC string");
    return esp32_mquickjs_wifi_throw_operation_error(ctx, "WIFI_AP_UNSUPPORTED", "wifi.deauthClient", ESP_ERR_NOT_SUPPORTED, 0, 0);
}
JSValue js_wifi_ap_clients(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc > 1) return JS_ThrowTypeError(ctx, "wifi.apClients(options?) expects at most one argument");
    return esp32_mquickjs_wifi_throw_operation_error(ctx, "WIFI_AP_UNSUPPORTED", "wifi.apClients", ESP_ERR_NOT_SUPPORTED, 0, 0);
}
JSValue js_wifi_start_ap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return esp32_mquickjs_wifi_throw_operation_error(ctx, "WIFI_AP_UNSUPPORTED", "wifi.startAP", ESP_ERR_NOT_SUPPORTED, 0, 0);
}
JSValue js_wifi_stop_ap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc > 1) return JS_ThrowTypeError(ctx, "wifi.stopAP(options?) expects at most one argument");
    uint32_t timeout_ms;
    if (!esp32_mquickjs_wifi_capture_stop_ap_timeout(ctx, argc ? argv[0] : JS_UNDEFINED, &timeout_ms)) return JS_EXCEPTION;
    return esp32_mquickjs_wifi_make_status_object(ctx);
}
bool esp32_mquickjs_init_wifi_ap_runtime(JSContext *ctx) { (void)ctx;return true; }
void esp32_mquickjs_deinit_wifi_ap_runtime(void) {}
#endif
