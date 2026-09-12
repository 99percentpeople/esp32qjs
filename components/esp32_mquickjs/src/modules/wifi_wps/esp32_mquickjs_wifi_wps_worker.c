#include "esp32_mquickjs_wifi_wps_worker.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wireless_core.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include <stdatomic.h>
#include <string.h>

/* Reviewed fixed-SDK native IPC ABI. All pointer-bearing input/output belongs
 * to this retained object, never to a caller stack or public Future. */
typedef struct { int (*fn)(void *); void *arg; uint32_t arg_size; } wps_ipc_config_t;
extern int esp_wifi_ipc_internal(wps_ipc_config_t *config, bool sync);
_Static_assert(sizeof(wps_ipc_config_t) == 12, "review WPS IPC ABI");
enum { WPS_BEGIN, WPS_START, WPS_STATUS, WPS_STOP, WPS_RETIRE, WPS_RELEASE,
    WPS_PIN_COPY, WPS_PIN_COMMIT, WPS_CREDENTIALS_COPY, WPS_CREDENTIALS_COMMIT };

struct esp32_mquickjs_wifi_wps_worker {
    esp_wps_config_t config;
    uint64_t identity;
    esp32_mquickjs_wifi_wps_native_status_t native;
    union { uint8_t pin[8]; esp32_mquickjs_wifi_wps_credentials_t credentials; } result;
    wps_ipc_config_t ipc;
    int command;
    esp_err_t command_error, error, cleanup_error;
    atomic_bool command_completed, timer_reached;
    esp_timer_handle_t timer;
    uint32_t revision;
    const char *stage;
    bool begin_attempted, start_attempted, closing, stop_done, capture_retired, retired, handoff_unknown;
    bool timer_started, timer_stopped, timer_drained;
};

static int wps_worker_native(void *argument)
{
    esp32_mquickjs_wifi_wps_worker_t *w = argument;
    int error;
    switch (w->command) {
    case WPS_BEGIN:
        error = esp32qjs_wps_native_begin(&w->config, &w->identity);
        esp32_mquickjs_wireless_secure_zero(&w->config, sizeof(w->config));
        break;
    case WPS_START:
        error = esp32qjs_wps_native_start(w->identity);
        break;
    case WPS_STATUS:
        error = esp32qjs_wps_native_status(w->identity, &w->native);
        break;
    case WPS_STOP:
        error = w->closing ? esp32qjs_wps_native_close(w->identity) :
            esp32qjs_wps_native_finish_capture(w->identity);
        break;
    case WPS_RETIRE:
        error = esp32qjs_wps_native_retire_state(w->identity);
        if (error == ESP_OK) error = esp32qjs_wps_native_checkpoint(w->identity, &w->revision);
        break;
    case WPS_RELEASE:
        error = esp32qjs_wps_native_release(w->identity, w->revision);
        if (error == ESP_OK) w->identity = 0;
        break;
    case WPS_PIN_COPY:
        error = esp32qjs_wps_native_pin_copy(w->identity, w->result.pin);
        break;
    case WPS_PIN_COMMIT:
        error = esp32qjs_wps_native_pin_commit(w->identity);
        break;
    case WPS_CREDENTIALS_COPY:
        error = esp32qjs_wps_native_credentials_copy(w->identity, &w->result.credentials);
        break;
    case WPS_CREDENTIALS_COMMIT:
        error = esp32qjs_wps_native_credentials_commit(w->identity);
        break;
    default:
        error = ESP_ERR_INVALID_STATE;
        break;
    }
    if (w->identity) esp32qjs_wps_native_status(w->identity, &w->native);
    w->command_error = error;
    atomic_store_explicit(&w->command_completed, true, memory_order_release);
    return error;
}

