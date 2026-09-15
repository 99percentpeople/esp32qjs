"""Deferred fixed-SDK production DPP row conversion and installation transactions.

Allocator/driver boundaries are controlled; no independent test state machine.
These tests do not prove cryptographic negotiation or radio scheduling.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import sys
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT


def sdk_struct(text, name):
    end = text.index('} ' + name + ';') + len('} ' + name + ';')
    return text[text.rfind('typedef struct {', 0, end):end]


class DppConfigTransaction(unittest.TestCase):
    def test_complete_key_equality_failed_replacement_and_oversized_received_keys(self):
        sdk_path = os.environ.get('IDF_PATH')
        if not sdk_path:
            self.skipTest('Set IDF_PATH to the reviewed SDK')
        sys.path.insert(0, str(ROOT / 'scripts'))
        from sdk_patches.wpa.dpp import patch_source, function
        sdk = Path(sdk_path)
        component = sdk / 'components/wpa_supplicant'
        public = (sdk / 'components/esp_wifi/include/esp_wifi_types_generic.h').read_text()
        source = patch_source('esp_supplicant/src/esp_dpp.c', (component / 'esp_supplicant/src/esp_dpp.c').read_bytes()).decode()
        code = TYPES + sdk_struct(public, 'esp_dpp_config_data_t') + BOUNDARIES
        for name in ('esp32qjs_dpp_config_valid', 'esp_dpp_stored_conf_matches_row',
                     'esp_dpp_conf_alloc_from_config_data', 'esp_supp_dpp_set_config',
                     'esp_dpp_process_config_obj', 'esp_dpp_handle_config_obj'):
            code += function(source, name)
        compile_run(self, code + MAIN)


TYPES = fixture_text('wifi/provisioning/dpp/test_dpp_config_transaction/types.inc')
BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_dpp_config_transaction/boundaries.inc')
MAIN = fixture_text('wifi/provisioning/dpp/test_dpp_config_transaction/main.inc')
