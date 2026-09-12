#include "esp32_mquickjs_wifi_csi_store.h"

uint32_t esp32_mquickjs_wifi_csi_store_reset_counters(esp32_mquickjs_wifi_csi_store_t *store)
{
    if (store == NULL || store->lock == NULL || store->unlock == NULL) return 0;
    uint32_t unavailable = 0;
    store->lock(store->opaque);
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS; ++i) {
        esp32_mquickjs_wifi_csi_store_entry_t *entry = &store->entries[i];
        if (entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_FREE) continue;
        if (entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE ||
            entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED)
            esp32_mquickjs_wifi_csi_resources_reset_counters(entry->resources);
        else ++unavailable;
    }
    store->unlock(store->opaque);
    return unavailable;
}

#include <limits.h>
#include <string.h>

static esp32_mquickjs_wifi_csi_store_entry_t *wifi_csi_store_find(
    esp32_mquickjs_wifi_csi_store_t *store,
    esp32_mquickjs_wifi_csi_resources_t *resources)
{
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS; ++i) {
        /* A retiring tombstone remains charged after allocator.free(ptr),
         * until that call returns. A new allocation can already reuse ptr;
         * only live entries participate in pointer lookup. */
        if (store->entries[i].resources == resources && resources != NULL &&
            (store->entries[i].state == ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE ||
             store->entries[i].state == ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED))
            return &store->entries[i];
    }
    return NULL;
}

static void wifi_csi_store_clear(esp32_mquickjs_wifi_csi_store_t *store,
    esp32_mquickjs_wifi_csi_store_entry_t *entry)
{
    store->reserved_slots -= entry->capacity;
    memset(entry, 0, sizeof(*entry));
}

esp32_mquickjs_wifi_csi_resources_t *esp32_mquickjs_wifi_csi_store_open(
    esp32_mquickjs_wifi_csi_store_t *store, uint32_t capacity, uint32_t max_packet_bytes,
    const esp32_mquickjs_wifi_csi_allocator_t *allocator,
    esp32_mquickjs_wifi_csi_store_result_t *result)
{
    esp32_mquickjs_wifi_csi_store_entry_t *entry = NULL;
    esp32_mquickjs_wifi_csi_resources_t *resources;
    size_t slots_bytes, payload_bytes;

    if (result == NULL) return NULL;
    *result = ESP32_MQUICKJS_WIFI_CSI_STORE_INVALID;
    if (store == NULL || store->lock == NULL || store->unlock == NULL ||
        allocator == NULL || allocator->calloc_fn == NULL ||
        allocator->malloc_fn == NULL || allocator->free_fn == NULL ||
        capacity == 0 || capacity > ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY ||
        store->max_frame_bytes == 0)
        return NULL;
    if (!esp32_mquickjs_wifi_csi_resources_size(capacity, store->max_frame_bytes,
            max_packet_bytes, &slots_bytes, &payload_bytes) ||
        slots_bytes > SIZE_MAX - sizeof(*resources) ||
        payload_bytes > SIZE_MAX - sizeof(*resources) - slots_bytes) return NULL;

    store->lock(store->opaque);
    *result = ESP32_MQUICKJS_WIFI_CSI_STORE_BUDGET;
    if (store->next_generation == 0) {
        *result = ESP32_MQUICKJS_WIFI_CSI_STORE_IDENTITY;
    } else if (store->reserved_slots <= store->maximum_slots &&
               capacity <= store->maximum_slots - store->reserved_slots) {
        for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS; ++i) {
            if (store->entries[i].state == ESP32_MQUICKJS_WIFI_CSI_STORE_FREE) {
                entry = &store->entries[i];
                entry->state = ESP32_MQUICKJS_WIFI_CSI_STORE_ALLOCATING;
                entry->generation = store->next_generation++;
                entry->capacity = capacity;
                entry->bytes = sizeof(*resources) + slots_bytes + payload_bytes;
                store->reserved_slots += capacity;
                break;
            }
        }
    }
    store->unlock(store->opaque);
    if (entry == NULL) return NULL;

    resources = allocator->calloc_fn(1, sizeof(*resources), allocator->opaque);
    if (resources == NULL || !esp32_mquickjs_wifi_csi_resources_init(resources,
            entry->generation, capacity, store->max_frame_bytes, max_packet_bytes, allocator)) {
        if (resources != NULL) allocator->free_fn(resources, allocator->opaque);
        /* Do not make budget reusable until all allocation cleanup returns. */
        store->lock(store->opaque);
        wifi_csi_store_clear(store, entry);
        store->unlock(store->opaque);
        *result = ESP32_MQUICKJS_WIFI_CSI_STORE_MEMORY;
        return NULL;
    }
    store->lock(store->opaque);
    entry->resources = resources;
    entry->pins = 1;
    entry->state = ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE;
    store->unlock(store->opaque);
    *result = ESP32_MQUICKJS_WIFI_CSI_STORE_OK;
    return resources;
}

