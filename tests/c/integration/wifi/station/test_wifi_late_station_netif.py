"""Late Station attachment executes the production initialization suffix."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import unittest
from tests.support.wireless_vm_fixture import extract
from tests.support.native_compile import compile_run

ROOT=TEST_ROOT
SOURCE=ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c'

class WiFiLateStationNetif(unittest.TestCase):
    def test_start_in_progress_is_reconciled_after_the_native_fence(self):
        source=SOURCE.read_text()
        code=BOUNDARY+extract(source,'wifi_start_existing_station_netif')
        code+=extract(source,'esp32_mquickjs_wifi_ensure_started')
        compile_run(self,code+AFTER_FENCE)

    def test_started_radio_initializes_stack_before_publishing_station(self):
        source=SOURCE.read_text()
        init=extract(source,'wifi_init_helper')
        start=init.index('    if (esp32_mquickjs_wifi_radio_get_status(&radio_status)')
        stop=init.index('    s_wifi_setup_stage = NULL;',start)
        code=BOUNDARY
        if 'static esp_err_t wifi_start_existing_station_netif(' in source:
            code+=extract(source,'wifi_start_existing_station_netif')
        # Exact production suffix, including the shared fail label. SDK/RTOS
        # calls alone are injected; no substitute initialization state machine.
        code+='static int initialize_suffix(void) { esp32_mquickjs_wifi_radio_status_t radio_status;int err=0;\n'+init[start:stop]+'\nreturn 0; fail:return err;}\n'
        compile_run(self,code+MAIN)

BOUNDARY=fixture_text('wifi/station/test_wifi_late_station_netif/boundary.inc')
AFTER_FENCE=fixture_text('wifi/station/test_wifi_late_station_netif/after_fence.inc')
MAIN=fixture_text('wifi/station/test_wifi_late_station_netif/main.inc')
