#include "esp32_mquickjs_wifi_monitor_resources.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include <string.h>

enum { MONITOR_FREE, MONITOR_WRITING, MONITOR_EVENT, MONITOR_PUBLIC, MONITOR_CLOSED };

void esp32_mquickjs_wifi_monitor_resources_reset_counters(esp32_mquickjs_wifi_monitor_resources_t *resources)
{
    if (resources == NULL || !resources->lock_initialized) return;
    portENTER_CRITICAL(&resources->lock);
    uint32_t leased = resources->counters.leased_frames;
    uint32_t publishers = resources->counters.publishers;
    memset(&resources->counters, 0, sizeof(resources->counters));
    resources->counters.leased_frames = leased;
    resources->counters.publishers = publishers;
    portEXIT_CRITICAL(&resources->lock);
}

static void monitor_add(uint64_t *counter, uint64_t amount)
{
    *counter = amount > UINT64_MAX - *counter ? UINT64_MAX : *counter + amount;
}

bool esp32_mquickjs_wifi_monitor_resources_size(uint32_t capacity, uint32_t snap_length, size_t *output)
{
    if (output == NULL) return false;
    *output = 0;
    if (capacity == 0 || capacity > ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY || snap_length == 0 ||
        snap_length > ESP32_MQUICKJS_WIFI_MONITOR_MAX_SNAP_LENGTH ||
        sizeof(esp32_mquickjs_wifi_monitor_slot_t) > SIZE_MAX / capacity || snap_length > SIZE_MAX / capacity) return false;
    size_t slots = capacity * sizeof(esp32_mquickjs_wifi_monitor_slot_t), payload = (size_t)capacity * snap_length;
    if (slots > SIZE_MAX - payload) return false;
    *output = slots + payload;
    return true;
}

bool esp32_mquickjs_wifi_monitor_resources_init(esp32_mquickjs_wifi_monitor_resources_t *resources,
    uint32_t generation, uint32_t capacity, uint32_t snap_length, bool require_complete,
    const esp32_mquickjs_wifi_monitor_allocator_t *allocator,
    esp32_mquickjs_wifi_monitor_publish_fn publish, void *publish_opaque)
{
    size_t bytes;
    if (resources == NULL || generation == 0 || allocator == NULL || allocator->calloc_fn == NULL ||
        allocator->malloc_fn == NULL || allocator->free_fn == NULL || publish == NULL ||
        !esp32_mquickjs_wifi_monitor_resources_size(capacity, snap_length, &bytes)) return false;
    if (!resources->lock_initialized) {
        portMUX_INITIALIZE(&resources->lock);
        resources->lock_initialized = true;
    }
    portENTER_CRITICAL(&resources->lock);
    bool available = !resources->initialized && generation > resources->generation;
    portEXIT_CRITICAL(&resources->lock);
    if (!available) return false;
    esp32_mquickjs_wifi_monitor_slot_t *slots = allocator->calloc_fn(capacity, sizeof(*slots), allocator->opaque);
    if (slots == NULL) return false;
    uint8_t *payload = allocator->malloc_fn((size_t)capacity * snap_length, allocator->opaque);
    if (payload == NULL) { allocator->free_fn(slots, allocator->opaque); return false; }
    portENTER_CRITICAL(&resources->lock);
    available = !resources->initialized && generation > resources->generation;
    if (available) available = esp32_mquickjs_native_pool_init(&resources->pool, capacity);
    if (available) {
        resources->slots = slots;
        resources->payload = payload;
        resources->allocator = *allocator;
        resources->publish = publish;
        resources->publish_opaque = publish_opaque;
        resources->generation = generation;
        resources->next_identity = 1;
        resources->capacity = (uint16_t)capacity;
        resources->snap_length = (uint16_t)snap_length;
        resources->require_complete = require_complete;
        resources->accepting = false;
        memset(&resources->counters, 0, sizeof(resources->counters));
        resources->initialized = true;
    }
    portEXIT_CRITICAL(&resources->lock);
    if (!available) {
        allocator->free_fn(payload, allocator->opaque);
        allocator->free_fn(slots, allocator->opaque);
    }
    return available;
}

bool esp32_mquickjs_wifi_monitor_resources_set_accepting(esp32_mquickjs_wifi_monitor_resources_t *resources, bool accepting)
{
    if (resources == NULL || !resources->lock_initialized) return false;
    portENTER_CRITICAL(&resources->lock);
    bool valid = resources->initialized && (!accepting || resources->next_identity != 0);
    if (valid) resources->accepting = accepting;
    portEXIT_CRITICAL(&resources->lock);
    return valid;
}

