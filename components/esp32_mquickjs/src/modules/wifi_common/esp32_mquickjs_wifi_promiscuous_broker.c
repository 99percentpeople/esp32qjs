#include "esp32_mquickjs_wifi_promiscuous_broker.h"

#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include <string.h>
#include "freertos/FreeRTOS.h"

enum { WIFI_PROMISCUOUS_RESERVED = 1, WIFI_PROMISCUOUS_ACTIVE, WIFI_PROMISCUOUS_CLOSING };
typedef struct {
    uint32_t identity;
    uint8_t phase, entered;
    esp32_mquickjs_wifi_promiscuous_subscriber_t *subscriber;
} wifi_promiscuous_slot_t;

/* Only bounded pointers/control facts live permanently here; filters and
 * queues belong to retained session control storage. Never reset identities
 * on runtime teardown, Radio restart, or the last subscriber's close. */
static struct {
    portMUX_TYPE lock;
    wifi_promiscuous_slot_t slots[ESP32_MQUICKJS_WIFI_PROMISCUOUS_MAX_SUBSCRIBERS];
    uint32_t next_identity, overlapping_dispatches;
    bool dispatch_busy;
} s_promiscuous = { .lock = portMUX_INITIALIZER_UNLOCKED, .next_identity = 1 };

static wifi_promiscuous_slot_t *wifi_promiscuous_find(uint32_t identity)
{
    if (identity == 0) return NULL;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_PROMISCUOUS_MAX_SUBSCRIBERS; ++i)
        if (s_promiscuous.slots[i].identity == identity) return &s_promiscuous.slots[i];
    return NULL;
}

esp32_mquickjs_wifi_promiscuous_result_t esp32_mquickjs_wifi_promiscuous_reserve(
    esp32_mquickjs_wifi_promiscuous_subscriber_t *subscriber,
    const esp32_mquickjs_wifi_rx_filter_t *filter,
    esp32_mquickjs_wifi_promiscuous_sink_t sink, void *context,
    esp32_mquickjs_wifi_promiscuous_token_t *output)
{
    if (subscriber == NULL || sink == NULL || output == NULL ||
        !esp32_mquickjs_wifi_rx_filter_valid(filter)) return ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_ARGUMENT;
    if (output->identity != 0) return ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_STATE;
    /* Allow a caller's staging filter to reside in the as-yet-unowned control. */
    esp32_mquickjs_wifi_rx_filter_t captured = *filter;
    esp32_mquickjs_wifi_promiscuous_result_t result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_CAPACITY;
    portENTER_CRITICAL(&s_promiscuous.lock);
    wifi_promiscuous_slot_t *free_slot = NULL;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_PROMISCUOUS_MAX_SUBSCRIBERS; ++i) {
        wifi_promiscuous_slot_t *slot = &s_promiscuous.slots[i];
        if (slot->subscriber == subscriber) {
            result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_STATE;
            goto done;
        }
        if (slot->identity == 0 && free_slot == NULL) free_slot = slot;
    }
    if (free_slot == NULL) goto done;
    if (s_promiscuous.next_identity == 0) {
        result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_IDENTITY_EXHAUSTED;
        goto done;
    }
    *subscriber = (esp32_mquickjs_wifi_promiscuous_subscriber_t){
        .filter = captured, .sink = sink, .context = context,
    };
    free_slot->identity = s_promiscuous.next_identity;
    s_promiscuous.next_identity = s_promiscuous.next_identity == UINT32_MAX ? 0 : s_promiscuous.next_identity + 1U;
    free_slot->phase = WIFI_PROMISCUOUS_RESERVED;
    free_slot->entered = 0;
    free_slot->subscriber = subscriber;
    output->identity = free_slot->identity;
    result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK;
done:
    portEXIT_CRITICAL(&s_promiscuous.lock);
    return result;
}

