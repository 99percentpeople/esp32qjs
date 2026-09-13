#include "esp32_mquickjs_wifi_raw_tx_broker.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_wifi_raw_tx_pump.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <string.h>

#define RAW_TX_SLOTS ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_MAX_IN_FLIGHT
#define RAW_TX_NONE UINT8_MAX

typedef struct {
    esp32_mquickjs_wifi_raw_tx_broker_status_t native;
    uint8_t *buffer;
    void *descriptor;
    esp32_mquickjs_wifi_raw_tx_interface_t interface;
} raw_tx_record_t;
static portMUX_TYPE s_raw_tx_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    /* Registration/callback counters are boot-owned; per-packet records never
     * point to JS, a Session, or an executor task. */
    esp32_mquickjs_wifi_raw_tx_broker_status_t status;
    uint32_t next_identity;
    uint8_t submitting;
    raw_tx_record_t records[RAW_TX_SLOTS];
} s_raw_tx = {.next_identity = 1, .submitting = RAW_TX_NONE};

static void raw_tx_count(uint32_t *value) { if (*value != UINT32_MAX) ++*value; }
static void raw_tx_note_correlation_locked(const char *reason, unsigned index)
{
    if (s_raw_tx.status.correlation_failure_reason != NULL) return;
    s_raw_tx.status.correlation_failure_reason = reason;
    s_raw_tx.status.correlation_failure_generation = s_raw_tx.status.generation;
    s_raw_tx.status.correlation_failure_identity = index < RAW_TX_SLOTS
        ? s_raw_tx.records[index].native.token.identity : 0U;
}
static unsigned raw_tx_find(const esp32_mquickjs_wifi_raw_tx_token_t *token)
{
    if (token == NULL || token->identity == 0U) return RAW_TX_NONE;
    for (unsigned i = 0; i < RAW_TX_SLOTS; ++i) {
        const esp32_mquickjs_wifi_raw_tx_token_t *actual = &s_raw_tx.records[i].native.token;
        if (actual->identity == token->identity && actual->generation == token->generation &&
            actual->radio_lease_identity == token->radio_lease_identity) return i;
    }
    return RAW_TX_NONE;
}
static unsigned raw_tx_first(void)
{
    for (unsigned i = 0; i < RAW_TX_SLOTS; ++i)
        if (s_raw_tx.records[i].native.token.identity != 0U) return i;
    return RAW_TX_NONE;
}
static void raw_tx_status_locked(unsigned index, esp32_mquickjs_wifi_raw_tx_broker_status_t *out)
{
    *out = index == RAW_TX_NONE ? (esp32_mquickjs_wifi_raw_tx_broker_status_t){0} : s_raw_tx.records[index].native;
    out->registered = s_raw_tx.status.registered;
    out->registration_uncertain = s_raw_tx.status.registration_uncertain;
    out->unregister_written = s_raw_tx.status.unregister_written;
    out->control_busy = s_raw_tx.status.control_busy;
    out->generation = s_raw_tx.status.generation;
    out->callbacks_active = s_raw_tx.status.callbacks_active;
    out->orphan_callbacks = s_raw_tx.status.orphan_callbacks;
    out->invalid_callbacks = s_raw_tx.status.invalid_callbacks;
    out->mismatched_callbacks = s_raw_tx.status.mismatched_callbacks;
    out->duplicate_callbacks = s_raw_tx.status.duplicate_callbacks;
    out->correlation_failure_reason = s_raw_tx.status.correlation_failure_reason;
    out->correlation_failure_identity = s_raw_tx.status.correlation_failure_identity;
    out->correlation_failure_generation = s_raw_tx.status.correlation_failure_generation;
    out->cleanup_error = s_raw_tx.status.cleanup_error;
    out->identity_exhausted = s_raw_tx.next_identity == 0U;
    out->max_in_flight = RAW_TX_SLOTS;
    out->in_flight = 0;
    for (unsigned i = 0; i < RAW_TX_SLOTS; ++i)
        if (s_raw_tx.records[i].native.token.identity != 0U) ++out->in_flight;
}

