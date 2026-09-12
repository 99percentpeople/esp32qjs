#include "esp32_mquickjs_wifi_twt_setup_timer.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "rom/ets_sys.h"
#include <stddef.h>
#include <string.h>

extern uint8_t setup_timer_param[372];
void itwt_setup_timeout_fn(void *argument);
void itwt_setup_dwell_timeout_fn(void *argument);
int ieee80211_timer_process(int signal, int operation, void *argument);
void __real_itwt_setup_timeout_fn_process(void *argument);
void __real_itwt_setup_dwell_timeout_fn_process(void *argument);
typedef struct {
    esp32_mquickjs_wifi_twt_setup_timer_identity_t native;
    uint32_t identity;
    esp_err_t fault, cleanup_error;
    uint8_t phase, stage;
    /* These flags share the same lock; retain the existing 24-byte entry. */
    bool active : 1, fired : 1, cancel_stopped : 1;
} twt_setup_timer_entry_t;
static DRAM_ATTR struct {
    twt_setup_timer_entry_t *entries;
    esp32_mquickjs_wifi_twt_setup_timer_snapshot_t snapshot;
} s_setup_timer;
static DRAM_ATTR portMUX_TYPE s_setup_timer_lock = portMUX_INITIALIZER_UNLOCKED;
_Static_assert(sizeof(ETSTimer) == 20 && offsetof(ETSTimer, timer_arg) == 16 &&
    sizeof(twt_setup_timer_entry_t) == 24, "reviewed C5 setup timer layout");

