#pragma once
#include "esp32_mquickjs_wifi_raw_tx_limits.h"
#include <stdbool.h>
#include <stdint.h>

/* Native scheduling ledger. Caller serializes access with its task mutex.
 * No allocator, SDK, JS, timer or callback pointer is stored here. The adapter
 * owns frame storage and must retain it independently through native completion.
 * This is not yet a public periodic API or an autonomous timer implementation. */

typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_SKIP,
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_STOP,
} esp32_mquickjs_wifi_raw_tx_periodic_busy_t;
typedef struct {
    uint32_t interval_us, start_delay_us, count; /* count=0: until stopped/exhausted */
    esp32_mquickjs_wifi_raw_tx_periodic_busy_t busy;
    bool stop_on_error;
} esp32_mquickjs_wifi_raw_tx_periodic_options_t;
typedef struct { uint32_t generation, sequence; } esp32_mquickjs_wifi_raw_tx_periodic_ticket_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_NOT_DUE,
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_ISSUE,
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_SKIPPED,
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_HALTED,
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_INVALID,
} esp32_mquickjs_wifi_raw_tx_periodic_due_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_SUCCESS,
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_FAILED,
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_UNKNOWN,
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_REJECTED,
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_ABORTED,
    ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_DROPPED,
} esp32_mquickjs_wifi_raw_tx_periodic_outcome_t;
typedef struct {
    uint32_t generation;
    esp32_mquickjs_wifi_raw_tx_periodic_options_t options;
    int64_t next_due_us, last_observed_us;
    uint32_t scheduled, issued, submitted, completed, failed, unknown, rejected, aborted, dropped;
    uint32_t skipped_busy, skipped_late;
    bool running, closing, faulted, exhausted, uncertain;
    esp32_mquickjs_wifi_raw_tx_periodic_ticket_t active;
    bool active_submitted;
} esp32_mquickjs_wifi_raw_tx_periodic_t;

/* generation is a fresh nonzero owner identity, never reused while old tickets
 * can exist. A live ledger must not be reinitialized. Failure leaves output
 * untouched. Timestamp arithmetic is checked before publishing any state. */
bool esp32_mquickjs_wifi_raw_tx_periodic_init(esp32_mquickjs_wifi_raw_tx_periodic_t *output,
    uint32_t generation, const esp32_mquickjs_wifi_raw_tx_periodic_options_t *options, int64_t now_us);
/* Consume due schedule opportunities, issuing at most the most recent one.
 * Older elapsed deadlines count as skipped_late; no catch-up queue. count limits
 * schedule opportunities, including skips, not successful RF transmissions.
 * busy is a caller-observed admission constraint; an outstanding ticket is also
 * busy. The caller must recheck actual queue/Radio admission before submitting.
 * output must be empty and must not alias this ledger; non-ISSUE leaves it empty.
 * active tickets survive stop/close/count exhaustion and uncertain outcomes. */
esp32_mquickjs_wifi_raw_tx_periodic_due_t esp32_mquickjs_wifi_raw_tx_periodic_due(
    esp32_mquickjs_wifi_raw_tx_periodic_t *periodic, int64_t now_us, bool busy,
    esp32_mquickjs_wifi_raw_tx_periodic_ticket_t *output);
bool esp32_mquickjs_wifi_raw_tx_periodic_submitted(esp32_mquickjs_wifi_raw_tx_periodic_t *periodic,
    const esp32_mquickjs_wifi_raw_tx_periodic_ticket_t *ticket);
/* Admission raced another producer; reclassify the unsubmitted reservation as
 * busy without consuming another scheduled opportunity or retaining a packet. */
bool esp32_mquickjs_wifi_raw_tx_periodic_busy(esp32_mquickjs_wifi_raw_tx_periodic_t *periodic,
    esp32_mquickjs_wifi_raw_tx_periodic_ticket_t *ticket);
/* Success/failed/unknown require SDK acceptance plus proven native completion.
 * Rejected/aborted require proof no native submission happened. Public timeout
 * or cancellation is NOT such proof. Exact terminal finish clears the ticket. */
bool esp32_mquickjs_wifi_raw_tx_periodic_finish(esp32_mquickjs_wifi_raw_tx_periodic_t *periodic,
    esp32_mquickjs_wifi_raw_tx_periodic_ticket_t *ticket,
    esp32_mquickjs_wifi_raw_tx_periodic_outcome_t outcome);
/* Uncertain driver ownership stops production and retains the active ticket.
 * It cannot be retired as a no-submit rejection/abort. A later proven physical
 * termination uses the distinct terminated helper below. */
bool esp32_mquickjs_wifi_raw_tx_periodic_uncertain(esp32_mquickjs_wifi_raw_tx_periodic_t *periodic,
    const esp32_mquickjs_wifi_raw_tx_periodic_ticket_t *ticket);
/* Exact terminal native-teardown proof, including previously uncertain or
 * accepted sends. Never use for timeout/GC/unregister. Does not imply no RF;
 * records aborted, stops production, preserves fault history and clears ticket. */
bool esp32_mquickjs_wifi_raw_tx_periodic_terminated(esp32_mquickjs_wifi_raw_tx_periodic_t *periodic,
    esp32_mquickjs_wifi_raw_tx_periodic_ticket_t *ticket, bool driver_accepted);
void esp32_mquickjs_wifi_raw_tx_periodic_stop(esp32_mquickjs_wifi_raw_tx_periodic_t *periodic);
void esp32_mquickjs_wifi_raw_tx_periodic_close(esp32_mquickjs_wifi_raw_tx_periodic_t *periodic);
/* Means only ledger tickets drained; adapter timer/callback/worker and payload
 * references need their own retirement proof before freeing any storage. */
bool esp32_mquickjs_wifi_raw_tx_periodic_drained(const esp32_mquickjs_wifi_raw_tx_periodic_t *periodic);
