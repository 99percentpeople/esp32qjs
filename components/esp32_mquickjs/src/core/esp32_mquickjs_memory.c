#include "esp32_mquickjs_memory.h"

#include <limits.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

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
    esp32_mquickjs_memory_class_t memory_class;
    esp32_mquickjs_memory_relocated_fn relocated;
    void *opaque;
    uint64_t last_used;
    uint16_t borrows;
    bool external;
    bool transitioning;
};

typedef struct {
    portMUX_TYPE lock;
    esp32_mquickjs_memory_block_t *blocks;
    size_t internal_reserve_bytes;
    size_t dma_largest_reserve_bytes;
    size_t managed_internal_bytes;
    size_t managed_psram_bytes;
    size_t pinned_bytes;
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
    return memory_has_psram() &&
           heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA |
                                    MALLOC_CAP_8BIT) > 0;
}

static bool memory_is_external(const void *data)
{
    return data != NULL && esp_ptr_external_ram(data);
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

    memory_heap_info(&internal, &dma);
    taskENTER_CRITICAL(&s_memory.lock);
    reserve = s_memory.internal_reserve_bytes;
    taskEXIT_CRITICAL(&s_memory.lock);
    return dma.largest_free_block >= request_bytes &&
           internal.total_free_bytes >= request_bytes &&
           internal.total_free_bytes - request_bytes >= reserve / 2U;
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
        data = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
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
        } else {
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

void *esp32_mquickjs_memory_payload_alloc(
    size_t size,
    esp32_mquickjs_memory_class_t memory_class)
{
    return memory_alloc_classified(size, memory_class, false);
}

void *esp32_mquickjs_memory_payload_calloc(
    size_t count,
    size_t size,
    esp32_mquickjs_memory_class_t memory_class)
{
    if (size != 0 && count > SIZE_MAX / size) {
        memory_note_failure();
        return NULL;
    }
    return memory_alloc_classified(count * size, memory_class, true);
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

void *esp32_mquickjs_memory_payload_realloc(
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

static bool memory_class_counts_as_pinned(
    esp32_mquickjs_memory_class_t memory_class)
{
    return !memory_class_is_movable(memory_class);
}

static void memory_add_block(esp32_mquickjs_memory_block_t *block)
{
    taskENTER_CRITICAL(&s_memory.lock);
    block->last_used = ++s_memory.use_sequence;
    block->next = s_memory.blocks;
    s_memory.blocks = block;
    if (block->external) {
        s_memory.managed_psram_bytes += block->size;
    } else {
        s_memory.managed_internal_bytes += block->size;
        if (memory_class_counts_as_pinned(block->memory_class)) {
            s_memory.pinned_bytes += block->size;
        }
    }
    taskEXIT_CRITICAL(&s_memory.lock);
}

esp32_mquickjs_memory_block_t *esp32_mquickjs_memory_block_alloc(
    size_t size,
    esp32_mquickjs_memory_class_t memory_class,
    esp32_mquickjs_memory_relocated_fn relocated,
    void *opaque)
{
    esp32_mquickjs_memory_block_t *block;
    void *data;

    if (!memory_class_is_movable(memory_class) &&
        !memory_class_is_explicitly_pinned(memory_class) &&
        memory_class != ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL &&
        memory_class != ESP32_MQUICKJS_MEMORY_EXTERNAL &&
        memory_class != ESP32_MQUICKJS_MEMORY_DEFAULT) {
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
    block->memory_class = memory_class;
    block->relocated = relocated;
    block->opaque = opaque;
    block->external = memory_is_external(data);
    memory_add_block(block);
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

    next = esp32_mquickjs_memory_payload_realloc(
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
    if (old_external) {
        s_memory.managed_psram_bytes -= old_size;
    } else {
        s_memory.managed_internal_bytes -= old_size;
        if (memory_class_counts_as_pinned(block->memory_class)) {
            s_memory.pinned_bytes -= old_size;
        }
    }
    block->data = next;
    block->size = size;
    block->external = memory_is_external(next);
    if (block->external) {
        s_memory.managed_psram_bytes += block->size;
    } else {
        s_memory.managed_internal_bytes += block->size;
        if (memory_class_counts_as_pinned(block->memory_class)) {
            s_memory.pinned_bytes += block->size;
        }
    }
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
    if (block->external) {
        s_memory.managed_psram_bytes -= block->size;
    } else {
        s_memory.managed_internal_bytes -= block->size;
        if (memory_class_counts_as_pinned(block->memory_class)) {
            s_memory.pinned_bytes -= block->size;
        }
    }
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
        s_memory.managed_internal_bytes -= size;
        s_memory.managed_psram_bytes += size;
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
    if (block->external) {
        s_memory.managed_psram_bytes -= size;
    } else {
        s_memory.managed_internal_bytes -= size;
    }
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

    taskENTER_CRITICAL(&s_memory.lock);
    blocks = s_memory.blocks;
    s_memory.blocks = NULL;
    s_memory.managed_internal_bytes = 0;
    s_memory.managed_psram_bytes = 0;
    s_memory.pinned_bytes = 0;
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

bool esp32_mquickjs_memory_prepare_internal_dma(size_t total_bytes,
                                                size_t largest_block_bytes)
{
    multi_heap_info_t internal;
    multi_heap_info_t dma;
    size_t reserve;

    esp32_mquickjs_memory_init();
    memory_heap_info(&internal, &dma);
    taskENTER_CRITICAL(&s_memory.lock);
    reserve = s_memory.internal_reserve_bytes;
    taskEXIT_CRITICAL(&s_memory.lock);
    if (dma.largest_free_block < largest_block_bytes ||
        internal.total_free_bytes < total_bytes ||
        internal.total_free_bytes - total_bytes < reserve / 2U) {
        memory_note_failure();
        return false;
    }
    return true;
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
    out->pinned_bytes = s_memory.pinned_bytes;
    out->migration_count = s_memory.migration_count;
    out->migration_bytes = s_memory.migration_bytes;
    out->eviction_count = s_memory.eviction_count;
    out->allocation_failures = s_memory.allocation_failures;
    for (block = s_memory.blocks; block != NULL; block = block->next) {
        if (!block->transitioning && block->borrows == 0 &&
            block->data != NULL && memory_class_is_movable(block->memory_class)) {
            out->movable_idle_bytes += block->size;
        }
    }
    taskEXIT_CRITICAL(&s_memory.lock);
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
