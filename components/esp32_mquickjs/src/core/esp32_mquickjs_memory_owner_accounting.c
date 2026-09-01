#include "esp32_mquickjs_memory_owner_accounting.h"

#include <limits.h>
#include <string.h>

static int memory_owner_entry_compare(
    const esp32_mquickjs_memory_owner_entry_t *left,
    const esp32_mquickjs_memory_owner_entry_t *right)
{
    int owner_order = strcmp(left->owner, right->owner);

    if (owner_order != 0) {
        return owner_order;
    }
    if (left->memory_class != right->memory_class) {
        return left->memory_class < right->memory_class ? -1 : 1;
    }
    if (left->region != right->region) {
        return left->region < right->region ? -1 : 1;
    }
    return 0;
}

static esp32_mquickjs_memory_owner_entry_t *memory_owner_find(
    esp32_mquickjs_memory_owner_accounting_t *accounting,
    const char *owner,
    uint8_t memory_class,
    uint8_t region)
{
    size_t index;

    for (index = 0; index < accounting->count; ++index) {
        esp32_mquickjs_memory_owner_entry_t *entry =
            &accounting->entries[index];

        if (entry->memory_class == memory_class &&
            entry->region == region && strcmp(entry->owner, owner) == 0) {
            return entry;
        }
    }
    return NULL;
}

bool esp32_mquickjs_memory_owner_add(
    esp32_mquickjs_memory_owner_accounting_t *accounting,
    const char *owner,
    uint8_t memory_class,
    uint8_t region,
    size_t bytes,
    uint32_t blocks)
{
    esp32_mquickjs_memory_owner_entry_t *entry;

    if (accounting == NULL || owner == NULL || owner[0] == '\0' ||
        (bytes == 0 && blocks == 0)) {
        return false;
    }
    entry = memory_owner_find(accounting, owner, memory_class, region);
    if (entry != NULL) {
        if (bytes > SIZE_MAX - entry->bytes ||
            blocks > UINT32_MAX - entry->blocks) {
            return false;
        }
        entry->bytes += bytes;
        entry->blocks += blocks;
        return true;
    }
    if (accounting->count >= ESP32_MQUICKJS_MEMORY_MAX_OWNER_ENTRIES) {
        return false;
    }
    entry = &accounting->entries[accounting->count++];
    entry->owner = owner;
    entry->memory_class = memory_class;
    entry->region = region;
    entry->bytes = bytes;
    entry->blocks = blocks;
    return true;
}

bool esp32_mquickjs_memory_owner_remove(
    esp32_mquickjs_memory_owner_accounting_t *accounting,
    const char *owner,
    uint8_t memory_class,
    uint8_t region,
    size_t bytes,
    uint32_t blocks)
{
    esp32_mquickjs_memory_owner_entry_t *entry;
    size_t index;

    if (accounting == NULL || owner == NULL || owner[0] == '\0' ||
        (bytes == 0 && blocks == 0)) {
        return false;
    }
    entry = memory_owner_find(accounting, owner, memory_class, region);
    if (entry == NULL || entry->bytes < bytes || entry->blocks < blocks) {
        return false;
    }
    entry->bytes -= bytes;
    entry->blocks -= blocks;
    if (entry->bytes != 0 || entry->blocks != 0) {
        return true;
    }
    index = (size_t)(entry - accounting->entries);
    accounting->count--;
    if (index != accounting->count) {
        accounting->entries[index] = accounting->entries[accounting->count];
    }
    memset(&accounting->entries[accounting->count], 0,
           sizeof(accounting->entries[accounting->count]));
    return true;
}

size_t esp32_mquickjs_memory_owner_snapshot(
    const esp32_mquickjs_memory_owner_accounting_t *accounting,
    esp32_mquickjs_memory_owner_entry_t *entries,
    size_t capacity)
{
    size_t count;
    size_t index;

    if (accounting == NULL || entries == NULL || capacity == 0) {
        return 0;
    }
    count = accounting->count < capacity ? accounting->count : capacity;
    memcpy(entries, accounting->entries, count * sizeof(*entries));
    for (index = 1; index < count; ++index) {
        esp32_mquickjs_memory_owner_entry_t current = entries[index];
        size_t position = index;

        while (position > 0 &&
               memory_owner_entry_compare(&current,
                                          &entries[position - 1]) < 0) {
            entries[position] = entries[position - 1];
            position--;
        }
        entries[position] = current;
    }
    return count;
}