bool esp32_mquickjs_wifi_monitor_resources_deinit(esp32_mquickjs_wifi_monitor_resources_t *resources)
{
    if (resources == NULL || !resources->lock_initialized) return false;
    portENTER_CRITICAL(&resources->lock);
    bool ready = resources->initialized && !resources->accepting &&
        resources->counters.publishers == 0 && resources->counters.leased_frames == 0;
    esp32_mquickjs_wifi_monitor_slot_t *slots = NULL;
    uint8_t *payload = NULL;
    esp32_mquickjs_wifi_monitor_allocator_t allocator = {0};
    if (ready) {
        slots = resources->slots;
        payload = resources->payload;
        allocator = resources->allocator;
        resources->slots = NULL;
        resources->payload = NULL;
        resources->initialized = false;
        resources->capacity = resources->snap_length = 0;
        resources->publish = NULL;
        resources->publish_opaque = NULL;
    }
    portEXIT_CRITICAL(&resources->lock);
    if (ready) {
        allocator.free_fn(payload, allocator.opaque);
        allocator.free_fn(slots, allocator.opaque);
    }
    return ready;
}

void esp32_mquickjs_wifi_monitor_resources_snapshot(esp32_mquickjs_wifi_monitor_resources_t *resources,
    esp32_mquickjs_wifi_monitor_snapshot_t *output)
{
    if (output == NULL) return;
    memset(output, 0, sizeof(*output));
    if (resources == NULL || !resources->lock_initialized) return;
    portENTER_CRITICAL(&resources->lock);
    output->initialized = resources->initialized;
    output->accepting = resources->accepting;
    output->identity_exhausted = resources->generation != 0 && resources->next_identity == 0;
    output->generation = resources->generation;
    output->counters = resources->counters;
    if (resources->initialized) {
        output->free_slots = esp32_mquickjs_native_pool_available(&resources->pool);
        (void)esp32_mquickjs_wifi_monitor_resources_size(resources->capacity, resources->snap_length, &output->allocated_bytes);
    }
    portEXIT_CRITICAL(&resources->lock);
}

static esp32_mquickjs_wifi_monitor_slot_t *monitor_slot_locked(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event)
{
    if (!resources->initialized || event == NULL || event->generation != resources->generation ||
        event->index >= resources->capacity) return NULL;
    esp32_mquickjs_wifi_monitor_slot_t *slot = &resources->slots[event->index];
    if (slot->owner == MONITOR_FREE || esp32_mquickjs_native_lease_is_stale(&slot->lease, event->identity)) return NULL;
    return slot;
}

static void monitor_account_return_locked(esp32_mquickjs_wifi_monitor_resources_t *resources,
    esp32_mquickjs_wifi_monitor_slot_t *slot)
{
    /* Pool release, accounting and possible re-acquire are serialized by this
     * lock. A last ref cannot account a newly reused slot's lease by mistake. */
    if (slot->owner != MONITOR_FREE && atomic_load_explicit(&slot->lease.returned, memory_order_acquire)) {
        slot->owner = MONITOR_FREE;
        --resources->counters.leased_frames;
    }
}

static bool monitor_close_locked(esp32_mquickjs_wifi_monitor_resources_t *resources,
    esp32_mquickjs_wifi_monitor_slot_t *slot)
{
    slot->owner = MONITOR_CLOSED;
    bool result = esp32_mquickjs_native_lease_request_close(&slot->lease, slot->lease.generation);
    monitor_account_return_locked(resources, slot);
    return result;
}

