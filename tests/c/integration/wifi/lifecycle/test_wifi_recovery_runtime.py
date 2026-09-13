"""Deferred production recovery stepper, AP handoff and shared restoration.

SDK/Radio physical phases and owner retirement are injected boundaries, with
actual Radio production coverage in test_wifi_action_recovery. No fixture
import, C compilation or execution until the Wi-Fi implementation phase ends.
"""
from tests.support.fixtures import fixture_text
import re
import unittest

from tests.c.integration.wifi.lifecycle.test_wifi_restart_runtime import runtime_code, MAIN as RESTART_MAIN
from tests.c.integration.wifi.config.test_wifi_configuration_cleanup import WIFI, AP
from tests.c.integration.wifi.config.test_wifi_config_controls import structure, HEADER
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def recovery_request_code(ftm=True, twt=False):
    header = HEADER.read_text()
    return (('#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#define CONFIG_IDF_TARGET_ESP32C5 1\n' if twt else '') +
            f'#define CONFIG_ESP_WIFI_FTM_ENABLE {int(ftm)}\n#define CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT {int(ftm)}\n' +
            re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_recovery_kind_t;', header).group(0) + '\n' +
            structure(header, 'esp32_mquickjs_wifi_recovery_request_t') + '\n')


def recovery_runtime_code(ap):
    source = WIFI.read_text()
    code = runtime_code(ap)
    # Mirror wifi_init_helper's existing rejection boundary, including the
    # diagnostic stage left by the recovery stepper after native retirement.
    code = code.replace('!s_wifi_state.runtime_cleanup_pending);',
                        '!s_wifi_state.runtime_cleanup_pending && !s_wifi_state.cleanup_stage && !s_wifi_state.cleanup_error);')
    code += recovery_request_code()
    code += structure((WIFI.parents[3] / 'internal/esp32_mquickjs_wifi.h').read_text(),
                      'esp32_mquickjs_wifi_recovery_t')
    code += re.search(r'enum \{\s*WIFI_RECOVERY_NEW,.*?\n\};', source, re.S).group(0)
    code += BOUNDARIES
    ap_source = AP.read_text()
    if not ap:
        ap_source = ap_source[ap_source.rindex('esp_err_t esp32_mquickjs_wifi_ap_begin_recovery('):]
    code += extract(ap_source, 'esp32_mquickjs_wifi_ap_begin_recovery')
    code += ''.join(extract(source, name) for name in (
        'esp32_mquickjs_wifi_recovery_dispose',
        'esp32_mquickjs_wifi_recovery_begin', 'esp32_mquickjs_wifi_recovery_step'))
    code += RESTART_MAIN[:RESTART_MAIN.index('int main(void)')]
    return code


class WiFiRecoveryRuntime(unittest.TestCase):
    def test_exact_admission_yield_to_native_owner_and_shared_restoration(self):
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, recovery_runtime_code(ap) + MAIN)

    def test_disposal_transfers_cleanup_and_reconstruction_failure_uses_normal_cleanup(self):
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, recovery_runtime_code(ap) + FAILURE_MAIN)


BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_recovery_runtime/boundaries.inc')

MAIN = fixture_text('wifi/lifecycle/test_wifi_recovery_runtime/main.inc')

FAILURE_MAIN = fixture_text('wifi/lifecycle/test_wifi_recovery_runtime/failure_main.inc')
