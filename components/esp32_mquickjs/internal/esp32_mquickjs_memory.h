#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ESP32_MQUICKJS_MEMORY_DEFAULT,
    ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,
    ESP32_MQUICKJS_MEMORY_DMA_INTERNAL,
    ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL,
    ESP32_MQUICKJS_MEMORY_EXTERNAL,
    ESP32_MQUICKJS_MEMORY_HOT_MOVABLE,
    ESP32_MQUICKJS_MEMORY_COLD_MOVABLE,
    ESP32_MQUICKJS_MEMORY_CACHE_EVICTABLE,
} esp32_mquickjs_memory_class_t;

typedef enum {
    ESP32_MQUICKJS_MEMORY_PRESSURE_NORMAL,
    ESP32_MQUICKJS_MEMORY_PRESSURE_GUARDED,
    ESP32_MQUICKJS_MEMORY_PRESSURE_CRITICAL,
} esp32_mquickjs_memory_pressure_t;

typedef struct esp32_mquickjs_memory_block esp32_mquickjs_memory_block_t;

typedef void (*esp32_mquickjs_memory_relocated_fn)(void *opaque,
                                                   void *data,
                                                   size_t size);

typedef struct {
    esp32_mquickjs_memory_pressure_t pressure;
    size_t internal_reserve_bytes;
    size_t dma_largest_reserve_bytes;
    size_t managed_internal_bytes;
    size_t managed_psram_bytes;
    size_t pinned_bytes;
    size_t movable_idle_bytes;
    uint32_t migration_count;
    size_t migration_bytes;
    uint32_t eviction_count;
    uint32_t allocation_failures;
} esp32_mquickjs_memory_status_t;

/* Initialize the boot-scoped policy from the current capability heaps. */
void esp32_mquickjs_memory_init(void);

/*
 * Run one bounded pressure pass. Call only from a runtime safe point; payload
 * allocation helpers deliberately never invoke migration from driver tasks.
 */
void esp32_mquickjs_memory_maintain(void);

/* Release any generation-scoped stable blocks left after JS_FreeContext(). */
void esp32_mquickjs_memory_release_generation(void);

/*
 * Reserve-check an internal-only DMA allocation set before entering a driver.
 * `total_bytes` covers aggregate heap pressure while `largest_block_bytes`
 * models the largest individual allocation the driver is expected to make.
 */
bool esp32_mquickjs_memory_prepare_internal_dma(size_t total_bytes,
                                                size_t largest_block_bytes);

void esp32_mquickjs_memory_get_status(esp32_mquickjs_memory_status_t *out);
const char *esp32_mquickjs_memory_pressure_name(
    esp32_mquickjs_memory_pressure_t pressure);

/*
 * Allocate a transferable payload. The returned pointer is an ordinary
 * heap_caps pointer and remains compatible with heap_caps_free(). This is for
 * payloads whose ownership is handed to an existing framework object.
 */
void *esp32_mquickjs_memory_payload_alloc(
    size_t size,
    esp32_mquickjs_memory_class_t memory_class);
void *esp32_mquickjs_memory_payload_calloc(
    size_t count,
    size_t size,
    esp32_mquickjs_memory_class_t memory_class);
void *esp32_mquickjs_memory_payload_realloc(
    void *data,
    size_t size,
    esp32_mquickjs_memory_class_t memory_class);

/*
 * Movable blocks expose a stable handle. Raw data may only be retained while
 * a borrow is active; relocation callbacks update owner-side cached pointers.
 */
esp32_mquickjs_memory_block_t *esp32_mquickjs_memory_block_alloc(
    size_t size,
    esp32_mquickjs_memory_class_t memory_class,
    esp32_mquickjs_memory_relocated_fn relocated,
    void *opaque);
bool esp32_mquickjs_memory_block_resize(esp32_mquickjs_memory_block_t *block,
                                       size_t size);
void *esp32_mquickjs_memory_block_borrow(esp32_mquickjs_memory_block_t *block);
void esp32_mquickjs_memory_block_release(esp32_mquickjs_memory_block_t *block);
size_t esp32_mquickjs_memory_block_size(
    const esp32_mquickjs_memory_block_t *block);
bool esp32_mquickjs_memory_block_free(esp32_mquickjs_memory_block_t *block);
