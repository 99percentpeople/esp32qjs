#include "esp32_mquickjs_future_scheduler.h"

#include <assert.h>
#include <stdint.h>

static void test_public_capacity_cannot_consume_internal_reserve(void)
{
    esp32_mquickjs_future_scheduler_slot_t slots[4] = {0};

    slots[0].allocated = true;
    slots[1].allocated = true;
    assert(esp32_mquickjs_future_scheduler_find_free(
               slots, 4, 2, false) ==
           ESP32_MQUICKJS_FUTURE_SCHEDULER_NO_SLOT);
    assert(esp32_mquickjs_future_scheduler_find_free(
               slots, 4, 2, true) == 2);

    slots[2].allocated = true;
    slots[3].allocated = true;
    slots[1].allocated = false;
    assert(esp32_mquickjs_future_scheduler_find_free(
               slots, 4, 2, true) == 1);
    slots[1].allocated = true;
    assert(esp32_mquickjs_future_scheduler_find_free(
               slots, 4, 2, true) ==
           ESP32_MQUICKJS_FUTURE_SCHEDULER_NO_SLOT);
}

static void test_resource_lane_saturation_and_fifo(void)
{
    static const int lane_a;
    static const int lane_b;
    esp32_mquickjs_future_scheduler_slot_t slots[6] = {0};

    slots[0] = (esp32_mquickjs_future_scheduler_slot_t){
        .allocated = true,
        .driver_active = true,
        .driver_started = true,
        .submission_sequence = 1,
        .resource_key = &lane_a,
    };
    slots[1] = (esp32_mquickjs_future_scheduler_slot_t){
        .allocated = true,
        .driver_active = true,
        .lane_waiting = true,
        .submission_sequence = 2,
        .resource_key = &lane_a,
    };
    slots[2] = (esp32_mquickjs_future_scheduler_slot_t){
        .allocated = true,
        .driver_active = true,
        .lane_waiting = true,
        .submission_sequence = 3,
        .resource_key = &lane_a,
    };
    slots[3] = (esp32_mquickjs_future_scheduler_slot_t){
        .allocated = true,
        .driver_active = true,
        .submission_sequence = 4,
        .resource_key = &lane_a,
    };
    slots[4] = (esp32_mquickjs_future_scheduler_slot_t){
        .allocated = true,
        .driver_active = true,
        .lane_waiting = true,
        .submission_sequence = 5,
        .resource_key = &lane_b,
    };

    assert(esp32_mquickjs_future_scheduler_lane_admission(
               slots, 6, 3, 2) == ESP32_MQUICKJS_FUTURE_LANE_REJECT);
    assert(esp32_mquickjs_future_scheduler_next_waiting(slots, 6) == 4);

    slots[0].driver_started = false;
    slots[0].allocated = false;
    assert(esp32_mquickjs_future_scheduler_next_waiting(slots, 6) == 1);
    assert(esp32_mquickjs_future_scheduler_lane_admission(
               slots, 6, 3, 3) == ESP32_MQUICKJS_FUTURE_LANE_WAIT);

    slots[1].lane_waiting = false;
    slots[1].driver_started = true;
    assert(esp32_mquickjs_future_scheduler_next_waiting(slots, 6) == 4);
}

static void test_runtime_stop_retains_pending_driver(void)
{
    assert(!esp32_mquickjs_future_scheduler_teardown_must_wait(
        false, false, false));
    assert(esp32_mquickjs_future_scheduler_teardown_must_wait(
        true, true, false));
    assert(esp32_mquickjs_future_scheduler_teardown_must_wait(
        true, false, false));
    assert(!esp32_mquickjs_future_scheduler_teardown_must_wait(
        true, true, true));
}

int main(void)
{
    test_public_capacity_cannot_consume_internal_reserve();
    test_resource_lane_saturation_and_fifo();
    test_runtime_stop_retains_pending_driver();
    return 0;
}
