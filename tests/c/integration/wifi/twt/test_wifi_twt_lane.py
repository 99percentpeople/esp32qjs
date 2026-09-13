"""Deferred production TWT ledger cases. No alternate lifecycle implementation.

SDK declarations come from the pinned local header; only scheduling and proof
delivery are controlled here. These cases do not establish an SDK drain probe.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import tempfile
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit, INTERNAL
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.wireless_vm_fixture import ROOT, CORE, build, run


class WiFiTwtLane(unittest.TestCase):
    def test_identity_early_events_close_and_retirement(self):
        header = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        extra = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n'
        extra += 'typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NO_MEM 0x101\n'
        extra += '#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n'
        for name in ('wifi_twt_setup_cmds_t', 'wifi_btwt_setup_status_t'):
            extra += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
        extra += structure(header, 'wifi_twt_setup_config_t')
        extra += '\ntypedef wifi_twt_setup_config_t wifi_itwt_setup_config_t;\n'
        for name in ('wifi_btwt_setup_config_t', 'wifi_event_sta_itwt_setup_t', 'wifi_event_sta_btwt_setup_t'):
            extra += structure(header, name)
        extra += unit(INTERNAL / 'esp32_mquickjs_wifi_twt_options.h')
        extra += unit(INTERNAL / 'esp32_mquickjs_wifi_twt_lane.h')
        extra += unit(CORE / 'esp32_mquickjs_options.c')
        for name in ('options', 'lane'):
            extra += unit(ROOT / f'components/esp32_mquickjs/src/modules/wifi_twt/esp32_mquickjs_wifi_twt_{name}.c')
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, extra, MAIN)
            run([str(binary)])


MAIN = fixture_text('wifi/twt/test_wifi_twt_lane/main.inc')
