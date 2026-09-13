"""Deferred production Radio interval/ESP-NOW adapters; SDK and state storage injected."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def integration_code():
    code = control_code('esp32c5/representative', False)
    code = code.replace('wifi_mode_t effective_mode;', 'wifi_mode_t effective_mode;bool stop_required;')
    code = code.replace('client;} wifi_radio_live_lease_t;', 'client;bool fixed_channel,channel_conflict;} wifi_radio_live_lease_t;')
    code += '\n#define CONFIG_ESP32_MQUICKJS_WIFI_RADIO 1\n#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_interval.h')
    code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_interval.c')
    code += BOUNDARIES
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    for name in ('wifi_radio_interval_writer', 'wifi_radio_interval_owner',
                 'esp32_mquickjs_wifi_radio_interval_configure', 'esp32_mquickjs_wifi_radio_interval_release',
                 'esp32_mquickjs_wifi_radio_interval_status', 'esp32_mquickjs_wifi_radio_write_interval'):
        code += extract(radio, name)
    wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
    code += extract(wifi, 'esp32_mquickjs_wifi_apply_interval')
    now = (COMPONENT / 'src/modules/espnow/esp32_mquickjs_espnow.c').read_text()
    for name in ('espnow_restore_power_save', 'espnow_apply_power_save'):
        code += extract(now, name)
    return code


class WiFiIntervalIntegration(unittest.TestCase):
    def test_real_admission_exact_tokens_partial_write_and_restore_without_native_module(self):
        compile_run(self, integration_code() + MAIN)


BOUNDARIES = fixture_text('wifi/config/test_wifi_interval_integration/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_interval_integration/main.inc')
