"""Deferred production native cancellation wrappers with injected SDK record.

Host pointer width differs: this checks wrapper decisions and no delegation on
mismatch. The pinned layout assertions and linker wrapping need target evidence;
SDK task serialization and RF cancellation require the later device stage.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.tx.test_wifi_action_lane import PRELUDE
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiActionSdk(unittest.TestCase):
    def test_nan_release_runs_in_native_dispatch_and_returns_exact_snapshot(self):
        source = (COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action_sdk.c').read_text()
        ndp = (COMPONENT / 'internal/esp32_mquickjs_wifi_nan_ndp.h').read_text()
        code = PRELUDE + '#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1\n'
        code += sdk_types('esp32c5/representative', ('wifi_action_tx_req_t', 'wifi_roc_req_t'))
        code += ndp[ndp.index('typedef struct {'):ndp.index('} esp32_mquickjs_wifi_nan_ndp_status_t;') +
                    len('} esp32_mquickjs_wifi_nan_ndp_status_t;')] + '\n'
        start = source.index('#define ACTION_SDK_NAN_RELEASE_TYPE')
        code += source[start:source.index('\n#endif', start)] + BOUNDARIES + NAN_BOUNDARY
        for name in ('esp32_mquickjs_wifi_action_receive', 'action_sdk_owner', 'action_sdk_guarded_callback',
                     '__wrap_wifi_action_tx_process', 'esp32_mquickjs_wifi_nan_sdk_ndp_release'):
            code += extract(source, name)
        compile_run(self, code + NAN_MAIN)

    def test_cancel_checks_native_owner_in_ioctl_context(self):
        for target in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(target=target):
                code = PRELUDE + sdk_types(target, ('wifi_action_tx_req_t', 'wifi_roc_req_t')) + BOUNDARIES
                source = (COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action_sdk.c').read_text()
                for name in ('esp32_mquickjs_wifi_action_receive', 'action_sdk_owner', 'action_sdk_guarded_callback',
                             '__wrap_wifi_action_tx_process', '__wrap_wifi_roc_process'):
                    code += extract(source, name)
                compile_run(self, code + MAIN)


BOUNDARIES = fixture_text('wifi/tx/test_wifi_action_sdk/boundaries.inc')
NAN_BOUNDARY = fixture_text('wifi/tx/test_wifi_action_sdk/nan_boundary.inc')
NAN_MAIN = fixture_text('wifi/tx/test_wifi_action_sdk/nan_main.inc')
MAIN = fixture_text('wifi/tx/test_wifi_action_sdk/main.inc')
