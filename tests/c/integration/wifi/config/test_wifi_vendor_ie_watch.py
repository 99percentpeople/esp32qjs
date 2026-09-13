"""Deferred production broker, capture/close and VM conversion checks.

SDK callbacks/locks and EventQueue send/retain are injected boundaries. This does
not qualify actual FreeRTOS scheduling, SDK unregister, EventQueue wake/reaper,
public constructor OOM, RF reception or the later shared W-09 budget.
Do not import/compile/run until the Wi-Fi stage validation is authorized.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import build, extract, run

DIRECTORY = COMPONENT / 'src/modules/wifi_vendor_ie'
HEADER = COMPONENT / 'internal/esp32_mquickjs_wifi_vendor_ie_watch.h'


def declarations():
    return (PRELUDE + sdk_types('esp32c5/representative', ('wifi_vendor_ie_type_t', 'vendor_ie_data_t'))
            + structure(HEADER.read_text(), 'esp32_mquickjs_wifi_vendor_ie_broker_status_t'))


class WiFiVendorIeWatch(unittest.TestCase):
    def test_broker_registration_uncertainty_drain_suffix_and_stale_generation(self):
        code = declarations() + BROKER_BOUNDARIES
        code += unit(DIRECTORY / 'esp32_mquickjs_wifi_vendor_ie_broker.c')
        compile_run(self, code + BROKER_MAIN)

    def test_capture_copies_filters_full_lock_busy_close_and_retired_charge(self):
        code = capture_code()
        compile_run(self, code + CAPTURE_MAIN)

    def test_converter_nth_allocation_and_moving_gc(self):
        source = (DIRECTORY / 'esp32_mquickjs_wifi_vendor_ie_watch.c').read_text()
        code = capture_code(vm=True)
        code += ''.join(extract(source, name) for name in (
            'vendor_watch_frame_name', 'vendor_watch_to_js', 'esp32_mquickjs_wifi_vendor_ie_watch_status',
            'vendor_watch_hex', 'vendor_watch_oui'))
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            run([str(binary)])


def capture_code(vm=False):
    source = (DIRECTORY / 'esp32_mquickjs_wifi_vendor_ie_watch.c').read_text()
    code = declarations() + CAPTURE_BOUNDARIES
    if not vm:
        code += '#define heap_caps_free free\n'
    code += source[source.index('#define VENDOR_WATCH_MAX_SOURCES'):source.index('static void vendor_watch_count')]
    code += CAPTURE_SEND
    for name in ('vendor_watch_count', 'vendor_watch_matches', 'esp32_mquickjs_wifi_vendor_ie_watch_capture',
                 'vendor_watch_closed', 'vendor_watch_destroyed', 'esp32_mquickjs_deinit_wifi_vendor_ie_watch_runtime'):
        code += extract(source, name)
    return code


PRELUDE = fixture_text('wifi/config/test_wifi_vendor_ie_watch/prelude.inc')

BROKER_BOUNDARIES = fixture_text('wifi/config/test_wifi_vendor_ie_watch/broker_boundaries.inc')

BROKER_MAIN = fixture_text('wifi/config/test_wifi_vendor_ie_watch/broker_main.inc')

CAPTURE_BOUNDARIES = fixture_text('wifi/config/test_wifi_vendor_ie_watch/capture_boundaries.inc')

CAPTURE_SEND = fixture_text('wifi/config/test_wifi_vendor_ie_watch/capture_send.inc')

CAPTURE_MAIN = fixture_text('wifi/config/test_wifi_vendor_ie_watch/capture_main.inc')

VM_MAIN = fixture_text('wifi/config/test_wifi_vendor_ie_watch/vm_main.inc')
