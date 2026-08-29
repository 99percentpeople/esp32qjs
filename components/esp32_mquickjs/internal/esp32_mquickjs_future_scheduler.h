#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32_MQUICKJS_FUTURE_SCHEDULER_NO_SLOT SIZE_MAX

typedef const void *esp32_mquickjs_future_scheduler_resource_key_t;

typedef struct {
    bool allocated;
    bool driver_active;
    bool driver_started;
    bool lane_waiting;
    uint64_t submission_sequence;
    esp32_mquickjs_future_scheduler_resource_key_t resource_key;
} esp32_mquickjs_future_scheduler_slot_t;

typedef enum {
    ESP32_MQUICKJS_FUTURE_LANE_START = 0,
    ESP32_MQUICKJS_FUTURE_LANE_WAIT,
    ESP32_MQUICKJS_FUTURE_LANE_REJECT,
} esp32_mquickjs_future_lane_admission_t;

size_t esp32_mquickjs_future_scheduler_find_free(
    const esp32_mquickjs_future_scheduler_slot_t *slots,
    size_t slot_count,
    size_t user_capacity,
    bool internal);

esp32_mquickjs_future_lane_admission_t
esp32_mquickjs_future_scheduler_lane_admission(
    const esp32_mquickjs_future_scheduler_slot_t *slots,
    size_t slot_count,
    size_t candidate_index,
    size_t waiter_limit);

size_t esp32_mquickjs_future_scheduler_next_waiting(
    const esp32_mquickjs_future_scheduler_slot_t *slots,
    size_t slot_count);

bool esp32_mquickjs_future_scheduler_teardown_must_wait(
    bool driver_active,
    bool poll_available,
    bool driver_ready);