static void raw_tx_complete(const esp_80211_tx_info_t *info, void *descriptor, bool exact)
{
    portENTER_CRITICAL(&s_raw_tx_lock);
    if (s_raw_tx.status.callbacks_active == UINT32_MAX) {
        raw_tx_note_correlation_locked("callback-overflow", RAW_TX_NONE);
        s_raw_tx.status.registration_uncertain = true;
        portEXIT_CRITICAL(&s_raw_tx_lock); return;
    }
    ++s_raw_tx.status.callbacks_active;
    portEXIT_CRITICAL(&s_raw_tx_lock);
    uint64_t now = (uint64_t)esp_timer_get_time();
    esp32_mquickjs_wifi_raw_tx_snapshot_t snapshot;
    bool valid = esp32_mquickjs_wifi_raw_tx_snapshot(info, now, &snapshot);
    portENTER_CRITICAL(&s_raw_tx_lock);
    unsigned index = RAW_TX_NONE;
    if (exact) {
        for (unsigned i = 0; i < RAW_TX_SLOTS; ++i)
            if (descriptor != NULL && s_raw_tx.records[i].descriptor == descriptor) { index = i; break; }
    } else index = raw_tx_first();
    if (index == RAW_TX_NONE || !s_raw_tx.records[index].native.operation_active) {
        raw_tx_note_correlation_locked("orphan-callback", RAW_TX_NONE);
        raw_tx_count(&s_raw_tx.status.orphan_callbacks);
        s_raw_tx.status.registration_uncertain = true;
    } else {
        raw_tx_record_t *record = &s_raw_tx.records[index];
        esp32_mquickjs_wifi_raw_tx_broker_status_t *native = &record->native;
        if (!valid) {
            raw_tx_count(&s_raw_tx.status.invalid_callbacks);
            if (exact) raw_tx_note_correlation_locked(info == NULL ? "invalid-info" : "invalid-interface", index);
        }
        else if (snapshot.interface != record->interface ||
            (snapshot.destination_available && memcmp(snapshot.destination, record->buffer + 4, 6) != 0) ||
            (snapshot.source_available && memcmp(snapshot.source, record->buffer + 10, 6) != 0)) {
            raw_tx_count(&s_raw_tx.status.mismatched_callbacks);
            if (exact) raw_tx_note_correlation_locked(snapshot.interface != record->interface ? "interface-mismatch" :
                (snapshot.destination_available && memcmp(snapshot.destination, record->buffer + 4, 6) != 0)
                    ? "destination-mismatch" : "source-mismatch", index);
        } else if (native->driver_completed) {
            raw_tx_count(&s_raw_tx.status.duplicate_callbacks);
            raw_tx_note_correlation_locked("duplicate-callback", index);
            native->correlation_fault = native->quarantined = true;
        } else {
            native->completion = snapshot;
            native->driver_completed = true;
            /* SDK may recycle the descriptor after this callback. Detach it
             * now so subsequent allocation cannot alias an unretired result. */
            record->descriptor = NULL;
            if (native->submit_returned && !native->driver_accepted) {
                raw_tx_note_correlation_locked("completion-after-rejection", index);
                native->correlation_fault = native->quarantined = true;
            } else native->quarantined = false;
        }
    }
    if (exact) {
        if (index != RAW_TX_NONE && !s_raw_tx.records[index].native.driver_completed) {
            s_raw_tx.records[index].descriptor = NULL;
            s_raw_tx.records[index].native.correlation_fault = true;
            s_raw_tx.records[index].native.quarantined = true;
            s_raw_tx.status.registration_uncertain = true;
        }
        if (s_raw_tx.status.registration_uncertain) {
            for (unsigned i = 0; i < RAW_TX_SLOTS; ++i)
                if (s_raw_tx.records[i].native.operation_active && !s_raw_tx.records[i].native.driver_completed)
                    s_raw_tx.records[i].native.correlation_fault = s_raw_tx.records[i].native.quarantined = true;
        }
    }
    --s_raw_tx.status.callbacks_active;
    portEXIT_CRITICAL(&s_raw_tx_lock);
    esp32_mquickjs_wifi_raw_tx_pump_wake();
}

