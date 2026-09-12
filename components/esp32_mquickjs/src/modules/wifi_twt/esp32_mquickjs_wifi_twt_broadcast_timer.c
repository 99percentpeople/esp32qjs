#include "esp32_mquickjs_wifi_twt_broadcast_timer.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp32_mquickjs_wifi_twt_broadcast_event.h"
#include "esp32_mquickjs_wifi_twt_tx.h"
#include "esp32_mquickjs_wifi_twt_teardown_tx.h"
#include "esp32_mquickjs_wifi_twt_sdk.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "rom/ets_sys.h"
#include <stddef.h>
#include <string.h>

ESP_EVENT_DECLARE_BASE(ESP32QJS_WIFI_RADIO_CONTROL_EVENT);
extern uint8_t btwt_setup_timer[1248];
void btwt_setup_timeout_fn(void *);
void btwt_setup_dwell_timeout_fn(void *);
void __real_btwt_setup_timeout_fn_process(void *);
void __real_btwt_setup_dwell_timeout_fn_process(void *);
void __real_ieee80211_close_all_twt_sessions(void);
int ieee80211_process_btwt_setup_action(void *node, const uint8_t *body);
int ieee80211_timer_process(int signal, int operation, void *argument);
typedef struct {
    uintptr_t node;
    uint32_t identity;
    esp_err_t fault, cleanup_error;
    uint8_t parameter[17], phase, stage;
    bool active : 1, fired : 1, busy : 1, stopped : 1;
    uint8_t dialog; /* Echoed RX token, valid only during the response phase. */
    esp32_mquickjs_wifi_btwt_timer_result_t result;
} btwt_timer_entry_t;
static DRAM_ATTR struct {
    btwt_timer_entry_t *entries;
    esp32_mquickjs_wifi_btwt_timer_snapshot_t snapshot;
} s_btwt_timer;
static DRAM_ATTR portMUX_TYPE s_btwt_timer_lock = portMUX_INITIALIZER_UNLOCKED;
_Static_assert(sizeof(ETSTimer) == 20 && offsetof(ETSTimer, timer_arg) == 16 &&
    sizeof(esp32_mquickjs_wifi_btwt_timer_result_t) == 64 && sizeof(btwt_timer_entry_t) == 104, "reviewed C5 broadcast timer layout");