esp32_mquickjs_wifi_promiscuous_result_t esp32_mquickjs_wifi_promiscuous_activate(
    const esp32_mquickjs_wifi_promiscuous_token_t *token)
{
    if (token == NULL) return ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_ARGUMENT;
    esp32_mquickjs_wifi_promiscuous_result_t result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_TOKEN;
    portENTER_CRITICAL(&s_promiscuous.lock);
    wifi_promiscuous_slot_t *slot = wifi_promiscuous_find(token->identity);
    if (slot != NULL) {
        result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_STATE;
        if (slot->phase == WIFI_PROMISCUOUS_RESERVED || slot->phase == WIFI_PROMISCUOUS_ACTIVE) {
            slot->phase = WIFI_PROMISCUOUS_ACTIVE;
            result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK;
        }
    }
    portEXIT_CRITICAL(&s_promiscuous.lock);
    return result;
}

esp32_mquickjs_wifi_promiscuous_result_t esp32_mquickjs_wifi_promiscuous_begin_close(
    const esp32_mquickjs_wifi_promiscuous_token_t *token)
{
    if (token == NULL) return ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_ARGUMENT;
    esp32_mquickjs_wifi_promiscuous_result_t result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_TOKEN;
    portENTER_CRITICAL(&s_promiscuous.lock);
    wifi_promiscuous_slot_t *slot = wifi_promiscuous_find(token->identity);
    if (slot != NULL) {
        slot->phase = WIFI_PROMISCUOUS_CLOSING;
        result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK;
    }
    portEXIT_CRITICAL(&s_promiscuous.lock);
    return result;
}

esp32_mquickjs_wifi_promiscuous_result_t esp32_mquickjs_wifi_promiscuous_finish_close(
    esp32_mquickjs_wifi_promiscuous_token_t *token)
{
    if (token == NULL) return ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_ARGUMENT;
    esp32_mquickjs_wifi_promiscuous_result_t result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_TOKEN;
    portENTER_CRITICAL(&s_promiscuous.lock);
    wifi_promiscuous_slot_t *slot = wifi_promiscuous_find(token->identity);
    if (slot != NULL) {
        result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_STATE;
        if (slot->phase == WIFI_PROMISCUOUS_CLOSING) {
            result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_DRAINING;
            if (slot->entered == 0) {
                memset(slot, 0, sizeof(*slot));
                token->identity = 0;
                result = ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK;
            }
        }
    }
    portEXIT_CRITICAL(&s_promiscuous.lock);
    return result;
}

static void wifi_promiscuous_snapshot_locked(esp32_mquickjs_wifi_promiscuous_snapshot_t *output)
{
    memset(output, 0, sizeof(*output));
    output->dispatch_busy = s_promiscuous.dispatch_busy;
    output->overlapping_dispatches = s_promiscuous.overlapping_dispatches;
    output->identity_exhausted = s_promiscuous.next_identity == 0;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_PROMISCUOUS_MAX_SUBSCRIBERS; ++i) {
        const wifi_promiscuous_slot_t *slot = &s_promiscuous.slots[i];
        if (slot->identity == 0) continue;
        output->entered += slot->entered;
        if (slot->phase == WIFI_PROMISCUOUS_RESERVED) ++output->reserved;
        else if (slot->phase == WIFI_PROMISCUOUS_ACTIVE) ++output->active;
        else { ++output->closing; continue; }
        const esp32_mquickjs_wifi_rx_filter_t *filter = &slot->subscriber->filter;
        output->required_types |= filter->type_mask;
        if ((filter->type_mask & (1U << ESP32_MQUICKJS_WIFI_PACKET_CONTROL)) != 0)
            output->required_control_subtypes |= filter->subtype_filter ? filter->subtype_mask : UINT16_MAX;
        if (!filter->valid_only && filter->type_mask != 0) output->require_error_frames = true;
    }
}

