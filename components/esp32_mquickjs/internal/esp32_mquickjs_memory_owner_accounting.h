#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32_MQUICKJS_MEMORY_MAX_OWNER_ENTRIES 48U

typedef struct {
    const char *owner;
    uint8_t memory_class;
    uint8_t region;
    size_t bytes;
    uint32_t blocks;
} esp32_mquickjs_memory_owner_entry_t;

typedef struct {
    esp32_mquickjs_memory_owner_entry_t
        entries[ESP32_MQUICKJS_MEMORY_MAX_OWNER_ENTRIES];
    size_t count;
} esp32_mquickjs_memory_owner_accounting_t;

bool esp32_mquickjs_memory_owner_add(
    esp32_mquickjs_memory_owner_accounting_t *accounting,
    const char *owner,
    uint8_t memory_class,
    uint8_t region,
    size_t bytes,
    uint32_t blocks);
bool esp32_mquickjs_memory_owner_remove(
    esp32_mquickjs_memory_owner_accounting_t *accounting,
    const char *owner,
    uint8_t memory_class,
    uint8_t region,
    size_t bytes,
    uint32_t blocks);
size_t esp32_mquickjs_memory_owner_snapshot(
    const esp32_mquickjs_memory_owner_accounting_t *accounting,
    esp32_mquickjs_memory_owner_entry_t *entries,
    size_t capacity);
