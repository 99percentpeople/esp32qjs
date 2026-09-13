"""Deferred AP pre-start production helper ownership and rollback regressions.

The actual helper executes against SDK mode/allocator/return boundaries. This
does not replace target linkage or native ioctl/RF validation. Do not execute
until the consolidated Wi-Fi test phase.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import re
import unittest
from pathlib import Path
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract

BASE = TEST_ROOT / 'components/esp32_mquickjs'


def source():
    header = (BASE / 'internal/esp32_mquickjs_wifi_ap_prestart.h').read_text()
    code = (BASE / 'src/modules/wifi/esp32_mquickjs_wifi_ap_prestart.c').read_text()
    code = re.sub(r'^#include[^\n]*\n', '', code, flags=re.M)
    result = structure(header, 'esp32_mquickjs_wifi_ap_prestart_result_t')
    policy = extract((BASE / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text(),
                     'esp32_mquickjs_wifi_radio_pmf_disable_allowed')
    return PREFIX + sdk_types('esp32c5/representative') + result + BOUNDARIES + policy + code + SDK


class WiFiAPPrestart(unittest.TestCase):
    def test_capture_exact_identity_and_no_native_use_after_release(self):
        compile_run(self, source() + fixture_text('wifi/ap/test_wifi_ap_prestart/test_capture_exact_identity_and_no_native_use_after_release.inc'))

    def test_config_and_readback_precede_ap_start_and_keep_station_mode(self):
        compile_run(self, source() + fixture_text('wifi/ap/test_wifi_ap_prestart/test_config_and_readback_precede_ap_start_and_keep_station_mode.inc'))

    def test_native_failure_and_readback_mismatch_preserve_original_error_and_rollback(self):
        compile_run(self, source() + fixture_text('wifi/ap/test_wifi_ap_prestart/test_native_failure_and_readback_mismatch_preserve_original_error_and_rollback.inc'))

    def test_disabled_pmf_is_ap_only_before_allocation_and_failure_rolls_back(self):
        compile_run(self, source() + fixture_text('wifi/ap/test_wifi_ap_prestart/test_disabled_pmf_is_ap_only_before_allocation_and_failure_rolls_back.inc'))

    def test_existing_native_ap_or_factory_failure_never_installs_after_allocation(self):
        compile_run(self, source() + fixture_text('wifi/ap/test_wifi_ap_prestart/test_existing_native_ap_or_factory_failure_never_installs_after_allocation.inc'))

    def test_context_and_dispatch_failure_do_not_write_and_unarmed_mode_is_forwarded(self):
        compile_run(self, source() + fixture_text('wifi/ap/test_wifi_ap_prestart/test_context_and_dispatch_failure_do_not_write_and_unarmed_mode_is_forwarded.inc'))


PREFIX = fixture_text('wifi/ap/test_wifi_ap_prestart/prefix.inc')

BOUNDARIES = fixture_text('wifi/ap/test_wifi_ap_prestart/boundaries.inc')

SDK = fixture_text('wifi/ap/test_wifi_ap_prestart/sdk.inc')