static esp_err_t wps_worker_dispatch(esp32_mquickjs_wifi_wps_worker_t *w, int command)
{
    if (w->handoff_unknown) return ESP_ERR_INVALID_STATE;
    w->command = command;
    atomic_store_explicit(&w->command_completed, false, memory_order_relaxed);
    esp_err_t error = esp_wifi_ipc_internal(&w->ipc, true);
    if (atomic_load_explicit(&w->command_completed, memory_order_acquire))
        return w->command_error != ESP_OK ? w->command_error : error;
    /* These exact fixed-SDK failures precede dispatch. Otherwise the native
     * task may still access every field above: never retry, scrub or free it. */
    if (error != ESP_ERR_NO_MEM && error != ESP_ERR_WIFI_NOT_INIT && error != ESP_ERR_INVALID_ARG) {
        w->handoff_unknown = true;
        if (error == ESP_OK) error = ESP_ERR_INVALID_STATE;
        if (w->error == ESP_OK) w->error = error;
        w->stage = "wps-ipc-unconfirmed";
        return error;
    }
    return error;
}

bool esp32_mquickjs_wifi_wps_config_valid(const esp_wps_config_t *config)
{
    if (!config || (config->wps_type != WPS_TYPE_PBC && config->wps_type != WPS_TYPE_PIN)) return false;
    const wps_factory_information_t *f = &config->factory_info;
    if (!memchr(f->manufacturer, 0, sizeof(f->manufacturer)) ||
        !memchr(f->model_number, 0, sizeof(f->model_number)) ||
        !memchr(f->model_name, 0, sizeof(f->model_name)) ||
        !memchr(f->device_name, 0, sizeof(f->device_name))) return false;
    /* Absent PIN or the SDK's "00000000" default requests generation. Other
     * supplied PINs must be eight digits with a checksum; never silently
     * replace malformed input with a generated PIN. */
    if (!config->pin[0]) return true;
    if (config->wps_type == WPS_TYPE_PBC)
        return memcmp(config->pin, "00000000", sizeof(config->pin)) == 0;
    if (config->pin[8]) return false;
    unsigned checksum = 0;
    for (unsigned i = 0; i < 8; ++i) {
        if (config->pin[i] < '0' || config->pin[i] > '9') return false;
        checksum += (unsigned)(config->pin[i] - '0') * (i % 2 ? 1 : 3);
    }
    return checksum % 10 == 0;
}

esp_err_t esp32_mquickjs_wifi_wps_worker_create(const esp_wps_config_t *config,
    esp32_mquickjs_wifi_wps_worker_t **out)
{
    if (!out || *out || !esp32_mquickjs_wifi_wps_config_valid(config)) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_wps_worker_t *w = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*w), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!w) return ESP_ERR_NO_MEM;
    w->config = *config;
    w->ipc = (wps_ipc_config_t){.fn = wps_worker_native, .arg = w, .arg_size = 0};
    atomic_init(&w->command_completed, false);
    atomic_init(&w->timer_reached, false);
    *out = w;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_wps_worker_prepare(esp32_mquickjs_wifi_wps_worker_t *w)
{
    if (!w || w->begin_attempted || w->closing) return ESP_ERR_INVALID_STATE;
    w->begin_attempted = true;
    w->stage = "wps-native-enable";
    w->error = wps_worker_dispatch(w, WPS_BEGIN);
    if (w->error == ESP_OK) w->stage = NULL;
    return w->error;
}

esp_err_t esp32_mquickjs_wifi_wps_worker_start(esp32_mquickjs_wifi_wps_worker_t *w)
{
    if (!w || !w->begin_attempted || w->handoff_unknown || w->error != ESP_OK ||
        !w->identity || w->start_attempted || w->closing) return ESP_ERR_INVALID_STATE;
    w->start_attempted = true;
    w->stage = "wps-native-start";
    w->error = wps_worker_dispatch(w, WPS_START);
    if (w->error == ESP_OK) w->stage = NULL;
    return w->error;
}

