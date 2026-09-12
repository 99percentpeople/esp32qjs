#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One boot-scoped ledger, serialized by the existing memory manager lock. */
typedef enum {
    ESP32_MQUICKJS_MEMORY_BUDGET_NONE,
    ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL,
    ESP32_MQUICKJS_MEMORY_BUDGET_POOL,
    ESP32_MQUICKJS_MEMORY_BUDGET_RETIRED_POOL,
    ESP32_MQUICKJS_MEMORY_BUDGET_QUEUE,
    ESP32_MQUICKJS_MEMORY_BUDGET_TX,
    ESP32_MQUICKJS_MEMORY_BUDGET_STACK,
    ESP32_MQUICKJS_MEMORY_BUDGET_COPY,
    ESP32_MQUICKJS_MEMORY_BUDGET_ROLE_COUNT,
} esp32_mquickjs_memory_budget_role_t;

#define ESP32_MQUICKJS_MEMORY_BUDGET_REGIONS 2U

typedef struct {
    size_t limit;
    size_t control_reserve;
    /* Includes allocator calls in progress and tracking-node requested bytes. */
    size_t reserved;
    size_t high_water;
    size_t roles[ESP32_MQUICKJS_MEMORY_BUDGET_ROLE_COUNT];
} esp32_mquickjs_memory_budget_region_t;

typedef struct {
    /* Same ordering as memory_region_t: internal, PSRAM. */
    esp32_mquickjs_memory_budget_region_t regions[ESP32_MQUICKJS_MEMORY_BUDGET_REGIONS];
    uint32_t rejected;
} esp32_mquickjs_memory_budget_t;

bool esp32_mquickjs_memory_budget_init(esp32_mquickjs_memory_budget_t *budget,
    size_t internal_limit, size_t psram_limit, size_t control_reserve);

/* The caller owns each reservation; admission is atomic across both regions. */
bool esp32_mquickjs_memory_budget_reserve(esp32_mquickjs_memory_budget_t *budget,
    esp32_mquickjs_memory_budget_role_t role, const size_t bytes[2]);
bool esp32_mquickjs_memory_budget_release(esp32_mquickjs_memory_budget_t *budget,
    esp32_mquickjs_memory_budget_role_t role, const size_t bytes[2]);
bool esp32_mquickjs_memory_budget_retire(esp32_mquickjs_memory_budget_t *budget,
    const size_t bytes[2]);
/* Caller holds the manager lock. Preserve all reservations, roles and limits. */
void esp32_mquickjs_memory_budget_reset_counters(esp32_mquickjs_memory_budget_t *budget);
const char *esp32_mquickjs_memory_budget_role_name(
    esp32_mquickjs_memory_budget_role_t role);
