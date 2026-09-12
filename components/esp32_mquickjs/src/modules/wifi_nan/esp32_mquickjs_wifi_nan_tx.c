#include "esp32_mquickjs_wifi_nan_tx.h"
#include "esp32_mquickjs_wifi_nan_ndp.h"
#include "esp32_mquickjs_wifi_nan_timer.h"
#include "esp32_mquickjs_wifi_nan_pasn_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
#if !CONFIG_IDF_TARGET_ESP32C5 || !CONFIG_SOC_WIFI_HE_SUPPORT
#error "NAN buffer ABI requires the reviewed C5 archive and recycler"
#endif
#include "esp32_mquickjs_memory.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include <string.h>

#define NAN_TX_CAPACITY ESP32_MQUICKJS_NAN_TX_CAPACITY
typedef struct {
    void *buffer;
    uint32_t ticket;
    uint32_t message_identity, ndp_identity, callbacks;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    uint32_t pairing_identity;
    bool pairing_result_recorded;
    bool authentication;
#endif
    uint8_t service_id, ndp_id, peer[6];
    bool allocating, recycling, recycled, datapath, data;
} nan_tx_entry_t;
typedef struct {
    esp32_mquickjs_wifi_nan_ndp_status_t status;
    const void *request;
    uintptr_t native_ndl;
    uint16_t ssi_len;
    uint8_t ssi[ESP_WIFI_MAX_SVC_SSI_LEN];
} nan_ndp_entry_t;
typedef struct {
    uintptr_t timer, argument, ndl;
    esp_timer_handle_t handle;
    uint32_t identity;
    uint8_t phase;
    bool active, armed, fired, stopped;
} nan_timer_entry_t;
typedef struct {
    nan_tx_entry_t entries[NAN_TX_CAPACITY];
    uint32_t submissions, rejected;
    esp_err_t error;
    bool closing;
    struct {
        esp32_mquickjs_wifi_nan_message_tx_status_t status;
        uint32_t *context;
        TaskHandle_t scope_task;
    } message;
    nan_ndp_entry_t datapaths[ESP_WIFI_NAN_DATAPATH_MAX_PEERS];
    uint32_t ndp_scope_identity;
    TaskHandle_t ndp_scope_task;
    nan_timer_entry_t timers[3]; /* one setup timer and two peer inactivity timers */
    uintptr_t deleting_ndl;
    uint32_t timer_callbacks;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    uint8_t paired_cache_services[ESP_WIFI_NAN_DATAPATH_MAX_PEERS];
    esp32_mquickjs_wifi_nan_credential_info_t credentials[ESP32_MQUICKJS_NAN_CACHED_PAIRINGS];
    struct {
        TaskHandle_t task;
        uint32_t identity;
        uint8_t service_id;
        bool authentication;
    } pairing;
    struct {
        uint32_t identity, followups_pending;
        esp_err_t error;
        int64_t completed_us;
    } pairing_result;
#endif
} nan_tx_pool_t;
static portMUX_TYPE s_nan_tx_lock = portMUX_INITIALIZER_UNLOCKED;
/* The IRAM recycler can only touch this internal-memory pool, its scalar
 * ticket and this DRAM lock/pointer. No allocations or JS in that closure. */
static nan_tx_pool_t *s_nan_tx;
static uint32_t s_nan_tx_next_ticket = 1;
static uint32_t s_nan_ndp_next_identity = 1;
static uint32_t s_nan_timer_next_identity = 1;
static esp_err_t nan_timers_close(void);
static esp_err_t nan_timer_retire_peer_native(uintptr_t ndl);
static nan_ndp_entry_t *nan_ndp_peer_locked(uint8_t ndp_id, const uint8_t peer[6]);

