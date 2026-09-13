"""Production NAN SDK/observer cases. Deferred; no fixture execution on import."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tests.support.wireless_vm_fixture import extract

ROOT = TEST_ROOT
BASE = ROOT / 'components/esp32_mquickjs'
SDK = Path('/home/zach/esp/esp-idf/components/esp_wifi')


class NanNativeControl(unittest.TestCase):
    def sources(self):
        path = SDK / 'wifi_apps/nan_app/src/nan_app.c'
        if not path.exists():
            self.skipTest('reviewed SDK unavailable')
        spec = importlib.util.spec_from_file_location('nan_patch', ROOT / 'scripts/patch_idf_nan.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        original = path.read_text()
        return original, patch.patch_source(original)

    def compile_run(self, source, *, expect_failure=False):
        cc = shutil.which('cc')
        if cc is None:
            self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as folder:
            path, binary = Path(folder) / 'case.c', Path(folder) / 'case'
            path.write_text(source)
            result = subprocess.run([cc, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-function', '-Wno-unused-variable', '-I' + str(BASE / 'internal'),
                '-I' + str(ROOT / 'tests/c/support/radio_stubs'), str(path), '-o', str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            if expect_failure:
                self.assertNotEqual(result.returncode, 0, 'original path unexpectedly satisfied regression')
            else:
                self.assertEqual(result.returncode, 0, result.stderr)

    def observer(self):
        source = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk.inc').read_text()
        source = source.replace('#include "freertos/FreeRTOS.h"', '').replace('#include "freertos/portmacro.h"', '')
        # Queries depend on the SDK service database, absent from this observer
        # fixture. Their complete production include runs in test_wifi_nan_query.
        source = source.replace('#include "esp32_mquickjs_wifi_nan_query_sdk.inc"', '')
        # Load common bridge declarations without the SDK request prototypes;
        # each case supplies its own native request boundary below. Enable the
        # real service callback scopes in the implementation being exercised.
        headers = '\n'.join(line for line in source.splitlines()
                            if line.startswith('#include "esp32_mquickjs_'))
        return PORT_BOUNDARY + headers + '\n#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1\n' + source

    def test_actual_observer_retirement_race_stale_identity_and_exhaustion(self):
        self.compile_run(self.observer() + OBSERVER_MAIN)

    def test_actual_service_callback_remains_owned_after_notice_until_free_returns(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(self.observer() + SERVICE_CALLBACK_BOUNDARY +
                extract(source, 'nan_app_replied_cb') + SERVICE_CALLBACK_MAIN, expect_failure=before)

    def test_actual_native_tx_scope_survives_application_callback_return(self):
        _, prepared = self.sources()
        bridge = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk.inc').read_text()
        # The native boundary models the pinned archive's post-app peer write;
        # all admission/quiescence and both wrapper/app scopes are production.
        self.compile_run(self.observer() + SERVICE_CALLBACK_BOUNDARY +
            extract(prepared, 'nan_app_replied_cb') +
            '\nextern void __real_nan_sdf_txcb(void *buffer);\n' +
            extract(bridge, '__wrap_nan_sdf_txcb') + NATIVE_TX_SCOPE_MAIN)

    def test_actual_publish_and_subscribe_release_data_lock_during_driver_call(self):
        original, prepared = self.sources()
        for kind in ('publish', 'subscribe'):
            sdk_name = 'esp_wifi_nan_' + kind + '_service'
            checked = 'esp32_mquickjs_wifi_nan_sdk_' + kind
            for source, before in ((original, True), (prepared, False)):
                body = extract(source, sdk_name) if before else (
                    extract(source, 'esp32qjs_nan_' + kind + '_service') + extract(source, checked))
                main = SERVICE_START_MAIN.replace('CONFIG_TYPE', 'wifi_nan_' + kind + '_cfg_t')
                main = main.replace('CHECKED_CALL', checked)
                if before:
                    main = 'int main(void){wifi_nan_' + kind + '_cfg_t config={.service_name="test"};' + sdk_name + '(&config);return 0;}'
                self.compile_run(SERVICE_START_BOUNDARY + body + main, expect_failure=before)

    def test_actual_cancel_freezes_before_driver_and_preserves_failed_service(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            body = extract(source, 'esp_wifi_nan_cancel_service')
            if not before:
                body += extract(source, 'esp32_mquickjs_wifi_nan_sdk_cancel_service')
            main = 'int main(void){esp_wifi_nan_cancel_service(7);return 0;}' if before else SERVICE_CANCEL_MAIN
            self.compile_run(self.observer() + SERVICE_CANCEL_BOUNDARY + body + main, expect_failure=before)

    def test_actual_native_create_wrapper_binds_host_policy_before_observer(self):
        _, prepared = self.sources()
        self.compile_run(self.observer() + SERVICE_BIND_BOUNDARY +
            extract(prepared, '__wrap_nan_start_publish_service') +
            extract(prepared, '__wrap_nan_start_subscribe_service') + SERVICE_BIND_MAIN)

    def test_actual_message_submit_requires_exact_peer_and_preserves_native_error(self):
        _, prepared = self.sources()
        self.compile_run(MESSAGE_SUBMIT_BOUNDARY + extract(prepared, 'esp32_mquickjs_wifi_nan_sdk_send') + fixture_text('wifi/nan/test_idf_nan_control/test_actual_message_submit_requires_exact_peer_and_preserves_native_error.inc'))

    def test_actual_ndp_response_mac_failure_unlocks_once_and_preserves_error(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(RESPONSE_BOUNDARY + extract(source, 'esp_wifi_nan_datapath_resp') + r'''
int main(void){wifi_nan_datapath_resp_t request={.ndp_id=1};
    assert(esp_wifi_nan_datapath_resp(&request)==-72);assert(!depth&&!submits);}
''', expect_failure=before)

    def test_actual_termination_completes_before_event_allocation_failure(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(self.observer() + TERMINATION_BOUNDARY +
                extract(source, 'nan_app_ndp_terminated_cb') + TERMINATION_MAIN,
                expect_failure=before)

    def test_actual_start_timeout_preserves_context_and_bounds_wait(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            prepare = '' if before else extract(source, 'esp32_mquickjs_wifi_nan_sdk_sync_prepare')
            self.compile_run(self.observer() + START_BOUNDARY +
                prepare + extract(source, 'esp_wifi_nan_sync_start') + START_MAIN,
                expect_failure=before)

    def test_ram_only_start_never_loads_persisted_nan_identity(self):
        original, prepared = self.sources()
        boundary = START_BOUNDARY.replace('typedef struct{int channel;}wifi_nan_sync_config_t;',
            'typedef struct{int channel;bool use_nvs_for_caching,reset_current_nvs_creds,group_mgmt_prot;}wifi_nan_sync_config_t;')
        boundary = boundary.replace('static struct{unsigned state;void *nan_netif;}s_nan_ctx',
            'static struct{unsigned state;void *nan_netif;bool own_nik_valid,use_nvs_for_caching,group_mgmt_prot,nira_cached;'
            'unsigned num_peer_creds,nik_lifetime;uint8_t own_nik[16],peer_creds[64];}s_nan_ctx')
        boundary += fixture_text('wifi/nan/test_idf_nan_control/test_ram_only_start_never_loads_persisted_nan_identity.inc')
        main = fixture_text('wifi/nan/test_idf_nan_control/test_ram_only_start_never_loads_persisted_nan_identity-main.inc')
        for source, before in ((original, True), (prepared, False)):
            prepare = '' if before else extract(source, 'esp32_mquickjs_wifi_nan_sdk_sync_prepare')
            self.compile_run(self.observer() + boundary + prepare + extract(source, 'esp_wifi_nan_sync_start') + main,
                expect_failure=before)

    def test_actual_event_post_never_blocks_native_control(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(self.observer() + POST_BOUNDARY +
                extract(source, 'nan_app_post_event') + POST_MAIN,
                expect_failure=before)

    def test_actual_init_failure_and_revisit_preserve_allocated_native_objects(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(INIT_BOUNDARY + extract(source, 'esp_nan_app_deinit') +
                extract(source, 'esp_nan_app_init') + INIT_MAIN, expect_failure=before)

    def test_actual_handler_cleanup_preserves_error_and_retries_only_failed_unregister(self):
        original, prepared = self.sources()
        for source, before in ((original, True), (prepared, False)):
            self.compile_run(HANDLER_BOUNDARY + extract(source, 'nan_clear_app_default_handlers') + fixture_text('wifi/nan/test_idf_nan_control/test_actual_handler_cleanup_preserves_error_and_retries_only_failed_unregister.inc'), expect_failure=before)


MESSAGE_SUBMIT_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/message_submit_boundary.inc')

PORT_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/port_boundary.inc')
HANDLER_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/handler_boundary.inc')
OBSERVER_MAIN = fixture_text('wifi/nan/test_idf_nan_control/observer_main.inc')
RESPONSE_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/response_boundary.inc')
TERMINATION_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/termination_boundary.inc')
TERMINATION_MAIN = fixture_text('wifi/nan/test_idf_nan_control/termination_main.inc')
START_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/start_boundary.inc')
START_MAIN = fixture_text('wifi/nan/test_idf_nan_control/start_main.inc')
POST_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/post_boundary.inc')
POST_MAIN = fixture_text('wifi/nan/test_idf_nan_control/post_main.inc')
INIT_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/init_boundary.inc')
INIT_MAIN = fixture_text('wifi/nan/test_idf_nan_control/init_main.inc')

SERVICE_CALLBACK_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/service_callback_boundary.inc')
SERVICE_CALLBACK_MAIN = fixture_text('wifi/nan/test_idf_nan_control/service_callback_main.inc')

NATIVE_TX_SCOPE_MAIN = fixture_text('wifi/nan/test_idf_nan_control/native_tx_scope_main.inc')

SERVICE_START_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/service_start_boundary.inc')
SERVICE_START_MAIN = fixture_text('wifi/nan/test_idf_nan_control/service_start_main.inc')

SERVICE_CANCEL_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/service_cancel_boundary.inc')
SERVICE_CANCEL_MAIN = fixture_text('wifi/nan/test_idf_nan_control/service_cancel_main.inc')

SERVICE_BIND_BOUNDARY = fixture_text('wifi/nan/test_idf_nan_control/service_bind_boundary.inc')
SERVICE_BIND_MAIN = fixture_text('wifi/nan/test_idf_nan_control/service_bind_main.inc')
