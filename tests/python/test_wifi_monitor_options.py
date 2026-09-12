"""Deferred real MQuickJS Monitor capture, moving GC and property failure cases."""
import re
import tempfile
import unittest

from wireless_vm_fixture import ROOT, CORE, INTERNAL, build, extract, run
from test_wifi_rx_target import unit


def production_options_code(band):
    filter_header = (INTERNAL / 'esp32_mquickjs_wifi_rx_filter.h').read_text()
    filter_types = filter_header[filter_header.index('#define ESP32_MQUICKJS_WIFI_RX_FILTER_MAC_CAPACITY'):
                                 filter_header.index('/* Per subscriber')]
    capture_header = (INTERNAL / 'esp32_mquickjs_wifi_monitor_capture.h').read_text()
    options_type = re.search(r'typedef struct \{\n    esp32_mquickjs_wifi_rx_filter_t filter;.*?\} esp32_mquickjs_wifi_monitor_capture_options_t;',
                             capture_header, re.S).group(0)
    limits = ''
    for header, names in [
        ('esp32_mquickjs_wifi_monitor_resources.h', ['ESP32_MQUICKJS_WIFI_MONITOR_MAX_SNAP_LENGTH']),
        ('esp32_mquickjs_native_pool.h', ['ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY'])]:
        source = (INTERNAL / header).read_text()
        for name in names:
            limits += re.search(r'^#define ' + name + r' .+$', source, re.M).group(0) + '\n'
    helpers = unit(CORE / 'esp32_mquickjs_options.c')
    channel = extract((ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text(),
                      'esp32_mquickjs_wifi_radio_5ghz_channel_bit')
    body = '#include "mquickjs_priv.h"\n#include <math.h>\n#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
    body += '#define CONFIG_SOC_WIFI_SUPPORT_5G ' + str(band) + '\n'
    body += filter_types + options_type + '\n' + limits
    body += unit(INTERNAL / 'esp32_mquickjs_wifi_monitor_options.h')
    body += INJECT_PROPERTIES + helpers + channel
    body += unit(ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor_options.c')
    return body


class WiFiMonitorOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binaries = [build(cls.temp.name + '/' + str(band), production_options_code(band), MAIN)
                        for band in [0, 1]]

    def check(self, expression, valid=True, scenario=0, bands=(0, 1)):
        for band in bands:
            with self.subTest(expression=expression, band=band):
                run([str(self.binaries[band]), expression, str(int(valid)), str(scenario)])

    def test_defaults_and_complete_capture(self):
        self.check('undefined', scenario=1)
        self.check('({})', scenario=1)
        self.check('({channel:"current",filter:{},capture:{},buffering:{},powerSavePolicy:"preserve"})', scenario=1)
        self.check('({channel:6,powerSavePolicy:"require-none",filter:{types:["management","data"],subtypes:[0,15],'
                   'sourceMac:["02:00:00:00:00:01","02:00:00:00:00:02"],destinationMac:"ff:ff:ff:ff:ff:ff",'
                   'bssid:"AA:BB:CC:DD:EE:FF",minimumRssi:-128,sampleEvery:4294967295,maximumRateHz:1000000,validOnly:false},'
                   'capture:{snapLength:16384,requireComplete:true},buffering:{poolCapacity:128,queueCapacity:128,overflow:"drop-newest"}})', scenario=2)
        self.check('({filter:{types:[],subtypes:[],maximumRateHz:0}})', scenario=3)
        macs = ','.join('"02:00:00:00:00:%02x"' % i for i in range(8))
        self.check('({filter:{sourceMac:[' + macs + ']}})')
        self.check('({capture:{snapLength:1},buffering:{poolCapacity:1,queueCapacity:128},filter:{minimumRssi:127}})')

    def test_unknown_fields_nul_null_and_enum_rejections(self):
        for expression in ['null', 'true', '[]', '1', '"options"',
                           '({legacy:true})', '({"channel\\x00":6})', '({channel:"current\\x00"})',
                           '({channel:"6"})', '({powerSavePolicy:"none"})', '({powerSavePolicy:"preserve\\x00"})',
                           '({filter:null})', '({capture:[]})', '({buffering:null})',
                           '({filter:{unknown:true}})', '({capture:{schema:"legacy"}})',
                           '({buffering:{overflow:"drop-oldest"}})', '({buffering:{overflow:"drop-newest\\x00"}})',
                           '({filter:{types:["unknown"]}})', '({filter:{types:["data\\x00"]}})',
                           '({filter:{types:["data","data"]}})', '({filter:{subtypes:[1,1]}})',
                           '({filter:{subtypes:[16]}})', '({filter:{subtypes:[-1]}})',
                           '({filter:{subtypes:[1.5]}})', '({filter:{subtypes:[null]}})',
                           '({filter:{subtypes:[NaN]}})', '({filter:{types:[undefined,"data"]}})',
                           '({filter:{types:"data"}})', '({filter:{subtypes:1}})',
                           '({filter:{validOnly:1}})', '({capture:{requireComplete:"true"}})']:
            self.check(expression, valid=False)

    def test_mac_shapes_duplicates_and_bounds(self):
        for value in ['null', '[]', '[1]', '"AA:BB:CC:DD:EE"', '"AA-BB-CC-DD-EE-FF"',
                      '"AA:BB:CC:DD:EE:FG"', '"AA:BB:CC:DD:EE:FF\\x00"',
                      '["AA:BB:CC:DD:EE:FF","aa:bb:cc:dd:ee:ff"]',
                      '[' + ','.join('"02:00:00:00:00:%02x"' % i for i in range(9)) + ']']:
            for role in ['sourceMac', 'destinationMac', 'bssid']:
                self.check('({filter:{' + role + ':' + value + '}})', valid=False)

    def test_numbers_do_not_coerce_and_5ghz_is_target_gated(self):
        for path, bad in [
            ('channel', ['0','15','35','37','255','256','-1','1.5','NaN','Infinity','true','null']),
            ('capture.snapLength', ['0','16385','-1','1.5','"24"','true','NaN','Infinity']),
            ('buffering.poolCapacity', ['0','129','-1','1.5','null']),
            ('buffering.queueCapacity', ['0','129','false']),
            ('filter.sampleEvery', ['0','4294967296','-1','1.5','"2"']),
            ('filter.maximumRateHz', ['-1','1000001','0.5','null']),
            ('filter.minimumRssi', ['-129','128','0.5','"-60"','NaN'])]:
            for value in bad:
                pieces = path.split('.')
                expression = pieces[-1] + ':' + value
                if len(pieces) == 2:
                    expression = pieces[0] + ':{' + expression + '}'
                self.check('({' + expression + '})', valid=False)
        for channel in [1, 14]:
            self.check('({channel:' + str(channel) + '})')
        for channel in [36, 64, 100, 149, 177]:
            self.check('({channel:' + str(channel) + '})', bands=(1,))
            self.check('({channel:' + str(channel) + '})', valid=False, bands=(0,))


INJECT_PROPERTIES = r'''
static JSValue monitor_get_property(JSContext *ctx,JSValue object,const char *key) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
    JS_PopGCRef(ctx,&r);return result;
}
static JSValue monitor_get_index(JSContext *ctx,JSValue object,uint32_t index) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyUint32(ctx,*root,index);
    JS_PopGCRef(ctx,&r);return result;
}
#define JS_GetPropertyStr monitor_get_property
#define JS_GetPropertyUint32 monitor_get_index
'''

MAIN = r'''
#undef JS_GetPropertyStr
#undef JS_GetPropertyUint32
int main(int argc,char **argv) {
    assert(argc==4);bool expected=atoi(argv[2])!=0;int scenario=atoi(argv[3]),total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef r;JSValue *root=push_root(ctx,&r);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"options",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        esp32_mquickjs_wifi_monitor_options_t output,before;
        memset(&output,0xa5,sizeof(output));memcpy(&before,&output,sizeof(output));
        calls=0;fail_at=nth;collect=true;inject=true;
        bool ok=esp32_mquickjs_wifi_monitor_capture_options(ctx,*root,&output);
        inject=false;if(!nth)total=calls;
        assert(ok==(expected && nth==0));
        if(!ok) {
            assert(JS_HasException(ctx) && !memcmp(&output,&before,sizeof(output)));
            (void)JS_GetException(ctx);
        } else {
            assert(!JS_HasException(ctx));
            if(scenario==1) {
                assert(!output.capture.channel && !output.capture.require_power_save_none);
                assert(output.capture.filter.type_mask==15 && output.capture.filter.sample_every==1 && output.capture.filter.valid_only);
                assert(!output.capture.filter.subtype_filter && !output.capture.filter.source.count && !output.require_complete);
                assert(output.pool_capacity==16 && output.queue_capacity==16 && output.snap_length==2048);
            } else if(scenario==2) {
                assert(output.capture.channel==6 && output.capture.require_power_save_none);
                assert(output.capture.filter.type_mask==5 && output.capture.filter.subtype_filter && output.capture.filter.subtype_mask==32769);
                assert(output.capture.filter.source.count==2 && output.capture.filter.source.values[1][5]==2);
                assert(output.capture.filter.destination.count==1 && output.capture.filter.destination.values[0][0]==255);
                assert(output.capture.filter.bssid.count==1 && output.capture.filter.bssid.values[0][0]==170);
                assert(output.capture.filter.minimum_rssi_set && output.capture.filter.minimum_rssi==-128);
                assert(output.capture.filter.sample_every==UINT32_MAX && output.capture.filter.maximum_rate_hz==1000000 && !output.capture.filter.valid_only);
                assert(output.snap_length==16384 && output.pool_capacity==128 && output.queue_capacity==128 && output.require_complete);
            } else if(scenario==3) {
                assert(!output.capture.filter.type_mask && output.capture.filter.subtype_filter && !output.capture.filter.subtype_mask);
            }
        }
        assert(root_count==1 && !native_live);
        pop_root(ctx,&r);assert(!root_count);
        JS_FreeContext(ctx);free(heap);
    }
}
'''
