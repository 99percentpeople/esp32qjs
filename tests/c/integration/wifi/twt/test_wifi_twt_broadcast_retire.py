"""Deferred production broadcast result + timer/native marker + retire executor.

Only scheduling, SDK queue boundaries and copied-event delivery are injected.
No import/compile/run until all Wi-Fi APIs reach the staged validation phase.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.twt.test_wifi_twt_setup_timer import PRELUDE
from tests.c.integration.wifi.twt.test_wifi_twt_broadcast_event import broadcast_types, BOUNDARIES
from tests.c.integration.wifi.twt.test_wifi_twt_broadcast_timer import MAIN as TIMER_MAIN
from tests.c.integration.wifi.twt.test_wifi_twt_setup_result import RADIO_BOUNDARIES
from tests.support.wireless_vm_fixture import extract
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run


class WiFiTwtBroadcastRetire(unittest.TestCase):
    def test_real_markers_exact_release_stale_events_and_cleanup_suffix(self):
        code = '#define TEST_REAL_BTWT_TEARDOWN_RETIRE 1\n' + PRELUDE + '\n#include <stdatomic.h>\n' + broadcast_types() + BOUNDARIES
        code += structure((COMPONENT / 'internal/esp32_mquickjs_wifi_twt_lane.h').read_text(), 'esp32_mquickjs_wifi_twt_token_t')
        for name in ('teardown_tx', 'tx', 'broadcast_event', 'broadcast_timer', 'fence', 'broadcast_retire'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        code += fixture_text('wifi/twt/test_wifi_twt_broadcast_retire/test_real_markers_exact_release_stale_events_and_cleanup_suffix.inc')
        for name in ('broadcast_event', 'teardown_tx', 'broadcast_timer', 'broadcast_rx', 'fence', 'broadcast_retire'):
            code += unit(COMPONENT / f'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_{name}.c')
        radio_bounds = RADIO_BOUNDARIES.replace(
            'static void esp32_mquickjs_wifi_btwt_setup_observe_fence(const void *event){(void)event;assert(0);}', '')
        code += radio_bounds + fixture_text('wifi/twt/test_wifi_twt_broadcast_retire/test_real_markers_exact_release_stale_events_and_cleanup_suffix-02.inc')
        code += extract((COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text(), 'wifi_radio_lifecycle_fence')
        code += TIMER_MAIN.split('int main(void) {')[0].replace(
            'esp32_mquickjs_wifi_btwt_setup_observe_fence(&saved_fence)',
            'wifi_radio_lifecycle_fence(NULL,ESP32QJS_WIFI_RADIO_CONTROL_EVENT,6,&saved_fence)').replace(
            'assert(!locked && us==12345)',
            'assert(!locked && us==(((struct timer_record *)h)->args.callback==twt_fence_callback?1:12345))')
        compile_run(self, code + TEARDOWN_BOUNDARIES + MAIN)


TEARDOWN_BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_broadcast_retire/teardown_boundaries.inc')


MAIN = fixture_text('wifi/twt/test_wifi_twt_broadcast_retire/main.inc')
