#include "esp32_mquickjs_wifi_vendor_ie_watch.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_options.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define VENDOR_WATCH_MAX_SOURCES 4U
#define VENDOR_WATCH_MAX_HANDLES 8U
#define VENDOR_WATCH_MAX_CAPACITY 32U
#define VENDOR_WATCH_CAPACITY_BUDGET 64U
#define VENDOR_WATCH_MAX_OUIS 8U
#define VENDOR_WATCH_MAX_SEQUENCE UINT64_C(9007199254740991)

typedef struct {
    uint64_t sequence, timestamp_us;
    uint32_t generation;
    int32_t rssi;
    uint16_t length;
    uint8_t frame, address[6], bytes[ESP32_MQUICKJS_WIFI_VENDOR_IE_MAX_BYTES];
} vendor_watch_event_t;
typedef struct {
    esp32_mquickjs_event_queue_t *queue;
    uint8_t oui[VENDOR_WATCH_MAX_OUIS][3];
    uint8_t oui_count, capacity;
} vendor_watch_source_t;

/* Runtime task creates the boot-owned mutex. Callback only tries a published
 * handle. Close/context release may run on the reaper, so registry and budget
 * changes always hold the mutex. Never hold it across JS allocation or SDK. */
static StaticSemaphore_t s_vendor_watch_mutex_storage;
static _Atomic(SemaphoreHandle_t) s_vendor_watch_mutex;
static vendor_watch_source_t *s_vendor_sources[VENDOR_WATCH_MAX_SOURCES];
static uint32_t s_vendor_watch_handles, s_vendor_watch_capacity;
static uint64_t s_vendor_watch_sequence;
static _Atomic uint32_t s_vendor_watch_busy, s_vendor_watch_invalid;
static _Atomic uint32_t s_vendor_watch_filtered, s_vendor_watch_dropped;

static void vendor_watch_count(_Atomic uint32_t *counter)
{
    uint32_t old = atomic_load(counter);
    while (old != UINT32_MAX && !atomic_compare_exchange_weak(counter, &old, old + 1U)) {}
}

static bool vendor_watch_matches(const vendor_watch_source_t *source, const uint8_t oui[3])
{
    if (source->oui_count == 0U) return true;
    for (unsigned i = 0; i < source->oui_count; ++i)
        if (memcmp(source->oui[i], oui, 3) == 0) return true;
    return false;
}

void esp32_mquickjs_wifi_vendor_ie_watch_capture(uint32_t generation, wifi_vendor_ie_type_t frame,
    const uint8_t address[6], const vendor_ie_data_t *data, int rssi)
{
    SemaphoreHandle_t mutex = atomic_load(&s_vendor_watch_mutex);
    if (mutex == NULL) return;
    if (xSemaphoreTake(mutex, 0) != pdTRUE) { vendor_watch_count(&s_vendor_watch_busy); return; }
    bool any = false;
    for (unsigned i = 0; i < VENDOR_WATCH_MAX_SOURCES; ++i) any |= s_vendor_sources[i] != NULL;
    if (!any) goto done;
    /* SDK supplies one complete element with length bytes after the length
     * field. No trailing bytes, padding or borrowed pointer enter the queue. */
    if ((unsigned)frame > WIFI_VND_IE_TYPE_ASSOC_RESP || address == NULL || data == NULL ||
        data->element_id != WIFI_VENDOR_IE_ELEMENT_ID || data->length < 4U) {
        vendor_watch_count(&s_vendor_watch_invalid); goto done;
    }
    if (s_vendor_watch_sequence == VENDOR_WATCH_MAX_SEQUENCE) goto done;
    int64_t now = esp_timer_get_time();
    if (now < 0 || (uint64_t)now > VENDOR_WATCH_MAX_SEQUENCE) {
        vendor_watch_count(&s_vendor_watch_invalid); goto done;
    }
    vendor_watch_event_t event = {
        .sequence = ++s_vendor_watch_sequence, .timestamp_us = (uint64_t)now,
        .generation = generation, .rssi = rssi, .length = (uint16_t)data->length + 2U, .frame = (uint8_t)frame,
    };
    memcpy(event.address, address, sizeof(event.address));
    memcpy(event.bytes, data, event.length);
    for (unsigned i = 0; i < VENDOR_WATCH_MAX_SOURCES; ++i) {
        vendor_watch_source_t *source = s_vendor_sources[i];
        if (source == NULL) continue;
        if (!vendor_watch_matches(source, event.bytes + 2)) { vendor_watch_count(&s_vendor_watch_filtered); continue; }
        /* This path never waits for a receiver or acquires the queue send
         * mutex. Our source lock keeps close/reaper from destroying the queue. */
        if (!esp32_mquickjs_event_queue_try_send_from_callback(source->queue, &event))
            vendor_watch_count(&s_vendor_watch_dropped);
    }
done:
    xSemaphoreGive(mutex);
}