void esp32_mquickjs_wifi_promiscuous_snapshot(esp32_mquickjs_wifi_promiscuous_snapshot_t *output)
{
    if (output == NULL) return;
    portENTER_CRITICAL(&s_promiscuous.lock);
    wifi_promiscuous_snapshot_locked(output);
    portEXIT_CRITICAL(&s_promiscuous.lock);
}

bool esp32_mquickjs_wifi_promiscuous_owned_requirements(
    const esp32_mquickjs_wifi_promiscuous_token_t *tokens, unsigned count,
    esp32_mquickjs_wifi_promiscuous_snapshot_t *output)
{
    if (output == NULL) return false;
    memset(output, 0, sizeof(*output));
    if (count > ESP32_MQUICKJS_WIFI_PROMISCUOUS_MAX_SUBSCRIBERS || (count != 0 && tokens == NULL)) return false;
    bool valid = false;
    portENTER_CRITICAL(&s_promiscuous.lock);
    for (unsigned i = 0; i < count; ++i) {
        wifi_promiscuous_slot_t *slot = wifi_promiscuous_find(tokens[i].identity);
        if (slot == NULL || slot->phase == WIFI_PROMISCUOUS_CLOSING) goto done;
        for (unsigned j = 0; j < i; ++j)
            if (tokens[j].identity == tokens[i].identity) goto done;
    }
    wifi_promiscuous_snapshot_locked(output);
    valid = count == (unsigned)output->reserved + output->active;
done:
    if (!valid) memset(output, 0, sizeof(*output));
    portEXIT_CRITICAL(&s_promiscuous.lock);
    return valid;
}

bool esp32_mquickjs_wifi_promiscuous_dispatch(const void *buffer,
    wifi_promiscuous_pkt_type_t type, uint64_t callback_time_us)
{
    wifi_promiscuous_slot_t *entered[ESP32_MQUICKJS_WIFI_PROMISCUOUS_MAX_SUBSCRIBERS];
    unsigned count = 0;
    portENTER_CRITICAL(&s_promiscuous.lock);
    if (s_promiscuous.dispatch_busy) {
        if (s_promiscuous.overlapping_dispatches != UINT32_MAX) ++s_promiscuous.overlapping_dispatches;
        portEXIT_CRITICAL(&s_promiscuous.lock);
        return false;
    }
    s_promiscuous.dispatch_busy = true;
    /* Retain the complete admission snapshot before leaving the critical
     * section. Closing cannot free or recycle any selected slot/control. */
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_PROMISCUOUS_MAX_SUBSCRIBERS; ++i) {
        wifi_promiscuous_slot_t *slot = &s_promiscuous.slots[i];
        if (slot->phase == WIFI_PROMISCUOUS_ACTIVE) {
            slot->entered = 1;
            entered[count++] = slot;
        }
    }
    portEXIT_CRITICAL(&s_promiscuous.lock);

    if (count != 0) {
        esp32_mquickjs_wifi_rx_target_view_t view;
        (void)esp32_mquickjs_wifi_rx_target_view(buffer, type, &view);
        for (unsigned i = 0; i < count; ++i) {
            wifi_promiscuous_slot_t *slot = entered[i];
            esp32_mquickjs_wifi_promiscuous_subscriber_t *subscriber = slot->subscriber;
            esp32_mquickjs_wifi_rx_filter_result_t result = esp32_mquickjs_wifi_rx_filter_evaluate(
                &subscriber->filter, &subscriber->filter_state, &view, callback_time_us);
            subscriber->sink(subscriber->context, &view, result, callback_time_us);
            portENTER_CRITICAL(&s_promiscuous.lock);
            slot->entered = 0;
            portEXIT_CRITICAL(&s_promiscuous.lock);
            /* No access to this slot/control after releasing its retain. */
        }
    }
    portENTER_CRITICAL(&s_promiscuous.lock);
    s_promiscuous.dispatch_busy = false;
    portEXIT_CRITICAL(&s_promiscuous.lock);
    return true;
}
#endif