static void raw_tx_callback(const esp_80211_tx_info_t *info)
{
#if ESP32_MQUICKJS_RAW_TX_DESCRIPTOR_IDENTITY
    /* The reviewed descriptor hook delivered the exact result already. Keep
     * registration non-null so the SDK builds its ordinary callback metadata. */
    (void)info;
#else
    raw_tx_complete(info, NULL, false);
#endif
}

#if ESP32_MQUICKJS_RAW_TX_DESCRIPTOR_IDENTITY
extern void *ic_ebuf_alloc(const void *bytes, int kind, int length);
extern void ieee80211_get_tx_info_from_eb(void *descriptor, esp_80211_tx_info_t *info);
extern void ieee80211_copy_eb_header(void *destination, void *source);
void esp32qjs_raw_tx_copy_header(void *destination, void *source)
{
    ieee80211_copy_eb_header(destination, source);
    /* HMAC can replace a cache descriptor even for a one-shot packet. The
     * reviewed caller recycles source immediately after this hook and only
     * then dispatches destination. Unrelated SDK traffic has no binding. */
    portENTER_CRITICAL(&s_raw_tx_lock);
    unsigned index = RAW_TX_NONE;
    for (unsigned i = 0; i < RAW_TX_SLOTS; ++i)
        if (source != NULL && s_raw_tx.records[i].descriptor == source) { index = i; break; }
    if (index != RAW_TX_NONE && destination != source) {
        bool collision = destination == NULL;
        for (unsigned i = 0; i < RAW_TX_SLOTS; ++i)
            if (destination != NULL && s_raw_tx.records[i].descriptor == destination) collision = true;
        s_raw_tx.records[index].descriptor = collision ? NULL : destination;
        if (collision) {
            raw_tx_note_correlation_locked("descriptor-transfer", index);
            s_raw_tx.status.registration_uncertain = true;
            for (unsigned i = 0; i < RAW_TX_SLOTS; ++i)
                if (s_raw_tx.records[i].native.operation_active && !s_raw_tx.records[i].native.driver_completed)
                    s_raw_tx.records[i].native.correlation_fault = s_raw_tx.records[i].native.quarantined = true;
        }
    }
    portEXIT_CRITICAL(&s_raw_tx_lock);
}
void *esp32qjs_raw_tx_alloc(const void *bytes, int kind, int length)
{
    void *descriptor = ic_ebuf_alloc(bytes, kind, length);
    if (descriptor == NULL) return NULL;
    portENTER_CRITICAL(&s_raw_tx_lock);
    unsigned index = s_raw_tx.submitting;
    if (index < RAW_TX_SLOTS && s_raw_tx.records[index].buffer == bytes &&
        s_raw_tx.records[index].native.byte_length == length && s_raw_tx.records[index].descriptor == NULL) {
        for (unsigned i = 0; i < RAW_TX_SLOTS; ++i)
            if (s_raw_tx.records[i].descriptor == descriptor) {
                raw_tx_note_correlation_locked("descriptor-reused", index);
                s_raw_tx.status.registration_uncertain = true;
            }
        s_raw_tx.records[index].descriptor = descriptor;
    } else {
        raw_tx_note_correlation_locked("allocation-binding", index);
        s_raw_tx.status.registration_uncertain = true;
    }
    portEXIT_CRITICAL(&s_raw_tx_lock);
    return descriptor;
}
void esp32qjs_raw_tx_info(void *descriptor, esp_80211_tx_info_t *info)
{
    ieee80211_get_tx_info_from_eb(descriptor, info);
    raw_tx_complete(info, descriptor, true);
}
#endif

