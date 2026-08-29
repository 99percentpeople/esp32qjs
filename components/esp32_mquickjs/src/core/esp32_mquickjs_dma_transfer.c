#include "esp32_mquickjs_dma_transfer.h"

#include <limits.h>
#include <string.h>

esp32_mquickjs_dma_path_t esp32_mquickjs_dma_classify_source(
    bool dma_capable,
    bool external,
    bool direct_external_dma)
{
    if (!dma_capable) {
        return ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL;
    }
    if (!external) {
        return ESP32_MQUICKJS_DMA_PATH_DIRECT_INTERNAL;
    }
    return direct_external_dma
               ? ESP32_MQUICKJS_DMA_PATH_DIRECT_EXTERNAL
               : ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL;
}

void esp32_mquickjs_dma_workspace_init(
    esp32_mquickjs_dma_workspace_t *workspace,
    uint8_t *first_tx,
    uint8_t *first_rx,
    uint8_t *second_tx,
    uint8_t *second_rx,
    size_t capacity)
{
    if (workspace == NULL) {
        return;
    }
    memset(workspace, 0, sizeof(*workspace));
    workspace->slots[0].tx = first_tx;
    workspace->slots[0].rx = first_rx;
    workspace->slots[1].tx = second_tx;
    workspace->slots[1].rx = second_rx;
    workspace->capacity = capacity;
}

void esp32_mquickjs_dma_workspace_release(
    esp32_mquickjs_dma_workspace_t *workspace,
    uint8_t slot_index)
{
    if (workspace == NULL ||
        slot_index >= ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT) {
        return;
    }
    workspace->slots[slot_index].in_use = false;
}

void esp32_mquickjs_dma_workspace_release_buffers(
    uint8_t *tx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT],
    uint8_t *rx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT],
    esp32_mquickjs_dma_buffer_release_fn release,
    void *opaque)
{
    uint32_t index;

    if (tx == NULL || rx == NULL) {
        return;
    }
    for (index = 0; index < ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT;
         ++index) {
        if (tx[index] != NULL && release != NULL) {
            release(tx[index], opaque);
        }
        if (rx[index] != NULL && release != NULL) {
            release(rx[index], opaque);
        }
        tx[index] = NULL;
        rx[index] = NULL;
    }
}

bool esp32_mquickjs_dma_workspace_allocate_buffers(
    uint8_t *tx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT],
    uint8_t *rx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT],
    size_t bytes,
    esp32_mquickjs_dma_buffer_allocate_fn allocate,
    esp32_mquickjs_dma_buffer_release_fn release,
    void *opaque)
{
    uint32_t index;

    if (tx == NULL || rx == NULL || bytes == 0 || allocate == NULL ||
        release == NULL) {
        return false;
    }
    memset(tx, 0,
           sizeof(*tx) * ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT);
    memset(rx, 0,
           sizeof(*rx) * ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT);
    for (index = 0; index < ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT;
         ++index) {
        tx[index] = allocate(bytes, opaque);
        if (tx[index] == NULL) {
            esp32_mquickjs_dma_workspace_release_buffers(
                tx, rx, release, opaque);
            return false;
        }
        rx[index] = allocate(bytes, opaque);
        if (rx[index] == NULL) {
            esp32_mquickjs_dma_workspace_release_buffers(
                tx, rx, release, opaque);
            return false;
        }
    }
    return true;
}

void esp32_mquickjs_dma_cursor_begin(
    esp32_mquickjs_dma_cursor_t *cursor,
    const uint8_t *data,
    size_t length,
    esp32_mquickjs_dma_path_t path,
    uint32_t source_span)
{
    if (cursor == NULL) {
        return;
    }
    memset(cursor, 0, sizeof(*cursor));
    cursor->data = data;
    cursor->length = length;
    cursor->path = path;
    cursor->source_span = source_span;
}

