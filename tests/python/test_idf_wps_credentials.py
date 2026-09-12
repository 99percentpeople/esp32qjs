"""Deferred WPS credential-boundary tests using patched production C bodies.

Do not run until the Wi-Fi phase gate. These injected allocator/driver edges
do not prove RF, eloop retirement, event delivery or public Session behavior.
"""
import os
from pathlib import Path
import re
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps import OUTPUTS, function, patch_source


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


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
#define SSID_MAX_LEN 32
#define ETH_ALEN 6
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define WIFI_IF_STA 0
#define WPS_AUTH_WPAPSK 2
#define WPS_ENCR_AES 8
#define WIFI_AUTH_WPA_PSK 2
#define os_memcpy memcpy
#define os_memset memset
#define MAX_CRED_COUNT 3
'''

FACTORY = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static unsigned unsafe_formats;
static int os_snprintf(char *out,size_t size,const char *format,...) {
    /* Observe the real formatter boundary without executing the original UB. */
    if(strcmp(format,"%s")){unsafe_formats++;return 0;}
    va_list args;va_start(args,format);
    int result=vsnprintf(out,size,format,args);va_end(args);return result;
}
'''

DRIVER = r'''
/* SDK storage shapes at injected driver boundaries, not a WPS state machine. */
typedef struct {struct {
    u8 ssid[32],password[64];
    struct {int authmode;} threshold;
    int bssid_set,sae_pwe_h2e;
    unsigned untouched;
} sta;} wifi_config_t;
static struct wps_sm {struct wps_credential creds[MAX_CRED_COUNT];u8 ap_cred_cnt;} native;
static bool missing_owner;
static struct wps_sm *wps_sm_get(void) {return missing_owner?NULL:&native;}
static wifi_config_t saved,installed;
static unsigned calls[4],allocated,dirty_frees;
static bool fail_alloc;
static int get_error,disconnect_error,set_error;
static void *allocation;
static void *os_zalloc(size_t n) {
    calls[0]++;if(fail_alloc)return NULL;
    assert(n==sizeof(wifi_config_t) && !allocation);
    allocation=calloc(1,n);assert(allocation);allocated++;return allocation;
}
static void os_free(void *p) {
    if(!p)return;
    assert(p==allocation && allocated==1);
    for(size_t i=0;i<sizeof(wifi_config_t);i++)if(((u8 *)p)[i]){dirty_frees++;break;}
    free(p);allocation=NULL;allocated--;
}
static int esp_wifi_get_config(int iface,wifi_config_t *config) {
    assert(iface==WIFI_IF_STA);calls[1]++;*config=saved;return get_error;
}
static int esp_wifi_disconnect(void) {calls[2]++;return disconnect_error;}
static int esp_wifi_set_config(int iface,wifi_config_t *config) {
    assert(iface==WIFI_IF_STA);calls[3]++;installed=*config;return set_error;
}
static void reset(void) {
    assert(!allocated);memset(calls,0,sizeof(calls));
    fail_alloc=false;get_error=disconnect_error=set_error=0;
    memset(&saved,0x55,sizeof(saved));saved.sta.threshold.authmode=7;
    memset(&installed,0,sizeof(installed));
}
'''

MAIN = r'''
int main(void) {
    struct wps_credential cred={0};
    memcpy(cred.ssid,"new",3);cred.ssid_len=3;
    memcpy(cred.key,"new-pass",8);cred.key_len=8;
    reset();
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_OK);
    assert(!memcmp(installed.sta.ssid,"new",3) && !memcmp(installed.sta.password,"new-pass",8));
    for(unsigned i=3;i<32;i++)assert(!installed.sta.ssid[i]);
    for(unsigned i=8;i<64;i++)assert(!installed.sta.password[i]);
    assert(installed.sta.untouched==saved.sta.untouched && installed.sta.threshold.authmode==7);
    assert(!installed.sta.bssid_set && !installed.sta.sae_pwe_h2e);
    assert(!allocated && !dirty_frees);
    reset();fail_alloc=true;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_NO_MEM && !calls[1] && !calls[2] && !calls[3]);
    reset();get_error=201;
    assert(esp32qjs_wps_apply_credential(&cred)==201 && !calls[2] && !calls[3]);
    reset();disconnect_error=202;
    assert(esp32qjs_wps_apply_credential(&cred)==202 && !calls[3]);
    reset();set_error=203;
    assert(esp32qjs_wps_apply_credential(&cred)==203 && calls[3]==1);
    assert(!allocated && !dirty_frees);
    reset();cred.ssid_len=32;cred.key_len=64;
    memset(cred.ssid,0x61,32);memset(cred.key,0x62,64);
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_OK);
    assert(!memcmp(installed.sta.ssid,cred.ssid,32) && !memcmp(installed.sta.password,cred.key,64));
    reset();cred.key_len=0;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_OK);
    for(unsigned i=0;i<64;i++)assert(!installed.sta.password[i]);
    reset();cred.ssid_len=33;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_INVALID_ARG && !calls[0]);
    cred.ssid_len=SIZE_MAX;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_INVALID_ARG && !calls[0]);
    cred.ssid_len=0;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_INVALID_ARG && !calls[0]);
    cred.ssid_len=3;cred.key_len=65;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_INVALID_ARG && !calls[0]);
    assert(save_credentials_cb(NULL,&cred)==ESP_FAIL && !native.ap_cred_cnt);
    cred.key_len=SIZE_MAX;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_ERR_INVALID_ARG && !calls[0]);
    assert(esp32qjs_wps_apply_credential(NULL)==ESP_ERR_INVALID_ARG && !calls[0]);
    cred.key_len=8;cred.auth_type=WPS_AUTH_WPAPSK;cred.encr_type=WPS_ENCR_AES;
    assert(esp32qjs_wps_apply_credential(&cred)==ESP_OK);
#ifdef CONFIG_WPS_STRICT
    assert(installed.sta.threshold.authmode==7);
#else
    assert(installed.sta.threshold.authmode==WIFI_AUTH_WPA_PSK);
#endif
    cred.cred_attr=cred.key;cred.cred_attr_len=8;
    for(unsigned i=0;i<MAX_CRED_COUNT;i++) {
        assert(save_credentials_cb(NULL,&cred)==ESP_OK);
        assert(native.ap_cred_cnt==i+1 && !native.creds[i].cred_attr && !native.creds[i].cred_attr_len);
        assert(!memcmp(native.creds[i].ssid,cred.ssid,32) && !memcmp(native.creds[i].key,cred.key,64));
    }
    assert(save_credentials_cb(NULL,&cred)==ESP_FAIL && native.ap_cred_cnt==MAX_CRED_COUNT);
    assert(save_credentials_cb(NULL,NULL)==ESP_FAIL);
    missing_owner=true;assert(save_credentials_cb(NULL,&cred)==ESP_FAIL);
    assert(!allocated && !dirty_frees);
    return 0;
}
'''
