#include "esp32_mquickjs_future_scheduler.h"

static bool future_lane_in_use(
    const esp32_mquickjs_future_scheduler_slot_t *slots,
    size_t slot_count,
    size_t candidate_index)
{
    const esp32_mquickjs_future_scheduler_slot_t *candidate;
    size_t index;

    if (slots == NULL || candidate_index >= slot_count) {
        return false;
    }
    candidate = &slots[candidate_index];
    if (candidate->resource_key == NULL) {
        return false;
    }
    for (index = 0; index < slot_count; ++index) {
        const esp32_mquickjs_future_scheduler_slot_t *slot = &slots[index];

        if (index != candidate_index && slot->allocated &&
            slot->driver_active && slot->driver_started &&
            slot->resource_key == candidate->resource_key) {
            return true;
        }
    }
    return false;
}

static size_t future_lane_waiter_count(
    const esp32_mquickjs_future_scheduler_slot_t *slots,
    size_t slot_count,
    size_t candidate_index)
{
    const esp32_mquickjs_future_scheduler_slot_t *candidate;
    size_t count = 0;
    size_t index;

    if (slots == NULL || candidate_index >= slot_count) {
        return 0;
    }
    candidate = &slots[candidate_index];
    if (candidate->resource_key == NULL) {
        return 0;
    }
    for (index = 0; index < slot_count; ++index) {
        const esp32_mquickjs_future_scheduler_slot_t *slot = &slots[index];

        if (index != candidate_index && slot->allocated &&
            slot->driver_active && slot->lane_waiting &&
            slot->resource_key == candidate->resource_key) {
            count++;
        }
    }
    return count;
}

size_t esp32_mquickjs_future_scheduler_find_free(
    const esp32_mquickjs_future_scheduler_slot_t *slots,
    size_t slot_count,
    size_t user_capacity,
    bool internal)
{
    size_t index;

    if (slots == NULL || user_capacity > slot_count) {
        return ESP32_MQUICKJS_FUTURE_SCHEDULER_NO_SLOT;
    }
    if (internal) {
        for (index = user_capacity; index < slot_count; ++index) {
            if (!slots[index].allocated) {
                return index;
            }
        }
    }
    for (index = 0; index < user_capacity; ++index) {
        if (!slots[index].allocated) {
            return index;
        }
    }
    return ESP32_MQUICKJS_FUTURE_SCHEDULER_NO_SLOT;
}

esp32_mquickjs_future_lane_admission_t
esp32_mquickjs_future_scheduler_lane_admission(
    const esp32_mquickjs_future_scheduler_slot_t *slots,
    size_t slot_count,
    size_t candidate_index,
    size_t waiter_limit)
{
    const esp32_mquickjs_future_scheduler_slot_t *candidate;
    size_t waiter_count;

    if (slots == NULL || candidate_index >= slot_count) {
        return ESP32_MQUICKJS_FUTURE_LANE_REJECT;
    }
    candidate = &slots[candidate_index];
    if (candidate->resource_key == NULL) {
        return ESP32_MQUICKJS_FUTURE_LANE_START;
    }
    waiter_count = future_lane_waiter_count(
        slots, slot_count, candidate_index);
    if (!future_lane_in_use(slots, slot_count, candidate_index) &&
        waiter_count == 0) {
        return ESP32_MQUICKJS_FUTURE_LANE_START;
    }
    return waiter_count >= waiter_limit
               ? ESP32_MQUICKJS_FUTURE_LANE_REJECT
               : ESP32_MQUICKJS_FUTURE_LANE_WAIT;
}

size_t esp32_mquickjs_future_scheduler_next_waiting(
    const esp32_mquickjs_future_scheduler_slot_t *slots,
    size_t slot_count)
{
    size_t candidate = ESP32_MQUICKJS_FUTURE_SCHEDULER_NO_SLOT;
    size_t index;

    if (slots == NULL) {
        return candidate;
    }
    for (index = 0; index < slot_count; ++index) {
        const esp32_mquickjs_future_scheduler_slot_t *slot = &slots[index];

        if (!slot->allocated || !slot->driver_active ||
            !slot->lane_waiting ||
            future_lane_in_use(slots, slot_count, index)) {
            continue;
        }
        if (candidate == ESP32_MQUICKJS_FUTURE_SCHEDULER_NO_SLOT ||
            slot->submission_sequence <
                slots[candidate].submission_sequence) {
            candidate = index;
        }
    }
    return candidate;
}

bool esp32_mquickjs_future_scheduler_teardown_must_wait(
    bool driver_active,
    bool poll_available,
    bool driver_ready)
{
    return driver_active && (!poll_available || !driver_ready);
}
