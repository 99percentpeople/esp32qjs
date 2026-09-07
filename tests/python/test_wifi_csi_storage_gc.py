"""Synthetic CSI callbacks through production pool, Frame/Batch/View/Source + VM."""
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from wireless_vm_fixture import ROOT,CORE,build,extract,run
CSI=ROOT/'components/esp32_mquickjs/src/modules/wifi_csi'
SDK=r'''
#include "esp32_mquickjs_wifi_csi_resources.h"
#define WIFI_CSI_CLOSED 0
#define WIFI_CSI_RUNNING 1
#define WIFI_CSI_BATCH_HEADER_BYTES 24U
#define WIFI_CSI_BATCH_DIRECTORY_BYTES 16U
#define WIFI_CSI_BATCH_METADATA_BYTES 192U
#define WIFI_CSI_BINARY_VERSION 1U
#define CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_BATCH_FRAMES 2
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
typedef struct { int lock,lifecycle;bool resources_destroying;esp32_mquickjs_wifi_csi_resources_t resources;void *event_queue; } wifi_csi_session_t;
static wifi_csi_session_t s_wifi_csi;
static void wifi_csi_snapshot_stats(wifi_csi_session_t *s) { (void)s; }
static void *pool_calloc(size_t n,size_t size,void *p) { (void)p;return heap_caps_calloc(n,size,1); }
static void *pool_malloc(size_t n,void *p) { (void)p;return heap_caps_malloc(n,1); }
static void pool_free(void *p,void *opaque) { (void)opaque;heap_caps_free(p); }
static esp32_mquickjs_wifi_csi_event_t events[2];static unsigned queued;
static bool publish(const esp32_mquickjs_wifi_csi_event_t *event,void *p) { (void)p;assert(queued<2);events[queued++]=*event;return true; }
static bool esp32_mquickjs_event_queue_try_receive(void *queue,esp32_mquickjs_wifi_csi_event_t *event) {
    (void)queue;if(!queued)return false;*event=events[0];memmove(events,events+1,--queued*sizeof(*events));return true;
}
static int64_t esp_timer_get_time(void) { return 1000; }
static wifi_csi_session_t *wifi_csi_session_from_value(JSContext *ctx,JSValue value,bool closed) { (void)ctx;(void)value;(void)closed;return &s_wifi_csi; }
'''
MAIN=r'''
int main(int argc,char **argv) {
    (void)argc;int mode=atoi(argv[1]);collect=atoi(argv[2]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;inject=false;
        memset(&s_wifi_csi,0,sizeof(s_wifi_csi));s_wifi_csi.lifecycle=WIFI_CSI_RUNNING;queued=0;
        esp32_mquickjs_wifi_csi_allocator_t allocator={pool_calloc,pool_malloc,pool_free,NULL};
        assert(esp32_mquickjs_wifi_csi_resources_init(&s_wifi_csi.resources,1,2,64,&allocator));
        esp32_mquickjs_wifi_csi_resources_set_accepting(&s_wifi_csi.resources,true);
        uint8_t bytes[]={1,2,3,4};
        esp32_mquickjs_wifi_csi_metadata_t metadata={.timestamp_us=INT64_C(1)<<45,.channel=6,.rssi=-50,
            .bandwidth_available=true,.bandwidth_mhz=20,.mcs_available=true,.antenna_available=true,
            .noise_floor_available=true,.stbc_available=true,.channel_estimate_valid_available=true,.channel_estimate_valid=true,
            .layout={.known=true,.schema=ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_LEGACY,.sample_encoding=ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT8,.byte_length=4,.sample_bits=8,.iq_pair_count=2,.segment_count=1,
                .segments={{.type=ESP32_MQUICKJS_WIFI_CSI_SEGMENT_LLTF,.length_bytes=4,.iq_pair_count=2,.subcarrier_range_count=1,.subcarrier_ranges={{-1,1}},.null_subcarrier_count=1}}}};
        unsigned count=mode>=4 ? 2 : 1;
        for(unsigned i=0;i<count;i++)assert(esp32_mquickjs_wifi_csi_callback_publish(&s_wifi_csi.resources,&metadata,bytes,4,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
        JSGCRef owner_ref,result_ref;JSValue *owner=JS_PushGCRef(ctx,&owner_ref),*result=JS_PushGCRef(ctx,&result_ref);*owner=*result=JS_UNDEFINED;
        JSValue arg=JS_NewInt32(ctx,0);
        if(mode>=1 && mode<=3)*owner=js_wifi_csi_session_receive(ctx,owner,0,NULL);
        if(mode>=5)*owner=js_wifi_csi_session_receive_batch(ctx,owner,0,NULL);
        calls=0;fail_at=nth;moved_roots=0;inject=true;
        if(mode==0)*result=js_wifi_csi_session_receive(ctx,owner,0,NULL);
        if(mode==1)*result=js_wifi_csi_frame_samples(ctx,owner,0,NULL);
        if(mode==2)*result=js_wifi_csi_frame_copy_samples(ctx,owner,0,NULL);
        if(mode==3)*result=js_wifi_csi_frame_source(ctx,owner,0,NULL);
        if(mode==4)*result=js_wifi_csi_session_receive_batch(ctx,owner,0,NULL);
        if(mode==5)*result=js_wifi_csi_batch_samples(ctx,owner,1,&arg);
        if(mode==6)*result=js_wifi_csi_batch_source(ctx,owner,0,NULL);
        if(mode==7)*result=js_wifi_csi_batch_info(ctx,owner,1,&arg);
        inject=false;if(!nth)total=calls;
        if(nth) { if(!JS_IsException(*result) || !JS_HasException(ctx)) {fprintf(stderr,"CSI swallowed failure mode=%d nth=%d\n",mode,nth);return 1;} (void)JS_GetException(ctx); }
        else { assert(!JS_IsException(*result) && !JS_HasException(ctx));if(collect && (mode==0 || mode==4 || mode==7))assert(moved_roots>0); }
        /* Close public owners while retained views/sources survive. */
        if(mode>=1 && mode<=3)assert(js_wifi_csi_frame_close(ctx,owner,0,NULL)==JS_TRUE);
        if(mode>=5)assert(js_wifi_csi_batch_close(ctx,owner,0,NULL)==JS_TRUE);
        if(!nth && (mode==1 || mode==2 || mode==5)) {
            const uint8_t *data;size_t n;assert(esp32_mquickjs_byte_view_acquire_read(ctx,*result,"test",&data,&n));assert(n==4 && !memcmp(data,bytes,4));
            esp32_mquickjs_byte_view_release_read(ctx,*result);
        }
        if(!nth && (mode==3 || mode==6)) {
            assert(atomic_load(&s_wifi_csi.resources.counters.leased_frames)>0);
            assert(!esp32_mquickjs_wifi_csi_resources_deinit(&s_wifi_csi.resources));
            esp32_mquickjs_byte_span_source_t source;JSValue error;
            assert(esp32_mquickjs_open_byte_span_source(ctx,*result,"test",&source,&error));
            esp32_mquickjs_byte_span_t span;size_t size=0;unsigned parts=0;
            while(esp32_mquickjs_byte_span_source_next(ctx,&source,&span)) {size+=span.length;parts++;}
            assert(parts==(mode==3 ? 1 : 3));assert(size==(mode==3 ? 4 : 24+2*16+2*192+8));
            esp32_mquickjs_byte_span_source_close(ctx,&source);
            assert(!esp32_mquickjs_open_byte_span_source(ctx,*result,"test",&source,&error));(void)JS_GetException(ctx);
            assert(js_byte_span_source_close(ctx,result,0,NULL)==JS_TRUE);
        }
        JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&owner_ref);JS_GC(ctx);
        while(queued) {esp32_mquickjs_wifi_csi_event_t event;esp32_mquickjs_event_queue_try_receive(NULL,&event);esp32_mquickjs_wifi_csi_slot_discard_event(&s_wifi_csi.resources,&event);}
        assert(!atomic_load(&s_wifi_csi.resources.counters.leased_frames));
        s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);
        assert(!s_wifi_csi.resources.slots && root_count==0 && native_live==0);
        JS_FreeContext(ctx);free(heap);
    }
    printf("CSI mode=%d gc=%d boundaries=%d\n",mode,collect,total);
}
'''
class WifiCsiStorageGc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        csi=(CSI/'esp32_mquickjs_wifi_csi.c').read_text()
        types=csi[csi.index('typedef struct {\n    uint32_t generation;\n} wifi_csi_session_ref_t;'):csi.index('static const char *TAG')]
        constants='\n'.join(line for line in csi.splitlines() if line.startswith('#define WIFI_CSI_BATCH_') or line.startswith('#define WIFI_CSI_BINARY_VERSION'))
        extra=SDK+constants+'\n'+types
        for path in [CORE/'esp32_mquickjs_native_pool.c',CORE/'esp32_mquickjs_native_lease.c',CSI/'esp32_mquickjs_wifi_csi_resources.c',CSI/'esp32_mquickjs_wifi_csi_batch.c',CORE/'esp32_mquickjs_options.c']:
            text=path.read_text()
            if path.name=='esp32_mquickjs_options.c':
                header=(ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace('#include "esp32_mquickjs_types.h"','')
                text=text.replace('#include "esp32_mquickjs_options.h"',header)
            extra+=text
        names=['wifi_csi_secondary_name','wifi_csi_phy_name','wifi_csi_format_mac','wifi_csi_string_equals','wifi_csi_maybe_destroy_resources','wifi_csi_resolve_event','wifi_csi_lease_release','wifi_csi_lease_request_close','wifi_csi_retain_event','wifi_csi_byte_view_release','wifi_csi_sample_encoding_name','wifi_csi_layout_schema_name','wifi_csi_segment_type_name','wifi_csi_layout_to_js','wifi_csi_frame_info_to_js','wifi_csi_make_frame']
        extra+='\n'.join(extract(csi,n) for n in names)
        # Both production source ops tables and all iterator/control encoding code.
        extra+=csi[csi.index('static bool wifi_csi_frame_source_next('):csi.index('static wifi_csi_session_t *wifi_csi_session_from_value(')]
        extra+='\n'.join(extract(csi,n) for n in ['wifi_csi_frame_from_value','wifi_csi_batch_from_value','js_wifi_csi_frame_finalizer','js_wifi_csi_frame_samples','js_wifi_csi_frame_copy_samples','js_wifi_csi_frame_source','js_wifi_csi_frame_close','wifi_csi_batch_release_owner','js_wifi_csi_batch_finalizer','wifi_csi_batch_index','js_wifi_csi_batch_info','js_wifi_csi_batch_samples','js_wifi_csi_batch_source','js_wifi_csi_batch_close'])
        # Only the scheduling boundary is controlled. Frame conversion and owner
        # transfer use the production implementation on actual pool events.
        extra+='''static JSValue js_wifi_csi_session_receive(JSContext *ctx,JSValue *self,int argc,JSValue *argv) {
            (void)self;(void)argc;(void)argv;esp32_mquickjs_wifi_csi_event_t event;
            if(!esp32_mquickjs_event_queue_try_receive(NULL,&event))return JS_NULL;
            return wifi_csi_make_frame(ctx,&event);
        }\n'''
        extra+=extract(csi,'js_wifi_csi_session_receive_batch')
        classes='';entries='';decl=''
        for name,offset in [('FRAME',43),('BATCH',44)]:
            lower=name.lower();classes+=f'static const JSClassDef {lower}_class=JS_CLASS_DEF("WiFiCsi{lower}",0,js_byte_view_constructor,JS_CLASS_WIFI_CSI_{name},NULL,NULL,NULL,js_wifi_csi_{lower}_finalizer);\n'
            entries+=f'JS_PROP_CLASS_DEF("WiFiCsi{lower}", &{lower}_class),'
            decl+=f'#define JS_CLASS_WIFI_CSI_{name} (JS_CLASS_USER+{offset})\nvoid js_wifi_csi_{lower}_finalizer(JSContext *,void *);\n'
        cls.binary=build(cls.temp.name,extra,MAIN,classes,entries,decl)

    def test_synthetic_frame_batch_view_source_nth_failure_and_moving_gc(self):
        for mode in range(8):
            for gc in (0,1):
                with self.subTest(mode=mode,gc=gc):run([str(self.binary),str(mode),str(gc)])
