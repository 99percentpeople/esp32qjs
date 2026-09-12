#include "esp32_mquickjs_wifi_raw_tx_broker.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <string.h>

static portMUX_TYPE s_raw_tx_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    esp32_mquickjs_wifi_raw_tx_broker_status_t status;
    uint32_t next_identity;
    uint8_t *buffer;
    esp32_mquickjs_wifi_raw_tx_interface_t interface;
} s_raw_tx = {.next_identity = 1};

static void raw_tx_count(uint32_t *value) { if (*value != UINT32_MAX) ++*value; }
static bool raw_tx_token_matches(const esp32_mquickjs_wifi_raw_tx_token_t *token)
{
    return token != NULL && (s_raw_tx.status.operation_active || s_raw_tx.status.native_terminated) && token->identity != 0 &&
        token->identity == s_raw_tx.status.token.identity && token->generation == s_raw_tx.status.token.generation &&
        token->radio_lease_identity == s_raw_tx.status.token.radio_lease_identity;
}
static void raw_tx_clear_operation(void)
{
    s_raw_tx.buffer = NULL;
    s_raw_tx.status.operation_active = false;
    s_raw_tx.status.native_terminated = false;
    s_raw_tx.status.submit_returned = s_raw_tx.status.driver_accepted = s_raw_tx.status.driver_completed = false;
    s_raw_tx.status.abandoned = s_raw_tx.status.quarantined = s_raw_tx.status.correlation_fault = false;
    s_raw_tx.status.submit_error = ESP_OK;
    s_raw_tx.status.submitted_at_us = 0;
    s_raw_tx.status.byte_length = 0;
    s_raw_tx.status.frame_type = 0;
    memset(&s_raw_tx.status.token, 0, sizeof(s_raw_tx.status.token));
    memset(&s_raw_tx.status.completion, 0, sizeof(s_raw_tx.status.completion));
}

static void raw_tx_callback(const esp_80211_tx_info_t *info)
{
    portENTER_CRITICAL(&s_raw_tx_lock);
    if (s_raw_tx.status.callbacks_active == UINT32_MAX) {
        s_raw_tx.status.correlation_fault = true;
        s_raw_tx.status.quarantined = true;
        s_raw_tx.status.registration_uncertain = true;
        portEXIT_CRITICAL(&s_raw_tx_lock); return;
    }
    ++s_raw_tx.status.callbacks_active;
    portEXIT_CRITICAL(&s_raw_tx_lock);
    uint64_t now = (uint64_t)esp_timer_get_time();
    esp32_mquickjs_wifi_raw_tx_snapshot_t snapshot;
    bool valid = esp32_mquickjs_wifi_raw_tx_snapshot(info, now, &snapshot);
    portENTER_CRITICAL(&s_raw_tx_lock);
    if (!s_raw_tx.status.operation_active) {
        raw_tx_count(&s_raw_tx.status.orphan_callbacks);
        /* An unexpected completion cannot be assigned to a future request.
         * Keep registration unavailable until the physical deinit fence. */
        s_raw_tx.status.registration_uncertain = true;
    }
    else if (!valid) raw_tx_count(&s_raw_tx.status.invalid_callbacks);
    else if (snapshot.interface != s_raw_tx.interface ||
        (snapshot.destination_available && memcmp(snapshot.destination, s_raw_tx.buffer + 4, 6) != 0) ||
        (snapshot.source_available && memcmp(snapshot.source, s_raw_tx.buffer + 10, 6) != 0)) {
        /* A mismatch is not terminal for the current operation. Addresses are
         * only a rejection check; matching bytes never serve as an identity. */
        raw_tx_count(&s_raw_tx.status.mismatched_callbacks);
    } else if (s_raw_tx.status.driver_completed) {
        raw_tx_count(&s_raw_tx.status.duplicate_callbacks);
        s_raw_tx.status.correlation_fault = true;
        s_raw_tx.status.quarantined = true;
    } else {
        s_raw_tx.status.completion = snapshot;
        s_raw_tx.status.driver_completed = true;
        if (s_raw_tx.status.submit_returned && !s_raw_tx.status.driver_accepted) {
            s_raw_tx.status.correlation_fault = true;
            s_raw_tx.status.quarantined = true;
        } else s_raw_tx.status.quarantined = false;
    }
    --s_raw_tx.status.callbacks_active;
    portEXIT_CRITICAL(&s_raw_tx_lock);
}

