"""Deferred original/patched SDK EAP replacement and secret-release regression.

Compiles actual SDK setter/reset/config-deinit bodies through the production
patcher. Allocation/timer/storage boundaries are injected; does not model or
prove EAP task retirement, Radio admission or authentication. Do not execute
until the Wi-Fi phase gate.
"""
import os
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run
from test_wifi_rx_target import unit

ROOT = Path(__file__).resolve().parents[2]
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


PRELUDE = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t u8;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_ARG -3
#define ESP_ERR_NOT_SUPPORTED -4
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 1
#define os_memcpy memcpy
#define os_strlen strlen
#define os_strnlen strnlen
#define os_strcmp strcmp
#define os_strcat strcat
#define os_snprintf snprintf
#define os_bzero(p,n) memset(p,0,n)
static struct {void *pointer;size_t size;} blocks[128];
static unsigned active,dirty_frees,timers;
static bool fail_next;
static void *os_zalloc(size_t size) {
    if(fail_next){fail_next=false;return NULL;}
    void *p=calloc(1,size);assert(p);
    for(unsigned i=0;i<128;i++)if(!blocks[i].pointer){blocks[i].pointer=p;blocks[i].size=size;active++;return p;}
    assert(0);return NULL;
}
static void os_free(void *p) {
    if(!p)return;
    for(unsigned i=0;i<128;i++)if(blocks[i].pointer==p) {
        bool dirty=false;for(size_t j=0;j<blocks[i].size;j++)if(((u8 *)p)[j])dirty=true;
        if(dirty)dirty_frees++;
        blocks[i].pointer=NULL;active--;free(p);return;
    }
    assert(!"unknown allocation/double free");
}
static char *os_strdup(const char *s) {size_t n=strlen(s)+1;char *p=os_zalloc(n);if(p)memcpy(p,s,n);return p;}
static void config_changed_handler(void *a,void *b) {(void)a;(void)b;}
static int eloop_register_timeout(unsigned sec,unsigned usec,void (*callback)(void *,void *),void *a,void *b) {
    assert(!sec && !usec && callback==config_changed_handler && !a && !b);timers++;return 0;
}
/* SDK storage shape at the actual deinit boundary; no replacement state machine. */
struct eap_peer_config {
    u8 *anonymous_identity,*identity,*password,*new_password;
    size_t anonymous_identity_len,identity_len,password_len,new_password_len;
    void *eap_methods;
};
struct eap_sm {struct eap_peer_config config;};
static void *config_methods;
'''

MAIN = r'''
int main(void) {
    u8 first[600],second[600];memset(first,0x41,sizeof(first));memset(second,0x42,sizeof(second));
    int (*setters[])(const unsigned char *,int)={esp_eap_client_set_identity,esp_eap_client_set_username,
        esp_eap_client_set_password,esp_eap_client_set_new_password};
    void (*clearers[])(void)={esp_eap_client_clear_identity,esp_eap_client_clear_username,
        esp_eap_client_clear_password,esp_eap_client_clear_new_password};
    u8 **values[]={&g_wpa_anonymous_identity,&g_wpa_username,&g_wpa_password,&g_wpa_new_password};
    int *lengths[]={&g_wpa_anonymous_identity_len,&g_wpa_username_len,&g_wpa_password_len,&g_wpa_new_password_len};
    for(unsigned i=0;i<4;i++) {
        assert(setters[i](first,24)==ESP_OK && active==1);
        u8 *previous=*values[i];unsigned before=timers;
        fail_next=true;assert(setters[i](second,16)==ESP_ERR_NO_MEM);
        if(PATCHED)assert(*values[i]==previous && *lengths[i]==24 && !memcmp(previous,first,24) && timers==before);
        else assert(!*values[i] && *lengths[i]==24 && active==0);
        assert(setters[i](second,16)==ESP_OK);
#if PATCHED
        assert(setters[i](*values[i]+1,8)==ESP_OK && *lengths[i]==8 && !memcmp(*values[i],second,8));
        assert(setters[i](NULL,1)==ESP_ERR_INVALID_ARG && *lengths[i]==8);
#endif
        clearers[i]();assert(!*values[i] && !*lengths[i] && !active);
        assert(setters[i](first,24)==ESP_OK);eap_globals_reset();assert(!active);
    }
    /* Full-sized PAC replacement used to leak. Failed replacement must keep it. */
    assert(esp_eap_client_set_pac_file(first,600)==ESP_OK);
    u8 *old_pac=g_wpa_pac_file;
    assert(esp_eap_client_set_pac_file(second,512)==ESP_OK);
    assert(active==(PATCHED?1U:2U) && g_wpa_pac_file_len==512);
    if(!PATCHED)os_free(old_pac); /* Release the proven unreferenced original leak. */
    old_pac=g_wpa_pac_file;fail_next=true;
    assert(esp_eap_client_set_pac_file(first,600)==ESP_ERR_NO_MEM);
    if(PATCHED)assert(g_wpa_pac_file==old_pac && g_wpa_pac_file_len==512);
    else {assert(!g_wpa_pac_file && active==1);os_free(old_pac);}
    eap_globals_reset();assert(!active);
    /* Existing short-input semantics create a 512-byte empty PAC. Wipe all 512
     * even if native code later fills it while logical length remains zero. */
    assert(esp_eap_client_set_pac_file(first,1)==ESP_OK && !g_wpa_pac_file_len);
    memset(g_wpa_pac_file,0x77,512);esp_eap_client_clear_certificate_and_key();assert(!active);
    esp_eap_fast_config fast={.fast_provisioning=2,.fast_max_pac_list_len=99,.fast_pac_format_binary=true};
    assert(esp_eap_client_set_fast_params(fast)==ESP_OK);
    char *old_phase=g_wpa_phase1_options;fail_next=true;
    assert(esp_eap_client_set_fast_params(fast)==ESP_ERR_NO_MEM);
    if(PATCHED)assert(g_wpa_phase1_options==old_phase);else assert(!g_wpa_phase1_options);
    eap_globals_reset();assert(!active);
#ifndef CONFIG_TLS_INTERNAL_CLIENT
    assert(esp_eap_client_set_domain_name("radius.example")==ESP_OK);
    char *old_domain=g_wpa_domain_match;fail_next=true;
    assert(esp_eap_client_set_domain_name("other.example")==ESP_ERR_NO_MEM);
    if(PATCHED)assert(g_wpa_domain_match==old_domain);else assert(!g_wpa_domain_match);
#if PATCHED
    assert(esp_eap_client_set_domain_name(g_wpa_domain_match+1)==ESP_OK && !strcmp(g_wpa_domain_match,"adius.example"));
#endif
    assert(esp_eap_client_set_domain_name(NULL)==ESP_OK);assert(!active);
#else
    assert(esp_eap_client_set_domain_name("radius.example")==ESP_ERR_NOT_SUPPORTED);
#endif
    /* Complete and partial-init SM copies are wiped by actual SDK deinit. */
    for(unsigned count=0;count<=4;count++) {
        struct eap_sm sm={0};u8 **p[]={&sm.config.anonymous_identity,&sm.config.identity,&sm.config.password,&sm.config.new_password};
        size_t *n[]={&sm.config.anonymous_identity_len,&sm.config.identity_len,&sm.config.password_len,&sm.config.new_password_len};
        for(unsigned i=0;i<4;i++){*n[i]=24;if(i<count){*p[i]=os_zalloc(24);memset(*p[i],0xa5,24);}}
        eap_peer_config_deinit(&sm);assert(!active && !config_methods);
    }
    /* Borrowed cert/key pointers must only be cleared, never freed by SDK. */
    g_wpa_private_key=first;g_wpa_private_key_len=600;g_wpa_client_cert=second;g_wpa_client_cert_len=600;
    g_wpa_private_key_passwd=first;g_wpa_private_key_passwd_len=600;g_wpa_ca_cert=second;g_wpa_ca_cert_len=600;
    eap_globals_reset();assert(!g_wpa_private_key && first[0]==0x41 && second[0]==0x42 && !active);
    if(PATCHED)assert(!dirty_frees);else assert(dirty_frees>0);
    return 0;
}
'''
