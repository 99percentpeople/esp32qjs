#pragma once

#include <stdbool.h>
#include <stddef.h>

#define ESP32_MQUICKJS_REAPER_CAPACITY 16U
#define ESP32_MQUICKJS_REAPER_RESERVED_CAPACITY 1U
#define ESP32_MQUICKJS_REAPER_GENERAL_CAPACITY \
    (ESP32_MQUICKJS_REAPER_CAPACITY - ESP32_MQUICKJS_REAPER_RESERVED_CAPACITY)
#define ESP32_MQUICKJS_REAPER_RESERVED_EVENT_QUEUE 0U
#define ESP32_MQUICKJS_REAPER_BATCH_LIMIT 4U
#define ESP32_MQUICKJS_REAPER_RETRY_MS 10U

typedef bool (*esp32_mquickjs_reaper_fn_t)(void *opaque);

typedef struct {
    esp32_mquickjs_reaper_fn_t reap;
    void *opaque;
    bool active;
} esp32_mquickjs_reaper_entry_t;

typedef struct {
    esp32_mquickjs_reaper_entry_t entries[ESP32_MQUICKJS_REAPER_CAPACITY];
    size_t cursor;
    size_t pending;
} esp32_mquickjs_reaper_registry_t;

void esp32_mquickjs_reaper_init(esp32_mquickjs_reaper_registry_t *registry);

bool esp32_mquickjs_reaper_register(esp32_mquickjs_reaper_registry_t *registry,
                                    esp32_mquickjs_reaper_fn_t reap,
                                    void *opaque);

bool esp32_mquickjs_reaper_register_reserved(
    esp32_mquickjs_reaper_registry_t *registry,
    size_t reserved_slot,
    esp32_mquickjs_reaper_fn_t reap,
    void *opaque);

bool esp32_mquickjs_reaper_unregister(esp32_mquickjs_reaper_registry_t *registry,
                                      esp32_mquickjs_reaper_fn_t reap,
                                      void *opaque);

size_t esp32_mquickjs_reaper_poll(esp32_mquickjs_reaper_registry_t *registry,
                                  size_t batch_limit,
                                  size_t *out_attempted);

size_t esp32_mquickjs_reaper_pending(
    const esp32_mquickjs_reaper_registry_t *registry);

size_t esp32_mquickjs_reaper_capacity(
    const esp32_mquickjs_reaper_registry_t *registry);
