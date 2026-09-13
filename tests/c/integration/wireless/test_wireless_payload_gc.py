"""Production wireless payload converters, native pools and real VM ownership."""
from tests.support.fixtures import fixture_text
import pathlib
import sys
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run

SDK=fixture_text('wireless/test_wireless_payload_gc/sdk.inc')
MAIN=fixture_text('wireless/test_wireless_payload_gc/main.inc')
class WirelessPayloadGc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        ble=(ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c').read_text()
        espnow=(ROOT/'components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c').read_text()
        extra=(CORE/'esp32_mquickjs_native_pool.c').read_text()+SDK
        extra+=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_format_address')
        extra+='\n'.join(extract(ble,n) for n in ['ble_address_type_name','ble_format_address','ble_address_to_js','ble_find_connection','ble_retire_handle','ble_new_connection_handle','ble_scan_pool_release','ble_scan_event_to_js','ble_notification_pool_release','ble_notification_event_to_js','ble_server_event_pool_release','ble_server_event_to_js','js_ble_connection_finalizer'])
        extra+='\n'.join(extract(espnow,n) for n in ['espnow_format_address','espnow_release_rx_slot','espnow_receive_event_to_js'])
        classes='static const JSClassDef connection_class=JS_CLASS_DEF("BLEConnection",0,js_byte_view_constructor,JS_CLASS_BLE_CONNECTION,NULL,NULL,NULL,js_ble_connection_finalizer);'
        cls.binary=build(cls.temp.name,extra,MAIN,classes,'JS_PROP_CLASS_DEF("BLEConnection", &connection_class),',
                         '#define JS_CLASS_BLE_CONNECTION (JS_CLASS_USER+34)\nvoid js_ble_connection_finalizer(JSContext *,void *);')

    def test_payload_copy_view_event_failure_and_gc(self):
        for mode in range(4):
            for gc in (0,1):
                with self.subTest(mode=mode,gc=gc):run([str(self.binary),str(mode),str(gc),'0'])

    def test_server_write_dequeued_after_connection_disappears(self):
        run([str(self.binary),'2','1','1'])
