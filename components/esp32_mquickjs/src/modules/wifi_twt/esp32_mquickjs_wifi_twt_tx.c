#include "esp32_mquickjs_wifi_nan_tx.h"
#include "esp32_mquickjs_wifi_twt_tx.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp32_mquickjs_wifi_twt_sdk.h"
#include "esp32_mquickjs_wifi_twt_probe_result.h"
#include "esp32_mquickjs_wifi_twt_teardown_tx.h"
#include "esp32_mquickjs_wifi_twt_information.h"
#include "esp32_mquickjs_wifi_twt_broadcast_submit.h"
#include "esp32_mquickjs_wifi_offchan_frame.h"
#include <stddef.h>
#include <string.h>

/* Reviewed C5 net80211/PP archives and ROM linker maps are hash-gated.
 * TWT builders set callback bits 18/19/20 before ht_action_output. Observing
 * this boundary also includes frames cached off-channel before ppTxPkt.
 * Final wdev_funcs_init installs the wrapper at pp_wdev_funcs + 304; reviewed
 * rev0/rev100 ROM completion and ic_ebuf_recycle_tx use this entry. Wrapping
 * only flash call relocations would not establish the native recycle path.
 * The recycle wrapper runs on the common RX/TX path: keep it and everything
 * it accesses in internal memory, with no allocation/logging/SDK call locked. */
#define TWT_TX_CAPACITY 64U
#define TWT_TX_CALLBACK_MASK UINT32_C(0x1c0000)
#define TWT_TX_OUTPUT_RETURNED 1U
#define TWT_TX_RECYCLING 2U
#define TWT_TX_RECYCLED 4U
#define TWT_TX_PROBE 8U
#define TWT_TX_CALLBACK_SEEN 16U
#define TWT_TX_CALLBACK_DEFERRED 32U
#define TWT_TX_CALLBACK_BUSY 64U
#define TWT_TX_PROBE_SUBMIT 128U
/* probe_status is otherwise unused for non-probe frames. */
#define TWT_TX_MANAGED_TEARDOWN 128U
#define TWT_TX_INFORMATION 64U
#define TWT_TX_INFORMATION_WAKE 32U
#define TWT_TX_BROADCAST_SETUP 16U
#define TWT_TX_BROADCAST_CANCELLED 8U
typedef struct { void *buffer; uint32_t setup_identity; uint8_t flags, probe_status; } twt_tx_entry_t;
typedef struct { uintptr_t node; uint8_t parameter[17], dialog; } twt_broadcast_identity_t;
typedef union {
    esp32_mquickjs_wifi_twt_information_identity_t information;
    twt_broadcast_identity_t broadcast;
} twt_tx_identity_t;
typedef struct {
    twt_tx_identity_t entries[TWT_TX_CAPACITY];
    uint8_t broadcast_dialogs[32]; /* Last issued wire token, never wrap/reuse. */
} twt_tx_storage_t;
/* Synchronous native producer stack only. Neither callback nor queued message
 * retains this address; every access and removal uses the existing TX lock. */
typedef struct twt_information_submit {
    struct twt_information_submit *previous;
    TaskHandle_t task;
    bool wake_held, output_seen;
} twt_information_submit_t;
static DRAM_ATTR struct {
    twt_tx_entry_t *entries;
    TaskHandle_t probe_task;
    twt_tx_entry_t *probe_entry;
    twt_information_submit_t *information_submit;
    twt_tx_storage_t *information;
    TaskHandle_t information_task;
    esp32_mquickjs_wifi_twt_tx_snapshot_t snapshot;
} s_twt_tx;
static DRAM_ATTR portMUX_TYPE s_twt_tx_lock = portMUX_INITIALIZER_UNLOCKED;
_Static_assert(sizeof(void *) == 4 && sizeof(twt_tx_entry_t) == 12,
    "reviewed C5 EB pointers and bounded 768-byte TWT ledger");
_Static_assert(sizeof(esp32_mquickjs_wifi_twt_information_identity_t) == 24 &&
    sizeof(twt_tx_identity_t) == 24 && sizeof(twt_tx_storage_t) == 1568,
    "bounded optional identity storage and boot-scoped broadcast dialogs");

int __real_ht_action_output(void *node, void *buffer);
int __real_ic_tx_pkt(void *buffer);
int __real_wifi_sta_itwt_send_probe_req_process(void *message);
void __real_esf_buf_recycle(void *buffer);
void __wrap_esf_buf_recycle(void *buffer);
int __real_itwt_probe_rc_tx_cb(int status);
void __real_he_twt_setup_txcb(void *buffer);
void __real_he_twt_information_txcb(void *buffer);
int __real_ieee80211_itwt_information(void *node, uint32_t flow, uint32_t size, uint32_t all, uint32_t timeout);
void pm_twt_wake_up(void);
void pm_twt_wake_done(void);

/* Only ieee80211_itwt_information + 0x160 is redirected. Preserve every other
 * PM caller, including native information completions and IRAM callers. */
void esp32_mquickjs_wifi_twt_information_wake_up_native(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    twt_information_submit_t *scope = s_twt_tx.information_submit;
    if (scope != NULL && scope->task == task) scope->wake_held = true;
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    pm_twt_wake_up();
}
int __wrap_ieee80211_itwt_information(void *node, uint32_t flow, uint32_t size, uint32_t all, uint32_t timeout)
{
    if (xPortInIsrContext()) return ESP_ERR_INVALID_STATE;
    if (!esp32_mquickjs_wifi_twt_information_producer_allowed(node, flow, size, all, timeout))
        return ESP_ERR_INVALID_STATE;
    twt_information_submit_t scope = {.task = xTaskGetCurrentTaskHandle()};
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (s_twt_tx.information_submit != NULL && s_twt_tx.information_submit->task != scope.task) {
        portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
        return ESP_ERR_INVALID_STATE;
    }
    scope.previous = s_twt_tx.information_submit;
    s_twt_tx.information_submit = &scope;
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    int error = __real_ieee80211_itwt_information(node, flow, size, all, timeout);
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    s_twt_tx.information_submit = scope.previous;
    bool release = scope.wake_held && !scope.output_seen;
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    /* No EB reached output, so no TX completion can repay this reference.
     * An output error alone is NOT evidence of that absence. */
    if (release) pm_twt_wake_done();
    return error;
}

