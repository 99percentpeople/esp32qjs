#include "esp32_mquickjs_wifi_ap_prestart.h"
#if ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_wireless_core.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

/* Configure BEFORE native AP allocation. Both the native setter and PMF API
 * may start an allocated AP even before _do_wifi_start(AP). The private setter
 * accepts only the old STA mode, keeping all other native validation intact.
 * The build pins the full SDK archive for each supported target. */
extern int __real_wifi_mode_set(int mode);
extern int esp32qjs_wifi_ap_prestart_set_config_native(wifi_config_t *config);
extern bool current_task_is_wifi_task(void);
extern uint8_t g_ic[];

/* Wi-Fi task only. Reviewed C3/C5/S3 native AP interface slot, also used by
 * wifi_create_softap/wifi_destroy_softap and both configuration setters. Read
 * through memcpy; never change native pointers or duplicate SDK global state. */
static bool wifi_ap_prestart_interface_absent(void)
{
    uint32_t interface;
    memcpy(&interface, g_ic + 20, sizeof(interface));
    return interface == 0;
}

typedef struct {
    uint32_t generation, identity;
    wifi_config_t requested, previous, work;
    bool (*accept)(const wifi_config_t *, const wifi_config_t *);
    esp32_mquickjs_wifi_ap_prestart_result_t result;
    bool active;
} wifi_ap_prestart_t;

static portMUX_TYPE s_ap_prestart_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_ap_prestart_t *s_ap_prestart;
static bool s_ap_prestart_reserving;

static bool wifi_ap_prestart_exact(uint32_t generation, uint32_t identity)
{
    return identity && s_ap_prestart && s_ap_prestart->generation == generation && s_ap_prestart->identity == identity;
}

esp_err_t esp32_mquickjs_wifi_ap_prestart_prepare(uint32_t generation, uint32_t identity,
    const wifi_config_t *requested, const wifi_config_t *previous,
    bool (*accept)(const wifi_config_t *, const wifi_config_t *))
{
    if (!identity || !requested || !previous || !accept) return ESP_ERR_INVALID_ARG;
    if (!requested->ap.pmf_cfg.capable && (requested->ap.pmf_cfg.required ||
        !esp32_mquickjs_wifi_radio_pmf_disable_allowed(WIFI_IF_AP, requested))) return ESP_ERR_NOT_SUPPORTED;
    portENTER_CRITICAL(&s_ap_prestart_lock);
    bool available = !s_ap_prestart && !s_ap_prestart_reserving;
    if (available) s_ap_prestart_reserving = true;
    portEXIT_CRITICAL(&s_ap_prestart_lock);
    if (!available) return ESP_ERR_INVALID_STATE;
    wifi_ap_prestart_t *state = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*state),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state) {
        state->generation = generation;
        state->identity = identity;
        state->requested = *requested;
        state->previous = *previous;
        state->accept = accept;
    }
    portENTER_CRITICAL(&s_ap_prestart_lock);
    s_ap_prestart = state;
    s_ap_prestart_reserving = false;
    portEXIT_CRITICAL(&s_ap_prestart_lock);
    return state ? ESP_OK : ESP_ERR_NO_MEM;
}

/* Runs on the Wi-Fi task before AP allocation and before any AP start. */
static esp_err_t wifi_ap_prestart_write(wifi_ap_prestart_t *state, const wifi_config_t *config)
{
    if (!wifi_ap_prestart_interface_absent()) return ESP_ERR_INVALID_STATE;
    if (!config->ap.pmf_cfg.capable && (config->ap.pmf_cfg.required ||
        !esp32_mquickjs_wifi_radio_pmf_disable_allowed(WIFI_IF_AP, config))) return ESP_ERR_NOT_SUPPORTED;
    state->work = *config;  /* SDK may normalize its input; keep original capture. */
    esp_err_t error = esp32qjs_wifi_ap_prestart_set_config_native(&state->work);
    esp32_mquickjs_wireless_secure_zero(&state->work, sizeof(state->work));
    if (!error && !config->ap.pmf_cfg.capable) error = esp_wifi_disable_pmf_config(WIFI_IF_AP);
    if (!error) error = esp_wifi_get_config(WIFI_IF_AP, &state->work);
    if (!error && !state->accept(config, &state->work)) error = ESP_ERR_INVALID_RESPONSE;
    esp32_mquickjs_wireless_secure_zero(&state->work, sizeof(state->work));
    return error;
}

