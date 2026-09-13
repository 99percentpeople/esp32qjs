
typedef intptr_t JSValue;
typedef struct {JSValue val;} JSGCRef;
typedef struct {
    unsigned count,roots;bool fail_object,fail_property;
    struct {int class_id;void *opaque;} objects[32];
} JSContext;
#define JS_CLASS_BYTE_VIEW 1
#define JS_CLASS_BYTE_SPAN_SOURCE 2
#define JS_CLASS_BITMAP_SPAN_SOURCE 3
#define JS_EXCEPTION (-1)
#define JS_UNDEFINED 0
#define JS_TRUE 1
#define JS_IsException(v) ((v)==JS_EXCEPTION)
#define JS_IsUndefined(v) ((v)==JS_UNDEFINED)
static JSValue js_error(JSContext *ctx,const char *message,...){(void)ctx;(void)message;return JS_EXCEPTION;}
#define JS_ThrowInternalError js_error
#define JS_ThrowReferenceError js_error
#define JS_ThrowTypeError js_error
#define JS_ThrowOutOfMemory(ctx) ((void)(ctx),JS_EXCEPTION)
#include "utils/esp32_mquickjs_byte_source.h"
static JSValue JS_NewObjectClassUser(JSContext *ctx,int cls){
    if(ctx->fail_object)return JS_EXCEPTION;
    assert(ctx->count<32);
    ctx->objects[ctx->count].class_id=cls;return ++ctx->count;
}
static int JS_GetClassID(JSContext *ctx,JSValue object){return ctx->objects[object-1].class_id;}
static void *JS_GetOpaque(JSContext *ctx,JSValue object){return ctx->objects[object-1].opaque;}
static void JS_SetOpaque(JSContext *ctx,JSValue object,void *opaque){ctx->objects[object-1].opaque=opaque;}
static JSValue *JS_PushGCRef(JSContext *ctx,JSGCRef *r){++ctx->roots;return &r->val;}
static JSValue JS_PopGCRef(JSContext *ctx,JSGCRef *r){assert(ctx->roots);--ctx->roots;return r->val;}
static JSValue *JS_AddGCRef(JSContext *ctx,JSGCRef *r){return JS_PushGCRef(ctx,r);}
static void JS_DeleteGCRef(JSContext *ctx,JSGCRef *r){(void)JS_PopGCRef(ctx,r);}
static JSValue JS_SetPropertyStr(JSContext *ctx,JSValue object,const char *key,JSValue value){
    (void)object;(void)key;(void)value;return ctx->fail_property?JS_EXCEPTION:JS_TRUE;
}
