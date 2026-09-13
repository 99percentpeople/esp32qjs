"""Deferred managed broadcast submission through production dispatch and timer.

The SDK public call/native builder are injected boundaries, not an alternative
state machine. No fixture import/compile/run before the Wi-Fi validation stage.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import unittest
from tests.c.integration.wifi.twt.test_wifi_twt_setup_timer import PRELUDE
from tests.c.integration.wifi.twt.test_wifi_twt_broadcast_event import broadcast_types, BOUNDARIES
from tests.c.integration.wifi.twt.test_wifi_twt_broadcast_timer import MAIN as TIMER_MAIN
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiTwtBroadcastSubmit(unittest.TestCase):
    def test_exact_dispatch_retained_result_and_uncertain_return(self):
        twt = COMPONENT / 'src/modules/wifi_twt'
        sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        code = PRELUDE + broadcast_types() + BOUNDARIES
        code += structure(sdk, 'wifi_btwt_setup_config_t')
        code += fixture_text('wifi/twt/test_wifi_twt_broadcast_submit/test_exact_dispatch_retained_result_and_uncertain_return.inc')
        code += extract((twt / 'esp32_mquickjs_wifi_twt_options.c').read_text(), 'esp32_mquickjs_wifi_btwt_options_valid')
        for name in ('teardown_tx', 'tx', 'broadcast_event', 'broadcast_timer', 'broadcast_submit'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        for name in ('broadcast_event', 'broadcast_timer', 'broadcast_rx', 'broadcast_submit'):
            code += unit(twt / f'esp32_mquickjs_wifi_twt_{name}.c')
        code += extract((twt / 'esp32_mquickjs_wifi_twt_sdk.c').read_text(), '__wrap_wifi_sta_btwt_setup_process')
        code += TIMER_MAIN.split('int main(void) {')[0]
        compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/twt/test_wifi_twt_broadcast_submit/main.inc')