esp_err_t esp32_mquickjs_wifi_wps_worker_status(esp32_mquickjs_wifi_wps_worker_t *w,
    esp32_mquickjs_wifi_wps_worker_status_t *status)
{
    if (!w || !status) return ESP_ERR_INVALID_ARG;
    if (w->handoff_unknown) {
        /* No receipt: native may still be writing identity/status/result. Do
         * not inspect those fields, even for diagnostics. This is a retained
         * diagnostic fault, not permission to retry after a later receipt. */
        *status = (esp32_mquickjs_wifi_wps_worker_status_t){.error = w->error,
            .cleanup_error = w->cleanup_error, .reserved_bytes = sizeof(*w),
            .stage = w->stage, .closing = w->closing, .handoff_unknown = true};
        return ESP_OK;
    }
    esp_err_t error = ESP_OK;
    if (w->identity) error = wps_worker_dispatch(w, WPS_STATUS);
    if (w->handoff_unknown) {
        *status = (esp32_mquickjs_wifi_wps_worker_status_t){.error = error,
            .cleanup_error = w->cleanup_error, .reserved_bytes = sizeof(*w),
            .stage = "wps-native-status", .closing = w->closing, .handoff_unknown = true};
        return ESP_OK;
    }
    *status = (esp32_mquickjs_wifi_wps_worker_status_t){.native = w->native,
        .error = w->error != ESP_OK ? w->error : w->native.error,
        .cleanup_error = w->cleanup_error, .reserved_bytes = sizeof(*w) + (w->identity ? w->native.reserved_bytes : 0),
        .stage = w->stage, .closing = w->closing, .capture_retired = w->capture_retired,
        .retired = w->retired, .handoff_unknown = w->handoff_unknown};
    if (error != ESP_OK) { status->error = error; status->stage = "wps-native-status"; }
    return ESP_OK;
}

static void wps_worker_timer(void *argument)
{
    esp32_mquickjs_wifi_wps_worker_t *w = argument;
    atomic_store_explicit(&w->timer_reached, true, memory_order_release);
}

static esp_err_t wps_worker_timer_drain(esp32_mquickjs_wifi_wps_worker_t *w)
{
    if (w->timer_drained) return ESP_OK;
    esp_err_t error;
    if (!w->timer) {
        const esp_timer_create_args_t args = {.callback = wps_worker_timer, .arg = w,
            .dispatch_method = ESP_TIMER_TASK, .name = "qjs_wps_drain"};
        w->stage = "wps-timer-create";
        error = esp_timer_create(&args, &w->timer);
        if (error != ESP_OK) return error;
    }
    if (!w->timer_started) {
        w->stage = "wps-timer-start";
        error = esp_timer_start_once(w->timer, 1);
        if (error != ESP_OK) return error;
        w->timer_started = true;
    }
    w->stage = "wps-timer-wait";
    if (!atomic_load_explicit(&w->timer_reached, memory_order_acquire)) return ESP_ERR_NOT_FINISHED;
    if (!w->timer_stopped) {
        w->stage = "wps-timer-stop";
        error = esp_timer_stop_blocking(w->timer, 1);
        if (error != ESP_OK) return error;
        w->timer_stopped = true;
    }
    w->stage = "wps-timer-delete";
    error = esp_timer_delete(w->timer);
    if (error != ESP_OK) return error;
    w->timer = NULL;
    w->timer_drained = true;
    return ESP_OK;
}