static bool dma_workspace_acquire(esp32_mquickjs_dma_workspace_t *workspace,
                                  uint8_t *out_slot)
{
    uint8_t offset;

    if (workspace == NULL || out_slot == NULL || workspace->capacity == 0) {
        return false;
    }
    for (offset = 0; offset < ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT;
         ++offset) {
        uint8_t index = (uint8_t)(
            (workspace->next_slot + offset) %
            ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT);

        if (!workspace->slots[index].in_use) {
            workspace->slots[index].in_use = true;
            workspace->next_slot = (uint8_t)(
                (index + 1U) % ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT);
            *out_slot = index;
            return true;
        }
    }
    return false;
}

bool esp32_mquickjs_dma_cursor_next(
    esp32_mquickjs_dma_cursor_t *cursor,
    esp32_mquickjs_dma_workspace_t *workspace,
    size_t max_transfer_size,
    bool transmit,
    bool receive,
    esp32_mquickjs_dma_chunk_t *out)
{
    bool staged;
    size_t remaining;
    size_t limit;
    uint8_t slot_index = ESP32_MQUICKJS_DMA_NO_SLOT;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        out->slot_index = ESP32_MQUICKJS_DMA_NO_SLOT;
    }
    if (cursor == NULL || out == NULL || max_transfer_size == 0 ||
        cursor->offset >= cursor->length ||
        (transmit && cursor->data == NULL)) {
        return false;
    }

    staged = cursor->path == ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL ||
             receive;
    limit = max_transfer_size;
    if (staged) {
        if (workspace == NULL || workspace->capacity == 0) {
            return false;
        }
        if (limit > workspace->capacity) {
            limit = workspace->capacity;
        }
        if (!dma_workspace_acquire(workspace, &slot_index)) {
            return false;
        }
    }

    remaining = cursor->length - cursor->offset;
    out->length = remaining < limit ? remaining : limit;
    out->span_offset = cursor->offset;
    out->source_span = cursor->source_span;
    out->path = cursor->path;
    out->slot_index = slot_index;
    out->uses_staging = staged;
    if (transmit) {
        if (cursor->path == ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL) {
            memcpy(workspace->slots[slot_index].tx,
                   cursor->data + cursor->offset, out->length);
            out->tx_data = workspace->slots[slot_index].tx;
        } else {
            out->tx_data = cursor->data + cursor->offset;
        }
    }
    if (receive) {
        out->rx_data = workspace->slots[slot_index].rx;
    }
    cursor->offset += out->length;
    out->keep_cs_active = cursor->offset < cursor->length;
    return true;
}

void esp32_mquickjs_dma_stats_init(esp32_mquickjs_dma_stats_t *stats)
{
    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
}

static uint8_t dma_path_bit(esp32_mquickjs_dma_path_t path)
{
    return path <= ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL
               ? (uint8_t)(1U << (unsigned)path)
               : 0U;
}

void esp32_mquickjs_dma_stats_note_span(
    esp32_mquickjs_dma_stats_t *stats,
    esp32_mquickjs_dma_path_t path)
{
    if (stats == NULL) {
        return;
    }
    stats->source_spans++;
    stats->path_mask |= dma_path_bit(path);
}

void esp32_mquickjs_dma_stats_note_transaction(
    esp32_mquickjs_dma_stats_t *stats,
    esp32_mquickjs_dma_path_t path,
    size_t completed_bytes,
    size_t staged_bytes)
{
    if (stats == NULL) {
        return;
    }
    stats->transactions++;
    stats->bytes += completed_bytes;
    stats->staged_bytes += staged_bytes;
    stats->path_mask |= dma_path_bit(path);
}

