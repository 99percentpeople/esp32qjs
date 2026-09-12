#include "esp32_mquickjs_wifi_dpp_worker.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
#include "esp32_mquickjs_wireless_core.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include <stdatomic.h>
#include <string.h>

/* Same fixed-SDK synchronous IPC ABI already used by WPS/Enterprise workers.
 * Keep config, command, output and receipt together until native handoff is
 * known. The driver may retain this pointer even after the public wait ends. */
typedef struct { int (*fn)(void *); void *arg; uint32_t arg_size; } dpp_ipc_config_t;
extern int esp_wifi_ipc_internal(dpp_ipc_config_t *config, bool sync);
_Static_assert(sizeof(dpp_ipc_config_t) == 12, "review DPP IPC ABI");
enum { DPP_BEGIN, DPP_BOOTSTRAP, DPP_LISTEN, DPP_STATUS, DPP_CLOSE, DPP_RELEASE,
    DPP_URI_COPY, DPP_URI_COMMIT, DPP_CONFIG_COPY, DPP_CONFIGS_COMMIT, DPP_SELECT, DPP_CHECK_CONNECTION };

struct esp32_mquickjs_wifi_dpp_worker {
    esp32_mquickjs_wifi_dpp_worker_options_t options;
    uint64_t identity;
    esp32_mquickjs_wifi_dpp_result_status_t native;
    union { char uri[ESP32QJS_DPP_URI_MAX + 1U]; esp_dpp_config_data_t config; } result;
    dpp_ipc_config_t ipc;
    unsigned config_index;
    uint8_t expected_akm, peer_bssid[6];
    int command;
    esp_err_t command_error, error, cleanup_error;
    atomic_bool command_completed;
    const char *stage;
    bool begin_attempted, bootstrap_submitted, listening, closing;
    bool capture_retired, retired, handoff_unknown;
    bool selection_attempted, connection_selected, install_connector;
};

static int dpp_worker_native(void *argument)
{
    esp32_mquickjs_wifi_dpp_worker_t *w = argument;
    esp_err_t error;
    switch (w->command) {
    case DPP_BEGIN:
        error = esp32qjs_dpp_native_begin(&w->identity);
        break;
    case DPP_BOOTSTRAP:
        error = esp32qjs_dpp_native_bootstrap(w->identity, w->options.channels,
            w->options.has_key ? w->options.private_key_hex_der : NULL,
            w->options.has_info ? w->options.info : NULL);
        /* Successful bootstrap owns a separate scrubbed native job. */
        esp32_mquickjs_wireless_secure_zero(&w->options, sizeof(w->options));
        break;
    case DPP_LISTEN:
        error = esp32qjs_dpp_native_listen(w->identity);
        break;
    case DPP_STATUS:
        error = esp32qjs_dpp_result_status(w->identity, &w->native);
        break;
    case DPP_CLOSE:
        error = esp32qjs_dpp_native_close(w->identity);
        break;
    case DPP_RELEASE:
        error = esp32qjs_dpp_result_release(w->identity);
        if (error == ESP_OK) w->identity = 0;
        break;
    case DPP_URI_COPY:
        error = esp32qjs_dpp_result_uri_copy(w->identity, w->result.uri, sizeof(w->result.uri));
        break;
    case DPP_URI_COMMIT:
        error = esp32qjs_dpp_result_uri_commit(w->identity);
        break;
    case DPP_CONFIG_COPY:
        error = esp32qjs_dpp_result_config_copy(w->identity, w->config_index, &w->result.config);
        break;
    case DPP_CONFIGS_COMMIT:
        error = esp32qjs_dpp_result_configs_commit(w->identity);
        break;
    case DPP_SELECT:
        error = esp32qjs_dpp_native_select(w->identity, w->install_connector ? &w->result.config : NULL);
        esp32_mquickjs_wireless_secure_zero(&w->result, sizeof(w->result));
        break;
    case DPP_CHECK_CONNECTION:
        error = esp32qjs_dpp_native_check_connection(w->identity, w->expected_akm, w->peer_bssid);
        break;
    default:
        error = ESP_ERR_INVALID_STATE;
        break;
    }
    if (w->identity && w->command != DPP_STATUS) {
        esp_err_t status_error = esp32qjs_dpp_result_status(w->identity, &w->native);
        if (error == ESP_OK) error = status_error;
    }
    w->command_error = error;
    atomic_store_explicit(&w->command_completed, true, memory_order_release);
    return error;
}

