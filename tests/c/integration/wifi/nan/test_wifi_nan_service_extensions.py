"""Deferred NAN credential/vendor production ownership and native crypto failures."""
from tests.support.fixtures import fixture_text
import importlib.util
import unittest

from tests.c.integration.wifi.nan.test_idf_nan_control import NanNativeControl, ROOT, SDK
from tests.c.integration.wifi.nan.test_wifi_nan_session import NanSessionLifecycle, PREFIX
from tests.support.wireless_vm_fixture import extract


class NanServiceExtensions(unittest.TestCase):
    compile_case = NanSessionLifecycle.compile_case

    def test_security_and_vendor_copies_survive_input_change_and_wipe_on_close(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_service_extensions/test_security_and_vendor_copies_survive_input_change_and_wipe_on_close.inc'), security=True)

    def test_invalid_security_and_oom_do_not_reserve_an_owner(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_service_extensions/test_invalid_security_and_oom_do_not_reserve_an_owner.inc'), security=True)

    def test_security_disabled_and_vendor_send_capture(self):
        self.compile_case(HELPER + fixture_text('wifi/nan/test_wifi_nan_service_extensions/test_security_disabled_and_vendor_send_capture.inc'))


HELPER = fixture_text('wifi/nan/test_wifi_nan_service_extensions/helper.inc')


class NanSecurityDerivation(unittest.TestCase):
    compile_run = NanNativeControl.compile_run

    def test_group_key_install_returns_original_error_without_submitting_the_suffix(self):
        source = SDK / 'wifi_apps/nan_app/src/nan_security.c'
        if not source.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('nan_security_patch', ROOT / 'scripts/patch_idf_nan.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        prepared = patch.patch_security_source(source.read_text())
        self.compile_run(PREFIX + GROUP_BOUNDARY +
            extract(prepared, 'esp32_mquickjs_wifi_nan_sdk_security_install_group_keys') + fixture_text('wifi/nan/test_wifi_nan_service_extensions/test_group_key_install_returns_original_error_without_submitting_the_suffix.inc'))

    def test_unresolved_pmkid_cannot_leave_a_usable_encrypted_context(self):
        source = SDK / 'wifi_apps/nan_app/src/nan_security.c'
        if not source.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('nan_security_patch', ROOT / 'scripts/patch_idf_nan.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        original = source.read_text()
        for prepared in (False, True):
            code = patch.patch_security_source(original) if prepared else original
            for mode in (0, 1, 2):
                self.compile_run(PREFIX + CRYPTO_BOUNDARY + PENDING_BOUNDARY +
                    extract(code, 'nan_security_apply_pending') + PENDING_MAIN.replace('MATCH_MODE', str(mode)),
                    expect_failure=not prepared and mode != 2)

    def test_pending_security_material_clear_wipes_entire_records(self):
        source = SDK / 'wifi_apps/nan_app/src/nan_security.c'
        if not source.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('nan_security_patch', ROOT / 'scripts/patch_idf_nan.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        prepared = patch.patch_security_source(source.read_text())
        body = extract(prepared, 'esp32_mquickjs_wifi_nan_sdk_security_clear_pending')
        self.compile_run(PREFIX + CRYPTO_BOUNDARY + r'''
static struct{bool valid;uint8_t material[80];}s_pending_m1,s_pending_scia;
''' + body + fixture_text('wifi/nan/test_wifi_nan_service_extensions/test_pending_security_material_clear_wipes_entire_records.inc'))

    def test_original_and_prepared_mac_hmac_failures_and_partial_material_cleanup(self):
        source = SDK / 'wifi_apps/nan_app/src/nan_security.c'
        if not source.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('nan_security_patch', ROOT / 'scripts/patch_idf_nan.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        original = source.read_text()
        for prepared in (False, True):
            code = patch.patch_security_source(original) if prepared else original
            for mode in (1, 2):
                with self.subTest(prepared=prepared, mode=mode):
                    self.compile_run(PREFIX + CRYPTO_BOUNDARY + extract(code, 'nan_derive_security_params') +
                        CRYPTO_MAIN.replace('FAILURE_MODE', str(mode)), expect_failure=not prepared)


CRYPTO_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_service_extensions/crypto_boundary.inc')
CRYPTO_MAIN = fixture_text('wifi/nan/test_wifi_nan_service_extensions/crypto_main.inc')

PENDING_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_service_extensions/pending_boundary.inc')
PENDING_MAIN = fixture_text('wifi/nan/test_wifi_nan_service_extensions/pending_main.inc')

GROUP_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_service_extensions/group_boundary.inc')