static void IRAM_ATTR twt_tx_changed(void)
{
    if (s_twt_tx.snapshot.revision != UINT32_MAX) ++s_twt_tx.snapshot.revision;
    else if (s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK)
        s_twt_tx.snapshot.fault = ESP32_MQUICKJS_WIFI_TWT_TX_REVISION_EXHAUSTED;
}
static void IRAM_ATTR twt_tx_finished(twt_tx_entry_t *entry)
{
    /* The slot cannot be reused until BOTH wrappers return from their native
     * calls. A synchronous recycle, including reallocation of the same EB
     * address during recycle, therefore cannot retire a later submission. */
    unsigned lifetime = entry->flags & (TWT_TX_OUTPUT_RETURNED | TWT_TX_RECYCLING |
        TWT_TX_RECYCLED | TWT_TX_CALLBACK_BUSY | TWT_TX_PROBE_SUBMIT);
    bool information_wake = !(entry->flags & TWT_TX_PROBE) &&
        (entry->probe_status & (TWT_TX_INFORMATION | TWT_TX_INFORMATION_WAKE)) ==
        (TWT_TX_INFORMATION | TWT_TX_INFORMATION_WAKE);
    if (lifetime == (TWT_TX_OUTPUT_RETURNED | TWT_TX_RECYCLED) && !information_wake) {
        if (s_twt_tx.probe_entry == entry) s_twt_tx.probe_entry = NULL;
        entry->buffer = NULL;
        entry->setup_identity = 0;
        entry->flags = 0;
        entry->probe_status = 0;
        --s_twt_tx.snapshot.tracked;
    }
    twt_tx_changed();
}
static uint32_t twt_tx_callbacks(void *buffer)
{
    if (buffer == NULL) return 0;
    const uint8_t *metadata;
    uint32_t callbacks;
    memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
    if (metadata == NULL) return 0;
    memcpy(&callbacks, metadata + 20, sizeof(callbacks));
    return callbacks;
}
static twt_tx_entry_t *twt_tx_begin(void *buffer)
{
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    bool allocate = s_twt_tx.entries == NULL && s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK;
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    twt_tx_entry_t *fresh = allocate ? esp32_mquickjs_memory_wireless_calloc("wifi", TWT_TX_CAPACITY, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL) : NULL;
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (s_twt_tx.entries == NULL && s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK) {
        if (fresh != NULL) {
            s_twt_tx.entries = fresh;
            fresh = NULL;
            s_twt_tx.snapshot.reserved_bytes = TWT_TX_CAPACITY * sizeof(twt_tx_entry_t);
        } else s_twt_tx.snapshot.fault = ESP32_MQUICKJS_WIFI_TWT_TX_NO_MEMORY;
    }
    twt_tx_entry_t *entry = NULL;
    if (s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK) {
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i) {
            twt_tx_entry_t *candidate = &s_twt_tx.entries[i];
            if (candidate->buffer == buffer && !(candidate->flags & (TWT_TX_RECYCLING | TWT_TX_RECYCLED))) {
                s_twt_tx.snapshot.fault = ESP32_MQUICKJS_WIFI_TWT_TX_DUPLICATE;
                break;
            }
            if (candidate->buffer == NULL && entry == NULL) entry = candidate;
        }
        if (s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK && entry == NULL)
            s_twt_tx.snapshot.fault = ESP32_MQUICKJS_WIFI_TWT_TX_CAPACITY;
        if (s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK) {
            entry->buffer = buffer;
            entry->setup_identity = 0;
            entry->flags = 0;
            ++s_twt_tx.snapshot.tracked;
            ++s_twt_tx.snapshot.output_calls;
        } else entry = NULL;
    }
    twt_tx_changed();
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    if (fresh != NULL) esp32_mquickjs_memory_payload_free(fresh);
    return entry;
}
static void twt_tx_output_returned(twt_tx_entry_t *entry)
{
    if (entry != NULL) {
        portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
        --s_twt_tx.snapshot.output_calls;
        entry->flags |= TWT_TX_OUTPUT_RETURNED;
        twt_tx_finished(entry);
        portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    }
}
static const uint8_t *twt_tx_body(void *buffer, bool completion, uint32_t minimum)
{
    const uint8_t *descriptor, *frame, *metadata;
    uint32_t length, protection;
    uint16_t header_length, flags;
    memcpy(&descriptor, (const uint8_t *)buffer + 4, sizeof(descriptor));
    memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
    memcpy(&length, (const uint8_t *)buffer + 24, sizeof(length));
    memcpy(&header_length, (const uint8_t *)buffer + 20, sizeof(header_length));
    if (descriptor == NULL || metadata == NULL || length < minimum || header_length < 24U) return NULL;
    memcpy(&frame, descriptor + 4, sizeof(frame));
    if (frame == NULL) return NULL;
    unsigned offset;
    if (completion) {
        /* Exact original callback layout, including native prefix adjustment.
         * The header is filled by mgmt_output, so it cannot select the body
         * offset at our earlier output boundary. */
        memcpy(&flags, (const uint8_t *)buffer + 40, sizeof(flags));
        if (flags & 0x2000U) frame += 8;
        offset = (frame[1] & 0x40U) ? 32U : 24U;
    } else {
        memcpy(&protection, metadata, sizeof(protection));
        offset = (protection & 1U) ? 32U : 24U;
    }
    return frame + offset;
}
bool esp32_mquickjs_wifi_twt_tx_broadcast_teardown_fields(void *buffer, bool completion, uint8_t *slot)
{
    if (buffer == NULL || slot == NULL) return false;
    const uint8_t *body = twt_tx_body(buffer, completion, 3);
    if (body == NULL || !((body[0] == 22 && body[1] == 7) || (body[0] == 23 && body[1] == 5)) ||
        (body[2] & 0xe0U) != 0x60U) return false;
    *slot = body[2] & 31U;
    return true;
}
static bool twt_tx_setup_fields(void *buffer, bool completion, uint8_t *dialog, uint8_t *flow)
{
    const uint8_t *body = twt_tx_body(buffer, completion, 20);
    if (body == NULL) return false;
    if (!((body[0] == 22U && body[1] == 6U) || (body[0] == 23U && body[1] == 4U)) ||
        body[3] != 216U || body[4] != 15U || (body[5] & 12U) != 0U) return false;
    *dialog = body[2];
    *flow = (body[6] >> 7) | ((body[7] & 3U) << 1);
    return true;
}
static bool twt_tx_information_control(void *buffer, bool completion, uint8_t *control)
{
    const uint8_t *body = twt_tx_body(buffer, completion, 3);
    if (body == NULL || !((body[0] == 22U && body[1] == 11U) || (body[0] == 23U && body[1] == 6U))) return false;
    /* The reviewed builder emits exactly 3 or 11 body bytes. Do not let the
     * completion parse an 8-byte next-TWT value absent from this frame. */
    if ((body[2] & 0x60U) && twt_tx_body(buffer, completion, 11) == NULL) return false;
    *control = body[2];
    return true;
}
static bool twt_tx_broadcast_fields(void *buffer, bool completion, twt_broadcast_identity_t *out)
{
    /* The reviewed SDK allocates twenty bytes but transmits fifteen for bTWT.
     * Read only the twelve-byte IE, not the five allocation-tail bytes. */
    const uint8_t *body = twt_tx_body(buffer, completion, 15);
    if (body == NULL || !((body[0] == 22 && body[1] == 6) || (body[0] == 23 && body[1] == 4)) ||
        body[2] == 0 || body[3] != 216 || body[4] != 10 || (body[5] & 12U) != 12U ||
        ((body[6] >> 1) & 7U) > TWT_DEMAND) return false;
    *out = (twt_broadcast_identity_t){.dialog = body[2]};
    memcpy(out->parameter, body + 3, 12);
    return true;
}
uint16_t esp32_mquickjs_wifi_twt_tx_broadcast_remaining(unsigned slot)
{
    if (slot >= 32) return 0;
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    uint16_t remaining = UINT8_MAX - (s_twt_tx.information ? s_twt_tx.information->broadcast_dialogs[slot] : 0);
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    return remaining;
}
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(unsigned broadcast_id)
{
    if (broadcast_id >= 32 || xPortInIsrContext()) return ESP_ERR_INVALID_ARG;
    if (esp32_mquickjs_wifi_btwt_setup_held(broadcast_id)) return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp32_mquickjs_wifi_btwt_timer_available_native(broadcast_id);
    if (error != ESP_OK) return error;
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (s_twt_tx.snapshot.fault != ESP32_MQUICKJS_WIFI_TWT_TX_OK) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK && s_twt_tx.information != NULL &&
        s_twt_tx.information->broadcast_dialogs[broadcast_id] == UINT8_MAX) error = ESP_ERR_NO_MEM;
    if (s_twt_tx.entries != NULL && s_twt_tx.information != NULL)
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i) {
            const twt_tx_entry_t *entry = &s_twt_tx.entries[i];
            if (entry->buffer != NULL && !(entry->flags & TWT_TX_PROBE) &&
                (entry->probe_status & TWT_TX_BROADCAST_SETUP) &&
                (s_twt_tx.information->entries[i].broadcast.parameter[10] >> 3) == broadcast_id)
                error = ESP_ERR_INVALID_STATE;
        }
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    return error;
}
void esp32_mquickjs_wifi_twt_tx_broadcast_cancel_native(void)
{
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (s_twt_tx.entries != NULL)
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i) {
            twt_tx_entry_t *entry = &s_twt_tx.entries[i];
            if (entry->buffer != NULL && !(entry->flags & TWT_TX_PROBE) &&
                (entry->probe_status & TWT_TX_BROADCAST_SETUP)) entry->probe_status |= TWT_TX_BROADCAST_CANCELLED;
        }
    twt_tx_changed();
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
}
static esp_err_t twt_tx_broadcast_request(unsigned slot, uint32_t identity, bool cancel, uint32_t *revision)
{
    if (slot >= 32 || !identity || xPortInIsrContext()) return ESP_ERR_INVALID_ARG;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    esp_err_t error = ESP_OK;
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (s_twt_tx.entries != NULL && s_twt_tx.information != NULL)
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i) {
            twt_tx_entry_t *entry = &s_twt_tx.entries[i];
            if (entry->buffer == NULL || entry->setup_identity != identity || (entry->flags & TWT_TX_PROBE) ||
                !(entry->probe_status & TWT_TX_BROADCAST_SETUP) ||
                (s_twt_tx.information->entries[i].broadcast.parameter[10] >> 3) != slot) continue;
            if (task == NULL || task != s_twt_tx.information_task) error = ESP_ERR_INVALID_STATE;
            else if (cancel) {
                if (!(entry->probe_status & TWT_TX_BROADCAST_CANCELLED)) {
                    entry->probe_status |= TWT_TX_BROADCAST_CANCELLED;
                    twt_tx_changed();
                }
                if (entry->flags & TWT_TX_CALLBACK_BUSY) error = ESP_ERR_NOT_FINISHED;
            } else error = ESP_ERR_NOT_FINISHED;
        }
    if (s_twt_tx.snapshot.fault != ESP32_MQUICKJS_WIFI_TWT_TX_OK) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK && revision != NULL) *revision = s_twt_tx.snapshot.revision;
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_cancel_request_native(unsigned slot, uint32_t identity)
{
    return twt_tx_broadcast_request(slot, identity, true, NULL);
}
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_quiescent_native(unsigned slot, uint32_t identity, uint32_t *revision)
{
    if (revision == NULL) return ESP_ERR_INVALID_ARG;
    return twt_tx_broadcast_request(slot, identity, false, revision);
}
int __real_ieee80211_btwt_setup(void *node, const wifi_btwt_setup_config_t *config);
int __wrap_ieee80211_btwt_setup(void *node, const wifi_btwt_setup_config_t *config)
{
    if (config == NULL || config->btwt_id >= 32 || (unsigned)config->setup_cmd > TWT_DEMAND || !config->timeout_time_ms)
        return ESP_ERR_INVALID_ARG;
    if (!esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native((uintptr_t)node)) return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(config->btwt_id);
    return error != ESP_OK ? error : __real_ieee80211_btwt_setup(node, config);
}
static esp_err_t twt_tx_broadcast_capture(void *node, void *buffer, twt_tx_entry_t *entry)
{
    twt_broadcast_identity_t native;
    if (entry == NULL) return ESP_ERR_INVALID_STATE;
    if (xPortInIsrContext() || twt_tx_callbacks(buffer) != (1U << 19) ||
        !twt_tx_broadcast_fields(buffer, false, &native) ||
        !esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native((uintptr_t)node)) return ESP_ERR_INVALID_STATE;
    unsigned broadcast_id = native.parameter[10] >> 3;
    esp_err_t error = esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(broadcast_id);
    if (error != ESP_OK) return error;
    uint8_t *body = (uint8_t *)twt_tx_body(buffer, false, 15);
    native.node = (uintptr_t)node;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    bool allocate = s_twt_tx.information == NULL;
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    twt_tx_storage_t *fresh = allocate ? esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_TX) : NULL;
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (fresh != NULL && s_twt_tx.information == NULL) {
        s_twt_tx.information = fresh; fresh = NULL;
        s_twt_tx.snapshot.reserved_bytes += sizeof(*s_twt_tx.information);
    }
    bool valid = task != NULL && s_twt_tx.information != NULL && s_twt_tx.snapshot.revision != UINT32_MAX &&
        s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK;
    if (valid && s_twt_tx.information_task != NULL && s_twt_tx.information_task != task)
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i)
            if (s_twt_tx.entries[i].buffer != NULL && !(s_twt_tx.entries[i].flags & TWT_TX_PROBE) &&
                (s_twt_tx.entries[i].probe_status & (TWT_TX_INFORMATION | TWT_TX_BROADCAST_SETUP))) valid = false;
    if (valid && s_twt_tx.information->broadcast_dialogs[broadcast_id] == UINT8_MAX) {
        valid = false; error = ESP_ERR_NO_MEM;
    }
    if (valid) {
        native.dialog = ++s_twt_tx.information->broadcast_dialogs[broadcast_id];
        if (native.dialog == UINT8_MAX)
            s_twt_tx.snapshot.broadcast_dialog_exhausted_mask |= UINT32_C(1) << broadcast_id;
        /* Producer owns this newly built EB until the original output call.
         * Rewrite only its dialog, preserving protection and all other bytes.
         * Once allocated, a token is consumed even if output later fails. */
        body[2] = native.dialog;
        s_twt_tx.information->entries[entry - s_twt_tx.entries].broadcast = native;
        s_twt_tx.information_task = task;
        entry->setup_identity = s_twt_tx.snapshot.revision;
        entry->probe_status = TWT_TX_BROADCAST_SETUP;
    } else if (s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK && s_twt_tx.information == NULL)
        s_twt_tx.snapshot.fault = ESP32_MQUICKJS_WIFI_TWT_TX_NO_MEMORY;
    if (!valid && error == ESP_OK)
        error = s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_NO_MEMORY ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE;
    twt_tx_changed();
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    if (fresh != NULL) esp32_mquickjs_memory_payload_free(fresh);
    if (valid) error = esp32_mquickjs_wifi_btwt_setup_begin_native(broadcast_id, entry->setup_identity,
        native.node, native.parameter);
    if (error == ESP_OK) error = esp32_mquickjs_wifi_btwt_submit_bind_native(broadcast_id, entry->setup_identity, native.parameter);
    return error;
}
static esp_err_t twt_tx_information_capture(void *node, void *buffer, twt_tx_entry_t *entry)
{
    uint8_t control;
    esp32_mquickjs_wifi_twt_information_identity_t native;
    bool valid = entry != NULL && twt_tx_callbacks(buffer) == (1U << 20) &&
        twt_tx_information_control(buffer, false, &control) &&
        esp32_mquickjs_wifi_twt_sdk_information_capture_native(control & 7U, &control, &native) && native.node == (uintptr_t)node;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    twt_information_submit_t *before = s_twt_tx.information_submit;
    bool scoped = before != NULL && before->task == task && before->wake_held && !before->output_seen;
    if (s_twt_tx.information_task != NULL && s_twt_tx.information_task != task && s_twt_tx.entries != NULL)
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i)
            if (s_twt_tx.entries[i].buffer != NULL && !(s_twt_tx.entries[i].flags & TWT_TX_PROBE) &&
                (s_twt_tx.entries[i].probe_status & (TWT_TX_INFORMATION | TWT_TX_BROADCAST_SETUP))) valid = false;
    bool allocate = valid && scoped && s_twt_tx.information == NULL &&
        s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK;
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    twt_tx_storage_t *fresh = allocate
        ? esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*fresh), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_TX) : NULL;
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (fresh != NULL && s_twt_tx.information == NULL) {
        s_twt_tx.information = fresh; fresh = NULL;
        s_twt_tx.snapshot.reserved_bytes += sizeof(*s_twt_tx.information);
    }
    twt_information_submit_t *scope = s_twt_tx.information_submit;
    bool owned = scope != NULL && scope->task == task && scope->wake_held && !scope->output_seen;
    esp_err_t error = ESP_OK;
    if (s_twt_tx.snapshot.fault != ESP32_MQUICKJS_WIFI_TWT_TX_OK || !valid || !owned ||
        s_twt_tx.information == NULL || s_twt_tx.snapshot.revision == UINT32_MAX) {
        if (s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK)
            s_twt_tx.snapshot.fault = allocate && s_twt_tx.information == NULL
                ? ESP32_MQUICKJS_WIFI_TWT_TX_NO_MEMORY : ESP32_MQUICKJS_WIFI_TWT_TX_INFORMATION_IDENTITY;
        error = s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_NO_MEMORY ||
            s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_CAPACITY ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE;
    } else {
        s_twt_tx.information->entries[entry - s_twt_tx.entries].information = native;
        s_twt_tx.information_task = task;
        entry->setup_identity = s_twt_tx.snapshot.revision;
        entry->probe_status = TWT_TX_INFORMATION | TWT_TX_INFORMATION_WAKE;
        scope->output_seen = true;
    }
    twt_tx_changed();
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    if (fresh != NULL) esp32_mquickjs_memory_payload_free(fresh);
    if (error == ESP_OK) esp32_mquickjs_wifi_twt_information_bind_native(&native, entry->setup_identity);
    return error;
}
int __wrap_ht_action_output(void *node, void *buffer)
{
    uint32_t callbacks = twt_tx_callbacks(buffer);
    uint32_t teardown_identity = 0;
    twt_tx_entry_t *entry = (callbacks & TWT_TX_CALLBACK_MASK) ? twt_tx_begin(buffer) : NULL;
    if (callbacks & (1U << 20)) {
        esp_err_t error = twt_tx_information_capture(node, buffer, entry);
        if (error != ESP_OK) {
            bool duplicate = false;
            portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
            if (entry == NULL && s_twt_tx.entries != NULL)
                for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i)
                    if (s_twt_tx.entries[i].buffer == buffer &&
                        !(s_twt_tx.entries[i].flags & (TWT_TX_RECYCLING | TWT_TX_RECYCLED))) duplicate = true;
            portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
            if (!duplicate) __wrap_esf_buf_recycle(buffer);
            twt_tx_output_returned(entry);
            return error;
        }
    }
    if (callbacks & (1U << 18)) {
        esp_err_t error = esp32_mquickjs_wifi_twt_teardown_tx_output_native(node, buffer, entry != NULL, &teardown_identity);
        if (error != ESP_OK) {
            bool duplicate = false;
            portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
            if (entry == NULL && s_twt_tx.entries != NULL)
                for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i)
                    if (s_twt_tx.entries[i].buffer == buffer &&
                        !(s_twt_tx.entries[i].flags & (TWT_TX_RECYCLING | TWT_TX_RECYCLED))) duplicate = true;
            portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
            if (!duplicate) __wrap_esf_buf_recycle(buffer);
            twt_tx_output_returned(entry);
            return error;
        }
        if (teardown_identity != 0U && entry != NULL) {
            portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
            entry->setup_identity = teardown_identity;
            entry->probe_status = TWT_TX_MANAGED_TEARDOWN;
            portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
        }
    }
    bool setup = (callbacks & (1U << 19)) != 0U;
    if (setup) {
        uint8_t dialog, flow;
        uint32_t identity = 0;
        bool broadcast = ((const uint8_t *)buffer)[61] != 0U;
        esp_err_t capture_error = broadcast ? twt_tx_broadcast_capture(node, buffer, entry) : ESP_OK;
        bool valid = broadcast ? capture_error == ESP_OK :
            entry != NULL && twt_tx_setup_fields(buffer, false, &dialog, &flow) &&
            esp32_mquickjs_wifi_twt_setup_tx_capture_native((uintptr_t)node, dialog, flow, &identity);
        portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
        if (valid && !broadcast) entry->setup_identity = identity;
        else if (!valid && !(broadcast && capture_error == ESP_ERR_NO_MEM) &&
            s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK)
            s_twt_tx.snapshot.fault = broadcast ? ESP32_MQUICKJS_WIFI_TWT_TX_BROADCAST_IDENTITY : ESP32_MQUICKJS_WIFI_TWT_TX_SETUP_IDENTITY;
        bool duplicate = false;
        if (entry == NULL && s_twt_tx.entries != NULL) {
            for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i)
                if (s_twt_tx.entries[i].buffer == buffer &&
                    !(s_twt_tx.entries[i].flags & (TWT_TX_RECYCLING | TWT_TX_RECYCLED))) duplicate = true;
        }
        esp_err_t error = s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_NO_MEMORY ||
            s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_CAPACITY ? ESP_ERR_NO_MEM : broadcast ? capture_error : ESP_ERR_INVALID_STATE;
        twt_tx_changed();
        portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
        if (!valid) {
            /* The reviewed builder propagates output errors. It has already
             * installed pending/timer state; caller still owes retirement.
             * Return the allocated EB without emitting an unidentified TX. */
            if (!duplicate) __wrap_esf_buf_recycle(buffer);
            twt_tx_output_returned(entry);
            return error;
        }
    }
    /* Other TWT frames remain observational on tracking failure. Preserve
     * native output/PMF behavior and the sticky incomplete-retirement fault. */
    uint32_t broadcast_identity = 0;
    unsigned broadcast_slot = 32;
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (entry != NULL && entry->probe_status == TWT_TX_BROADCAST_SETUP) {
        broadcast_identity = entry->setup_identity;
        broadcast_slot = s_twt_tx.information->entries[entry - s_twt_tx.entries].broadcast.parameter[10] >> 3;
    }
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    int result = __real_ht_action_output(node, buffer);
    if (broadcast_identity != 0)
        esp32_mquickjs_wifi_btwt_setup_submitted_native(broadcast_slot, broadcast_identity, result);
    if (teardown_identity != 0U) esp32_mquickjs_wifi_twt_teardown_tx_output_returned(buffer, teardown_identity);
    twt_tx_output_returned(entry);
    return result;
}
void __wrap_he_twt_setup_txcb(void *buffer)
{
    if (buffer == NULL) return;
    if (xPortInIsrContext()) return;
    twt_broadcast_identity_t broadcast = {0};
    bool is_broadcast = false;
    twt_tx_entry_t *entry = NULL;
    uint32_t identity = 0;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (s_twt_tx.entries != NULL) {
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i) {
            twt_tx_entry_t *candidate = &s_twt_tx.entries[i];
            if (candidate->buffer == buffer && candidate->setup_identity != 0U &&
                (candidate->probe_status == 0U || (candidate->probe_status == TWT_TX_BROADCAST_SETUP &&
                    s_twt_tx.information != NULL && s_twt_tx.information_task == task)) &&
                !(candidate->flags & TWT_TX_PROBE) &&
                !(candidate->flags & (TWT_TX_RECYCLING | TWT_TX_RECYCLED | TWT_TX_CALLBACK_SEEN))) {
                entry = candidate; identity = entry->setup_identity;
                is_broadcast = entry->probe_status == TWT_TX_BROADCAST_SETUP;
                if (is_broadcast) broadcast = s_twt_tx.information->entries[i].broadcast;
                entry->flags |= TWT_TX_CALLBACK_SEEN | TWT_TX_CALLBACK_BUSY;
                twt_tx_changed(); break;
            }
        }
    }
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    if (entry == NULL) return;
    uint8_t dialog, flow;
    if (is_broadcast) {
        twt_broadcast_identity_t current;
        if (((const uint8_t *)buffer)[61] != 0U && twt_tx_broadcast_fields(buffer, true, &current) &&
            current.dialog == broadcast.dialog && !memcmp(current.parameter, broadcast.parameter, sizeof(current.parameter)) &&
            esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native(broadcast.node) &&
            esp32_mquickjs_wifi_btwt_timer_available_native(broadcast.parameter[10] >> 3) == ESP_OK) {
            const uint8_t *metadata;
            memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
            esp32_mquickjs_wifi_btwt_setup_tx_complete_native(broadcast.parameter[10] >> 3, identity,
                broadcast.node, broadcast.dialog, broadcast.parameter, metadata[19]);
        }
    } else if (((const uint8_t *)buffer)[61] == 0U && twt_tx_setup_fields(buffer, true, &dialog, &flow) &&
        esp32_mquickjs_wifi_twt_setup_tx_matches_native(identity, dialog, flow))
        __real_he_twt_setup_txcb(buffer);
    /* The real callback may synchronously cause recycling. Only retained
     * ledger storage is accessed after return; never read that EB again. */
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    entry->flags &= ~TWT_TX_CALLBACK_BUSY;
    twt_tx_finished(entry);
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
}
void __wrap_he_twt_information_txcb(void *buffer)
{
    if (buffer == NULL || xPortInIsrContext()) return;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    twt_tx_entry_t *entry = NULL;
    esp32_mquickjs_wifi_twt_information_identity_t native = {0};
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (s_twt_tx.entries != NULL && s_twt_tx.information != NULL && s_twt_tx.information_task == task)
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i) {
            twt_tx_entry_t *candidate = &s_twt_tx.entries[i];
            if (candidate->buffer == buffer && candidate->setup_identity != 0U &&
                (candidate->probe_status & TWT_TX_INFORMATION) &&
                !(candidate->flags & (TWT_TX_PROBE | TWT_TX_RECYCLING | TWT_TX_RECYCLED | TWT_TX_CALLBACK_SEEN))) {
                entry = candidate; native = s_twt_tx.information->entries[i].information;
                entry->flags |= TWT_TX_CALLBACK_SEEN | TWT_TX_CALLBACK_BUSY;
                entry->probe_status &= (uint8_t)~TWT_TX_INFORMATION_WAKE;
                twt_tx_changed(); break;
            }
        }
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    if (entry == NULL) return;
    uint8_t control;
    bool valid = twt_tx_information_control(buffer, true, &control) && control == native.control &&
        esp32_mquickjs_wifi_twt_sdk_information_matches_native(control & 7U, &native);
    uint32_t tx_identity = entry->setup_identity; /* Slot pinned by CALLBACK_BUSY. */
    bool managed = esp32_mquickjs_wifi_twt_information_callback_begin(tx_identity);
    if (valid) __real_he_twt_information_txcb(buffer); /* First call repays PM. */
    else pm_twt_wake_done(); /* Reject mutation, repay only this TX's reference. */
    if (managed) esp32_mquickjs_wifi_twt_information_callback_end(tx_identity, valid);
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    entry->flags &= (uint8_t)~TWT_TX_CALLBACK_BUSY;
    twt_tx_finished(entry);
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
}
esp_err_t esp32_mquickjs_wifi_twt_tx_information_quiescent_native(uint32_t identity)
{
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    esp_err_t error = s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (s_twt_tx.entries != NULL)
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i) {
            const twt_tx_entry_t *entry = &s_twt_tx.entries[i];
            if (entry->buffer != NULL && (!identity || entry->setup_identity == identity) &&
                !(entry->flags & TWT_TX_PROBE) && (entry->probe_status & TWT_TX_INFORMATION)) error = ESP_ERR_NOT_FINISHED;
        }
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    return error;
}
void esp32_mquickjs_wifi_twt_tx_cleanup_native(void)
{
    if (xPortInIsrContext()) return;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    bool native = s_twt_tx.information_task == task;
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    if (!native) return;
    for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i) {
        portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
        twt_tx_entry_t *entry = s_twt_tx.entries != NULL ? &s_twt_tx.entries[i] : NULL;
        bool repay = entry != NULL && entry->buffer != NULL &&
            !(entry->flags & (TWT_TX_PROBE | TWT_TX_RECYCLING | TWT_TX_CALLBACK_BUSY)) &&
            (entry->flags & (TWT_TX_OUTPUT_RETURNED | TWT_TX_RECYCLED)) == (TWT_TX_OUTPUT_RETURNED | TWT_TX_RECYCLED) &&
            (entry->probe_status & (TWT_TX_INFORMATION | TWT_TX_INFORMATION_WAKE)) ==
                (TWT_TX_INFORMATION | TWT_TX_INFORMATION_WAKE);
        if (repay) {
            entry->probe_status &= (uint8_t)~TWT_TX_INFORMATION_WAKE;
            entry->flags |= TWT_TX_CALLBACK_BUSY;
            twt_tx_changed();
        }
        portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
        if (!repay) continue;
        pm_twt_wake_done();
        portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
        entry->flags &= (uint8_t)~TWT_TX_CALLBACK_BUSY;
        twt_tx_finished(entry);
        portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    }
}
static bool twt_tx_probe(void *buffer)
{
    if (buffer == NULL || xPortInIsrContext()) return false;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    bool scoped = s_twt_tx.probe_task != NULL && s_twt_tx.probe_task == task;
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    if (!scoped) return false;
    /* The reviewed TWT probe handler calls send_probereq with callback index
     * zero. That index is shared with ordinary connection probes, so neither
     * callback identity nor a global 'probe pending' flag identifies this TX.
     * Require this native call's task AND its actual Probe Request header. */
    const uint8_t *metadata, *descriptor, *frame;
    uint32_t callbacks;
    uint16_t header_length;
    memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
    memcpy(&descriptor, (const uint8_t *)buffer + 4, sizeof(descriptor));
    memcpy(&header_length, (const uint8_t *)buffer + 20, sizeof(header_length));
    if (metadata == NULL || descriptor == NULL || header_length < 24U) return false;
    memcpy(&callbacks, metadata + 20, sizeof(callbacks));
    if (callbacks != 1U) return false;
    memcpy(&frame, descriptor + 4, sizeof(frame));
    return frame != NULL && (frame[0] & 0xfcU) == 0x40U;
}
int __wrap_ic_tx_pkt(void *buffer)
{
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    esp32_mquickjs_wifi_nan_tx_submitting(buffer);
#endif
    /* ic_tx_pkt itself is flash-resident in the reviewed C5 image. Only the
     * common recycler below needs an IRAM-only call/data closure. */
    if (!twt_tx_probe(buffer)) return __real_ic_tx_pkt(buffer);
    twt_tx_entry_t *entry = twt_tx_begin(buffer);
    /* A TWT probe must not be transmitted without callback identity. The
     * native handler's nonzero send-result path posts failure and releases
     * its PM wake reference. Return its already allocated EB before failing. */
    if (entry == NULL) {
        __wrap_esf_buf_recycle(buffer);
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    bool claimed = s_twt_tx.probe_entry == NULL;
    if (claimed) {
        s_twt_tx.probe_entry = entry;
        entry->flags |= TWT_TX_PROBE | TWT_TX_PROBE_SUBMIT;
    } else if (s_twt_tx.snapshot.fault == ESP32_MQUICKJS_WIFI_TWT_TX_OK)
        s_twt_tx.snapshot.fault = ESP32_MQUICKJS_WIFI_TWT_TX_PROBE_SCOPE;
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    if (!claimed) {
        __wrap_esf_buf_recycle(buffer);
        twt_tx_output_returned(entry);
        return ESP_ERR_INVALID_STATE;
    }
    int result = __real_ic_tx_pkt(buffer);
    twt_tx_output_returned(entry);
    return result;
}
int __wrap_wifi_sta_itwt_send_probe_req_process(void *message)
{
    int admission = esp32_mquickjs_wifi_twt_sdk_probe_admit_native();
    if (admission != ESP_OK) return admission;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (task == NULL || s_twt_tx.probe_task != NULL || s_twt_tx.probe_entry != NULL ||
        s_twt_tx.snapshot.fault != ESP32_MQUICKJS_WIFI_TWT_TX_OK) {
        portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_twt_tx.probe_task = task;
    s_twt_tx.snapshot.probe_calls = 1;
    twt_tx_changed();
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    uint32_t identity = 0;
    int result = esp32_mquickjs_wifi_twt_probe_result_begin_native(&identity);
    if (result == ESP_OK) {
        result = __real_wifi_sta_itwt_send_probe_req_process(message);
        esp_err_t timer_error = esp32_mquickjs_wifi_twt_probe_timer_finish_native();
        if (result == ESP_OK) result = timer_error;
    }
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    s_twt_tx.snapshot.probe_calls = 0;
    s_twt_tx.probe_task = NULL;
    twt_tx_entry_t *entry = s_twt_tx.probe_entry;
    bool deferred = entry != NULL && (entry->flags & TWT_TX_CALLBACK_DEFERRED) && result == ESP_OK;
    int status = entry != NULL ? entry->probe_status : 0;
    if (entry != NULL) {
        entry->flags &= ~(TWT_TX_PROBE_SUBMIT | TWT_TX_CALLBACK_DEFERRED);
        if (deferred) entry->flags |= TWT_TX_CALLBACK_BUSY;
        twt_tx_finished(entry);
    }
    twt_tx_changed();
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    if (deferred) {
        (void)__real_itwt_probe_rc_tx_cb(status);
        esp_err_t timer_error = esp32_mquickjs_wifi_twt_probe_timer_finish_native();
        if (result == ESP_OK) result = timer_error;
        portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
        entry->flags &= ~TWT_TX_CALLBACK_BUSY;
        twt_tx_finished(entry);
        portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    }
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(identity, result);
    return result;
}
/* The build-local C5 call site passes EB instead of its status byte. It is the
 * only undefined reference to this SDK symbol. Ordinary connection callbacks
 * return -1 to retain cnx traversal; recognized TWT callbacks return 0 even on
 * failure, so their error cannot advance an unrelated connection candidate. */
int __wrap_itwt_probe_rc_tx_cb(void *buffer)
{
    twt_tx_entry_t *entry = NULL;
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (buffer != NULL && s_twt_tx.entries != NULL) {
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i) {
            twt_tx_entry_t *candidate = &s_twt_tx.entries[i];
            if (candidate->buffer == buffer && (candidate->flags & TWT_TX_PROBE) &&
                !(candidate->flags & (TWT_TX_RECYCLING | TWT_TX_RECYCLED))) {
                entry = candidate;
                break;
            }
        }
    }
    if (entry == NULL) {
        portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
        return -1;
    }
    if (entry != s_twt_tx.probe_entry || (entry->flags & TWT_TX_CALLBACK_SEEN)) {
        portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
        return 0;
    }
    const uint8_t *metadata;
    memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
    entry->probe_status = metadata[19];
    entry->flags |= TWT_TX_CALLBACK_SEEN;
    bool deferred = (entry->flags & TWT_TX_PROBE_SUBMIT) != 0U;
    entry->flags |= deferred ? TWT_TX_CALLBACK_DEFERRED : TWT_TX_CALLBACK_BUSY;
    int status = entry->probe_status;
    twt_tx_changed();
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    if (!deferred) {
        (void)__real_itwt_probe_rc_tx_cb(status);
        (void)esp32_mquickjs_wifi_twt_probe_timer_finish_native();
        portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
        entry->flags &= ~TWT_TX_CALLBACK_BUSY;
        twt_tx_finished(entry);
        portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    }
    return 0;
}
void IRAM_ATTR __wrap_esf_buf_recycle(void *buffer)
{
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    uint32_t nan_ticket = esp32_mquickjs_wifi_nan_tx_recycling(buffer);
#endif
#if (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    uint64_t offchan_ticket = esp32qjs_wifi_offchan_frame_recycle(buffer);
#endif
    twt_tx_entry_t *entry = NULL;
    uint32_t teardown_identity = 0;
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (buffer != NULL && s_twt_tx.entries != NULL && s_twt_tx.snapshot.tracked != 0U) {
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i) {
            twt_tx_entry_t *candidate = &s_twt_tx.entries[i];
            if (candidate->buffer == buffer && !(candidate->flags & (TWT_TX_RECYCLING | TWT_TX_RECYCLED))) {
                entry = candidate;
                if (!(entry->flags & TWT_TX_PROBE) && entry->probe_status == TWT_TX_MANAGED_TEARDOWN)
                    teardown_identity = entry->setup_identity;
                entry->flags |= TWT_TX_RECYCLING;
                ++s_twt_tx.snapshot.recycle_calls;
                twt_tx_changed();
                break;
            }
        }
    }
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    if (teardown_identity != 0U) esp32_mquickjs_wifi_twt_teardown_tx_recycle(buffer, teardown_identity, true);
    __real_esf_buf_recycle(buffer);
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    esp32_mquickjs_wifi_nan_tx_recycled(nan_ticket);
#endif
#if (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    esp32qjs_wifi_offchan_frame_recycled(offchan_ticket);
#endif
    /* Never read the EB or metadata after the native recycler: either may
     * already be freed/reused. Only our separately retained slot is touched. */
    if (entry != NULL) {
        if (teardown_identity != 0U) esp32_mquickjs_wifi_twt_teardown_tx_recycle(buffer, teardown_identity, false);
        portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
        --s_twt_tx.snapshot.recycle_calls;
        entry->flags = (entry->flags & ~TWT_TX_RECYCLING) | TWT_TX_RECYCLED;
        twt_tx_finished(entry);
        portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    }
}
void esp32_mquickjs_wifi_twt_tx_snapshot(esp32_mquickjs_wifi_twt_tx_snapshot_t *output)
{
    if (output == NULL) return;
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    *output = s_twt_tx.snapshot;
    output->probe_buffer_present = s_twt_tx.probe_entry != NULL;
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
}
bool esp32_mquickjs_wifi_twt_tx_teardown_identity(void *buffer, uint32_t *identity)
{
    if (buffer == NULL || identity == NULL) return false;
    bool found = false;
    portENTER_CRITICAL_SAFE(&s_twt_tx_lock);
    if (s_twt_tx.entries != NULL)
        for (unsigned i = 0; i < TWT_TX_CAPACITY; ++i) {
            const twt_tx_entry_t *entry = &s_twt_tx.entries[i];
            if (entry->buffer != buffer || (entry->flags & (TWT_TX_RECYCLING | TWT_TX_RECYCLED))) continue;
            *identity = !(entry->flags & TWT_TX_PROBE) && entry->probe_status == TWT_TX_MANAGED_TEARDOWN
                ? entry->setup_identity : 0;
            found = true;
            break;
        }
    portEXIT_CRITICAL_SAFE(&s_twt_tx_lock);
    return found;
}
#endif
