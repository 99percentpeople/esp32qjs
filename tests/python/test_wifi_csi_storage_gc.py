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
#include "esp32_mquickjs_wifi_csi_store.h"
#include "esp32_mquickjs_wifi_csi_wire.h"
#define WIFI_CSI_CLOSED 0
#define WIFI_CSI_RUNNING 1
#define CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_BATCH_FRAMES 2
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
typedef struct { int lock,lifecycle;struct {uint32_t queue_capacity;} options;_Atomic(esp32_mquickjs_wifi_csi_resources_t *) resources;void *event_queue; } wifi_csi_session_t;
static wifi_csi_session_t s_wifi_csi;
static void store_lock(void *p){(void)p;}
static esp32_mquickjs_wifi_csi_store_t s_wifi_csi_store={.lock=store_lock,.unlock=store_lock,.maximum_slots=4,.max_frame_bytes=64,.next_generation=1};
static esp32_mquickjs_wifi_csi_store_result_t allocation_result;
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
OPTIONS_MAIN=r'''
static int run_source_options(bool batch_mode) {
    const char *cases[]={
        "owner.source()", "owner.source(null)", "owner.source({})",
        "owner.source({format: undefined})", "owner.source({format: 'esp32qjs-csi/1\\x00'})",
        "owner.source({format: 'esp32qjs-csi/1', extra: 1})",
        "owner.source({get format() { owner.close(); gc(); return 'esp32qjs-csi/1'; }})",
        "(function () { try { owner.source({get format() { throw 12345; }}); } catch (e) { return e === 12345; } return false; })()",
        "owner.source({get format() { gc(); return 'esp32qjs-csi/1'; }})",
        "(function () { var reads=0; var s=owner.source({get format() { reads++; return 'esp32qjs-csi/1'; }}); if (reads!==1) throw new Error('repeated getter'); return s; })()"
    };
    for(unsigned index=0;index<sizeof(cases)/sizeof(cases[0]);index++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;inject=false;memset(&s_wifi_csi,0,sizeof(s_wifi_csi));s_wifi_csi.lifecycle=WIFI_CSI_RUNNING;s_wifi_csi.options.queue_capacity=2;queued=0;
        esp32_mquickjs_wifi_csi_allocator_t allocator={pool_calloc, pool_malloc, pool_free, NULL, NULL};
        assert((s_wifi_csi.resources=esp32_mquickjs_wifi_csi_store_open(&s_wifi_csi_store,2,0,&allocator,&allocation_result))!=NULL);
        esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,true);
        uint8_t bytes[]={1,2,3,4};
        esp32_mquickjs_wifi_csi_metadata_t metadata={.timestamp_us=123456,.radio_generation=17,.channel=6,
            .layout={.byte_length=4,.segment_count=1,.segments={{.length_bytes=4}}}};
        assert(esp32_mquickjs_wifi_csi_callback_publish(s_wifi_csi.resources,&metadata,bytes,4,NULL,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
        JSGCRef owner_ref,result_ref;
        JSValue *owner=JS_PushGCRef(ctx,&owner_ref),*result=JS_PushGCRef(ctx,&result_ref);*owner=*result=JS_UNDEFINED;
        *owner=batch_mode?js_wifi_csi_session_receive_batch(ctx,owner,0,NULL):js_wifi_csi_session_receive(ctx,owner,0,NULL);
        assert(!JS_IsException(*owner));
        assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"owner",*owner)));
        *result=JS_Eval(ctx,cases[index],strlen(cases[index]),"source-options",JS_EVAL_RETVAL);
        if(index<=6) {
            assert(JS_IsException(*result) && JS_HasException(ctx));(void)JS_GetException(ctx);
            assert(atomic_load(&s_wifi_csi.resources->counters.leased_frames)==(index==6?0:1));
            if(index==6)assert(JS_GetOpaque(ctx,*owner)==NULL);
        } else if(index==7)assert(*result==JS_TRUE && !JS_HasException(ctx));
        else {
            assert(!JS_IsException(*result) && !JS_HasException(ctx));
            esp32_mquickjs_byte_span_source_t stream;JSValue error;
            assert(esp32_mquickjs_open_byte_span_source(ctx,*result,"test",&stream,&error));
            esp32_mquickjs_byte_span_t span;size_t total=0;
            while(esp32_mquickjs_byte_span_source_next(ctx,&stream,&span))total+=span.length;
            assert(total==332);
            esp32_mquickjs_byte_span_source_close(ctx,&stream);
            assert(js_byte_span_source_close(ctx,result,0,NULL)==JS_TRUE);
        }
        assert((batch_mode?js_wifi_csi_batch_close(ctx,owner,0,NULL):js_wifi_csi_frame_close(ctx,owner,0,NULL))==JS_TRUE);
        JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&owner_ref);JS_GC(ctx);
        assert(!s_wifi_csi.resources || !atomic_load(&s_wifi_csi.resources->counters.leased_frames));
        esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,false);s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);
        assert(!s_wifi_csi.resources && !s_wifi_csi_store.reserved_slots && root_count==0 && native_live==0);
        JS_FreeContext(ctx);free(heap);
    }
    return 0;
}
'''
SMALL_QUEUE_MAIN = r'''
static int run_small_queue_batch(void) {
    void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    memset(&s_wifi_csi,0,sizeof(s_wifi_csi));s_wifi_csi.lifecycle=WIFI_CSI_RUNNING;
    s_wifi_csi.options.queue_capacity=1;queued=0;
    esp32_mquickjs_wifi_csi_allocator_t allocator={pool_calloc, pool_malloc, pool_free, NULL, NULL};
    assert((s_wifi_csi.resources=esp32_mquickjs_wifi_csi_store_open(&s_wifi_csi_store,1,0,&allocator,&allocation_result))!=NULL);
    esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,true);
    uint8_t bytes[]={1,2,3,4};
    esp32_mquickjs_wifi_csi_metadata_t metadata={.channel=6,.timestamp_us=1,
        .layout={.byte_length=4,.sample_encoding=ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT8}};
    assert(esp32_mquickjs_wifi_csi_callback_publish(s_wifi_csi.resources,&metadata,bytes,4,NULL,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    JSGCRef result_ref,options_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*options=JS_PushGCRef(ctx,&options_ref);
    const char *expression="({maximumFrames:2})";
    *options=JS_Eval(ctx,expression,strlen(expression),"oversized-batch",JS_EVAL_RETVAL);assert(!JS_IsException(*options));
    *result=js_wifi_csi_session_receive_batch(ctx,result,1,options);
    assert(JS_IsException(*result)&&JS_HasException(ctx));JS_GetException(ctx);
    assert(queued==1 && atomic_load(&s_wifi_csi.resources->counters.leased_frames)==1);
    /* The omitted maximum is clipped to queue=1 and the queued owner survives
     * the previous validation failure. Call the actual batch adapter. */
    *result=js_wifi_csi_session_receive_batch(ctx,result,0,NULL);
    assert(!JS_IsException(*result));wifi_csi_batch_ref_t *batch=JS_GetOpaque(ctx,*result);
    assert(batch && batch->frame_count==1 && !queued);
    assert(js_wifi_csi_batch_close(ctx,result,0,NULL)==JS_TRUE);
    JS_PopGCRef(ctx,&options_ref);JS_PopGCRef(ctx,&result_ref);JS_GC(ctx);
    esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,false);s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);
    assert(!s_wifi_csi.resources && !s_wifi_csi_store.reserved_slots && !root_count && !native_live);
    JS_FreeContext(ctx);free(heap);return 0;
}
'''
REOPEN_MAIN=r'''
static int run_retained_reopen(bool moving, bool packet_mode) {
    void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    memset(&s_wifi_csi,0,sizeof(s_wifi_csi));queued=0;collect=moving;inject=true;fail_at=0;
    esp32_mquickjs_wifi_csi_allocator_t allocator={pool_calloc, pool_malloc, pool_free, NULL, NULL};
    s_wifi_csi.resources=esp32_mquickjs_wifi_csi_store_open(&s_wifi_csi_store,2,packet_mode?40:0,&allocator,&allocation_result);assert(s_wifi_csi.resources);
    s_wifi_csi.lifecycle=WIFI_CSI_RUNNING;s_wifi_csi.options.queue_capacity=2;
    uint8_t packet_bytes[37]={8};
    esp32_mquickjs_wifi_csi_packet_input_t packet_input={packet_bytes,37,37,37};
    if(packet_mode)s_wifi_csi.resources->packet_options=(esp32_mquickjs_wifi_csi_packet_options_t){.mode=ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL,.snap_length=40};
    unsigned old_generation=s_wifi_csi.resources->generation;
    esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,true);
    uint8_t bytes[]={7,8,9,10};esp32_mquickjs_wifi_csi_metadata_t metadata={.timestamp_us=55,.layout={.byte_length=4,.segment_count=1,.segments={{.length_bytes=4}}}};
    assert(esp32_mquickjs_wifi_csi_callback_publish(s_wifi_csi.resources,&metadata,bytes,4,packet_mode?&packet_input:NULL,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    JSGCRef frame_ref,view_ref,source_ref;
    JSValue *frame=JS_PushGCRef(ctx,&frame_ref),*view=JS_PushGCRef(ctx,&view_ref),*source=JS_PushGCRef(ctx,&source_ref);
    *frame=js_wifi_csi_session_receive(ctx,frame,0,NULL);assert(!JS_IsException(*frame));
    *view=packet_mode?js_wifi_csi_frame_packet_bytes(ctx,frame,0,NULL):js_wifi_csi_frame_samples(ctx,frame,0,NULL);assert(!JS_IsException(*view));
    *source=packet_mode?js_wifi_csi_frame_packet_source(ctx,frame,0,NULL):js_wifi_csi_frame_sample_source(ctx,frame,0,NULL);assert(!JS_IsException(*source));
    esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,false);
    s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);assert(!s_wifi_csi.resources);
    s_wifi_csi.resources=esp32_mquickjs_wifi_csi_store_open(&s_wifi_csi_store,2,0,&allocator,&allocation_result);assert(s_wifi_csi.resources);
    s_wifi_csi.lifecycle=WIFI_CSI_RUNNING;assert(s_wifi_csi.resources->generation!=old_generation && s_wifi_csi_store.reserved_slots==4);
    /* Public methods on the old Frame must resolve its own generation. */
    const uint8_t *data;size_t length;assert(esp32_mquickjs_byte_view_acquire_read(ctx,*view,"old",&data,&length));
    assert(length==(packet_mode?37:4) && !memcmp(data,packet_mode?packet_bytes:bytes,length));esp32_mquickjs_byte_view_release_read(ctx,*view);
    assert(js_wifi_csi_frame_close(ctx,frame,0,NULL)==JS_TRUE);
    assert(js_byte_view_close(ctx,view,0,NULL)==JS_TRUE);
    esp32_mquickjs_byte_span_source_t stream;JSValue error;
    assert(esp32_mquickjs_open_byte_span_source(ctx,*source,"old",&stream,&error));
    /* Source close preserves the generic active-read busy contract. The old
     * pool remains readable across the rejected close and a new generation. */
    assert(JS_IsException(js_byte_span_source_close(ctx,source,0,NULL)) && JS_HasException(ctx));
    (void)JS_GetException(ctx);
    esp32_mquickjs_byte_span_t span;assert(esp32_mquickjs_byte_span_source_next(ctx,&stream,&span));
    assert(span.length==(packet_mode?37:4) && !memcmp(span.data,packet_mode?packet_bytes:bytes,span.length));
    assert(s_wifi_csi_store.reserved_slots==4);
    esp32_mquickjs_byte_span_source_close(ctx,&stream);
    assert(js_byte_span_source_close(ctx,source,0,NULL)==JS_TRUE);
    assert(js_byte_span_source_close(ctx,source,0,NULL)==JS_TRUE);
    assert(!esp32_mquickjs_wifi_csi_store_acquire(&s_wifi_csi_store,old_generation));
    assert(s_wifi_csi_store.reserved_slots==2 && s_wifi_csi.resources);
    s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);
    JS_PopGCRef(ctx,&source_ref);JS_PopGCRef(ctx,&view_ref);JS_PopGCRef(ctx,&frame_ref);JS_GC(ctx);
    inject=collect=false;assert(!s_wifi_csi.resources && !s_wifi_csi_store.reserved_slots && !native_live && !root_count);
    JS_FreeContext(ctx);free(heap);return 0;
}
'''
MAIN=OPTIONS_MAIN+SMALL_QUEUE_MAIN+REOPEN_MAIN+r'''
int main(int argc,char **argv) {
    (void)argc;if(!strcmp(argv[1],"reopen"))return run_retained_reopen(atoi(argv[2]),false);
    if(!strcmp(argv[1],"packet-reopen"))return run_retained_reopen(atoi(argv[2]),true);
    if(!strcmp(argv[1],"options"))return run_source_options(atoi(argv[2]));
    if(!strcmp(argv[1],"batch-limit"))return run_small_queue_batch();
    bool packet_mode=argc>3 && atoi(argv[3]);
    int mode=atoi(argv[1]);collect=atoi(argv[2]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;inject=false;
        memset(&s_wifi_csi,0,sizeof(s_wifi_csi));s_wifi_csi.lifecycle=WIFI_CSI_RUNNING;s_wifi_csi.options.queue_capacity=2;queued=0;
        esp32_mquickjs_wifi_csi_allocator_t allocator={pool_calloc, pool_malloc, pool_free, NULL, NULL};
        assert((s_wifi_csi.resources=esp32_mquickjs_wifi_csi_store_open(&s_wifi_csi_store,2,packet_mode?40:0,&allocator,&allocation_result))!=NULL);
        esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,true);
        uint8_t packet_bytes[37]={8};
        esp32_mquickjs_wifi_csi_packet_input_t packet_input={packet_bytes,37,37,37};
        if(packet_mode)s_wifi_csi.resources->packet_options=(esp32_mquickjs_wifi_csi_packet_options_t){.mode=ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL,.snap_length=40};
        uint8_t bytes[]={1,2,3,4};
        esp32_mquickjs_wifi_csi_metadata_t metadata={.timestamp_us=INT64_C(1)<<45,.radio_generation=17,.channel=6,.rssi=-50,
            .phy_flags=ESP32_MQUICKJS_WIFI_RX_WIRE_FEC_AVAILABLE|ESP32_MQUICKJS_WIFI_RX_WIRE_LDPC,
            .phy=ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU,.guard_interval_ns=800,.he_ltf_size=4,.dcm_state=1,
            .bandwidth_available=true,.bandwidth_mhz=20,.mcs_available=true,.antenna_available=true,
            .noise_floor_available=true,.stbc_available=true,.channel_estimate_valid_available=true,.channel_estimate_valid=true,
            .layout={.known=true,.schema=ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_LEGACY,.sample_encoding=ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT8,.byte_length=4,.sample_bits=8,.iq_pair_count=2,.segment_count=1,
                .segments={{.type=ESP32_MQUICKJS_WIFI_CSI_SEGMENT_LLTF,.length_bytes=4,.iq_pair_count=2,.subcarrier_range_count=1,.subcarrier_ranges={{-1,0}},.null_subcarrier_count=1}}}};
        if(mode==8 || mode==12)metadata.layout.iq_pair_count=99;
        if(mode==9 || mode==11){metadata.layout.byte_length=3;metadata.layout.iq_pair_count=1;metadata.layout.trailing_padding_bytes=1;
            metadata.layout.segments[0].length_bytes=2;metadata.layout.segments[0].iq_pair_count=1;metadata.layout.segments[0].subcarrier_ranges[0].end=-1;}
        bool batch_mode=(mode>=4 && mode<=9) || mode==14;
        bool frame_mode=(mode>=1 && mode<=3) || (mode>=10 && !batch_mode);
        bool wire_mode=mode==6 || mode==8 || mode==9 || mode>=10;
        unsigned count=batch_mode ? 2 : 1;
        for(unsigned i=0;i<count;i++)assert(esp32_mquickjs_wifi_csi_callback_publish(s_wifi_csi.resources,&metadata,bytes,(mode==9 || mode==11)?3:4,packet_mode?&packet_input:NULL,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
        JSGCRef owner_ref,result_ref,options_ref;
        JSValue *owner=JS_PushGCRef(ctx,&owner_ref),*result=JS_PushGCRef(ctx,&result_ref),*options=JS_PushGCRef(ctx,&options_ref);
        *owner=*result=*options=JS_UNDEFINED;
        const char *expression="({format: 'esp32qjs-csi/1'})";
        *options=JS_Eval(ctx,expression,strlen(expression),"source-options",JS_EVAL_RETVAL);assert(!JS_IsException(*options));
        JSValue arg=JS_NewInt32(ctx,0);
        if(frame_mode)*owner=js_wifi_csi_session_receive(ctx,owner,0,NULL);
        if(batch_mode && mode!=4)*owner=js_wifi_csi_session_receive_batch(ctx,owner,0,NULL);
        calls=0;fail_at=nth;moved_roots=0;inject=true;
        if(mode==0)*result=js_wifi_csi_session_receive(ctx,owner,0,NULL);
        if(mode==1)*result=packet_mode?js_wifi_csi_frame_packet_bytes(ctx,owner,0,NULL):js_wifi_csi_frame_samples(ctx,owner,0,NULL);
        if(mode==2)*result=packet_mode?js_wifi_csi_frame_copy_packet_bytes(ctx,owner,0,NULL):js_wifi_csi_frame_copy_samples(ctx,owner,0,NULL);
        if(mode==3)*result=packet_mode?js_wifi_csi_frame_packet_source(ctx,owner,0,NULL):js_wifi_csi_frame_sample_source(ctx,owner,0,NULL);
        if(mode==4)*result=js_wifi_csi_session_receive_batch(ctx,owner,0,NULL);
        if(mode==5)*result=packet_mode?js_wifi_csi_batch_packet_bytes(ctx,owner,1,&arg):js_wifi_csi_batch_samples(ctx,owner,1,&arg);
        if(wire_mode && batch_mode)*result=js_wifi_csi_batch_source(ctx,owner,1,options);
        if(wire_mode && frame_mode)*result=js_wifi_csi_frame_source(ctx,owner,1,options);
        if(mode==7)*result=js_wifi_csi_batch_info(ctx,owner,1,&arg);
        inject=false;if(!nth)total=calls;
        if(nth) { if(!JS_IsException(*result) || !JS_HasException(ctx)) {fprintf(stderr,"CSI swallowed failure mode=%d nth=%d\n",mode,nth);return 1;} (void)JS_GetException(ctx); }
        else if(mode==8 || mode==12) { assert(JS_IsException(*result) && JS_HasException(ctx));(void)JS_GetException(ctx); }
        else { assert(!JS_IsException(*result) && !JS_HasException(ctx));if(collect && (mode==0 || mode==4 || mode==7))assert(moved_roots>0); }
        if(!nth && (mode==0 || mode==7)) {
            JSGCRef check_ref;JSValue *value=JS_PushGCRef(ctx,&check_ref);
            *value=mode==0?JS_GetPropertyStr(ctx,*result,"info"):*result;
            assert(!JS_IsException(*value));
            assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*value,"sourceMac")));
            assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*value,"rssi")));
            assert(JS_GetPropertyStr(ctx,*value,"rxSequence")== (packet_mode?JS_NewInt32(ctx,0):JS_NULL));
            JSGCRef child_ref;JSValue *child=JS_PushGCRef(ctx,&child_ref);
            *child=JS_GetPropertyStr(ctx,*value,"signal");assert(JS_GetPropertyStr(ctx,*child,"rssi")==JS_NewInt32(ctx,-50));
            *child=JS_GetPropertyStr(ctx,*value,"channel");assert(JS_GetPropertyStr(ctx,*child,"primary")==JS_NewInt32(ctx,6));
            *child=JS_GetPropertyStr(ctx,*value,"addresses");assert(JS_GetClassID(ctx,*child)==JS_CLASS_OBJECT);
            if(!packet_mode)assert(JS_GetPropertyStr(ctx,*child,"source")==JS_NULL);
            *child=JS_GetPropertyStr(ctx,*value,"validity");assert(JS_GetPropertyStr(ctx,*child,"callbackDataValid")==JS_TRUE);
            assert(JS_GetPropertyStr(ctx,*child,"layoutKnown")==JS_TRUE);
            JS_PopGCRef(ctx,&child_ref);
            *value=JS_GetPropertyStr(ctx,*value,"phy");assert(!JS_IsException(*value));
            assert(wifi_csi_string_equals(ctx,JS_GetPropertyStr(ctx,*value,"fecCoding"),"ldpc"));
            assert(JS_GetPropertyStr(ctx,*value,"aggregation")==JS_NULL);
            assert(JS_GetPropertyStr(ctx,*value,"sounding")==JS_NULL);
            assert(JS_GetPropertyStr(ctx,*value,"legacyRate")==JS_NULL);
            JSValue gi=JS_GetPropertyStr(ctx,*value,"guardIntervalNs");
            assert(gi==JS_NewInt32(ctx,800));
            JSValue ltf=JS_GetPropertyStr(ctx,*value,"heLtfSize");assert(ltf==JS_NewInt32(ctx,4));
            assert(JS_GetPropertyStr(ctx,*value,"dcm")==JS_FALSE);
            JS_PopGCRef(ctx,&check_ref);
        }
        /* Close public owners while retained views/sources survive. */
        if(frame_mode)assert(js_wifi_csi_frame_close(ctx,owner,0,NULL)==JS_TRUE);
        if(batch_mode && mode!=4)assert(js_wifi_csi_batch_close(ctx,owner,0,NULL)==JS_TRUE);
        if(!nth && (mode==1 || mode==2 || mode==5)) {
            const uint8_t *data;size_t n;assert(esp32_mquickjs_byte_view_acquire_read(ctx,*result,"test",&data,&n));assert(n==(packet_mode?37:4) && !memcmp(data,packet_mode?packet_bytes:bytes,n));
            esp32_mquickjs_byte_view_release_read(ctx,*result);
        }
        if(!nth && (mode==3 || (wire_mode && mode!=8 && mode!=12))) {
            assert(atomic_load(&s_wifi_csi.resources->counters.leased_frames)>0);
            assert(!esp32_mquickjs_wifi_csi_resources_deinit(s_wifi_csi.resources));
            if(mode>=13){esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,false);s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);assert(!s_wifi_csi.resources && s_wifi_csi_store.reserved_slots==2);}
            esp32_mquickjs_byte_span_source_t source;JSValue error;
            assert(esp32_mquickjs_open_byte_span_source(ctx,*result,"test",&source,&error));
            if(mode==14 || mode==15) {
                assert(JS_IsException(js_byte_span_source_close(ctx,result,0,NULL)) && JS_HasException(ctx));
                (void)JS_GetException(ctx);
                assert(s_wifi_csi_store.reserved_slots==2);
            }
            esp32_mquickjs_byte_span_t span;size_t size=0;unsigned parts=0;
            while(esp32_mquickjs_byte_span_source_next(ctx,&source,&span)) {
                if(!packet_mode && (mode==9 || mode==11) && parts>0){if(span.length==1)assert(span.data[0]==0);else assert(span.length==3 && !memcmp(span.data,bytes,3));}
                if(wire_mode && parts==0){assert(span.length==32+count*(40+256));assert(!memcmp(span.data,"E32QCSI1",8));}
                if(packet_mode && span.length==37)assert(!memcmp(span.data,packet_bytes,37));
                size+=span.length;parts++;
                if(mode==13)break;
            }
            assert(parts==(mode==3 || mode==13 ? 1 : (mode==9 || mode==11) ? count*(packet_mode?4:2)+1 : count*(packet_mode?3:1)+1));
            assert(size==(mode==3 ? (packet_mode?37:4) : mode==13 ? 32+count*(40+256) : 32+count*(40+256+4+(packet_mode?40:0))));
            esp32_mquickjs_byte_span_source_close(ctx,&source);
            assert(!esp32_mquickjs_open_byte_span_source(ctx,*result,"test",&source,&error));(void)JS_GetException(ctx);
            assert(js_byte_span_source_close(ctx,result,0,NULL)==JS_TRUE);
        }
        JS_PopGCRef(ctx,&options_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&owner_ref);JS_GC(ctx);
        while(queued) {esp32_mquickjs_wifi_csi_event_t event;esp32_mquickjs_event_queue_try_receive(NULL,&event);wifi_csi_update_event(&event,WIFI_CSI_EVENT_DISCARD);}
        assert(!s_wifi_csi.resources || !atomic_load(&s_wifi_csi.resources->counters.leased_frames));
        esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,false);s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);
        assert(!s_wifi_csi.resources && !s_wifi_csi_store.reserved_slots && root_count==0 && native_live==0);
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
        for path in [CORE/'esp32_mquickjs_native_pool.c',CORE/'esp32_mquickjs_native_lease.c',CSI/'esp32_mquickjs_wifi_csi_resources.c',CSI/'esp32_mquickjs_wifi_csi_packet.c',CSI.parent/'wifi_common/esp32_mquickjs_wifi_rx.c',CSI/'esp32_mquickjs_wifi_csi_store.c',CSI/'esp32_mquickjs_wifi_csi_batch.c',CSI/'esp32_mquickjs_wifi_csi_wire.c',CSI.parent/'wifi_common/esp32_mquickjs_wifi_rx_wire.c',CSI.parent/'wifi_common/esp32_mquickjs_wifi_rx_wire_metadata.c',CORE/'esp32_mquickjs_options.c']:
            text=path.read_text()
            if path.name=='esp32_mquickjs_options.c':
                header=(ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace('#include "esp32_mquickjs_types.h"','')
                text=text.replace('#include "esp32_mquickjs_options.h"',header)
            extra+=text
        names=['wifi_csi_secondary_name','wifi_csi_phy_name','wifi_csi_format_mac','wifi_csi_string_equals','wifi_csi_maybe_destroy_resources','wifi_csi_resolve_event','wifi_csi_lease_release','wifi_csi_lease_request_close','wifi_csi_retain_event','wifi_csi_byte_view_release','wifi_csi_sample_encoding_name','wifi_csi_layout_schema_name','wifi_csi_segment_type_name','wifi_csi_layout_to_js','wifi_csi_packet_mode_name','wifi_csi_packet_to_js','wifi_csi_frame_info_to_js','wifi_csi_make_frame']
        extra+=csi[csi.index('typedef enum {\n    WIFI_CSI_EVENT_RETAIN'):csi.index('static bool wifi_csi_update_event(')]
        names.insert(names.index('wifi_csi_lease_release'),'wifi_csi_update_event')
        extra+='\n'.join(extract(csi,n) for n in names)
        # Both production source ops tables and all iterator/control encoding code.
        extra+=csi[csi.index('static bool wifi_csi_sample_source_next('):csi.index('static wifi_csi_session_t *wifi_csi_session_from_value(')]
        extra+='\n'.join(extract(csi,n) for n in ['wifi_csi_frame_from_value','wifi_csi_batch_from_value','js_wifi_csi_frame_finalizer','wifi_csi_frame_bytes','js_wifi_csi_frame_samples','js_wifi_csi_frame_copy_samples','js_wifi_csi_frame_packet_bytes','js_wifi_csi_frame_copy_packet_bytes','wifi_csi_frame_data_source','js_wifi_csi_frame_sample_source','js_wifi_csi_frame_packet_source','js_wifi_csi_frame_source','js_wifi_csi_frame_close','wifi_csi_batch_release_owner','js_wifi_csi_batch_finalizer','wifi_csi_batch_index','js_wifi_csi_batch_info','wifi_csi_batch_bytes','js_wifi_csi_batch_samples','js_wifi_csi_batch_packet_bytes','js_wifi_csi_batch_source','js_wifi_csi_batch_close'])
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
            lower=name.lower()
            classes+=f'static const JSPropDef {lower}_proto[]={{JS_CFUNC_DEF("source",1,js_wifi_csi_{lower}_source),JS_CFUNC_DEF("close",0,js_wifi_csi_{lower}_close),JS_PROP_END}};\n'
            classes+=f'static const JSClassDef {lower}_class=JS_CLASS_DEF("WiFiCsi{lower}",0,js_byte_view_constructor,JS_CLASS_WIFI_CSI_{name},NULL,{lower}_proto,NULL,js_wifi_csi_{lower}_finalizer);\n'
            entries+=f'JS_PROP_CLASS_DEF("WiFiCsi{lower}", &{lower}_class),'
            decl+=f'#define JS_CLASS_WIFI_CSI_{name} (JS_CLASS_USER+{offset})\nvoid js_wifi_csi_{lower}_finalizer(JSContext *,void *);\n'
            for method in ['source','close']:
                decl+=f'JSValue js_wifi_csi_{lower}_{method}(JSContext *,JSValue *,int,JSValue *);\n'
        cls.binary=build(cls.temp.name,extra,MAIN,classes,entries,decl)

    def test_synthetic_frame_batch_view_source_nth_failure_and_moving_gc(self):
        for mode in range(16):
            for gc in (0,1):
                with self.subTest(mode=mode,gc=gc):run([str(self.binary),str(mode),str(gc)])

    def test_real_vm_source_option_getters_close_gc_throw_and_single_read(self):
        for batch in (0, 1):
            with self.subTest(batch=batch):
                run([str(self.binary), "options", str(batch)])

    def test_batch_limit_uses_the_selected_queue_before_consuming_an_event(self):
        run([str(self.binary), "batch-limit", "0"])

    def test_retained_view_and_active_source_survive_new_generation_with_gc(self):
        for collect in (0, 1):
            run([str(self.binary), "reopen", str(collect)])

    def test_packet_view_and_active_packet_source_survive_new_pool_with_gc(self):
        for collect in (0, 1):
            run([str(self.binary), "packet-reopen", str(collect)])

    def test_packet_conversion_views_copy_batch_and_wire_with_nth_oom_and_gc(self):
        for mode in range(16):
            for collect in (0, 1):
                with self.subTest(mode=mode, collect=collect):
                    run([str(self.binary), str(mode), str(collect), "1"])