esp32_mquickjs_wifi_csi_resources_t *esp32_mquickjs_wifi_csi_store_acquire(
    esp32_mquickjs_wifi_csi_store_t *store, uint32_t generation)
{
    esp32_mquickjs_wifi_csi_resources_t *resources = NULL;
    store->lock(store->opaque);
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS; ++i) {
        esp32_mquickjs_wifi_csi_store_entry_t *entry = &store->entries[i];
        if (entry->generation == generation && entry->pins != UINT32_MAX &&
            (entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE ||
             entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED)) {
            ++entry->pins;
            resources = entry->resources;
            break;
        }
    }
    store->unlock(store->opaque);
    return resources;
}

void esp32_mquickjs_wifi_csi_store_release(
    esp32_mquickjs_wifi_csi_store_t *store,
    esp32_mquickjs_wifi_csi_resources_t *resources)
{
    bool destroy = false;
    esp32_mquickjs_wifi_csi_store_entry_t *entry;
    if (resources == NULL) return;
    store->lock(store->opaque);
    entry = wifi_csi_store_find(store, resources);
    if (entry != NULL && entry->pins != 0 &&
        (entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE ||
         entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED)) {
        --entry->pins;
        if (entry->pins == 0 && entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED &&
            atomic_load_explicit(&resources->callbacks_active, memory_order_acquire) == 0 &&
            atomic_load_explicit(&resources->counters.leased_frames, memory_order_acquire) == 0) {
            entry->state = ESP32_MQUICKJS_WIFI_CSI_STORE_RETIRING;
            destroy = true;
        }
    }
    store->unlock(store->opaque);
    if (!destroy) return;
    esp32_mquickjs_wifi_csi_allocator_t allocator = resources->allocator;
    bool released = esp32_mquickjs_wifi_csi_resources_deinit(resources);
    if (released) allocator.free_fn(resources, allocator.opaque);
    store->lock(store->opaque);
    if (released) wifi_csi_store_clear(store, entry);
    else entry->state = ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED;
    store->unlock(store->opaque);
}

bool esp32_mquickjs_wifi_csi_store_retire(
    esp32_mquickjs_wifi_csi_store_t *store,
    esp32_mquickjs_wifi_csi_resources_t *resources)
{
    bool retired = false;
    store->lock(store->opaque);
    esp32_mquickjs_wifi_csi_store_entry_t *entry = wifi_csi_store_find(store, resources);
    if (entry != NULL && entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE &&
        entry->pins != 0 && !atomic_load_explicit(&resources->accepting, memory_order_acquire) &&
        atomic_load_explicit(&resources->callbacks_active, memory_order_acquire) == 0) {
        entry->state = ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED;
        retired = true;
    }
    store->unlock(store->opaque);
    if (retired) {
        esp32_mquickjs_wifi_csi_allocator_t allocator = resources->allocator;
        if (allocator.retire_fn != NULL) {
            allocator.retire_fn(resources->payload_storage, allocator.opaque);
            allocator.retire_fn(resources->slots, allocator.opaque);
            allocator.retire_fn(resources, allocator.opaque);
        }
        esp32_mquickjs_wifi_csi_store_release(store, resources);
    }
    return retired;
}

size_t esp32_mquickjs_wifi_csi_store_snapshot(
    esp32_mquickjs_wifi_csi_store_t *store,
    esp32_mquickjs_wifi_csi_store_snapshot_t
        output[ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS])
{
    size_t count = 0;
    store->lock(store->opaque);
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS; ++i) {
        const esp32_mquickjs_wifi_csi_store_entry_t *entry = &store->entries[i];
        if (entry->state == ESP32_MQUICKJS_WIFI_CSI_STORE_FREE) continue;
        esp32_mquickjs_wifi_csi_store_snapshot_t *item = &output[count++];
        *item = (esp32_mquickjs_wifi_csi_store_snapshot_t){
            .generation = entry->generation, .capacity = entry->capacity,
            .bytes = entry->bytes, .state = entry->state,
        };
        /* Allocating/freeing entries remain charged, but their pointers are
         * unavailable. Never dereference a resource once its free has begun. */
        if (entry->state != ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE &&
            entry->state != ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED) continue;
        esp32_mquickjs_wifi_csi_resources_t *r = entry->resources;
        item->free_slots = esp32_mquickjs_native_pool_available(&r->pool);
        item->identity_exhausted = atomic_load_explicit(&r->identity_exhausted, memory_order_acquire);
#define CSI_STORE_COUNTER(name) item->name = atomic_load_explicit(&r->counters.name, memory_order_acquire)
        CSI_STORE_COUNTER(leased_frames);
        CSI_STORE_COUNTER(callbacks);
        CSI_STORE_COUNTER(accepted);
        CSI_STORE_COUNTER(dropped_pool_full);
        CSI_STORE_COUNTER(dropped_queue_full);
        CSI_STORE_COUNTER(dropped_closing);
        CSI_STORE_COUNTER(dropped_identity_exhausted);
#undef CSI_STORE_COUNTER
    }
    store->unlock(store->opaque);
    return count;
}
