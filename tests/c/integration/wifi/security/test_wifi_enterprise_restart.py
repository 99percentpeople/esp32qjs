"""Deferred Enterprise restart executor/AP/cleanup composition.

Actual runtime and AP coordinator bodies; injected Radio/SDK phase boundaries
assert retention and ordering. Installer, native admission and final publication
are exercised in the separate production EAP and policy fixtures. No RF proof.
"""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.config.test_wifi_configuration_cleanup import BOUNDARIES as CLEANUP_BOUNDARIES, WIFI, AP
from tests.c.integration.wifi.security.test_wifi_enterprise_stop import EAP_BOUNDARIES
from tests.c.integration.wifi.lifecycle.test_wifi_restart_runtime import BOUNDARIES as RESTART_BOUNDARIES, MAIN as RESTART_MAIN
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.monitor.test_wifi_rx_target import INTERNAL, unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, extract


def enterprise_restart_code():
    code = '#include <stdbool.h>\n#include <stdlib.h>\nstatic bool ap_release_blocked;\n' + CLEANUP_BOUNDARIES
    code = code.replace('assert(cleanup);return exact(t)', '(void)cleanup;return exact(t)')
    code = code.replace('assert(!finish && native_token.identity && !running && !s_ap_netif);',
                        'assert(!finish && native_token.identity && !running);')
    code = code.replace('if(!sta_storage)return 0;',
                        'if(!sta_storage){s_wifi_state.runtime_cleanup_pending=false;return 0;}')
    code = code.replace('if(owner->acquired){', 'if(owner==&s_ap_lease && ap_release_blocked)return;\n    if(owner->acquired){')
    code += '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n'
    code += 'typedef struct esp32_mquickjs_wifi_eap_profile esp32_mquickjs_wifi_eap_profile_t;\n'
    code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_config.h')
    eap = EAP_BOUNDARIES.replace('static bool s_wifi_ap_stop_cleanup;', '')
    eap = eap.replace('action==ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE',
                      '(action==ESP32_MQUICKJS_WIFI_EAP_CONFIG_DISABLE || action==ESP32_MQUICKJS_WIFI_EAP_CONFIG_ENABLE)')
    code += eap
    code += structure((INTERNAL / 'esp32_mquickjs_wifi.h').read_text(), 'esp32_mquickjs_wifi_configuration_execution_t')
    for name in ('esp32_mquickjs_wifi_radio_stop_snapshot_t', 'esp32_mquickjs_wifi_radio_restart_selection_t'):
        code += structure((INTERNAL / 'esp32_mquickjs_wifi_radio.h').read_text(), name)
    code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
    code += RESTART_BOUNDARIES
    code += NATIVE_BOUNDARIES
    ap, wifi = AP.read_text(), WIFI.read_text()
    for name in ('wifi_ap_retire_netif', 'wifi_ap_retire_for_token',
                 'esp32_mquickjs_wifi_ap_begin_configuration', 'esp32_mquickjs_wifi_ap_begin_enterprise_restart',
                 'esp32_mquickjs_wifi_ap_begin_enterprise_stop', 'esp32_mquickjs_wifi_ap_begin_stopped_restart',
                 'esp32_mquickjs_wifi_ap_release_enterprise_stop',
                 'esp32_mquickjs_wifi_ap_retire_for_configuration', 'esp32_mquickjs_wifi_ap_retire_for_recovery'):
        code += extract(ap, name)
    for name in ('esp32_mquickjs_wifi_connection_reserved_locked', 'wifi_helpers_idle',
                 'esp32_mquickjs_wifi_retire_for_recovery', 'wifi_begin_configuration_cleanup',
                 'wifi_begin_enterprise_stop', 'wifi_finish_enterprise_clear', 'wifi_finish_configuration_cleanup',
                 'wifi_restart_restore_interfaces', 'wifi_begin_enterprise_restart', 'wifi_restart_enterprise_interfaces',
                 'wifi_restart_retry_interfaces', 'wifi_restart_interfaces_inner',
                 'esp32_mquickjs_wifi_restart_stopped_interfaces', 'esp32_mquickjs_wifi_eap_prepare_runtime_destroy'):
        code += extract(wifi, name)
    return code + RESTART_MAIN[:RESTART_MAIN.index('int main(void)')]


class WiFiEnterpriseRestart(unittest.TestCase):
    def test_source_clear_reinstall_failure_explicit_retry_and_runtime_retirement(self):
        compile_run(self, enterprise_restart_code() + MAIN)


NATIVE_BOUNDARIES = fixture_text('wifi/security/test_wifi_enterprise_restart/native_boundaries.inc')

MAIN = fixture_text('wifi/security/test_wifi_enterprise_restart/main.inc')
