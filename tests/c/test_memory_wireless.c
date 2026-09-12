/* Production manager and ledger, with only heap/FreeRTOS boundaries replaced.
 * Execution is part of the concentrated Wi-Fi phase, not an implementation check. */
#include "esp32_mquickjs_memory.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Thread_local unsigned test_memory_lock_depth;
static bool psram;
static unsigned allocation_calls, fail_at, live;
static struct { void *pointer; size_t size; uint32_t caps; } allocations[256];
static pthread_mutex_t heap_lock = PTHREAD_MUTEX_INITIALIZER;
static void (*after_free)(void);

void heap_caps_get_info(multi_heap_info_t *info, uint32_t caps)
{
    (void)caps;
    assert(!test_memory_lock_depth);
    *info = (multi_heap_info_t){.total_free_bytes = 1024 * 1024, .largest_free_block = 1024 * 1024};
}
size_t heap_caps_get_total_size(uint32_t caps)
{ (void)caps; assert(!test_memory_lock_depth); return psram ? 1024 * 1024 : 0; }
bool esp_psram_is_initialized(void) { return psram; }
bool esp_ptr_external_ram(const void *data)
{
    bool external = false;
    pthread_mutex_lock(&heap_lock);
    for (size_t i = 0; i < 256; ++i)
        if (allocations[i].pointer == data && data != NULL)
            external = (allocations[i].caps & MALLOC_CAP_SPIRAM) != 0;
    pthread_mutex_unlock(&heap_lock);
    return external;
}
bool esp_ptr_dma_ext_capable(const void *data) { return esp_ptr_external_ram(data); }
void *heap_caps_malloc(size_t size, uint32_t caps)
{
    assert(!test_memory_lock_depth);
    pthread_mutex_lock(&heap_lock);
    ++allocation_calls;
    if ((fail_at && allocation_calls == fail_at) || (!psram && (caps & MALLOC_CAP_SPIRAM))) {
        pthread_mutex_unlock(&heap_lock);
        return NULL;
    }
    for (size_t i = 0; i < 256; ++i) {
        if (allocations[i].pointer != NULL) continue;
        void *p = malloc(size);
        assert(p != NULL);
        allocations[i].pointer = p;
        allocations[i].size = size;
        allocations[i].caps = caps;
        ++live;
        pthread_mutex_unlock(&heap_lock);
        return p;
    }
    abort();
}
void *heap_caps_calloc(size_t count, size_t size, uint32_t caps)
{
    assert(!size || count <= SIZE_MAX / size);
    void *p = heap_caps_malloc(count * size, caps);
    if (p != NULL) memset(p, 0, count * size);
    return p;
}
void heap_caps_free(void *data)
{
    assert(!test_memory_lock_depth);
    if (data == NULL) return;
    pthread_mutex_lock(&heap_lock);
    for (size_t i = 0; i < 256; ++i) {
        if (allocations[i].pointer != data) continue;
        allocations[i].pointer = NULL;
        --live;
        free(data);
        pthread_mutex_unlock(&heap_lock);
        if (after_free != NULL) after_free();
        return;
    }
    abort();
}
void *heap_caps_realloc(void *data, size_t size, uint32_t caps)
{
    /* Budgeted fixed storage must never reach the realloc driver boundary. */
    (void)data; (void)size; (void)caps;
    abort();
}
static esp32_mquickjs_memory_status_t status(void)
{
    esp32_mquickjs_memory_status_t s;
    esp32_mquickjs_memory_get_status(&s);
    return s;
}
static void empty(void)
{
    esp32_mquickjs_memory_status_t s = status();
    assert(live == 0 && s.allocation_count == 0);
    assert(s.wireless.regions[0].reserved == 0 && s.wireless.regions[1].reserved == 0);
}
static size_t held;
static unsigned free_observations;
static void observe_free(void)
{
    assert(status().wireless.regions[0].reserved == held);
    assert(esp32_mquickjs_memory_wireless_alloc("blocked", 1,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_COPY) == NULL);
    ++free_observations;
}

static pthread_mutex_t race_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t race_condition = PTHREAD_COND_INITIALIZER;
static unsigned attempted, accepted;
static void *race_allocate(void *unused)
{
    (void)unused;
    void *p = esp32_mquickjs_memory_wireless_alloc("race", 2048,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_TX);
    pthread_mutex_lock(&race_lock);
    if (p != NULL) ++accepted;
    ++attempted;
    pthread_cond_broadcast(&race_condition);
    while (attempted != 2) pthread_cond_wait(&race_condition, &race_lock);
    pthread_mutex_unlock(&race_lock);
    esp32_mquickjs_memory_payload_free(p);
    return NULL;
}