static int IRAM_ATTR btwt_timer_index(const void *timer)
{
    uintptr_t start = (uintptr_t)btwt_setup_timer, address = (uintptr_t)timer;
    return address >= start && address < start + 640U && (address - start) % 20U == 0U ?
        (int)((address - start) / 20U) : -1;
}
static void IRAM_ATTR btwt_timer_changed_locked(void)
{
    if (s_btwt_timer.snapshot.revision != UINT32_MAX) ++s_btwt_timer.snapshot.revision;
    else if (s_btwt_timer.snapshot.fault == ESP_OK) {
        s_btwt_timer.snapshot.fault = ESP_ERR_NO_MEM;
        s_btwt_timer.snapshot.fault_stage = ESP32_MQUICKJS_WIFI_BTWT_TIMER_IDENTITY;
        s_btwt_timer.snapshot.fault_slot = UINT8_MAX;
    }
}
static void IRAM_ATTR btwt_timer_failed_locked(unsigned slot, esp_err_t error, esp32_mquickjs_wifi_btwt_timer_stage_t stage)
{
    if (s_btwt_timer.snapshot.fault == ESP_OK) {
        s_btwt_timer.snapshot.fault = error;
        s_btwt_timer.snapshot.fault_slot = slot;
        s_btwt_timer.snapshot.fault_stage = stage;
    }
    if (s_btwt_timer.entries != NULL) {
        btwt_timer_entry_t *entry = &s_btwt_timer.entries[slot];
        entry->active = false;
        if (entry->fault == ESP_OK) { entry->fault = error; entry->stage = stage; }
        if (stage == ESP32_MQUICKJS_WIFI_BTWT_TIMER_STOP || stage == ESP32_MQUICKJS_WIFI_BTWT_TIMER_DELETE)
            entry->cleanup_error = error;
    }
    btwt_timer_changed_locked();
}
static void IRAM_ATTR btwt_timer_failed(unsigned slot, esp_err_t error, esp32_mquickjs_wifi_btwt_timer_stage_t stage)
{
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_failed_locked(slot, error, stage);
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
}
static bool btwt_timer_allocate(void)
{
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    bool present = s_btwt_timer.entries != NULL;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (present) return true;
    btwt_timer_entry_t *fresh = esp32_mquickjs_memory_wireless_calloc("wifi", 32, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (fresh == NULL) return false;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (s_btwt_timer.entries == NULL) {
        s_btwt_timer.entries = fresh;
        s_btwt_timer.snapshot.reserved_bytes = 32U * sizeof(*fresh);
        fresh = NULL;
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (fresh != NULL) esp32_mquickjs_memory_payload_free(fresh);
    return true;
}
esp_err_t esp32_mquickjs_wifi_btwt_setup_begin_native(unsigned slot, uint32_t identity,
    uintptr_t node, const uint8_t parameter[17])
{
    if (slot >= 32 || identity == 0 || node == 0 || parameter == NULL || (parameter[10] >> 3) != slot)
        return ESP_ERR_INVALID_ARG;
    esp_err_t error = esp32_mquickjs_wifi_btwt_timer_available_native(slot);
    if (error != ESP_OK) return error;
    if (!btwt_timer_allocate()) return ESP_ERR_NO_MEM;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = &s_btwt_timer.entries[slot];
    error = s_btwt_timer.snapshot.fault;
    if (error == ESP_OK && s_btwt_timer.snapshot.revision == UINT32_MAX) error = ESP_ERR_NO_MEM;
    if (error == ESP_OK && (entry->active || entry->busy || entry->result.tx_busy || entry->result.publishing || entry->result.fence_pending || entry->result.held ||
        (entry->result.identity != 0 && identity <= entry->result.identity))) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK) {
        *entry = (btwt_timer_entry_t){.node = node, .result = {.identity = identity}};
        memcpy(entry->parameter, parameter, sizeof(entry->parameter));
        btwt_timer_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    return error;
}
esp_err_t esp32_mquickjs_wifi_btwt_setup_hold_native(unsigned slot, uint32_t identity)
{
    if (slot >= 32 || identity == 0) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    bool exact = entry != NULL && entry->result.identity == identity && !entry->result.held &&
        entry->phase == 0 && !entry->result.tx_busy && !entry->result.publishing && !entry->result.complete;
    esp_err_t error = s_btwt_timer.snapshot.fault;
    if (error == ESP_OK && s_btwt_timer.snapshot.revision == UINT32_MAX) error = ESP_ERR_NO_MEM;
    if (error == ESP_OK && !exact) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK) { entry->result.held = true; btwt_timer_changed_locked(); }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    return error;
}
bool esp32_mquickjs_wifi_btwt_setup_held(unsigned slot)
{
    if (slot >= 32) return false;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    bool held = s_btwt_timer.entries != NULL && s_btwt_timer.entries[slot].result.held;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    return held;
}
bool esp32_mquickjs_wifi_btwt_setup_owner_native(unsigned slot, uint32_t identity, uintptr_t *node)
{
    if (slot >= 32 || !identity || node == NULL) return false;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    const btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    bool exact = entry != NULL && entry->result.identity == identity && entry->result.held &&
        entry->result.complete && entry->result.seen && entry->result.event.status == BTWT_SETUP_SUCCESS &&
        !entry->result.ambiguous && !entry->result.native_closed && !entry->result.cancel_requested &&
        !entry->active && !entry->busy && !entry->result.tx_busy && !entry->result.publishing &&
        entry->fault == ESP_OK && entry->result.native_error == ESP_OK;
    uintptr_t current = exact ? entry->node : 0;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (!exact || !esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native(current) ||
        !esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(slot)) return false;
    *node = current;
    return true;
}
void esp32_mquickjs_wifi_btwt_setup_submitted_native(unsigned slot, uint32_t identity, esp_err_t error)
{
    if (slot >= 32 || identity == 0) return;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    if (entry != NULL && entry->result.identity == identity) {
        entry->result.submit_error = error;
        if (error != ESP_OK) entry->result.complete = true;
        /* An output error cannot undo a synchronous callback, nor prove that
         * a response timer or an RF request was never created. */
        btwt_timer_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
}
static void btwt_timer_callback(void *argument)
{
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    unsigned slot = 32;
    uint8_t phase = 0;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (identity != 0U && s_btwt_timer.entries != NULL) {
        for (unsigned i = 0; i < 32; ++i) {
            btwt_timer_entry_t *entry = &s_btwt_timer.entries[i];
            if (entry->identity == identity && entry->active && !entry->fired && entry->fault == ESP_OK) {
                slot = i; phase = entry->phase; entry->fired = true;
                btwt_timer_changed_locked(); break;
            }
        }
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (slot == 32) return;
    int error = ieee80211_timer_process(7, phase, argument);
    if (error != 0) {
        portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
        btwt_timer_entry_t *entry = &s_btwt_timer.entries[slot];
        if (entry->identity == identity && entry->active)
            btwt_timer_failed_locked(slot, error, ESP32_MQUICKJS_WIFI_BTWT_TIMER_POST);
        portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    }
}
bool esp32_mquickjs_wifi_btwt_timer_setfn(void *timer, void *callback, void *argument)
{
    int slot = btwt_timer_index(timer);
    if (slot < 0) return false;
    uint8_t phase = callback == (void *)btwt_setup_timeout_fn ? 30 :
        callback == (void *)btwt_setup_dwell_timeout_fn ? 31 : 0;
    esp32_mquickjs_wifi_btwt_timer_identity_t native;
    if (!phase || !esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(slot, phase, argument, &native)) {
        btwt_timer_failed(slot, ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_WIFI_BTWT_TIMER_ARGUMENT); return true;
    }
    if (!btwt_timer_allocate()) {
        btwt_timer_failed(slot, ESP_ERR_NO_MEM, ESP32_MQUICKJS_WIFI_BTWT_TIMER_ALLOCATE); return true;
    }
    ETSTimer *legacy = timer;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = &s_btwt_timer.entries[slot];
    esp_err_t error = s_btwt_timer.snapshot.fault;
    if (error == ESP_OK && (entry->busy || entry->result.publishing || legacy->timer_arg != NULL)) error = ESP_ERR_INVALID_STATE;
    /* A tracked request can install its response timer once and replace it
     * with one dwell timer. Never attach an unrelated timer to its result. */
    if (error == ESP_OK && entry->result.identity != 0 &&
        (entry->node != native.node ||
         (phase == 30 && (entry->phase != 0 || memcmp(entry->parameter, native.parameter, 17))) ||
         (phase == 31 && (entry->phase != 30 || entry->active || entry->dialog != 0))))
        error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK && (s_btwt_timer.snapshot.last_identity == UINT32_MAX ||
        s_btwt_timer.snapshot.revision == UINT32_MAX)) error = ESP_ERR_NO_MEM;
    uint32_t identity = 0;
    if (error == ESP_OK) {
        identity = ++s_btwt_timer.snapshot.last_identity;
        esp32_mquickjs_wifi_btwt_timer_result_t result = entry->result.identity != 0 ?
            entry->result : (esp32_mquickjs_wifi_btwt_timer_result_t){0};
        result.timer_identity = identity;
        *entry = (btwt_timer_entry_t){.node = native.node, .identity = identity, .phase = phase, .active = true,
            .result = result};
        memcpy(entry->parameter, native.parameter, sizeof(entry->parameter));
        btwt_timer_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (error != ESP_OK) {
        btwt_timer_failed(slot, error, error == ESP_ERR_NO_MEM ? ESP32_MQUICKJS_WIFI_BTWT_TIMER_IDENTITY :
            ESP32_MQUICKJS_WIFI_BTWT_TIMER_ARGUMENT); return true;
    }
    const esp_timer_create_args_t args = {.callback = btwt_timer_callback, .arg = (void *)(uintptr_t)identity,
        .dispatch_method = ESP_TIMER_TASK, .name = "bTWT setup"};
    esp_timer_handle_t handle = NULL;
    error = esp_timer_create(&args, &handle);
    if (error != ESP_OK) {
        btwt_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_BTWT_TIMER_CREATE); return true;
    }
    memset(legacy, 0, sizeof(*legacy));
    legacy->timer_expire = UINT32_C(0x12121212); legacy->timer_arg = handle;
    return true;
}
bool IRAM_ATTR esp32_mquickjs_wifi_btwt_timer_disarm(void *timer)
{
    int slot = btwt_timer_index(timer);
    if (slot < 0) return false;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    bool known = entry != NULL && entry->identity != 0U;
    bool stopped = known && entry->stopped;
    if (known) { entry->active = false; btwt_timer_changed_locked(); }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    esp_timer_handle_t handle = ((ETSTimer *)timer)->timer_arg;
    if (handle == NULL || stopped) return true;
    if (!known) { btwt_timer_failed(slot, ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_WIFI_BTWT_TIMER_ARGUMENT); return true; }
    esp_err_t error = esp_timer_stop(handle);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        btwt_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_BTWT_TIMER_STOP); return true;
    }
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    entry->stopped = true; entry->cleanup_error = ESP_OK;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    return true;
}
bool esp32_mquickjs_wifi_btwt_timer_done(void *timer)
{
    int slot = btwt_timer_index(timer);
    if (slot < 0) return false;
    (void)esp32_mquickjs_wifi_btwt_timer_disarm(timer);
    ETSTimer *legacy = timer;
    if (legacy->timer_arg == NULL) return true;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    bool stopped = s_btwt_timer.entries != NULL && s_btwt_timer.entries[slot].stopped;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (!stopped) return true; /* Failed stop: never skip to delete. */
    esp_err_t error = esp_timer_delete(legacy->timer_arg);
    if (error != ESP_OK) { btwt_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_BTWT_TIMER_DELETE); return true; }
    legacy->timer_arg = NULL; legacy->timer_expire = 0;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    s_btwt_timer.entries[slot].cleanup_error = ESP_OK;
    btwt_timer_changed_locked();
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    return true;
}
bool IRAM_ATTR esp32_mquickjs_wifi_btwt_timer_arm(void *timer, uint64_t us, bool repeat)
{
    int slot = btwt_timer_index(timer);
    if (slot < 0) return false;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    bool valid = s_btwt_timer.entries != NULL && s_btwt_timer.entries[slot].active &&
        !s_btwt_timer.entries[slot].fired && !s_btwt_timer.entries[slot].busy && s_btwt_timer.entries[slot].fault == ESP_OK;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    esp_timer_handle_t handle = ((ETSTimer *)timer)->timer_arg;
    if (!valid || handle == NULL || repeat) {
        btwt_timer_failed(slot, repeat ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_WIFI_BTWT_TIMER_START); return true;
    }
    esp_err_t error = esp_timer_stop(handle);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        btwt_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_BTWT_TIMER_STOP); return true;
    }
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    s_btwt_timer.entries[slot].stopped = false;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    error = esp_timer_start_once(handle, us);
    if (error != ESP_OK) btwt_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_BTWT_TIMER_START);
    return true;
}
esp_err_t esp32_mquickjs_wifi_btwt_timer_cancel_native(unsigned slot, uint32_t identity)
{
    if (slot >= 32 || identity == 0) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    bool exact = entry != NULL && entry->identity == identity;
    bool busy = exact && (entry->busy || entry->result.tx_busy || entry->result.publishing);
    if (exact) { entry->active = false; btwt_timer_changed_locked(); }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (!exact) return ESP_ERR_INVALID_STATE;
    if (busy) return ESP_ERR_NOT_FINISHED;
    ETSTimer *timer = (ETSTimer *)(btwt_setup_timer + 20U * slot);
    (void)esp32_mquickjs_wifi_btwt_timer_done(timer);
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    esp_err_t error = entry->cleanup_error;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    return error != ESP_OK ? error : timer->timer_arg != NULL ? ESP_ERR_NOT_FINISHED : ESP_OK;
}
/* Native queue only. A teardown outcome and native buffer/PM retirement are
 * separate facts. A connection close revokes control and may make an absent
 * callback safe to retire, but it never bypasses TX/recycler/fault checks. */
