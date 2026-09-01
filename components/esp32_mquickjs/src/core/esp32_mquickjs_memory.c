#include "esp32_mquickjs_memory.h"

#include <limits.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "soc/soc_caps.h"

#define MEMORY_MIN_INTERNAL_RESERVE_BYTES (16U * 1024U)
#define MEMORY_MAX_INTERNAL_RESERVE_BYTES (64U * 1024U)
#define MEMORY_MIN_DMA_RESERVE_BYTES (8U * 1024U)
#define MEMORY_MAX_DMA_RESERVE_BYTES (32U * 1024U)
#define MEMORY_LARGE_DEFAULT_BYTES 4096U
#define MEMORY_MAX_ACTIONS_PER_PASS 4U

struct esp32_mquickjs_memory_block {
    struct esp32_mquickjs_memory_block *next;
    void *data;
    size_t size;
    const char *owner;
    esp32_mquickjs_memory_class_t memory_class;
    esp32_mquickjs_memory_relocated_fn relocated;
    void *opaque;
    uint64_t last_used;
    uint16_t borrows;
    bool external;
    bool transitioning;
};

typedef struct esp32_mquickjs_memory_payload {
    struct esp32_mquickjs_memory_payload *next;
    void *data;
    size_t size;
    const char *owner;
    esp32_mquickjs_memory_class_t memory_class;
    bool external;
    bool transitioning;
} esp32_mquickjs_memory_payload_t;

typedef struct {
    portMUX_TYPE lock;
    esp32_mquickjs_memory_block_t *blocks;
    esp32_mquickjs_memory_payload_t *payloads;
    size_t internal_reserve_bytes;
    size_t dma_largest_reserve_bytes;
    size_t managed_internal_bytes;
    size_t managed_psram_bytes;
    size_t managed_pinned_bytes;
    esp32_mquickjs_memory_owner_accounting_t owner_accounting;
    esp32_mquickjs_memory_dma_accounting_t dma_accounting;
    uint64_t use_sequence;
    uint32_t migration_count;
    size_t migration_bytes;
    uint32_t eviction_count;
    uint32_t allocation_failures;
    bool initialized;
    bool maintaining;
} esp32_mquickjs_memory_manager_t;

static esp32_mquickjs_memory_manager_t s_memory = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

static size_t memory_clamp(size_t value, size_t minimum, size_t maximum)
{
    if (value < minimum) {
        return minimum;
    }
    return value > maximum ? maximum : value;
}

static bool memory_has_psram(void)
{
#ifdef CONFIG_SPIRAM
    return esp_psram_is_initialized() &&
           heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
#else
    return false;
#endif
}

static bool memory_has_external_dma(void)
{
#if SOC_PSRAM_DMA_CAPABLE
    /*
     * External DMA is a SoC capability, not a heap capability intersection.
     * ESP-IDF intentionally registers PSRAM without MALLOC_CAP_DMA and adjusts
     * SPIRAM|DMA requests in the capability allocator.
     */
    return memory_has_psram();
#else
    return false;
#endif
}

static bool memory_is_external(const void *data)
{
    return data != NULL && esp_ptr_external_ram(data);
}

static uint8_t memory_region_for_external(bool external)
{
    return external ? ESP32_MQUICKJS_MEMORY_REGION_PSRAM
                    : ESP32_MQUICKJS_MEMORY_REGION_INTERNAL;
}

static esp32_mquickjs_memory_class_t memory_owner_class(
    esp32_mquickjs_memory_class_t memory_class,
    bool external)
{
    if (memory_class == ESP32_MQUICKJS_MEMORY_DEFAULT) {
        return external ? ESP32_MQUICKJS_MEMORY_EXTERNAL
                        : ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL;
    }
    if (memory_class == ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL && !external) {
        return ESP32_MQUICKJS_MEMORY_DMA_INTERNAL;
    }
    return memory_class;
}

static void memory_note_failure(void)
{
    taskENTER_CRITICAL(&s_memory.lock);
    if (s_memory.allocation_failures != UINT32_MAX) {
        s_memory.allocation_failures++;
    }
    taskEXIT_CRITICAL(&s_memory.lock);
}

