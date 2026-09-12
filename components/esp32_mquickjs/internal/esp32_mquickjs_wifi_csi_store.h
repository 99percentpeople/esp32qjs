#ifndef ESP32_MQUICKJS_WIFI_CSI_STORE_H
#define ESP32_MQUICKJS_WIFI_CSI_STORE_H

#include "esp32_mquickjs_wifi_csi_resources.h"

/* A CSI sub-budget. The shared wireless/control/queue budget is separate. */
#define ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS 8U

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_STORE_FREE = 0,
    ESP32_MQUICKJS_WIFI_CSI_STORE_ALLOCATING,
    ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE,
    ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED,
    ESP32_MQUICKJS_WIFI_CSI_STORE_RETIRING,
} esp32_mquickjs_wifi_csi_store_state_t;

typedef struct {
    esp32_mquickjs_wifi_csi_resources_t *resources;
    uint32_t generation;
    uint32_t capacity;
    uint32_t pins;
    size_t bytes;
    esp32_mquickjs_wifi_csi_store_state_t state;
} esp32_mquickjs_wifi_csi_store_entry_t;

typedef struct {
    void (*lock)(void *opaque);
    void (*unlock)(void *opaque);
    void *opaque;
    uint32_t maximum_slots;
    uint32_t max_frame_bytes;
    /* Boot-scoped, initialized to one. Zero permanently means exhausted. */
    _Atomic uint32_t next_generation;
    uint32_t reserved_slots;
    esp32_mquickjs_wifi_csi_store_entry_t
        entries[ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS];
} esp32_mquickjs_wifi_csi_store_t;

typedef enum {
    ESP32_MQUICKJS_WIFI_CSI_STORE_OK = 0,
    ESP32_MQUICKJS_WIFI_CSI_STORE_INVALID,
    ESP32_MQUICKJS_WIFI_CSI_STORE_BUDGET,
    ESP32_MQUICKJS_WIFI_CSI_STORE_IDENTITY,
    ESP32_MQUICKJS_WIFI_CSI_STORE_MEMORY,
} esp32_mquickjs_wifi_csi_store_result_t;

typedef struct {
    uint32_t generation;
    uint32_t capacity;
    size_t bytes;
    esp32_mquickjs_wifi_csi_store_state_t state;
    bool identity_exhausted;
    uint32_t dropped_identity_exhausted;
    uint32_t free_slots;
    uint32_t leased_frames;
    uint32_t callbacks;
    uint32_t accepted;
    uint32_t dropped_pool_full;
    uint32_t dropped_queue_full;
    uint32_t dropped_closing;
} esp32_mquickjs_wifi_csi_store_snapshot_t;

/* Open reserves slots AND a registry entry before any allocation. It returns
 * one control pin, released only by retire after native source cleanup. */
esp32_mquickjs_wifi_csi_resources_t *esp32_mquickjs_wifi_csi_store_open(
    esp32_mquickjs_wifi_csi_store_t *store, uint32_t capacity, uint32_t max_packet_bytes,
    const esp32_mquickjs_wifi_csi_allocator_t *allocator,
    esp32_mquickjs_wifi_csi_store_result_t *result);
/* Temporary access pins protect lookup/mutation/snapshot against final free.
 * An existing slot owner additionally protects pointers used after release. */
esp32_mquickjs_wifi_csi_resources_t *esp32_mquickjs_wifi_csi_store_acquire(
    esp32_mquickjs_wifi_csi_store_t *store, uint32_t generation);
void esp32_mquickjs_wifi_csi_store_release(
    esp32_mquickjs_wifi_csi_store_t *store,
    esp32_mquickjs_wifi_csi_resources_t *resources);
bool esp32_mquickjs_wifi_csi_store_retire(
    esp32_mquickjs_wifi_csi_store_t *store,
    esp32_mquickjs_wifi_csi_resources_t *resources);
size_t esp32_mquickjs_wifi_csi_store_snapshot(
    esp32_mquickjs_wifi_csi_store_t *store,
    esp32_mquickjs_wifi_csi_store_snapshot_t
        output[ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS]);
/* Reset readable active/retained histories; count allocating/retiring entries.
 * Store lock prevents transition into final free while counters are accessed. */
uint32_t esp32_mquickjs_wifi_csi_store_reset_counters(esp32_mquickjs_wifi_csi_store_t *store);

#endif
