
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
static char allocation;
static void *next_result;
static size_t seen_count,seen_size;
static int calls,failures;
static void *wifi_calloc(size_t count,size_t size) {
    calls++;seen_count=count;seen_size=size;return next_result;
}
static void esp32_mquickjs_wifi_smartconfig_allocation_failed(void) { failures++; }
