#!/usr/bin/env python3
"""Prepare pinned native NAN pairing/PASN sources inside one build only."""
import argparse
from sdk_patches.common.source import function, replace
import hashlib
from pathlib import Path
import re

INPUTS = {
    'esp_wifi/wifi_apps/nan_app/src/nan_pairing.c': 'a1aff52feb97e37b981715ffce00170ee2443ac628ff0748ad8ab8cfee523060',
    'esp_wifi/wifi_apps/include/apps_private/wifi_apps_private.h': 'cc28a994c15ef0e677e4a6bc6472310da36ffd4d52fcdb169fc96065e7fe56fa',
    'wpa_supplicant/esp_supplicant/src/esp_nan_supplicant.c': 'b00620357d2e2d8e92659c9477e73df6560827a52fa9edf7317c56cc3059221d',
    'wpa_supplicant/esp_supplicant/src/esp_nan_supp_i.h': 'c6581539ab5aea5845a0b6677a652468a45ffd2b5cb05a0de130be30dcc30dbe',
    'wpa_supplicant/esp_supplicant/include/esp_private/esp_supp_nan.h': '061c0a4bbfdb1aa824b40883fbd6bfe1a28e1efa52b300d6a7c66c96f969133d',
    'wpa_supplicant/port/eloop.c': '2decdfef8e932070a791d7fd4dfa85251fedbfd6ad06f5a0da22f4b1c6eab9ac',
    'wpa_supplicant/CMakeLists.txt': 'a9568d989bbc5bfb2a2efe3500493b5b6fe9f88fb46e0869e192b7b1a8315217',
}






