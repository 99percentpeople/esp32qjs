"""Deferred production EAP init/task/post/deinit scheduling and native snapshot.

The scheduler yields only at an empty queue wait or task exit, running the
actual SDK worker body between boundaries. No replacement EAP state machine.
Allocator, task, queue, semaphore and crypto-storage boundaries are injected.
"""
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import sys
import unittest
from tests.support.native_compile import compile_run
from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL, unit
from tests.support.wireless_vm_fixture import extract
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_eap_control import patch_source


class IDFEAPLifecycle(unittest.TestCase):
    def test_production_worker_exit_before_storage_and_retry(self):
        if not os.environ.get('IDF_PATH'):
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = Path(os.environ['IDF_PATH']) / 'components/wpa_supplicant'
        relative = 'esp_supplicant/src/esp_eap_client.c'
        original = (component / relative).read_bytes()
        source = patch_source(relative, original).decode()
        code = PRELUDE
        start = source.index('static void *s_wpa2_task_hdl')
        end = source.index('static void config_changed_handler', start)
        code += source[start:end]
        start = source.index('#define WPA_ADDR_LEN 6')
        end = source.index('static void wpa2_rxq_init', start)
        code += source[start:end]
        peer = (component / 'src/eap_peer/eap.c').read_text()
        code += '#define ESP_EAP_TYPE_ALL 15\n'
        code += peer[peer.index('u8 *g_wpa_anonymous_identity;'):peer.index('void eap_peer_config_deinit')]
        code += BOUNDARIES
        for name in ['wpa2_rxq_init','wpa2_rxq_enqueue','wpa2_rxq_dequeue','wpa2_rxq_remove','wpa2_rxq_deinit',
                     'wpa2_task','wpa2_post']:
            code += extract(source, name)
        start = source.index('static inline esp_err_t wpa2_task_delete')
        code += source[start:source.index('\n}\n', start)+3]
        code += extract(source, 'eap_peer_sm_deinit')
        code += extract(source, 'eap_peer_sm_init')
        code += extract(source, 'eap_sm_rx_eapol')
        code += extract(source, 'eap_client_disable_fn')
        code += extract(source, 'esp32qjs_eap_native_resources')
        code += extract(source, 'esp32qjs_eap_native_cleanup_error')
        code += extract(source, 'esp32qjs_eap_native_control_error')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_eap_sdk.h')
        code += unit(ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise/esp32_mquickjs_wifi_eap_sdk.c')
        compile_run(self, code + MAIN)

    def test_source_drift_is_rejected(self):
        if not os.environ.get('IDF_PATH'):
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        relative = 'esp_supplicant/src/esp_eap_client.c'
        original = (Path(os.environ['IDF_PATH']) / 'components/wpa_supplicant' / relative).read_bytes()
        for bad in [original+b'\n', patch_source(relative, original)]:
            with self.assertRaises(ValueError):patch_source(relative, bad)


PRELUDE = fixture_text('wifi/security/test_idf_eap_lifecycle/prelude.inc')

BOUNDARIES = fixture_text('wifi/security/test_idf_eap_lifecycle/boundaries.inc')

MAIN = fixture_text('wifi/security/test_idf_eap_lifecycle/main.inc')
