"""Deferred production EAP-WSC peer isolation and parent teardown regressions.

Only AST-parse during the Wi-Fi implementation wave. These fixtures execute
patched SDK function bodies with allocator/protocol/driver boundaries, not a
separate lifecycle model. They do not establish native queue or RF retirement.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import sys
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps import function
from patch_idf_wps_registrar import patch_source


def peer_init(source):
    start = source.index('static void * eap_wsc_init(')
    return source[start:source.index('\n}\n', start) + 3]


class WpsRegistrarPeers(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = Path(sdk) / 'components/wpa_supplicant'
        paths = ('src/eap_server/eap_server_wsc.c', 'src/ap/wps_hostapd.c',
                 'src/ap/ieee802_1x.c')
        cls.original = {p: (component / p).read_bytes() for p in paths}
        cls.patched = {p: patch_source(p, src).decode() for p, src in cls.original.items()}

    def test_original_peers_alias_and_reset_disables_parent_inside_eap(self):
        src = self.original['src/eap_server/eap_server_wsc.c'].decode()
        code = TYPES + peer_init(src) + function(src, 'eap_wsc_reset')
        compile_run(self, code + fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_peers/test_original_peers_alias_and_reset_disables_parent_inside_eap.inc'))

    def test_peers_own_protocol_and_credential_snapshots_reset_is_local(self):
        src = self.patched['src/eap_server/eap_server_wsc.c']
        code = TYPES + peer_init(src) + function(src, 'eap_wsc_reset')
        compile_run(self, code + fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_peers/test_peers_own_protocol_and_credential_snapshots_reset_is_local.inc'))

    def test_every_peer_allocation_failure_and_admission_rejection_preserves_parent(self):
        src = self.patched['src/eap_server/eap_server_wsc.c']
        code = TYPES + peer_init(src) + function(src, 'eap_wsc_reset')
        compile_run(self, code + fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_peers/test_every_peer_allocation_failure_and_admission_rejection_preserves_parent.inc'))

    def test_parent_deinit_retires_peers_before_authenticator_registrar_methods(self):
        wsc = self.patched['src/eap_server/eap_server_wsc.c']
        host = self.patched['src/ap/wps_hostapd.c']
        auth = self.patched['src/ap/ieee802_1x.c']
        code = TYPES + peer_init(wsc) + function(wsc, 'eap_wsc_reset')
        code += TEARDOWN_BOUNDARIES
        code += function(auth, 'esp32qjs_wps_eapol_deinit')
        code += function(host, 'ap_sta_server_sm_deinit')
        code += function(host, 'esp32qjs_wps_ap_hostap_release')
        code += function(host, 'hostapd_deinit_wps')
        compile_run(self, code + fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_peers/test_parent_deinit_retires_peers_before_authenticator_registrar_methods.inc'))


TYPES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_peers/types.inc')

TEARDOWN_BOUNDARIES = fixture_text('wifi/provisioning/wps/test_idf_wps_registrar_peers/teardown_boundaries.inc')