def patch_supplicant(source):
    source = replace(source, '#define IEEE80211_MGMT_HDRLEN 24',
        '#include "esp32_mquickjs_wifi_nan_pasn_state.inc"\n\n#define IEEE80211_MGMT_HDRLEN 24')
    source = replace(source, '/* Default NAN pairing PIN used when caller does not configure one. */\n#define NAN_DEFAULT_PAIRING_PIN "000000"\n', '')
    source = replace(source, 'static int pasn_responder_init(const uint8_t *peer_addr, uint32_t pincode,\n'
        '                               esp_nan_pairing_key_installed_cb_t pairing_key_installed_cb);\n', '')
    source = function(source, 'handle_auth_pasn', lambda s: s[:s.index('    if (!nan) {')] +
        '''    /* An Auth1 frame is not user consent. The explicitly admitted peer
     * and local address must match before touching the existing operation. */
    if (!nan || !nan->pasn || s_esp32qjs_pasn_status.closing || s_esp32qjs_pasn_status.error ||
        os_memcmp(mgmt->sa, nan->pasn_unicast_peer, ETH_ALEN) ||
        os_memcmp(mgmt->da, nan->cfg->dev_addr, ETH_ALEN)) return;
''' + s[s.index('    if (le_to_host16(mgmt->auth.auth_transaction) == 1) {'):])
    source = function(source, 'handle_auth_pasn', lambda s: replace(s,
        '    nan_pasn_auth_rx(nan, mgmt, len);',
        '    if (nan_pasn_auth_rx(nan, mgmt, len) < 0) esp32qjs_pasn_failure(nan, ESP_FAIL);'))
    source = function(source, 'handle_auth_pasn', lambda s: replace(s,
        '    if (auth1_verify) {',
        '    if (le_to_host16(mgmt->auth.auth_transaction) == 1 &&\n'
        '        auth1_verify != (bool)nan->pairing_verification) return;\n'
        '    if (auth1_verify) {'))
    source = function(source, 'nan_pasn_auth_timeout_cb', lambda s: replace(replace(s,
        '    struct nan_pasn_data *nan = eloop_ctx;',
        '    struct nan_pasn_data *nan = esp_nan_app_get_pasn_data();'),
        '    if (!nan) {',
        '    if (!nan || !eloop_ctx || nan->framework_identity != (uint32_t)(uintptr_t)eloop_ctx) {'))
    source = function(source, 'nan_pasn_auth_timeout_cb', lambda s: replace(s,
        '    nan_pasn_data_deinit(nan);',
        '    esp32qjs_pasn_failure(nan, ESP_ERR_TIMEOUT);\n    nan_pasn_data_deinit(nan);'))
    source = function(source, 'nan_pasn_auth_timeout_arm', lambda s: replace(s,
        'nan_pasn_auth_timeout_cb, nan,',
        'nan_pasn_auth_timeout_cb, (void *)(uintptr_t)nan->framework_identity,'))
    source = function(source, 'nan_pasn_auth_timeout_cancel', lambda s: replace(s,
        'nan_pasn_auth_timeout_cb, nan, ELOOP_ALL_CTX',
        'nan_pasn_auth_timeout_cb, (void *)(uintptr_t)nan->framework_identity, ELOOP_ALL_CTX'))
    source = function(source, 'nan_pasn_data_init', lambda s: '''struct nan_pasn_data *nan_pasn_data_init(void)
{
    s_esp32qjs_pasn_create_error = ESP_ERR_INVALID_STATE;
    if (!current_task_is_wifi_task() || esp_nan_app_get_pasn_data()) return NULL;
    s_esp32qjs_pasn_create_error = ESP_ERR_NO_MEM;
    if (s_esp32qjs_pasn_last_identity == UINT32_MAX) return NULL;
    uint32_t identity = ++s_esp32qjs_pasn_last_identity;
    struct nan_pasn_data *pd = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1,
        sizeof(*pd), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!pd) return NULL;
    pd->framework_identity = identity;
    pd->cfg = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1,
        sizeof(*pd->cfg), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!pd->cfg) goto failed;
    s_esp32qjs_pasn_create_error = esp_wifi_get_mac(WIFI_IF_NAN, pd->cfg->dev_addr);
    if (s_esp32qjs_pasn_create_error != ESP_OK) goto failed;
    s_esp32qjs_pasn_create_error = ESP_ERR_NO_MEM;
    pd->cfg->pasn_type = 0x03;
    pd->cfg->cb_ctx = pd;
    pd->cfg->pasn_send_mgmt = nan_pasn_esp_send_mgmt;
    pd->initiator_pmksa = pasn_initiator_pmksa_cache_init();
    if (!pd->initiator_pmksa) goto failed;
    pd->responder_pmksa = pasn_responder_pmksa_cache_init();
    if (!pd->responder_pmksa) goto failed;
    /* No implicit credential. Authentication commands install an explicit PIN;
     * verification uses the independently validated cached NPK/NIRA. */
    s_esp32qjs_pasn_status = (esp32_mquickjs_wifi_nan_pasn_status_t){.identity=identity, .active=true};
    s_esp32qjs_pasn_create_error = ESP_OK;
    return pd;
failed:
    nan_pasn_data_deinit(pd);
    return NULL;
}
''')
    source = function(source, 'nan_pasn_data_deinit', lambda s: replace(replace(s,
        '    os_free(pd->cfg);',
        '    if (pd->cfg) {\n        forced_memzero(pd->cfg, sizeof(*pd->cfg));\n'
        '        esp32_mquickjs_memory_payload_free(pd->cfg);\n    }'),
        '    os_free(pd);',
        '    if (pd->framework_identity == s_esp32qjs_pasn_status.identity) {\n'
        '        s_esp32qjs_pasn_status.active = false;\n        s_esp32qjs_pasn_status.retired = true;\n    }\n'
        '    forced_memzero(pd, sizeof(*pd));\n    esp32_mquickjs_memory_payload_free(pd);'))
    source = function(source, 'nan_pasn_esp_send_mgmt', lambda s: replace(replace(s,
        '    const u8 *da = data + 4;', '    const u8 *da;'),
        '    if (!data || data_len <= IEEE80211_MGMT_HDRLEN) {',
        '    if (!data || !nan || data_len <= IEEE80211_MGMT_HDRLEN ||\n'
        '        data_len - IEEE80211_MGMT_HDRLEN > UINT16_MAX ||\n'
        '        data_len - IEEE80211_MGMT_HDRLEN > SIZE_MAX - sizeof(*req)) {'))
    source = function(source, 'nan_pasn_esp_send_mgmt', lambda s: replace(s,
        '    /* If stack built Auth1', '    da = data + 4;\n\n    /* If stack built Auth1'))
    def send_mgmt(s):
        s = replace(s, '    req = os_zalloc(sizeof(*req) + body_len);',
            '    uint8_t service_id = 0;\n'
            '    uint32_t identity = esp32_mquickjs_wifi_nan_pasn_identity(da);\n'
            '    if (!identity || !esp32_mquickjs_wifi_nan_pairing_services(da, &service_id, NULL)) return -1;\n'
            '    req = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*req) + body_len,\n'
            '        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);')
        s = replace(s, '    ret = esp_wifi_send_mgmt_frm_internal(req);',
            '    ret = esp32_mquickjs_wifi_nan_tx_pairing_enter(identity, service_id, true);\n'
            '    if (ret == ESP_OK) {\n'
            '        ret = esp_wifi_send_mgmt_frm_internal(req);\n'
            '        esp32_mquickjs_wifi_nan_tx_pairing_leave(identity);\n    }')
        return replace(s, '    os_free(req);',
            '    forced_memzero(req, sizeof(*req) + body_len);\n'
            '    esp32_mquickjs_memory_payload_free(req);')
    source = function(source, 'nan_pasn_esp_send_mgmt', send_mgmt)
    # Admission excludes live NDPs for this peer. Re-verification must not end
    # unrelated datapaths belonging to the same publisher.
    source = function(source, 'nan_pasn_responder_verify_prepare', lambda s: replace(s,
        '    esp_nan_app_end_peer_datapaths(publish_id);', '    (void)publish_id;'))
    # Allocation/SAE failures must not leave a nonnull but unusable PASN object
    # that callers mistake for successful initialization.
    def initialize(s):
        s = replace(s, 'void nan_pasn_initialize(', 'int nan_pasn_initialize(')
        s = s.replace('return;', 'return ESP_ERR_NO_MEM;')
        s = replace(s, 'if (!nan) {\n        return ESP_ERR_NO_MEM;',
            'if (!nan || !addr || !nan->cfg) {\n        return ESP_ERR_INVALID_ARG;')
        s = replace(s, '        nan_pairing_apply_sae_pin(pasn, nan->cfg->pasn_type, nan);',
            '        nan_pairing_apply_sae_pin(pasn, nan->cfg->pasn_type, nan);\n'
            '        if (!pasn->pt) return ESP_FAIL;')
        s = replace(s, '    if (rsnxe) {', '    if (!rsnxe) return ESP_ERR_NO_MEM;\n    if (rsnxe) {')
        return s[:-2] + '    return ESP_OK;\n}\n'
    source = function(source, 'nan_pasn_initialize', initialize)
    source = function(source, 'nan_initiate_pasn_auth', lambda s: replace(s,
        '    nan_pasn_initialize(nan, addr);', '    if (nan_pasn_initialize(nan, addr) != ESP_OK) return -1;'))
    source = function(source, 'nan_initiate_pasn_verify', lambda s: replace(s,
        '    nan_pasn_initialize(nan, peer_addr);', '    if (nan_pasn_initialize(nan, peer_addr) != ESP_OK) { ret = -1; goto out; }'))
    source = function(source, 'nan_initiate_pasn_verify', lambda s: replace(s,
        'out:\n    forced_memzero(npk, sizeof(npk));',
        '    if (!ret && nan_pasn_auth_timeout_arm(nan, NAN_ROLE_PAIRING_INITIATOR)) ret = -1;\n'
        'out:\n    forced_memzero(npk, sizeof(npk));'))
    # Native key-install success must not hide KEK/flatten failure.
    for name in ('nan_handle_pasn_auth', 'nan_pasn_auth_rx'):
        source = function(source, name, lambda s: replace(s,
            '        nan_pasn_copy_keys_from_pasn(nan, pasn);' if name == 'nan_handle_pasn_auth' else
            '            nan_pasn_copy_keys_from_pasn(nan, pasn);',
            ('        ' if name == 'nan_handle_pasn_auth' else '            ') +
            'nan_pasn_copy_keys_from_pasn(nan, pasn);\n'
            '        if (!g_nan_pasn_saved_keys.valid) { esp32qjs_pasn_failure(nan, ESP_FAIL); return -1; }'))
    source = function(source, 'nan_handle_pasn_auth', lambda s: replace(s,
        '        if (nan_pasn_install_nan_pairwise_tk(nan, pasn) == 0 &&\n'
        '                nan->pairing_key_installed_cb) {',
        '        if (nan_pasn_install_nan_pairwise_tk(nan, pasn) != 0) {\n'
        '            esp32qjs_pasn_failure(nan, ESP_FAIL); return -1;\n        }\n'
        '        if (nan->pairing_key_installed_cb) {'))
    source = function(source, 'nan_pasn_auth_rx', lambda s: replace(s,
        '            if (tk_ret == 0 && nan->pairing_key_installed_cb) {',
        '            if (tk_ret != 0) { esp32qjs_pasn_failure(nan, ESP_FAIL); return -1; }\n'
        '            nan_pasn_auth_timeout_cancel(nan);\n'
        '            if (nan->pairing_key_installed_cb) {'))
    # Checked commands retain their stack input until native initialization has
    # really completed. No hidden heap context may replace another operation.
    start = source.index('struct nan_pasn_eloop_ctx {')
    source = source[:start] + '#include "esp32_mquickjs_wifi_nan_pasn_commands.inc"\n\n#endif /* CONFIG_ESP_WIFI_NAN_PAIRING */\n'
    source, count = re.subn(r'^[ \t]*wpa_hexdump(?:_key)?\([^;]*;\n', '', source, flags=re.M)
    if count != 15:
        raise ValueError(f'Expected 15 PASN dumps, found {count}')
    return source


