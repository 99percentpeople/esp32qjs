#include "esp32_mquickjs_reaper.h"

#include <string.h>

void esp32_mquickjs_reaper_init(esp32_mquickjs_reaper_registry_t *registry)
{
    if (registry != NULL) {
        memset(registry, 0, sizeof(*registry));
    }
}

bool esp32_mquickjs_reaper_register(esp32_mquickjs_reaper_registry_t *registry,
                                    esp32_mquickjs_reaper_fn_t reap,
                                    void *opaque)
{
    size_t available = ESP32_MQUICKJS_REAPER_GENERAL_CAPACITY;

    if (registry == NULL || reap == NULL) {
        return false;
    }
    for (size_t index = 0; index < ESP32_MQUICKJS_REAPER_GENERAL_CAPACITY;
         ++index) {
        esp32_mquickjs_reaper_entry_t *entry = &registry->entries[index];

        if (entry->active) {
            if (entry->reap == reap && entry->opaque == opaque) {
                return true;
            }
        } else if (available == ESP32_MQUICKJS_REAPER_GENERAL_CAPACITY) {
            available = index;
        }
    }
    if (available == ESP32_MQUICKJS_REAPER_GENERAL_CAPACITY) {
        return false;
    }
    registry->entries[available].reap = reap;
    registry->entries[available].opaque = opaque;
    registry->entries[available].active = true;
    registry->pending++;
    return true;
}

bool esp32_mquickjs_reaper_register_reserved(
    esp32_mquickjs_reaper_registry_t *registry,
    size_t reserved_slot,
    esp32_mquickjs_reaper_fn_t reap,
    void *opaque)
{
    size_t index;
    esp32_mquickjs_reaper_entry_t *entry;

    if (registry == NULL || reap == NULL ||
        reserved_slot >= ESP32_MQUICKJS_REAPER_RESERVED_CAPACITY) {
        return false;
    }
    index = ESP32_MQUICKJS_REAPER_GENERAL_CAPACITY + reserved_slot;
    entry = &registry->entries[index];
    if (entry->active) {
        return entry->reap == reap && entry->opaque == opaque;
    }
    entry->reap = reap;
    entry->opaque = opaque;
    entry->active = true;
    registry->pending++;
    return true;
}

bool esp32_mquickjs_reaper_unregister(esp32_mquickjs_reaper_registry_t *registry,
                                      esp32_mquickjs_reaper_fn_t reap,
                                      void *opaque)
{
    if (registry == NULL || reap == NULL) {
        return false;
    }
    for (size_t index = 0; index < ESP32_MQUICKJS_REAPER_CAPACITY; ++index) {
        esp32_mquickjs_reaper_entry_t *entry = &registry->entries[index];

        if (!entry->active || entry->reap != reap || entry->opaque != opaque) {
            continue;
        }
        memset(entry, 0, sizeof(*entry));
        registry->pending--;
        if (registry->pending == 0) {
            registry->cursor = 0;
        }
        return true;
    }
    return false;
}

size_t esp32_mquickjs_reaper_poll(esp32_mquickjs_reaper_registry_t *registry,
                                  size_t batch_limit,
                                  size_t *out_attempted)
{
    size_t attempted = 0;
    size_t completed = 0;
    size_t scanned = 0;
    size_t index;

    if (out_attempted != NULL) {
        *out_attempted = 0;
    }
    if (registry == NULL || registry->pending == 0 || batch_limit == 0) {
        return 0;
    }
    index = registry->cursor % ESP32_MQUICKJS_REAPER_CAPACITY;
    while (attempted < batch_limit &&
           scanned < ESP32_MQUICKJS_REAPER_CAPACITY &&
           registry->pending > 0) {
        esp32_mquickjs_reaper_entry_t *entry = &registry->entries[index];

        index = (index + 1U) % ESP32_MQUICKJS_REAPER_CAPACITY;
        scanned++;
        if (!entry->active) {
            continue;
        }
        attempted++;
        if (!entry->reap(entry->opaque)) {
            continue;
        }
        memset(entry, 0, sizeof(*entry));
        registry->pending--;
        completed++;
    }
    registry->cursor = registry->pending == 0 ? 0 : index;
    if (out_attempted != NULL) {
        *out_attempted = attempted;
    }
    return completed;
}

size_t esp32_mquickjs_reaper_pending(
    const esp32_mquickjs_reaper_registry_t *registry)
{
    return registry == NULL ? 0 : registry->pending;
}

size_t esp32_mquickjs_reaper_capacity(
    const esp32_mquickjs_reaper_registry_t *registry)
{
    return registry == NULL ? 0 : ESP32_MQUICKJS_REAPER_CAPACITY;
}
