#include "esp32_mquickjs_memory_budget.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    esp32_mquickjs_memory_budget_t b = {0};
    const size_t pool[] = {80, 100}, control[] = {20, 0}, one[] = {1, 0};
    assert(!esp32_mquickjs_memory_budget_init(&b, 100, 200, 101));
    assert(esp32_mquickjs_memory_budget_init(&b, 100, 200, 20));
    assert(esp32_mquickjs_memory_budget_reserve(&b, ESP32_MQUICKJS_MEMORY_BUDGET_POOL, pool));
    assert(!esp32_mquickjs_memory_budget_reserve(&b, ESP32_MQUICKJS_MEMORY_BUDGET_TX, one));
    assert(esp32_mquickjs_memory_budget_reserve(&b, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL, control));
    assert(b.regions[0].reserved == 100 && b.regions[1].reserved == 100);
    assert(esp32_mquickjs_memory_budget_retire(&b, pool));
    assert(b.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_POOL] == 0);
    assert(b.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_RETIRED_POOL] == 80);
    assert(!esp32_mquickjs_memory_budget_release(&b, ESP32_MQUICKJS_MEMORY_BUDGET_POOL, pool));
    assert(!esp32_mquickjs_memory_budget_reserve(&b, ESP32_MQUICKJS_MEMORY_BUDGET_COPY, one));
    assert(esp32_mquickjs_memory_budget_release(&b, ESP32_MQUICKJS_MEMORY_BUDGET_RETIRED_POOL, pool));
    assert(esp32_mquickjs_memory_budget_release(&b, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL, control));
    assert(b.regions[0].high_water == 100 && b.regions[1].high_water == 100);
    assert(!b.regions[0].reserved && !b.regions[1].reserved);
    const size_t cross_region[] = {1, 201};
    assert(!esp32_mquickjs_memory_budget_reserve(&b, ESP32_MQUICKJS_MEMORY_BUDGET_POOL, cross_region));
    assert(!b.regions[0].reserved && !b.regions[1].reserved);
    const size_t extra_control[] = {50, 0}, data[] = {50, 0};
    assert(esp32_mquickjs_memory_budget_reserve(&b, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL, extra_control));
    assert(esp32_mquickjs_memory_budget_reserve(&b, ESP32_MQUICKJS_MEMORY_BUDGET_TX, data));
    assert(!esp32_mquickjs_memory_budget_reserve(&b, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL, one));
    assert(esp32_mquickjs_memory_budget_init(&b, SIZE_MAX, SIZE_MAX, 0));
    const size_t maximum[] = {SIZE_MAX, SIZE_MAX};
    assert(esp32_mquickjs_memory_budget_reserve(&b, ESP32_MQUICKJS_MEMORY_BUDGET_POOL, maximum));
    assert(!esp32_mquickjs_memory_budget_reserve(&b, ESP32_MQUICKJS_MEMORY_BUDGET_TX, one));
    assert(esp32_mquickjs_memory_budget_retire(&b, maximum));
    assert(esp32_mquickjs_memory_budget_release(&b, ESP32_MQUICKJS_MEMORY_BUDGET_RETIRED_POOL, maximum));
    b.rejected = UINT32_MAX;
    assert(!esp32_mquickjs_memory_budget_reserve(&b, ESP32_MQUICKJS_MEMORY_BUDGET_NONE, one));
    assert(b.rejected == UINT32_MAX);
    return 0;
}
