"""Deferred production EAP control and deattach failure propagation.

SDK driver writes/register storage, method allocation and eloop are boundaries;
control admission, callback installation, public dispatch, suffix cleanup and
native diagnostics execute the actual composed SDK bodies.
"""
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import sys
import unittest
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract
from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, unit
sys.path.insert(0, str(ROOT / 'scripts'))
from sdk_patches.wpa.eap.control import patch_source


class IDFEAPControl(unittest.TestCase):
    def test_control_failures_identity_of_steps_and_wifi_task_dispatch(self):
        if not os.environ.get('IDF_PATH'):
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = Path(os.environ['IDF_PATH']) / 'components/wpa_supplicant'
        relative = 'esp_supplicant/src/esp_eap_client.c'
        source = patch_source(relative, (component / relative).read_bytes()).decode()
        main_relative = 'esp_supplicant/src/esp_wpa_main.c'
        main = patch_source(main_relative, (component / main_relative).read_bytes()).decode()
        code = PRELUDE + unit(component / 'esp_supplicant/include/esp_eap_client.h')
        driver = (component / 'esp_supplicant/src/esp_wifi_driver.h').read_text()
        for anchor in ['struct wpa2_funcs {', 'typedef esp_err_t (*wifi_wpa2_fn_t)']:
            start = driver.index(anchor)
            end = driver.index('};', start)+3 if anchor.startswith('struct') else driver.index('} wifi_wpa2_param_t;',start)+len('} wifi_wpa2_param_t;')
            code += driver[start:end]+'\n'
        start = source.index('static void *s_wpa2_task_hdl')
        code += source[start:source.index('static void config_changed_handler', start)]
        peer = (component / 'src/eap_peer/eap.c').read_text()
        code += peer[peer.index('u8 *g_wpa_anonymous_identity;'):peer.index('void eap_peer_config_deinit')]
        code += BOUNDARIES
        for name in ['wpa2_api_lock','wpa2_api_unlock','wpa2_is_enabled','wpa2_set_state',
                     'esp_client_enable_fn','esp32qjs_eap_enable_dispatch','esp_wifi_sta_enterprise_enable',
                     'eap_client_disable_fn','esp32qjs_eap_disable_dispatch','esp_wifi_sta_enterprise_disable',
                     'esp32qjs_eap_native_resources','esp32qjs_eap_native_control_error',
                     'esp32qjs_eap_on_worker','esp32qjs_eap_configuration_idle']:
            code += extract(source, name)
        code += extract(main, 'wpa_deattach')
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/security/test_idf_eap_control/prelude.inc')

BOUNDARIES = fixture_text('wifi/security/test_idf_eap_control/boundaries.inc')

MAIN = fixture_text('wifi/security/test_idf_eap_control/main.inc')
