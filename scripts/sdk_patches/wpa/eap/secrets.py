"""Scrub fixed-SDK EAP credential copies and preserve replacements on OOM.

Only build-local copies are written. This is not an EAP task/retirement fix and
must not be used as proof that concurrent credential mutation is safe.
"""
from __future__ import annotations
import argparse
from sdk_patches.common.source import function, replace
import hashlib
from pathlib import Path
import re

REVIEWED = {
    'src/utils/common.c': 'b99ef519c251d958c937fbf67f6df39721ee2f1e2cc9ed008300f59b6050961f',
    'port/os_xtensa.c': 'a27e9e2bc0169c4c6fa6ea86457a911b409bb0c974042f44143522344f89a9bc',
    'port/include/os.h': '0d79eb5a6f84354317b9b871500ca4869b2056797cec2b16867da009ffb43665',
    'src/eap_peer/eap_config.h': 'f30bf715325fe86c6bb50672c55ec534dca61493925ae5e91cb104abbafe469f',
    'CMakeLists.txt': 'a9568d989bbc5bfb2a2efe3500493b5b6fe9f88fb46e0869e192b7b1a8315217',
    'esp_supplicant/src/esp_eap_client.c': '568c313a3e3d597bd9a2e56cbce0fffc44aab09dd3b42eb1eeb508057fc78a7b',
    'src/eap_peer/eap.c': 'eaffe3ef896caea89c1ceb144ac9e0fb7b76d2c4aaaf03f3b9f46fb6d7bc329f',
    'esp_supplicant/include/esp_eap_client.h': '7a512a1dc9e388a5ecef95fc347befb5361e342b66c96a93904a8d41fd0bc6a0',
}






REPLACE_BYTES = '''/* ESP32QJS: prepare a complete copy before wiping the previous allocation.
 * Caller must serialize against EAP readers; this helper adds no task lock. */
static esp_err_t esp32qjs_eap_replace_bytes(u8 **target, int *target_len,
    const unsigned char *value, int len, int maximum)
{
    if (!value || len <= 0 || (maximum && len > maximum)) return ESP_ERR_INVALID_ARG;
    u8 *next = os_zalloc(len);
    if (!next) return ESP_ERR_NO_MEM;
    os_memcpy(next, value, len); /* Input may point into the old allocation. */
    bin_clear_free(*target, *target_len);
    *target = next;
    *target_len = len;
    eloop_register_timeout(0, 0, config_changed_handler, NULL, NULL);
    return ESP_OK;
}

'''

PAC = '''esp_err_t esp_eap_client_set_pac_file(const unsigned char *pac_file, int pac_file_len)
{
    if (!pac_file || pac_file_len < 0) return ESP_FAIL;
    /* Preserve the fixed SDK's empty-PAC provisioning semantics below 512.
     * Do not silently redefine its parser/format through this lifetime fix. */
    int capacity = pac_file_len < 512 ? 512 : pac_file_len;
    u8 *next = os_zalloc(capacity);
    if (!next) return ESP_ERR_NO_MEM;
    if (pac_file_len >= 512) os_memcpy(next, pac_file, pac_file_len);
    bin_clear_free(g_wpa_pac_file, g_wpa_pac_file_len < 512 ? 512 : g_wpa_pac_file_len);
    g_wpa_pac_file = next;
    g_wpa_pac_file_len = pac_file_len < 512 ? 0 : pac_file_len;
    eloop_register_timeout(0, 0, config_changed_handler, NULL, NULL);
    return ESP_OK;
}
'''

DOMAIN = '''esp_err_t esp_eap_client_set_domain_name(const char *domain_name)
{
#ifdef CONFIG_TLS_INTERNAL_CLIENT
    return ESP_ERR_NOT_SUPPORTED;
#else
    int len = domain_name ? os_strnlen(domain_name, MAX_DOMAIN_MATCH_LEN + 1) : 0;
    if (len > MAX_DOMAIN_MATCH_LEN) return ESP_ERR_INVALID_ARG;
    if (g_wpa_domain_match && domain_name && os_strcmp(g_wpa_domain_match, domain_name) == 0) return ESP_OK;
    char *next = domain_name ? os_strdup(domain_name) : NULL;
    if (domain_name && !next) return ESP_ERR_NO_MEM;
    str_clear_free(g_wpa_domain_match);
    g_wpa_domain_match = next;
    eloop_register_timeout(0, 0, config_changed_handler, NULL, NULL);
    return ESP_OK;
#endif
}
'''