esp32_mquickjs_dma_path_t esp32_mquickjs_dma_stats_path(
    const esp32_mquickjs_dma_stats_t *stats)
{
    uint8_t mask = stats != NULL ? stats->path_mask : 0;

    if (mask == 0 || mask == dma_path_bit(
                                ESP32_MQUICKJS_DMA_PATH_DIRECT_INTERNAL)) {
        return ESP32_MQUICKJS_DMA_PATH_DIRECT_INTERNAL;
    }
    if (mask == dma_path_bit(ESP32_MQUICKJS_DMA_PATH_DIRECT_EXTERNAL)) {
        return ESP32_MQUICKJS_DMA_PATH_DIRECT_EXTERNAL;
    }
    if (mask == dma_path_bit(ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL)) {
        return ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL;
    }
    return ESP32_MQUICKJS_DMA_PATH_MIXED;
}

const char *esp32_mquickjs_dma_path_name(esp32_mquickjs_dma_path_t path)
{
    switch (path) {
    case ESP32_MQUICKJS_DMA_PATH_DIRECT_INTERNAL:
        return "direct-internal";
    case ESP32_MQUICKJS_DMA_PATH_DIRECT_EXTERNAL:
        return "direct-external";
    case ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL:
        return "staged-internal";
    case ESP32_MQUICKJS_DMA_PATH_MIXED:
    default:
        return "mixed";
    }
}

esp32_mquickjs_dma_progress_t esp32_mquickjs_dma_validate_tx_progress(
    size_t actual_bytes,
    size_t expected_bytes)
{
    if (actual_bytes < expected_bytes) {
        return ESP32_MQUICKJS_DMA_PROGRESS_UNDERFLOW;
    }
    return actual_bytes > expected_bytes
               ? ESP32_MQUICKJS_DMA_PROGRESS_OVERFLOW
               : ESP32_MQUICKJS_DMA_PROGRESS_OK;
}

esp32_mquickjs_dma_progress_t esp32_mquickjs_dma_validate_rx_progress(
    size_t actual_bytes,
    size_t expected_bytes)
{
    return actual_bytes > expected_bytes
               ? ESP32_MQUICKJS_DMA_PROGRESS_OVERFLOW
               : (actual_bytes < expected_bytes
                      ? ESP32_MQUICKJS_DMA_PROGRESS_UNDERFLOW
                      : ESP32_MQUICKJS_DMA_PROGRESS_OK);
}

bool esp32_mquickjs_dma_rx_window_fits(
    size_t output_offset,
    size_t completed_bytes,
    size_t output_capacity)
{
    return output_offset <= output_capacity &&
           completed_bytes <= output_capacity - output_offset;
}

bool esp32_mquickjs_dma_completion_queue_init(
    esp32_mquickjs_dma_completion_queue_t *queue,
    uint32_t depth)
{
    if (queue == NULL || depth == 0 ||
        depth > ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT) {
        return false;
    }
    memset(queue, 0, sizeof(*queue));
    queue->depth = depth;
    return true;
}

bool esp32_mquickjs_dma_completion_queue_can_submit(
    const esp32_mquickjs_dma_completion_queue_t *queue)
{
    return queue != NULL && queue->depth > 0 &&
           queue->depth <= ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT &&
           queue->occupied < queue->depth &&
           queue->slots[queue->tail] == ESP32_MQUICKJS_DMA_QUEUE_SLOT_FREE;
}

bool esp32_mquickjs_dma_completion_queue_next_submit(
    const esp32_mquickjs_dma_completion_queue_t *queue,
    uint32_t *out_index)
{
    if (out_index == NULL ||
        !esp32_mquickjs_dma_completion_queue_can_submit(queue)) {
        return false;
    }
    *out_index = queue->tail;
    return true;
}

bool esp32_mquickjs_dma_completion_queue_note_submitted(
    esp32_mquickjs_dma_completion_queue_t *queue,
    uint32_t index)
{
    if (!esp32_mquickjs_dma_completion_queue_can_submit(queue) ||
        index != queue->tail || index >= queue->depth) {
        return false;
    }
    queue->slots[index] = ESP32_MQUICKJS_DMA_QUEUE_SLOT_SUBMITTED;
    queue->tail = (queue->tail + 1U) % queue->depth;
    queue->occupied++;
    queue->in_flight++;
    return true;
}

