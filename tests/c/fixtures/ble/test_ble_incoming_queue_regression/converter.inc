
typedef void JSContext;
typedef int JSValue;
typedef struct { JSValue val; } JSGCRef;
#define JS_UNDEFINED 0
#define JS_EXCEPTION -1
#define JS_IsException(v) ((v)==JS_EXCEPTION)
#define JS_IsUndefined(v) ((v)==JS_UNDEFINED)
#define JS_CLASS_BLE_CONNECTION 2
static int s_ble_closed_connection_ref;
static int calls,fail_at,roots,retired;
static JSGCRef *stack[2];
static JSValue *JS_PushGCRef(void *ctx,JSGCRef *r) { (void)ctx;assert(roots<2);stack[roots++]=r;return &r->val; }
static JSValue JS_PopGCRef(void *ctx,JSGCRef *r) { (void)ctx;assert(roots && stack[--roots]==r);return r->val; }
static int allocate(void) { return ++calls==fail_at ? JS_EXCEPTION : 42; }
static JSValue ble_new_connection_handle(void *ctx,unsigned i,ble_connection_slot_t *s) { (void)ctx;(void)i;(void)s;return allocate(); }
static JSValue JS_NewObject(void *ctx) { (void)ctx;return allocate(); }
static JSValue JS_NewString(void *ctx,const char *s) { (void)ctx;(void)s;return allocate(); }
static JSValue JS_NewUint32(void *ctx,unsigned n) { (void)ctx;(void)n;return allocate(); }
static JSValue JS_NewInt64(void *ctx,int64_t n) { (void)ctx;(void)n;return allocate(); }
static bool esp32_mquickjs_set_property_ref(void *ctx,JSValue *v,const char *key,JSValue value) {
    (void)ctx;(void)v;(void)key;return value!=JS_EXCEPTION && allocate()!=JS_EXCEPTION;
}
static void JS_ThrowReferenceError(void *ctx,const char *s) { (void)ctx;(void)s; }
static void ble_retire_handle(void *ctx,JSValue value,int cls,void *ref) {
    (void)ctx;(void)value;(void)cls;(void)ref;retired++;
}