static esp_err_t wps_worker_drain(esp32_mquickjs_wifi_wps_worker_t *w)
{
    if (w->capture_retired) return ESP_OK;
    if (!w->identity) { w->capture_retired = true; return ESP_OK; }
    esp_err_t error;
    if (!w->stop_done) {
        w->stage = "wps-input-stop";
        error = wps_worker_dispatch(w, WPS_STOP);
        if (error != ESP_OK) return error;
        w->stop_done = true;
    }
    error = wps_worker_timer_drain(w);
    if (error != ESP_OK) return error;
    /* This native dispatch is deliberately AFTER the timer callback returned:
     * old legacy timers can queue native work, but cannot overtake this fence. */
    w->stage = "wps-native-retire";
    error = wps_worker_dispatch(w, WPS_RETIRE);
    if (error != ESP_OK) return error;
    w->capture_retired = true;
    w->stage = NULL;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_wps_worker_finish_capture(esp32_mquickjs_wifi_wps_worker_t *w)
{
    if (!w || w->handoff_unknown || !w->start_attempted || w->closing) return ESP_ERR_INVALID_STATE;
    w->cleanup_error = wps_worker_drain(w);
    return w->cleanup_error;
}

static esp_err_t wps_worker_copy(esp32_mquickjs_wifi_wps_worker_t *w, bool credentials, void *out, bool commit)
{
    if (!w || w->closing || w->handoff_unknown || !w->identity ||
        (credentials && !w->capture_retired)) return ESP_ERR_INVALID_STATE;
    if ((!commit && !out) || (commit && out)) return ESP_ERR_INVALID_ARG;
    int command = credentials ? (commit ? WPS_CREDENTIALS_COMMIT : WPS_CREDENTIALS_COPY) :
        (commit ? WPS_PIN_COMMIT : WPS_PIN_COPY);
    esp_err_t error = wps_worker_dispatch(w, command);
    if (w->handoff_unknown) return error;
    if (error == ESP_OK && !commit)
        memcpy(out, &w->result, credentials ? sizeof(w->result.credentials) : sizeof(w->result.pin));
    esp32_mquickjs_wireless_secure_zero(&w->result, sizeof(w->result));
    return error;
}

esp_err_t esp32_mquickjs_wifi_wps_worker_pin(esp32_mquickjs_wifi_wps_worker_t *w, uint8_t pin[8], bool commit)
{
    return wps_worker_copy(w, false, pin, commit);
}
esp_err_t esp32_mquickjs_wifi_wps_worker_credentials(esp32_mquickjs_wifi_wps_worker_t *w,
    esp32_mquickjs_wifi_wps_credentials_t *credentials, bool commit)
{
    return wps_worker_copy(w, true, credentials, commit);
}

esp_err_t esp32_mquickjs_wifi_wps_worker_close(esp32_mquickjs_wifi_wps_worker_t *w)
{
    if (!w || w->handoff_unknown) return ESP_ERR_INVALID_STATE;
    if (w->retired) return ESP_OK;
    if (!w->closing) {
        w->closing = true;
        /* A terminal capture may already be retired but its result must still
         * be explicitly scrubbed by close before native record release. */
        if (w->identity) {
            w->capture_retired = false;
            w->stop_done = false;
        }
    }
    esp_err_t error = wps_worker_drain(w);
    if (error != ESP_OK) goto done;
    if (w->identity) {
        w->stage = "wps-result-release";
        error = wps_worker_dispatch(w, WPS_RELEASE);
        if (error == ESP_ERR_NOT_FINISHED && !w->handoff_unknown) {
            w->capture_retired = false;
            w->timer_drained = w->timer_started = w->timer_stopped = false;
            atomic_store_explicit(&w->timer_reached, false, memory_order_relaxed);
        }
        if (error != ESP_OK) goto done;
    }
    esp32_mquickjs_wireless_secure_zero(&w->config, sizeof(w->config));
    esp32_mquickjs_wireless_secure_zero(&w->result, sizeof(w->result));
    w->retired = true;
    w->stage = NULL;
done:
    w->cleanup_error = error;
    return error;
}

esp_err_t esp32_mquickjs_wifi_wps_worker_release(esp32_mquickjs_wifi_wps_worker_t **owner)
{
    if (!owner || !*owner || !(*owner)->retired || (*owner)->handoff_unknown ||
        (*owner)->identity || (*owner)->timer) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_wps_worker_t *w = *owner;
    *owner = NULL;
    esp32_mquickjs_wireless_secure_zero(w, sizeof(*w));
    esp32_mquickjs_memory_payload_free(w);
    return ESP_OK;
}
#endif
