"""Deferred tests of the actual SDK-owned WAPI lifecycle wrapper.

Native WAPI calls and the critical-section boundary are injected; no separate
lifecycle model replaces the production implementation.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import re
import unittest
from pathlib import Path
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract

BASE = TEST_ROOT / 'components/esp32_mquickjs'


def source():
    clean = lambda text: re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', text, flags=re.M)
    return PREFIX + clean((BASE / 'internal/esp32_mquickjs_wifi_wapi.h').read_text()) + \
        clean((BASE / 'src/modules/wifi/esp32_mquickjs_wifi_wapi_sdk.c').read_text()) + NATIVE


class WiFiWapiSdk(unittest.TestCase):
    def test_radio_preflight_never_uses_observation_as_mutation_authority(self):
        radio = (BASE / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        code = source() + RADIO_BOUNDARY + extract(radio, 'esp32_mquickjs_wifi_radio_wapi_prepare')
        code += extract(radio, 'esp32_mquickjs_wifi_radio_wapi_select')
        compile_run(self, code + fixture_text('wifi/security/test_wifi_wapi_sdk/test_radio_preflight_never_uses_observation_as_mutation_authority.inc'))

    def test_disabled_init_skip_duplicate_guard_and_physical_generation(self):
        compile_run(self, source() + fixture_text('wifi/security/test_wifi_wapi_sdk/test_disabled_init_skip_duplicate_guard_and_physical_generation.inc'))

    def test_reviewed_partial_init_errors_do_not_call_deinit_on_unpublished_state(self):
        compile_run(self, source() + fixture_text('wifi/security/test_wifi_wapi_sdk/test_reviewed_partial_init_errors_do_not_call_deinit_on_unpublished_state.inc'))

    def test_uncertain_deinit_retains_raw_error_and_never_replays_native_free(self):
        compile_run(self, source() + fixture_text('wifi/security/test_wifi_wapi_sdk/test_uncertain_deinit_retains_raw_error_and_never_replays_native_free.inc'))

    def test_busy_reentrancy_and_identity_exhaustion_do_not_reuse_native_state(self):
        compile_run(self, source() + fixture_text('wifi/security/test_wifi_wapi_sdk/test_busy_reentrancy_and_identity_exhaustion_do_not_reuse_native_state.inc'))


PREFIX = fixture_text('wifi/security/test_wifi_wapi_sdk/prefix.inc')

NATIVE = fixture_text('wifi/security/test_wifi_wapi_sdk/native.inc')

RADIO_BOUNDARY = fixture_text('wifi/security/test_wifi_wapi_sdk/radio_boundary.inc')
