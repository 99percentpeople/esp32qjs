"""Deferred production USD transport: native completion and physical retirement.

The fixture compiles the actual transport include with controlled driver,
recycler and eloop boundaries during the Wi-Fi validation wave only.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.nan.test_idf_nan_control import BASE, NanNativeControl


def clean(path):
    return re.sub(r'^#(?:include[^\n]*|pragma once)\n', '', path.read_text(), flags=re.M)


class NanUsdTransport(unittest.TestCase):
    compile_run = NanNativeControl.compile_run

    def production(self):
        header = BASE / 'internal'
        return (PREFIX + clean(header / 'esp32_mquickjs_wifi_nan_tx.h') +
                clean(header / 'esp32_mquickjs_wifi_offchan_frame.h') +
                clean(header / 'esp32_mquickjs_wifi_nan_usd_sdk.h') + BOUNDARIES +
                clean(BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_usd_transport.inc') + HELPERS)

    def test_early_result_retained_until_physical_recycler_and_exact_release(self):
        self.compile_run(self.production() + fixture_text('wifi/nan/test_wifi_nan_usd_transport/test_early_result_retained_until_physical_recycler_and_exact_release.inc'))

    def test_pending_copy_is_not_submitted_until_old_buffer_returns(self):
        self.compile_run(self.production() + fixture_text('wifi/nan/test_wifi_nan_usd_transport/test_pending_copy_is_not_submitted_until_old_buffer_returns.inc'))

    def test_subscriber_list_rotates_only_between_native_operations(self):
        import importlib.util
        from pathlib import Path
        from tests.support.wireless_vm_fixture import extract
        sdk = Path('/home/zach/esp/esp-idf/components/wpa_supplicant/src/common/nan_de.c')
        if not sdk.exists():
            self.skipTest('reviewed SDK unavailable')
        script = BASE.parents[1] / 'scripts/sdk_patches/wpa/nan_usd.py'
        spec = importlib.util.spec_from_file_location('usd_patch', script)
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        source = patch.patch_timer(sdk.read_text())
        helper = extract(source, 'esp32qjs_nan_usd_subscribe_channel')
        self.compile_run(fixture_text('wifi/nan/test_wifi_nan_usd_transport/test_subscriber_list_rotates_only_between_native_operations-02.inc') + helper + fixture_text('wifi/nan/test_wifi_nan_usd_transport/test_subscriber_list_rotates_only_between_native_operations.inc'))

    def test_timer_allocation_fails_before_submit_and_owner_can_be_retired(self):
        self.compile_run(self.production() + fixture_text('wifi/nan/test_wifi_nan_usd_transport/test_timer_allocation_fails_before_submit_and_owner_can_be_retired.inc'))


PREFIX = fixture_text('wifi/nan/test_wifi_nan_usd_transport/prefix.inc')

BOUNDARIES = fixture_text('wifi/nan/test_wifi_nan_usd_transport/boundaries.inc')

HELPERS = fixture_text('wifi/nan/test_wifi_nan_usd_transport/helpers.inc')
