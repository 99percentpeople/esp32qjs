"""Deferred production watch capture/conversion with queue/lock/SDK boundaries."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, build, extract, run
from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, sdk_types
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit

WATCH = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_watch.c'

BOUNDARIES = fixture_text('wifi/config/test_wifi_watch_values/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_watch_values/main.inc')


def watch_code():
    source = WATCH.read_text()
    types = source[source.index('typedef enum {'):source.index('typedef struct {\n    esp32_mquickjs_runtime_t')]
    header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi.h').read_text()
    limits = '\n'.join(line for line in header.splitlines() if line.startswith('#define ESP32_MQUICKJS_WIFI_WATCH_')) + '\n'
    code = PRELUDE + limits + sdk_types('esp32c5/representative', extra=(
        'wifi_event_t', 'wifi_event_sta_scan_done_t', 'wifi_event_sta_connected_t',
        'wifi_event_sta_disconnected_t', 'wifi_event_sta_authmode_change_t',
        'wifi_event_ap_staconnected_t', 'wifi_event_ap_stadisconnected_t',
        'wifi_event_ap_probe_req_rx_t', 'wifi_event_bss_rssi_low_t',
        'wifi_event_home_channel_change_t', 'wifi_event_ftm_report_t',
        'wifi_event_action_tx_status_t', 'wifi_event_roc_done_t',
        'wifi_event_ap_wrong_password_t', 'wifi_event_sta_beacon_offset_unstable_t',
        'wifi_event_sta_itwt_setup_t', 'wifi_event_sta_btwt_setup_t', 'wifi_event_sta_itwt_teardown_t',
        'wifi_event_sta_btwt_teardown_t', 'wifi_event_sta_itwt_probe_t', 'wifi_event_sta_itwt_suspend_t',
        'wifi_event_sta_twt_wakeup_t', 'wifi_event_neighbor_report_t'))
    code += '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
    code += unit(ROOT / "components/esp32_mquickjs/internal/esp32_mquickjs_wifi_neighbor.h")
    code += unit(ROOT / "components/esp32_mquickjs/src/modules/wifi_common/esp32_mquickjs_wifi_neighbor.c")
    code += types + BOUNDARIES
    code += source[source.index('static portMUX_TYPE s_neighbor_mux'):source.index('static void wifi_watch_drop(void)')]
    config = WATCH.with_name('esp32_mquickjs_wifi_config.c').read_text()
    code += ''.join(extract(config, name) for name in (
        'esp32_mquickjs_wifi_ssid_text', 'esp32_mquickjs_wifi_set_ssid_properties'))
    code += ''.join(extract(source, name) for name in (
        'wifi_watch_drop', 'wifi_watch_descriptor', 'esp32_mquickjs_wifi_watch_capture', 'wifi_watch_poll',
        'wifi_watch_number', 'wifi_watch_twt_setup_properties', 'wifi_watch_twt_suspend_properties', 'wifi_watch_neighbor_properties', 'wifi_watch_value_to_js', 'wifi_watch_to_js', 'wifi_watch_closed'))
    return code


class WiFiWatchValues(unittest.TestCase):
    def test_native_copy_unsigned_fields_redaction_queue_pressure_and_vm_roots(self):
        with tempfile.TemporaryDirectory() as tmp:
            run([str(build(tmp, watch_code(), MAIN))])
