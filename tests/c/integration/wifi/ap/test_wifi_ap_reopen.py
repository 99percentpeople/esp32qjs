"""Production shared AP helper and coordinator handoff; phase execution pending."""
from tests.support.fixtures import fixture_text
import unittest
import tests.c.integration.wifi.ap.test_wifi_ap_stop as fixture
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiAPReopen(unittest.TestCase):
    def code(self):
        code=fixture.WiFiAPStop().code()+BOUNDARIES
        code+=extract(fixture.fixture.AP.read_text(),'esp32_mquickjs_wifi_ap_reopen')
        code+=extract(fixture.fixture.WIFI.read_text(),'esp32_mquickjs_wifi_reopen_ap_shared')
        return code

    def test_setup_and_activation_failures_keep_the_same_token_for_stop_ap(self):
        compile_run(self,self.code()+fixture_text('wifi/ap/test_wifi_ap_reopen/test_setup_and_activation_failures_keep_the_same_token_for_stop_ap.inc'))

    def test_pending_station_operation_never_begins_shared_activation(self):
        compile_run(self,self.code()+fixture_text('wifi/ap/test_wifi_ap_reopen/test_pending_station_operation_never_begins_shared_activation.inc'))


BOUNDARIES = fixture_text('wifi/ap/test_wifi_ap_reopen/boundaries.inc')
