#include "esp32_mquickjs_wifi_twt_information_timer.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp32_mquickjs_wifi_twt_information.h"
#include "esp32_mquickjs_wifi_twt_setup_result.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "rom/ets_sys.h"
#include <stddef.h>
#include <string.h>

extern ETSTimer itwt_information_timer[8];
void itwt_information_timeout_fn(void *argument);
void __real_itwt_information_timeout_fn_process(void *argument);
int ieee80211_timer_process(int signal, int operation, void *argument);
typedef struct {
    esp32_mquickjs_wifi_twt_information_identity_t native;
    void *payload;
    uint32_t identity;
    esp_err_t fault, cleanup_error;
    bool active, fired, busy, stopped;
} twt_information_entry_t;
static DRAM_ATTR struct {
    twt_information_entry_t *entries;
    esp32_mquickjs_wifi_twt_information_timer_snapshot_t snapshot;
} s_information_timer;
static DRAM_ATTR portMUX_TYPE s_information_lock = portMUX_INITIALIZER_UNLOCKED;
_Static_assert(sizeof(ETSTimer) == 20 && offsetof(ETSTimer, timer_arg) == 16 &&
    sizeof(twt_information_entry_t) == 44, "reviewed C5 information timer and 352-byte ledger");

static int IRAM_ATTR information_index(const void *timer)
{
    uintptr_t start = (uintptr_t)itwt_information_timer, address = (uintptr_t)timer;
    return address >= start && address < start + 160U && (address - start) % 20U == 0U
        ? (int)((address - start) / 20U) : -1;
}
static void IRAM_ATTR information_changed_locked(void)
{
    if (s_information_timer.snapshot.revision != UINT32_MAX) ++s_information_timer.snapshot.revision;
    else if (s_information_timer.snapshot.fault == ESP_OK) s_information_timer.snapshot.fault = ESP_ERR_NO_MEM;
}
static void IRAM_ATTR information_failed_locked(unsigned slot, esp_err_t error, uint8_t stage)
{
    if (s_information_timer.snapshot.fault == ESP_OK) {
        s_information_timer.snapshot.fault = error;
        s_information_timer.snapshot.fault_stage = stage;
        s_information_timer.snapshot.fault_slot = slot;
    }
    if (s_information_timer.entries != NULL) {
        twt_information_entry_t *entry = &s_information_timer.entries[slot];
        entry->active = false;
        if (entry->fault == ESP_OK) entry->fault = error;
        if (stage == ESP32_MQUICKJS_WIFI_TWT_INFORMATION_STOP || stage == ESP32_MQUICKJS_WIFI_TWT_INFORMATION_DELETE)
            entry->cleanup_error = error;
    }
    information_changed_locked();
}
static void IRAM_ATTR information_failed(unsigned slot, esp_err_t error, uint8_t stage)
{
    portENTER_CRITICAL_SAFE(&s_information_lock);
    information_failed_locked(slot, error, stage);
    portEXIT_CRITICAL_SAFE(&s_information_lock);
}
static bool information_allocate(void)
{
    portENTER_CRITICAL_SAFE(&s_information_lock);
    bool present = s_information_timer.entries != NULL;
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    if (present) return true;
    twt_information_entry_t *fresh = esp32_mquickjs_memory_wireless_calloc("wifi", 8, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (fresh == NULL) return false;
    portENTER_CRITICAL_SAFE(&s_information_lock);
    if (s_information_timer.entries == NULL) {
        s_information_timer.entries = fresh;
        s_information_timer.snapshot.reserved_bytes = 8U * sizeof(*fresh);
        fresh = NULL;
    }
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    if (fresh != NULL) esp32_mquickjs_memory_payload_free(fresh);
    return true;
}
static void information_callback(void *argument)
{
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    unsigned slot = 8;
    portENTER_CRITICAL_SAFE(&s_information_lock);
    if (identity != 0U && s_information_timer.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i) {
            twt_information_entry_t *entry = &s_information_timer.entries[i];
            if (entry->identity == identity && entry->active && !entry->fired && entry->fault == ESP_OK) {
                slot = i; entry->fired = true; information_changed_locked(); break;
            }
        }
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    if (slot == 8) return;
    int error = ieee80211_timer_process(7, 29, argument);
    if (error != 0) {
        portENTER_CRITICAL_SAFE(&s_information_lock);
        if (s_information_timer.entries[slot].identity == identity && s_information_timer.entries[slot].active)
            information_failed_locked(slot, error, ESP32_MQUICKJS_WIFI_TWT_INFORMATION_POST);
        portEXIT_CRITICAL_SAFE(&s_information_lock);
    }
}
bool esp32_mquickjs_wifi_twt_information_timer_setfn(void *timer, void *callback, void *argument)
{
    int slot = information_index(timer);
    if (slot < 0) return false;
    /* Only the two reviewed SDK producers transfer a malloc(9) argument.
     * Unknown callbacks cannot authorize freeing an unknown pointer. */
    if (callback != (void *)itwt_information_timeout_fn || argument == NULL) {
        information_failed(slot, ESP_ERR_INVALID_ARG, ESP32_MQUICKJS_WIFI_TWT_INFORMATION_ARGUMENT);
        return true;
    }
    esp32_mquickjs_wifi_twt_information_identity_t native = {0};
    bool valid = esp32_mquickjs_wifi_twt_sdk_information_capture_native(slot, argument, &native);
    if (!valid || !information_allocate()) {
        esp32_mquickjs_memory_payload_free(argument);
        information_failed(slot, valid ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE,
            valid ? ESP32_MQUICKJS_WIFI_TWT_INFORMATION_ALLOCATE : ESP32_MQUICKJS_WIFI_TWT_INFORMATION_ARGUMENT);
        return true;
    }
    portENTER_CRITICAL_SAFE(&s_information_lock);
    twt_information_entry_t *entry = &s_information_timer.entries[slot];
    esp_err_t error = s_information_timer.snapshot.fault;
    if (error == ESP_OK && (entry->payload != NULL || entry->busy || entry->active || ((ETSTimer *)timer)->timer_arg != NULL))
        error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK && (s_information_timer.snapshot.last_identity == UINT32_MAX ||
        s_information_timer.snapshot.revision == UINT32_MAX)) error = ESP_ERR_NO_MEM;
    uint32_t identity = 0;
    if (error == ESP_OK) {
        identity = ++s_information_timer.snapshot.last_identity;
        *entry = (twt_information_entry_t){.native = native, .payload = argument,
            .identity = identity, .active = true};
        information_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    if (error != ESP_OK) {
        esp32_mquickjs_memory_payload_free(argument);
        information_failed(slot, error, ESP32_MQUICKJS_WIFI_TWT_INFORMATION_ARGUMENT);
        return true;
    }
    esp32_mquickjs_wifi_twt_information_timer_bound_native(&native, identity);
    const esp_timer_create_args_t args = {.callback = information_callback,
        .arg = (void *)(uintptr_t)identity, .dispatch_method = ESP_TIMER_TASK, .name = "iTWT information"};
    esp_timer_handle_t handle = NULL;
    error = esp_timer_create(&args, &handle);
    if (error != ESP_OK) {
        information_failed(slot, error, ESP32_MQUICKJS_WIFI_TWT_INFORMATION_CREATE);
        /* No callback can refer to payload; done also handles NULL timer. */
        esp32_mquickjs_wifi_twt_information_timer_done(timer);
        return true;
    }
    ETSTimer *legacy = timer;
    memset(legacy, 0, sizeof(*legacy));
    legacy->timer_expire = UINT32_C(0x12121212);
    legacy->timer_arg = handle;
    return true;
}
bool IRAM_ATTR esp32_mquickjs_wifi_twt_information_timer_disarm(void *timer)
{
    int slot = information_index(timer);
    if (slot < 0) return false;
    portENTER_CRITICAL_SAFE(&s_information_lock);
    twt_information_entry_t *entry = s_information_timer.entries != NULL ? &s_information_timer.entries[slot] : NULL;
    bool known = entry != NULL && entry->identity != 0U;
    bool stopped = known && entry->stopped;
    if (known) { entry->active = false; information_changed_locked(); }
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    esp_timer_handle_t handle = ((ETSTimer *)timer)->timer_arg;
    if (handle == NULL || stopped) return true;
    esp_err_t error = known ? esp_timer_stop(handle) : ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL_SAFE(&s_information_lock);
    if (known && (error == ESP_OK || error == ESP_ERR_INVALID_STATE)) {
        entry->stopped = true; entry->cleanup_error = ESP_OK; information_changed_locked();
    } else information_failed_locked(slot, error, ESP32_MQUICKJS_WIFI_TWT_INFORMATION_STOP);
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    return true;
}
bool esp32_mquickjs_wifi_twt_information_timer_done(void *timer)
{
    int slot = information_index(timer);
    if (slot < 0) return false;
    esp32_mquickjs_wifi_twt_information_timer_disarm(timer);
    portENTER_CRITICAL_SAFE(&s_information_lock);
    twt_information_entry_t *entry = s_information_timer.entries != NULL ? &s_information_timer.entries[slot] : NULL;
    bool known = entry != NULL && entry->identity != 0U;
    bool stopped = known && entry->stopped;
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    ETSTimer *legacy = timer;
    if (legacy->timer_arg != NULL) {
        if (!stopped) return true; /* Keep the failed stop suffix and handle. */
        esp_err_t error = esp_timer_delete(legacy->timer_arg);
        if (error != ESP_OK) {
            information_failed(slot, error, ESP32_MQUICKJS_WIFI_TWT_INFORMATION_DELETE);
            return true;
        }
        legacy->timer_arg = NULL; legacy->timer_expire = 0;
    }
    void *payload = NULL;
    portENTER_CRITICAL_SAFE(&s_information_lock);
    if (known) {
        entry->cleanup_error = ESP_OK;
        /* A native process has already detached its own argument and owns
         * the SDK free. Numeric queued callbacks borrow no payload storage. */
        payload = entry->payload; entry->payload = NULL;
        information_changed_locked();
    }
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    if (payload != NULL) esp32_mquickjs_memory_payload_free(payload);
    return true;
}
void esp32_mquickjs_wifi_twt_information_timers_connection_closed_native(void)
{
    uint8_t slots = 0;
    portENTER_CRITICAL_SAFE(&s_information_lock);
    if (s_information_timer.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i) {
            twt_information_entry_t *entry = &s_information_timer.entries[i];
            if (!entry->identity) continue;
            entry->active = false;
            if (!entry->busy) slots |= (uint8_t)(1U << i);
        }
    if (s_information_timer.entries != NULL) information_changed_locked();
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    for (unsigned i = 0; i < 8U; ++i)
        if (slots & (1U << i)) (void)esp32_mquickjs_wifi_twt_information_timer_done(&itwt_information_timer[i]);
}
bool IRAM_ATTR esp32_mquickjs_wifi_twt_information_timer_arm(void *timer, uint64_t us, bool repeat)
{
    int slot = information_index(timer);
    if (slot < 0) return false;
    portENTER_CRITICAL_SAFE(&s_information_lock);
    twt_information_entry_t *entry = s_information_timer.entries != NULL ? &s_information_timer.entries[slot] : NULL;
    bool valid = entry != NULL && entry->active && !entry->fired && entry->payload != NULL && entry->fault == ESP_OK;
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    esp_timer_handle_t handle = ((ETSTimer *)timer)->timer_arg;
    if (!valid || handle == NULL || repeat) {
        information_failed(slot, repeat ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_WIFI_TWT_INFORMATION_START);
        return true;
    }
    esp_err_t error = esp_timer_stop(handle);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        information_failed(slot, error, ESP32_MQUICKJS_WIFI_TWT_INFORMATION_STOP);
        return true;
    }
    error = esp_timer_start_once(handle, us);
    if (error != ESP_OK) information_failed(slot, error, ESP32_MQUICKJS_WIFI_TWT_INFORMATION_START);
    return true;
}
void __wrap_itwt_information_timeout_fn_process(void *argument)
{
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    unsigned slot = 8;
    void *payload = NULL;
    esp32_mquickjs_wifi_twt_information_identity_t native = {0};
    portENTER_CRITICAL_SAFE(&s_information_lock);
    if (identity != 0U && s_information_timer.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i) {
            twt_information_entry_t *entry = &s_information_timer.entries[i];
            if (entry->identity == identity && entry->active && entry->fired && !entry->busy && entry->fault == ESP_OK) {
                slot = i; native = entry->native; payload = entry->payload;
                entry->payload = NULL; entry->active = false; entry->busy = true;
                information_changed_locked(); break;
            }
        }
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    if (slot == 8) return;
    bool valid = payload != NULL && esp32_mquickjs_wifi_twt_sdk_information_resume_allowed_native(&native);
    if (valid)
        __real_itwt_information_timeout_fn_process(payload); /* Includes SDK free. */
    else {
        information_failed(slot, ESP_ERR_INVALID_STATE, ESP32_MQUICKJS_WIFI_TWT_INFORMATION_NATIVE);
        esp32_mquickjs_wifi_twt_information_timer_done(&itwt_information_timer[slot]);
        if (payload != NULL) esp32_mquickjs_memory_payload_free(payload);
    }
    portENTER_CRITICAL_SAFE(&s_information_lock);
    s_information_timer.entries[slot].busy = false;
    esp_err_t error = s_information_timer.entries[slot].fault;
    if (error == ESP_OK) error = s_information_timer.entries[slot].cleanup_error;
    information_changed_locked();
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    if (error == ESP_OK && (!valid || !esp32_mquickjs_wifi_twt_sdk_information_resumed_native(&native)))
        error = ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_twt_information_timer_finished_native(&native, identity, error);
}
esp_err_t esp32_mquickjs_wifi_twt_information_timer_replaceable_native(int16_t request_id, unsigned flow)
{
    if (request_id < 0 || flow >= 8U) return ESP_ERR_INVALID_ARG;
    uint8_t control = (uint8_t)(flow | 0x60U);
    esp32_mquickjs_wifi_twt_information_identity_t current;
    if (!esp32_mquickjs_wifi_twt_sdk_information_capture_native(flow, &control, &current) ||
        current.request_ids[flow] != request_id) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL_SAFE(&s_information_lock);
    esp_err_t error = s_information_timer.snapshot.fault;
    for (unsigned i = 0; error == ESP_OK && i < 8U; ++i) {
        const twt_information_entry_t *entry = s_information_timer.entries != NULL ? &s_information_timer.entries[i] : NULL;
        bool handle = itwt_information_timer[i].timer_arg != NULL;
        if (handle && (entry == NULL || !entry->identity)) { error = ESP_ERR_INVALID_STATE; break; }
        if (entry == NULL || (!handle && !entry->active && !entry->busy && entry->payload == NULL)) continue;
        if (i != flow && !(entry->native.flows & (1U << flow))) continue;
        if (i != flow || entry->native.node != current.node || entry->native.flows != (1U << flow) || entry->native.request_ids[flow] != request_id ||
            (entry->native.control & 0x80U) || entry->busy || entry->fault != ESP_OK || entry->cleanup_error != ESP_OK)
            error = ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_information_timer_cleanup_native(int16_t request_id, uint8_t retired_flows)
{
    if (request_id < 0) return ESP_ERR_INVALID_ARG;
    uint8_t slots = 0;
    /* Native queue serialization keeps timer identity/payload ownership stable.
     * esp_timer can only queue its numeric callback. Read close consent outside
     * the timer lock, avoiding lock nesting with the setup result ledger. */
    for (unsigned i = 0; i < 8U; ++i) {
        portENTER_CRITICAL_SAFE(&s_information_lock);
        const twt_information_entry_t *entry = s_information_timer.entries != NULL ? &s_information_timer.entries[i] : NULL;
        esp32_mquickjs_wifi_twt_information_identity_t native = {0};
        bool busy = false, live = false;
        if (entry != NULL) {
            native = entry->native;
            busy = entry->busy;
            live = entry->active || entry->payload != NULL || itwt_information_timer[i].timer_arg != NULL;
        }
        portEXIT_CRITICAL_SAFE(&s_information_lock);
        bool owned = false;
        for (unsigned flow = 0; flow < 8U; ++flow)
            if ((native.flows & (1U << flow)) && native.request_ids[flow] == request_id) owned = true;
        if (!owned) continue;
        if (busy) return ESP_ERR_NOT_FINISHED;
        if (live)
            for (unsigned flow = 0; flow < 8U; ++flow)
                if ((native.flows & (uint8_t)~retired_flows & (1U << flow)) &&
                    !esp32_mquickjs_wifi_twt_setup_request_closing(native.request_ids[flow]))
                    return ESP_ERR_NOT_FINISHED;
        slots |= (uint8_t)(1U << i);
    }
    for (unsigned i = 0; i < 8U; ++i)
        if (slots & (1U << i)) esp32_mquickjs_wifi_twt_information_timer_done(&itwt_information_timer[i]);
    uint32_t revision;
    return esp32_mquickjs_wifi_twt_information_timer_quiescent_native(request_id, &revision);
}
esp_err_t esp32_mquickjs_wifi_twt_information_timer_quiescent_native(int16_t request_id, uint32_t *revision)
{
    if (request_id < 0 || revision == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t error = ESP_OK;
    portENTER_CRITICAL_SAFE(&s_information_lock);
    if (s_information_timer.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i) {
            const twt_information_entry_t *entry = &s_information_timer.entries[i];
            bool owned = false;
            for (unsigned flow = 0; flow < 8U; ++flow)
                if ((entry->native.flows & (1U << flow)) && entry->native.request_ids[flow] == request_id) owned = true;
            if (!owned) continue;
            if (entry->cleanup_error != ESP_OK) { error = entry->cleanup_error; break; }
            if (entry->active || entry->busy || entry->payload != NULL || itwt_information_timer[i].timer_arg != NULL)
                error = ESP_ERR_NOT_FINISHED;
        }
    if (error == ESP_OK && s_information_timer.snapshot.revision == UINT32_MAX) error = ESP_ERR_NO_MEM;
    if (error == ESP_OK) *revision = s_information_timer.snapshot.revision;
    portEXIT_CRITICAL_SAFE(&s_information_lock);
    return error;
}
void esp32_mquickjs_wifi_twt_information_timer_snapshot(esp32_mquickjs_wifi_twt_information_timer_snapshot_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL_SAFE(&s_information_lock);
    *out = s_information_timer.snapshot;
    if (s_information_timer.entries != NULL)
        for (unsigned i = 0; i < 8U; ++i) {
            const twt_information_entry_t *entry = &s_information_timer.entries[i];
            if (entry->active) out->active_mask |= (uint8_t)(1U << i);
            if (entry->payload != NULL) out->payload_mask |= (uint8_t)(1U << i);
            if (entry->busy) out->busy_mask |= (uint8_t)(1U << i);
            if (entry->cleanup_error != ESP_OK) out->cleanup_mask |= (uint8_t)(1U << i);
        }
    portEXIT_CRITICAL_SAFE(&s_information_lock);
}
#endif
