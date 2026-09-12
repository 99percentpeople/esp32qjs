#include "esp32_mquickjs_wifi_raw_tx_queue.h"
#include <string.h>

#define NONE ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_NONE
#define MAX_PACKETS ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_MAX_PACKETS
#define MAX_FLUSHES ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_MAX_FLUSHES
typedef esp32_mquickjs_wifi_raw_tx_queue_t queue_t;
typedef esp32_mquickjs_wifi_raw_tx_payload_t payload_t;
typedef esp32_mquickjs_wifi_raw_tx_queue_slot_t slot_t;
typedef esp32_mquickjs_wifi_raw_tx_queue_totals_t totals_t;

static bool overlaps(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t ap = (uintptr_t)a, bp = (uintptr_t)b;
    if (a == NULL || b == NULL || an > UINTPTR_MAX - ap || bn > UINTPTR_MAX - bp) return true;
    return ap < bp + bn && bp < ap + an;
}

static bool valid(const queue_t *q)
{
    return q != NULL && q->initialized && q->generation != 0U && q->slots != NULL &&
        q->capacity != 0U && q->capacity <= MAX_PACKETS;
}

static bool aliases_queue(const queue_t *q, const void *value, size_t bytes)
{
    if (overlaps(q, sizeof(*q), value, bytes) ||
        overlaps(q->slots, sizeof(*q->slots) * q->capacity, value, bytes)) return true;
    for (unsigned i = 0; i < q->capacity; ++i)
        if (q->slots[i].allocated && overlaps(q->slots[i].payload.data,
            q->slots[i].payload.length, value, bytes)) return true;
    return false;
}

static bool empty_outputs(const queue_t *q, payload_t *removed, uint16_t capacity)
{
    if (removed == NULL || capacity < q->capacity ||
        aliases_queue(q, removed, sizeof(*removed) * q->capacity)) return false;
    for (unsigned i = 0; i < q->capacity; ++i)
        if (removed[i].data != NULL || removed[i].length != 0U) return false;
    return true;
}

bool esp32_mquickjs_wifi_raw_tx_queue_init(queue_t *q, slot_t *slots, uint16_t capacity, uint32_t generation)
{
    if (q == NULL || slots == NULL || capacity == 0U || capacity > MAX_PACKETS || generation == 0U ||
        q->initialized || overlaps(q, sizeof(*q), slots, sizeof(*slots) * capacity)) return false;
    memset(slots, 0, sizeof(*slots) * capacity);
    *q = (queue_t){.initialized = true, .generation = generation, .capacity = capacity,
        .slots = slots, .free_count = capacity, .next_flush_identity = 1,
        .pending_head = NONE, .pending_tail = NONE, .active = NONE};
    return true;
}

static void count_terminal(totals_t *totals, esp32_mquickjs_wifi_raw_tx_queue_outcome_t outcome, bool dropped)
{
    ++totals->settled;
    if (dropped) ++totals->dropped;
    else switch (outcome) {
    case ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_SUCCESS: ++totals->succeeded; break;
    case ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FAILED: ++totals->failed; break;
    case ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_UNKNOWN: ++totals->unknown; break;
    case ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_REJECTED: ++totals->rejected; break;
    case ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_ABORTED: ++totals->aborted; break;
    }
}

static void settle(queue_t *q, uint32_t sequence, esp32_mquickjs_wifi_raw_tx_queue_outcome_t outcome, bool dropped)
{
    count_terminal(&q->totals, outcome, dropped);
    for (unsigned i = 0; i < MAX_FLUSHES; ++i) {
        esp32_mquickjs_wifi_raw_tx_flush_watch_t *watch = &q->flushes[i];
        if (watch->identity == 0U || sequence > watch->status.fence) continue;
        count_terminal(&watch->status.totals, outcome, dropped);
        --watch->status.pending;
    }
}

static void drop_slot(queue_t *q, uint16_t index, payload_t *removed)
{
    slot_t *slot = &q->slots[index];
    *removed = slot->payload;
    settle(q, slot->sequence, ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_ABORTED, true);
    memset(slot, 0, sizeof(*slot));
    ++q->free_count;
    --q->queued;
}

static void evict_batch(queue_t *q, payload_t *removed, esp32_mquickjs_wifi_raw_tx_admission_t *result)
{
    uint16_t previous = NONE, first = q->pending_head;
    while (first != NONE && q->slots[first].batch_sequence == q->active_batch) {
        previous = first;
        first = q->slots[first].next;
    }
    /* Admission preflight proved at least one wholly unstarted batch exists. */
    uint32_t batch = q->slots[first].batch_sequence;
    uint16_t cursor = first;
    while (cursor != NONE && q->slots[cursor].batch_sequence == batch) {
        uint16_t next = q->slots[cursor].next;
        drop_slot(q, cursor, &removed[result->evicted_packets++]);
        cursor = next;
    }
    if (previous == NONE) q->pending_head = cursor;
    else q->slots[previous].next = cursor;
    if (cursor == NONE) q->pending_tail = previous;
    ++result->evicted_batches;
}

esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_queue_admit(
    queue_t *q, payload_t *frames, uint16_t count, esp32_mquickjs_wifi_raw_tx_queue_overflow_t overflow,
    payload_t *removed, uint16_t removed_capacity, esp32_mquickjs_wifi_raw_tx_admission_t *output)
{
    if (!valid(q) || frames == NULL || output == NULL || count == 0U || count > q->capacity ||
        (overflow != ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_REJECT_NEWEST &&
         overflow != ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_DROP_OLDEST_BATCH) ||
        aliases_queue(q, frames, sizeof(*frames) * count) || aliases_queue(q, output, sizeof(*output)) ||
        !empty_outputs(q, removed, removed_capacity) ||
        overlaps(frames, sizeof(*frames) * count, removed, sizeof(*removed) * q->capacity) ||
        overlaps(frames, sizeof(*frames) * count, output, sizeof(*output)) ||
        overlaps(removed, sizeof(*removed) * q->capacity, output, sizeof(*output)))
        return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID;
    if (q->closed) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_CLOSED;
    if (count > UINT32_MAX - q->last_sequence) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_EXHAUSTED;
    for (unsigned i = 0; i < count; ++i) {
        payload_t *frame = &frames[i];
        if (frame->data == NULL || frame->length < 24U || frame->length > 1500U ||
            aliases_queue(q, frame->data, frame->length) ||
            overlaps(frame->data, frame->length, frames, sizeof(*frames) * count) ||
            overlaps(frame->data, frame->length, removed, sizeof(*removed) * q->capacity) ||
            overlaps(frame->data, frame->length, output, sizeof(*output))) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID;
        /* Captured inputs are linear owned allocations. Reject aliases before
         * evicting anything; this is bounded by the 128-packet queue limit. */
        for (unsigned j = 0; j < i; ++j)
            if (overlaps(frame->data, frame->length, frames[j].data, frames[j].length))
                return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID;
    }
    if (q->free_count < count) {
        if (overflow == ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_REJECT_NEWEST) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FULL;
        unsigned evictable = 0;
        for (uint16_t i = q->pending_head; i != NONE; i = q->slots[i].next)
            if (q->slots[i].batch_sequence != q->active_batch) ++evictable;
        if (q->free_count + evictable < count) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FULL;
    }
    esp32_mquickjs_wifi_raw_tx_admission_t result = {.generation = q->generation,
        .first_sequence = q->last_sequence + 1U, .last_sequence = q->last_sequence + count,
        .batch_sequence = q->last_sequence + 1U, .admitted_packets = count};
    while (q->free_count < count) evict_batch(q, removed, &result);
    for (unsigned i = 0; i < count; ++i) {
        uint16_t index = 0;
        while (q->slots[index].allocated) ++index;
        q->slots[index] = (slot_t){.payload = frames[i], .sequence = result.first_sequence + i,
            .batch_sequence = result.batch_sequence, .next = NONE, .allocated = true};
        frames[i] = (payload_t){0};
        if (q->pending_tail == NONE) q->pending_head = index;
        else q->slots[q->pending_tail].next = index;
        q->pending_tail = index;
        ++q->queued;
        --q->free_count;
    }
    q->last_sequence = result.last_sequence;
    q->totals.admitted += count;
    *output = result;
    return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK;
}

static bool active_ticket(const queue_t *q, const esp32_mquickjs_wifi_raw_tx_ticket_t *ticket)
{
    return valid(q) && ticket != NULL && ticket->generation == q->generation && ticket->sequence != 0U &&
        q->active != NONE && q->active < q->capacity && q->slots[q->active].sequence == ticket->sequence;
}

bool esp32_mquickjs_wifi_raw_tx_queue_take(queue_t *q, esp32_mquickjs_wifi_raw_tx_ticket_t *ticket, payload_t *borrowed)
{
    if (!valid(q) || q->closed || q->active != NONE || q->pending_head == NONE || ticket == NULL || borrowed == NULL ||
        ticket->sequence != 0U || ticket->generation != 0U || borrowed->data != NULL || borrowed->length != 0U ||
        aliases_queue(q, ticket, sizeof(*ticket)) || aliases_queue(q, borrowed, sizeof(*borrowed)) ||
        overlaps(ticket, sizeof(*ticket), borrowed, sizeof(*borrowed))) return false;
    q->active = q->pending_head;
    slot_t *slot = &q->slots[q->active];
    q->pending_head = slot->next;
    if (q->pending_head == NONE) q->pending_tail = NONE;
    slot->next = NONE;
    --q->queued;
    q->active_batch = slot->batch_sequence;
    *ticket = (esp32_mquickjs_wifi_raw_tx_ticket_t){q->generation, slot->sequence};
    *borrowed = slot->payload;
    return true;
}

bool esp32_mquickjs_wifi_raw_tx_queue_accept(queue_t *q, const esp32_mquickjs_wifi_raw_tx_ticket_t *ticket)
{
    if (!active_ticket(q, ticket) || aliases_queue(q, ticket, sizeof(*ticket)) || q->slots[q->active].accepted) return false;
    q->slots[q->active].accepted = true;
    ++q->totals.submitted;
    for (unsigned i = 0; i < MAX_FLUSHES; ++i)
        if (q->flushes[i].identity != 0U && ticket->sequence <= q->flushes[i].status.fence)
            ++q->flushes[i].status.totals.submitted;
    return true;
}