esp_err_t esp32_mquickjs_wifi_nan_tx_message_begin(uint32_t identity, uint32_t *context)
{
    if (!identity || !context || *context) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool available = s_nan_tx && !s_nan_tx->closing && !s_nan_tx->error && !s_nan_tx->message.status.identity;
    if (available) {
        s_nan_tx->message.status = (esp32_mquickjs_wifi_nan_message_tx_status_t){.identity = identity, .buffer_retired = true};
        s_nan_tx->message.context = context;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return available ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool esp32_mquickjs_wifi_nan_tx_message_status(uint32_t identity,
    esp32_mquickjs_wifi_nan_message_tx_status_t *status)
{
    if (!identity || !status) return false;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool exact = s_nan_tx && s_nan_tx->message.status.identity == identity;
    if (exact) *status = s_nan_tx->message.status;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return exact;
}

esp_err_t esp32_mquickjs_wifi_nan_tx_message_release(uint32_t identity)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool exact = identity && s_nan_tx && s_nan_tx->message.status.identity == identity;
    bool retired = exact && s_nan_tx->message.status.buffer_retired && !s_nan_tx->message.scope_task;
    if (retired) {
        memset(&s_nan_tx->message, 0, sizeof(s_nan_tx->message));
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return retired ? ESP_OK : exact ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
}

/* Hash-pinned API -> ioctl -> nan_send_followup_msg preserves the context
 * output pointer. Scope only allocations on this exact WiFi task and stack;
 * pairing/native calls with another context remain ordinary SD buffers. */
extern int __real_nan_send_followup_msg(uint32_t *context, void *parameters);
int __wrap_nan_send_followup_msg(uint32_t *context, void *parameters)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool exact = s_nan_tx && s_nan_tx->message.status.identity && s_nan_tx->message.context == context &&
        !s_nan_tx->message.scope_task && !s_nan_tx->message.status.entered;
    if (exact) { s_nan_tx->message.scope_task = task; s_nan_tx->message.status.entered = true; }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    int result = __real_nan_send_followup_msg(context, parameters);
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (exact) s_nan_tx->message.scope_task = NULL;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return result;
}

static void IRAM_ATTR nan_tx_entry_retire(nan_tx_entry_t *entry)
{
    if (!entry->recycled || entry->callbacks) return;
    if (entry->message_identity && s_nan_tx->message.status.identity == entry->message_identity &&
        s_nan_tx->message.status.ticket == entry->ticket) {
        s_nan_tx->message.status.buffer_retired = true;
        s_nan_tx->message.status.retired_us = esp_timer_get_time();
    }
    entry->buffer = NULL; entry->ticket = entry->message_identity = entry->ndp_identity = 0;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (entry->pairing_identity && entry->pairing_identity == s_nan_tx->pairing_result.identity)
        s_nan_tx->pairing_result.completed_us = esp_timer_get_time();
    entry->pairing_identity = 0; entry->authentication = entry->pairing_result_recorded = false;
#endif
    entry->service_id = entry->ndp_id = 0;
    entry->recycling = entry->recycled = entry->datapath = entry->data = false;
}

uint32_t esp32_mquickjs_wifi_nan_tx_callback_enter(void *buffer)
{
    uint32_t ticket = 0;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx) for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
        nan_tx_entry_t *entry = &s_nan_tx->entries[i];
        if (!entry->ticket || entry->buffer != buffer ||
            (!entry->message_identity && !entry->datapath
#if CONFIG_ESP_WIFI_NAN_PAIRING
                && !entry->pairing_identity
#endif
            ) || entry->recycled) continue;
        if (entry->callbacks != UINT32_MAX) { ++entry->callbacks; ticket = entry->ticket; }
        else s_nan_tx->error = ESP_ERR_INVALID_STATE;
        break;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return ticket;
}

