#include "esp32_mquickjs_wifi_eap_install.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
#include "esp_eap_client.h"
struct os_reltime;
#include "utils/eloop.h"
#include <string.h>

bool current_task_is_wifi_task(void);
bool esp32qjs_eap_on_worker(void);
bool esp32qjs_eap_configuration_idle(void);
void esp32qjs_eap_configuration_changed(void);

/* Accessed exclusively on the Wi-Fi task. No JS/runtime pointer is retained. */
static esp32_mquickjs_wifi_eap_profile_t *s_eap_installed_profile;
static uint64_t s_eap_install_identity, s_eap_install_next_identity;
static esp32_mquickjs_wifi_eap_install_result_t s_eap_install_result;

static void eap_install_observe(esp32_mquickjs_wifi_eap_install_result_t *result)
{
    (void)esp32_mquickjs_wifi_eap_sdk_snapshot(&result->sdk);
    result->identity = s_eap_install_identity;
    result->retained = s_eap_installed_profile != NULL;
    result->enabled = result->sdk.entered &&
        (result->sdk.resources & ESP32_MQUICKJS_WIFI_EAP_SDK_ENABLED) &&
        (result->sdk.resources & ESP32_MQUICKJS_WIFI_EAP_SDK_DRIVER_ENABLED);
}

static esp_err_t eap_install_clear_owned(void)
{
    esp_err_t error = esp_wifi_sta_enterprise_disable();
    esp32_mquickjs_wifi_eap_sdk_snapshot_t state;
    esp_err_t observed = esp32_mquickjs_wifi_eap_sdk_snapshot(&state);
    if (error != ESP_OK) return error;
    if (observed != ESP_OK || !state.entered || state.resources != 0) return ESP_ERR_INVALID_STATE;
    /* Reset SDK policy that globals_reset does not reset. All operations are
     * on this same task after actual native retirement. */
    error = esp_eap_client_set_disable_time_check(false);
    if (error != ESP_OK) return error;
    esp_wifi_set_okc_support(false);
    esp32qjs_eap_configuration_changed();
    esp32_mquickjs_wifi_eap_profile_t *profile = s_eap_installed_profile;
    s_eap_installed_profile = NULL;
    s_eap_install_identity = 0;
    esp32_mquickjs_wifi_eap_profile_release(profile);
    return ESP_OK;
}

/* Profile storage always has a private trailing zero. PEM parsers require it
 * in the supplied length; DER keeps its exact length. Already terminated PEM
 * is not padded twice. Search is bounded, including inputs with leading text. */
static int eap_install_blob_length(esp32_mquickjs_wifi_eap_span_t span)
{
    static const char marker[] = "-----BEGIN ";
    for (size_t i = 0; i + sizeof(marker) - 1 <= span.length; ++i) {
        if (memcmp(span.data + i, marker, sizeof(marker) - 1) == 0)
            return (int)(span.length + (span.data[span.length - 1] != 0));
    }
    return (int)span.length;
}