esp32_mquickjs_wifi_monitor_publish_result_t esp32_mquickjs_wifi_monitor_publish(
    esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_rx_target_view_t *view,
    esp32_mquickjs_wifi_rx_filter_result_t filter_result, uint64_t callback_time_us)
{
    if (resources == NULL || !resources->lock_initialized) return ESP32_MQUICKJS_WIFI_MONITOR_CLOSING;
    portENTER_CRITICAL(&resources->lock);
    esp32_mquickjs_wifi_monitor_publish_result_t result = ESP32_MQUICKJS_WIFI_MONITOR_CLOSING;
    if (!resources->initialized) goto done;
    monitor_add(&resources->counters.callbacks, 1);
    if (!resources->accepting) { monitor_add(&resources->counters.dropped_closing, 1); goto done; }
    result = ESP32_MQUICKJS_WIFI_MONITOR_INVALID;
    if ((unsigned)filter_result >= ESP32_MQUICKJS_WIFI_MONITOR_FILTER_COUNTERS) {
        monitor_add(&resources->counters.invalid_callback_data, 1); goto done;
    }
    if (filter_result != ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT) {
        monitor_add(&resources->counters.filtered[filter_result], 1);
        result = ESP32_MQUICKJS_WIFI_MONITOR_FILTERED; goto done;
    }
    bool metadata_only = view != NULL && view->status == ESP32_MQUICKJS_WIFI_RX_SPAN_METADATA_ONLY;
    if (view == NULL || !view->metadata.available ||
        (metadata_only ? (view->bytes != NULL || view->readable_length != 0) :
         (view->status != ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET || view->bytes == NULL || view->readable_length == 0))) {
        monitor_add(&resources->counters.invalid_callback_data, 1); goto done;
    }
    monitor_add(&resources->counters.received_bytes, view->readable_length);
    if (resources->require_complete && (metadata_only || view->readable_length < view->metadata.driver_length ||
        view->metadata.driver_length > resources->snap_length)) {
        monitor_add(&resources->counters.dropped_required_complete, 1);
        result = ESP32_MQUICKJS_WIFI_MONITOR_REQUIRED_COMPLETE; goto done;
    }
    if (resources->next_identity == 0) {
        monitor_add(&resources->counters.dropped_identity_exhausted, 1);
        result = ESP32_MQUICKJS_WIFI_MONITOR_IDENTITY_EXHAUSTED; goto done;
    }
    uint16_t index;
    if (!esp32_mquickjs_native_pool_acquire(&resources->pool, &index)) {
        monitor_add(&resources->counters.dropped_pool_full, 1);
        result = ESP32_MQUICKJS_WIFI_MONITOR_POOL_FULL; goto done;
    }
    uint32_t identity = resources->next_identity;
    resources->next_identity = identity == UINT32_MAX ? 0 : identity + 1U;
    esp32_mquickjs_wifi_monitor_slot_t *slot = &resources->slots[index];
    if (!esp32_mquickjs_native_lease_init(&slot->lease, &resources->pool, index, identity)) {
        (void)esp32_mquickjs_native_pool_release(&resources->pool, index);
        monitor_add(&resources->counters.invalid_callback_data, 1); goto done;
    }
    slot->owner = MONITOR_WRITING;
    ++resources->counters.leased_frames;
    ++resources->counters.publishers;
    uint16_t length = view->readable_length < resources->snap_length ? view->readable_length : resources->snap_length;
    bool truncated = !metadata_only && length < view->metadata.driver_length;
    uint8_t *payload = resources->payload + (size_t)index * resources->snap_length;
    esp32_mquickjs_wifi_monitor_event_t event = { .generation = resources->generation, .identity = identity, .index = index };
    esp32_mquickjs_wifi_monitor_publish_fn publish = resources->publish;
    void *publish_opaque = resources->publish_opaque;
    portEXIT_CRITICAL(&resources->lock);

    /* Neither payload copies nor queue hooks execute inside a critical section.
     * Publisher + slot ownership pin all backing storage through this call. */
    if (length != 0) memcpy(payload, view->bytes, length);
    slot->info = (esp32_mquickjs_wifi_monitor_info_t){
        .driver = view->metadata, .header = view->header, .callback_time_us = callback_time_us,
        .readable_length = view->readable_length, .captured_length = length,
        .metadata_only = metadata_only, .truncated = truncated,
        .header_type_matches = view->header_type_matches,
    };
    portENTER_CRITICAL(&resources->lock);
    if (!resources->accepting) {
        (void)monitor_close_locked(resources, slot);
        --resources->counters.publishers;
        monitor_add(&resources->counters.dropped_closing, 1);
        result = ESP32_MQUICKJS_WIFI_MONITOR_CLOSING; goto done;
    }
    slot->owner = MONITOR_EVENT;
    portEXIT_CRITICAL(&resources->lock);
    bool published = publish(&event, publish_opaque);
    /* The queue may have already consumed/closed the event and the slot may
     * even be reused. Only the immutable token may be used after this hook. */
    if (!published) (void)esp32_mquickjs_wifi_monitor_discard_event(resources, &event);
    portENTER_CRITICAL(&resources->lock);
    --resources->counters.publishers;
    if (published) {
        monitor_add(&resources->counters.accepted, 1);
        monitor_add(&resources->counters.captured_bytes, length);
        if (truncated) monitor_add(&resources->counters.truncated_frames, 1);
        result = ESP32_MQUICKJS_WIFI_MONITOR_ACCEPTED;
    } else {
        monitor_add(&resources->counters.dropped_queue_full, 1);
        result = ESP32_MQUICKJS_WIFI_MONITOR_QUEUE_FULL;
    }
done:
    portEXIT_CRITICAL(&resources->lock);
    return result;
}

