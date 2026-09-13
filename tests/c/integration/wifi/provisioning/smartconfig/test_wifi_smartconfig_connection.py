"""Deferred production Station handoff, terminal storage and disconnect fences.

Uses the real SmartConfig connection hooks and existing Station event/disconnect
helpers. Radio admission, SDK submit and timer stop are injected boundaries;
this does not prove native SDK scheduling or RF behavior. Do not run until the
concentrated Wi-Fi validation phase.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.station.test_wifi_scan_lifecycle import SDK, WIFI, FUTURE, extract
from tests.support.native_compile import compile_run


def connection_code(defines='', boundaries='', functions=()):
    """Shared production connection/event paths; only SDK/Radio edges vary."""
    source = WIFI.read_text()
    record = re.search(r'static struct \{[^}]*\} s_wifi_smartconfig_connect;', source).group(0)
    code = defines + '#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1\n#define CONFIG_LWIP_IPV4 1\n' + SDK
    marker = 'static _Atomic(esp32_mquickjs_runtime_t *) s_wifi_runtime'
    code = code.replace(marker, record + '\nstatic uint32_t s_wifi_wps_capture_identity;\n' + marker, 1)
    code = re.sub(r'static void wifi_queue_connect_event\([^\n]*\n', '', code)
    code = code.replace('const esp32_mquickjs_wifi_scan_event_t *e) { (void)q;queued++;last_generation=e->generation;',
                        'const void *event) { (void)q;const esp32_mquickjs_wifi_scan_event_t *e=event;queued++;last_generation=e->generation;')
    code += BOUNDARIES + boundaries
    for name in ('wifi_queue_connect_event', 'wifi_helpers_idle', 'wifi_driver_helpers_ready',
                 'wifi_smartconfig_connection_exact_locked',
                 'wifi_begin_disconnect_locked', 'wifi_finish_disconnect_locked',
                 'wifi_post_disconnect_fence', 'wifi_request_disconnect',
                 'esp32_mquickjs_wifi_cancel_connect', 'wifi_process_driver_event',
                 'wifi_station_connect_submit', 'esp32_mquickjs_wifi_smartconfig_connect_begin',
                 'esp32_mquickjs_wifi_smartconfig_connect_status',
                 'esp32_mquickjs_wifi_smartconfig_connect_end') + functions:
        code += extract(source, name)
    return code + extract(FUTURE.read_text(), 'wifi_future_start')


class SmartConfigConnection(unittest.TestCase):
    def test_exact_generation_completion_and_cleanup_without_future_token(self):
        compile_run(self, connection_code() + MAIN)


BOUNDARIES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_connection/boundaries.inc')

MAIN = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_connection/main.inc')
