"""Production scan Future result/timeout and AP conversion in the real VM."""
import unittest

from tests.support.fixtures import fixture_text
from tests.support.wireless_vm_fixture import ROOT, extract, run
from tests.c.integration.wireless.test_wireless_status_gc import WirelessStatusGc, SDK


class WiFiScanPartial(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_future.c').read_text()
        cls.sdk = SDK.replace('static int driver_failure;',
            'static int driver_failure;static uint16_t fixture_scan_ap_count=2;static bool stopped_or_complete;static unsigned ap_reads;')
        cls.sdk = cls.sdk.replace('*n=2;return driver_failure==1',
            'assert(stopped_or_complete);ap_reads++;*n=fixture_scan_ap_count;return driver_failure==1')
        cls.extra = fixture_text('wifi/station/test_wifi_scan_partial/boundaries.inc')
        for name in ('wifi_future_operation_name', 'wifi_future_poll', 'wifi_scan_future_result',
                     'wifi_future_finish', 'wifi_scan_future_on_timeout'):
            if name + '(' in source:
                cls.extra += extract(source, name)
        cls.extra += '\n#define HAS_SCAN_TIMEOUT ' + str(int('wifi_scan_future_on_timeout(' in source)) + '\n'
        cls.main = fixture_text('wifi/station/test_wifi_scan_partial/main.inc')
        WirelessStatusGc.setUpClass.__func__(cls)

    def test_normal_scan_has_explicit_completion_metadata(self):
        run([str(self.binary), '0'])

    def test_deadline_partial_empty_and_native_failures_with_gc(self):
        for scenario in range(1, 10):
            with self.subTest(scenario=scenario):
                run([str(self.binary), str(scenario)])
