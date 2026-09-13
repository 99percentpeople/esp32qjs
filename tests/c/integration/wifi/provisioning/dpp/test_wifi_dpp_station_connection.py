"""Deferred DPP admission through the production shared Station connection slot.

Exercises actual Wi-Fi connection/event/disconnect helpers. SDK submit, timer,
Radio loan and event-loop post are injected boundaries. Run only in the Wi-Fi
validation wave; these cases do not establish SDK/RTOS or RF qualification.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.provisioning.smartconfig.test_wifi_smartconfig_connection import connection_code, WIFI
from tests.support.native_compile import compile_run


class DppStationConnection(unittest.TestCase):
    def test_capture_identity_link_snapshot_and_disconnect_retirement(self):
        header = WIFI.parents[3] / 'internal/esp32_mquickjs_wifi_dpp_station.h'
        status = re.search(r'typedef struct \{[^}]*\} esp32_mquickjs_wifi_dpp_connection_status_t;',
                           header.read_text()).group(0)
        code = connection_code(
            '#define CONFIG_ESP_WIFI_DPP_SUPPORT 1\n#define ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP 4\n',
            status + BOUNDARIES,
            ('esp32_mquickjs_wifi_dpp_connect_begin', 'esp32_mquickjs_wifi_dpp_connect_status',
             'esp32_mquickjs_wifi_dpp_connect_end'))
        timer = 'static int esp32_mquickjs_wifi_prepare_connect_timer(void) { return 0; }'
        assert timer in code
        code = code.replace(timer, 'static int timer_error;\n' + timer.replace('return 0;', 'return timer_error;'))
        compile_run(self, code + MAIN)


BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_station_connection/boundaries.inc')

MAIN = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_station_connection/main.inc')