void esp32_mquickjs_wifi_nan_tx_callback_leave(uint32_t ticket, bool success)
{
    if (!ticket) return;
    int64_t completed_us = esp_timer_get_time();
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx) for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
        nan_tx_entry_t *entry = &s_nan_tx->entries[i];
        if (entry->ticket != ticket || !entry->callbacks) continue;
        if (entry->message_identity == s_nan_tx->message.status.identity &&
            ticket == s_nan_tx->message.status.ticket && !s_nan_tx->message.status.tx_done) {
            s_nan_tx->message.status.tx_done = true;
            s_nan_tx->message.status.tx_succeeded = success;
            s_nan_tx->message.status.completed_us = completed_us;
        }
#if CONFIG_ESP_WIFI_NAN_PAIRING
        if (entry->pairing_identity == s_nan_tx->pairing_result.identity && entry->pairing_identity &&
            !entry->authentication && !entry->pairing_result_recorded) {
            entry->pairing_result_recorded = true;
            if (s_nan_tx->pairing_result.followups_pending) --s_nan_tx->pairing_result.followups_pending;
            else if (!s_nan_tx->pairing_result.error) s_nan_tx->pairing_result.error = ESP_ERR_INVALID_STATE;
            if (!success && !s_nan_tx->pairing_result.error) s_nan_tx->pairing_result.error = ESP_FAIL;
            s_nan_tx->pairing_result.completed_us = completed_us;
        }
#endif
        --entry->callbacks;
        nan_tx_entry_retire(entry);
        break;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

/* The hash-pinned C5 nan_alloc_sdf ABI takes five 32-bit words and returns an
 * EB pointer. Preserve every argument bit; the SDK retains frame construction,
 * encryption, queues and all physical buffer ownership. */
extern void *nan_alloc_sdf(uintptr_t body_out, uintptr_t destination, uintptr_t bssid,
    uintptr_t payload_length, uintptr_t protection);
extern void *nan_alloc_action(uintptr_t body_out, uintptr_t destination, uintptr_t bssid,
    uintptr_t payload_length, uintptr_t protection);
_Static_assert(sizeof(uintptr_t) == 4, "reviewed C5 NAN argument and EB layout");

