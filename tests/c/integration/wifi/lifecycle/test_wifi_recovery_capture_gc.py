"""Deferred MQuickJS recovery capture/result/error rooting and Nth allocation.

Uses production capture/converters, with native state observations injected.
No fixture import, compilation or execution in the API implementation phase.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.config.test_wifi_configuration_cleanup import WIFI
from tests.c.integration.wifi.lifecycle.test_wifi_recovery_runtime import recovery_request_code
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.wireless_vm_fixture import CORE, INTERNAL, build, extract, run

SOURCE = WIFI.parent.parent / 'wifi_common/esp32_mquickjs_wifi_recovery.c'


class WiFiRecoveryCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = SOURCE.read_text()
        header = (INTERNAL / 'esp32_mquickjs_wifi.h').read_text()
        code = recovery_request_code(twt=True) + BOUNDARIES
        code += structure(header, 'esp32_mquickjs_wifi_configuration_execution_t')
        code += structure(header, 'esp32_mquickjs_wifi_recovery_t')
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        for name in ('recovery_method', 'recovery_code', 'recovery_whole_generation', 'recovery_capture_kind', 'recovery_capture', 'ftm_recovery_capture', 'raw_tx_recovery_capture', 'twt_recovery_capture', 'recovery_error', 'recovery_finish', 'recovery_on_timeout'):
            code += extract(source, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_strict_immutable_identity_and_exception_allocation_paths(self):
        cases = [
            ('({sequence:51,radioGeneration:9})', 1),
            ('({sequence:4294967295,radioGeneration:4294967295,allowDisconnect:true,timeoutMs:60000})', 1),
            ('({sequence:0,radioGeneration:9})', 0),
            ('({sequence:1.5,radioGeneration:9})', 0),
            ('({sequence:51,radioGeneration:4294967296})', 0),
            ('({sequence:51})', 0),
            ('({sequence:51,radioGeneration:9,operationId:3})', 0),
            ('({sequence:51,radioGeneration:9,allowDisconnect:1})', 0),
            ('({sequence:51,radioGeneration:9,timeoutMs:0})', 0),
            ('({sequence:51,radioGeneration:9,get timeoutMs(){throw new Error("sentinel");}})', 0),
            ('(function(){var n=0;return {get sequence(){if(++n!==1)throw new Error("twice");return 51;},radioGeneration:9};})()', 1),
        ]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'capture', expression, str(expected)])
        for mode in ('finish', 'error', 'timeout'):
            run([str(self.binary), mode, '', '1'])

    def test_twt_generation_requires_both_consents_and_has_no_sequence(self):
        for expression, expected in [
            ('({radioGeneration:9,closeAll:true,allowDisconnect:true})', 1),
            ('({radioGeneration:9,closeAll:true,allowDisconnect:false})', 0),
            ('({radioGeneration:9,closeAll:true})', 0),
            ('({radioGeneration:9,allowDisconnect:true})', 0),
            ('({radioGeneration:9,closeAll:false,allowDisconnect:true})', 0),
            ('({radioGeneration:9,closeAll:1,allowDisconnect:true})', 0),
            ('({sequence:51,radioGeneration:9,closeAll:true,allowDisconnect:true})', 0),
            ('({radioGeneration:0,closeAll:true,allowDisconnect:true})', 0),
            ('({radioGeneration:9,closeAll:true,allowDisconnect:true,timeoutMs:60001})', 0),
            ('({get closeAll(){gc();return true;},get radioGeneration(){gc();return 9;},allowDisconnect:true})', 1),
        ]:
            with self.subTest(expression=expression):
                run([str(self.binary), 'capture', expression, str(expected), 'twt'])
        for mode in ('finish', 'error', 'timeout'):
            run([str(self.binary), mode, '', '1', 'twt'])


BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_recovery_capture_gc/boundaries.inc')

MAIN = fixture_text('wifi/lifecycle/test_wifi_recovery_capture_gc/main.inc')