static esp_err_t eap_install_apply(const esp32_mquickjs_wifi_eap_input_t *input, const char **stage)
{
    const esp32_mquickjs_wifi_eap_span_t *f = input->fields;
    const esp32_mquickjs_wifi_eap_policy_t *p = &input->policy;
    esp_err_t error;
#define EAP_INSTALL_STEP(name, call) do { *stage = name; error = (call); if (error != ESP_OK) return error; } while (0)
    EAP_INSTALL_STEP("methods", esp_eap_client_set_eap_methods(p->methods));
    EAP_INSTALL_STEP("time-check", esp_eap_client_set_disable_time_check(p->disable_time_check));
    EAP_INSTALL_STEP("phase2", esp_eap_client_set_ttls_phase2_method(p->ttls_phase2));
#if CONFIG_ESP_WIFI_SUITE_B_192
    EAP_INSTALL_STEP("suiteb", esp_eap_client_set_suiteb_192bit_certification(p->suiteb));
#endif
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE && CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT
    EAP_INSTALL_STEP("bundle", esp_eap_client_use_default_cert_bundle(p->default_bundle));
#endif
    if (f[ESP32_MQUICKJS_WIFI_EAP_ANONYMOUS_IDENTITY].length)
        EAP_INSTALL_STEP("anonymous-identity", esp_eap_client_set_identity(f[ESP32_MQUICKJS_WIFI_EAP_ANONYMOUS_IDENTITY].data, f[ESP32_MQUICKJS_WIFI_EAP_ANONYMOUS_IDENTITY].length));
    if (f[ESP32_MQUICKJS_WIFI_EAP_USERNAME].length)
        EAP_INSTALL_STEP("username", esp_eap_client_set_username(f[ESP32_MQUICKJS_WIFI_EAP_USERNAME].data, f[ESP32_MQUICKJS_WIFI_EAP_USERNAME].length));
    if (f[ESP32_MQUICKJS_WIFI_EAP_PASSWORD].length)
        EAP_INSTALL_STEP("password", esp_eap_client_set_password(f[ESP32_MQUICKJS_WIFI_EAP_PASSWORD].data, f[ESP32_MQUICKJS_WIFI_EAP_PASSWORD].length));
    if (f[ESP32_MQUICKJS_WIFI_EAP_NEW_PASSWORD].length)
        EAP_INSTALL_STEP("new-password", esp_eap_client_set_new_password(f[ESP32_MQUICKJS_WIFI_EAP_NEW_PASSWORD].data, f[ESP32_MQUICKJS_WIFI_EAP_NEW_PASSWORD].length));
    if (f[ESP32_MQUICKJS_WIFI_EAP_CA_CERT].length)
        EAP_INSTALL_STEP("ca-cert", esp_eap_client_set_ca_cert(f[ESP32_MQUICKJS_WIFI_EAP_CA_CERT].data, eap_install_blob_length(f[ESP32_MQUICKJS_WIFI_EAP_CA_CERT])));
    if (f[ESP32_MQUICKJS_WIFI_EAP_CLIENT_CERT].length)
        EAP_INSTALL_STEP("client-cert-key", esp_eap_client_set_certificate_and_key(
            f[ESP32_MQUICKJS_WIFI_EAP_CLIENT_CERT].data, eap_install_blob_length(f[ESP32_MQUICKJS_WIFI_EAP_CLIENT_CERT]),
            f[ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY].data, eap_install_blob_length(f[ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY]),
            f[ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY_PASSWORD].data, f[ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY_PASSWORD].length));
#if CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT
    if (f[ESP32_MQUICKJS_WIFI_EAP_DOMAIN].length)
        EAP_INSTALL_STEP("domain", esp_eap_client_set_domain_name((const char *)f[ESP32_MQUICKJS_WIFI_EAP_DOMAIN].data));
#else
    if (p->methods & ESP_EAP_TYPE_FAST) {
        esp_eap_fast_config fast = {.fast_provisioning = p->fast_provisioning,
            .fast_max_pac_list_len = p->fast_max_pac_list, .fast_pac_format_binary = p->fast_binary};
        EAP_INSTALL_STEP("fast", esp_eap_client_set_fast_params(fast));
        /* Empty PAC explicitly requests provisioning; never discard short data. */
        static const uint8_t empty_pac;
        EAP_INSTALL_STEP("pac", esp_eap_client_set_pac_file(
            f[ESP32_MQUICKJS_WIFI_EAP_PAC].length ? f[ESP32_MQUICKJS_WIFI_EAP_PAC].data : &empty_pac,
            f[ESP32_MQUICKJS_WIFI_EAP_PAC].length));
    }
#endif
    esp32qjs_eap_configuration_changed();
    EAP_INSTALL_STEP("enable", esp_wifi_sta_enterprise_enable());
    /* SDK enable unconditionally enables OKC; apply the explicit profile last. */
    esp_wifi_set_okc_support(p->okc);
    *stage = "enabled";
    return ESP_OK;
#undef EAP_INSTALL_STEP
}

typedef struct {
    esp32_mquickjs_wifi_eap_profile_t *profile;
    uint64_t identity;
    unsigned action; /* 0 install, 1 clear, 2 status, 3 restart source */
    esp32_mquickjs_wifi_eap_install_result_t *output;
} eap_install_call_t;