int __wrap_wifi_mode_set(int mode)
{
    portENTER_CRITICAL(&s_ap_prestart_lock);
    wifi_ap_prestart_t *state = s_ap_prestart && s_ap_prestart->active ? s_ap_prestart : NULL;
    bool repeated = state && mode == WIFI_MODE_APSTA && state->result.entered;
    if (state && mode == WIFI_MODE_APSTA && !repeated) state->result.entered = true;
    portEXIT_CRITICAL(&s_ap_prestart_lock);
    if (!state) return __real_wifi_mode_set(mode);
    if (mode != WIFI_MODE_APSTA) {
        int error = __real_wifi_mode_set(mode);
        /* Pinned SDK reverts allocation after our rejected mode callback. Its
         * return alone does not prove retirement: inspect the native AP slot. */
        if (!error && mode == WIFI_MODE_STA && current_task_is_wifi_task() &&
            wifi_ap_prestart_interface_absent()) {
            portENTER_CRITICAL(&s_ap_prestart_lock);
            state->result.ap_quiesced = state->result.mutated && !state->result.mode_ready;
            portEXIT_CRITICAL(&s_ap_prestart_lock);
        }
        return error;
    }
    if (repeated) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_ap_prestart_result_t result = {.entered = true, .stage = "ap-prestart-context"};
    wifi_mode_t before;
    esp_err_t error = current_task_is_wifi_task() ? esp_wifi_get_mode(&before) : ESP_ERR_INVALID_STATE;
    if (!error && (before != WIFI_MODE_STA || !wifi_ap_prestart_interface_absent())) error = ESP_ERR_INVALID_STATE;
    if (!error) {
        result.stage = "ap-prestart-config";
        result.mutated = true;
        error = wifi_ap_prestart_write(state, &state->requested);
        result.verified = !error;
        if (!error) {
            result.stage = "ap-prestart-create";
            error = __real_wifi_mode_set(mode);
            result.mode_ready = !error;
        }
        if (error) {
            /* AP has never started. Restore saved configuration in the same
             * native window only if allocation remains absent. The write guard
             * retains a fault if a failed factory left a partial interface.
             * Original error survives a failed rollback; do not retry here. */
            result.rollback_attempted = true;
            result.rollback_error = wifi_ap_prestart_write(state, &state->previous);
            result.rollback_complete = !result.rollback_error;
        }
    }
    result.error = error;
    portENTER_CRITICAL(&s_ap_prestart_lock);
    state->result = result;
    portEXIT_CRITICAL(&s_ap_prestart_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_ap_prestart_activate(uint32_t generation, uint32_t identity,
    esp32_mquickjs_wifi_ap_prestart_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_ap_prestart_result_t){0};
    portENTER_CRITICAL(&s_ap_prestart_lock);
    wifi_ap_prestart_t *state = wifi_ap_prestart_exact(generation, identity) && !s_ap_prestart->active &&
        !s_ap_prestart->result.returned ? s_ap_prestart : NULL;
    if (state) state->active = true;
    portEXIT_CRITICAL(&s_ap_prestart_lock);
    if (!state) return ESP_ERR_INVALID_STATE;
    /* Pinned ioctl command 10 is synchronous: allocation/queue failure occurs
     * before dispatch; accepted work waits for the native command semaphore.
     * The public caller's wait policy does not release this storage early. */
    esp_err_t error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    portENTER_CRITICAL(&s_ap_prestart_lock);
    state->active = false;
    state->result.returned = true;
    if (!state->result.error) {
        state->result.error = error ? error : state->result.verified ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
        if (state->result.error) state->result.stage = "ap-prestart-mode";
    }
    *result = state->result;
    portEXIT_CRITICAL(&s_ap_prestart_lock);
    return result->error;
}

esp_err_t esp32_mquickjs_wifi_ap_prestart_release(uint32_t generation, uint32_t identity)
{
    portENTER_CRITICAL(&s_ap_prestart_lock);
    wifi_ap_prestart_t *state = wifi_ap_prestart_exact(generation, identity) && !s_ap_prestart->active ? s_ap_prestart : NULL;
    if (state) s_ap_prestart = NULL;
    portEXIT_CRITICAL(&s_ap_prestart_lock);
    if (!state) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wireless_secure_zero(state, sizeof(*state));
    esp32_mquickjs_memory_payload_free(state);
    return ESP_OK;
}
#endif
