#pragma once

#include "esp32_mquickjs_wifi_csi_resources.h"

#include <stdlib.h>

typedef struct {
    unsigned calls;
    unsigned fail_at;
    unsigned allocations;
} wifi_csi_test_allocator_state_t;

static void *wifi_csi_test_calloc(size_t count, size_t size, void *opaque)
{
    wifi_csi_test_allocator_state_t *state = opaque;

    state->calls++;
    if (state->fail_at == state->calls) {
        return NULL;
    }
    void *value = calloc(count, size);
    if (value != NULL) {
        state->allocations++;
    }
    return value;
}

static void *wifi_csi_test_malloc(size_t size, void *opaque)
{
    wifi_csi_test_allocator_state_t *state = opaque;

    state->calls++;
    if (state->fail_at == state->calls) {
        return NULL;
    }
    void *value = malloc(size);
    if (value != NULL) {
        state->allocations++;
    }
    return value;
}

static void wifi_csi_test_free(void *ptr, void *opaque)
{
    wifi_csi_test_allocator_state_t *state = opaque;

    if (ptr != NULL) {
        state->allocations--;
        free(ptr);
    }
}

static esp32_mquickjs_wifi_csi_allocator_t wifi_csi_test_allocator(
    wifi_csi_test_allocator_state_t *state)
{
    esp32_mquickjs_wifi_csi_allocator_t allocator = {
        .calloc_fn = wifi_csi_test_calloc,
        .malloc_fn = wifi_csi_test_malloc,
        .free_fn = wifi_csi_test_free,
        .opaque = state,
    };

    return allocator;
}

static bool wifi_csi_test_publish_ok(
    const esp32_mquickjs_wifi_csi_event_t *event, void *opaque)
{
    esp32_mquickjs_wifi_csi_event_t *captured = opaque;

    *captured = *event;
    return true;
}