static void vendor_watch_closed(void *opaque)
{
    vendor_watch_source_t *source = opaque;
    xSemaphoreTake(s_vendor_watch_mutex, portMAX_DELAY);
    for (unsigned i = 0; i < VENDOR_WATCH_MAX_SOURCES; ++i)
        if (s_vendor_sources[i] == source) s_vendor_sources[i] = NULL;
    xSemaphoreGive(s_vendor_watch_mutex);
    /* Queue scratch/buffer and pending receives can outlive close. Their
     * capacity charge and this context survive until native destruction. */
}

static void vendor_watch_destroyed(void *opaque)
{
    vendor_watch_source_t *source = opaque;
    xSemaphoreTake(s_vendor_watch_mutex, portMAX_DELAY);
    --s_vendor_watch_handles;
    s_vendor_watch_capacity -= source->capacity;
    xSemaphoreGive(s_vendor_watch_mutex);
    esp32_mquickjs_memory_payload_free(source);
}

void esp32_mquickjs_deinit_wifi_vendor_ie_watch_runtime(void)
{
    if (s_vendor_watch_mutex == NULL) return;
    for (unsigned i = 0; i < VENDOR_WATCH_MAX_SOURCES; ++i) {
        esp32_mquickjs_event_queue_t *queue = NULL;
        xSemaphoreTake(s_vendor_watch_mutex, portMAX_DELAY);
        vendor_watch_source_t *source = s_vendor_sources[i];
        s_vendor_sources[i] = NULL;
        /* Detach even a disposed queue whose reaper already owns close. This
         * prevents both new capture and a retry loop waiting on that reaper.
         * Context and capacity remain charged until native destruction. */
        if (source != NULL && esp32_mquickjs_event_queue_retain(source->queue)) queue = source->queue;
        xSemaphoreGive(s_vendor_watch_mutex);
        if (queue != NULL) {
            (void)esp32_mquickjs_event_queue_close(queue);
            esp32_mquickjs_event_queue_release(queue);
        }
    }
}

static const char *vendor_watch_frame_name(unsigned frame)
{
    static const char *const names[] = {"beacon", "probe-request", "probe-response", "association-request", "association-response"};
    return frame < 5 ? names[frame] : "unknown";
}