static esp_err_t dpp_worker_dispatch(esp32_mquickjs_wifi_dpp_worker_t *w, int command)
{
    if (w->handoff_unknown) return ESP_ERR_INVALID_STATE;
    w->command = command;
    atomic_store_explicit(&w->command_completed, false, memory_order_relaxed);
    esp_err_t error = esp_wifi_ipc_internal(&w->ipc, true);
    if (atomic_load_explicit(&w->command_completed, memory_order_acquire))
        return w->command_error != ESP_OK ? w->command_error : error;
    if (error != ESP_ERR_NO_MEM && error != ESP_ERR_WIFI_NOT_INIT && error != ESP_ERR_INVALID_ARG) {
        w->handoff_unknown = true;
        if (error == ESP_OK) error = ESP_ERR_INVALID_STATE;
        if (w->error == ESP_OK) w->error = error;
        w->stage = "dpp-ipc-unconfirmed";
    }
    return error;
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_validate(const esp32_mquickjs_wifi_dpp_worker_options_t *options)
{
    if (!options || !memchr(options->channels, 0, sizeof(options->channels)) ||
        !memchr(options->private_key_hex_der, 0, sizeof(options->private_key_hex_der)) ||
        !memchr(options->info, 0, sizeof(options->info)) ||
        (!options->has_key && options->private_key_hex_der[0]) || (!options->has_info && options->info[0]))
        return ESP_ERR_INVALID_ARG;
    return esp32qjs_dpp_bootstrap_validate(options->channels,
        options->has_key ? options->private_key_hex_der : NULL, options->has_info ? options->info : NULL);
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_create(const esp32_mquickjs_wifi_dpp_worker_options_t *options,
    esp32_mquickjs_wifi_dpp_worker_t **out)
{
    if (!out || *out) return ESP_ERR_INVALID_ARG;
    esp_err_t error = esp32_mquickjs_wifi_dpp_worker_validate(options);
    if (error != ESP_OK) return error;
    esp32_mquickjs_wifi_dpp_worker_t *w = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*w), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!w) return ESP_ERR_NO_MEM;
    w->options = *options;
    w->ipc = (dpp_ipc_config_t){.fn = dpp_worker_native, .arg = w};
    atomic_init(&w->command_completed, false);
    *out = w;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_prepare(esp32_mquickjs_wifi_dpp_worker_t *w)
{
    if (!w || w->begin_attempted || w->closing) return ESP_ERR_INVALID_STATE;
    w->begin_attempted = true;
    w->stage = "dpp-native-init";
    w->error = dpp_worker_dispatch(w, DPP_BEGIN);
    if (w->error != ESP_OK) return w->error;
    w->stage = "dpp-native-bootstrap";
    w->error = dpp_worker_dispatch(w, DPP_BOOTSTRAP);
    if (w->error == ESP_OK) { w->bootstrap_submitted = true; w->stage = NULL; }
    return w->error;
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_listen(esp32_mquickjs_wifi_dpp_worker_t *w)
{
    if (!w || w->handoff_unknown || w->closing || w->capture_retired ||
        !w->bootstrap_submitted || w->error != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (w->listening) return ESP_OK;
    esp_err_t error = dpp_worker_dispatch(w, DPP_STATUS);
    if (error != ESP_OK) return error;
    if (w->native.terminal) return w->native.error != ESP_OK ? w->native.error : ESP_ERR_INVALID_STATE;
    if (!w->native.uri_length) return ESP_ERR_NOT_FINISHED;
    w->stage = "dpp-native-listen";
    w->error = dpp_worker_dispatch(w, DPP_LISTEN);
    if (w->error == ESP_OK) { w->listening = true; w->stage = NULL; }
    return w->error;
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_status(esp32_mquickjs_wifi_dpp_worker_t *w,
    esp32_mquickjs_wifi_dpp_worker_status_t *status)
{
    if (!w || !status) return ESP_ERR_INVALID_ARG;
    esp_err_t error = ESP_OK;
    if (!w->handoff_unknown && w->identity) error = dpp_worker_dispatch(w, DPP_STATUS);
    *status = (esp32_mquickjs_wifi_dpp_worker_status_t){.error = w->error,
        .cleanup_error = w->cleanup_error, .reserved_bytes = sizeof(*w),
        .stage = w->stage, .closing = w->closing, .handoff_unknown = w->handoff_unknown,
        .selection_attempted = w->selection_attempted, .connection_selected = w->connection_selected};
    /* Unknown handoff may still be writing identity/native/result. Never
     * inspect those fields, even after a late receipt becomes observable. */
    if (w->handoff_unknown) return ESP_OK;
    status->native = w->native;
    status->capture_retired = w->capture_retired;
    status->retired = w->retired;
    if (w->identity) status->reserved_bytes += w->native.reserved_bytes;
    if (status->error == ESP_OK) status->error = w->native.error;
    if (error != ESP_OK) { status->error = error; status->stage = "dpp-native-status"; }
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_uri(esp32_mquickjs_wifi_dpp_worker_t *w,
    char *out, size_t capacity, bool commit)
{
    if (!w || w->handoff_unknown || w->closing || !w->identity) return ESP_ERR_INVALID_STATE;
    if ((!commit && (!out || !capacity)) || (commit && (out || capacity))) return ESP_ERR_INVALID_ARG;
    esp_err_t error = dpp_worker_dispatch(w, commit ? DPP_URI_COMMIT : DPP_URI_COPY);
    if (w->handoff_unknown) return error;
    if (error == ESP_OK && !commit) {
        size_t length = strnlen(w->result.uri, sizeof(w->result.uri));
        if (length == sizeof(w->result.uri) || length >= capacity) error = ESP_ERR_INVALID_SIZE;
        else memcpy(out, w->result.uri, length + 1U);
    }
    esp32_mquickjs_wireless_secure_zero(&w->result, sizeof(w->result));
    return error;
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_config(esp32_mquickjs_wifi_dpp_worker_t *w,
    unsigned index, esp_dpp_config_data_t *out)
{
    if (!w || w->handoff_unknown || w->closing || !w->identity) return ESP_ERR_INVALID_STATE;
    if (!out) return ESP_ERR_INVALID_ARG;
    w->config_index = index;
    esp_err_t error = dpp_worker_dispatch(w, DPP_CONFIG_COPY);
    if (w->handoff_unknown) return error;
    if (error == ESP_OK) *out = w->result.config;
    esp32_mquickjs_wireless_secure_zero(&w->result, sizeof(w->result));
    return error;
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_configs_commit(esp32_mquickjs_wifi_dpp_worker_t *w)
{
    if (!w || w->handoff_unknown || w->closing || !w->identity) return ESP_ERR_INVALID_STATE;
    return dpp_worker_dispatch(w, DPP_CONFIGS_COMMIT);
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_finish_capture(esp32_mquickjs_wifi_dpp_worker_t *w)
{
    if (!w || w->handoff_unknown || w->closing || !w->identity || w->selection_attempted) return ESP_ERR_INVALID_STATE;
    if (w->capture_retired) return ESP_OK;
    esp_err_t error = dpp_worker_dispatch(w, DPP_STATUS);
    if (error != ESP_OK) return error;
    if (!w->native.terminal) return ESP_ERR_NOT_FINISHED;
    w->stage = "dpp-native-close";
    error = dpp_worker_dispatch(w, DPP_CLOSE);
    if (error == ESP_OK) { w->capture_retired = true; w->stage = NULL; }
    w->cleanup_error = error;
    return error;
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_select(esp32_mquickjs_wifi_dpp_worker_t *w,
    const esp_dpp_config_data_t *row, esp32_mquickjs_wifi_dpp_auth_t requested,
    esp32_mquickjs_wifi_dpp_auth_t *selected, wifi_config_t *station)
{
    if (!selected || !station) return ESP_ERR_INVALID_ARG;
    *selected = ESP32_MQUICKJS_DPP_AUTH_DEFAULT;
    memset(station, 0, sizeof(*station));
    if (!w || w->handoff_unknown || w->closing || !w->capture_retired ||
        !w->identity || w->selection_attempted || w->error != ESP_OK) return ESP_ERR_INVALID_STATE;
    wifi_config_t prepared;
    esp32_mquickjs_wifi_dpp_auth_t auth;
    esp_err_t error = esp32_mquickjs_wifi_dpp_connection_prepare(row, requested, &auth, &prepared);
    if (error != ESP_OK) return error;
    w->selection_attempted = true;
    w->stage = "dpp-capture-result-release";
    error = dpp_worker_dispatch(w, DPP_RELEASE);
    if (error != ESP_OK) goto done;
    memset(&w->native, 0, sizeof(w->native));
    w->capture_retired = w->bootstrap_submitted = w->listening = false;
    w->stage = "dpp-connection-init";
    error = dpp_worker_dispatch(w, DPP_BEGIN);
    if (error != ESP_OK) goto done;
    w->install_connector = auth == ESP32_MQUICKJS_DPP_AUTH_CONNECTOR;
    if (w->install_connector) w->result.config = *row;
    w->stage = "dpp-configuration-install";
    error = dpp_worker_dispatch(w, DPP_SELECT);
    if (error == ESP_OK) {
        *station = prepared; *selected = auth;
        w->connection_selected = true; w->stage = NULL;
    }
done:
    esp32_mquickjs_wireless_secure_zero(&prepared, sizeof(prepared));
    /* An unconfirmed command may still read/write the owned argument. */
    if (!w->handoff_unknown) esp32_mquickjs_wireless_secure_zero(&w->result, sizeof(w->result));
    w->error = error;
    return error;
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_check_connection(esp32_mquickjs_wifi_dpp_worker_t *w,
    esp32_mquickjs_wifi_dpp_auth_t selected, const uint8_t bssid[6])
{
    if (!w || !bssid || !w->connection_selected || w->handoff_unknown || w->closing || !w->identity)
        return ESP_ERR_INVALID_STATE;
    switch (selected) {
    case ESP32_MQUICKJS_DPP_AUTH_CONNECTOR: w->expected_akm = ESP_DPP_AKM_DPP; break;
    case ESP32_MQUICKJS_DPP_AUTH_PSK: w->expected_akm = ESP_DPP_AKM_PSK; break;
    case ESP32_MQUICKJS_DPP_AUTH_SAE: w->expected_akm = ESP_DPP_AKM_SAE; break;
    default: return ESP_ERR_INVALID_ARG;
    }
    memcpy(w->peer_bssid, bssid, sizeof(w->peer_bssid));
    w->stage = "dpp-negotiated-authentication";
    esp_err_t error = dpp_worker_dispatch(w, DPP_CHECK_CONNECTION);
    if (error == ESP_OK) w->stage = NULL;
    return error;
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_close(esp32_mquickjs_wifi_dpp_worker_t *w)
{
    if (!w || w->handoff_unknown) return ESP_ERR_INVALID_STATE;
    if (w->retired) return ESP_OK;
    w->closing = true;
    esp_err_t error = ESP_OK;
    if (w->identity && !w->capture_retired) {
        w->stage = "dpp-native-close";
        error = dpp_worker_dispatch(w, DPP_CLOSE);
        if (error != ESP_OK) goto done;
        w->capture_retired = true;
    }
    if (w->identity) {
        w->stage = "dpp-result-release";
        error = dpp_worker_dispatch(w, DPP_RELEASE);
        if (error != ESP_OK) goto done;
    }
    esp32_mquickjs_wireless_secure_zero(&w->options, sizeof(w->options));
    esp32_mquickjs_wireless_secure_zero(&w->result, sizeof(w->result));
    w->retired = true;
    w->stage = NULL;
done:
    w->cleanup_error = error;
    return error;
}

esp_err_t esp32_mquickjs_wifi_dpp_worker_release(esp32_mquickjs_wifi_dpp_worker_t **owner)
{
    if (!owner || !*owner || (*owner)->handoff_unknown || !(*owner)->retired || (*owner)->identity)
        return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_dpp_worker_t *w = *owner;
    *owner = NULL;
    esp32_mquickjs_wireless_secure_zero(w, sizeof(*w));
    esp32_mquickjs_memory_payload_free(w);
    return ESP_OK;
}
#endif
