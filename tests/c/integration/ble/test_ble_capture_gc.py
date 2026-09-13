"""Production capture/rollback at SDK boundaries, real argument roots and GC."""
from tests.support.fixtures import fixture_text
import pathlib
import sys
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run
SDK=fixture_text('ble/test_ble_capture_gc/sdk.inc')
MAIN=fixture_text('ble/test_ble_capture_gc/main.inc')
class BleCaptureGc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        ble=(ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c').read_text()
        options=(CORE/'esp32_mquickjs_options.c').read_text().replace('#include "esp32_mquickjs_options.h"',(ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace('#include "esp32_mquickjs_types.h"',''))
        names=['ble_next_generation','ble_is_object','ble_to_u32','ble_validate_option_keys','ble_retain_owner','ble_ms_to_625us','ble_get_number','ble_get_bool','ble_release_event_queue','ble_parse_timeout_option','ble_future_state_storage_free','ble_future_state_release','ble_scan_capture','ble_connection_operation_capture','ble_gatt_read_handle_capture','ble_gatt_write_handle_capture','ble_exchange_mtu_capture','ble_scanner_close_capture','ble_clear_remote_discovery','ble_discover_capture']
        cls.binary=build(cls.temp.name,SDK+(CORE/'esp32_mquickjs_native_pool.c').read_text()+options+'\n'.join(extract(ble,n) for n in names),MAIN)

    def test_capture_nth_failure_and_root_release(self):
        for mode in range(7):
            for gc in (0,1):
                with self.subTest(mode=mode,gc=gc):run([str(self.binary),str(mode),str(gc),'0'])

    def test_invalid_options_do_not_leave_state_or_queue(self):
        for mode in (0,1,2,3,5,6):
            with self.subTest(mode=mode):run([str(self.binary),str(mode),'1','1'])
