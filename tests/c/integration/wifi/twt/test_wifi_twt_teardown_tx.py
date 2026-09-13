"""Deferred production teardown scope + shared TX ledger under SDK scheduling.

Only native producer/callback/PM calls and allocation/locks are injected. Scope
release here consumes assumed external ordering; no full timer/event/RF proof.
Do not import, compile or run before the Wi-Fi API validation stage.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.twt.test_wifi_twt_tx import PRELUDE, MAIN as TX_MAIN
from tests.c.integration.wifi.twt.test_wifi_twt_broadcast_event import broadcast_types
from tests.support.native_compile import compile_run


class WiFiTwtTeardownTx(unittest.TestCase):
    def test_identity_pm_early_callback_recycle_reuse_and_faults(self):
        code = '#define TEST_REAL_TEARDOWN_TX 1\n' + PRELUDE + '\n#define ESP_ERR_NOT_FINISHED 0x10c\n'
        code += '\ntypedef int wifi_twt_setup_cmds_t;\n#define TWT_REJECT 7\n#define TWT_ACCEPT 4\n'
        code += broadcast_types(commands=False)
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_broadcast_event.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast_event.c')
        code += fixture_text('wifi/twt/test_wifi_twt_teardown_tx/test_identity_pm_early_callback_recycle_reuse_and_faults.inc')
        for name in ('probe_result', 'tx', 'teardown_tx'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_result.c')
        td = unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_teardown_tx.c')
        # Host metadata pointer at 56 overlaps C5 byte 62. Only this byte moves;
        # actual C5 EB layout and PM call-site relocations have target evidence.
        code += td.replace('((const uint8_t *)buffer)[62]', '((const uint8_t *)buffer)[65]')
        tx = unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_tx.c')
        code += tx.replace('((const uint8_t *)buffer)[61]', '((const uint8_t *)buffer)[64]')
        code += '\nstatic void (*td_output_hook)(void *);\n'
        boundary = TX_MAIN.split('int main(void) {')[0]
        boundary = boundary.replace('    if(early_setup_callback)', '    if(td_output_hook)td_output_hook(buffer);\n    if(early_setup_callback)', 1)
        compile_run(self, code + boundary + MAIN)


MAIN = fixture_text('wifi/twt/test_wifi_twt_teardown_tx/main.inc')
