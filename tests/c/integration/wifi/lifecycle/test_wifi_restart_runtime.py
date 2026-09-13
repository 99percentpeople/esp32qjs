"""Deferred real restart executor and central cleanup; native boundaries injected.

This checks runtime sequencing, not SDK rebuild, event delivery or RF. The
Radio checkpoint/replay and netif retirement have separate production fixtures.
"""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.config.test_wifi_configuration_cleanup import WiFiConfigurationCleanup, WIFI, AP
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, extract


def runtime_code(ap, wapi=False):
    code = WiFiConfigurationCleanup().code()
    code = '#include <stdlib.h>\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT %d\n' % ap + code
    code = '#define CONFIG_ESP_WIFI_WAPI_PSK %d\n' % wapi + code
    code = code.replace('assert(cleanup);return exact(t)', '(void)cleanup;return exact(t)')
    code = code.replace('assert(!finish && native_token.identity && !running && !s_ap_netif);',
                        'assert(!finish && native_token.identity && !running);')
    code = code.replace('if(!sta_storage)return 0;',
                        'if(!sta_storage){s_wifi_state.runtime_cleanup_pending=false;return 0;}')
    header = (WIFI.parents[3] / 'internal/esp32_mquickjs_wifi.h').read_text()
    code += structure(header, 'esp32_mquickjs_wifi_configuration_execution_t')
    radio_header = (WIFI.parents[3] / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    code += structure(radio_header, 'esp32_mquickjs_wifi_radio_stop_snapshot_t')
    code += structure(radio_header, 'esp32_mquickjs_wifi_radio_restart_selection_t')
    code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
    code += BOUNDARIES
    code += extract(AP.read_text(), 'esp32_mquickjs_wifi_ap_begin_stopped_restart')
    code += ''.join(extract(WIFI.read_text(), name) for name in (
        'wifi_restart_restore_interfaces', 'wifi_restart_retry_interfaces', 'wifi_restart_interfaces_inner', 'esp32_mquickjs_wifi_restart_interfaces',
        'esp32_mquickjs_wifi_restart_stopped_interfaces'))
    return code


class WiFiRestartRuntime(unittest.TestCase):
    def test_explicit_retry_reuses_checkpoint_and_retires_helpers_before_rebuild(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        setup += STOPPED_MAIN[:STOPPED_MAIN.index('int main(void)')]
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + setup + RETRY_MAIN)

    def test_off_source_retires_helpers_and_token_only_after_final_stop(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        setup += STOPPED_MAIN[:STOPPED_MAIN.index('int main(void)')]
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + setup + OFF_MAIN)

    def test_cold_source_defers_helpers_until_init_and_keeps_failure_cleanup(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        setup += STOPPED_MAIN[:STOPPED_MAIN.index('int main(void)')]
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + setup + fixture_text('wifi/lifecycle/test_wifi_restart_runtime/test_cold_source_defers_helpers_until_init_and_keeps_failure_cleanup.inc'))

    def test_wapi_policy_changes_only_after_checkpoint_and_retiring_old_helpers(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        compile_run(self, runtime_code(1, wapi=True) + setup + fixture_text('wifi/lifecycle/test_wifi_restart_runtime/test_wapi_policy_changes_only_after_checkpoint_and_retiring_old_helpers.inc'))

    def test_real_executor_modes_failure_handoff_and_cleanup_suffixes(self):
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + MAIN)

    def test_stopped_executor_ap_admission_allocation_and_cleanup_handoff(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + setup + STOPPED_MAIN)

    def test_missing_history_prepares_source_ap_before_checkpoint_and_retains_failed_helper(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        setup += STOPPED_MAIN[:STOPPED_MAIN.index('int main(void)')]
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + setup + SOURCE_MAIN)


BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_restart_runtime/boundaries.inc')


MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_runtime/main.inc')


STOPPED_MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_runtime/stopped_main.inc')


SOURCE_MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_runtime/source_main.inc')

OFF_MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_runtime/off_main.inc')

RETRY_MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_runtime/retry_main.inc')