esp_err_t esp32_mquickjs_wifi_raw_tx_broker_register(uint32_t generation)
{
    if (generation == 0) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_raw_tx_lock);
    if (s_raw_tx.status.native_terminated) {
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
    if (s_raw_tx.status.generation != generation || !s_raw_tx.status.registered ||
        s_raw_tx.status.registration_uncertain || s_raw_tx.status.unregister_written || s_raw_tx.status.control_busy ||
        s_raw_tx.status.operation_active || s_raw_tx.status.native_terminated ||
        s_raw_tx.status.callbacks_active != 0 || s_raw_tx.next_identity == 0) {
        s_raw_tx.status.identity_exhausted = s_raw_tx.next_identity == 0;
        portEXIT_CRITICAL(&s_raw_tx_lock); esp32_mquickjs_memory_payload_free(copy); return ESP_ERR_INVALID_STATE;
    }
    raw_tx_clear_operation();
    s_raw_tx.buffer = copy;
    s_raw_tx.interface = policy->interface;
    s_raw_tx.status.operation_active = true;
    s_raw_tx.status.submitted_at_us = now;
    s_raw_tx.status.byte_length = frame.byte_length;
    s_raw_tx.status.frame_type = frame.frame_type;
    s_raw_tx.status.token = (esp32_mquickjs_wifi_raw_tx_token_t){generation, s_raw_tx.next_identity++, radio_lease_identity};
    s_raw_tx.status.identity_exhausted = s_raw_tx.next_identity == 0;
    *token = s_raw_tx.status.token;
    portEXIT_CRITICAL(&s_raw_tx_lock);
    esp_err_t error = esp_wifi_80211_tx(policy->interface == ESP32_MQUICKJS_WIFI_RAW_TX_STATION ? WIFI_IF_STA : WIFI_IF_AP,
        copy, (int)length, policy->driver_sequence);
    portENTER_CRITICAL(&s_raw_tx_lock);
    s_raw_tx.status.submit_returned = true;
    s_raw_tx.status.driver_accepted = error == ESP_OK;
    s_raw_tx.status.submit_error = error;
    if (error != ESP_OK) {
        /* The API error is preserved. This boundary cannot prove when a driver
         * failure stopped referencing the submitted buffer, so never free/reuse
         * it merely because send returned an error. Radio deinit can fence it. */
        s_raw_tx.status.quarantined = true;
        if (s_raw_tx.status.driver_completed) s_raw_tx.status.correlation_fault = true;
    }
    portEXIT_CRITICAL(&s_raw_tx_lock);
    return error;
}

bool esp32_mquickjs_wifi_raw_tx_broker_abandon(const esp32_mquickjs_wifi_raw_tx_token_t *token)
{
    portENTER_CRITICAL(&s_raw_tx_lock);
    bool valid = raw_tx_token_matches(token);
    if (valid && !s_raw_tx.status.native_terminated) {
        s_raw_tx.status.abandoned = true;
        if (!s_raw_tx.status.driver_completed || s_raw_tx.status.correlation_fault) s_raw_tx.status.quarantined = true;
    }
    portEXIT_CRITICAL(&s_raw_tx_lock);
    return valid;
}