static int IRAM_ATTR setup_timer_index(const void *timer)
{
    uintptr_t start = (uintptr_t)setup_timer_param + 144U;
    uintptr_t value = (uintptr_t)timer;
    return value >= start && value < start + 160U && (value - start) % 20U == 0U
        ? (int)((value - start) / 20U) : -1;
}
static void IRAM_ATTR setup_timer_changed_locked(void)
{
    if (s_setup_timer.snapshot.revision != UINT32_MAX) ++s_setup_timer.snapshot.revision;
    else if (s_setup_timer.snapshot.fault == ESP_OK) {
        s_setup_timer.snapshot.fault = ESP_ERR_NO_MEM;
        s_setup_timer.snapshot.fault_stage = ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_IDENTITY;
        s_setup_timer.snapshot.fault_slot = UINT8_MAX;
    }
}
static void IRAM_ATTR setup_timer_failed_locked(unsigned slot, esp_err_t error,
    esp32_mquickjs_wifi_twt_setup_timer_stage_t stage)
{
    if (s_setup_timer.snapshot.fault == ESP_OK) {
        s_setup_timer.snapshot.fault = error;
        s_setup_timer.snapshot.fault_stage = stage;
        s_setup_timer.snapshot.fault_slot = slot;
    }
    if (s_setup_timer.entries != NULL) {
        twt_setup_timer_entry_t *entry = &s_setup_timer.entries[slot];
        entry->active = false;
        if (entry->fault == ESP_OK) { entry->fault = error; entry->stage = stage; }
        if (stage == ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_STOP || stage == ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_DELETE)
            entry->cleanup_error = error;
    }
    setup_timer_changed_locked();
}
static void IRAM_ATTR setup_timer_failed(unsigned slot, esp_err_t error,
    esp32_mquickjs_wifi_twt_setup_timer_stage_t stage)
{
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    setup_timer_failed_locked(slot, error, stage);
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
}
static bool setup_timer_allocate(void)
{
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    bool present = s_setup_timer.entries != NULL;
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    if (present) return true;
    /* Boot-retained bounded ledger, like the existing TX ledger. Timer callback
     * arguments are numeric; no per-callback heap owner or runtime is retained. */
    twt_setup_timer_entry_t *fresh = esp32_mquickjs_memory_wireless_calloc("wifi", 8, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (fresh == NULL) return false;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    if (s_setup_timer.entries == NULL) {
        s_setup_timer.entries = fresh;
        s_setup_timer.snapshot.reserved_bytes = 8U * sizeof(*fresh);
        fresh = NULL;
    }
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    if (fresh != NULL) esp32_mquickjs_memory_payload_free(fresh);
    return true;
}
static bool setup_tx_identity_native(uint32_t identity, uintptr_t node, uint8_t dialog,
    uint8_t flow, uint32_t *out)
{
    unsigned slot = 8;
    esp32_mquickjs_wifi_twt_setup_timer_identity_t native = {0};
    uint32_t found = 0;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    if (s_setup_timer.entries != NULL && s_setup_timer.snapshot.fault == ESP_OK) {
        for (unsigned i = 0; i < 8; ++i) {
            const twt_setup_timer_entry_t *entry = &s_setup_timer.entries[i];
            if (entry->identity != 0U && (identity == 0U || entry->identity == identity) &&
                (node == 0U || entry->native.node == node) && entry->native.dialog == dialog &&
                entry->native.flow == flow && entry->phase == 26 && entry->active &&
                !entry->fired && entry->fault == ESP_OK) {
                if (slot != 8) { slot = 8; break; }
                slot = i; native = entry->native; found = entry->identity;
            }
        }
    }
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    /* Native table mutation and the original TX callback share the Wi-Fi
     * task. No SDK/native calls while holding the timer or TX ledger lock. */
    if (slot == 8 || !esp32_mquickjs_wifi_twt_sdk_setup_tx_matches_native(slot, &native)) return false;
    *out = found;
    return true;
}
bool esp32_mquickjs_wifi_twt_setup_tx_capture_native(uintptr_t node, uint8_t dialog,
    uint8_t flow, uint32_t *identity)
{
    return node != 0U && identity != NULL && *identity == 0U &&
        setup_tx_identity_native(0, node, dialog, flow, identity);
}
bool esp32_mquickjs_wifi_twt_setup_tx_matches_native(uint32_t identity, uint8_t dialog, uint8_t flow)
{
    uint32_t found;
    return identity != 0U && setup_tx_identity_native(identity, 0, dialog, flow, &found);
}
static void setup_timer_callback(void *argument)
{
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    unsigned slot = 8;
    uint8_t phase = 0;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    if (identity != 0U && s_setup_timer.entries != NULL) {
        for (unsigned i = 0; i < 8; ++i) {
            twt_setup_timer_entry_t *entry = &s_setup_timer.entries[i];
            if (entry->identity == identity && entry->active && !entry->fired && entry->fault == ESP_OK) {
                slot = i; phase = entry->phase; entry->fired = true;
                setup_timer_changed_locked(); break;
            }
        }
    }
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    if (slot == 8) return;
    int error = ieee80211_timer_process(7, phase, argument);
    if (error != 0) {
        portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
        twt_setup_timer_entry_t *entry = &s_setup_timer.entries[slot];
        /* The synchronous dispatch may already have consumed/replaced this
         * timer. A late post error must not fault that successor. */
        if (entry->identity == identity && entry->active)
            setup_timer_failed_locked(slot, error, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_POST);
        portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    }
}
esp_err_t esp32_mquickjs_wifi_twt_setup_timer_cancel_native(int16_t request_id, uint8_t pending_mask)
{
    if (request_id < 0) return ESP_ERR_INVALID_ARG;
    uint8_t owned = 0;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    for (unsigned i = 0; i < 8U; ++i) {
        twt_setup_timer_entry_t *entry = s_setup_timer.entries != NULL ? &s_setup_timer.entries[i] : NULL;
        ETSTimer *timer = (ETSTimer *)(setup_timer_param + 144U + 20U * i);
        bool exact = entry != NULL && entry->identity != 0U && entry->native.request_id == request_id;
        if (exact) owned |= (uint8_t)(1U << i);
        else if ((pending_mask & (1U << i)) && (timer->timer_arg != NULL || (entry != NULL && entry->active))) {
            portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
            return ESP_ERR_INVALID_STATE; /* Never invalidate a foreign slot. */
        }
    }
    /* Revoke every matching numeric callback before any driver operation.
     * A timer already queued on ESP_TIMER_TASK will fail its active check. */
    for (unsigned i = 0; i < 8U; ++i)
        if (owned & (1U << i)) s_setup_timer.entries[i].active = false;
    if (owned != 0U) setup_timer_changed_locked();
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    for (unsigned i = 0; i < 8U; ++i) {
        if (!(owned & (1U << i))) continue;
        twt_setup_timer_entry_t *entry = &s_setup_timer.entries[i];
        ETSTimer *timer = (ETSTimer *)(setup_timer_param + 144U + 20U * i);
        if (timer->timer_arg == NULL) continue;
        if (!entry->cancel_stopped) {
            esp_err_t error = esp_timer_stop(timer->timer_arg);
            if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
                setup_timer_failed(i, error, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_STOP);
                return error;
            }
            portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
            entry->cancel_stopped = true;
            portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
        }
        esp_err_t error = esp_timer_delete(timer->timer_arg);
        if (error != ESP_OK) {
            setup_timer_failed(i, error, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_DELETE);
            return error;
        }
        timer->timer_arg = NULL; timer->timer_expire = 0;
        portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
        entry->cleanup_error = ESP_OK;
        setup_timer_changed_locked();
        portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    }
    return ESP_OK;
}
void esp32_mquickjs_wifi_twt_setup_timers_connection_closed_native(void)
{
    int16_t requests[8];
    unsigned count = 0;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    if (s_setup_timer.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i) {
            twt_setup_timer_entry_t *entry = &s_setup_timer.entries[i];
            if (!entry->identity) continue;
            entry->active = false;
            requests[count++] = entry->native.request_id;
        }
    if (count) setup_timer_changed_locked();
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    for (unsigned i = 0; i < count; ++i)
        (void)esp32_mquickjs_wifi_twt_setup_timer_cancel_native(requests[i], 0);
}
esp_err_t esp32_mquickjs_wifi_twt_setup_timer_quiescent_native(int16_t request_id, uint32_t *revision)
{
    if (request_id < 0 || revision == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t error = ESP_OK;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    if (s_setup_timer.snapshot.revision == UINT32_MAX) error = ESP_ERR_NO_MEM;
    for (unsigned i = 0; error == ESP_OK && i < 8U; ++i) {
        const twt_setup_timer_entry_t *entry = s_setup_timer.entries != NULL ? &s_setup_timer.entries[i] : NULL;
        if (entry == NULL || entry->identity == 0U || entry->native.request_id != request_id) continue;
        const ETSTimer *timer = (const ETSTimer *)(setup_timer_param + 144U + 20U * i);
        if (entry->active || timer->timer_arg != NULL || entry->cleanup_error != ESP_OK) error = ESP_ERR_NOT_FINISHED;
    }
    if (error == ESP_OK) *revision = s_setup_timer.snapshot.revision;
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    return error;
}
bool esp32_mquickjs_wifi_twt_setup_timer_setfn(void *timer, void *callback, void *argument)
{
    int slot = setup_timer_index(timer);
    if (slot < 0) return false;
    uint8_t phase = callback == (void *)itwt_setup_timeout_fn ? 26 :
        callback == (void *)itwt_setup_dwell_timeout_fn ? 27 : 0;
    esp32_mquickjs_wifi_twt_setup_timer_identity_t native;
    if (phase == 0 || !esp32_mquickjs_wifi_twt_sdk_setup_timer_capture_native(slot, phase, argument, &native)) {
        setup_timer_failed(slot, ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_ARGUMENT);
        return true;
    }
    if (!setup_timer_allocate()) {
        setup_timer_failed(slot, ESP_ERR_NO_MEM, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_ALLOCATE);
        return true;
    }
    ETSTimer *legacy = timer;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    twt_setup_timer_entry_t *entry = &s_setup_timer.entries[slot];
    entry->active = false;
    esp_err_t error = s_setup_timer.snapshot.fault;
    if (error == ESP_OK && legacy->timer_arg != NULL) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK && (s_setup_timer.snapshot.last_identity == UINT32_MAX ||
        s_setup_timer.snapshot.revision == UINT32_MAX)) error = ESP_ERR_NO_MEM;
    uint32_t identity = 0;
    if (error == ESP_OK) {
        identity = ++s_setup_timer.snapshot.last_identity;
        *entry = (twt_setup_timer_entry_t){.native = native, .identity = identity, .phase = phase, .active = true};
        setup_timer_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    if (error != ESP_OK) {
        setup_timer_failed(slot, error, error == ESP_ERR_NO_MEM ? ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_IDENTITY :
            ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_ARGUMENT);
        return true;
    }
    const esp_timer_create_args_t args = {.callback = setup_timer_callback, .arg = (void *)(uintptr_t)identity,
        .dispatch_method = ESP_TIMER_TASK, .name = "iTWT setup"};
    esp_timer_handle_t handle = NULL;
    error = esp_timer_create(&args, &handle);
    if (error != ESP_OK) {
        setup_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_CREATE);
        return true;
    }
    memset(legacy, 0, sizeof(*legacy));
    legacy->timer_expire = UINT32_C(0x12121212);
    legacy->timer_arg = handle;
    return true;
}
bool IRAM_ATTR esp32_mquickjs_wifi_twt_setup_timer_disarm(void *timer)
{
    int slot = setup_timer_index(timer);
    if (slot < 0) return false;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    bool known = s_setup_timer.entries != NULL && s_setup_timer.entries[slot].identity != 0U;
    if (known) { s_setup_timer.entries[slot].active = false; setup_timer_changed_locked(); }
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    esp_timer_handle_t handle = ((ETSTimer *)timer)->timer_arg;
    if (handle == NULL) return true;
    esp_err_t error = known ? esp_timer_stop(handle) : ESP_ERR_INVALID_STATE;
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE)
        setup_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_STOP);
    else if (!known) setup_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_ARGUMENT);
    return true;
}
bool esp32_mquickjs_wifi_twt_setup_timer_done(void *timer)
{
    int slot = setup_timer_index(timer);
    if (slot < 0) return false;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    bool known = s_setup_timer.entries != NULL && s_setup_timer.entries[slot].identity != 0U;
    if (known) { s_setup_timer.entries[slot].active = false; setup_timer_changed_locked(); }
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    ETSTimer *legacy = timer;
    if (legacy->timer_arg == NULL) return true;
    esp_err_t error = known ? esp_timer_delete(legacy->timer_arg) : ESP_ERR_INVALID_STATE;
    if (error != ESP_OK) {
        setup_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_DELETE);
        return true;
    }
    legacy->timer_arg = NULL; legacy->timer_expire = 0;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    s_setup_timer.entries[slot].cleanup_error = ESP_OK;
    setup_timer_changed_locked();
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    return true;
}
bool IRAM_ATTR esp32_mquickjs_wifi_twt_setup_timer_arm(void *timer, uint64_t us, bool repeat)
{
    int slot = setup_timer_index(timer);
    if (slot < 0) return false;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    bool valid = s_setup_timer.entries != NULL && s_setup_timer.entries[slot].active &&
        !s_setup_timer.entries[slot].fired && s_setup_timer.entries[slot].fault == ESP_OK;
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    esp_timer_handle_t handle = ((ETSTimer *)timer)->timer_arg;
    if (!valid || handle == NULL || repeat) {
        setup_timer_failed(slot, repeat ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_START);
        return true;
    }
    esp_err_t error = esp_timer_stop(handle);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        setup_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_STOP);
        return true;
    }
    error = esp_timer_start_once(handle, us);
    if (error != ESP_OK) setup_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_START);
    return true;
}
static void setup_timer_process(void *argument, uint8_t phase)
{
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    unsigned slot = 8;
    esp32_mquickjs_wifi_twt_setup_timer_identity_t native = {0};
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    if (identity != 0U && s_setup_timer.entries != NULL) {
        for (unsigned i = 0; i < 8; ++i) {
            twt_setup_timer_entry_t *entry = &s_setup_timer.entries[i];
            if (entry->identity == identity && entry->active && entry->fired && entry->phase == phase && entry->fault == ESP_OK) {
                slot = i; native = entry->native; entry->active = false;
                setup_timer_changed_locked(); break;
            }
        }
    }
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    if (slot == 8) return;
    if (!esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(slot, phase, &native)) {
        setup_timer_failed(slot, ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_NATIVE);
        return;
    }
    /* Both reviewed handlers use only this byte synchronously. No address in
     * reusable SDK request storage is carried by either asynchronous queue. */
    uint8_t dialog = native.dialog;
    if (phase == 26) __real_itwt_setup_timeout_fn_process(&dialog);
    else __real_itwt_setup_dwell_timeout_fn_process(&dialog);
}
void __wrap_itwt_setup_timeout_fn_process(void *argument)
{
    setup_timer_process(argument, 26);
}
void __wrap_itwt_setup_dwell_timeout_fn_process(void *argument)
{
    setup_timer_process(argument, 27);
}
esp_err_t esp32_mquickjs_wifi_twt_setup_timer_error(void)
{
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    esp_err_t error = s_setup_timer.snapshot.fault;
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
    return error;
}
void esp32_mquickjs_wifi_twt_setup_timer_snapshot(esp32_mquickjs_wifi_twt_setup_timer_snapshot_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL_SAFE(&s_setup_timer_lock);
    *out = s_setup_timer.snapshot;
    if (s_setup_timer.entries != NULL) {
        for (unsigned i = 0; i < 8; ++i) {
            const twt_setup_timer_entry_t *entry = &s_setup_timer.entries[i];
            if (entry->active) out->active_mask |= (uint8_t)(1U << i);
            if (entry->fired) out->fired_mask |= (uint8_t)(1U << i);
            if (entry->fault != ESP_OK) out->fault_mask |= (uint8_t)(1U << i);
            if (entry->cleanup_error != ESP_OK) out->cleanup_mask |= (uint8_t)(1U << i);
        }
    }
    portEXIT_CRITICAL_SAFE(&s_setup_timer_lock);
}
#endif
