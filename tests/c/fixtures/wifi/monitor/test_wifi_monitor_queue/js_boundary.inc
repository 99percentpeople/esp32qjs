
typedef int JSContext;
typedef int esp32_mquickjs_runtime_t;
typedef intptr_t JSValue;
typedef struct { JSValue value; } JSGCRef;
typedef struct { uint32_t generation; } esp32_mquickjs_future_token_t;
typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;
#define JS_EXCEPTION ((JSValue)-1)
#define JS_UNDEFINED ((JSValue)0)
#define JS_NULL ((JSValue)1)
#define JS_CLASS_EVENT_QUEUE 17
#define JS_IsException(value) ((value)==JS_EXCEPTION)
#define JS_IsUndefined(value) ((value)==JS_UNDEFINED)
#define MALLOC_CAP_8BIT 0
#define portMAX_DELAY 0
#define pdTRUE 1
typedef void *SemaphoreHandle_t;
static unsigned roots,queue_allocations,queue_frees,resource_frees,closed_calls,context_refs;
static unsigned sdk_allocation_calls,fail_sdk_allocation_at;
static bool fail_allocation,fail_resources,fail_object,fail_lookup,fail_retain,fail_bind,fail_context,fail_frame;
static void *js_opaque;
static JSValue *JS_PushGCRef(JSContext *ctx,JSGCRef *ref) { (void)ctx;++roots;ref->value=JS_UNDEFINED;return &ref->value; }
static JSValue JS_PopGCRef(JSContext *ctx,JSGCRef *ref) { (void)ctx;assert(roots);--roots;return ref->value; }
static void JS_DeleteGCRef(JSContext *ctx,JSGCRef *ref) { (void)ctx;(void)ref;assert(roots);--roots; }
static JSValue JS_ThrowInternalError(JSContext *ctx,const char *text) { (void)ctx;assert(text);return JS_EXCEPTION; }
static JSValue JS_ThrowReferenceError(JSContext *ctx,const char *text) { return JS_ThrowInternalError(ctx,text); }
static JSValue JS_ThrowOutOfMemory(JSContext *ctx) { (void)ctx;return JS_EXCEPTION; }
static JSValue JS_NewObjectClassUser(JSContext *ctx,int id) { (void)ctx;assert(roots && id==JS_CLASS_EVENT_QUEUE);return fail_object?JS_EXCEPTION:17; }
static void JS_SetOpaque(JSContext *ctx,JSValue value,void *opaque) { (void)ctx;assert(value==17);js_opaque=opaque; }
static void *JS_GetOpaque(JSContext *ctx,JSValue value) { (void)ctx;assert(value==17);return js_opaque; }
static int JS_GetClassID(JSContext *ctx,JSValue value) { (void)ctx;return (int)value; }
static void *heap_caps_calloc(size_t count,size_t size,int caps) {
    (void)caps;assert(!critical_depth);++sdk_allocation_calls;
    if(fail_allocation || (fail_sdk_allocation_at && sdk_allocation_calls==fail_sdk_allocation_at))return NULL;
    void *p=calloc(count,size);assert(p);++queue_allocations;return p;
}
static void heap_caps_free(void *p) { assert(!critical_depth);if(p){++queue_frees;free(p);} }
static int xSemaphoreTake(SemaphoreHandle_t lock,int timeout) { (void)timeout;assert(lock && !critical_depth);return pdTRUE; }
static void xSemaphoreGive(SemaphoreHandle_t lock) { assert(lock && !critical_depth); }
static void esp_timer_stop(void *timer) { assert(timer); }
static void esp_timer_delete(void *timer) { assert(timer); }
