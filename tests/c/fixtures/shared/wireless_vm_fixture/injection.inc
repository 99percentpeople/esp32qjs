
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>
#include <stdatomic.h>
static JSContext *test_ctx;
static int calls,fail_at,moved_roots,root_count,native_live;
static bool collect,inject;
static JSGCRef *roots[128];
static void *allocations[512];
static bool step(JSContext *ctx) {
    if(!inject)return false;
    if(collect && ctx) {
        JSValue old[128];for(int i=0;i<root_count;i++)old[i]=roots[i]->val;
        JS_GC(ctx);
        for(int i=0;i<root_count;i++)if(old[i]!=roots[i]->val)moved_roots++;
        char hole[512];memset(hole,'x',sizeof(hole));
        assert(!JS_IsException(JS_NewStringLen(ctx,hole,sizeof(hole))));
    }
    return ++calls==fail_at;
}
static void *heap_caps_malloc(size_t n,int caps) {
    (void)caps;if(inject && ++calls==fail_at)return NULL;
    void *p=malloc(n ? n : 1);assert(p);
    for(int i=0;i<512;i++)if(!allocations[i]) { allocations[i]=p;native_live++;return p; }
    assert(0);return NULL;
}
static void *heap_caps_calloc(size_t n,size_t size,int caps) {
    assert(!n || size<=SIZE_MAX/n);void *p=heap_caps_malloc(n*size,caps);if(p)memset(p,0,n*size);return p;
}
static void heap_caps_free(void *p) {
    if(!p)return;
    for(int i=0;i<512;i++)if(allocations[i]==p) {allocations[i]=NULL;native_live--;free(p);return;}
    assert(!"double free or unknown native allocation");
}
static void esp32_mquickjs_memory_payload_free(void *p) { heap_caps_free(p); }
#include "esp32_mquickjs_memory_budget.h"
/* Allocation/GC boundary only. Aggregate quota and allocator-return ordering
 * are exercised by test_memory_wireless against the complete production manager. */
#define esp32_mquickjs_memory_wireless_alloc(owner,n,policy,role) heap_caps_malloc(n,MALLOC_CAP_8BIT)
#define esp32_mquickjs_memory_wireless_calloc(owner,n,size,policy,role) heap_caps_calloc(n,size,MALLOC_CAP_8BIT)
#define esp32_mquickjs_event_queue_new_wireless(owner,...) esp32_mquickjs_event_queue_new(__VA_ARGS__)
static JSValue new_class(JSContext *ctx,int cls) { if(step(ctx))return JS_ThrowOutOfMemory(ctx);return JS_NewObjectClassUser(ctx,cls); }
static JSValue new_object(JSContext *ctx) { if(step(ctx))return JS_ThrowOutOfMemory(ctx);return JS_NewObject(ctx); }
static JSValue new_array(JSContext *ctx,int n) { if(step(ctx))return JS_ThrowOutOfMemory(ctx);return JS_NewArray(ctx,n); }
static JSValue new_string(JSContext *ctx,const char *s) { if(step(ctx))return JS_ThrowOutOfMemory(ctx);return JS_NewString(ctx,s); }
static JSValue new_string_len(JSContext *ctx,const char *s,size_t n) { if(step(ctx))return JS_ThrowOutOfMemory(ctx);return JS_NewStringLen(ctx,s,n); }
/* Match the linked VM's numeric allocation boundary: 31-bit integers are
 * immediate, -0 is a preallocated context constant, and 64-bit Hosts also
 * encode short floats inline. Injecting GC for a byte constructor invents a
 * relocation before an enclosing setter has received its by-value owner. */
static bool number_allocates(double n) {
    if(n==0 || (n>=-1073741824.0 && n<=1073741823.0 && n==(double)(int32_t)n))return false;
#ifdef JS_USE_SHORT_FLOAT
    if(fabs(n)>=0x1p-127 && fabs(n)<=0x1p+128)return false;
#endif
    return true;
}
static JSValue new_uint32(JSContext *ctx,uint32_t n) { if(number_allocates(n) && step(ctx))return JS_ThrowOutOfMemory(ctx);return JS_NewUint32(ctx,n); }
static JSValue new_float64(JSContext *ctx,double n) { if(number_allocates(n) && step(ctx))return JS_ThrowOutOfMemory(ctx);return JS_NewFloat64(ctx,n); }
static JSValue new_int64(JSContext *ctx,int64_t n) { if(number_allocates(n) && step(ctx))return JS_ThrowOutOfMemory(ctx);return JS_NewInt64(ctx,n); }
/* VM setters root their by-value inputs internally. Inject allocation failure at
 * entry; do not collect before those API entry points root their arguments. */
static JSValue set_property(JSContext *ctx,JSValue obj,const char *key,JSValue value) {
    if(inject && ++calls==fail_at)return JS_ThrowOutOfMemory(ctx);return JS_SetPropertyStr(ctx,obj,key,value);
}
static JSValue set_index(JSContext *ctx,JSValue obj,uint32_t index,JSValue value) {
    if(inject && ++calls==fail_at)return JS_ThrowOutOfMemory(ctx);return JS_SetPropertyUint32(ctx,obj,index,value);
}
static JSValue *track_root(JSGCRef *r,JSValue *v) { assert(root_count<128);roots[root_count++]=r;return v; }
static JSValue *push_root(JSContext *ctx,JSGCRef *r) { return track_root(r,JS_PushGCRef(ctx,r)); }
static JSValue *add_root(JSContext *ctx,JSGCRef *r) { return track_root(r,JS_AddGCRef(ctx,r)); }
static void untrack(JSGCRef *r) {
    for(int i=0;i<root_count;i++)if(roots[i]==r) { memmove(roots+i,roots+i+1,(root_count-i-1)*sizeof(*roots));root_count--;return; }
    assert(0);
}
static JSValue pop_root(JSContext *ctx,JSGCRef *r) {untrack(r);return JS_PopGCRef(ctx,r);}
static void delete_root(JSContext *ctx,JSGCRef *r) {untrack(r);JS_DeleteGCRef(ctx,r);}
#define JS_NewObjectClassUser new_class
#define JS_NewObject new_object
#define JS_NewArray new_array
#define JS_NewString new_string
#define JS_NewStringLen new_string_len
#define JS_NewUint32 new_uint32
#define JS_NewFloat64 new_float64
#define JS_NewInt64 new_int64
#define JS_SetPropertyStr set_property
#define JS_SetPropertyUint32 set_index
#define JS_PushGCRef push_root
#define JS_PopGCRef pop_root
#define JS_AddGCRef add_root
#define JS_DeleteGCRef delete_root
