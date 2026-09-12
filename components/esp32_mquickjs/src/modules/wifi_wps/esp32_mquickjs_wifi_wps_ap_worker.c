#include "esp32_mquickjs_wifi_wps_ap_worker.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
#include "esp32_mquickjs_wireless_core.h"
#include "esp32_mquickjs_wifi_wps_worker.h"
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
    WPS_PIN_COPY, WPS_PIN_COMMIT };

struct esp32_mquickjs_wifi_wps_ap_worker {
    esp_wps_config_t config;
    uint32_t identity;
    esp32_mquickjs_wifi_wps_ap_result_status_t native;
    uint8_t pin[8];
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
    esp32_mquickjs_wifi_wps_ap_worker_t *w = argument;
    int error;
    switch (w->command) {
    case WPS_BEGIN:
        error = esp32qjs_wps_ap_native_begin(&w->config, &w->identity);
        esp32_mquickjs_wireless_secure_zero(&w->config, sizeof(w->config));
        break;
    case WPS_START:
        error = esp32qjs_wps_ap_native_start(w->identity);
        break;
    case WPS_STATUS:
        error = esp32qjs_wps_ap_result_status(w->identity, &w->native);
        break;
    case WPS_STOP:
        error = esp32qjs_wps_ap_native_stop(w->identity);
        break;
    case WPS_RETIRE:
        error = esp32qjs_wps_ap_native_retire(w->identity);
        if (error == ESP_OK) error = esp32qjs_wps_ap_native_checkpoint(w->identity, &w->revision);
        break;
    case WPS_RELEASE:
        error = esp32qjs_wps_ap_native_release(w->identity, w->revision);
        if (error == ESP_OK) w->identity = 0;
        break;
    case WPS_PIN_COPY:
        error = esp32qjs_wps_ap_result_pin_copy(w->identity, w->pin);
        break;
    case WPS_PIN_COMMIT:
        error = esp32qjs_wps_ap_result_pin_commit(w->identity);
        break;
    default:
        error = ESP_ERR_INVALID_STATE;
        break;
    }
    if (w->identity) esp32qjs_wps_ap_result_status(w->identity, &w->native);
    w->command_error = error;
    atomic_store_explicit(&w->command_completed, true, memory_order_release);
    return error;
}

static esp_err_t wps_worker_dispatch(esp32_mquickjs_wifi_wps_ap_worker_t *w, int command)
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
        w->stage = "wps-ap-ipc-unconfirmed";
        return error;
    }
    return error;
}

esp_err_t esp32_mquickjs_wifi_wps_ap_worker_create(const esp_wps_config_t *config,
    esp32_mquickjs_wifi_wps_ap_worker_t **out)
{
    if (!out || *out || !esp32_mquickjs_wifi_wps_config_valid(config)) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_wps_ap_worker_t *w = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*w), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!w) return ESP_ERR_NO_MEM;
    w->config = *config;
    w->ipc = (wps_ipc_config_t){.fn = wps_worker_native, .arg = w, .arg_size = 0};
    atomic_init(&w->command_completed, false);
    atomic_init(&w->timer_reached, false);
    *out = w;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_wps_ap_worker_prepare(esp32_mquickjs_wifi_wps_ap_worker_t *w)
{
    if (!w || w->begin_attempted || w->closing) return ESP_ERR_INVALID_STATE;
    w->begin_attempted = true;
    w->stage = "wps-ap-native-enable";
    w->error = wps_worker_dispatch(w, WPS_BEGIN);
    if (w->error == ESP_OK) w->stage = NULL;
    return w->error;
}

esp_err_t esp32_mquickjs_wifi_wps_ap_worker_start(esp32_mquickjs_wifi_wps_ap_worker_t *w)
{
    if (!w || !w->begin_attempted || w->handoff_unknown || w->error != ESP_OK ||
        !w->identity || w->start_attempted || w->closing) return ESP_ERR_INVALID_STATE;
    w->start_attempted = true;
    w->stage = "wps-ap-native-start";
    w->error = wps_worker_dispatch(w, WPS_START);
    if (w->error == ESP_OK) w->stage = NULL;
    return w->error;
}

esp_err_t esp32_mquickjs_wifi_wps_ap_worker_status(esp32_mquickjs_wifi_wps_ap_worker_t *w,
    esp32_mquickjs_wifi_wps_ap_worker_status_t *status)
{
    if (!w || !status) return ESP_ERR_INVALID_ARG;
    if (w->handoff_unknown) {
        /* No receipt: native may still be writing identity/status/result. Do
         * not inspect those fields, even for diagnostics. This is a retained
         * diagnostic fault, not permission to retry after a later receipt. */
        *status = (esp32_mquickjs_wifi_wps_ap_worker_status_t){.error = w->error,
            .cleanup_error = w->cleanup_error, .reserved_bytes = sizeof(*w),
            .stage = w->stage, .closing = w->closing, .handoff_unknown = true};
        return ESP_OK;
    }
    esp_err_t error = ESP_OK;
    if (w->identity) error = wps_worker_dispatch(w, WPS_STATUS);
    if (w->handoff_unknown) {
        *status = (esp32_mquickjs_wifi_wps_ap_worker_status_t){.error = error,
            .cleanup_error = w->cleanup_error, .reserved_bytes = sizeof(*w),
            .stage = "wps-ap-native-status", .closing = w->closing, .handoff_unknown = true};
        return ESP_OK;
    }
    *status = (esp32_mquickjs_wifi_wps_ap_worker_status_t){.native = w->native,
        .error = w->error != ESP_OK ? w->error : w->native.error,
        .cleanup_error = w->cleanup_error, .reserved_bytes = sizeof(*w) + (w->identity ? w->native.reserved_bytes : 0),
        .stage = w->stage, .closing = w->closing, .capture_retired = w->capture_retired,
        .retired = w->retired, .handoff_unknown = w->handoff_unknown};
    if (error != ESP_OK) { status->error = error; status->stage = "wps-ap-native-status"; }
    return ESP_OK;
}