static bool monitor_change_owner(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event, uint8_t expected, bool take)
{
    if (resources == NULL || !resources->lock_initialized) return false;
    portENTER_CRITICAL(&resources->lock);
    esp32_mquickjs_wifi_monitor_slot_t *slot = monitor_slot_locked(resources, event);
    bool valid = slot != NULL && slot->owner == expected;
    if (valid) {
        if (take) slot->owner = MONITOR_PUBLIC;
        else valid = monitor_close_locked(resources, slot);
    }
    portEXIT_CRITICAL(&resources->lock);
    return valid;
}

bool esp32_mquickjs_wifi_monitor_take_event(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event)
{ return monitor_change_owner(resources, event, MONITOR_EVENT, true); }
bool esp32_mquickjs_wifi_monitor_discard_event(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event)
{ return monitor_change_owner(resources, event, MONITOR_EVENT, false); }
bool esp32_mquickjs_wifi_monitor_close_frame(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event)
{ return monitor_change_owner(resources, event, MONITOR_PUBLIC, false); }

bool esp32_mquickjs_wifi_monitor_frame_info(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event, esp32_mquickjs_wifi_monitor_info_t *output)
{
    if (output == NULL) return false;
    memset(output, 0, sizeof(*output));
    if (resources == NULL || !resources->lock_initialized) return false;
    portENTER_CRITICAL(&resources->lock);
    esp32_mquickjs_wifi_monitor_slot_t *slot = monitor_slot_locked(resources, event);
    bool valid = slot != NULL && slot->owner == MONITOR_PUBLIC;
    if (valid) *output = slot->info;
    portEXIT_CRITICAL(&resources->lock);
    return valid;
}

bool esp32_mquickjs_wifi_monitor_retain_frame(esp32_mquickjs_wifi_monitor_resources_t *resources,
    const esp32_mquickjs_wifi_monitor_event_t *event, esp32_mquickjs_wifi_monitor_ref_t *output)
{
    if (resources == NULL || !resources->lock_initialized || output == NULL || output->resources != NULL) return false;
    portENTER_CRITICAL(&resources->lock);
    esp32_mquickjs_wifi_monitor_slot_t *slot = monitor_slot_locked(resources, event);
    bool retained = slot != NULL && slot->owner == MONITOR_PUBLIC && esp32_mquickjs_native_lease_retain(&slot->lease, event->identity);
    if (retained) *output = (esp32_mquickjs_wifi_monitor_ref_t){ .resources = resources, .event = *event };
    portEXIT_CRITICAL(&resources->lock);
    return retained;
}

bool esp32_mquickjs_wifi_monitor_ref_data(const esp32_mquickjs_wifi_monitor_ref_t *ref,
    const uint8_t **bytes, size_t *length)
{
    if (bytes == NULL || length == NULL) return false;
    *bytes = NULL; *length = 0;
    if (ref == NULL || ref->resources == NULL) return false;
    esp32_mquickjs_wifi_monitor_resources_t *resources = ref->resources;
    portENTER_CRITICAL(&resources->lock);
    esp32_mquickjs_wifi_monitor_slot_t *slot = monitor_slot_locked(resources, &ref->event);
    bool valid = slot != NULL && (slot->owner == MONITOR_PUBLIC || slot->owner == MONITOR_CLOSED);
    if (valid) {
        *bytes = resources->payload + (size_t)ref->event.index * resources->snap_length;
        *length = slot->info.captured_length;
    }
    portEXIT_CRITICAL(&resources->lock);
    return valid;
}

bool esp32_mquickjs_wifi_monitor_release_ref(esp32_mquickjs_wifi_monitor_ref_t *ref)
{
    if (ref == NULL || ref->resources == NULL) return false;
    esp32_mquickjs_wifi_monitor_resources_t *resources = ref->resources;
    portENTER_CRITICAL(&resources->lock);
    esp32_mquickjs_wifi_monitor_slot_t *slot = monitor_slot_locked(resources, &ref->event);
    bool released = slot != NULL && esp32_mquickjs_native_lease_release(&slot->lease, ref->event.identity);
    if (released) monitor_account_return_locked(resources, slot);
    memset(ref, 0, sizeof(*ref));
    portEXIT_CRITICAL(&resources->lock);
    return released;
}
#endif