esp32_mquickjs_dma_completion_t
esp32_mquickjs_dma_completion_queue_note_returned(
    esp32_mquickjs_dma_completion_queue_t *queue,
    uint32_t index)
{
    bool in_order;

    if (queue == NULL || queue->depth == 0 || index >= queue->depth ||
        queue->slots[index] != ESP32_MQUICKJS_DMA_QUEUE_SLOT_SUBMITTED ||
        queue->in_flight == 0 || queue->occupied == 0) {
        return ESP32_MQUICKJS_DMA_COMPLETION_INVALID;
    }
    in_order = index == queue->head;
    queue->slots[index] = ESP32_MQUICKJS_DMA_QUEUE_SLOT_RETURNED;
    queue->in_flight--;
    while (queue->occupied > 0 &&
           queue->slots[queue->head] ==
               ESP32_MQUICKJS_DMA_QUEUE_SLOT_RETURNED) {
        queue->slots[queue->head] = ESP32_MQUICKJS_DMA_QUEUE_SLOT_FREE;
        queue->head = (queue->head + 1U) % queue->depth;
        queue->occupied--;
    }
    return in_order ? ESP32_MQUICKJS_DMA_COMPLETION_IN_ORDER
                    : ESP32_MQUICKJS_DMA_COMPLETION_OUT_OF_ORDER;
}

bool esp32_mquickjs_dma_completion_queue_peek_in_flight(
    const esp32_mquickjs_dma_completion_queue_t *queue,
    uint32_t *out_index)
{
    uint32_t offset;

    if (queue == NULL || out_index == NULL || queue->depth == 0 ||
        queue->in_flight == 0) {
        return false;
    }
    for (offset = 0; offset < queue->depth; ++offset) {
        uint32_t index = (queue->head + offset) % queue->depth;

        if (queue->slots[index] ==
            ESP32_MQUICKJS_DMA_QUEUE_SLOT_SUBMITTED) {
            *out_index = index;
            return true;
        }
    }
    return false;
}

bool esp32_mquickjs_dma_completion_queue_releasable(
    const esp32_mquickjs_dma_completion_queue_t *queue)
{
    return queue != NULL && queue->depth > 0 && queue->occupied == 0 &&
           queue->in_flight == 0;
}

esp32_mquickjs_dma_cancel_t esp32_mquickjs_dma_cancel_disposition(
    const esp32_mquickjs_dma_completion_queue_t *queue,
    bool completed,
    bool cancellation_requested)
{
    if (completed || cancellation_requested || queue == NULL) {
        return ESP32_MQUICKJS_DMA_CANCEL_REJECTED;
    }
    return esp32_mquickjs_dma_completion_queue_releasable(queue)
               ? ESP32_MQUICKJS_DMA_CANCEL_COMPLETE
               : ESP32_MQUICKJS_DMA_CANCEL_PENDING;
}

uint64_t esp32_mquickjs_dma_progress_timeout_us(size_t bytes,
                                                uint32_t actual_freq_hz)
{
    uint64_t wire_us;
    uint64_t timeout_us;

    if (actual_freq_hz == 0) {
        return 100000ULL;
    }
    wire_us = ((uint64_t)bytes * 8000000ULL + actual_freq_hz - 1U) /
              actual_freq_hz;
    if (wire_us > (UINT64_MAX - 50000ULL) / 4ULL) {
        timeout_us = UINT64_MAX;
    } else {
        timeout_us = wire_us * 4ULL + 50000ULL;
    }
    return timeout_us < 100000ULL ? 100000ULL : timeout_us;
}

bool esp32_mquickjs_dma_progress_timed_out(
    uint64_t now_us,
    uint64_t deadline_us,
    bool completion_observed)
{
    return !completion_observed && deadline_us > 0 && now_us >= deadline_us;
}
