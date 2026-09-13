
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#define BLE_STORE_OBJ_TYPE_PEER_SEC 1
#define BLE_LIFECYCLE_ACTIVE 1
static struct {
    uint8_t address[6];int own_addr_type,lifecycle;bool synchronized,role_central,role_peripheral;
    char device_name[32];uint16_t max_connections;
    struct { bool open; } connections[2];
    struct { bool active; atomic_uint dropped; } scanner;
    struct { bool active; } advertiser;
    struct { atomic_uint dropped; } server;
    atomic_uint reset_count,dropped_connection_events;
    atomic_int rejected_connection_error;
} s_ble;
static void ble_format_address(const uint8_t *addr,char *out) { (void)addr;strcpy(out,"10:20:30:40:50:60"); }
static const char *ble_address_type_name(int type) { (void)type;return "public"; }
static int ble_store_util_count(int type,int *out) { (void)type;*out=0;return 0; }
static int calls,fail_at,external_roots,moved_roots;
static JSGCRef *tracked_roots[16];
static bool collect;
static bool step(JSContext *ctx) {
    if(collect) {
        JSValue before[16];
        for(int i=0;i<external_roots;i++) before[i]=tracked_roots[i]->val;
        JS_GC(ctx);
        for(int i=0;i<external_roots;i++)
            if(before[i]!=tracked_roots[i]->val) moved_roots++;
        /* Leave an unrooted hole before the next live allocation so a later GC
         * must compact live objects. This is the actual VM, not a handle model. */
        char garbage[512];memset(garbage,'x',sizeof(garbage));
        assert(!JS_IsException(JS_NewStringLen(ctx,garbage,sizeof(garbage))));
    }
    return ++calls==fail_at;
}
static JSValue new_string(JSContext *ctx,const char *str) {
    if(step(ctx)) return JS_ThrowOutOfMemory(ctx);
    return JS_NewString(ctx,str);
}
static JSValue new_array(JSContext *ctx,int count) {
    if(step(ctx)) return JS_ThrowOutOfMemory(ctx);
    return JS_NewArray(ctx,count);
}
static JSValue new_object(JSContext *ctx) {
    if(step(ctx)) return JS_ThrowOutOfMemory(ctx);
    return JS_NewObject(ctx);
}
static JSValue set_index(JSContext *ctx,JSValue obj,uint32_t index,JSValue val) {
    if(++calls==fail_at) return JS_ThrowOutOfMemory(ctx);
    return JS_SetPropertyUint32(ctx,obj,index,val);
}
static JSValue set_property(JSContext *ctx,JSValue obj,const char *key,JSValue val) {
    if(++calls==fail_at) return JS_ThrowOutOfMemory(ctx);
    return JS_SetPropertyStr(ctx,obj,key,val);
}
static JSValue *push_root(JSContext *ctx,JSGCRef *ref) {
    assert(external_roots<16);tracked_roots[external_roots++]=ref;
    return JS_PushGCRef(ctx,ref);
}
static JSValue pop_root(JSContext *ctx,JSGCRef *ref) {
    assert(external_roots>0 && tracked_roots[external_roots-1]==ref);
    external_roots--;return JS_PopGCRef(ctx,ref);
}
#define JS_NewString new_string
#define JS_NewArray new_array
#define JS_NewObject new_object
#define JS_SetPropertyUint32 set_index
#define JS_SetPropertyStr set_property
#define JS_PushGCRef push_root
#define JS_PopGCRef pop_root
