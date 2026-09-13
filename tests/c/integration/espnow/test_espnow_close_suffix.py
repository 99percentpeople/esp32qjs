"""Deferred production close suffix and worker/control ownership regression."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class EspnowCloseSuffix(unittest.TestCase):
    def test_failed_suffix_retains_native_queue_and_public_handle_until_retirement(self):
        source = (COMPONENT / 'src/modules/espnow/esp32_mquickjs_espnow.c').read_text()
        names = ('espnow_begin_close', 'espnow_close_failure', 'espnow_restore_power_save', 'espnow_finish_close',
                 'espnow_close_worker', 'espnow_schedule_background_close', 'espnow_close_native',
                 'espnow_control_finish', 'espnow_control_destroy', 'espnow_closed_and_quiescent',
                 'espnow_cleanup_failed_restore')
        compile_run(self, BOUNDARIES + '\n'.join(extract(source, n) for n in names) + MAIN)


BOUNDARIES = fixture_text('espnow/test_espnow_close_suffix/boundaries.inc')

MAIN = fixture_text('espnow/test_espnow_close_suffix/main.inc')