bool esp32_mquickjs_wifi_raw_tx_broker_retire(esp32_mquickjs_wifi_raw_tx_token_t *token)
{
    portENTER_CRITICAL(&s_raw_tx_lock);
    bool ready = raw_tx_token_matches(token) && s_raw_tx.status.callbacks_active == 0 &&
        (s_raw_tx.status.native_terminated || (s_raw_tx.status.submit_returned &&
         s_raw_tx.status.driver_completed && !s_raw_tx.status.correlation_fault));
    uint8_t *buffer = NULL;
    if (ready) { buffer = s_raw_tx.buffer; raw_tx_clear_operation(); memset(token, 0, sizeof(*token)); }
    portEXIT_CRITICAL(&s_raw_tx_lock);
    if (buffer != NULL) esp32_mquickjs_memory_payload_free(buffer);
    return ready;
}

void esp32_mquickjs_wifi_raw_tx_broker_status(esp32_mquickjs_wifi_raw_tx_broker_status_t *output)
{
    if (output == NULL) return;
    portENTER_CRITICAL(&s_raw_tx_lock);
    *output = s_raw_tx.status;
    portEXIT_CRITICAL(&s_raw_tx_lock);
}

static esp_err_t raw_tx_unregister(uint32_t generation,
    const esp32_mquickjs_wifi_raw_tx_token_t *recovery)
{
    portENTER_CRITICAL(&s_raw_tx_lock);
    bool owner_ready = recovery != NULL
        ? raw_tx_token_matches(recovery) && s_raw_tx.status.operation_active && s_raw_tx.status.submit_returned
        : !s_raw_tx.status.operation_active;
    if (generation == 0 || s_raw_tx.status.generation != generation || !owner_ready || s_raw_tx.status.native_terminated ||
        s_raw_tx.status.control_busy || s_raw_tx.status.callbacks_active != 0) {
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
        /* A zero active count is not proof that the SDK cannot subsequently
         * enter a previously dispatched callback. Keep this registration
         * generation sealed until Radio's successful physical deinit fence. */
    } else s_raw_tx.status.registration_uncertain = true;
    s_raw_tx.status.control_busy = false;
    s_raw_tx.status.cleanup_error = error;
    portEXIT_CRITICAL(&s_raw_tx_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_raw_tx_broker_unregister(uint32_t generation)
{
    return raw_tx_unregister(generation, NULL);
}

esp_err_t esp32_mquickjs_wifi_raw_tx_broker_quiesce(uint32_t generation,
    const esp32_mquickjs_wifi_raw_tx_token_t *token)
{
    if (token == NULL) return ESP_ERR_INVALID_ARG;
    return raw_tx_unregister(generation, token);
}

bool esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(uint32_t generation)
{
    portENTER_CRITICAL(&s_raw_tx_lock);
    bool ready = generation != 0 && (s_raw_tx.status.generation == generation ||
        (s_raw_tx.status.native_terminated && s_raw_tx.status.token.generation == generation)) && !s_raw_tx.status.control_busy &&
        (!s_raw_tx.status.operation_active || s_raw_tx.status.submit_returned) && s_raw_tx.status.callbacks_active == 0;
    uint8_t *buffer = NULL;
    if (ready) {
        buffer = s_raw_tx.buffer;
        if (s_raw_tx.status.operation_active || s_raw_tx.status.native_terminated) {
            /* Deinit ended native references; it did not produce a completion.
             * Preserve the token/result for the queue/periodic/retired owner.
             * Repeated suffix calls cannot erase unconsumed termination proof. */
            s_raw_tx.buffer = NULL;
            s_raw_tx.status.operation_active = false;
            s_raw_tx.status.native_terminated = true;
        } else raw_tx_clear_operation();
        s_raw_tx.status.generation = 0;
        s_raw_tx.status.registered = s_raw_tx.status.registration_uncertain = s_raw_tx.status.unregister_written = false;
        s_raw_tx.status.cleanup_error = ESP_OK;
    }
    portEXIT_CRITICAL(&s_raw_tx_lock);
    if (buffer != NULL) esp32_mquickjs_memory_payload_free(buffer);
    return ready;
}
#endif
