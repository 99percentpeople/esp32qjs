"""Deferred production Monitor diagnostics converters with real moving MQuickJS.

Only native snapshot reads and receiver lookup are injected. No replacement
capture/queue state machine; lifecycle/concurrency coverage belongs to the
existing production Session/queue fixtures. Do not execute before Wi-Fi staging.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, INTERNAL, build, extract, run

MONITOR = ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor.c'


def production_diagnostics(target):
    source = MONITOR.read_text()
    body = '#define CONFIG_IDF_TARGET "' + target + '"\n'
    filter_header = (INTERNAL / 'esp32_mquickjs_wifi_rx_filter.h').read_text()
    body += re.search(r'typedef enum \{[^}]+\} esp32_mquickjs_wifi_rx_filter_result_t;', filter_header).group(0)
    for header, names in [
        ('esp32_mquickjs_wifi_rx_filter.h', ['ESP32_MQUICKJS_WIFI_RX_FILTER_MAC_CAPACITY']),
        ('esp32_mquickjs_native_pool.h', ['ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY']),
        ('esp32_mquickjs_wifi_monitor_resources.h', ['ESP32_MQUICKJS_WIFI_MONITOR_MAX_SNAP_LENGTH', 'ESP32_MQUICKJS_WIFI_MONITOR_FILTER_COUNTERS']),
        ('esp32_mquickjs_wifi_monitor_session.h', ['ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS']),
        ('esp32_mquickjs_wifi_monitor_options.h', ['ESP32_MQUICKJS_WIFI_MONITOR_MAX_BATCH_FRAMES'])]:
        text = (INTERNAL / header).read_text()
        for name in names:
            body += '\n' + re.search(r'^#define ' + name + r' .+$', text, re.M).group(0) + '\n'
    resources = (INTERNAL / 'esp32_mquickjs_wifi_monitor_resources.h').read_text()
    queue = (INTERNAL / 'esp32_mquickjs_event_queue.h').read_text()
    for text, name in [(resources, 'esp32_mquickjs_wifi_monitor_counters_t'),
                       (resources, 'esp32_mquickjs_wifi_monitor_snapshot_t'),
                       (queue, 'esp32_mquickjs_event_queue_stats_t')]:
        body += re.search(r'typedef struct \{[^}]+\} ' + name + ';', text).group(0) + '\n'
    body += source[source.index('#define SET('):source.index('static monitor_session_t *monitor_session_from_this')]
    body += BOUNDARIES
    body += ''.join(extract(source, name) for name in [
        'monitor_queue_status', 'js_wifi_monitor_session_stats', 'js_wifi_monitor_capabilities'])
    return body


class WiFiMonitorDiagnostics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binaries = [build(cls.temp.name + '/' + target, production_diagnostics(target), MAIN)
                        for target in ['esp32c3', 'esp32s3', 'esp32c5']]

    def test_capabilities_named_counters_queue_and_every_allocation_failure(self):
        for binary in self.binaries:
            for scenario in range(5):
                with self.subTest(binary=binary, scenario=scenario):
                    run([str(binary), str(scenario)])


BOUNDARIES = fixture_text('wifi/monitor/test_wifi_monitor_diagnostics/boundaries.inc')

MAIN = fixture_text('wifi/monitor/test_wifi_monitor_diagnostics/main.inc')
