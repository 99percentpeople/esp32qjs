"""Synthetic CSI callbacks through production pool, Frame/Batch/View/Source + VM."""
from tests.support.fixtures import fixture_text
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
import pathlib
import sys
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run
CSI=ROOT/'components/esp32_mquickjs/src/modules/wifi_csi'
SDK=fixture_text('wifi/csi/test_wifi_csi_storage_gc/sdk.inc')
OPTIONS_MAIN=fixture_text('wifi/csi/test_wifi_csi_storage_gc/options_main.inc')
SMALL_QUEUE_MAIN = fixture_text('wifi/csi/test_wifi_csi_storage_gc/small_queue_main.inc')
REOPEN_MAIN=fixture_text('wifi/csi/test_wifi_csi_storage_gc/reopen_main.inc')
MAIN=OPTIONS_MAIN+SMALL_QUEUE_MAIN+REOPEN_MAIN+fixture_text('wifi/csi/test_wifi_csi_storage_gc/main.inc')
class WifiCsiStorageGc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        csi=(CSI/'esp32_mquickjs_wifi_csi.c').read_text()
        types=csi[csi.index('typedef struct {\n    uint32_t generation;\n} wifi_csi_session_ref_t;'):csi.index('static const char *TAG')]
        constants='\n'.join(line for line in csi.splitlines() if line.startswith('#define WIFI_CSI_BATCH_') or line.startswith('#define WIFI_CSI_BINARY_VERSION'))
        extra=SDK+constants+'\n'+types
        for path in [CORE/'esp32_mquickjs_native_pool.c',CORE/'esp32_mquickjs_native_lease.c',CSI/'esp32_mquickjs_wifi_csi_resources.c',CSI/'esp32_mquickjs_wifi_csi_packet.c',CSI.parent/'wifi_common/esp32_mquickjs_wifi_rx.c',CSI.parent/'wifi_common/esp32_mquickjs_wifi_frame_type.c',CSI/'esp32_mquickjs_wifi_csi_store.c',CSI/'esp32_mquickjs_wifi_csi_batch.c',CSI/'esp32_mquickjs_wifi_csi_wire.c',CSI.parent/'wifi_common/esp32_mquickjs_wifi_rx_wire.c',CSI.parent/'wifi_common/esp32_mquickjs_wifi_rx_wire_metadata.c',CORE/'esp32_mquickjs_options.c']:
            text=unit(path) if path.name=='esp32_mquickjs_wifi_frame_type.c' else path.read_text()
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