static JSValue vendor_watch_to_js(JSContext *ctx, const void *input, void *opaque)
{
    (void)opaque;
    const vendor_watch_event_t *event = input;
    JSGCRef result_ref, data_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *data = JS_PushGCRef(ctx, &data_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    *data = JS_NewArray(ctx, 0);
    if (JS_IsException(*data)) goto fail;
    char address[18], oui[9];
    snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x", event->address[0], event->address[1],
        event->address[2], event->address[3], event->address[4], event->address[5]);
    snprintf(oui, sizeof(oui), "%02x:%02x:%02x", event->bytes[2], event->bytes[3], event->bytes[4]);
#define SET(obj, key, value) do { if (!esp32_mquickjs_set_property_ref(ctx, obj, key, value)) goto fail; } while (0)
    SET(result, "sequence", JS_NewFloat64(ctx, (double)event->sequence));
    SET(result, "timestampUs", JS_NewFloat64(ctx, (double)event->timestamp_us));
    SET(result, "radioGeneration", JS_NewUint32(ctx, event->generation));
    SET(result, "frame", JS_NewString(ctx, vendor_watch_frame_name(event->frame)));
    SET(result, "sourceMac", JS_NewString(ctx, address));
    SET(result, "rssi", JS_NewInt32(ctx, event->rssi));
    SET(result, "oui", JS_NewString(ctx, oui));
    SET(result, "vendorType", JS_NewInt32(ctx, event->bytes[5]));
    for (unsigned i = 0; i < event->length; ++i)
        if (JS_IsException(JS_SetPropertyUint32(ctx, *data, i, JS_NewInt32(ctx, event->bytes[i])))) goto fail;
    SET(result, "data", *data);
#undef SET
    JS_PopGCRef(ctx, &data_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &data_ref); JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

JSValue esp32_mquickjs_wifi_vendor_ie_watch_status(JSContext *ctx)
{
    esp32_mquickjs_wifi_vendor_ie_broker_status_t broker;
    esp32_mquickjs_wifi_vendor_ie_broker_status(&broker);
    uint32_t subscribers = 0, handles = 0, capacity = 0;
    uint64_t sequence = 0;
    if (s_vendor_watch_mutex != NULL) {
        xSemaphoreTake(s_vendor_watch_mutex, portMAX_DELAY);
        for (unsigned i = 0; i < VENDOR_WATCH_MAX_SOURCES; ++i) subscribers += s_vendor_sources[i] != NULL;
        handles = s_vendor_watch_handles; capacity = s_vendor_watch_capacity; sequence = s_vendor_watch_sequence;
        xSemaphoreGive(s_vendor_watch_mutex);
    }
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
#define SET(key, value) do { if (!esp32_mquickjs_set_property_ref(ctx, result, key, value)) goto fail; } while (0)
    SET("subscribers", JS_NewUint32(ctx, subscribers));
    SET("retainedHandles", JS_NewUint32(ctx, handles));
    SET("reservedCapacity", JS_NewUint32(ctx, capacity));
    SET("eventBytes", JS_NewUint32(ctx, sizeof(vendor_watch_event_t)));
    SET("capacityLimit", JS_NewUint32(ctx, VENDOR_WATCH_CAPACITY_BUDGET));
    SET("subscriberLimit", JS_NewUint32(ctx, VENDOR_WATCH_MAX_SOURCES));
    SET("retainedHandleLimit", JS_NewUint32(ctx, VENDOR_WATCH_MAX_HANDLES));
    SET("sequence", JS_NewFloat64(ctx, (double)sequence));
    SET("sequenceExhausted", JS_NewBool(sequence == VENDOR_WATCH_MAX_SEQUENCE));
    SET("droppedBusy", JS_NewUint32(ctx, atomic_load(&s_vendor_watch_busy)));
    SET("invalid", JS_NewUint32(ctx, atomic_load(&s_vendor_watch_invalid)));
    SET("filtered", JS_NewUint32(ctx, atomic_load(&s_vendor_watch_filtered)));
    SET("droppedQueue", JS_NewUint32(ctx, atomic_load(&s_vendor_watch_dropped)));
    SET("radioGeneration", JS_NewUint32(ctx, broker.generation));
    SET("registered", JS_NewBool(broker.registered));
    SET("registrationUncertain", JS_NewBool(broker.uncertain));
    SET("unregisterWritten", JS_NewBool(broker.unregister_written));
    SET("callbacksActive", JS_NewUint32(ctx, broker.callbacks_active));
    SET("registrationError", broker.error != ESP_OK ? JS_NewInt32(ctx, broker.error) : JS_NULL);
#undef SET
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &result_ref); return JS_EXCEPTION;
}