bool esp32_mquickjs_wifi_raw_tx_queue_finish(queue_t *q, esp32_mquickjs_wifi_raw_tx_ticket_t *ticket,
    esp32_mquickjs_wifi_raw_tx_queue_outcome_t outcome, payload_t *retired)
{
    if (!active_ticket(q, ticket) || (unsigned)outcome > ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_ABORTED ||
        retired == NULL || retired->data != NULL || retired->length != 0U ||
        aliases_queue(q, ticket, sizeof(*ticket)) || aliases_queue(q, retired, sizeof(*retired)) ||
        overlaps(ticket, sizeof(*ticket), retired, sizeof(*retired))) return false;
    slot_t *slot = &q->slots[q->active];
    if ((outcome <= ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_UNKNOWN && !slot->accepted) ||
        (outcome == ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_REJECTED && slot->accepted)) return false;
    *retired = slot->payload;
    settle(q, slot->sequence, outcome, false);
    memset(slot, 0, sizeof(*slot));
    ++q->free_count;
    q->active = NONE;
    if (q->pending_head == NONE || q->slots[q->pending_head].batch_sequence != q->active_batch) q->active_batch = 0;
    memset(ticket, 0, sizeof(*ticket));
    return true;
}

bool esp32_mquickjs_wifi_raw_tx_queue_close(queue_t *q, payload_t *removed, uint16_t capacity, uint16_t *removed_count)
{
    if (!valid(q) || removed_count == NULL || !empty_outputs(q, removed, capacity) ||
        aliases_queue(q, removed_count, sizeof(*removed_count)) ||
        overlaps(removed_count, sizeof(*removed_count), removed, sizeof(*removed) * q->capacity)) return false;
    uint16_t count = 0;
    while (q->pending_head != NONE) {
        uint16_t index = q->pending_head;
        q->pending_head = q->slots[index].next;
        drop_slot(q, index, &removed[count++]);
    }
    q->pending_tail = NONE;
    q->closed = true;
    if (q->active == NONE) q->active_batch = 0;
    *removed_count = count;
    return true;
}

esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_queue_flush_begin(
    queue_t *q, esp32_mquickjs_wifi_raw_tx_flush_token_t *token)
{
    if (!valid(q) || token == NULL || token->identity != 0U || token->generation != 0U || token->index != 0U ||
        aliases_queue(q, token, sizeof(*token))) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID;
    if (q->next_flush_identity == 0U) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_EXHAUSTED;
    unsigned index = 0;
    while (index < MAX_FLUSHES && q->flushes[index].identity != 0U) ++index;
    if (index == MAX_FLUSHES) return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FULL;
    q->flushes[index].identity = q->next_flush_identity++;
    q->flushes[index].status = (esp32_mquickjs_wifi_raw_tx_flush_status_t){
        .generation = q->generation, .fence = q->last_sequence,
        .pending = (uint16_t)(q->totals.admitted - q->totals.settled), .totals = q->totals};
    *token = (esp32_mquickjs_wifi_raw_tx_flush_token_t){q->generation, q->flushes[index].identity, (uint8_t)index};
    return ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK;
}

static bool flush_token(const queue_t *q, const esp32_mquickjs_wifi_raw_tx_flush_token_t *token)
{
    return valid(q) && token != NULL && token->generation == q->generation && token->identity != 0U &&
        token->index < MAX_FLUSHES && q->flushes[token->index].identity == token->identity;
}

bool esp32_mquickjs_wifi_raw_tx_queue_flush_status(const queue_t *q,
    const esp32_mquickjs_wifi_raw_tx_flush_token_t *token, esp32_mquickjs_wifi_raw_tx_flush_status_t *status)
{
    if (!flush_token(q, token) || status == NULL || aliases_queue(q, status, sizeof(*status)) ||
        overlaps(token, sizeof(*token), status, sizeof(*status))) return false;
    *status = q->flushes[token->index].status;
    return true;
}

bool esp32_mquickjs_wifi_raw_tx_queue_flush_release(queue_t *q, esp32_mquickjs_wifi_raw_tx_flush_token_t *token)
{
    if (!flush_token(q, token) || aliases_queue(q, token, sizeof(*token))) return false;
    memset(&q->flushes[token->index], 0, sizeof(q->flushes[token->index]));
    memset(token, 0, sizeof(*token));
    return true;
}

bool esp32_mquickjs_wifi_raw_tx_queue_deinit(queue_t *q)
{
    if (!valid(q) || !q->closed || q->active != NONE || q->queued != 0U || q->free_count != q->capacity) return false;
    for (unsigned i = 0; i < MAX_FLUSHES; ++i)
        if (q->flushes[i].identity != 0U) return false;
    memset(q->slots, 0, sizeof(*q->slots) * q->capacity);
    memset(q, 0, sizeof(*q));
    return true;
}
