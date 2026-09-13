"""Deferred actual HAL adapters with native allocator/storage/error boundaries.

Fixed C5 target compilation checks ROM/ABI; this host unit checks wrapper error
and ownership behavior, not native counter layouts or Wi-Fi task scheduling.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiHeStatisticsSdk(unittest.TestCase):
    def test_rx_pair_errors_repeat_enable_and_tx_partial_allocations(self):
        source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_he_statistics_sdk.c').read_text()
        code = BOUNDARIES + extract(source, '__wrap_hal_enable_rx_statistics')
        code += extract(source, '__wrap_hal_enable_tx_statistics')
        compile_run(self, code + MAIN)

    def test_actual_snapshot_dispatch_retirement_and_discard(self):
        source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_he_statistics_sdk.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_he_statistics.h').read_text()
        code = BOUNDARIES + extract(source, '__wrap_hal_enable_rx_statistics')
        code += extract(source, '__wrap_hal_enable_tx_statistics')
        code += structure(header, 'esp32_mquickjs_wifi_he_statistics_t')
        code += structure(source, 'he_statistics_command_t')
        code += fixture_text('wifi/config/test_wifi_he_statistics_sdk/test_actual_snapshot_dispatch_retirement_and_discard.inc')
        code += extract(source, 'he_statistics_snapshot_dispatch')
        code += extract(source, 'esp32_mquickjs_wifi_he_statistics_snapshot')
        compile_run(self, code + fixture_text('wifi/config/test_wifi_he_statistics_sdk/test_actual_snapshot_dispatch_retirement_and_discard-02.inc'))


BOUNDARIES = fixture_text('wifi/config/test_wifi_he_statistics_sdk/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_he_statistics_sdk/main.inc')