esp_err_t esp32_mquickjs_wifi_raw_tx_broker_register(uint32_t generation)
{
    if (generation == 0) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_raw_tx_lock);
    for (unsigned i = 0; i < RAW_TX_SLOTS; ++i) if (s_raw_tx.records[i].native.native_terminated) {
        portEXIT_CRITICAL(&s_raw_tx_lock); return ESP_ERR_INVALID_STATE;
    }
    if (s_raw_tx.status.registered && s_raw_tx.status.generation == generation &&
        !s_raw_tx.status.control_busy && !s_raw_tx.status.registration_uncertain && !s_raw_tx.status.unregister_written) {
        portEXIT_CRITICAL(&s_raw_tx_lock); return ESP_OK;
    }
    if (s_raw_tx.status.generation != 0 || s_raw_tx.status.control_busy || s_raw_tx.status.callbacks_active != 0) {
        portEXIT_CRITICAL(&s_raw_tx_lock); return ESP_ERR_INVALID_STATE;
    }
    s_raw_tx.status.generation = generation;
    s_raw_tx.status.correlation_failure_reason = NULL;
    s_raw_tx.status.correlation_failure_identity = s_raw_tx.status.correlation_failure_generation = 0U;
    s_raw_tx.status.control_busy = true;
    portEXIT_CRITICAL(&s_raw_tx_lock);
    esp_err_t error = esp_wifi_register_80211_tx_cb(raw_tx_callback);
    portENTER_CRITICAL(&s_raw_tx_lock);
    s_raw_tx.status.control_busy = false;
    s_raw_tx.status.registered = error == ESP_OK;
    s_raw_tx.status.registration_uncertain = error != ESP_OK;
    s_raw_tx.status.cleanup_error = error;
    portEXIT_CRITICAL(&s_raw_tx_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_raw_tx_broker_submit(uint32_t generation, uint32_t radio_lease_identity,
    const uint8_t *bytes, size_t length, const esp32_mquickjs_wifi_raw_tx_validation_policy_t *policy,
    esp32_mquickjs_wifi_raw_tx_token_t *token, esp32_mquickjs_wifi_raw_tx_validation_t *validation)
{
    if (generation == 0 || radio_lease_identity == 0 || token == NULL || validation == NULL ||
        token->generation != 0 || token->identity != 0 || token->radio_lease_identity != 0) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_raw_tx_validated_frame_t frame;
    *validation = esp32_mquickjs_wifi_raw_tx_validate(bytes, length, policy, &frame);
    if (*validation != ESP32_MQUICKJS_WIFI_RAW_TX_VALID) return ESP_ERR_INVALID_ARG;
    uint8_t *copy = esp32_mquickjs_memory_wireless_alloc("wifi.raw-tx", length, ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_TX);
    if (copy == NULL) return ESP_ERR_NO_MEM;
    memcpy(copy, bytes, length);
    uint64_t now = (uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&s_raw_tx_lock);
    unsigned index = 0;
    while (index < RAW_TX_SLOTS && s_raw_tx.records[index].native.token.identity != 0U) ++index;
    if (s_raw_tx.status.generation != generation || !s_raw_tx.status.registered ||
        s_raw_tx.status.registration_uncertain || s_raw_tx.status.unregister_written || s_raw_tx.status.control_busy ||
        index == RAW_TX_SLOTS || s_raw_tx.submitting != RAW_TX_NONE || s_raw_tx.next_identity == 0) {
        portEXIT_CRITICAL(&s_raw_tx_lock); esp32_mquickjs_memory_payload_free(copy); return ESP_ERR_INVALID_STATE;
    }
    raw_tx_record_t *record = &s_raw_tx.records[index];
    memset(record, 0, sizeof(*record));
    record->buffer = copy;
    record->interface = policy->interface;
    record->native.operation_active = true;
    record->native.submitted_at_us = now;
    record->native.byte_length = frame.byte_length;
    record->native.frame_type = frame.frame_type;
    record->native.token = (esp32_mquickjs_wifi_raw_tx_token_t){generation, s_raw_tx.next_identity++, radio_lease_identity};
    *token = record->native.token;
    s_raw_tx.submitting = index;
    portEXIT_CRITICAL(&s_raw_tx_lock);
    esp_err_t error = esp_wifi_80211_tx(policy->interface == ESP32_MQUICKJS_WIFI_RAW_TX_STATION ? WIFI_IF_STA : WIFI_IF_AP,
        copy, (int)length, policy->driver_sequence);
    portENTER_CRITICAL(&s_raw_tx_lock);
    s_raw_tx.submitting = RAW_TX_NONE;
    record->native.submit_returned = true;
    record->native.driver_accepted = error == ESP_OK;
    record->native.submit_error = error;
    if (error != ESP_OK) {
        record->native.quarantined = true;
        if (record->native.driver_completed) {
            raw_tx_note_correlation_locked("completion-after-rejection", index);
            record->native.correlation_fault = true;
        }
    }
    portEXIT_CRITICAL(&s_raw_tx_lock);
    return error;
}

bool esp32_mquickjs_wifi_raw_tx_broker_abandon(const esp32_mquickjs_wifi_raw_tx_token_t *token)
{
    portENTER_CRITICAL(&s_raw_tx_lock);
    unsigned index = raw_tx_find(token);
    if (index != RAW_TX_NONE && !s_raw_tx.records[index].native.native_terminated) {
        esp32_mquickjs_wifi_raw_tx_broker_status_t *native = &s_raw_tx.records[index].native;
        native->abandoned = true;
        if (!native->driver_completed || native->correlation_fault) native->quarantined = true;
    }
    portEXIT_CRITICAL(&s_raw_tx_lock);
    return index != RAW_TX_NONE;
}

bool esp32_mquickjs_wifi_raw_tx_broker_retire(esp32_mquickjs_wifi_raw_tx_token_t *token)
{
    portENTER_CRITICAL(&s_raw_tx_lock);
    unsigned index = raw_tx_find(token);
    esp32_mquickjs_wifi_raw_tx_broker_status_t *native = index == RAW_TX_NONE ? NULL : &s_raw_tx.records[index].native;
    bool ready = native != NULL && s_raw_tx.status.callbacks_active == 0 &&
        (native->native_terminated || (native->submit_returned && native->driver_completed && !native->correlation_fault));
    uint8_t *buffer = NULL;
    if (ready) {
        buffer = s_raw_tx.records[index].buffer;
        memset(&s_raw_tx.records[index], 0, sizeof(s_raw_tx.records[index]));
        memset(token, 0, sizeof(*token));
    }
    portEXIT_CRITICAL(&s_raw_tx_lock);
    if (buffer != NULL) esp32_mquickjs_memory_payload_free(buffer);
    return ready;
}

void esp32_mquickjs_wifi_raw_tx_broker_status(esp32_mquickjs_wifi_raw_tx_broker_status_t *output)
{
    if (output == NULL) return;
    portENTER_CRITICAL(&s_raw_tx_lock);
    raw_tx_status_locked(raw_tx_first(), output);
    portEXIT_CRITICAL(&s_raw_tx_lock);
}
bool esp32_mquickjs_wifi_raw_tx_broker_result(const esp32_mquickjs_wifi_raw_tx_token_t *token,
    esp32_mquickjs_wifi_raw_tx_broker_status_t *output)
{
    if (output == NULL) return false;
    portENTER_CRITICAL(&s_raw_tx_lock);
    unsigned index = raw_tx_find(token);
    raw_tx_status_locked(index, output);
    portEXIT_CRITICAL(&s_raw_tx_lock);
    return index != RAW_TX_NONE;
}
uint32_t esp32_mquickjs_wifi_raw_tx_broker_owner_identity(uint32_t lease_identity)
{
    uint32_t identity = 0;
    portENTER_CRITICAL(&s_raw_tx_lock);
    for (unsigned i = 0; i < RAW_TX_SLOTS; ++i)
        if (s_raw_tx.records[i].native.token.radio_lease_identity == lease_identity &&
            s_raw_tx.records[i].native.token.identity != 0U) { identity = s_raw_tx.records[i].native.token.identity; break; }
    portEXIT_CRITICAL(&s_raw_tx_lock);
    return identity;
}

static esp_err_t raw_tx_unregister(uint32_t generation, const esp32_mquickjs_wifi_raw_tx_token_t *recovery)
{
    portENTER_CRITICAL(&s_raw_tx_lock);
    unsigned index = recovery == NULL ? RAW_TX_NONE : raw_tx_find(recovery);
    bool owner_ready = recovery != NULL ? index != RAW_TX_NONE && s_raw_tx.records[index].native.operation_active &&
        s_raw_tx.records[index].native.submit_returned : raw_tx_first() == RAW_TX_NONE;
    if (generation == 0 || s_raw_tx.status.generation != generation || !owner_ready ||
        s_raw_tx.status.control_busy || s_raw_tx.status.callbacks_active != 0 || s_raw_tx.submitting != RAW_TX_NONE) {
        portEXIT_CRITICAL(&s_raw_tx_lock); return ESP_ERR_INVALID_STATE;
    }
    bool write = !s_raw_tx.status.unregister_written;
    s_raw_tx.status.control_busy = true;
    portEXIT_CRITICAL(&s_raw_tx_lock);
    esp_err_t error = write ? esp_wifi_register_80211_tx_cb(NULL) : ESP_OK;
    portENTER_CRITICAL(&s_raw_tx_lock);
    if (error == ESP_OK) {
        s_raw_tx.status.unregister_written = true;
        s_raw_tx.status.registered = s_raw_tx.status.registration_uncertain = false;
        if (s_raw_tx.status.callbacks_active != 0) error = ESP_ERR_TIMEOUT;
    } else s_raw_tx.status.registration_uncertain = true;
    s_raw_tx.status.control_busy = false;
    s_raw_tx.status.cleanup_error = error;
    portEXIT_CRITICAL(&s_raw_tx_lock);
    return error;
}
esp_err_t esp32_mquickjs_wifi_raw_tx_broker_unregister(uint32_t generation) { return raw_tx_unregister(generation, NULL); }
esp_err_t esp32_mquickjs_wifi_raw_tx_broker_quiesce(uint32_t generation, const esp32_mquickjs_wifi_raw_tx_token_t *token)
{
    return token == NULL ? ESP_ERR_INVALID_ARG : raw_tx_unregister(generation, token);
}

bool esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(uint32_t generation)
{
    uint8_t *buffers[RAW_TX_SLOTS] = {0};
    portENTER_CRITICAL(&s_raw_tx_lock);
    unsigned first = raw_tx_first();
    bool ready = generation != 0 && (s_raw_tx.status.generation == generation ||
        (first != RAW_TX_NONE && s_raw_tx.records[first].native.native_terminated &&
         s_raw_tx.records[first].native.token.generation == generation)) &&
        !s_raw_tx.status.control_busy && s_raw_tx.status.callbacks_active == 0 && s_raw_tx.submitting == RAW_TX_NONE;
    if (ready) {
        for (unsigned i = 0; i < RAW_TX_SLOTS; ++i) {
            raw_tx_record_t *record = &s_raw_tx.records[i];
            buffers[i] = record->buffer;
            record->buffer = NULL;
            record->descriptor = NULL;
            if (record->native.token.identity != 0U) {
                record->native.operation_active = false;
                record->native.native_terminated = true;
            }
        }
        s_raw_tx.status.generation = 0;
        s_raw_tx.status.registered = s_raw_tx.status.registration_uncertain = s_raw_tx.status.unregister_written = false;
        s_raw_tx.status.cleanup_error = ESP_OK;
    }
    portEXIT_CRITICAL(&s_raw_tx_lock);
    for (unsigned i = 0; i < RAW_TX_SLOTS; ++i) if (buffers[i] != NULL) esp32_mquickjs_memory_payload_free(buffers[i]);
    return ready;
}
#endif
