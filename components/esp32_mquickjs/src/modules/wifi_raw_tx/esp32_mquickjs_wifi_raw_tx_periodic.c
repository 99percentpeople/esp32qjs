#include "esp32_mquickjs_wifi_raw_tx_periodic.h"
#include <limits.h>
#include <stddef.h>

typedef esp32_mquickjs_wifi_raw_tx_periodic_t periodic_t;
typedef esp32_mquickjs_wifi_raw_tx_periodic_ticket_t ticket_t;

static bool aliases(const periodic_t *periodic, const ticket_t *ticket)
{
    uintptr_t p = (uintptr_t)periodic, t = (uintptr_t)ticket;
    return sizeof(*periodic) > UINTPTR_MAX - p || sizeof(*ticket) > UINTPTR_MAX - t ||
        (p < t + sizeof(*ticket) && t < p + sizeof(*periodic));
}

bool esp32_mquickjs_wifi_raw_tx_periodic_init(periodic_t *output, uint32_t generation,
    const esp32_mquickjs_wifi_raw_tx_periodic_options_t *options, int64_t now_us)
{
    if (output == NULL || options == NULL || generation == 0U || output->generation != 0U ||
        now_us < 0 || options->interval_us < ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_MIN_INTERVAL_US ||
        options->start_delay_us > INT64_MAX - now_us ||
        (options->busy != ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_SKIP &&
         options->busy != ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_STOP)) return false;
    *output = (periodic_t){.generation = generation, .options = *options,
        .next_due_us = now_us + options->start_delay_us, .last_observed_us = now_us, .running = true};
    return true;
}

static bool exact(const periodic_t *periodic, const ticket_t *ticket)
{
    return periodic != NULL && ticket != NULL && !aliases(periodic, ticket) && periodic->generation != 0U &&
        ticket->generation == periodic->generation && ticket->sequence != 0U &&
        ticket->generation == periodic->active.generation && ticket->sequence == periodic->active.sequence;
}

static void exhausted(periodic_t *periodic)
{
    periodic->running = false;
    periodic->faulted = true;
    periodic->exhausted = true;
}

esp32_mquickjs_wifi_raw_tx_periodic_due_t esp32_mquickjs_wifi_raw_tx_periodic_due(
    periodic_t *periodic, int64_t now_us, bool busy, ticket_t *output)
{
    if (periodic == NULL || output == NULL || aliases(periodic, output) ||
        output->generation != 0U || output->sequence != 0U || periodic->generation == 0U ||
        now_us < 0) return ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_INVALID;
    if (!periodic->running) return ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_HALTED;
    if (now_us < periodic->last_observed_us) {
        periodic->running = false; periodic->faulted = true;
        return ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_HALTED;
    }
    periodic->last_observed_us = now_us;
    if (now_us < periodic->next_due_us) return ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_NOT_DUE;
    uint64_t due = (uint64_t)(now_us - periodic->next_due_us) / periodic->options.interval_us + 1U;
    uint32_t remaining = periodic->options.count != 0U
        ? periodic->options.count - periodic->scheduled : UINT32_MAX - periodic->scheduled;
    if (remaining == 0U) {
        if (periodic->options.count == 0U) exhausted(periodic);
        else periodic->running = false;
        return ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_HALTED;
    }
    if (due > remaining) due = remaining;
    /* Advancing uses the absolute schedule, never now+interval; delayed worker
     * observations cannot gradually drift the requested schedule. */
    uint64_t delta = due * periodic->options.interval_us;
    bool limit = due == remaining;
    if (!limit && delta > (uint64_t)(INT64_MAX - periodic->next_due_us)) {
        exhausted(periodic); return ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_HALTED;
    }
    periodic->scheduled += (uint32_t)due;
    periodic->skipped_late += (uint32_t)due - 1U;
    if (!limit) periodic->next_due_us += (int64_t)delta;
    else if (periodic->options.count == 0U) exhausted(periodic);
    else periodic->running = false;
    if (busy || periodic->active.sequence != 0U) {
        ++periodic->skipped_busy;
        if (periodic->options.busy == ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_STOP) periodic->running = false;
        return ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_SKIPPED;
    }
    ++periodic->issued;
    periodic->active = (ticket_t){periodic->generation, periodic->scheduled};
    periodic->active_submitted = false;
    *output = periodic->active;
    return ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_ISSUE;
}

