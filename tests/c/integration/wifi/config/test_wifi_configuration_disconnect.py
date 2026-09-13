"""Production configuration disconnect wait and native epoch/fence event path.

Native event delivery is scheduled through the production event processor.
No independent operation state machine; runtime/SDK/RTOS calls are boundaries.
Execution deferred to the combined Wi-Fi phase.
"""
from tests.support.fixtures import fixture_text
import unittest
import tests.c.integration.wifi.station.test_wifi_scan_lifecycle as base
from tests.support.native_compile import compile_run

BOUNDARIES = fixture_text('wifi/config/test_wifi_configuration_disconnect/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_configuration_disconnect/main.inc')


class WiFiConfigurationDisconnect(unittest.TestCase):
    def test_native_timeout_late_events_saturation_and_explicit_retry(self):
        source = base.WIFI.read_text()
        names = ['wifi_release_radio_operation', 'esp32_mquickjs_wifi_drain_scan',
                 'wifi_begin_disconnect_locked', 'wifi_finish_disconnect_locked',
                 'wifi_post_disconnect_fence', 'wifi_request_disconnect', 'wifi_process_driver_event',
                 'wifi_finish_configuration_disconnect']
        production = ''.join(base.extract(source, name) for name in names)
        compile_run(self, base.SDK + BOUNDARIES + production + MAIN)