int main(void)
{
    esp32_mquickjs_memory_init();
    for (unsigned nth = 1; nth <= 2; ++nth) {
        allocation_calls = 0; fail_at = nth;
        assert(esp32_mquickjs_memory_wireless_calloc("failure", 4, 16,
            ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_POOL) == NULL);
        empty();
    }
    fail_at = 0;
    unsigned before = allocation_calls;
    assert(esp32_mquickjs_memory_wireless_calloc("overflow", SIZE_MAX, 2,
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY) == NULL);
    assert(esp32_mquickjs_memory_wireless_alloc("overflow", SIZE_MAX,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_COPY) == NULL);
    assert(allocation_calls == before);
    void *p = esp32_mquickjs_memory_wireless_alloc("wifi.csi", 32,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
    assert(p != NULL);
    size_t overhead = status().wireless.regions[0].reserved - 32;
    esp32_mquickjs_memory_payload_free(p);
    p = esp32_mquickjs_memory_wireless_alloc("wifi.csi", 3584 - overhead,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
    assert(p != NULL);
    assert(esp32_mquickjs_memory_wireless_alloc("ble.scan", 1,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_POOL) == NULL);
    void *control = esp32_mquickjs_memory_wireless_alloc("control", 512 - overhead,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    assert(control != NULL);
    assert(esp32_mquickjs_memory_wireless_retire(p));
    assert(esp32_mquickjs_memory_wireless_retire(p)); /* idempotent on the owned pointer */
    esp32_mquickjs_memory_release_generation();
    assert(status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_RETIRED_POOL] == 3584);
    assert(esp32_mquickjs_memory_payload_realloc("wifi.csi", p, 16,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL) == NULL);
    held = 4096; after_free = observe_free;
    esp32_mquickjs_memory_payload_free(p);
    after_free = NULL;
    assert(free_observations == 2 && status().wireless.regions[0].reserved == 512);
    esp32_mquickjs_memory_payload_free(control);
    empty();
    psram = true;
    p = esp32_mquickjs_memory_wireless_alloc("internal", 64,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
    assert(p != NULL && !esp_ptr_external_ram(p));
    assert(status().wireless.regions[0].reserved == 64);
    assert(status().wireless.regions[1].reserved == overhead);
    assert(esp32_mquickjs_memory_wireless_retire(p));
    assert(status().wireless.regions[1].roles[ESP32_MQUICKJS_MEMORY_BUDGET_RETIRED_POOL] == overhead);
    esp32_mquickjs_memory_payload_free(p);
    p = esp32_mquickjs_memory_wireless_alloc("external", 64,
        ESP32_MQUICKJS_MEMORY_EXTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_TX);
    assert(p != NULL && esp_ptr_external_ram(p));
    assert(status().wireless.regions[1].reserved == 64 + overhead);
    esp32_mquickjs_memory_payload_free(p);
    empty();
    psram = false;
    char names[ESP32_MQUICKJS_MEMORY_MAX_OWNER_ENTRIES + 1][16];
    void *owners[ESP32_MQUICKJS_MEMORY_MAX_OWNER_ENTRIES];
    for (size_t i = 0; i <= ESP32_MQUICKJS_MEMORY_MAX_OWNER_ENTRIES; ++i) {
        snprintf(names[i], sizeof(names[i]), "owner-%u", (unsigned)i);
        p = esp32_mquickjs_memory_wireless_alloc(names[i], 1,
            ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
        if (i < ESP32_MQUICKJS_MEMORY_MAX_OWNER_ENTRIES) { assert(p != NULL); owners[i] = p; }
        else assert(p == NULL);
    }
    assert(status().wireless.regions[0].reserved == (overhead + 1) * ESP32_MQUICKJS_MEMORY_MAX_OWNER_ENTRIES);
    for (size_t i = 0; i < ESP32_MQUICKJS_MEMORY_MAX_OWNER_ENTRIES; ++i)
        esp32_mquickjs_memory_payload_free(owners[i]);
    empty();
    pthread_t a, b;
    assert(!pthread_create(&a, NULL, race_allocate, NULL));
    assert(!pthread_create(&b, NULL, race_allocate, NULL));
    assert(!pthread_join(a, NULL) && !pthread_join(b, NULL));
    assert(accepted == 1);
    empty();
    return 0;
}