static esp_err_t btwt_teardown_cut_native(unsigned slot, uint32_t identity, bool native_closed, uint32_t *revision)
{
    esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t state;
    esp32_mquickjs_wifi_twt_teardown_tx_snapshot(&state);
    if (!state.broadcast || state.identity != identity || state.flow != slot) {
        *revision = 0;
        return ESP_OK;
    }
    uint32_t current;
    esp_err_t error = esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(identity, &current);
    if (error != ESP_OK) return error;
    /* Quiescence may settle a retained PM reference. Read its resulting
     * revision/outcome; copied observation errors do not revoke completion. */
    esp32_mquickjs_wifi_twt_teardown_tx_snapshot(&state);
    if (!state.broadcast || state.identity != identity || state.flow != slot || state.revision != current)
        return ESP_ERR_NOT_FINISHED;
    if (!native_closed) {
        if (state.submit_error != ESP_OK) return state.submit_error;
        if (state.completion_error != ESP_OK) return state.completion_error;
        if (state.completion_ambiguous) return ESP_ERR_INVALID_STATE;
        if (!state.completion_seen) return ESP_ERR_NOT_FINISHED;
        if (state.completion_status != BTWT_TEARDOWN_SUCCESS) return ESP_FAIL;
    }
    *revision = current;
    return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_btwt_setup_cancel_native(unsigned slot, uint32_t identity)
{
    if (slot >= 32 || !identity) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    bool exact = entry != NULL && entry->result.identity == identity && entry->result.held;
    bool busy = exact && (entry->busy || entry->result.tx_busy || entry->result.publishing);
    uint32_t timer_identity = exact ? entry->identity : 0;
    bool native_closed = exact && entry->result.native_closed;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (!exact) return ESP_ERR_INVALID_STATE;
    if (busy) return ESP_ERR_NOT_FINISHED;
    /* Dwell writes the established bitmap before PM/table changes. Native
     * queue serialization and busy exclusion are both required here. Never
     * turn an established Agreement into a locally cancelled pending setup. */
    if (esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(slot)) return ESP_ERR_INVALID_STATE;
    uint32_t teardown_revision;
    esp_err_t teardown_error = btwt_teardown_cut_native(slot, identity, native_closed, &teardown_revision);
    if (teardown_error != ESP_OK) return teardown_error;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (!entry->result.cancel_requested) {
        entry->result.cancel_requested = true;
        entry->active = false; entry->dialog = 0;
        btwt_timer_changed_locked();
    }
    bool cancelled = entry->result.cancelled;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (cancelled) return ESP_OK;
    esp_err_t tx_error = esp32_mquickjs_wifi_twt_tx_broadcast_cancel_request_native(slot, identity);
    esp_err_t error = timer_identity ? esp32_mquickjs_wifi_btwt_timer_cancel_native(slot, timer_identity) : ESP_OK;
    if (error == ESP_OK) error = tx_error;
    if (error == ESP_OK) {
        portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
        entry->result.cancelled = true;
        entry->result.complete = true;
        btwt_timer_changed_locked();
        portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    }
    return error;
}
esp_err_t esp32_mquickjs_wifi_btwt_setup_quiescent_native(unsigned slot, uint32_t identity,
    esp32_mquickjs_wifi_btwt_cut_t *out)
{
    if (slot >= 32 || !identity || out == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    const btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    bool exact = entry != NULL && entry->result.identity == identity && entry->result.held;
    bool ready = exact && entry->result.cancelled && !entry->active && !entry->busy &&
        !entry->result.tx_busy && !entry->result.publishing && entry->cleanup_error == ESP_OK;
    uint32_t revision = s_btwt_timer.snapshot.revision;
    bool native_closed = exact && entry->result.native_closed;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (!exact) return ESP_ERR_INVALID_STATE;
    if (!ready) return ESP_ERR_NOT_FINISHED;
    if (revision == UINT32_MAX) return ESP_ERR_NO_MEM;
    const ETSTimer *timer = (const ETSTimer *)(btwt_setup_timer + 20U * slot);
    if (timer->timer_arg != NULL || esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(slot)) return ESP_ERR_NOT_FINISHED;
    esp32_mquickjs_wifi_btwt_cut_t cut = {.timer_revision = revision};
    esp_err_t error = btwt_teardown_cut_native(slot, identity, native_closed, &cut.teardown_revision);
    if (error != ESP_OK) return error;
    error = esp32_mquickjs_wifi_twt_tx_broadcast_quiescent_native(slot, identity, &cut.tx_revision);
    if (error == ESP_OK) *out = cut;
    return error;
}
esp_err_t esp32_mquickjs_wifi_btwt_setup_post_fence(unsigned slot, uint32_t identity,
    const esp32_mquickjs_wifi_btwt_cut_t *cut, uint32_t *sequence)
{
    if (slot >= 32 || !identity || cut == NULL || sequence == NULL) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_twt_tx_snapshot_t tx;
    esp32_mquickjs_wifi_twt_tx_snapshot(&tx);
    if (tx.fault != ESP32_MQUICKJS_WIFI_TWT_TX_OK || tx.revision != cut->tx_revision) return ESP_ERR_NOT_FINISHED;
    esp32_mquickjs_wifi_btwt_event_fence_t event = {.slot = slot, .identity = identity, .cut = *cut};
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    esp_err_t error = ESP_OK;
    if (entry == NULL || entry->result.identity != identity || !entry->result.held ||
        !entry->result.cancelled || entry->active || entry->busy || entry->result.tx_busy ||
        entry->result.publishing || entry->result.fence_pending || s_btwt_timer.snapshot.revision != cut->timer_revision)
        error = ESP_ERR_NOT_FINISHED;
    else if (cut->timer_revision == UINT32_MAX || entry->result.fence_sequence == UINT32_MAX) error = ESP_ERR_NO_MEM;
    else {
        event.sequence = ++entry->result.fence_sequence;
        entry->result.fence_pending = true;
        entry->result.fence_posted = entry->result.fence_seen = false;
        /* Fence bookkeeping does not change the native-state revision it
         * proves. Native/result mutations still invalidate cut independently. */
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (error != ESP_OK) return error;
    error = esp_event_post(ESP32QJS_WIFI_RADIO_CONTROL_EVENT, ESP32_MQUICKJS_WIFI_BTWT_FENCE_EVENT,
        &event, sizeof(event), 0);
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    bool exact = entry->result.identity == identity && entry->result.held && entry->result.fence_sequence == event.sequence;
    if (exact) {
        entry->result.fence_pending = false;
        if (error == ESP_OK && s_btwt_timer.snapshot.revision == cut->timer_revision) {
            entry->result.fence_posted = true;
            *sequence = event.sequence; /* Preserve an early handler's seen bit. */
        } else {
            entry->result.fence_posted = entry->result.fence_seen = false;
            if (error == ESP_OK) error = ESP_ERR_NOT_FINISHED;
        }
    } else if (error == ESP_OK) error = ESP_ERR_INVALID_STATE;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    return error;
}
void esp32_mquickjs_wifi_btwt_setup_observe_fence(const esp32_mquickjs_wifi_btwt_event_fence_t *event)
{
    if (event == NULL || event->slot >= 32 || !event->identity || !event->sequence) return;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[event->slot] : NULL;
    if (entry != NULL && entry->result.identity == event->identity && entry->result.held && entry->result.cancelled &&
        entry->result.fence_sequence == event->sequence && s_btwt_timer.snapshot.revision == event->cut.timer_revision &&
        (entry->result.fence_pending || entry->result.fence_posted)) entry->result.fence_seen = true;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
}
esp_err_t esp32_mquickjs_wifi_btwt_setup_release_native(unsigned slot, uint32_t identity,
    const esp32_mquickjs_wifi_btwt_cut_t *cut, uint32_t sequence)
{
    if (cut == NULL || !sequence) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_btwt_cut_t current;
    esp_err_t error = esp32_mquickjs_wifi_btwt_setup_quiescent_native(slot, identity, &current);
    if (error != ESP_OK) return error;
    if (current.tx_revision != cut->tx_revision || current.timer_revision != cut->timer_revision ||
        current.teardown_revision != cut->teardown_revision) return ESP_ERR_NOT_FINISHED;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = &s_btwt_timer.entries[slot];
    bool exact = entry->result.identity == identity && entry->result.held && entry->result.cancelled &&
        s_btwt_timer.snapshot.revision == cut->timer_revision && entry->result.fence_sequence == sequence &&
        !entry->result.fence_pending && entry->result.fence_posted && entry->result.fence_seen;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (!exact) return ESP_ERR_NOT_FINISHED;
    /* No driver calls under the timer lock. held + cancel_requested prevent
     * same-request admission while releasing the shared native TX scope. */
    if (cut->teardown_revision != 0 &&
        !esp32_mquickjs_wifi_twt_teardown_tx_release_native(identity, cut->teardown_revision)) return ESP_ERR_NOT_FINISHED;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    exact = entry->result.identity == identity && entry->result.held && entry->result.cancelled &&
        s_btwt_timer.snapshot.revision == cut->timer_revision && entry->result.fence_sequence == sequence &&
        !entry->result.fence_pending && entry->result.fence_posted && entry->result.fence_seen;
    if (exact) {
        entry->result.held = false;
        entry->result.fence_posted = entry->result.fence_seen = false;
        btwt_timer_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    return exact ? ESP_OK : ESP_ERR_NOT_FINISHED;
}
void esp32_mquickjs_wifi_btwt_timer_bind_response_native(uintptr_t node, uint8_t dialog, const uint8_t parameter[17])
{
    if (parameter == NULL) return;
    unsigned slot = parameter[10] >> 3;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    bool valid = dialog != 0 && entry != NULL && entry->node == node && entry->phase == 30 && entry->active &&
        !entry->busy && entry->dialog == 0 && entry->fault == ESP_OK &&
        !memcmp(entry->parameter, parameter, sizeof(entry->parameter));
    if (valid) { entry->dialog = dialog; btwt_timer_changed_locked(); }
    else btwt_timer_failed_locked(slot, ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_WIFI_BTWT_TIMER_NATIVE);
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
}
static void btwt_timer_publish_result(unsigned slot, uint32_t identity,
    const esp32_mquickjs_wifi_btwt_event_scope_t *scope, esp_err_t cleanup_error)
{
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = &s_btwt_timer.entries[slot];
    bool exact = entry->identity == identity;
    if (exact) {
        esp32_mquickjs_wifi_btwt_timer_result_t *result = &entry->result;
        if (scope->seen) {
            if (!result->seen) result->event = scope->event.setup;
            else if (memcmp(&result->event, &scope->event.setup, sizeof(result->event)))
                result->ambiguous = true;
            result->seen = true;
        }
        result->ambiguous |= scope->ambiguous;
        if (result->native_error == ESP_OK)
            result->native_error = entry->fault != ESP_OK ? entry->fault : cleanup_error;
        result->timer_identity = identity;
        result->complete = true;
        result->publishing = scope->seen;
        btwt_timer_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (exact && scope->seen) {
        esp_err_t observation_error = esp32_mquickjs_wifi_btwt_event_publish(scope);
        portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
        if (entry->identity == identity) {
            entry->result.observation_error = observation_error;
            entry->result.publishing = false;
            btwt_timer_changed_locked();
        }
        portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    }
}
void esp32_mquickjs_wifi_btwt_setup_tx_complete_native(unsigned slot, uint32_t identity,
    uintptr_t node, uint8_t dialog, const uint8_t parameter[17], uint8_t status)
{
    if (slot >= 32 || !identity || !dialog || parameter == NULL) return;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    bool exact = entry != NULL && entry->result.identity == identity && entry->node == node &&
        entry->phase == 0 && !entry->busy && !entry->result.tx_busy && !entry->result.cancel_requested && !entry->result.complete &&
        !memcmp(entry->parameter, parameter, sizeof(entry->parameter));
    if (exact) { entry->result.tx_busy = true; btwt_timer_changed_locked(); }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (!exact) return;
    esp32_mquickjs_wifi_btwt_event_scope_t scope = {0};
    esp_err_t error = ESP_OK;
    if (!esp32_mquickjs_wifi_btwt_event_begin(&scope, WIFI_EVENT_BTWT_SETUP, slot)) error = ESP_ERR_INVALID_STATE;
    else {
        esp32_mquickjs_wifi_twt_sdk_broadcast_tx_complete_native(node, dialog, parameter, status);
        bool ended = esp32_mquickjs_wifi_btwt_event_end(&scope);
        error = scope.error;
        if (error == ESP_OK && (!ended || scope.ambiguous ||
            (status != 1 && (!scope.seen || scope.event.setup.status != BTWT_SETUP_TXFAIL)) ||
            (status == 1 && scope.seen))) error = ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    uint32_t timer_identity = entry->identity;
    entry->result.tx_busy = false;
    btwt_timer_changed_locked();
    if (error == ESP_OK) error = entry->fault;
    if (error == ESP_OK && status == 1 &&
        (entry->phase != 30 || !entry->active || entry->dialog != dialog)) error = ESP_ERR_INVALID_STATE;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (error != ESP_OK) {
        btwt_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_BTWT_TIMER_NATIVE);
        if (timer_identity != 0) (void)esp32_mquickjs_wifi_btwt_timer_cancel_native(slot, timer_identity);
    }
    if (error != ESP_OK || status != 1) btwt_timer_publish_result(slot, timer_identity, &scope, error);
}
int esp32_mquickjs_wifi_btwt_response_native(uintptr_t node, const uint8_t body[20])
{
    if (body == NULL || !esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native(node)) return -1;
    unsigned slot = body[13] >> 3;
    uint8_t command = (body[6] >> 1) & 7U;
    if (body[2] == 0 || body[3] != 216 || body[4] != 10 || (body[5] & 12U) != 12U ||
        (body[6] & 1U) || command < TWT_ACCEPT) return -1;
    uint32_t identity = 0;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    if (entry != NULL && entry->node == node && entry->phase == 30 && entry->dialog == body[2] &&
        entry->active && !entry->fired && !entry->busy && entry->fault == ESP_OK) {
        identity = entry->identity;
        entry->dialog = 0; entry->active = false; /* One response can consume it. */
        btwt_timer_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (identity == 0) return -1;
    esp32_mquickjs_wifi_btwt_event_scope_t scope = {0};
    /* Do not enter the SDK if stop/delete failed: it would replace the shared
     * parameters before setfn notices the still-owned old handle. */
    esp_err_t error = esp32_mquickjs_wifi_btwt_timer_cancel_native(slot, identity);
    if (error != ESP_OK) { btwt_timer_publish_result(slot, identity, &scope, error); return -1; }
    if (!esp32_mquickjs_wifi_btwt_event_begin(&scope, WIFI_EVENT_BTWT_SETUP, slot)) {
        btwt_timer_failed(slot, ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_WIFI_BTWT_TIMER_NATIVE);
        btwt_timer_publish_result(slot, identity, &scope, ESP_ERR_INVALID_STATE); return -1;
    }
    int result = ieee80211_process_btwt_setup_action((void *)node, body);
    bool ended = esp32_mquickjs_wifi_btwt_event_end(&scope);
    error = scope.error;
    if (error == ESP_OK && (!ended || scope.ambiguous || (command != TWT_ACCEPT && !scope.seen)))
        error = ESP_ERR_INVALID_STATE;
    if (command == TWT_ACCEPT) {
        portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
        entry = &s_btwt_timer.entries[slot];
        bool installed = entry->identity != identity && entry->phase == 31 && entry->active && entry->fault == ESP_OK;
        identity = entry->identity;
        if (error == ESP_OK) error = entry->fault != ESP_OK ? entry->fault : !installed ? ESP_ERR_INVALID_STATE : ESP_OK;
        portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    }
    if (error != ESP_OK) {
        btwt_timer_failed(slot, error, ESP32_MQUICKJS_WIFI_BTWT_TIMER_NATIVE);
        (void)esp32_mquickjs_wifi_btwt_timer_cancel_native(slot, identity);
    }
    if (command != TWT_ACCEPT || error != ESP_OK) btwt_timer_publish_result(slot, identity, &scope, error);
    /* ACCEPT creates a new numeric dwell timer; its native process owns the
     * terminal result. Duplicates/queued old timeouts cannot consume it. */
    return error != ESP_OK ? -1 : result;
}
static void btwt_timer_process(void *argument, uint8_t phase)
{
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    unsigned slot = 32;
    esp32_mquickjs_wifi_btwt_timer_identity_t native = {0};
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (identity != 0U && s_btwt_timer.entries != NULL) {
        for (unsigned i = 0; i < 32; ++i) {
            btwt_timer_entry_t *entry = &s_btwt_timer.entries[i];
            if (entry->identity == identity && entry->phase == phase && entry->active && entry->fired && !entry->busy && entry->fault == ESP_OK) {
                slot = i; native.node = entry->node;
                memcpy(native.parameter, entry->parameter, sizeof(native.parameter));
                entry->active = false; entry->busy = true;
                btwt_timer_changed_locked(); break;
            }
        }
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (slot == 32) return;
    esp32_mquickjs_wifi_btwt_event_scope_t scope = {0};
    esp_err_t native_error = ESP_OK;
    if (!esp32_mquickjs_wifi_twt_sdk_broadcast_timer_matches_native(slot, phase, &native) ||
        !esp32_mquickjs_wifi_btwt_event_begin(&scope, WIFI_EVENT_BTWT_SETUP, slot)) {
        native_error = ESP_ERR_INVALID_STATE;
    } else {
        if (phase == 30) __real_btwt_setup_timeout_fn_process(native.parameter);
        else __real_btwt_setup_dwell_timeout_fn_process(native.parameter);
        /* The SDK posts SUCCESS before finishing its PM/table writes. Capture
         * synchronously, but do not expose completion until it returns. */
        bool ended = esp32_mquickjs_wifi_btwt_event_end(&scope);
        native_error = scope.error;
        if (native_error == ESP_OK && (!ended || !scope.seen || scope.ambiguous))
            native_error = ESP_ERR_INVALID_STATE;
    }
    if (native_error != ESP_OK)
        btwt_timer_failed(slot, native_error, ESP32_MQUICKJS_WIFI_BTWT_TIMER_NATIVE);
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    s_btwt_timer.entries[slot].busy = false;
    btwt_timer_changed_locked();
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    /* Native response timeout deletes its timer; dwell does not. Both routes
     * now finish the same retained stop/delete suffix, including match failure. */
    esp_err_t cleanup_error = esp32_mquickjs_wifi_btwt_timer_cancel_native(slot, identity);
    btwt_timer_publish_result(slot, identity, &scope, cleanup_error);
}
void __wrap_btwt_setup_timeout_fn_process(void *argument) { btwt_timer_process(argument, 30); }
void __wrap_btwt_setup_dwell_timeout_fn_process(void *argument) { btwt_timer_process(argument, 31); }
void __wrap_ieee80211_close_all_twt_sessions(void)
{
    /* Native connection PM teardown clears both agreement bitmaps but does
     * not retire the broadcast timers. Revoke every callback before returning
     * to the original close; a subsequent association cannot consume it. */
    esp32_mquickjs_wifi_twt_setup_results_connection_closed_native();
    esp32_mquickjs_wifi_twt_setup_timers_connection_closed_native();
    esp32_mquickjs_wifi_twt_information_timers_connection_closed_native();
    esp32_mquickjs_wifi_twt_tx_broadcast_cancel_native();
    uint32_t identities[32] = {0};
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (s_btwt_timer.entries != NULL) {
        for (unsigned i = 0; i < 32; ++i) {
            identities[i] = s_btwt_timer.entries[i].identity;
            s_btwt_timer.entries[i].active = false;
            s_btwt_timer.entries[i].result.native_closed = true;
        }
        btwt_timer_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    for (unsigned i = 0; i < 32; ++i)
        if (identities[i]) (void)esp32_mquickjs_wifi_btwt_timer_cancel_native(i, identities[i]);
    __real_ieee80211_close_all_twt_sessions();
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    if (s_btwt_timer.entries != NULL) {
        for (unsigned i = 0; i < 32; ++i) {
            btwt_timer_entry_t *entry = &s_btwt_timer.entries[i];
            if (entry->result.identity != 0 && !entry->result.complete) {
                entry->result.complete = true;
                entry->result.native_error = entry->fault != ESP_OK ? entry->fault : ESP_ERR_INVALID_STATE;
            }
        }
        btwt_timer_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
}
esp_err_t esp32_mquickjs_wifi_btwt_timer_available_native(unsigned slot)
{
    if (slot >= 32U) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    const btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    esp_err_t error = s_btwt_timer.snapshot.fault;
    if (error == ESP_OK && entry != NULL && (entry->active || entry->busy || entry->result.tx_busy || entry->result.publishing))
        error = ESP_ERR_INVALID_STATE;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    const ETSTimer *timer = (const ETSTimer *)(btwt_setup_timer + 20U * slot);
    return error != ESP_OK ? error : timer->timer_arg != NULL ? ESP_ERR_INVALID_STATE : ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_btwt_timer_error(void)
{
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    esp_err_t error = s_btwt_timer.snapshot.fault;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    return error;
}
bool esp32_mquickjs_wifi_btwt_timer_result(unsigned slot, uint32_t identity,
    esp32_mquickjs_wifi_btwt_timer_result_t *out)
{
    if (slot >= 32 || identity == 0 || out == NULL) return false;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    const btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    bool exact = entry != NULL && entry->identity == identity;
    if (exact) *out = entry->result;
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    return exact;
}
bool esp32_mquickjs_wifi_btwt_setup_result(unsigned slot, uint32_t identity,
    esp32_mquickjs_wifi_btwt_timer_result_t *out)
{
    if (slot >= 32 || !identity || out == NULL) return false;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    const btwt_timer_entry_t *entry = s_btwt_timer.entries != NULL ? &s_btwt_timer.entries[slot] : NULL;
    bool exact = entry != NULL && entry->result.identity == identity;
    if (exact) {
        *out = entry->result;
        /* Callback posting/arming faults can occur outside native processing.
         * Expose them without inventing a successful event or retiring storage. */
        if (out->native_error == ESP_OK && entry->fault != ESP_OK) out->native_error = entry->fault;
        if (out->native_error != ESP_OK) out->complete = true;
        if (entry->busy || out->tx_busy) out->complete = false;
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
    return exact;
}
void esp32_mquickjs_wifi_btwt_timer_snapshot(esp32_mquickjs_wifi_btwt_timer_snapshot_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL_SAFE(&s_btwt_timer_lock);
    *out = s_btwt_timer.snapshot;
    if (s_btwt_timer.entries != NULL) {
        for (unsigned i = 0; i < 32; ++i) {
            const btwt_timer_entry_t *entry = &s_btwt_timer.entries[i];
            uint32_t bit = UINT32_C(1) << i;
            if (entry->active) out->active_mask |= bit;
            if (entry->fired) out->fired_mask |= bit;
            if (entry->busy) out->busy_mask |= bit;
            if (entry->fault != ESP_OK) out->fault_mask |= bit;
            if (entry->cleanup_error != ESP_OK) out->cleanup_mask |= bit;
            if (entry->result.complete) out->complete_mask |= bit;
            if (entry->result.observation_error != ESP_OK) out->observation_error_mask |= bit;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_timer_lock);
}
#endif