static void wps_worker_timer(void *argument)
{
    esp32_mquickjs_wifi_wps_ap_worker_t *w = argument;
    atomic_store_explicit(&w->timer_reached, true, memory_order_release);
}

static esp_err_t wps_worker_timer_drain(esp32_mquickjs_wifi_wps_ap_worker_t *w)
{
    if (w->timer_drained) return ESP_OK;
    esp_err_t error;
    if (!w->timer) {
        const esp_timer_create_args_t args = {.callback = wps_worker_timer, .arg = w,
            .dispatch_method = ESP_TIMER_TASK, .name = "qjs_wps_ap_drain"};
        w->stage = "wps-ap-timer-create";
        error = esp_timer_create(&args, &w->timer);
        if (error != ESP_OK) return error;
    }
    if (!w->timer_started) {
        w->stage = "wps-ap-timer-start";
        error = esp_timer_start_once(w->timer, 1);
        if (error != ESP_OK) return error;
        w->timer_started = true;
    }
    w->stage = "wps-ap-timer-wait";
    if (!atomic_load_explicit(&w->timer_reached, memory_order_acquire)) return ESP_ERR_NOT_FINISHED;
    if (!w->timer_stopped) {
        w->stage = "wps-ap-timer-stop";
        error = esp_timer_stop_blocking(w->timer, 1);
        if (error != ESP_OK) return error;
        w->timer_stopped = true;
    }
    w->stage = "wps-ap-timer-delete";
    error = esp_timer_delete(w->timer);
    if (error != ESP_OK) return error;
    w->timer = NULL;
    w->timer_drained = true;
    return ESP_OK;
}

static esp_err_t wps_worker_drain(esp32_mquickjs_wifi_wps_ap_worker_t *w)
{
    if (w->capture_retired) return ESP_OK;
    if (!w->identity) { w->capture_retired = true; return ESP_OK; }
    esp_err_t error;
    if (!w->stop_done) {
        w->stage = "wps-ap-input-stop";
        error = wps_worker_dispatch(w, WPS_STOP);
        if (error != ESP_OK) return error;
        w->stop_done = true;
    }
    error = wps_worker_timer_drain(w);
    if (error != ESP_OK) return error;
    /* This native dispatch is deliberately AFTER the timer callback returned:
     * old legacy timers can queue native work, but cannot overtake this fence. */
    w->stage = "wps-ap-native-retire";
    error = wps_worker_dispatch(w, WPS_RETIRE);
    if (error != ESP_OK) return error;
    w->capture_retired = true;
    w->stage = NULL;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_wps_ap_worker_finish_capture(esp32_mquickjs_wifi_wps_ap_worker_t *w)
{
    if (!w || w->handoff_unknown || !w->start_attempted || w->closing) return ESP_ERR_INVALID_STATE;
    int error = wps_worker_dispatch(w, WPS_STATUS);
    if (error != ESP_OK || w->handoff_unknown) return error;
    if (!w->native.terminal) return ESP_ERR_INVALID_STATE;
    w->cleanup_error = wps_worker_drain(w);
    return w->cleanup_error;
}

esp_err_t esp32_mquickjs_wifi_wps_ap_worker_pin(esp32_mquickjs_wifi_wps_ap_worker_t *w,
    uint8_t pin[8], bool commit)
{
    if (!w || w->closing || w->handoff_unknown || !w->identity) return ESP_ERR_INVALID_STATE;
    if ((!commit && !pin) || (commit && pin)) return ESP_ERR_INVALID_ARG;
    int error = wps_worker_dispatch(w, commit ? WPS_PIN_COMMIT : WPS_PIN_COPY);
    if (w->handoff_unknown) return error;
    if (error == ESP_OK && !commit) memcpy(pin, w->pin, sizeof(w->pin));
    esp32_mquickjs_wireless_secure_zero(w->pin, sizeof(w->pin));
    return error;
}

esp_err_t esp32_mquickjs_wifi_wps_ap_worker_close(esp32_mquickjs_wifi_wps_ap_worker_t *w)
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
        w->stage = "wps-ap-result-release";
        error = wps_worker_dispatch(w, WPS_RELEASE);
        if (error == ESP_ERR_NOT_FINISHED && !w->handoff_unknown) {
            w->capture_retired = false;
            w->timer_drained = w->timer_started = w->timer_stopped = false;
            atomic_store_explicit(&w->timer_reached, false, memory_order_relaxed);
        }
        if (error != ESP_OK) goto done;
    }
    esp32_mquickjs_wireless_secure_zero(&w->config, sizeof(w->config));
    esp32_mquickjs_wireless_secure_zero(w->pin, sizeof(w->pin));
    w->retired = true;
    w->stage = NULL;
done:
    w->cleanup_error = error;
    return error;
}

esp_err_t esp32_mquickjs_wifi_wps_ap_worker_release(esp32_mquickjs_wifi_wps_ap_worker_t **owner)
{
    if (!owner || !*owner || !(*owner)->retired || (*owner)->handoff_unknown ||
        (*owner)->identity || (*owner)->timer) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_wps_ap_worker_t *w = *owner;
    *owner = NULL;
    esp32_mquickjs_wireless_secure_zero(w, sizeof(*w));
    esp32_mquickjs_memory_payload_free(w);
    return ESP_OK;
}
#endif
