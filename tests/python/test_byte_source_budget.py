"""Deferred production ByteView/Source ownership with the complete memory manager.

Only JS/heap/lock boundaries are replaced. Moving GC and setters use the real VM
in test_wireless_byte_storage_gc; this fixture checks actual budget lifetime.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from wireless_vm_fixture import ROOT, CORE, extract


class ByteSourceBudget(unittest.TestCase):
    def test_control_reserve_failed_transfer_read_retention_and_final_release(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = (CORE / 'esp32_mquickjs_byte_source.c').read_text()
        heap = (ROOT / 'tests/c/test_memory_wireless.c').read_text().split('int main(void)')[0]
        structs = source[source.index('#define ESP32_MQUICKJS_BYTE_SPAN_SOURCE_OWNER_KEY'):
                         source.index('static void *byte_source_allocate')]
        code = heap + SHIM + structs
        code += (CORE / 'esp32_mquickjs_byte_span.c').read_text()
        for name in [
            'byte_source_allocate', 'leased_byte_span_source_next', 'leased_byte_span_source_close',
            'byte_view_from_value', 'byte_span_source_from_value', 'byte_view_release', 'byte_view_make',
            'esp32_mquickjs_open_byte_span_source', 'esp32_mquickjs_new_wireless_byte_span_source',
            'esp32_mquickjs_new_wireless_owned_byte_view', 'esp32_mquickjs_new_wireless_retained_byte_view',
            'esp32_mquickjs_new_byte_span_source', 'esp32_mquickjs_new_owned_byte_view',
            'esp32_mquickjs_byte_view_acquire_read', 'esp32_mquickjs_byte_view_release_read',
            'js_byte_view_close', 'js_byte_span_source_close',
            'js_byte_view_finalizer', 'js_byte_span_source_finalizer',
        ]:
            code += extract(source, name)
        with tempfile.TemporaryDirectory() as temp:
            path, binary = Path(temp) / 'case.c', Path(temp) / 'case'
            for name in ('mquickjs.h', 'esp32_mquickjs_types.h'):
                (Path(temp) / name).write_text('#pragma once\n')
            path.write_text(code + MAIN)
            sources = [CORE / ('esp32_mquickjs_' + n + '.c') for n in
                       ['memory', 'memory_budget', 'memory_owner_accounting', 'memory_dma_accounting']]
            result = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                     '-Wno-unused-function', '-I' + temp,
                                     '-I' + str(ROOT / 'tests/c/memory_stubs'),
                                     '-I' + str(ROOT / 'components/esp32_mquickjs/internal'),
                                     str(path), *map(str, sources), '-o', str(binary)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


SHIM = r'''
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
'''
MAIN = r'''
static unsigned destroyed,inner_closed;
static bool fail_open,bad_iterator;
static void destroy(JSContext *ctx,void *p){(void)ctx;++destroyed;esp32_mquickjs_memory_payload_free(p);}
static bool next(JSContext *ctx,void *p,esp32_mquickjs_byte_span_t *out){(void)ctx;(void)p;(void)out;return false;}
static void close_inner(JSContext *ctx,void *p){(void)ctx;(void)p;++inner_closed;}
static bool open_inner(JSContext *ctx,JSValue value,void *p,esp32_mquickjs_byte_span_source_t *out,JSValue *error){
    (void)ctx;(void)value;if(fail_open){*error=JS_EXCEPTION;return false;}
    *out=(esp32_mquickjs_byte_span_source_t){.opaque=p,.next=bad_iterator?NULL:next,.close=close_inner};return true;
}
static const esp32_mquickjs_byte_span_source_object_ops_t ops={.open=open_inner,.destroy=destroy};
static void *pool(void){
    void *p=esp32_mquickjs_memory_wireless_alloc("wifi.csi",32,
        ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
    assert(p);return p;
}
int main(void){
    esp32_mquickjs_memory_init();
    void *probe=pool();size_t overhead=status().wireless.regions[0].reserved-32;
    esp32_mquickjs_memory_payload_free(probe);empty();
    for(unsigned source=0;source<2;++source)for(unsigned failure=0;failure<4;++failure){
        JSContext ctx={0};void *payload=pool();unsigned before=destroyed;
        ctx.fail_object=failure==0;ctx.fail_property=failure==3;
        allocation_calls=0;fail_at=failure==1?1:failure==2?2:0;
        JSValue value=source
            ?esp32_mquickjs_new_wireless_byte_span_source("wifi.csi",&ctx,JS_TRUE,&ops,payload)
            :esp32_mquickjs_new_wireless_owned_byte_view("wifi.csi",&ctx,payload,32);
        if(!source && failure==3){
            assert(value!=JS_EXCEPTION);assert(js_byte_view_close(&ctx,&value,0,NULL)==JS_TRUE);
        }else assert(value==JS_EXCEPTION);
        if(source)assert(destroyed==before+1);
        fail_at=0;assert(!ctx.roots);empty();
    }
    {
        JSContext ctx={0};uint8_t *payload=pool();
        JSValue value=esp32_mquickjs_new_wireless_retained_byte_view("wifi.csi",&ctx,payload,32,
            esp32_mquickjs_memory_payload_free,payload);assert(value!=JS_EXCEPTION);
        assert(esp32_mquickjs_memory_wireless_retire(payload));
        size_t held_bytes=status().wireless.regions[0].reserved;
        assert(status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_RETIRED_POOL]>0);
        const uint8_t *data;size_t length;
        assert(esp32_mquickjs_byte_view_acquire_read(&ctx,value,"test",&data,&length));
        assert(data==payload && length==32);
        assert(js_byte_view_close(&ctx,&value,0,NULL)==JS_TRUE);
        assert(js_byte_view_close(&ctx,&value,0,NULL)==JS_TRUE);
        esp32_mquickjs_memory_release_generation();
        assert(status().wireless.regions[0].reserved==held_bytes);
        esp32_mquickjs_byte_view_release_read(&ctx,value);assert(!JS_GetOpaque(&ctx,value));empty();
    }
    {
        JSContext ctx={0};void *payload=pool();unsigned before=destroyed;
        JSValue value=esp32_mquickjs_new_wireless_byte_span_source("wifi.csi",&ctx,JS_UNDEFINED,&ops,payload);
        assert(value!=JS_EXCEPTION);size_t baseline=status().wireless.regions[0].reserved;
        esp32_mquickjs_byte_span_source_t stream;JSValue error;
        for(unsigned nth=1;nth<=2;++nth){
            allocation_calls=0;fail_at=nth;
            assert(!esp32_mquickjs_open_byte_span_source(&ctx,value,"test",&stream,&error));
            assert(!ctx.roots && status().wireless.regions[0].reserved==baseline);
        }
        fail_at=0;fail_open=true;
        assert(!esp32_mquickjs_open_byte_span_source(&ctx,value,"test",&stream,&error));
        fail_open=false;bad_iterator=true;
        assert(!esp32_mquickjs_open_byte_span_source(&ctx,value,"test",&stream,&error));
        bad_iterator=false;assert(inner_closed==1 && !ctx.roots);
        assert(status().wireless.regions[0].reserved==baseline);
        size_t data_bytes=baseline-
            status().wireless.regions[0].roles[ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL];
        /* Existing source handles use the control reserve, not the data
         * budget. Fill the latter while leaving room for the read lease. */
        void *fill=esp32_mquickjs_memory_wireless_alloc("fill",3584-data_bytes-overhead,
            ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL,ESP32_MQUICKJS_MEMORY_BUDGET_POOL);assert(fill);
        assert(!esp32_mquickjs_memory_wireless_alloc("copy",1,
            ESP32_MQUICKJS_MEMORY_DEFAULT,ESP32_MQUICKJS_MEMORY_BUDGET_COPY));
        assert(esp32_mquickjs_open_byte_span_source(&ctx,value,"test",&stream,&error));
        assert(ctx.roots==1 && status().wireless.regions[0].reserved>3584);
        assert(js_byte_span_source_close(&ctx,&value,0,NULL)==JS_EXCEPTION && destroyed==before);
        esp32_mquickjs_byte_span_source_close(&ctx,&stream);
        esp32_mquickjs_byte_span_source_close(&ctx,&stream);
        assert(inner_closed==2 && !ctx.roots);
        ctx.fail_property=true;
        assert(js_byte_span_source_close(&ctx,&value,0,NULL)==JS_EXCEPTION);
        assert(destroyed==before+1 && !JS_GetOpaque(&ctx,value));
        esp32_mquickjs_memory_payload_free(fill);empty();
    }
    {
        JSContext ctx={0};void *payload=pool();
        JSValue value=esp32_mquickjs_new_wireless_byte_span_source("wifi.csi",&ctx,JS_UNDEFINED,&ops,payload);
        assert(value!=JS_EXCEPTION);
        js_byte_span_source_finalizer(&ctx,JS_GetOpaque(&ctx,value));empty();
        value=esp32_mquickjs_new_owned_byte_view(&ctx,heap_caps_malloc(32,MALLOC_CAP_8BIT),32);
        assert(value!=JS_EXCEPTION && !status().wireless.regions[0].reserved);
        js_byte_view_finalizer(&ctx,JS_GetOpaque(&ctx,value));empty();
    }
    return 0;
}
'''
