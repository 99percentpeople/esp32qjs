"""Production configure binding and error conversion with VM allocation/GC faults.

Capture/apply/status are explicit boundaries; their production implementations
have separate fixtures. This checks the public allocation/free contract and
per-call error metadata, not RF behavior or a duplicate native state machine.
"""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run
from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, sdk_types, structure

STATE = fixture_text('wifi/config/test_wifi_public_configure/state.inc')

BOUNDARIES = fixture_text('wifi/config/test_wifi_public_configure/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_public_configure/main.inc')

class WiFiPublicConfigure(unittest.TestCase):
    def test_public_allocation_release_and_current_error_records(self):
        radio_header=(ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_radio.h').read_text()
        wifi_header=(ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_wifi.h').read_text()
        wifi=(ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        future=(ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_future.c').read_text()
        structs='\n'.join(structure(radio_header,n) for n in ('esp32_mquickjs_wifi_radio_config_controls_t',
            'esp32_mquickjs_wifi_radio_start_controls_t','esp32_mquickjs_wifi_radio_config_result_t'))
        structs+='\n'.join(structure(wifi_header,n) for n in ('esp32_mquickjs_wifi_configuration_t',
            'esp32_mquickjs_wifi_configuration_execution_t'))
        body=PRELUDE+sdk_types('esp32c5/representative')+structs+STATE
        body+=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_secure_zero')
        body+=extract((CORE/'esp32_mquickjs.c').read_text(),'esp32_mquickjs_throw_native_error')
        body+=extract(wifi,'wifi_make_configuration_status')+extract(wifi,'esp32_mquickjs_wifi_throw_configuration_error')
        body+=extract(future,'wifi_station_unsupported')+BOUNDARIES+extract(wifi,'js_wifi_configure')
        with tempfile.TemporaryDirectory() as tmp:
            binary=build(tmp,body,MAIN)
            run([str(binary)])