static int eap_install_dispatch(void *opaque, void *unused)
{
    (void)unused;
    eap_install_call_t *call = opaque;
    esp32_mquickjs_wifi_eap_install_result_t *out = call->output;
    out->entered = true;
    if (call->action) {
        if (!call->identity || call->identity != s_eap_install_identity || !s_eap_installed_profile)
            return out->error = ESP_ERR_INVALID_STATE;
        if (call->action == 3 && (call->profile != s_eap_installed_profile ||
            s_eap_install_next_identity == UINT64_MAX)) return out->error = ESP_ERR_INVALID_STATE;
        *out = s_eap_install_result;
        out->entered = true;
        if (call->action == 1) {
            out->stage = "clear";
            out->error = out->cleanup_error = eap_install_clear_owned();
        }
        eap_install_observe(out);
        if (call->action == 3 && (!out->enabled || out->cleanup_error != ESP_OK ||
            out->sdk.cleanup_error != ESP_OK || out->sdk.control_error != ESP_OK))
            return out->error = ESP_ERR_INVALID_STATE;
        if (s_eap_installed_profile) s_eap_install_result = *out;
        return out->error;
    }
    out->stage = "admission";
    if (s_eap_installed_profile || !esp32qjs_eap_configuration_idle())
        return out->error = ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_eap_input_t input;
    if (!esp32_mquickjs_wifi_eap_profile_view(call->profile, &input))
        return out->error = ESP_ERR_INVALID_ARG;
    /* The fixed TLS adapter otherwise chooses VERIFY_NONE. Require explicit
     * trust for certificate-authenticated methods before any SDK mutation. */
    if ((input.policy.methods & (ESP_EAP_TYPE_TLS | ESP_EAP_TYPE_PEAP | ESP_EAP_TYPE_TTLS)) &&
        !input.policy.default_bundle && !input.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT].length) {
        out->stage = "server-trust";
        return out->error = ESP_ERR_INVALID_ARG;
    }
    out->stage = "identity";
    if (s_eap_install_next_identity == UINT64_MAX) return out->error = ESP_ERR_INVALID_STATE;
    out->stage = "profile-reference";
    if (!esp32_mquickjs_wifi_eap_profile_retain(call->profile))
        return out->error = ESP_ERR_NO_MEM;
    s_eap_installed_profile = call->profile;
    s_eap_install_identity = ++s_eap_install_next_identity;
    out->stage = "baseline-disable";
    out->error = esp_wifi_sta_enterprise_disable();
    if (out->error == ESP_OK) out->error = eap_install_apply(&input, &out->stage);
    if (out->error != ESP_OK) out->cleanup_error = eap_install_clear_owned();
    eap_install_observe(out);
    if (s_eap_installed_profile) s_eap_install_result = *out;
    return out->error;
}

static esp_err_t eap_install_call(eap_install_call_t *call)
{
    if (!call->output) return ESP_ERR_INVALID_ARG;
    *call->output = (esp32_mquickjs_wifi_eap_install_result_t){.stage = "dispatch",
        .error = ESP_ERR_INVALID_STATE, .cleanup_error = ESP_OK,
        .sdk = {.resources = UINT32_MAX, .cleanup_error = ESP_ERR_INVALID_STATE, .control_error = ESP_ERR_INVALID_STATE}};
    if (esp32qjs_eap_on_worker()) return ESP_ERR_INVALID_STATE;
    int result = current_task_is_wifi_task() ? eap_install_dispatch(call, NULL) :
        eloop_register_timeout_blocking(eap_install_dispatch, call, NULL);
    if (!call->output->entered) call->output->error = result ? result : ESP_ERR_INVALID_STATE;
    return call->output->error;
}

esp_err_t esp32_mquickjs_wifi_eap_install(esp32_mquickjs_wifi_eap_profile_t *profile,
    esp32_mquickjs_wifi_eap_install_result_t *output)
{
    eap_install_call_t call = {.profile = profile, .output = output};
    return eap_install_call(&call);
}

esp_err_t esp32_mquickjs_wifi_eap_install_clear(uint64_t identity,
    esp32_mquickjs_wifi_eap_install_result_t *output)
{
    eap_install_call_t call = {.identity = identity, .action = 1, .output = output};
    return eap_install_call(&call);
}

esp_err_t esp32_mquickjs_wifi_eap_install_status(uint64_t identity,
    esp32_mquickjs_wifi_eap_install_result_t *output)
{
    eap_install_call_t call = {.identity = identity, .action = 2, .output = output};
    return eap_install_call(&call);
}

esp_err_t esp32_mquickjs_wifi_eap_install_restart_source(uint64_t identity,
    esp32_mquickjs_wifi_eap_profile_t *profile, esp32_mquickjs_wifi_eap_install_result_t *output)
{
    if (!profile) return ESP_ERR_INVALID_ARG;
    eap_install_call_t call = {.identity = identity, .profile = profile, .action = 3, .output = output};
    return eap_install_call(&call);
}
#endif
