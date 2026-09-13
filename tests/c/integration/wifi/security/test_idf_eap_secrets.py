"""Deferred original/patched SDK EAP replacement and secret-release regression.

Compiles actual SDK setter/reset/config-deinit bodies through the production
patcher. Allocation/timer/storage boundaries are injected; does not model or
prove EAP task retirement, Radio admission or authentication. Do not execute
until the Wi-Fi phase gate.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import sys
import unittest
from tests.support.native_compile import compile_run
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_eap_secrets import function, patch_source


class IDFEAPSecrets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed ESP-IDF')
        cls.component = Path(sdk) / 'components/wpa_supplicant'
        cls.sources = {p: (cls.component / p).read_bytes() for p in (
            'esp_supplicant/src/esp_eap_client.c', 'src/eap_peer/eap.c')}

    def test_hash_drift_and_double_patch_rejected(self):
        for relative, source in self.sources.items():
            patched = patch_source(relative, source)
            for bad in (source + b'\n', patched):
                with self.subTest(relative=relative), self.assertRaises(ValueError):
                    patch_source(relative, bad)

    def test_original_release_gap_and_patched_copy_wipe(self):
        for patched in (False, True):
            for internal_tls in (False, True):
                client_name = 'esp_supplicant/src/esp_eap_client.c'
                peer_name = 'src/eap_peer/eap.c'
                client = (patch_source(client_name, self.sources[client_name]) if patched else self.sources[client_name]).decode()
                peer = (patch_source(peer_name, self.sources[peer_name]) if patched else self.sources[peer_name]).decode()
                code = PRELUDE + f'\n#define PATCHED {int(patched)}\n'
                if internal_tls:
                    code += '#define CONFIG_TLS_INTERNAL_CLIENT 1\n'
                code += unit(self.component / 'esp_supplicant/include/esp_eap_client.h')
                # Use the actual SDK's generic secure-zero implementation.
                os_header = (self.component / 'port/include/os.h').read_text()
                start = os_header.index('static void * (* const volatile memset_func)')
                code += os_header[start:os_header.index('\n#endif', start)]
                common = (self.component / 'src/utils/common.c').read_text()
                code += function(common, 'bin_clear_free') + function(common, 'str_clear_free')
                code += peer[peer.index('u8 *g_wpa_anonymous_identity;'):peer.index('void eap_peer_config_deinit')]
                code += '#define ANONYMOUS_ID_LEN_MAX 128\n#define USERNAME_LEN_MAX 128\n#define MAX_DOMAIN_MATCH_LEN 255\n'
                code += '#define PHASE1_PARAM_STRING_LEN 67\n'
                if patched:
                    code += function(client, 'esp32qjs_eap_replace_bytes')
                for name in ['eap_globals_reset', 'esp_eap_client_set_identity', 'esp_eap_client_clear_identity',
                             'esp_eap_client_set_username', 'esp_eap_client_clear_username',
                             'esp_eap_client_set_password', 'esp_eap_client_clear_password',
                             'esp_eap_client_set_new_password', 'esp_eap_client_clear_new_password',
                             'esp_eap_client_set_pac_file', 'esp_eap_client_clear_certificate_and_key',
                             'esp_eap_client_set_fast_params', 'esp_eap_client_set_domain_name']:
                    code += function(client, name)
                code += function(peer, 'eap_peer_config_deinit')
                with self.subTest(patched=patched, internal_tls=internal_tls):
                    compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/security/test_idf_eap_secrets/prelude.inc')

MAIN = fixture_text('wifi/security/test_idf_eap_secrets/main.inc')