def patch_pairing(source):
    source = replace(source, '#include "esp_nan.h"', '#include "esp_nan.h"\n'
        '#include "esp32_mquickjs_wifi_nan_pasn_sdk.h"\n#include "esp32_mquickjs_wifi_nan_sdk.h"\n#include "esp32_mquickjs_memory.h"\n'
        '#include "esp32_mquickjs_wifi_nan_tx.h"\n#include "esp32_mquickjs_wifi_nan_ndp.h"')
    source = replace(source, 'struct nan_pairing_fup_ctx {',
        'struct nan_pairing_fup_ctx {\n    uint32_t framework_identity;')
    source = replace(source, 'static struct peer_svc_info *nan_find_peer_svc_exact(',
        '#include "esp32_mquickjs_wifi_nan_pairing_pending.inc"\n\n'
        'static struct peer_svc_info *nan_find_peer_svc_exact(')
    source = source.replace('os_zalloc(sizeof(*ctx))',
        'esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*ctx), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL)')
    source = replace(source, 'os_zalloc(alloc_len)',
        'esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, alloc_len, ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL)')
    source = source.replace('os_free(ctx);', 'esp32qjs_pairing_context_free(ctx);')
    source = replace(source,
        'eloop_register_timeout(0, 0, nan_app_send_pairing_followup_eloop, NULL, ctx)',
        'esp32qjs_pairing_schedule(ctx)', 2)
    source = function(source, 'nan_app_send_pairing_followup_eloop', lambda s: replace(s,
        '    (void) nan_app_send_pairing_followup(',
        '    if (ctx == s_esp32qjs_pairing_pending) s_esp32qjs_pairing_pending = NULL;\n'
        '    if (!ctx->framework_identity || ctx->framework_identity != esp32_mquickjs_wifi_nan_pasn_identity(ctx->peer_mac)) {\n'
        '        esp32qjs_pairing_context_free(ctx); return;\n    }\n'
        '    esp_err_t error = nan_app_send_pairing_followup('))
    source = function(source, 'nan_app_send_pairing_followup_eloop', lambda s: replace(s,
        '\n    esp32qjs_pairing_context_free(ctx);',
        '\n    if (error != ESP_OK) esp32_mquickjs_wifi_nan_pasn_result(ctx->peer_mac, error, false);\n'
        '    esp32qjs_pairing_context_free(ctx);'))
    source = function(source, 'nan_pairing_nik_fup_timeout_cb', lambda s: replace(replace(s,
        '    (void)eloop_data;',
        '    if (!eloop_data || (uint32_t)(uintptr_t)eloop_data != esp32_mquickjs_wifi_nan_pasn_identity(NULL)) return;'),
        '    evt.status = WIFI_NAN_PAIRING_STATUS_ACCEPTED;',
        '    evt.status = WIFI_NAN_PAIRING_STATUS_REJECTED;'))
    source = function(source, 'nan_pairing_arm_pending', lambda s: replace(replace(s,
        '    if (!own || !peer_mac) {',
        '    uint32_t identity = esp32_mquickjs_wifi_nan_pasn_identity(peer_mac);\n'
        '    if (!own || !peer_mac || !identity) {'),
        'nan_pairing_nik_fup_timeout_cb, NULL,',
        'nan_pairing_nik_fup_timeout_cb, (void *)(uintptr_t)identity,'))
    source = function(source, 'nan_pairing_arm_pending', lambda s: replace(s,
        '        own->nik_fup_pending = false;',
        '        own->nik_fup_pending = false;\n'
        '        esp32_mquickjs_wifi_nan_pasn_result(peer_mac, ESP_ERR_NO_MEM, false);'))
    source = function(source, 'nan_pairing_cancel_svc_pending', lambda s: replace(s,
        'nan_pairing_nik_fup_timeout_cb, NULL, own',
        'nan_pairing_nik_fup_timeout_cb, ELOOP_ALL_CTX, own'))
    source = function(source, 'nan_pairing_key_installed_cb', lambda s: replace(s,
        '    if (!peer_nmi) {',
        '    if (!peer_nmi || !esp32_mquickjs_wifi_nan_pasn_identity(peer_nmi)) {'))
    source = function(source, 'nan_pairing_key_installed_cb', lambda s: replace(s,
        '    const struct nan_pasn_key_material *keys = nan_pasn_get_saved_keys();',
        '    esp32_mquickjs_wifi_nan_pasn_result(peer_nmi, ESP_OK, false);\n'
        '    const struct nan_pasn_key_material *keys = nan_pasn_get_saved_keys();'))
    source = replace(source, 'nan_app_post_event(WIFI_EVENT_NAN_PAIRING_CONFIRM, &evt, sizeof(evt));',
        'esp32qjs_pairing_post(&evt);', 4)
    source = replace(source,
        '        ESP_LOGE(TAG, "Invalid pincode %u (valid range %u..%u or UINT32_MAX for default)",\n'
        '                 cfg->cred.pincode, NAN_PAIRING_PINCODE_MIN, NAN_PAIRING_PINCODE_MAX);',
        '        ESP_LOGE(TAG, "Invalid pairing credential");')
    source = replace(source, '            cfg->cred.pincode != UINT32_MAX &&\n', '')
    source = function(source, 'esp_wifi_nan_pairing_start', lambda s: replace(s,
        '        ret = esp_nan_supp_pasn_responder_init(cfg->peer_nmi, cfg->cred.pincode,',
        '        ret = cfg->pairing_verification ? esp32_mquickjs_wifi_nan_pasn_verify_responder(cfg->peer_nmi) :\n'
        '            esp_nan_supp_pasn_responder_init(cfg->peer_nmi, cfg->cred.pincode,'))
    source = function(source, 'esp_wifi_nan_pairing_start', lambda s: replace(replace(s,
        'esp_err_t esp_wifi_nan_pairing_start(', 'static esp_err_t esp32qjs_pairing_start_native('),
        '    if (!cfg) {',
        '    if (esp32_mquickjs_wifi_nan_pasn_identity(NULL) || esp_nan_app_get_pasn_data()) return ESP_ERR_INVALID_STATE;\n'
        '    if (!cfg) {') + '''
static int esp32qjs_pairing_start_command(void *opaque, void *unused)
{
    (void)unused;
    return esp32qjs_pairing_start_native(opaque);
}
esp_err_t esp_wifi_nan_pairing_start(wifi_nan_pairing_config_t *config)
{
    extern bool current_task_is_wifi_task(void);
    if (!config || !config->peer_svc_id || (config->peer_nmi[0] & 1) ||
        !memcmp(config->peer_nmi, (uint8_t[6]){0}, 6) ||
        (config->self_role != NAN_PAIRING_ROLE_INITIATOR && config->self_role != NAN_PAIRING_ROLE_RESPONDER) ||
        (!config->pairing_verification && config->cred.pincode > NAN_PAIRING_PINCODE_MAX))
        return ESP_ERR_INVALID_ARG;
    wifi_nan_pairing_config_t copy = *config;
    esp_err_t error = current_task_is_wifi_task() ? esp32qjs_pairing_start_command(&copy, NULL) :
        eloop_register_timeout_blocking(esp32qjs_pairing_start_command, &copy, NULL);
    forced_memzero(&copy, sizeof(copy));
    return error;
}
''')
    # Prevent short frames from reaching the attribute header or secret parser.
    source = function(source, 'nan_app_receive_pairing_followup', lambda s: replace(s,
        '    if (shared_key_attr[0] != NAN_ATTR_ID_SHARED_KEY_DESC)',
        '    if (!shared_key_attr || shared_key_attr_buf_len < 3 || !peer_mac ||\n'
        '        !esp32_mquickjs_wifi_nan_pasn_identity(peer_mac) ||\n'
        '        shared_key_attr[0] != NAN_ATTR_ID_SHARED_KEY_DESC)'))
    return patch_pairing_identity(source)