esp_err_t esp32_mquickjs_wifi_nan_tx_open(void)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool available = !s_nan_tx && s_nan_tx_next_ticket;
    bool exhausted = !s_nan_tx_next_ticket;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (!available) return exhausted ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE;
    nan_tx_pool_t *pool = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*pool),
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!pool) return ESP_ERR_NO_MEM;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    available = !s_nan_tx && s_nan_tx_next_ticket;
    if (available) s_nan_tx = pool;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (!available) esp32_mquickjs_memory_payload_free(pool);
    return available ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_nan_tx_seal(void)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx) {
        s_nan_tx->closing = true;
        for (unsigned i = 0; i < 3; ++i) s_nan_tx->timers[i].active = false;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

esp_err_t esp32_mquickjs_wifi_nan_tx_close(void)
{
    esp32_mquickjs_wifi_nan_tx_seal();
    esp_err_t error = nan_timers_close();
    if (error != ESP_OK) return error;
    nan_tx_pool_t *pool = NULL;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool drained = true;
    if (s_nan_tx) {
        s_nan_tx->closing = true;
        if (s_nan_tx->message.scope_task || s_nan_tx->ndp_scope_task ||
            s_nan_tx->timer_callbacks || s_nan_tx->deleting_ndl) drained = false;
#if CONFIG_ESP_WIFI_NAN_PAIRING
        if (s_nan_tx->pairing.task) drained = false;
#endif
        for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i)
            if (s_nan_tx->entries[i].ticket) drained = false;
        if (drained) { pool = s_nan_tx; s_nan_tx = NULL; }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (pool) esp32_mquickjs_memory_payload_free(pool);
    return drained ? ESP_OK : ESP_ERR_TIMEOUT;
}

void esp32_mquickjs_wifi_nan_tx_status(esp32_mquickjs_wifi_nan_tx_status_t *status)
{
    if (!status) return;
    *status = (esp32_mquickjs_wifi_nan_tx_status_t){0};
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    status->identity_exhausted = !s_nan_tx_next_ticket;
    if (s_nan_tx) {
        status->reserved_bytes = sizeof(*s_nan_tx);
        status->closing = s_nan_tx->closing;
        status->error = s_nan_tx->error;
        status->submissions = s_nan_tx->submissions;
        status->rejected = s_nan_tx->rejected;
        for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
            nan_tx_entry_t *entry = &s_nan_tx->entries[i];
            if (!entry->ticket) continue;
            ++status->tracked;
            if (entry->datapath ? !entry->ndp_id : !entry->service_id) ++status->unidentified;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

bool esp32_mquickjs_wifi_nan_tx_service_drained(uint8_t service_id)
{
    bool drained = service_id != 0;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx) {
        if (s_nan_tx->error) drained = false;
        for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
            nan_tx_entry_t *entry = &s_nan_tx->entries[i];
            /* Data EBs carry no service lookup ID. Their exact NDP owner
             * prevents service cancellation until peer retirement; group EBs
             * belong to the Session and must not pin an unrelated service. */
            if (entry->ticket && !entry->data &&
                (!entry->service_id || entry->service_id == service_id)) drained = false;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return drained;
}

/* Both reviewed allocators have the same five-word ABI. NDP is separately
 * identified: metadata byte 8 is its NDP ID, never a service ID. Retain the
 * destination before the SDK can recycle the frame. Ordinary data is tracked
 * at nan_dp_post_tx; beacons remain outside this ledger. */
static void *nan_tx_allocate(uintptr_t body_out, uintptr_t destination, uintptr_t bssid,
    uintptr_t payload_length, uintptr_t protection, bool datapath)
{
    if (datapath && !destination) return NULL;
    nan_tx_entry_t *entry = NULL;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx && !s_nan_tx->closing && !s_nan_tx->error && s_nan_tx_next_ticket) {
        for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
            if (s_nan_tx->entries[i].ticket) continue;
            entry = &s_nan_tx->entries[i];
            *entry = (nan_tx_entry_t){.ticket = s_nan_tx_next_ticket++, .allocating = true, .datapath = datapath};
#if CONFIG_ESP_WIFI_NAN_PAIRING
            if (!datapath && s_nan_tx->pairing.task == task && !s_nan_tx->pairing.authentication) {
                entry->pairing_identity = s_nan_tx->pairing.identity;
                entry->service_id = s_nan_tx->pairing.service_id;
            }
#endif
            if (datapath) {
                memcpy(entry->peer, (const void *)destination, sizeof(entry->peer));
                nan_ndp_entry_t *operation = nan_ndp_peer_locked(0, entry->peer);
                if (operation && !operation->status.native_deleted) entry->ndp_identity = operation->status.identity;
            }
            if (!datapath && s_nan_tx->message.scope_task == task && s_nan_tx->message.status.identity) {
                if (s_nan_tx->message.status.ticket) {
                    entry->ticket = 0; entry->allocating = false; entry = NULL;
                    s_nan_tx->error = ESP_ERR_INVALID_RESPONSE;
                    break;
                }
                entry->message_identity = s_nan_tx->message.status.identity;
                s_nan_tx->message.status.ticket = entry->ticket;
                s_nan_tx->message.status.buffer_retired = false;
            }
            break;
        }
    }
    if (!entry && s_nan_tx && s_nan_tx->rejected != UINT32_MAX) ++s_nan_tx->rejected;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (!entry) return NULL;
    void *buffer = datapath ? nan_alloc_action(body_out, destination, bssid, payload_length, protection) :
        nan_alloc_sdf(body_out, destination, bssid, payload_length, protection);
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    entry->buffer = buffer;
    entry->allocating = false;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (buffer && entry->pairing_identity) ++s_nan_tx->pairing_result.followups_pending;
#endif
    if (entry->message_identity) s_nan_tx->message.status.allocated = buffer != NULL;
    if (!buffer) { entry->recycled = true; nan_tx_entry_retire(entry); }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return buffer;
}

void *esp32qjs_wifi_nan_alloc_sdf(uintptr_t body_out, uintptr_t destination, uintptr_t bssid,
    uintptr_t payload_length, uintptr_t protection)
{
    return nan_tx_allocate(body_out, destination, bssid, payload_length, protection, false);
}

void *esp32qjs_wifi_nan_alloc_action(uintptr_t body_out, uintptr_t destination, uintptr_t bssid,
    uintptr_t payload_length, uintptr_t protection)
{
    return nan_tx_allocate(body_out, destination, bssid, payload_length, protection, true);
}

bool esp32_mquickjs_wifi_nan_tx_datapath_drained(uint8_t ndp_id, const uint8_t peer[6])
{
    if (!ndp_id || !peer) return false;
    bool drained = true;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx) {
        if (s_nan_tx->error) drained = false;
        for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
            const nan_tx_entry_t *entry = &s_nan_tx->entries[i];
            if (entry->ticket && entry->datapath && !memcmp(entry->peer, peer, 6) &&
                (!entry->ndp_id || entry->ndp_id == ndp_id)) drained = false;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return drained;
}

void esp32_mquickjs_wifi_nan_tx_submitting(void *buffer)
{
    if (!buffer) return;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx) for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
        nan_tx_entry_t *entry = &s_nan_tx->entries[i];
        if (!entry->ticket || entry->buffer != buffer || entry->allocating || entry->recycling || entry->data) continue;
#if CONFIG_ESP_WIFI_NAN_PAIRING
        /* Auth metadata does not carry the SD service/subtype fields. */
        if (entry->authentication) {
            if (s_nan_tx->submissions != UINT32_MAX) ++s_nan_tx->submissions;
            break;
        }
#endif
        /* Producer completed construction before ic_tx_pkt. The pinned SD
         * callback uses EB+56 metadata+8 low byte as its service lookup key. */
        const uint8_t *metadata;
        memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
        uint8_t id = metadata ? metadata[8] : 0;
        uint8_t *saved_id = entry->datapath ? &entry->ndp_id : &entry->service_id;
        uint8_t subtype = metadata ? metadata[9] & 0x7f : 0;
        if (!metadata || (*saved_id && *saved_id != id) ||
            (entry->datapath && (!id || subtype < 3 || subtype > 7))) {
            s_nan_tx->error = ESP_ERR_INVALID_RESPONSE;
        } else {
            *saved_id = id;
        }
        if (s_nan_tx->submissions != UINT32_MAX) ++s_nan_tx->submissions;
        break;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

uint32_t IRAM_ATTR esp32_mquickjs_wifi_nan_tx_recycling(void *buffer)
{
    uint32_t ticket = 0;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (buffer && s_nan_tx) for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
        nan_tx_entry_t *entry = &s_nan_tx->entries[i];
        if (!entry->ticket || entry->buffer != buffer || entry->allocating || entry->recycling) continue;
        entry->recycling = true;
        ticket = entry->ticket;
        break;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return ticket;
}

void IRAM_ATTR esp32_mquickjs_wifi_nan_tx_recycled(uint32_t ticket)
{
    if (!ticket) return;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx) for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
        nan_tx_entry_t *entry = &s_nan_tx->entries[i];
        if (entry->ticket != ticket || !entry->recycling) continue;
        /* Never dereference the now-recycled EB or reuse its address as a
         * completion identity. A nested allocation can already own it. */
        entry->buffer = NULL;
        entry->recycled = true;
        nan_tx_entry_retire(entry);
        break;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}
#include "esp32_mquickjs_wifi_nan_ndp.inc"
#include "esp32_mquickjs_wifi_nan_timer.inc"
#include "esp32_mquickjs_wifi_nan_data.inc"
#if CONFIG_ESP_WIFI_NAN_PAIRING
#include "esp32_mquickjs_wifi_nan_pairing_tx.inc"
#endif
#endif