def patch_source(relative: str, source: bytes) -> bytes:
    if relative not in ('esp_supplicant/src/esp_eap_client.c', 'src/eap_peer/eap.c'):
        raise ValueError('Not an EAP patch input: ' + relative)
    if hashlib.sha256(source).hexdigest() != REVIEWED[relative]:
        raise ValueError('Unreviewed SDK EAP source: ' + relative)
    text = source.decode()
    if relative == 'src/eap_peer/eap.c':
        for field in ('anonymous_identity', 'identity', 'password', 'new_password'):
            text = replace(text, f'os_free(sm->config.{field});',
                           f'bin_clear_free(sm->config.{field}, sm->config.{field}_len);')
        return text.encode()
    text = replace(text, '#define ANONYMOUS_ID_LEN_MAX 128', REPLACE_BYTES + '#define ANONYMOUS_ID_LEN_MAX 128')
    for field, parameter, maximum in (
        ('anonymous_identity', 'identity', 'ANONYMOUS_ID_LEN_MAX'),
        ('username', 'username', 'USERNAME_LEN_MAX'), ('password', 'password', '0'),
        ('new_password', 'new_password', '0'),
    ):
        name = 'esp_eap_client_set_' + parameter
        replacement = f'''esp_err_t {name}(const unsigned char *{parameter}, int len)
{{
    return esp32qjs_eap_replace_bytes(&g_wpa_{field}, &g_wpa_{field}_len, {parameter}, len, {maximum});
}}
'''
        text = replace(text, function(text, name), replacement)
        # After replacing the setter, the SDK global reset and explicit clear remain.
        text = replace(text, f'os_free(g_wpa_{field});', f'bin_clear_free(g_wpa_{field}, g_wpa_{field}_len);', 2)
    text = replace(text, function(text, 'esp_eap_client_set_pac_file'), PAC)
    text = replace(text, 'os_free(g_wpa_pac_file);',
                   'bin_clear_free(g_wpa_pac_file, g_wpa_pac_file_len < 512 ? 512 : g_wpa_pac_file_len);', 2)
    text = replace(text, function(text, 'esp_eap_client_set_domain_name'), DOMAIN)
    text = replace(text, 'os_free(g_wpa_domain_match);', 'str_clear_free(g_wpa_domain_match);')
    old = '''    // Free the old buffer if it already exists
    if (g_wpa_phase1_options != NULL) {
        os_free(g_wpa_phase1_options);
    }
    g_wpa_phase1_options = (char *)os_zalloc(sizeof(config_for_supplicant));
    if (g_wpa_phase1_options == NULL) {
        eloop_register_timeout(0, 0, config_changed_handler, NULL, NULL);
        return ESP_ERR_NO_MEM;
    }
    os_memcpy(g_wpa_phase1_options, &config_for_supplicant, sizeof(config_for_supplicant));'''
    text = replace(text, old, '''    char *next = (char *)os_zalloc(sizeof(config_for_supplicant));
    if (!next) return ESP_ERR_NO_MEM;
    os_memcpy(next, &config_for_supplicant, sizeof(config_for_supplicant));
    str_clear_free(g_wpa_phase1_options);
    g_wpa_phase1_options = next;''')
    text = replace(text, 'os_free(g_wpa_phase1_options);', 'str_clear_free(g_wpa_phase1_options);')
    text = replace(text, 'os_free(sm->eapKeyData);', 'bin_clear_free(sm->eapKeyData, sm->eapKeyDataLen);', 2)
    return text.encode()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--component', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.output_dir.resolve().is_relative_to(args.component.resolve()):
        parser.error('output must be outside the shared SDK component')
    try:
        for relative, digest in REVIEWED.items():
            if hashlib.sha256((args.component / relative).read_bytes()).hexdigest() != digest:
                raise ValueError('Unreviewed SDK EAP dependency: ' + relative)
        outputs = {name: patch_source(relative, (args.component / relative).read_bytes()) for name, relative in (
            ('esp_eap_client.c', 'esp_supplicant/src/esp_eap_client.c'), ('eap.c', 'src/eap_peer/eap.c'))}
    except ValueError as error:
        parser.error(str(error))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, source in outputs.items():
        output = args.output_dir / name
        if not output.exists() or output.read_bytes() != source:
            output.write_bytes(source)
        print('ESP32QJS EAP secret cleanup: reviewed build-local ' + name + ' ' + hashlib.sha256(source).hexdigest())


if __name__ == '__main__':
    main()
