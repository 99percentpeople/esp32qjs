"""Deferred real MQuickJS Monitor capture, moving GC and property failure cases."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest

from tests.support.wireless_vm_fixture import ROOT, CORE, INTERNAL, build, extract, run
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit


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


INJECT_PROPERTIES = fixture_text('wifi/monitor/test_wifi_monitor_options/inject_properties.inc')

MAIN = fixture_text('wifi/monitor/test_wifi_monitor_options/main.inc')
