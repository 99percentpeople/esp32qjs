
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
typedef intptr_t JSValue;typedef int JSContext;
typedef struct{JSValue val;}JSGCRef;
typedef struct{int value;}esp32_mquickjs_future_driver_t;
#define JS_NULL (-2)
#define JS_UNDEFINED (-3)
#define JS_EXCEPTION (-1)
static JSGCRef*roots[300];static unsigned root_count;
static JSValue*JS_AddGCRef(JSContext*ctx,JSGCRef*ref){(void)ctx;assert(root_count<300);roots[root_count++]=ref;return &ref->val;}
static void JS_DeleteGCRef(JSContext*ctx,JSGCRef*ref){(void)ctx;unsigned i=0;while(i<root_count&&roots[i]!=ref)++i;
 assert(i<root_count);roots[i]=roots[--root_count];roots[root_count]=NULL;}
static JSValue*JS_PushGCRef(JSContext*ctx,JSGCRef*ref){ref->val=JS_UNDEFINED;return JS_AddGCRef(ctx,ref);}
static JSValue JS_PopGCRef(JSContext*ctx,JSGCRef*ref){JSValue value=ref->val;JS_DeleteGCRef(ctx,ref);return value;}
