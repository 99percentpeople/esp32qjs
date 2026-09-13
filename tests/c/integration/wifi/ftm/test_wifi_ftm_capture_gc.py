"""Deferred real VM FTM option/status/report/error conversions with GC/Nth OOM.

Native Session allocation, close/release and report-entry read use production
functions. Prepared report bytes are the producer boundary; this does not replace
or execute Session/Radio scheduling, covered separately by the native fixtures.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types
from tests.c.integration.wifi.monitor.test_wifi_rx_target import INTERNAL, unit
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run

SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm.c'
SESSION = SOURCE.with_name('esp32_mquickjs_wifi_ftm_session.c')
RADIO = SOURCE.parent.parent / 'wifi_radio/esp32_mquickjs_wifi_radio.c'


class WiFiFtmCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        source = SOURCE.read_text();native = SESSION.read_text()
        code = fixture_text('wifi/ftm/test_wifi_ftm_capture_gc/setupclass-code.inc')
        code += '#define JS_CLASS_WIFI_FTM_SESSION (JS_CLASS_USER+52)\n'
        code += 'typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 2\n#define ESP_ERR_INVALID_STATE 3\n#define ESP_ERR_NO_MEM 4\n#define ESP_ERR_TIMEOUT 5\n'
        code += sdk_types('esp32c5/representative', ('wifi_ftm_initiator_cfg_t','wifi_event_ftm_report_t','wifi_ftm_report_entry_t'))
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_ftm_radio.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_ftm_session.h')
        code += BOUNDARIES
        start = native.index('struct esp32_mquickjs_wifi_ftm_session {')
        end = native.index('/* Detach only', start)
        code += native[start:end]
        code += extract(RADIO.read_text(), 'esp32_mquickjs_wifi_ftm_config_valid')
        for name in ('ftm_detach_entries_locked','ftm_free_entries','esp32_mquickjs_wifi_ftm_retain',
                     'esp32_mquickjs_wifi_ftm_release','esp32_mquickjs_wifi_ftm_create','esp32_mquickjs_wifi_ftm_close',
                     'esp32_mquickjs_wifi_ftm_status','esp32_mquickjs_wifi_ftm_report_entry'):
            code += extract(native, name)
        code += re.search(r'typedef enum \{[^}]*\} ftm_operation_t;', source).group(0)
        code += re.search(r'typedef struct \{[^}]*\} ftm_options_t;', source).group(0)
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        code += source[start:source.index('\n};', start)+3]
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0)+'\n'
        for name in ('ftm_hex','ftm_mac','ftm_address','ftm_options','ftm_status_name','ftm_operation_name',
                     'ftm_error','ftm_status_to_js','ftm_timestamp','ftm_report_to_js','ftm_finish'):
            code += extract(source, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_capture_options_validate_before_allocation(self):
        base = 'peerAddress:"02:11:22:33:44:55",channel:6'
        cases = [('({'+base+'})',1),('({'+base+',frameCount:64,burstPeriodMs:10000,maxReportEntries:0,timeoutMs:1})',1),
                 ('({'+base+',burstPeriodMs:100})',0),('({'+base+',burstPeriodMs:201})',0),
                 ('({'+base+',frameCount:8})',0),('({'+base+',frameCount:32.1})',0),
                 ('({'+base+',maxReportEntries:65})',0),('({'+base+',maxReportEntries:4294967295})',0),
                 ('({'+base+',timeoutMs:0})',0),('({'+base+',timeoutMs:"10"})',0),
                 ('({'+base+',extra:true})',0),('({'+base.replace('02:','03:')+'})',0),
                 ('({'+base.replace('02:11:22:33:44:55','00:00:00:00:00:00')+'})',0),
                 ('({'+base.replace('44:55','44:55\\u0000')+'})',0),
                 ('({'+base.replace('channel:6','channel:36')+'})',0),('null',0),('[]',0),
                 ('({'+base+',get frameCount(){gc();throw new Error("sentinel");}})',0),
                 ('(function(){var n=0;return {'+base+',get frameCount(){gc();if(++n!==1)throw new Error("twice");return 24;}};})()',1)]
        for value,expected in cases:
            with self.subTest(value=value):run([str(self.binary),'capture',value,str(expected)])

    def test_report_words_gc_each_allocation_and_retry(self):
        for mode in ('report','status','error','receive-timeout'):run([str(self.binary),mode,'','1'])


BOUNDARIES = fixture_text('wifi/ftm/test_wifi_ftm_capture_gc/boundaries.inc')
MAIN = fixture_text('wifi/ftm/test_wifi_ftm_capture_gc/main.inc')
