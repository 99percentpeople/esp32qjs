#include "esp32_mquickjs_memory_budget.h"

#include <string.h>

static bool budget_role_valid(esp32_mquickjs_memory_budget_role_t role)
{
    return role > ESP32_MQUICKJS_MEMORY_BUDGET_NONE &&
           role < ESP32_MQUICKJS_MEMORY_BUDGET_ROLE_COUNT;
}

bool esp32_mquickjs_memory_budget_init(esp32_mquickjs_memory_budget_t *budget,
    size_t internal_limit, size_t psram_limit, size_t control_reserve)
{
    if (budget == NULL || control_reserve > internal_limit) return false;
    memset(budget, 0, sizeof(*budget));
    budget->regions[0].limit = internal_limit;
    budget->regions[0].control_reserve = control_reserve;
    budget->regions[1].limit = psram_limit;
    return true;
}

bool esp32_mquickjs_memory_budget_reserve(esp32_mquickjs_memory_budget_t *budget,
    esp32_mquickjs_memory_budget_role_t role, const size_t bytes[2])
{
    if (budget == NULL) return false;
    if (!budget_role_valid(role) || bytes == NULL) goto reject;
    for (size_t i = 0; i < ESP32_MQUICKJS_MEMORY_BUDGET_REGIONS; ++i) {
        const esp32_mquickjs_memory_budget_region_t *r = &budget->regions[i];
        if (bytes[i] > r->limit - r->reserved) goto reject;
        if (role != ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL) {
            size_t data = r->reserved - r->roles[ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL];
            if (bytes[i] > r->limit - r->control_reserve - data) goto reject;
        }
    }
    for (size_t i = 0; i < ESP32_MQUICKJS_MEMORY_BUDGET_REGIONS; ++i) {
        esp32_mquickjs_memory_budget_region_t *r = &budget->regions[i];
        r->reserved += bytes[i];
        r->roles[role] += bytes[i];
        if (r->reserved > r->high_water) r->high_water = r->reserved;
    }
    return true;
reject:
    if (budget->rejected != UINT32_MAX) ++budget->rejected;
    return false;
}

bool esp32_mquickjs_memory_budget_release(esp32_mquickjs_memory_budget_t *budget,
    esp32_mquickjs_memory_budget_role_t role, const size_t bytes[2])
{
    if (budget == NULL || bytes == NULL || !budget_role_valid(role)) return false;
    for (size_t i = 0; i < ESP32_MQUICKJS_MEMORY_BUDGET_REGIONS; ++i) {
        if (bytes[i] > budget->regions[i].roles[role]) return false;
    }
    for (size_t i = 0; i < ESP32_MQUICKJS_MEMORY_BUDGET_REGIONS; ++i) {
        budget->regions[i].reserved -= bytes[i];
        budget->regions[i].roles[role] -= bytes[i];
    }
    return true;
}

bool esp32_mquickjs_memory_budget_retire(esp32_mquickjs_memory_budget_t *budget,
    const size_t bytes[2])
{
    if (budget == NULL || bytes == NULL) return false;
    for (size_t i = 0; i < ESP32_MQUICKJS_MEMORY_BUDGET_REGIONS; ++i) {
        if (bytes[i] > budget->regions[i].roles[ESP32_MQUICKJS_MEMORY_BUDGET_POOL]) return false;
    }
    for (size_t i = 0; i < ESP32_MQUICKJS_MEMORY_BUDGET_REGIONS; ++i) {
        budget->regions[i].roles[ESP32_MQUICKJS_MEMORY_BUDGET_POOL] -= bytes[i];
        budget->regions[i].roles[ESP32_MQUICKJS_MEMORY_BUDGET_RETIRED_POOL] += bytes[i];
    }
    return true;
}

void esp32_mquickjs_memory_budget_reset_counters(esp32_mquickjs_memory_budget_t *budget)
{
    if (budget == NULL) return;
    budget->rejected = 0;
    for (size_t i = 0; i < ESP32_MQUICKJS_MEMORY_BUDGET_REGIONS; ++i)
        budget->regions[i].high_water = budget->regions[i].reserved;
}

const char *esp32_mquickjs_memory_budget_role_name(
    esp32_mquickjs_memory_budget_role_t role)
{
    static const char *const names[] = {
        "none", "control", "pool", "retiredPool", "queue", "tx", "stack", "copy",
    };
    return budget_role_valid(role) ? names[role] : "none";
}