def patch_pairing_identity(source):
    source = replace(source, 'static esp_nan_app_bootstrap_cb_t s_bootstrap_cb;',
        'static esp_nan_app_bootstrap_cb_t s_bootstrap_cb;\n'
        '#include "esp32_mquickjs_wifi_nan_pairing_binding.inc"')
    source = function(source, 'nan_pairing_resolve_own_inst', lambda s: '''static uint8_t nan_pairing_resolve_own_inst(uint8_t peer_svc_id, const uint8_t *peer_nmi)
{
    uint8_t own = 0, remote = 0;
    return esp32_mquickjs_wifi_nan_pairing_services(peer_nmi, &own, &remote) &&
        peer_svc_id == remote ? own : 0;
}
''')
    # NPK belongs to the explicitly selected credential, not the first service
    # with a cached key or the only key belonging to an unrelated service.
    source = function(source, 'nan_peer_cred_lookup', lambda s: '''static const wifi_nan_peer_creds_t *nan_peer_cred_lookup(void)
{
    const esp32qjs_pairing_binding_t *binding = s_esp32qjs_pairing_binding;
    if (!current_task_is_wifi_task() || !binding || !binding->config.verification ||
        !esp32_mquickjs_wifi_nan_pasn_identity(binding->config.peer) ||
        (binding->credential_expires_us && esp_timer_get_time() >= binding->credential_expires_us)) return NULL;
    const struct own_svc_info *own = nan_find_own_svc(binding->config.service_id);
    return own && !memcmp(own->svc_hash, binding->service_hash, 6) &&
        nan_peer_cred_npk_present(&binding->credential) ? &binding->credential : NULL;
}
''')
    # Limit the native verification flags to the bound service as well.
    for name in ('nan_pairing_take_verify_session', 'esp_nan_pairing_clear_verify_session'):
        source = function(source, name, lambda s: replace(s,
            '        if (own->verify_session_pending &&',
            '        if (s_esp32qjs_pairing_binding &&\n'
            '                own->svc_id == s_esp32qjs_pairing_binding->config.service_id &&\n'
            '                own->verify_session_pending &&'))
    source = function(source, 'esp_nan_pairing_mark_verify_session', lambda s: replace(s,
        '    if (!own_inst_id || !peer_nmi) {',
        '    uint8_t admitted = 0;\n'
        '    if (!esp32_mquickjs_wifi_nan_pairing_services(peer_nmi, &admitted, NULL) || own_inst_id != admitted) {'))
    source = function(source, 'esp32qjs_pairing_start_native', lambda s: replace(s,
        '    if (!cfg) {',
        '    uint8_t admitted_peer = 0;\n'
        '    if (!cfg || !esp32_mquickjs_wifi_nan_pairing_services(cfg->peer_nmi, NULL, &admitted_peer) ||\n'
        '        cfg->peer_svc_id != admitted_peer) {'))
    # Keep the SDK signature available, but it can never create an unbound
    # operation. The framework exact-service command is the admission point.
    source = function(source, 'esp_wifi_nan_pairing_start', lambda s: s +
        '\n#include "esp32_mquickjs_wifi_nan_pairing_start.inc"\n')

    def installed(s):
        s = replace(s, '    uint8_t own_svc_id = 0;',
            '    uint8_t own_svc_id = 0;\n'
            '    if (!esp32_mquickjs_wifi_nan_pairing_services(peer_nmi, &own_svc_id, &peer_remote_svc_id)) return;')
        s = replace(s, 'nan_find_peer_svc(0, 0, (uint8_t *)peer_nmi)',
            'nan_find_peer_svc_exact(own_svc_id, peer_remote_svc_id, peer_nmi)')
        s = replace(s, '    NAN_DATA_UNLOCK();\n\n    uint32_t lifetime_sec',
            '    NAN_DATA_UNLOCK();\n'
            '    if (!peer) { esp32_mquickjs_wifi_nan_pasn_result(peer_nmi, ESP_ERR_INVALID_STATE, false); return; }\n\n'
            '    uint32_t lifetime_sec')
        s = replace(s, '                                      own ? own->svc_hash : NULL);',
            '                                      own ? own->svc_hash : NULL);\n'
            '            forced_memzero(npk_tmp, sizeof(npk_tmp));')
        s = replace(s, '        if (!ctx) {',
            '        if (!ctx) {\n            esp32_mquickjs_wifi_nan_pasn_result(peer_nmi, ESP_ERR_NO_MEM, false);')
        s = replace(s, '        (void)nan_app_register_paired_peer(peer_nmi, role, ndp_csid,\n'
            '                                           nd_pmk, nd_pmk_len,\n'
            '                                           lifetime_sec);',
            '        esp_err_t cache_error = nan_app_register_paired_peer(peer_nmi, role, ndp_csid,\n'
            '            nd_pmk, nd_pmk_len, lifetime_sec);\n'
            '        if (cache_error != ESP_OK) {\n'
            '            esp32_mquickjs_wifi_nan_pasn_result(peer_nmi, cache_error, false); return;\n        }')
        return s
    source = function(source, 'nan_pairing_key_installed_cb', installed)

    def received(s):
        s = replace(s, '    if (!shared_key_attr || !peer_mac) {',
            '    uint8_t own_id = 0, remote_id = 0;\n'
            '    if (!shared_key_attr || !esp32_mquickjs_wifi_nan_pairing_services(peer_mac, &own_id, &remote_id) ||\n'
            '        svc_id != own_id || peer_svc_id != remote_id) {')
        start = s.index('    if (!p_peer_svc) {')
        end = s.index('    if (p_peer_svc) {', start)
        s = s[:start] + s[end:]
        # Both secret arrays are wiped on every return, including failures
        # following decrypt, cache updates and deferred-send allocation.
        s = replace(s, '    uint8_t nik[NAN_APP_PEER_NIK_LEN];',
            '    uint8_t nik[NAN_APP_PEER_NIK_LEN] __attribute__((cleanup(esp32qjs_pairing_clear_nik))) = {0};')
        s = replace(s, '    uint8_t persist_npk[ESP_WIFI_NAN_NPK_LEN] = {0};',
            '    uint8_t persist_npk[ESP_WIFI_NAN_NPK_LEN] __attribute__((cleanup(esp32qjs_pairing_clear_npk))) = {0};')
        s = replace(s, '        const struct nan_paired_peer *paired = nan_app_find_paired_peer(peer_mac);\n', '')
        s = replace(s, '        } else if (paired) {\n'
            '            memcpy(persist_npk, paired->nd_pmk, ESP_WIFI_NAN_NPK_LEN);\n'
            '            npk_src = persist_npk;\n', '\n')
        s = replace(s, '    if (!ctx) {\n        return;',
            '    if (!ctx) {\n        esp32_mquickjs_wifi_nan_pasn_result(peer_mac, ESP_ERR_NO_MEM, false);\n        return;')
        s = replace(s, '    /* Persist outside the lock; NVS writes can block. */',
            '    if (!p_peer_svc || !own) {\n'
            '        esp32_mquickjs_wifi_nan_pasn_result(peer_mac, ESP_ERR_INVALID_STATE, false); return;\n    }\n'
            '    /* Persist outside the lock; NVS writes can block. */')
        return s
    source = function(source, 'nan_app_receive_pairing_followup', received)
    def send_followup(s):
        s = replace(s, '    uint8_t plain[64] = {0};',
            '    uint8_t plain[64] __attribute__((cleanup(esp32qjs_pairing_clear_plain))) = {0};')
        s = replace(s, '    uint8_t nik[NAN_PASN_NIK_LEN];',
            '    uint8_t nik[NAN_PASN_NIK_LEN] __attribute__((cleanup(esp32qjs_pairing_clear_nik))) = {0};')
        return replace(s, '    return esp_nan_internal_send_followup(&fup, &tx_ctx, &params_i);',
            '    uint8_t own_id = 0, remote_id = 0;\n'
            '    uint32_t identity = esp32_mquickjs_wifi_nan_pasn_identity(peer_mac);\n'
            '    if (!esp32_mquickjs_wifi_nan_pairing_services(peer_mac, &own_id, &remote_id) ||\n'
            '        own_id != svc_id || remote_id != peer_svc_id) return ESP_ERR_INVALID_STATE;\n'
            '    esp_err_t error = esp32_mquickjs_wifi_nan_tx_pairing_enter(identity, svc_id, false);\n'
            '    if (error != ESP_OK) return error;\n'
            '    error = esp_nan_internal_send_followup(&fup, &tx_ctx, &params_i);\n'
            '    esp32_mquickjs_wifi_nan_tx_pairing_leave(identity);\n'
            '    return error;')
    source = function(source, 'nan_app_send_pairing_followup', send_followup)
    # NIK exchange stages key material in the existing binding. Only the
    # checked commit command publishes it after protocol and actual TX finish.
    source = function(source, 'nan_app_update_peer_creds', lambda s: """static void nan_app_update_peer_creds(const uint8_t *peer_nik, const uint8_t *npk,
                                      const uint8_t service_hash[6], uint32_t lifetime_sec)
{
    esp32qjs_pairing_binding_t *binding = s_esp32qjs_pairing_binding;
    if (!binding || !peer_nik || !npk || !service_hash || binding->committed_credential_id ||
        memcmp(binding->service_hash, service_hash, 6) ||
        !esp32_mquickjs_wifi_nan_pasn_identity(binding->config.peer)) return;
    forced_memzero(&binding->credential, sizeof(binding->credential));
    memcpy(binding->credential.peer_nik, peer_nik, ESP_WIFI_NAN_NIK_LEN);
    memcpy(binding->credential.npk, npk, ESP_WIFI_NAN_NPK_LEN);
    memcpy(binding->credential.service_hash, service_hash, 6);
    binding->credential.is_valid = true;
    if (!binding->config.verification)
        binding->credential_expires_us = lifetime_sec ? esp_timer_get_time() + (int64_t)lifetime_sec * 1000000 : 0;
}
""")
    source = function(source, 'nan_pairing_key_installed_cb', lambda s: replace(s,
        '                                      own ? own->svc_hash : NULL);',
        '                                      own ? own->svc_hash : NULL, nik_lifetime_sec);'))
    source = function(source, 'nan_app_receive_pairing_followup', lambda s: replace(s,
        'nan_app_update_peer_creds(nik, npk_src, own ? own->svc_hash : NULL);',
        'nan_app_update_peer_creds(nik, npk_src, own ? own->svc_hash : NULL, lifetime_sec);'))
    source += '\n#include "esp32_mquickjs_wifi_nan_pairing_cache.inc"\n#include "esp32_mquickjs_wifi_nan_bootstrap_sdk.inc"\n'
    # The app's receive callback already owns the complete service callback
    # guard. Forward through the existing observer without JS, allocation or
    # waiting on an application event queue.
    source = function(source, 'nan_app_bootstrap_notify', lambda s: """static void nan_app_bootstrap_notify(const wifi_nan_bootstrap_event_t *evt)
{
    if (!evt || !evt->own_svc_id || !evt->peer_svc_id || (evt->peer_nmi[0] & 1) ||
        !memcmp(evt->peer_nmi, (uint8_t[6]){0}, 6)) return;
    esp32_mquickjs_wifi_nan_sdk_bootstrap_t data = {.type = evt->type, .peer_service_id = evt->peer_svc_id,
        .status = evt->status, .methods = evt->methods};
    esp32_mquickjs_wifi_nan_sdk_notice_t notice = {.kind = ESP32_MQUICKJS_NAN_SDK_BOOTSTRAP,
        .service_id = evt->own_svc_id, .data = &data, .size = sizeof(data)};
    memcpy(notice.peer, evt->peer_nmi, 6);
    esp32_mquickjs_wifi_nan_sdk_notice(&notice);
    if (s_bootstrap_cb) s_bootstrap_cb(evt);
}
""")
    source = function(source, 'nan_app_parse_npba_from_receive', lambda s: replace(s,
        '        nan_app_bootstrap_indication(peer_svc_id, own_svc_id, peer_nmi, npba->methods);',
        '        if (npba->status != WIFI_NAN_PAIRING_STATUS_ACCEPTED) {\n'
        '            wifi_nan_bootstrap_event_t evt = {.type = npba->type, .status = npba->status,\n'
        '                .own_svc_id = own_svc_id, .peer_svc_id = peer_svc_id, .methods = npba->methods};\n'
        '            memcpy(evt.peer_nmi, peer_nmi, 6); nan_app_bootstrap_notify(&evt);\n'
        '        } else nan_app_bootstrap_indication(peer_svc_id, own_svc_id, peer_nmi, npba->methods);'))
    for name in ('nan_app_bootstrap_indication', 'nan_app_bootstrap_completed'):
        source = function(source, name, lambda s: replace(s, '\n{\n', '\n{\n    if (!peer_nmi) return;\n'))

    def nira(s):
        discovery = s.replace('nan_verify_nira_internal(', 'esp32qjs_nan_discovery_verify_nira(')
        discovery = replace(discovery, '        if (!s_nan_ctx.peer_creds[i].is_valid) {',
            '        esp32_mquickjs_wifi_nan_credential_info_t metadata;\n'
            '        if (!s_nan_ctx.peer_creds[i].is_valid || !esp32_mquickjs_wifi_nan_credential_info(i, &metadata)) {')
        start = s.index('    /* The peer derives its NIRA tag')
        end = s.index('\n    if (match) {', start)
        return discovery + '\n' + s[:start] + '''    /* Verify only the admitted credential. MAC randomization does not
     * authorize searching another service's NIK or switching its NPK. */
    uint8_t own_id = 0;
    if (!esp32_mquickjs_wifi_nan_pairing_services(peer_mac, &own_id, NULL) ||
        !esp32_mquickjs_wifi_nan_pasn_identity(peer_mac) ||
        nira_attr[0] != NAN_ATTR_ID_IDENTITY_RESOLUTION ||
        WPA_GET_LE16(nira_attr + 1) != NAN_NIRA_ATTR_LEN - 3 ||
        nira_attr[3] != NAN_NIRA_CIPHER_VER) return false;
    NAN_DATA_LOCK();
    const wifi_nan_peer_creds_t *credential = nan_peer_cred_lookup();
    if (credential && !nan_pairing_derive_nira_tag(credential->peer_nik, peer_mac, nonce, expected_tag))
        match = os_memcmp_const(expected_tag, received_tag, NAN_NIRA_TAG_LEN) == 0;
    if (match && own_inst_id) *own_inst_id = own_id;
    NAN_DATA_UNLOCK();
    forced_memzero(expected_tag, sizeof(expected_tag));
''' + s[end:]
    source = function(source, 'nan_verify_nira_internal', nira)
    source = function(source, 'esp_nan_verify_nira', lambda s: replace(s,
        'nan_verify_nira_internal(', 'esp32qjs_nan_discovery_verify_nira('))
    return source


def prepare(components, output):
    sources = {}
    for name, digest in INPUTS.items():
        data = (components / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('Unreviewed pairing input: ' + name)
        sources[Path(name).name] = data.decode()
    prepared = {
        'esp_nan_supplicant.c': patch_supplicant(sources['esp_nan_supplicant.c']),
        'esp_nan_supp_i.h': replace(sources['esp_nan_supp_i.h'], 'struct nan_pasn_data {',
            'struct nan_pasn_data {\n    uint32_t framework_identity;'),
        'nan_pairing.c': patch_pairing(sources['nan_pairing.c']),
    }
    output.mkdir(parents=True, exist_ok=True)
    for name, source in prepared.items():
        (output / name).write_text(source)
    return prepared


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--components', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.components, args.output_dir)


if __name__ == '__main__':
    main()
