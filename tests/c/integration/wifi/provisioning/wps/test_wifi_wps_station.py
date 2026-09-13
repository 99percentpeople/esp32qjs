"""Deferred production WPS helper reservation and DHCP/IP event fence cases."""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.station.test_wifi_scan_lifecycle import SDK, WIFI, FUTURE, extract
from tests.support.native_compile import compile_run


class WPSStation(unittest.TestCase):
    def test_exact_reservation_blocks_futures_until_link_drain(self):
        source = WIFI.read_text()
        record = re.search(r'static struct \{[^}]*\} s_wifi_smartconfig_connect;', source).group(0)
        globals_ = record + '\nstatic uint32_t s_wifi_wps_capture_identity, s_wifi_wps_next_identity=1;\n'
        code = '#define CONFIG_ESP_WIFI_DPP_SUPPORT 1\n#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1\n#define CONFIG_LWIP_IPV4 1\n' + SDK
        marker = 'static _Atomic(esp32_mquickjs_runtime_t *) s_wifi_runtime'
        code = code.replace(marker, globals_ + marker, 1)
        code += BOUNDARIES
        for name in ('wifi_helpers_idle', 'wifi_driver_helpers_ready', 'wifi_begin_disconnect_locked',
                     'wifi_finish_disconnect_locked', 'wifi_post_disconnect_fence', 'wifi_request_disconnect',
                     'esp32_mquickjs_wifi_cancel_connect', 'wifi_process_driver_event'):
            code += extract(source, name)
        code += (WIFI.parent / 'esp32_mquickjs_wifi_wps_station.inc').read_text()
        code += extract(FUTURE.read_text(), 'wifi_future_start')
        compile_run(self, code + CASES)


BOUNDARIES = fixture_text('wifi/provisioning/wps/test_wifi_wps_station/boundaries.inc')

CASES = fixture_text('wifi/provisioning/wps/test_wifi_wps_station/cases.inc')
