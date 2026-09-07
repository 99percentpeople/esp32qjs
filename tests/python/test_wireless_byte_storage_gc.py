"""Exercise production byte storage and its actual MQuickJS class finalizers."""
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from wireless_vm_fixture import build, run

EXTRA = r'''
static int released,destroyed,opened,closed,open_mode;
static uint8_t bytes[]={1,2,3,4};
static void release(void *p) { assert(p==bytes);released++; }
static void destroy(JSContext *ctx,void *p) { (void)ctx;assert(p==bytes);destroyed++; }
static bool next(JSContext *ctx,void *p,esp32_mquickjs_byte_span_t *out) {
    (void)ctx;out->data=p;out->length=4;return true;
}
static void close_inner(JSContext *ctx,void *p) { (void)ctx;assert(p==bytes);closed++; }
static bool open_inner(JSContext *ctx,JSValue value,void *p,esp32_mquickjs_byte_span_source_t *out,JSValue *error) {
    (void)value;opened++;
    if(open_mode==1) { *error=JS_ThrowInternalError(ctx,"opener failed");return false; }
    out->opaque=p;out->next=open_mode==2 ? NULL : next;out->close=close_inner;return true;
}
static size_t length(void *p) { assert(p==bytes);return 4; }
static const esp32_mquickjs_byte_span_source_object_ops_t ops={.open=open_inner,.destroy=destroy,.known_length=length};
'''
MAIN = r'''
int main(int argc,char **argv) {
    (void)argc;int mode=atoi(argv[1]);collect=atoi(argv[2]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        inject=false;released=destroyed=opened=closed=0;calls=0;fail_at=nth;moved_roots=0;open_mode=0;
        JSGCRef result_ref,owner_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*owner=JS_PushGCRef(ctx,&owner_ref);
        *result=*owner=JS_UNDEFINED;
        if(mode>=2) *owner=esp32_mquickjs_new_retained_byte_view(ctx,bytes,4,release,bytes);
        if(mode>=3) *result=esp32_mquickjs_new_byte_span_source(ctx,*owner,&ops,bytes);
        uint8_t *owned=NULL;if(mode==0) { owned=heap_caps_malloc(4,1);memcpy(owned,bytes,4); }
        inject=true;JSValue value=JS_UNDEFINED;
        if(mode==0) value=esp32_mquickjs_new_owned_byte_view(ctx,owned,4);
        if(mode==1) value=esp32_mquickjs_new_retained_byte_view(ctx,bytes,4,release,bytes);
        if(mode==2) value=esp32_mquickjs_new_byte_span_source(ctx,*owner,&ops,bytes);
        esp32_mquickjs_byte_span_source_t span={0};
        if(mode>=3 && mode<=5) {
            open_mode=mode-3;
            bool ok=esp32_mquickjs_open_byte_span_source(ctx,*result,"test",&span,&value);
            assert(ok==!JS_IsException(value));
        }
        if(mode==6) value=js_byte_span_source_close(ctx,result,0,NULL);
        inject=false;
        if(mode<=2)*result=value;
        if(nth==0)total=calls;
        if(nth || mode==4 || mode==5) {
            assert(JS_IsException(value) && JS_HasException(ctx));(void)JS_GetException(ctx);
        } else assert(!JS_IsException(value) && !JS_HasException(ctx));
        if(mode<=1 && !nth) {
            const uint8_t *data;size_t size;
            assert(esp32_mquickjs_byte_view_acquire_read(ctx,*result,"test",&data,&size));assert(size==4 && !memcmp(data,bytes,4));
            assert(js_byte_view_close(ctx,result,0,NULL)==JS_TRUE);
            assert(js_byte_view_close(ctx,result,0,NULL)==JS_TRUE);
            assert(released==0 && !esp32_mquickjs_byte_view_is_open(ctx,*result));
            esp32_mquickjs_byte_view_release_read(ctx,*result);
        }
        if(mode==3 && !nth) {
            *owner=JS_UNDEFINED;JS_GC(ctx);assert(released==0);
            assert(JS_IsException(js_byte_span_source_close(ctx,result,0,NULL)));(void)JS_GetException(ctx);
            esp32_mquickjs_byte_span_t chunk;
            assert(esp32_mquickjs_byte_span_source_next(ctx,&span,&chunk));assert(chunk.length==4);
            esp32_mquickjs_byte_span_source_close(ctx,&span);esp32_mquickjs_byte_span_source_close(ctx,&span);assert(closed==1);
        }
        if(mode>=3) { assert(js_byte_span_source_close(ctx,result,0,NULL)==JS_TRUE);assert(destroyed==1); }
        JS_PopGCRef(ctx,&owner_ref);JS_PopGCRef(ctx,&result_ref);JS_GC(ctx);
        assert(root_count==0 && native_live==0);
        if(mode)assert(released==1);
        if(mode>=2)assert(destroyed==1);
        JS_FreeContext(ctx);free(heap);
    }
    printf("byte storage mode=%d gc=%d boundaries=%d\n",mode,collect,total);
}
'''
INPUT_MAIN = r"""
int main(void) {
    void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    JSGCRef array_ref,view_ref;JSValue *array=JS_PushGCRef(ctx,&array_ref),*view=JS_PushGCRef(ctx,&view_ref);
    *array=JS_NewObject(ctx);*view=esp32_mquickjs_new_retained_byte_view(ctx,bytes,4,release,bytes);
    double invalid[]={-1,1.5,4294967297.0,NAN,INFINITY};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        JSValue value=JS_NewFloat64(ctx,invalid[i]);
        assert(JS_IsException(js_byte_view_get_uint8(ctx,view,1,&value)));(void)JS_GetException(ctx);
        JS_SetPropertyStr(ctx,*array,"length",JS_NewFloat64(ctx,invalid[i]));JS_SetPropertyUint32(ctx,*array,0,JS_NewInt32(ctx,7));
        esp32_mquickjs_byte_source_t source;uint8_t *owned=NULL;JSValue error;
        if(esp32_mquickjs_get_byte_source(ctx,*array,"test",&source,&owned,&error)) {
            fprintf(stderr,"invalid array length accepted: %g\n",invalid[i]);return 1;
        }
        assert(JS_HasException(ctx) && !owned);(void)JS_GetException(ctx);
        JS_SetPropertyStr(ctx,*array,"length",JS_NewInt32(ctx,1));
        JS_SetPropertyUint32(ctx,*array,0,JS_NewFloat64(ctx,invalid[i]));
        if(esp32_mquickjs_get_byte_source(ctx,*array,"test",&source,&owned,&error)) {
            fprintf(stderr,"invalid byte accepted: %g\n",invalid[i]);return 1;
        }
        assert(JS_HasException(ctx) && !owned);(void)JS_GetException(ctx);
    }
    JS_PopGCRef(ctx,&view_ref);JS_PopGCRef(ctx,&array_ref);JS_GC(ctx);assert(native_live==0 && released==1);
    JS_FreeContext(ctx);free(heap);
}
"""
class WirelessByteStorageGc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        cls.binary=build(pathlib.Path(cls.temp.name)/"storage",EXTRA,MAIN)
        cls.input_binary=build(pathlib.Path(cls.temp.name)/"input",EXTRA,INPUT_MAIN)

    def test_construction_close_read_lease_and_finalizer(self):
        for mode in range(7):
            for gc in (0,1):
                with self.subTest(mode=mode,gc=gc):
                    run([str(self.binary),str(mode),str(gc)])

    def test_invalid_length_byte_and_offset_are_rejected_before_transfer(self):
        run([str(self.input_binary)])
