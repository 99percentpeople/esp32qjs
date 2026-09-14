"""Production ESP-NOW callback, queue settlement and result conversion in real VM."""
import tempfile
import unittest
from tests.support.fixtures import fixture_text
from tests.support.wireless_vm_fixture import ROOT, INTERNAL, build, extract, run
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit

class EspNowCompletion(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT / 'components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c').read_text()
        code = unit(INTERNAL / 'esp32_mquickjs_native_status.h')
        code += fixture_text('espnow/test_espnow_completion/boundaries.inc')
        code += extract((ROOT / 'components/esp32_mquickjs/src/core/esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_format_address')
        for name in ('espnow_format_address', 'espnow_notify_tx_worker', 'espnow_send_callback', 'espnow_complete_queued_send', 'espnow_send_finish'):
            code += extract(source, name)
        cls.binary = build(cls.temp.name, code, fixture_text('espnow/test_espnow_completion/main.inc'))

    def test_callback_enum_queue_rejection_absence_and_result_gc(self):
        for code in (0, 1, 99, -1, -2):
            with self.subTest(code=code):
                run([str(self.binary), str(code)])