static void memory_heap_info(multi_heap_info_t *internal,
                             multi_heap_info_t *dma)
{
    memset(internal, 0, sizeof(*internal));
    memset(dma, 0, sizeof(*dma));
    heap_caps_get_info(internal, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    heap_caps_get_info(
        dma, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
}

static esp32_mquickjs_memory_pressure_t memory_pressure_now(void)
{
    multi_heap_info_t internal;
    multi_heap_info_t dma;
    size_t internal_reserve;
    size_t dma_reserve;

    memory_heap_info(&internal, &dma);
    taskENTER_CRITICAL(&s_memory.lock);
    internal_reserve = s_memory.internal_reserve_bytes;
    dma_reserve = s_memory.dma_largest_reserve_bytes;
    taskEXIT_CRITICAL(&s_memory.lock);

    if (internal.total_free_bytes < internal_reserve / 2U ||
        dma.largest_free_block < dma_reserve / 2U) {
        return ESP32_MQUICKJS_MEMORY_PRESSURE_CRITICAL;
    }
    if (internal.total_free_bytes < internal_reserve ||
        dma.largest_free_block < dma_reserve) {
        return ESP32_MQUICKJS_MEMORY_PRESSURE_GUARDED;
    }
    return ESP32_MQUICKJS_MEMORY_PRESSURE_NORMAL;
}

static bool memory_internal_dma_can_fit(size_t request_bytes)
{
    multi_heap_info_t internal;
    multi_heap_info_t dma;
    size_t reserve;
    size_t pending_bytes;
    size_t pending_largest_bytes;
    size_t required_bytes;
    size_t required_largest;

    memory_heap_info(&internal, &dma);
    taskENTER_CRITICAL(&s_memory.lock);
    reserve = s_memory.internal_reserve_bytes;
    pending_bytes = s_memory.dma_accounting.pending_bytes;
    pending_largest_bytes = s_memory.dma_accounting.pending_largest_bytes;
    taskEXIT_CRITICAL(&s_memory.lock);
    if (request_bytes > SIZE_MAX - pending_bytes ||
        request_bytes > SIZE_MAX - pending_largest_bytes) {
        return false;
    }
    required_bytes = request_bytes + pending_bytes;
    required_largest = request_bytes + pending_largest_bytes;
    return dma.largest_free_block >= required_largest &&
           internal.total_free_bytes >= required_bytes &&
           internal.total_free_bytes - required_bytes >= reserve / 2U;
}

static bool memory_internal_can_fit(size_t request_bytes)
{
    multi_heap_info_t internal;
    multi_heap_info_t dma;
    size_t reserve;
    size_t pending_bytes;
    size_t required_bytes;

    memory_heap_info(&internal, &dma);
    taskENTER_CRITICAL(&s_memory.lock);
    reserve = s_memory.internal_reserve_bytes;
    pending_bytes = s_memory.dma_accounting.pending_bytes;
    taskEXIT_CRITICAL(&s_memory.lock);
    if (request_bytes > SIZE_MAX - pending_bytes) {
        return false;
    }
    required_bytes = request_bytes + pending_bytes;
    return internal.largest_free_block >= request_bytes &&
           internal.total_free_bytes >= required_bytes &&
           internal.total_free_bytes - required_bytes >= reserve / 2U;
}

void esp32_mquickjs_memory_init(void)
{
    multi_heap_info_t internal;
    multi_heap_info_t dma;

    memory_heap_info(&internal, &dma);
    taskENTER_CRITICAL(&s_memory.lock);
    if (s_memory.initialized) {
        taskEXIT_CRITICAL(&s_memory.lock);
        return;
    }
    s_memory.internal_reserve_bytes = memory_clamp(
        internal.total_free_bytes / 5U,
        MEMORY_MIN_INTERNAL_RESERVE_BYTES,
        MEMORY_MAX_INTERNAL_RESERVE_BYTES);
    s_memory.dma_largest_reserve_bytes = memory_clamp(
        dma.largest_free_block / 2U,
        MEMORY_MIN_DMA_RESERVE_BYTES,
        MEMORY_MAX_DMA_RESERVE_BYTES);
    s_memory.initialized = true;
    taskEXIT_CRITICAL(&s_memory.lock);
}

static void *memory_alloc_once(size_t size,
                               esp32_mquickjs_memory_class_t memory_class,
                               bool zero)
{
    bool has_psram = memory_has_psram();
    bool has_external_dma = memory_has_external_dma();
    bool guarded = memory_pressure_now() !=
                   ESP32_MQUICKJS_MEMORY_PRESSURE_NORMAL;
    void *data = NULL;

    if (size == 0) {
        size = 1;
    }
    switch (memory_class) {
    case ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL:
        if (memory_internal_can_fit(size)) {
            data = heap_caps_malloc(size,
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        break;
    case ESP32_MQUICKJS_MEMORY_DMA_INTERNAL:
        if (memory_internal_dma_can_fit(size)) {
            data = heap_caps_malloc(
                size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        }
        break;
    case ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL:
        if (has_external_dma) {
            data = heap_caps_malloc(
                size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
            if (data != NULL && !esp_ptr_dma_ext_capable(data)) {
                heap_caps_free(data);
                data = NULL;
            }
        }
        if (data == NULL && memory_internal_dma_can_fit(size)) {
            data = heap_caps_malloc(
                size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        }
        break;
    case ESP32_MQUICKJS_MEMORY_EXTERNAL:
    case ESP32_MQUICKJS_MEMORY_COLD_MOVABLE:
    case ESP32_MQUICKJS_MEMORY_CACHE_EVICTABLE:
        if (has_psram) {
            data = heap_caps_malloc(size,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        } else if (memory_internal_can_fit(size)) {
            data = heap_caps_malloc(size, MALLOC_CAP_8BIT);
        }
        break;
    case ESP32_MQUICKJS_MEMORY_HOT_MOVABLE:
        if (has_psram && guarded) {
            data = heap_caps_malloc(size,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (data == NULL) {
            data = heap_caps_malloc(size,
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (data == NULL && has_psram) {
            data = heap_caps_malloc(size,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        break;
    case ESP32_MQUICKJS_MEMORY_DEFAULT:
    default:
        if (has_psram && (guarded || size >= MEMORY_LARGE_DEFAULT_BYTES)) {
            data = heap_caps_malloc(size,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (data == NULL) {
            data = heap_caps_malloc(size, MALLOC_CAP_8BIT);
        }
        break;
    }
    if (data != NULL && zero) {
        memset(data, 0, size);
    }
    return data;
}

static void *memory_alloc_classified(size_t size,
                                     esp32_mquickjs_memory_class_t memory_class,
                                     bool zero)
{
    void *data;

    esp32_mquickjs_memory_init();
    data = memory_alloc_once(size, memory_class, zero);
    if (data == NULL) {
        memory_note_failure();
    }
    return data;
}

static uint32_t memory_realloc_caps(
    esp32_mquickjs_memory_class_t memory_class,
    size_t size)
{
    bool has_psram = memory_has_psram();

    switch (memory_class) {
    case ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL:
        return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    case ESP32_MQUICKJS_MEMORY_DMA_INTERNAL:
        return MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
    case ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL:
        return memory_has_external_dma()
                   ? MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT
                   : MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
    case ESP32_MQUICKJS_MEMORY_EXTERNAL:
    case ESP32_MQUICKJS_MEMORY_COLD_MOVABLE:
    case ESP32_MQUICKJS_MEMORY_CACHE_EVICTABLE:
        return has_psram ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                         : MALLOC_CAP_8BIT;
    case ESP32_MQUICKJS_MEMORY_HOT_MOVABLE:
        return has_psram && memory_pressure_now() !=
                                ESP32_MQUICKJS_MEMORY_PRESSURE_NORMAL
                   ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                   : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    case ESP32_MQUICKJS_MEMORY_DEFAULT:
    default:
        return has_psram && size >= MEMORY_LARGE_DEFAULT_BYTES
                   ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                   : MALLOC_CAP_8BIT;
    }
}

static void *memory_realloc_classified(
    void *data,
    size_t size,
    esp32_mquickjs_memory_class_t memory_class)
{
    void *next;
    uint32_t caps;
    bool external_dma_class;
    bool retry_internal_dma;

    esp32_mquickjs_memory_init();
    external_dma_class =
        memory_class == ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL;
    retry_internal_dma = external_dma_class && memory_has_external_dma();
    if (memory_class == ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL &&
        size != 0 && !memory_internal_can_fit(size)) {
        memory_note_failure();
        return NULL;
    }
    if (memory_class == ESP32_MQUICKJS_MEMORY_DMA_INTERNAL &&
        size != 0 && !memory_internal_dma_can_fit(size)) {
        memory_note_failure();
        return NULL;
    }
    if (external_dma_class && !retry_internal_dma && size != 0 &&
        !memory_internal_dma_can_fit(size)) {
        memory_note_failure();
        return NULL;
    }
    caps = memory_realloc_caps(memory_class, size);
    next = heap_caps_realloc(data, size, caps);
    if (next == NULL && size != 0 && retry_internal_dma &&
        memory_internal_dma_can_fit(size)) {
        next = heap_caps_realloc(
            data, size,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    }
    if (next == NULL && size != 0) {
        memory_note_failure();
    }
    return next;
}

static bool memory_account_add_locked(
    const char *owner,
    esp32_mquickjs_memory_class_t memory_class,
    bool external,
    size_t size)
{
    esp32_mquickjs_memory_class_t owner_class =
        memory_owner_class(memory_class, external);

    if (!esp32_mquickjs_memory_owner_add(
            &s_memory.owner_accounting, owner, (uint8_t)owner_class,
            memory_region_for_external(external), size, 1)) {
        return false;
    }
    if (external) {
        s_memory.managed_psram_bytes += size;
    } else {
        s_memory.managed_internal_bytes += size;
        if (owner_class != ESP32_MQUICKJS_MEMORY_HOT_MOVABLE &&
            owner_class != ESP32_MQUICKJS_MEMORY_COLD_MOVABLE &&
            owner_class != ESP32_MQUICKJS_MEMORY_CACHE_EVICTABLE) {
            s_memory.managed_pinned_bytes += size;
        }
    }
    return true;
}

static bool memory_account_remove_locked(
    const char *owner,
    esp32_mquickjs_memory_class_t memory_class,
    bool external,
    size_t size)
{
    esp32_mquickjs_memory_class_t owner_class =
        memory_owner_class(memory_class, external);

    if (!esp32_mquickjs_memory_owner_remove(
            &s_memory.owner_accounting, owner, (uint8_t)owner_class,
            memory_region_for_external(external), size, 1)) {
        return false;
    }
    if (external) {
        s_memory.managed_psram_bytes -= size;
    } else {
        s_memory.managed_internal_bytes -= size;
        if (owner_class != ESP32_MQUICKJS_MEMORY_HOT_MOVABLE &&
            owner_class != ESP32_MQUICKJS_MEMORY_COLD_MOVABLE &&
            owner_class != ESP32_MQUICKJS_MEMORY_CACHE_EVICTABLE) {
            s_memory.managed_pinned_bytes -= size;
        }
    }
    return true;
}

static esp32_mquickjs_memory_payload_t *memory_payload_metadata_alloc(void)
{
    esp32_mquickjs_memory_payload_t *payload = NULL;

    if (memory_has_psram()) {
        payload = heap_caps_calloc(
            1, sizeof(*payload), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (payload == NULL) {
        payload = heap_caps_calloc(
            1, sizeof(*payload), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return payload;
}

static void *memory_payload_alloc_tracked(
    const char *owner,
    size_t size,
    esp32_mquickjs_memory_class_t memory_class,
    bool zero)
{
    esp32_mquickjs_memory_payload_t *payload;
    void *data;
    size_t actual_size = size == 0 ? 1U : size;
    bool external;

    if (owner == NULL || owner[0] == '\0') {
        memory_note_failure();
        return NULL;
    }
    data = memory_alloc_classified(actual_size, memory_class, zero);
    if (data == NULL) {
        return NULL;
    }
    payload = memory_payload_metadata_alloc();
    if (payload == NULL) {
        heap_caps_free(data);
        memory_note_failure();
        return NULL;
    }
    external = memory_is_external(data);
    taskENTER_CRITICAL(&s_memory.lock);
    if (!memory_account_add_locked(owner, memory_class, external,
                                   actual_size)) {
        taskEXIT_CRITICAL(&s_memory.lock);
        heap_caps_free(payload);
        heap_caps_free(data);
        memory_note_failure();
        return NULL;
    }
    payload->next = s_memory.payloads;
    payload->data = data;
    payload->size = actual_size;
    payload->owner = owner;
    payload->memory_class = memory_class;
    payload->external = external;
    s_memory.payloads = payload;
    taskEXIT_CRITICAL(&s_memory.lock);
    return data;
}

void *esp32_mquickjs_memory_payload_alloc(
    const char *owner,
    size_t size,
    esp32_mquickjs_memory_class_t memory_class)
{
    return memory_payload_alloc_tracked(owner, size, memory_class, false);
}

void *esp32_mquickjs_memory_payload_calloc(
    const char *owner,
    size_t count,
    size_t size,
    esp32_mquickjs_memory_class_t memory_class)
{
    if (size != 0 && count > SIZE_MAX / size) {
        memory_note_failure();
        return NULL;
    }
    return memory_payload_alloc_tracked(
        owner, count * size, memory_class, true);
}

void *esp32_mquickjs_memory_payload_realloc(
    const char *owner,
    void *data,
    size_t size,
    esp32_mquickjs_memory_class_t memory_class)
{
    esp32_mquickjs_memory_payload_t *payload;
    void *next;
    size_t old_size;
    bool old_external;
    bool next_external;

    if (data == NULL) {
        return esp32_mquickjs_memory_payload_alloc(
            owner, size, memory_class);
    }
    if (size == 0) {
        esp32_mquickjs_memory_payload_free(data);
        return NULL;
    }
    taskENTER_CRITICAL(&s_memory.lock);
    for (payload = s_memory.payloads; payload != NULL;
         payload = payload->next) {
        if (payload->data == data) {
            break;
        }
    }
    if (payload == NULL || payload->transitioning || owner == NULL ||
        strcmp(payload->owner, owner) != 0) {
        taskEXIT_CRITICAL(&s_memory.lock);
        memory_note_failure();
        return NULL;
    }
    payload->transitioning = true;
    old_size = payload->size;
    old_external = payload->external;
    taskEXIT_CRITICAL(&s_memory.lock);

    next = memory_realloc_classified(data, size, memory_class);
    if (next == NULL) {
        taskENTER_CRITICAL(&s_memory.lock);
        payload->transitioning = false;
        taskEXIT_CRITICAL(&s_memory.lock);
        return NULL;
    }
    next_external = memory_is_external(next);
    taskENTER_CRITICAL(&s_memory.lock);
    (void)memory_account_remove_locked(
        payload->owner, payload->memory_class, old_external, old_size);
    (void)memory_account_add_locked(payload->owner, memory_class,
                                    next_external, size);
    payload->data = next;
    payload->size = size;
    payload->memory_class = memory_class;
    payload->external = next_external;
    payload->transitioning = false;
    taskEXIT_CRITICAL(&s_memory.lock);
    return next;
}

void esp32_mquickjs_memory_payload_free(void *data)
{
    esp32_mquickjs_memory_payload_t **cursor;
    esp32_mquickjs_memory_payload_t *payload;

    if (data == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_memory.lock);
    cursor = &s_memory.payloads;
    while (*cursor != NULL && (*cursor)->data != data) {
        cursor = &(*cursor)->next;
    }
    payload = *cursor;
    if (payload == NULL) {
        taskEXIT_CRITICAL(&s_memory.lock);
        heap_caps_free(data);
        return;
    }
    if (payload->transitioning) {
        taskEXIT_CRITICAL(&s_memory.lock);
        return;
    }
    *cursor = payload->next;
    (void)memory_account_remove_locked(
        payload->owner, payload->memory_class, payload->external,
        payload->size);
    taskEXIT_CRITICAL(&s_memory.lock);
    heap_caps_free(payload->data);
    heap_caps_free(payload);
}

static esp32_mquickjs_memory_block_t *memory_block_metadata_alloc(void)
{
    esp32_mquickjs_memory_block_t *block = NULL;

    if (memory_has_psram()) {
        block = heap_caps_calloc(
            1, sizeof(*block), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (block == NULL) {
        block = heap_caps_calloc(
            1, sizeof(*block), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return block;
}

static bool memory_class_is_explicitly_pinned(
    esp32_mquickjs_memory_class_t memory_class)
{
    return memory_class == ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL ||
           memory_class == ESP32_MQUICKJS_MEMORY_DMA_INTERNAL;
}

static bool memory_class_is_movable(esp32_mquickjs_memory_class_t memory_class)
{
    return memory_class == ESP32_MQUICKJS_MEMORY_HOT_MOVABLE ||
           memory_class == ESP32_MQUICKJS_MEMORY_COLD_MOVABLE ||
           memory_class == ESP32_MQUICKJS_MEMORY_CACHE_EVICTABLE;
}

static bool memory_add_block(esp32_mquickjs_memory_block_t *block)
{
    bool added;

    taskENTER_CRITICAL(&s_memory.lock);
    added = memory_account_add_locked(
        block->owner, block->memory_class, block->external, block->size);
    if (!added) {
        taskEXIT_CRITICAL(&s_memory.lock);
        return false;
    }
    block->last_used = ++s_memory.use_sequence;
    block->next = s_memory.blocks;
    s_memory.blocks = block;
    taskEXIT_CRITICAL(&s_memory.lock);
    return true;
}

esp32_mquickjs_memory_block_t *esp32_mquickjs_memory_block_alloc(
    const char *owner,
    size_t size,
    esp32_mquickjs_memory_class_t memory_class,
    esp32_mquickjs_memory_relocated_fn relocated,
    void *opaque)
{
    esp32_mquickjs_memory_block_t *block;
    void *data;

    if (owner == NULL || owner[0] == '\0' ||
        (!memory_class_is_movable(memory_class) &&
        !memory_class_is_explicitly_pinned(memory_class) &&
        memory_class != ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL &&
        memory_class != ESP32_MQUICKJS_MEMORY_EXTERNAL &&
        memory_class != ESP32_MQUICKJS_MEMORY_DEFAULT)) {
        return NULL;
    }
    data = memory_alloc_classified(size, memory_class, false);
    if (data == NULL) {
        return NULL;
    }
    block = memory_block_metadata_alloc();
    if (block == NULL) {
        heap_caps_free(data);
        memory_note_failure();
        return NULL;
    }
    block->data = data;
    block->size = size == 0 ? 1U : size;
    block->owner = owner;
    block->memory_class = memory_class;
    block->relocated = relocated;
    block->opaque = opaque;
    block->external = memory_is_external(data);
    if (!memory_add_block(block)) {
        heap_caps_free(data);
        heap_caps_free(block);
        memory_note_failure();
        return NULL;
    }
    if (relocated != NULL) {
        relocated(opaque, data, block->size);
    }
    return block;
}

bool esp32_mquickjs_memory_block_resize(esp32_mquickjs_memory_block_t *block,
                                       size_t size)
{
    void *next;
    size_t old_size;
    bool old_external;

    if (block == NULL) {
        return false;
    }
    taskENTER_CRITICAL(&s_memory.lock);
    if (block->borrows != 0 || block->transitioning) {
        taskEXIT_CRITICAL(&s_memory.lock);
        return false;
    }
    block->transitioning = true;
    old_size = block->size;
    old_external = block->external;
    taskEXIT_CRITICAL(&s_memory.lock);

    next = memory_realloc_classified(
        block->data,
        size,
        old_external && memory_class_is_movable(block->memory_class)
            ? ESP32_MQUICKJS_MEMORY_COLD_MOVABLE
            : block->memory_class);
    if (next == NULL && size != 0) {
        taskENTER_CRITICAL(&s_memory.lock);
        block->transitioning = false;
        taskEXIT_CRITICAL(&s_memory.lock);
        return false;
    }
    taskENTER_CRITICAL(&s_memory.lock);
    (void)memory_account_remove_locked(
        block->owner, block->memory_class, old_external, old_size);
    block->data = next;
    block->size = size;
    block->external = memory_is_external(next);
    (void)memory_account_add_locked(
        block->owner, block->memory_class, block->external, block->size);
    block->last_used = ++s_memory.use_sequence;
    taskEXIT_CRITICAL(&s_memory.lock);
    if (block->relocated != NULL) {
        block->relocated(block->opaque, next, size);
    }
    taskENTER_CRITICAL(&s_memory.lock);
    block->transitioning = false;
    taskEXIT_CRITICAL(&s_memory.lock);
    return true;
}

void *esp32_mquickjs_memory_block_borrow(esp32_mquickjs_memory_block_t *block)
{
    void *data = NULL;

    if (block == NULL) {
        return NULL;
    }
    taskENTER_CRITICAL(&s_memory.lock);
    if (!block->transitioning && block->data != NULL &&
        block->borrows != UINT16_MAX) {
        block->borrows++;
        block->last_used = ++s_memory.use_sequence;
        data = block->data;
    }
    taskEXIT_CRITICAL(&s_memory.lock);
    return data;
}

void esp32_mquickjs_memory_block_release(esp32_mquickjs_memory_block_t *block)
{
    if (block == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_memory.lock);
    if (block->borrows > 0) {
        block->borrows--;
        block->last_used = ++s_memory.use_sequence;
    }
    taskEXIT_CRITICAL(&s_memory.lock);
}

size_t esp32_mquickjs_memory_block_size(
    const esp32_mquickjs_memory_block_t *block)
{
    size_t size = 0;

    if (block == NULL) {
        return 0;
    }
    taskENTER_CRITICAL(&s_memory.lock);
    if (!block->transitioning) {
        size = block->size;
    }
    taskEXIT_CRITICAL(&s_memory.lock);
    return size;
}

bool esp32_mquickjs_memory_block_free(esp32_mquickjs_memory_block_t *block)
{
    esp32_mquickjs_memory_block_t **cursor;

    if (block == NULL) {
        return true;
    }
    taskENTER_CRITICAL(&s_memory.lock);
    if (block->borrows != 0 || block->transitioning) {
        taskEXIT_CRITICAL(&s_memory.lock);
        return false;
    }
    cursor = &s_memory.blocks;
    while (*cursor != NULL && *cursor != block) {
        cursor = &(*cursor)->next;
    }
    if (*cursor != block) {
        taskEXIT_CRITICAL(&s_memory.lock);
        return false;
    }
    *cursor = block->next;
    (void)memory_account_remove_locked(
        block->owner, block->memory_class, block->external, block->size);
    taskEXIT_CRITICAL(&s_memory.lock);
    heap_caps_free(block->data);
    heap_caps_free(block);
    return true;
}

static esp32_mquickjs_memory_block_t *memory_oldest_candidate(bool evict)
{
    esp32_mquickjs_memory_block_t *candidate = NULL;
    esp32_mquickjs_memory_block_t *block;

    taskENTER_CRITICAL(&s_memory.lock);
    for (block = s_memory.blocks; block != NULL; block = block->next) {
        bool eligible = !block->transitioning && block->borrows == 0 &&
                        block->data != NULL && !block->external;

        if (evict) {
            eligible = eligible &&
                       block->memory_class ==
                           ESP32_MQUICKJS_MEMORY_CACHE_EVICTABLE;
        } else {
            eligible = eligible &&
                       memory_class_is_movable(block->memory_class);
        }
        if (eligible &&
            (candidate == NULL || block->last_used < candidate->last_used)) {
            candidate = block;
        }
    }
    if (candidate != NULL) {
        candidate->transitioning = true;
    }
    taskEXIT_CRITICAL(&s_memory.lock);
    return candidate;
}

static bool memory_migrate_one(void)
{
    esp32_mquickjs_memory_block_t *block = memory_oldest_candidate(false);
    void *next;
    size_t size;

    if (block == NULL) {
        return false;
    }
    size = block->size;
    next = heap_caps_realloc(block->data, size,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    taskENTER_CRITICAL(&s_memory.lock);
    if (next != NULL) {
        (void)memory_account_remove_locked(
            block->owner, block->memory_class, false, size);
        (void)memory_account_add_locked(
            block->owner, block->memory_class, true, size);
        block->data = next;
        block->external = true;
        block->last_used = ++s_memory.use_sequence;
        if (s_memory.migration_count != UINT32_MAX) {
            s_memory.migration_count++;
        }
        s_memory.migration_bytes += size;
    }
    taskEXIT_CRITICAL(&s_memory.lock);
    if (next != NULL && block->relocated != NULL) {
        block->relocated(block->opaque, next, size);
    }
    taskENTER_CRITICAL(&s_memory.lock);
    block->transitioning = false;
    taskEXIT_CRITICAL(&s_memory.lock);
    return next != NULL;
}

static bool memory_evict_one(void)
{
    esp32_mquickjs_memory_block_t *block = memory_oldest_candidate(true);
    void *data;
    size_t size;

    if (block == NULL) {
        return false;
    }
    data = block->data;
    size = block->size;
    taskENTER_CRITICAL(&s_memory.lock);
    block->data = NULL;
    block->size = 0;
    (void)memory_account_remove_locked(
        block->owner, block->memory_class, block->external, size);
    block->external = false;
    block->last_used = ++s_memory.use_sequence;
    if (s_memory.eviction_count != UINT32_MAX) {
        s_memory.eviction_count++;
    }
    taskEXIT_CRITICAL(&s_memory.lock);
    heap_caps_free(data);
    if (block->relocated != NULL) {
        block->relocated(block->opaque, NULL, 0);
    }
    taskENTER_CRITICAL(&s_memory.lock);
    block->transitioning = false;
    taskEXIT_CRITICAL(&s_memory.lock);
    return true;
}

void esp32_mquickjs_memory_maintain(void)
{
    unsigned actions = 0;

    esp32_mquickjs_memory_init();
    taskENTER_CRITICAL(&s_memory.lock);
    if (s_memory.maintaining) {
        taskEXIT_CRITICAL(&s_memory.lock);
        return;
    }
    s_memory.maintaining = true;
    taskEXIT_CRITICAL(&s_memory.lock);

    while (memory_pressure_now() != ESP32_MQUICKJS_MEMORY_PRESSURE_NORMAL &&
           actions < MEMORY_MAX_ACTIONS_PER_PASS) {
        bool changed = false;

        if (memory_has_psram()) {
            changed = memory_migrate_one();
        }
        if (!changed &&
            memory_pressure_now() ==
                ESP32_MQUICKJS_MEMORY_PRESSURE_CRITICAL) {
            changed = memory_evict_one();
        }
        if (!changed) {
            break;
        }
        actions++;
    }
    taskENTER_CRITICAL(&s_memory.lock);
    s_memory.maintaining = false;
    taskEXIT_CRITICAL(&s_memory.lock);
}

void esp32_mquickjs_memory_release_generation(void)
{
    esp32_mquickjs_memory_block_t *blocks;
    esp32_mquickjs_memory_block_t *block;

    taskENTER_CRITICAL(&s_memory.lock);
    blocks = s_memory.blocks;
    s_memory.blocks = NULL;
    for (block = blocks; block != NULL; block = block->next) {
        if (block->data != NULL) {
            (void)memory_account_remove_locked(
                block->owner, block->memory_class, block->external,
                block->size);
        }
    }
    taskEXIT_CRITICAL(&s_memory.lock);

    /*
     * JavaScript finalizers normally unregister their blocks first. MQuickJS
     * may still leave unreachable user objects until context teardown, so the
     * manager owns this final backstop after JS_FreeContext has invalidated
     * every owner and relocation callback.
     */
    while (blocks != NULL) {
        esp32_mquickjs_memory_block_t *next = blocks->next;

        heap_caps_free(blocks->data);
        heap_caps_free(blocks);
        blocks = next;
    }
}

bool esp32_mquickjs_memory_reserve_internal_dma(
    esp32_mquickjs_memory_dma_reservation_t *reservation,
    const char *owner,
    size_t total_bytes,
    size_t largest_block_bytes)
{
    multi_heap_info_t internal;
    multi_heap_info_t dma;
    size_t reserve;
    size_t required_bytes;
    size_t required_largest;
    bool accepted = false;

    esp32_mquickjs_memory_init();
    if (reservation == NULL || owner == NULL || owner[0] == '\0' ||
        total_bytes == 0 || largest_block_bytes == 0 ||
        largest_block_bytes > total_bytes) {
        return false;
    }
    memory_heap_info(&internal, &dma);
    taskENTER_CRITICAL(&s_memory.lock);
    reserve = s_memory.internal_reserve_bytes;
    if (total_bytes <= SIZE_MAX - s_memory.dma_accounting.pending_bytes &&
        largest_block_bytes <=
            SIZE_MAX - s_memory.dma_accounting.pending_largest_bytes) {
        required_bytes = total_bytes + s_memory.dma_accounting.pending_bytes;
        required_largest = largest_block_bytes +
                           s_memory.dma_accounting.pending_largest_bytes;
        if (dma.largest_free_block >= required_largest &&
            internal.total_free_bytes >= required_bytes &&
            internal.total_free_bytes - required_bytes >= reserve / 2U) {
            accepted = esp32_mquickjs_memory_dma_accounting_reserve(
                &s_memory.dma_accounting, reservation, total_bytes,
                largest_block_bytes);
            if (accepted) {
                reservation->owner = owner;
            }
        }
    }
    taskEXIT_CRITICAL(&s_memory.lock);
    if (!accepted) {
        memory_note_failure();
        return false;
    }
    return true;
}

bool esp32_mquickjs_memory_commit_driver_pinned(
    esp32_mquickjs_memory_dma_reservation_t *reservation,
    size_t driver_pinned_bytes)
{
    bool committed;

    taskENTER_CRITICAL(&s_memory.lock);
    committed = reservation != NULL && reservation->owner != NULL &&
        esp32_mquickjs_memory_owner_add(
            &s_memory.owner_accounting, reservation->owner,
            ESP32_MQUICKJS_MEMORY_DMA_INTERNAL,
            ESP32_MQUICKJS_MEMORY_REGION_INTERNAL,
            driver_pinned_bytes, 1);
    if (committed) {
        committed = esp32_mquickjs_memory_dma_accounting_commit(
            &s_memory.dma_accounting, reservation, driver_pinned_bytes);
        if (!committed) {
            (void)esp32_mquickjs_memory_owner_remove(
                &s_memory.owner_accounting, reservation->owner,
                ESP32_MQUICKJS_MEMORY_DMA_INTERNAL,
                ESP32_MQUICKJS_MEMORY_REGION_INTERNAL,
                driver_pinned_bytes, 1);
        }
    }
    taskEXIT_CRITICAL(&s_memory.lock);
    return committed;
}

bool esp32_mquickjs_memory_commit_staging_pinned(
    esp32_mquickjs_memory_dma_reservation_t *reservation,
    size_t staging_pinned_bytes,
    uint32_t dma_staging_pools)
{
    bool committed;

    taskENTER_CRITICAL(&s_memory.lock);
    committed = reservation != NULL && reservation->owner != NULL &&
        esp32_mquickjs_memory_owner_add(
            &s_memory.owner_accounting, reservation->owner,
            ESP32_MQUICKJS_MEMORY_DMA_INTERNAL,
            ESP32_MQUICKJS_MEMORY_REGION_INTERNAL,
            staging_pinned_bytes, dma_staging_pools);
    if (committed) {
        committed = esp32_mquickjs_memory_dma_accounting_commit_staging(
            &s_memory.dma_accounting, reservation, staging_pinned_bytes,
            dma_staging_pools);
        if (!committed) {
            (void)esp32_mquickjs_memory_owner_remove(
                &s_memory.owner_accounting, reservation->owner,
                ESP32_MQUICKJS_MEMORY_DMA_INTERNAL,
                ESP32_MQUICKJS_MEMORY_REGION_INTERNAL,
                staging_pinned_bytes, dma_staging_pools);
        }
    }
    taskEXIT_CRITICAL(&s_memory.lock);
    return committed;
}

bool esp32_mquickjs_memory_release_driver_pinned(
    esp32_mquickjs_memory_dma_reservation_t *reservation)
{
    bool released;
    const char *owner;
    size_t bytes;
    uint32_t blocks;

    taskENTER_CRITICAL(&s_memory.lock);
    owner = reservation == NULL ? NULL : reservation->owner;
    bytes = reservation == NULL ? 0 :
        reservation->driver_pinned_bytes +
        reservation->staging_pinned_bytes;
    blocks = reservation == NULL ? 0 :
        (reservation->driver_pinned_bytes != 0 ? 1U : 0U) +
        reservation->dma_staging_pools;
    released = esp32_mquickjs_memory_dma_accounting_release(
        &s_memory.dma_accounting, reservation);
    if (released && bytes != 0) {
        released = esp32_mquickjs_memory_owner_remove(
            &s_memory.owner_accounting, owner,
            ESP32_MQUICKJS_MEMORY_DMA_INTERNAL,
            ESP32_MQUICKJS_MEMORY_REGION_INTERNAL, bytes, blocks);
    }
    taskEXIT_CRITICAL(&s_memory.lock);
    return released;
}

void esp32_mquickjs_memory_get_status(esp32_mquickjs_memory_status_t *out)
{
    esp32_mquickjs_memory_block_t *block;

    if (out == NULL) {
        return;
    }
    esp32_mquickjs_memory_init();
    memset(out, 0, sizeof(*out));
    out->pressure = memory_pressure_now();
    taskENTER_CRITICAL(&s_memory.lock);
    out->internal_reserve_bytes = s_memory.internal_reserve_bytes;
    out->dma_largest_reserve_bytes = s_memory.dma_largest_reserve_bytes;
    out->managed_internal_bytes = s_memory.managed_internal_bytes;
    out->managed_psram_bytes = s_memory.managed_psram_bytes;
    out->pinned_bytes = s_memory.managed_pinned_bytes +
                        s_memory.dma_accounting.driver_pinned_bytes +
                        s_memory.dma_accounting.staging_pinned_bytes;
    out->driver_pinned_bytes = s_memory.dma_accounting.driver_pinned_bytes;
    out->staging_pinned_bytes = s_memory.dma_accounting.staging_pinned_bytes;
    out->dma_staging_pools = s_memory.dma_accounting.dma_staging_pools;
    out->pending_dma_reservation_bytes =
        s_memory.dma_accounting.pending_bytes;
    out->migration_count = s_memory.migration_count;
    out->migration_bytes = s_memory.migration_bytes;
    out->eviction_count = s_memory.eviction_count;
    out->allocation_failures = s_memory.allocation_failures;
    out->allocation_count = esp32_mquickjs_memory_owner_snapshot(
        &s_memory.owner_accounting, out->allocations,
        ESP32_MQUICKJS_MEMORY_MAX_OWNER_ENTRIES);
    for (block = s_memory.blocks; block != NULL; block = block->next) {
        if (!block->transitioning && block->borrows == 0 &&
            block->data != NULL && memory_class_is_movable(block->memory_class)) {
            out->movable_idle_bytes += block->size;
        }
    }
    taskEXIT_CRITICAL(&s_memory.lock);
}

const char *esp32_mquickjs_memory_class_name(
    esp32_mquickjs_memory_class_t memory_class)
{
    switch (memory_class) {
    case ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL:
        return "pinned-internal";
    case ESP32_MQUICKJS_MEMORY_DMA_INTERNAL:
        return "dma-internal";
    case ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL:
        return "dma-external";
    case ESP32_MQUICKJS_MEMORY_EXTERNAL:
        return "external";
    case ESP32_MQUICKJS_MEMORY_HOT_MOVABLE:
        return "hot-movable";
    case ESP32_MQUICKJS_MEMORY_COLD_MOVABLE:
        return "cold-movable";
    case ESP32_MQUICKJS_MEMORY_CACHE_EVICTABLE:
        return "cache-evictable";
    case ESP32_MQUICKJS_MEMORY_DEFAULT:
    default:
        return "pinned-internal";
    }
}

const char *esp32_mquickjs_memory_region_name(uint8_t region)
{
    return region == ESP32_MQUICKJS_MEMORY_REGION_PSRAM
               ? "psram"
               : "internal";
}

const char *esp32_mquickjs_memory_pressure_name(
    esp32_mquickjs_memory_pressure_t pressure)
{
    switch (pressure) {
    case ESP32_MQUICKJS_MEMORY_PRESSURE_NORMAL:
        return "normal";
    case ESP32_MQUICKJS_MEMORY_PRESSURE_GUARDED:
        return "guarded";
    case ESP32_MQUICKJS_MEMORY_PRESSURE_CRITICAL:
        return "critical";
    default:
        return "critical";
    }
}