static int vendor_watch_hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool vendor_watch_oui(JSContext *ctx, JSValue value, uint8_t oui[3])
{
    if (!JS_IsString(ctx, value)) return false;
    JSCStringBuf buffer; size_t length;
    const char *text = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (text == NULL || length != 8 || text[2] != ':' || text[5] != ':') return false;
    for (unsigned i = 0; i < 3; ++i) {
        int high = vendor_watch_hex((unsigned char)text[i * 3]), low = vendor_watch_hex((unsigned char)text[i * 3 + 1]);
        if (high < 0 || low < 0) return false;
        oui[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

JSValue js_wifi_vendor_ie_watch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    static const char *const keys[] = {"oui", "capacity"};
    vendor_watch_source_t parsed = {.capacity = 8};
    JSGCRef options_ref, value_ref, item_ref, queue_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    if (argc > 1) goto invalid;
    *options = argc ? argv[0] : JS_UNDEFINED;
    if (!JS_IsUndefined(*options)) {
        if (!esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.vendorIe.watch", keys, 2)) goto fail;
        *value = JS_GetPropertyStr(ctx, *options, "capacity");
        if (JS_IsException(*value)) goto fail;
        uint32_t capacity;
        if (!JS_IsUndefined(*value)) {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, VENDOR_WATCH_MAX_CAPACITY, &capacity)) goto invalid;
            parsed.capacity = (uint8_t)capacity;
        }
        *value = JS_GetPropertyStr(ctx, *options, "oui");
        if (JS_IsException(*value)) goto fail;
        if (!JS_IsUndefined(*value)) {
            if (JS_IsString(ctx, *value)) {
                if (!vendor_watch_oui(ctx, *value, parsed.oui[0])) goto invalid;
                parsed.oui_count = 1;
            } else {
                if (!JS_IsArray(ctx, *value)) goto invalid;
                *item = JS_GetPropertyStr(ctx, *value, "length");
                uint32_t count;
                if (JS_IsException(*item)) goto fail;
                if (!esp32_mquickjs_value_to_bounded_u32(ctx, *item, 1, VENDOR_WATCH_MAX_OUIS, &count)) goto invalid;
                for (unsigned i = 0; i < count; ++i) {
                    *item = JS_GetPropertyUint32(ctx, *value, i);
                    if (JS_IsException(*item)) goto fail;
                    if (!vendor_watch_oui(ctx, *item, parsed.oui[i])) goto invalid;
                    for (unsigned j = 0; j < i; ++j) if (!memcmp(parsed.oui[i], parsed.oui[j], 3)) goto invalid;
                }
                parsed.oui_count = (uint8_t)count;
            }
        }
    }
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    if (runtime == NULL) { JS_ThrowInternalError(ctx, "wifi.vendorIe.watch requires an active runtime"); goto fail; }
    if (s_vendor_watch_mutex == NULL) s_vendor_watch_mutex = xSemaphoreCreateMutexStatic(&s_vendor_watch_mutex_storage);
    if (s_vendor_watch_mutex == NULL) { JS_ThrowOutOfMemory(ctx); goto fail; }
    vendor_watch_source_t *source = esp32_mquickjs_memory_wireless_alloc("wifi", sizeof(*source), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (source == NULL) { JS_ThrowOutOfMemory(ctx); goto fail; }
    *source = parsed;
    xSemaphoreTake(s_vendor_watch_mutex, portMAX_DELAY);
    unsigned slot;
    for (slot = 0; slot < VENDOR_WATCH_MAX_SOURCES; ++slot) if (s_vendor_sources[slot] == NULL) break;
    bool available = slot != VENDOR_WATCH_MAX_SOURCES && s_vendor_watch_handles < VENDOR_WATCH_MAX_HANDLES &&
        parsed.capacity <= VENDOR_WATCH_CAPACITY_BUDGET - s_vendor_watch_capacity &&
        s_vendor_watch_sequence != VENDOR_WATCH_MAX_SEQUENCE;
    if (available) { ++s_vendor_watch_handles; s_vendor_watch_capacity += parsed.capacity; }
    xSemaphoreGive(s_vendor_watch_mutex);
    if (!available) { esp32_mquickjs_memory_payload_free(source); JS_ThrowInternalError(ctx, "Vendor IE watch capacity or sequence exhausted"); goto fail; }
    *queue = esp32_mquickjs_event_queue_new_wireless("wifi", ctx, runtime, sizeof(vendor_watch_event_t), parsed.capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST, vendor_watch_to_js, NULL, vendor_watch_closed, source);
    if (JS_IsException(*queue)) { vendor_watch_destroyed(source); goto fail; }
    source->queue = esp32_mquickjs_event_queue_from_value(ctx, *queue);
    if (!esp32_mquickjs_event_queue_bind_context_release(source->queue, vendor_watch_destroyed)) {
        (void)esp32_mquickjs_event_queue_dispose(ctx, *queue); vendor_watch_destroyed(source);
        JS_ThrowInternalError(ctx, "Vendor IE queue lifetime binding failed"); goto fail;
    }
    xSemaphoreTake(s_vendor_watch_mutex, portMAX_DELAY);
    s_vendor_sources[slot] = source;
    xSemaphoreGive(s_vendor_watch_mutex);
    JSValue result = JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &value_ref); JS_PopGCRef(ctx, &options_ref);
    return result;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Invalid wifi.vendorIe.watch options; capacity 1..32, unique OUI strings xx:xx:xx");
fail:
    JS_PopGCRef(ctx, &queue_ref); JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &value_ref); JS_PopGCRef(ctx, &options_ref);
    return JS_EXCEPTION;
}
#endif