bool esp32_mquickjs_wifi_raw_tx_periodic_submitted(periodic_t *periodic, const ticket_t *ticket)
{
    if (!exact(periodic, ticket) || periodic->active_submitted || periodic->uncertain) return false;
    periodic->active_submitted = true;
    ++periodic->submitted;
    return true;
}

bool esp32_mquickjs_wifi_raw_tx_periodic_finish(periodic_t *periodic, ticket_t *ticket,
    esp32_mquickjs_wifi_raw_tx_periodic_outcome_t outcome)
{
    if (!exact(periodic, ticket) || outcome < ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_SUCCESS ||
        outcome > ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_DROPPED || periodic->uncertain) return false;
    bool completion = outcome <= ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_UNKNOWN;
    if (completion != periodic->active_submitted) return false;
    if (completion) ++periodic->completed;
    if (outcome == ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_FAILED ||
        outcome == ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_REJECTED ||
        outcome == ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_DROPPED) {
        ++periodic->failed;
        if (periodic->options.stop_on_error) { periodic->running = false; periodic->faulted = true; }
    }
    if (outcome == ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_UNKNOWN) ++periodic->unknown;
    if (outcome == ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_REJECTED) ++periodic->rejected;
    if (outcome == ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_ABORTED) ++periodic->aborted;
    if (outcome == ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_DROPPED) ++periodic->dropped;
    periodic->active = (ticket_t){0}; periodic->active_submitted = false;
    *ticket = (ticket_t){0};
    return true;
}

bool esp32_mquickjs_wifi_raw_tx_periodic_busy(periodic_t *periodic, ticket_t *ticket)
{
    if (!exact(periodic, ticket) || periodic->active_submitted || periodic->uncertain) return false;
    --periodic->issued;
    ++periodic->skipped_busy;
    if (periodic->options.busy == ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_STOP) periodic->running = false;
    periodic->active = (ticket_t){0};
    *ticket = (ticket_t){0};
    return true;
}

bool esp32_mquickjs_wifi_raw_tx_periodic_uncertain(periodic_t *periodic, const ticket_t *ticket)
{
    if (!exact(periodic, ticket)) return false;
    periodic->running = false; periodic->faulted = true; periodic->uncertain = true;
    return true;
}

bool esp32_mquickjs_wifi_raw_tx_periodic_terminated(periodic_t *periodic, ticket_t *ticket, bool driver_accepted)
{
    if (!exact(periodic, ticket) || (periodic->active_submitted && !driver_accepted)) return false;
    /* Acceptance may only become observable after uncertainty was recorded. */
    if (driver_accepted && !periodic->active_submitted) ++periodic->submitted;
    ++periodic->aborted;
    periodic->running = false;
    periodic->uncertain = false;
    periodic->active = (ticket_t){0}; periodic->active_submitted = false;
    *ticket = (ticket_t){0};
    return true;
}

void esp32_mquickjs_wifi_raw_tx_periodic_stop(periodic_t *periodic)
{
    if (periodic != NULL) periodic->running = false;
}

void esp32_mquickjs_wifi_raw_tx_periodic_close(periodic_t *periodic)
{
    if (periodic != NULL) { periodic->running = false; periodic->closing = true; }
}

bool esp32_mquickjs_wifi_raw_tx_periodic_drained(const periodic_t *periodic)
{
    return periodic != NULL && periodic->generation != 0U && !periodic->running && periodic->active.sequence == 0U;
}
