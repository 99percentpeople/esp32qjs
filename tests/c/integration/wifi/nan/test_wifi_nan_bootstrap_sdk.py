"""Deferred pinned SDK bootstrap parser and submit-only production entry cases."""
from tests.support.fixtures import fixture_text
import importlib.util
import tempfile
import unittest
from tests.c.integration.wifi.nan.test_idf_nan_control import NanNativeControl, ROOT, BASE, SDK
from tests.support.wireless_vm_fixture import extract


class NanBootstrapSDK(unittest.TestCase):
    compile_run = NanNativeControl.compile_run

    def test_request_status_is_preserved_before_observer_and_no_cookie_is_invented(self):
        if not SDK.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('bootstrap_patch', ROOT / 'scripts/patch_idf_nan_pairing.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        original = (SDK / 'wifi_apps/nan_app/src/nan_pairing.c').read_text()
        with tempfile.TemporaryDirectory() as directory:
            from pathlib import Path
            prepared = patch.prepare(SDK.parent, Path(directory))['nan_pairing.c']
        for source, before in ((original, True), (prepared, False)):
            code = BOUNDARY
            if before:
                code += 'static void nan_app_bootstrap_notify(const wifi_nan_bootstrap_event_t*e){last_status=e->status;++notices;}\n'
            else:
                code += extract(source, 'nan_app_bootstrap_notify')
            for name in ('nan_app_bootstrap_indication', 'nan_app_bootstrap_completed', 'nan_app_parse_npba_from_receive'):
                code += extract(source, name)
            self.compile_run(code + fixture_text('wifi/nan/test_wifi_nan_bootstrap_sdk/test_request_status_is_preserved_before_observer_and_no_cookie_is_invented.inc'), expect_failure=before)

    def test_exact_service_role_prevalidation_and_native_submission_error(self):
        code = BOUNDARY + SUBMIT_BOUNDARY
        code += (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_bootstrap_sdk.inc').read_text()
        self.compile_run(code + fixture_text('wifi/nan/test_wifi_nan_bootstrap_sdk/test_exact_service_role_prevalidation_and_native_submission_error.inc'))


BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_bootstrap_sdk/boundary.inc')

SUBMIT_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_bootstrap_sdk/submit_boundary.inc')
