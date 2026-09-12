#include "esp32_mquickjs_wifi_smartconfig_timer.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

typedef struct {
    ETSTimer *timer;
    esp_timer_handle_t handle;
    ETSTimerFunc *fn;
    void *argument;
    uint32_t identity, callbacks;
    bool busy, enabled, delete_pending, stopped;
} smartconfig_timer_entry_t;
typedef struct {
    esp32_mquickjs_wifi_smartconfig_token_t token;
    esp_err_t error, cleanup_error;
    esp32_mquickjs_wifi_smartconfig_timer_stage_t stage;
    bool closing, cleanup_busy;
    smartconfig_timer_entry_t entries[ESP32_MQUICKJS_SMARTCONFIG_TIMERS];
} smartconfig_timers_t;

static DRAM_ATTR portMUX_TYPE s_sc_timers_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR smartconfig_timers_t *s_sc_timers;
static DRAM_ATTR uint32_t s_sc_timer_identity;
static bool s_sc_timers_reserving;

static bool sc_timers_exact(const esp32_mquickjs_wifi_smartconfig_token_t *token)
{
    return token && token->identity && s_sc_timers &&
        token->identity == s_sc_timers->token.identity &&
        token->radio_generation == s_sc_timers->token.radio_generation;
}
static smartconfig_timer_entry_t *IRAM_ATTR sc_timer_find(ETSTimer *timer)
{
    if (s_sc_timers && timer) for (unsigned i = 0; i < ESP32_MQUICKJS_SMARTCONFIG_TIMERS; ++i)
        if (s_sc_timers->entries[i].timer == timer) return &s_sc_timers->entries[i];
    return NULL;
}
static void IRAM_ATTR sc_timer_failed(esp_err_t error, esp32_mquickjs_wifi_smartconfig_timer_stage_t stage)
{
    if (error == ESP_OK) return;
    if (s_sc_timers->error == ESP_OK) {
        s_sc_timers->error = error;
        s_sc_timers->stage = stage;
    }
    if (stage == ESP32_MQUICKJS_SC_TIMER_STOP || stage == ESP32_MQUICKJS_SC_TIMER_DELETE)
        s_sc_timers->cleanup_error = error;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_timers_begin(
    const esp32_mquickjs_wifi_smartconfig_token_t *token,
    ETSTimer *const timers[ESP32_MQUICKJS_SMARTCONFIG_TIMERS])
{
    if (!token || !token->identity || !token->radio_generation || !timers) return ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < ESP32_MQUICKJS_SMARTCONFIG_TIMERS; ++i) {
        if (!timers[i]) return ESP_ERR_INVALID_ARG;
        for (unsigned j = 0; j < i; ++j) if (timers[i] == timers[j]) return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    bool allowed = !s_sc_timers && !s_sc_timers_reserving;
    if (allowed) s_sc_timers_reserving = true;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    if (!allowed) return ESP_ERR_INVALID_STATE;
    smartconfig_timers_t *fresh = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    esp_err_t error = fresh ? ESP_OK : ESP_ERR_NO_MEM;
    if (fresh) {
        fresh->token = *token;
        for (unsigned i = 0; i < ESP32_MQUICKJS_SMARTCONFIG_TIMERS; ++i) {
            /* A foreign/pre-existing handle cannot become ours by address. */
            if (timers[i]->timer_arg) error = ESP_ERR_INVALID_STATE;
            fresh->entries[i].timer = timers[i];
        }
    }
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    if (error == ESP_OK) { s_sc_timers = fresh; fresh = NULL; }
    s_sc_timers_reserving = false;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    if (fresh) esp32_mquickjs_memory_payload_free(fresh);
    return error;
}

static void sc_timer_callback(void *argument)
{
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    smartconfig_timer_entry_t *entry = NULL;
    ETSTimerFunc *fn = NULL;
    void *original_argument = NULL;
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    if (identity && s_sc_timers && !s_sc_timers->closing)
        for (unsigned i = 0; i < ESP32_MQUICKJS_SMARTCONFIG_TIMERS; ++i) {
            smartconfig_timer_entry_t *candidate = &s_sc_timers->entries[i];
            if (candidate->identity != identity || !candidate->enabled) continue;
            entry = candidate;
            ++entry->callbacks;
            fn = entry->fn;
            original_argument = entry->argument;
            break;
        }
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    if (!entry) return;
    fn(original_argument);
    /* callbacks pins the registry across stop/delete and caller retirement. */
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    --entry->callbacks;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
}

bool esp32_mquickjs_wifi_smartconfig_timer_setfn(ETSTimer *timer, ETSTimerFunc *fn, void *argument)
{
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    smartconfig_timer_entry_t *entry = sc_timer_find(timer);
    if (!entry) { portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return false; }
    if (s_sc_timers->closing) { portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return true; }
    if (!fn || entry->busy || entry->callbacks || entry->delete_pending ||
        (entry->handle && (entry->fn != fn || entry->argument != argument))) {
        sc_timer_failed(ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_SC_TIMER_ADMISSION);
        portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return true;
    }
    if (entry->handle) { portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return true; }
    if (s_sc_timer_identity == UINT32_MAX) {
        sc_timer_failed(ESP_ERR_NO_MEM, ESP32_MQUICKJS_SC_TIMER_CREATE);
        portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return true;
    }
    entry->identity = ++s_sc_timer_identity;
    entry->fn = fn;
    entry->argument = argument;
    entry->busy = true;
    uint32_t identity = entry->identity;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    esp_timer_handle_t handle = NULL;
    const esp_timer_create_args_t args = {.callback = sc_timer_callback,
        .arg = (void *)(uintptr_t)identity, .dispatch_method = ESP_TIMER_TASK, .name = "qjs_sc"};
    esp_err_t error = esp_timer_create(&args, &handle);
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    entry->handle = handle;
    if (error == ESP_OK) {
        memset(timer, 0, sizeof(*timer));
        timer->timer_expire = UINT32_C(0x12121212);
        timer->timer_arg = handle;
    } else {
        entry->identity = 0;
        sc_timer_failed(error, ESP32_MQUICKJS_SC_TIMER_CREATE);
    }
    entry->busy = false;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    return true;
}

bool IRAM_ATTR esp32_mquickjs_wifi_smartconfig_timer_arm(ETSTimer *timer, uint64_t us, bool repeat)
{
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    smartconfig_timer_entry_t *entry = sc_timer_find(timer);
    if (!entry) { portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return false; }
    if (s_sc_timers->closing) { portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return true; }
    if (entry->busy || entry->delete_pending || !entry->handle || !entry->identity || !us) {
        sc_timer_failed(ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_SC_TIMER_START);
        portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return true;
    }
    entry->busy = true;
    entry->enabled = true; /* An immediate callback may run before start returns. */
    esp_timer_handle_t handle = entry->handle;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    esp_err_t error = esp_timer_stop(handle);
    esp32_mquickjs_wifi_smartconfig_timer_stage_t stage = ESP32_MQUICKJS_SC_TIMER_STOP;
    if (error == ESP_OK || error == ESP_ERR_INVALID_STATE) {
        stage = ESP32_MQUICKJS_SC_TIMER_START;
        error = repeat ? esp_timer_start_periodic(handle, us) : esp_timer_start_once(handle, us);
    }
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    if (error != ESP_OK) { entry->enabled = false; sc_timer_failed(error, stage); }
    entry->busy = false;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    return true;
}

bool IRAM_ATTR esp32_mquickjs_wifi_smartconfig_timer_disarm(ETSTimer *timer)
{
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    smartconfig_timer_entry_t *entry = sc_timer_find(timer);
    if (!entry) { portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return false; }
    entry->enabled = false;
    if (entry->stopped) { portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return true; }
    if (entry->busy) {
        sc_timer_failed(ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_SC_TIMER_STOP);
        portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return true;
    }
    entry->busy = true;
    esp_timer_handle_t handle = entry->handle;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    esp_err_t error = handle ? esp_timer_stop(handle) : ESP_OK;
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    if (error != ESP_ERR_INVALID_STATE) sc_timer_failed(error, ESP32_MQUICKJS_SC_TIMER_STOP);
    entry->busy = false;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    return true;
}

bool esp32_mquickjs_wifi_smartconfig_timer_done(ETSTimer *timer)
{
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    smartconfig_timer_entry_t *entry = sc_timer_find(timer);
    if (!entry) { portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return false; }
    entry->enabled = false;
    if (entry->busy) {
        sc_timer_failed(ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_SC_TIMER_STOP);
        portEXIT_CRITICAL_SAFE(&s_sc_timers_lock); return true;
    }
    entry->busy = true;
    esp_timer_handle_t handle = entry->handle;
    bool stopped = entry->stopped;
    entry->delete_pending = true;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    esp_err_t error = ESP_OK;
    esp32_mquickjs_wifi_smartconfig_timer_stage_t stage = ESP32_MQUICKJS_SC_TIMER_STOP;
    if (handle) {
        error = stopped ? ESP_OK : esp_timer_stop_blocking(handle, 1);
        if (error == ESP_OK) {
            portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
            entry->stopped = true;
            portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
        }
        if (error == ESP_OK) { stage = ESP32_MQUICKJS_SC_TIMER_DELETE; error = esp_timer_delete(handle); }
    }
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    if (error == ESP_OK) {
        entry->handle = NULL;
        entry->identity = 0;
        entry->fn = NULL;
        entry->argument = NULL;
        entry->delete_pending = entry->stopped = false;
        timer->timer_arg = NULL;
        timer->timer_expire = 0;
    } else sc_timer_failed(error, stage);
    entry->busy = false;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    return true;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_timers_status(
    const esp32_mquickjs_wifi_smartconfig_token_t *token,
    esp32_mquickjs_wifi_smartconfig_timer_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    bool exact = sc_timers_exact(token);
    if (exact) {
        *status = (esp32_mquickjs_wifi_smartconfig_timer_status_t){.error = s_sc_timers->error,
            .cleanup_error = s_sc_timers->cleanup_error, .stage = s_sc_timers->stage,
            .closing = s_sc_timers->closing, .reserved_bytes = sizeof(*s_sc_timers),
            .busy = s_sc_timers->cleanup_busy};
        for (unsigned i = 0; i < ESP32_MQUICKJS_SMARTCONFIG_TIMERS; ++i) {
            status->handles += s_sc_timers->entries[i].handle != NULL;
            status->callbacks += s_sc_timers->entries[i].callbacks;
            status->busy += s_sc_timers->entries[i].busy;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_timers_close(const esp32_mquickjs_wifi_smartconfig_token_t *token)
{
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    bool exact = sc_timers_exact(token);
    if (exact) {
        s_sc_timers->closing = true;
        for (unsigned i = 0; i < ESP32_MQUICKJS_SMARTCONFIG_TIMERS; ++i)
            s_sc_timers->entries[i].enabled = false;
    }
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_timers_cleanup(const esp32_mquickjs_wifi_smartconfig_token_t *token)
{
    ETSTimer *timers[ESP32_MQUICKJS_SMARTCONFIG_TIMERS];
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    bool exact = sc_timers_exact(token) && s_sc_timers->closing && !s_sc_timers->cleanup_busy;
    if (exact) s_sc_timers->cleanup_busy = true;
    if (exact) for (unsigned i = 0; i < ESP32_MQUICKJS_SMARTCONFIG_TIMERS; ++i)
        timers[i] = s_sc_timers->entries[i].timer;
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    if (!exact) return ESP_ERR_INVALID_STATE;
    /* cleanup_busy pins the exact registry across every SDK call in the loop. */
    for (unsigned i = 0; i < ESP32_MQUICKJS_SMARTCONFIG_TIMERS; ++i)
        (void)esp32_mquickjs_wifi_smartconfig_timer_done(timers[i]);
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    esp_err_t error = sc_timers_exact(token) ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (error == ESP_OK) {
        for (unsigned i = 0; i < ESP32_MQUICKJS_SMARTCONFIG_TIMERS; ++i)
            if (s_sc_timers->entries[i].handle || s_sc_timers->entries[i].callbacks || s_sc_timers->entries[i].busy)
                error = s_sc_timers->cleanup_error ? s_sc_timers->cleanup_error : ESP_ERR_NOT_FINISHED;
        if (error == ESP_OK) s_sc_timers->cleanup_error = ESP_OK;
        s_sc_timers->cleanup_busy = false;
    }
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_timers_release(const esp32_mquickjs_wifi_smartconfig_token_t *token)
{
    smartconfig_timers_t *retired = NULL;
    portENTER_CRITICAL_SAFE(&s_sc_timers_lock);
    bool ready = sc_timers_exact(token) && s_sc_timers->closing && !s_sc_timers->cleanup_busy;
    if (ready) for (unsigned i = 0; i < ESP32_MQUICKJS_SMARTCONFIG_TIMERS; ++i)
        if (s_sc_timers->entries[i].handle || s_sc_timers->entries[i].callbacks || s_sc_timers->entries[i].busy)
            ready = false;
    if (ready) { retired = s_sc_timers; s_sc_timers = NULL; }
    portEXIT_CRITICAL_SAFE(&s_sc_timers_lock);
    if (!retired) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_memory_payload_free(retired);
    return ESP_OK;
}
#endif
