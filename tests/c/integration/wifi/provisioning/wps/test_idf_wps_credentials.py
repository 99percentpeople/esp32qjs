"""Deferred WPS credential-boundary tests using patched production C bodies.

Do not run until the Wi-Fi phase gate. These injected allocator/driver edges
do not prove RF, eloop retirement, event delivery or public Session behavior.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import re
import sys
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))
from sdk_patches.wpa.wps.credentials import OUTPUTS, function, patch_source


class IDFWPSCredentials(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed ESP-IDF')
        cls.component = Path(sdk) / 'components/wpa_supplicant'
        cls.sources = {p: (cls.component / p).read_bytes() for p in OUTPUTS.values()}

    def test_hash_drift_and_double_patch_rejected(self):
        for relative, source in self.sources.items():
            for bad in (source + b'\n', patch_source(relative, source)):
                with self.subTest(relative=relative), self.assertRaises(ValueError):
                    patch_source(relative, bad)

    def test_driver_errors_bounds_shorter_replacement_and_secure_free(self):
        source = patch_source('esp_supplicant/src/esp_wps.c',
                              self.sources['esp_supplicant/src/esp_wps.c']).decode()
        header = (self.component / 'src/wps/wps.h').read_text()
        start = header.index('struct wps_credential {')
        credential = header[start:header.index('\n};', start) + 3]
        os_header = (self.component / 'port/include/os.h').read_text()
        start = os_header.index('static void * (* const volatile memset_func)')
        secure_zero = os_header[start:os_header.index('\n#endif', start)]
        common = (self.component / 'src/utils/common.c').read_text()
        for strict in (False, True):
            code = PRELUDE + credential + '\n' + DRIVER
            if strict:
                code += '#define CONFIG_WPS_STRICT 1\n'
            code += secure_zero + function(common, 'bin_clear_free')
            for name in ('esp32qjs_wps_credential_valid', 'esp32qjs_wps_apply_credential',
                         'save_credentials_cb'):
                code += function(source, name)
            with self.subTest(strict=strict):
                compile_run(self, code + MAIN)

    def test_original_trailing_bytes_and_patched_replacement(self):
        relative = 'esp_supplicant/src/esp_wps.c'
        original = self.sources[relative].decode()
        # Execute the original SDK's actual three-line get/copy sequence. Its
        # allocator already returned zeroed memory, but get_config refills it.
        start = original.index('            esp_wifi_get_config(WIFI_IF_STA, config);')
        end = original.index('\n#ifndef CONFIG_WPS_STRICT', start)
        body = original[start:end]
        header = (self.component / 'src/wps/wps.h').read_text()
        start = header.index('struct wps_credential {')
        credential = header[start:header.index('\n};', start) + 3]
        code = PRELUDE + credential + '\n' + DRIVER
        code += '\nint main(void) {\n'
        code += 'struct wps_sm *sm=&native; wifi_config_t value={0}, *config=&value;\n'
        code += 'memset(&saved, 0x55, sizeof(saved)); sm->creds[0].ssid_len=3; sm->creds[0].key_len=8;\n'
        code += 'memcpy(sm->creds[0].ssid,"new",3); memcpy(sm->creds[0].key,"new-pass",8);\n'
        code += body
        code += '\nassert(config->sta.ssid[3]==0x55 && config->sta.password[8]==0x55); return 0; }\n'
        compile_run(self, code)

    def test_factory_strings_remain_literal_at_formatter_boundary(self):
        relative = 'esp_supplicant/src/esp_wps.c'
        header = (self.component / 'esp_supplicant/include/esp_wps.h').read_text()
        limits = '\n'.join(re.findall(r'^#define WPS_MAX_\w+_LEN\s+\d+', header, re.M))
        for patched in (False, True):
            source = (patch_source(relative, self.sources[relative]) if patched
                      else self.sources[relative]).decode()
            body = function(source, 'wps_dev_init')
            calls = re.findall(r'    os_snprintf\(dev->(?:manufacturer|model_name|model_number|device_name),[^;]+;', body)
            self.assertEqual(len(calls), 4)
            code = FACTORY + limits + '\n'
            code += 'int main(void) {\n'
            code += 'struct {char manufacturer[65],model_name[33],model_number[33],device_name[33];} output={0}, input={0};\n'
            code += '__typeof__(output) *dev=&output, *s_factory_info=&input;\n'
            for field in ('manufacturer', 'model_name', 'model_number', 'device_name'):
                code += f'strcpy(input.{field}, "value-%n-%s-%%");\n'
            code += '\n'.join(calls)
            code += f'\nassert(unsafe_formats=={0 if patched else 4});\n'
            if patched:
                for field in ('manufacturer', 'model_name', 'model_number', 'device_name'):
                    code += f'assert(!strcmp(output.{field},input.{field}));\n'
            code += 'return 0;}\n'
            with self.subTest(patched=patched):
                compile_run(self, code)


PRELUDE = fixture_text('wifi/provisioning/wps/test_idf_wps_credentials/prelude.inc')

FACTORY = fixture_text('wifi/provisioning/wps/test_idf_wps_credentials/factory.inc')

DRIVER = fixture_text('wifi/provisioning/wps/test_idf_wps_credentials/driver.inc')

MAIN = fixture_text('wifi/provisioning/wps/test_idf_wps_credentials/main.inc')
