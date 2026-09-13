
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static size_t live_size;
static uint8_t *live;
static void *psni_info;
static int unrelated_owner;
static int queries, scrubs, releases;
static void release_backing(void *p) { free(p); }
static size_t heap_caps_get_allocated_size(void *p) {
    assert(p && p==live);queries++;return live_size;
}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t length) {
    assert(p==live && length==live_size);scrubs++;
    volatile uint8_t *v=p;while(length--)*v++=0;
}
static void observed_free(void *p) {
    releases++;
    if(!p)return;
    assert(p==live);
    assert(psni_info!=p); /* Revocation precedes the allocator's actual free. */
    for(size_t i=0;i<live_size;i++)assert(live[i]==0);
    /* Adjacent storage/canaries are outside the allocator's reported span. */
    assert(live[-1]==0x71 && live[live_size]==0x72);
}
#define free observed_free
