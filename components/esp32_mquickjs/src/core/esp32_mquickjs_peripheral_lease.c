#include "esp32_mquickjs_peripheral_lease.h"

#include <string.h>

#define ESP32_MQUICKJS_PERIPHERAL_MAX_INSTANCES 8

typedef struct {
    esp32_mquickjs_peripheral_owner_t owner;
    uint32_t generation;
} esp32_mquickjs_peripheral_entry_t;

static esp32_mquickjs_peripheral_entry_t
    s_entries[ESP32_MQUICKJS_PERIPHERAL_KIND_COUNT]
             [ESP32_MQUICKJS_PERIPHERAL_MAX_INSTANCES];
static uint32_t s_next_generation = 1;

void esp32_mquickjs_peripheral_leases_reset(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_next_generation = 1;
}

static uint32_t peripheral_take_generation(void)
{
    uint32_t generation = s_next_generation++;

    if (generation == 0) {
        generation = s_next_generation++;
    }
    return generation;
}

bool esp32_mquickjs_peripheral_lease_acquire(
    esp32_mquickjs_peripheral_kind_t kind,
    int index,
    esp32_mquickjs_peripheral_owner_t owner,
    esp32_mquickjs_peripheral_lease_t *out_lease)
{
    esp32_mquickjs_peripheral_entry_t *entry;

    if (out_lease == NULL || kind < 0 ||
        kind >= ESP32_MQUICKJS_PERIPHERAL_KIND_COUNT || index < 0 ||
        index >= ESP32_MQUICKJS_PERIPHERAL_MAX_INSTANCES ||
        owner == ESP32_MQUICKJS_PERIPHERAL_OWNER_NONE) {
        return false;
    }
    entry = &s_entries[kind][index];
    if (entry->owner != ESP32_MQUICKJS_PERIPHERAL_OWNER_NONE) {
        return false;
    }
    entry->owner = owner;
    entry->generation = peripheral_take_generation();
    out_lease->kind = kind;
    out_lease->owner = owner;
    out_lease->index = (int8_t)index;
    out_lease->generation = entry->generation;
    out_lease->held = true;
    return true;
}

bool esp32_mquickjs_peripheral_lease_acquire_any(
    esp32_mquickjs_peripheral_kind_t kind,
    int count,
    esp32_mquickjs_peripheral_owner_t owner,
    esp32_mquickjs_peripheral_lease_t *out_lease)
{
    int index;

    if (count > ESP32_MQUICKJS_PERIPHERAL_MAX_INSTANCES) {
        count = ESP32_MQUICKJS_PERIPHERAL_MAX_INSTANCES;
    }
    for (index = 0; index < count; ++index) {
        if (esp32_mquickjs_peripheral_lease_acquire(kind, index, owner,
                                                    out_lease)) {
            return true;
        }
    }
    return false;
}

bool esp32_mquickjs_peripheral_lease_is_held(
    const esp32_mquickjs_peripheral_lease_t *lease)
{
    const esp32_mquickjs_peripheral_entry_t *entry;

    if (lease == NULL || !lease->held || lease->kind < 0 ||
        lease->kind >= ESP32_MQUICKJS_PERIPHERAL_KIND_COUNT ||
        lease->index < 0 ||
        lease->index >= ESP32_MQUICKJS_PERIPHERAL_MAX_INSTANCES) {
        return false;
    }
    entry = &s_entries[lease->kind][lease->index];
    return entry->owner == lease->owner &&
           entry->generation == lease->generation;
}

void esp32_mquickjs_peripheral_lease_release(
    esp32_mquickjs_peripheral_lease_t *lease)
{
    esp32_mquickjs_peripheral_entry_t *entry;

    if (!esp32_mquickjs_peripheral_lease_is_held(lease)) {
        if (lease != NULL) {
            memset(lease, 0, sizeof(*lease));
        }
        return;
    }
    entry = &s_entries[lease->kind][lease->index];
    entry->owner = ESP32_MQUICKJS_PERIPHERAL_OWNER_NONE;
    memset(lease, 0, sizeof(*lease));
}
