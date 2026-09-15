"""Prepare the pinned NAN native control boundary in the build directory.

This does not grant Radio ownership or register a JavaScript NAN API. The Session
layer must serialize public/native operations, retain timed-out requests, and
fence the SDK before reusing service/datapath identifiers. SDK crypto checks are
preserved. Shared IDF inputs are never modified.
"""
from __future__ import annotations

import argparse
from sdk_patches.common.source import function, replace
import hashlib
from pathlib import Path
import re

REVIEWED = {
    'wifi_apps/nan_app/src/nan_security.c': 'ccc6b692015614484d7c6057f7a1df8e6fbea604efb9efae301966d99e0cadb8',
    'wifi_apps/nan_app/src/nan_app.c': 'a65955e38b0edbb08413ffcb7fe8b3bd8432deda7b293e619635de66b16493ba',
    'wifi_apps/nan_app/src/nan_i.h': '963aa1ce9158ee9185df76bb6a18e688a68308b1c0739d6499c87644621e0d2f',
    'wifi_apps/nan_app/include/esp_nan.h': 'a02b21e318e9c68d723bb4c55f2cb42ac9e07d60bfaaf3c9c3504e13b478fc73',
    'include/esp_wifi_types_generic.h': '8e398a8d22b18c199ea6d48e42784e8a897c22000a09ee9ac7d22cf0f5110e5c',
    'src/wifi_default.c': 'eb83024ec77d3eb05c539477006d74bf1e8634a153ab0d01eb5a850d5b9260d8',
    'CMakeLists.txt': '827b80c66d553fe4855b7e95020b45dec1eef08d546f57d3ee2b83778c375871',
}


def datapath_control(source: str) -> str:
    source = function(source, 'nan_reset_service', lambda s: replace(s,
        'memset(p_own_svc, 0, sizeof(struct own_svc_info));',
        '''#if CONFIG_ESP_WIFI_NAN_PAIRING
        esp32qjs_nan_pairing_reset_service(p_own_svc->svc_id);
#endif
        forced_memzero(p_own_svc, sizeof(*p_own_svc));'''))
    source = function(source, 'nan_app_ndp_indication_cb', lambda s: replace(s,
        '    nan_security_apply_pending(ndl, p_own_svc, pub_id, peer_nmi, peer_ndi);',
        '''    nan_security_apply_pending(ndl, p_own_svc, pub_id, peer_nmi, peer_ndi);
    if (p_own_svc->user_cfg.num_credentials &&
        (!ndl || ndl->security_ctx.type != WIFI_NAN_SECURITY_ENCRYPTED ||
         ndl->handshake_state != NAN_HANDSHAKE_M1_RCVD)) {
        /* A configured secret requires a matched PMKID and a parsed M1. A
         * warning or an encrypted flag without resolved keys is insufficient. */
        NAN_DATA_UNLOCK();
        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_FAILED,
            pub_id, ndp_id, ESP_ERR_INVALID_RESPONSE, 0, peer_nmi);
        ndp_resp.accept = false;
        ndp_resp.ndp_id = ndp_id;
        MACADDR_COPY(ndp_resp.peer_mac, peer_nmi);
        esp_err_t deny_error = esp_nan_internal_datapath_resp(&ndp_resp,
            (uint8_t *)&own_ipv6.u_addr.ip6.addr[2]);
        if (deny_error != ESP_OK) esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_DISCOVERY_NDP_DENIED,
            pub_id, ndp_id, deny_error, 0, peer_nmi);
        return;
    }'''))
    for name in ('nan_app_ndp_response_indication_cb', 'nan_app_ndp_confirm_cb',
                 'nan_app_ndp_terminated_cb', 'esp_nan_ndp_tx_done_cb'):
        source = function(source, name, lambda s: replace(s, '\n{\n',
            '\n{\n    esp32qjs_nan_service_guard_t guard __attribute__((cleanup(esp32qjs_nan_service_leave))) =\n'
            '        esp32qjs_nan_datapath_enter();\n'
            '    if (!guard.entered) return;\n'))

    # The host record contains PMK/PTK and other security state in secure
    # configurations. All resets, including STOP and pre-claim failures, wipe
    # through the SDK's existing non-optimizable secure-zero primitive.
    source = function(source, 'nan_reset_ndl', lambda s: replace(replace(s,
        'memset(s_nan_ctx.ndl, 0, sizeof(struct ndl_info) * ESP_WIFI_NAN_DATAPATH_MAX_PEERS);',
        'forced_memzero(s_nan_ctx.ndl, sizeof(s_nan_ctx.ndl));'),
        'memset(ndl, 0, sizeof(struct ndl_info));', 'forced_memzero(ndl, sizeof(*ndl));'))

    def teardown(s):
        s = replace(s, '    esp_nan_internal_datapath_end(&ndp_end);',
            '    esp_err_t end_error = esp_nan_internal_datapath_end(&ndp_end);')
        s = replace(s, '    nan_reset_ndl(ndp_id, false);', '''    if (end_error != ESP_OK) {
        /* A failed end does not prove native retirement. Keep the exact host
         * security record until confirmed termination or physical STOP. */
        os_event_group_set_bits(nan_event_group, NDP_REJECTED);
        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_CLEANUP_FAILED,
            0, ndp_id, end_error, 0, peer_nmi);
        return;
    }
    /* A successful end only submits its termination frame. Host security
     * stays live for native retries until termination or physical STOP. */''')
        return s
    source = function(source, 'nan_ndp_confirm_teardown', teardown)

    def confirm(s):
        s = replace(s, '''    if (ndl->security_ctx.type == WIFI_NAN_SECURITY_ENCRYPTED) {
        if (!ndl->ptk_set''', '''    if (ndl->security_ctx.type == WIFI_NAN_SECURITY_ENCRYPTED) {
        if ((ndl->gtk_required && (!ndl->gtk_set || ndl->gtk_len != NAN_ND_GTK_LEN)) ||
            (ndl->igtk_required && (!ndl->igtk_set || ndl->igtk_len != NAN_ND_GTK_LEN)) ||
            (ndl->bigtk_required && (!ndl->bigtk_set || ndl->bigtk_len != NAN_ND_GTK_LEN))) {
            esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_FAILED,
                0, ndp_id, ESP_ERR_INVALID_RESPONSE, 0, peer_nmi);
            nan_ndp_confirm_teardown(peer_nmi, ndp_id);
            goto done;
        }
        if (!ndl->ptk_set''')
        s = replace(s, '''        ESP_LOGE(TAG, "%s: NAN netif is NULL", __func__);
        goto done;''', '''        ESP_LOGE(TAG, "%s: NAN netif is NULL", __func__);
        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_FAILED,
            0, ndp_id, ESP_ERR_INVALID_STATE, 0, peer_nmi);
        nan_ndp_confirm_teardown(peer_nmi, ndp_id);
        goto done;''')
        s = replace(s, '''    if (esp_wifi_register_if_rxcb(driver,  esp_netif_receive, s_nan_ctx.nan_netif) != ESP_OK) {
        ESP_LOGE(TAG, "%s: esp_wifi_register_if_rxcb failed", __func__);
        goto done;
    }''', '''    esp_err_t rx_error = esp_wifi_register_if_rxcb(driver, esp_netif_receive, s_nan_ctx.nan_netif);
    if (rx_error != ESP_OK) {
        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_FAILED,
            0, ndp_id, rx_error, 0, peer_nmi);
        nan_ndp_confirm_teardown(peer_nmi, ndp_id);
        goto done;
    }''')
        before = '''    /* Allocate the confirm event before installing the pairwise key, so an
     * allocation failure can tear the NDP down without leaving a stale key
     * bound to peer_ndi in the MAC's key store. */
    size_t evt_data_len = sizeof(wifi_event_ndp_confirm_t) + ssi_len;
    wifi_event_ndp_confirm_t *evt = (wifi_event_ndp_confirm_t *)os_zalloc(evt_data_len);
    if (!evt) {
        ESP_LOGE(TAG, "Failed to allocate for event, terminate NDP");
        nan_ndp_confirm_teardown(peer_nmi, ndp_id);
        goto done;
    }'''
        s = replace(s, before, '''    /* Protocol completion is independent of optional event allocation. The
     * stack header is valid through the synchronous netif/control calls;
     * SSI remains borrowed from peer_info until this callback returns. */
    wifi_event_ndp_confirm_t completion = {0};
    wifi_event_ndp_confirm_t *evt = &completion;
    size_t evt_data_len = sizeof(*evt) + (ssi ? ssi_len : 0);''')
        s = replace(s, '            os_free(evt);\n', '', count=2)
        for key in ('ND-TK', 'NM-TK'):
            line = '            ESP_LOGE(TAG, "NDP confirm: failed to install ' + key + ' (ndp_id=%d, ret=%d)", ndp_id, ret);'
            s = replace(s, line, line + '\n'
                '            esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_FAILED,\n'
                '                0, ndp_id, ret, 0, peer_nmi);')
        for text, error in (
                ('NDP confirm: own GTK (TX) install failed (ndp_id=%d, ret=%d)", ndp_id, gret);', 'gret'),
                ('NDP confirm: peer GTK (RX) install failed (ndp_id=%d, ret=%d)", ndp_id, gret);', 'gret'),
                ('NDP confirm: peer IGTK (RX) install failed, rc=0x%x (ndp_id=%d)", r, ndp_id);', 'r'),
                ('NDP confirm: peer BIGTK (RX) install failed, rc=0x%x (ndp_id=%d)", r, ndp_id);', 'r'),
                ('NDP confirm: peer IGTK len=%d unsupported (BIP-CMAC-128 only); skipping",\n                         ndl->igtk_len);', 'ESP_ERR_INVALID_RESPONSE'),
                ('NDP confirm: peer BIGTK len=%d unsupported (BIP-CMAC-128 only); skipping",\n                         ndl->bigtk_len);', 'ESP_ERR_INVALID_RESPONSE')):
            s = replace(s, text, text + '\n'
                '                esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_FAILED,\n'
                '                    0, ndp_id, ' + error + ', 0, peer_nmi);\n'
                '                nan_ndp_confirm_teardown(peer_nmi, ndp_id);\n'
                '                goto done;')
        s = replace(s, '''         * bound to the peer NDI. Best-effort — a GTK install failure must not
         * tear down the working unicast datapath. */''', '''         * bound to the peer NDI. Failed negotiated key installation prevents
         * publishing a successful protected connection. */''')
        s = replace(s, '''        memcpy(evt->ssi, ssi, ssi_len);
        evt->ssi_len = ssi_len;
        ESP_LOG_BUFFER_HEXDUMP(TAG, ssi, ssi_len, ESP_LOG_DEBUG);''',
            '        evt->ssi_len = ssi_len;')
        s = replace(s, '''    os_event_group_set_bits(nan_event_group, NDP_ACCEPTED);
    nan_app_post_event(WIFI_EVENT_NDP_CONFIRM, evt, evt_data_len);
    os_free(evt);''', '''    os_event_group_set_bits(nan_event_group, NDP_ACCEPTED);
    esp32qjs_nan_ndp_control(ESP32_MQUICKJS_NAN_SDK_NDP_ACCEPTED, 0, ndp_id, status,
        peer_nmi, peer_ndi, own_ndi, evt->ipv6_identifier, ssi, ssi_len);
    wifi_event_ndp_confirm_t *observation = os_zalloc(evt_data_len);
    if (!observation) {
        esp32qjs_nan_event_dropped();
        return;
    }
    memcpy(observation, evt, sizeof(*evt));
    if (evt->ssi_len) memcpy(observation->ssi, ssi, evt->ssi_len);
    nan_app_post_event(WIFI_EVENT_NDP_CONFIRM, observation, evt_data_len);
    os_free(observation);''')
        return s
    source = function(source, 'nan_app_ndp_confirm_cb', confirm)
    source = function(source, 'nan_app_ndp_confirm_cb', lambda s: replace(s,
        '        nan_reset_ndl(ndp_id, false);',
        '        /* Native delete owns the final host/key release after its callbacks return. */'))
    source = function(source, 'nan_app_ndp_terminated_cb', lambda s: replace(s,
        '    nan_reset_ndl(ndp_id, false);',
        '    /* Keep host/key state until the complete native peer deletion returns. */'))
    source = function(source, 'nan_app_ndp_indication_cb', lambda s: replace(s,
        '    esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_REQUEST, pub_id, ndp_id, ndp_resp_needed, 0, peer_nmi);',
        '    esp32qjs_nan_ndp_control(ESP32_MQUICKJS_NAN_SDK_NDP_REQUEST, pub_id, ndp_id, ndp_resp_needed,\n'
        '        peer_nmi, peer_ndi, NULL, NULL, ssi, ssi_len);'))
    source = function(source, 'nan_app_ndp_indication_cb', lambda s: replace(s,
        '            nan_reset_ndl(ndp_id, false);',
        '            /* A queued reject is not native retirement; native delete releases host state. */', count=2))
    source = function(source, 'esp_wifi_nan_datapath_req', lambda s: replace(s,
        '''        NAN_DATA_LOCK();
        nan_ndl_release(ndp_id);
        NAN_DATA_UNLOCK();''', '''        /* Waiting ended; the native request, retry timer and TX callbacks
         * have not necessarily ended. Only termination/STOP may release NDL. */
        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_FAILED,
            0, ndp_id, ESP_ERR_TIMEOUT, 0, req->peer_mac);'''))
    return source


def patch_source(source: str) -> str:
    if hashlib.sha256(source.encode()).hexdigest() != REVIEWED['wifi_apps/nan_app/src/nan_app.c']:
        raise ValueError('Unreviewed NAN application source')
    source = replace(source, '#include "nan_i.h"',
        '#include "nan_i.h"\n#include "esp32_mquickjs_wifi_nan_sdk.inc"')
    source = function(source, 'esp_wifi_nan_usd_start', lambda s: '''esp_err_t esp_wifi_nan_usd_start(void)
{
    esp32_mquickjs_wifi_nan_usd_sdk_status_t status;
    esp32_mquickjs_wifi_nan_usd_sdk_status(&status);
    if (s_usd_in_progress)
        return status.error ? status.error : status.closing ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (status.initialized || status.identity || status.registered_handlers)
        return ESP_ERR_INVALID_STATE;
    wifi_mode_t mode;
    esp_err_t error = esp_wifi_get_mode(&mode);
    if (error != ESP_OK) return error;
    if (mode == WIFI_MODE_NAN) return ESP_ERR_INVALID_STATE;
    if (mode != WIFI_MODE_STA) {
        error = esp_wifi_set_mode(WIFI_MODE_STA);
        if (error != ESP_OK) return error;
    }
    error = esp_nan_usd_init();
    if (error == ESP_OK) s_usd_in_progress = true;
    return error;
}
''')
    source = function(source, 'esp_wifi_nan_usd_stop', lambda s: '''esp_err_t esp_wifi_nan_usd_stop(void)
{
    /* A failed start can still own an engine or registered callbacks. Deinit
     * checks its own ledger and only repeats unfinished unregister suffixes. */
    esp_err_t error = esp_nan_usd_deinit();
    if (error == ESP_OK) s_usd_in_progress = false;
    return error;
}
''')
    for name, service_id in (('nan_app_service_match_cb', 'sub_id'),
                            ('nan_app_replied_cb', 'pub_id'), ('nan_app_receive_cb', 'svc_id'),
                            ('nan_app_ndp_indication_cb', 'pub_id')):
        source = function(source, name, lambda s, service_id=service_id: replace(s, '\n{\n',
            '\n{\n    esp32qjs_nan_service_guard_t guard __attribute__((cleanup(esp32qjs_nan_service_leave))) =\n'
            '        esp32qjs_nan_service_enter(' + service_id + ');\n'
            '    if (!guard.entered) return;\n'))
    source = function(source, 'nan_app_post_event', lambda s: replace(s,
        '    g_wifi_osi_funcs._event_post(WIFI_EVENT, event_id, event_data, event_data_size, OSI_FUNCS_TIME_BLOCKING);',
        '    esp32qjs_nan_event(event_id, event_data, event_data_size);\n'
        '    if (g_wifi_osi_funcs._event_post(WIFI_EVENT, event_id, event_data, event_data_size, 0) != ESP_OK)\n'
        '        esp32qjs_nan_event_dropped();'))

    def init(s):
        s = replace(s, '''    if (nan_event_group) {
        os_event_group_delete(nan_event_group);
        nan_event_group = NULL;
    }
    nan_event_group = os_event_group_create();''', '''    /* SDK init may be revisited after a partial outer Wi-Fi initialization.
     * Never replace a live lock/event group referenced by native callbacks. */
    if (nan_event_group || s_nan_data_lock) return;
    nan_event_group = os_event_group_create();
    if (!nan_event_group) {
        ESP_LOGE(TAG, "Failed to create NAN event group");
        return;
    }''')
        return s
    source = function(source, 'esp_nan_app_init', init)

    def start(s):
        # RAM-only sessions must not import a prior persistent identity or key
        # cache. This does not erase NVS or add a development-state migration.
        s = replace(s, '} else if (esp_wifi_nan_load_saved_creds(',
            '} else if (nan_cfg->use_nvs_for_caching && esp_wifi_nan_load_saved_creds(')
        s = replace(s, 'if (!s_nan_data_lock)', 'if (!s_nan_data_lock || !nan_event_group)')
        s = replace(s, 'ESP_LOGE(TAG, "NAN Data lock doesn\'t exist");\n        return ESP_FAIL;',
            'ESP_LOGE(TAG, "NAN native initialization incomplete");\n        return ESP_ERR_NO_MEM;')
        s = replace(s, '''    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "Starting wifi failed");
        NAN_DATA_LOCK();
        s_nan_ctx.nan_netif = NULL;
        NAN_DATA_UNLOCK();
        return ESP_FAIL;
    }''', '''    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Starting wifi failed");
        return ret;
    }''')
        s = replace(s, 'portMAX_DELAY);', 'pdMS_TO_TICKS(ESP32QJS_NAN_LIFECYCLE_WAIT_MS));')
        s = replace(s, '''    if (!(bits & NAN_STARTED_BIT)) {
        NAN_DATA_LOCK();
        s_nan_ctx.nan_netif = NULL;
        NAN_DATA_UNLOCK();
        return ESP_FAIL;
    }''', '''    if (!(bits & NAN_STARTED_BIT)) {
        /* A timeout is not driver termination. Keep the netif/context for a
         * late START and explicit STOP under the original Radio owner. */
        return ESP_ERR_TIMEOUT;
    }''')
        return s
    source = function(source, 'esp_wifi_nan_sync_start', start)

    def split_start(s):
        marker = '    os_event_group_clear_bits(nan_event_group, NAN_STARTED_BIT);'
        before, after = s.split(marker)
        prepare = replace(before, 'esp_wifi_nan_sync_start(', 'esp32_mquickjs_wifi_nan_sdk_sync_prepare(')
        prepare = replace(prepare, '''        ESP_LOGI(TAG, "NAN already started");
        NAN_DATA_UNLOCK();
        return ESP_OK;''', '''        NAN_DATA_UNLOCK();
        return ESP_ERR_INVALID_STATE;''')
        prepare = replace(prepare, '    wifi_mode_t mode;', '''    if (s_usd_in_progress) return ESP_ERR_INVALID_STATE;
    wifi_mode_t mode;''')
        # Keep the public SDK entry point, sharing its reviewed preparation
        # body with Radio. Only Radio's path separates driver submission.
        return prepare + marker + '\n    s_esp32qjs_nan_start_error = ESP_OK;\n    return ESP_OK;\n}\n\n' + '''esp_err_t esp_wifi_nan_sync_start(const wifi_nan_sync_config_t *nan_cfg)
{
    if (!nan_cfg) return ESP_ERR_INVALID_ARG;
    if (s_nan_data_lock && nan_event_group) {
        NAN_DATA_LOCK();
        bool started = (s_nan_ctx.state & NAN_STARTED_BIT) != 0;
        NAN_DATA_UNLOCK();
        if (started) return ESP_OK;
    }
    esp_err_t ret = esp32_mquickjs_wifi_nan_sdk_sync_prepare(nan_cfg);
    if (ret != ESP_OK) return ret;
''' + after
    source = function(source, 'esp_wifi_nan_sync_start', split_start)

    source = function(source, 'nan_clear_app_default_handlers', lambda s: '''static esp_err_t nan_clear_app_default_handlers(void)
{
    if (!s_app_default_handlers_set) return ESP_OK;
    esp_err_t error = esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, nan_app_action_got_ipv6);
    if (error == ESP_OK) s_app_default_handlers_set = false;
    return error;
}
''')
    source = function(source, 'nan_set_app_default_handlers', lambda s: '''static esp_err_t nan_set_app_default_handlers(void)
{
    if (s_app_default_handlers_set) return ESP_OK;
    esp_err_t error = esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, nan_app_action_got_ipv6, NULL);
    if (error == ESP_OK) s_app_default_handlers_set = true;
    return error;
}
''')
    source = function(source, 'esp_nan_action_start', lambda s: replace(s,
        '    nan_set_app_default_handlers();', '''    if (!s_nan_data_lock || !nan_event_group) return;
    esp_err_t handler_error = nan_set_app_default_handlers();
    s_esp32qjs_nan_start_error = handler_error;
    if (handler_error != ESP_OK) {
        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_STARTED, 0, 0, handler_error, 0, NULL);
        return;
    }'''))
    source = function(source, 'esp_nan_action_start', lambda s: replace(s,
        '    nan_security_install_own_group_integrity_keys();',
        '''    s_esp32qjs_nan_start_error = esp32_mquickjs_wifi_nan_sdk_security_install_group_keys();
    if (s_esp32qjs_nan_start_error != ESP_OK) {
        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_STARTED, 0, 0, s_esp32qjs_nan_start_error, 0, NULL);
        return;
    }'''))

    def stop(s):
        s = replace(s, '''    if (!(s_nan_ctx.state & NAN_STARTED_BIT)) {
        ESP_LOGE(TAG, "NAN isn't started");''', '''    if (!(s_nan_ctx.state & NAN_STARTED_BIT)) {
        if (s_nan_ctx.state & NAN_STOPPED_BIT) {
            NAN_DATA_UNLOCK();
            goto stopped;
        }
        ESP_LOGE(TAG, "NAN isn't started");''')
        s = replace(s, 'portMAX_DELAY);', 'pdMS_TO_TICKS(ESP32QJS_NAN_LIFECYCLE_WAIT_MS));')
        s = replace(s, '''    if (!(bits & NAN_STOPPED_BIT)) {
        return ESP_FAIL;
    }

    NAN_DATA_LOCK();''', '''    if (!(bits & NAN_STOPPED_BIT)) {
        return ESP_ERR_TIMEOUT;
    }

stopped:
    NAN_DATA_LOCK();''')
        return s
    source = function(source, 'esp_wifi_nan_sync_stop', stop)
    source = function(source, 'esp_wifi_nan_sync_stop', lambda s: s + '''
/* This check follows Radio's START/default-handler fence. A driver START event
 * alone cannot prove the SDK installed its protocol callbacks. */
esp_err_t esp32_mquickjs_wifi_nan_sdk_sync_ready(void)
{
    if (!s_nan_data_lock || !nan_event_group) return ESP_ERR_NO_MEM;
    if (s_esp32qjs_nan_start_error != ESP_OK) return s_esp32qjs_nan_start_error;
    NAN_DATA_LOCK();
    bool ready = (s_nan_ctx.state & NAN_STARTED_BIT) && s_nan_ctx.nan_netif && s_app_default_handlers_set;
    NAN_DATA_UNLOCK();
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_nan_sdk_sync_reset(void)
{
    /* Caller owns physical STOP/default-loop retirement, or has never
     * attempted START. A retained handler flag prevents lost cleanup duty. */
    esp_err_t error = nan_clear_app_default_handlers();
    if (error != ESP_OK) return error;
    if (!s_nan_data_lock) return ESP_OK;
    NAN_DATA_LOCK();
    if (s_nan_ctx.state & NAN_STARTED_BIT) {
        NAN_DATA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }
    nan_reset_service(0, true);
    nan_reset_ndl(0, true);
#if CONFIG_ESP_WIFI_NAN_SECURITY
    esp32_mquickjs_wifi_nan_sdk_security_clear_pending();
#endif
    forced_memzero(&s_nan_ctx, sizeof(s_nan_ctx));
    NAN_DATA_UNLOCK();
    return ESP_OK;
}
''')
    source = function(source, 'esp_nan_action_start', lambda s: replace(s,
        '    os_event_group_set_bits(nan_event_group, NAN_STARTED_BIT);',
        '    os_event_group_set_bits(nan_event_group, NAN_STARTED_BIT);\n'
        '    esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_STARTED, 0, 0, ESP_OK, 0, NULL);'))
    source = function(source, 'esp_nan_action_stop', lambda s: replace(s,
        '    os_event_group_set_bits(nan_event_group, NAN_STOPPED_BIT);',
        '    os_event_group_set_bits(nan_event_group, NAN_STOPPED_BIT);\n'
        '    esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_STOPPED, 0, 0, ESP_OK, 0, NULL);'))
    source = function(source, 'nan_action_txdone_cb', lambda s: replace(s,
        '    if (nan_event_group && s_fup_context == context) {',
        '    if (nan_event_group && s_fup_context == context) {\n'
        '        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_TX_DONE, 0, 0, tx_status, context, NULL);'))

    def terminated(s):
        s = replace(s, '    os_event_group_set_bits(nan_event_group, NDP_TERMINATED);\n', '')
        return replace(s, '    NAN_DATA_UNLOCK();',
            '    NAN_DATA_UNLOCK();\n'
            '    os_event_group_set_bits(nan_event_group, NDP_TERMINATED);\n'
            '    esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_TERMINATED, 0, ndp_id, reason, 0, init_ndi);')
    source = function(source, 'nan_app_ndp_terminated_cb', terminated)
    source = function(source, 'nan_ndp_confirm_teardown', lambda s: replace(s,
        '    os_event_group_set_bits(nan_event_group, NDP_REJECTED);',
        '    os_event_group_set_bits(nan_event_group, NDP_REJECTED);\n'
        '    esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_REJECTED, 0, ndp_id, ESP_FAIL, 0, peer_nmi);'))
    source = function(source, 'nan_app_ndp_confirm_cb', lambda s: replace(replace(s,
        '    wifi_netif_driver_t driver = esp_netif_get_io_driver(s_nan_ctx.nan_netif);\n\n', ''),
        '    /* If interface not ready when started, rxcb to be registered on connection */',
        '    wifi_netif_driver_t driver = esp_netif_get_io_driver(s_nan_ctx.nan_netif);\n\n'
        '    /* If interface not ready when started, rxcb to be registered on connection */'))
    source = function(source, 'nan_app_ndp_confirm_cb', lambda s: replace(s,
        '        os_event_group_set_bits(nan_event_group, NDP_REJECTED);',
        '        os_event_group_set_bits(nan_event_group, NDP_REJECTED);\n'
        '        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_REJECTED, 0, ndp_id, status, 0, peer_nmi);'))
    source = function(source, 'nan_app_ndp_indication_cb', lambda s: replace(s,
        '    size_t evt_data_len = sizeof(wifi_event_ndp_indication_t) + ssi_len;',
        '    esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_REQUEST, pub_id, ndp_id, ndp_resp_needed, 0, peer_nmi);\n'
        '    size_t evt_data_len = sizeof(wifi_event_ndp_indication_t) + ssi_len;'))
    source = function(source, 'nan_app_ndp_indication_cb', lambda s: replace(s,
        '    NAN_DATA_LOCK();\n    struct own_svc_info *p_own_svc = nan_find_own_svc(pub_id);',
        '''    if (esp32qjs_nan_is_discovery_only(pub_id)) {
        /* Discovery-only publication grants no datapath permission. Reject
         * on the current WiFi task, before creating host NDL/security state;
         * the native response call must remain outside NAN_DATA_LOCK. */
        ndp_resp.ndp_id = ndp_id;
        MACADDR_COPY(ndp_resp.peer_mac, peer_nmi);
        esp_err_t error = esp_nan_internal_datapath_resp(&ndp_resp, (uint8_t *)&own_ipv6.u_addr.ip6.addr[2]);
        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_DISCOVERY_NDP_DENIED, pub_id, ndp_id, error, 0, peer_nmi);
        return;
    }
    NAN_DATA_LOCK();
    struct own_svc_info *p_own_svc = nan_find_own_svc(pub_id);'''))

    for name, arg, failure in [('esp_wifi_nan_publish_service', 'publish_cfg', '0'),
            ('esp_wifi_nan_subscribe_service', 'subscribe_cfg', '0'),
            ('esp_wifi_nan_send_message', 'fup_params', 'ESP_ERR_INVALID_ARG')]:
        def validate(s, arg=arg, failure=failure):
            s = s.replace('\n{\n', '\n{\n    if (!' + arg + ') return ' + failure + ';\n', 1)
            return replace(s, 'if (' + arg + '->ssi &&',
                'if (' + arg + '->ssi && ' + arg + '->ssi_len >= sizeof(wifi_nan_wfa_ssi_t) &&')
        source = function(source, name, validate)

    def service_start(s, kind, variable, instance):
        api = 'esp_wifi_nan_' + kind + '_service'
        core = 'esp32qjs_nan_' + kind + '_service'
        config_type = 'wifi_nan_' + kind + '_cfg_t'
        declaration = 'uint8_t ' + api + '(const ' + config_type + ' *' + variable + ')'
        s = replace(s, declaration,
            'static uint8_t ' + core + '(const ' + config_type + ' *' + variable + ', esp_err_t *native_error)')
        s = replace(s, '\n{\n', '\n{\n    *native_error = ESP_FAIL;\n')
        s = replace(s, '    if (!cfg) {\n', '    if (!cfg) {\n        *native_error = ESP_ERR_NO_MEM;\n')
        s = replace(s, '        if (!cfg->pairing) {\n',
            '        if (!cfg->pairing) {\n            *native_error = ESP_ERR_NO_MEM;\n')
        call = 'esp_nan_internal_' + kind + '_service(cfg, (uint8_t *) &' + instance + ', false)'
        s = replace(s, '    if (' + call + ' != ESP_OK) {',
            '    /* Publish host policy before the WiFi task can expose this service. */\n'
            '    nan_finalize_own_svc(cfg->service_name, NAN_SVC_ID_PENDING, ' +
            ('cfg->ndp_resp_needed' if kind == 'publish' else 'false') + ', service_id);\n'
            '    /* Radio serializes service mutations. The preclaimed host slot\n'
            '     * stays live, but native callbacks must be able to acquire\n'
            '     * NAN_DATA_LOCK while the driver worker waits on its ioctl. */\n'
            '    NAN_DATA_UNLOCK();\n'
            '    *native_error = ' + call + ';\n'
            '    NAN_DATA_LOCK();\n'
            '    if (*native_error != ESP_OK) {')
        return s + '\nuint8_t ' + api + '(const ' + config_type + ' *config)\n{\n' + \
            '    esp_err_t error;\n    return ' + core + '(config, &error);\n}\n' + \
            '\n#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE\n' + \
            'esp_err_t esp32_mquickjs_wifi_nan_sdk_' + kind + '(const ' + config_type + ' *config, uint8_t *id)\n{\n' + \
            '    if (!config || !id || *id || !config->service_name[0] ||\n' + \
            '        !memchr(config->service_name, 0, sizeof(config->service_name)) ||\n' + \
            '        !memchr(config->matching_filter, 0, sizeof(config->matching_filter))) return ESP_ERR_INVALID_ARG;\n' + \
            '    if (config->usd_discovery_flag || s_usd_in_progress || !s_nan_data_lock || !nan_event_group)\n' + \
            '        return ESP_ERR_INVALID_STATE;\n' + \
            '    esp_err_t error;\n    *id = ' + core + '(config, &error);\n' + \
            '    return *id ? ESP_OK : error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;\n}\n#endif\n'
    for kind, variable, instance in [('publish', 'publish_cfg', 'pub_id'), ('subscribe', 'subscribe_cfg', 'sub_id')]:
        source = function(source, 'esp_wifi_nan_' + kind + '_service',
            lambda s, kind=kind, variable=variable, instance=instance: service_start(s, kind, variable, instance))

    def service_cancel(s):
        s = replace(s, '    NAN_DATA_LOCK();', '    esp_err_t cancel_error = ESP_FAIL;\n    NAN_DATA_LOCK();')
        start = s.index('    if (p_own_svc->type == ESP_NAN_PUBLISH) {')
        end = s.index('\nfail:\n', start)
        s = s[:start] + '''    uint8_t type = p_own_svc->type;
    /* Do not wait for WiFi-task cancellation while holding the mutex needed
     * by callbacks on that task. Radio owns serialization and freezes the
     * exact service before entering this path. Never use p_own_svc after the
     * unlock; only the saved type and scalar native ID cross the driver call. */
    NAN_DATA_UNLOCK();
    if (type == ESP_NAN_PUBLISH)
        cancel_error = esp_nan_internal_publish_service(NULL, &service_id, true);
    else if (type == ESP_NAN_SUBSCRIBE)
        cancel_error = esp_nan_internal_subscribe_service(NULL, &service_id, true);
    NAN_DATA_LOCK();
    if (cancel_error != ESP_OK) goto fail;
    nan_reset_service(service_id, false);
    goto done;
''' + s[end:]
        s = replace(s, 'fail:\n    NAN_DATA_UNLOCK();\n    return ESP_FAIL;',
            'fail:\n    NAN_DATA_UNLOCK();\n    return cancel_error;')
        return s + '''
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
esp_err_t esp32_mquickjs_wifi_nan_sdk_cancel_service(uint8_t id)
{
    if (!id) return ESP_ERR_INVALID_ARG;
    if (s_usd_in_progress || !s_nan_data_lock || !nan_event_group) return ESP_ERR_INVALID_STATE;
    if (esp32_mquickjs_wifi_nan_ndp_service_busy(id)) return ESP_ERR_INVALID_STATE;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    esp_err_t pairing_error = esp32_mquickjs_wifi_nan_pairing_service_idle(id);
    if (pairing_error != ESP_OK) return pairing_error;
#endif
    esp_err_t error = esp32_mquickjs_wifi_nan_sdk_service_quiesce(id);
    if (error != ESP_OK) return error;
    /* Success leaves this ID frozen until actual TX buffers retire. A failed
     * native cancel leaves the old host record and its security state live. */
    return esp_wifi_nan_cancel_service(id);
}
#endif
'''
    source = function(source, 'esp_wifi_nan_cancel_service', service_cancel)
    # Bind the real ID on the WiFi task before the ioctl completes and its next
    # RX/TX callback can run. Merely doing this on the requesting worker loses
    # first matches and can observe pending secure services as open services.
    for kind, service_type in [('publish', 'ESP_NAN_PUBLISH'), ('subscribe', 'ESP_NAN_SUBSCRIBE')]:
        config_type = 'wifi_nan_' + kind + '_cfg_t'
        name = 'nan_start_' + kind + '_service'
        source += '\n#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE && CONFIG_IDF_TARGET_ESP32C5\n' + \
            'extern int __real_' + name + '(const ' + config_type + ' *config, uint8_t *id);\n' + \
            'int __wrap_' + name + '(const ' + config_type + ' *config, uint8_t *id)\n{\n' + \
            '    int result = __real_' + name + '(config, id);\n' + \
            '    if (result == 0 && id && *id) {\n' + \
            '        NAN_DATA_LOCK();\n' + \
            '        struct own_svc_info *service = nan_find_own_svc_by_name(config->service_name);\n' + \
            '        bool bound = service && service->svc_id == NAN_SVC_ID_PENDING && service->type == ' + service_type + ';\n' + \
            '        if (bound) service->svc_id = *id;\n' + \
            '        NAN_DATA_UNLOCK();\n' + \
            '        if (bound) esp32qjs_nan_set_discovery_only(*id, !config->datapath_reqd);\n' + \
            '        esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_SERVICE_BOUND, *id, 0,\n' + \
            '            bound ? ESP_OK : ESP_ERR_INVALID_RESPONSE, ' + service_type + ', NULL);\n' + \
            '    }\n    return result;\n}\n#endif\n'
    for name, arg, failure in [('esp_wifi_nan_datapath_req', 'req', '0'),
            ('esp_wifi_nan_datapath_resp', 'resp', 'ESP_ERR_INVALID_ARG'),
            ('esp_wifi_nan_datapath_end', 'req', 'ESP_ERR_INVALID_ARG')]:
        source = function(source, name, lambda s, arg=arg, failure=failure:
            s.replace('\n{\n', '\n{\n    if (!' + arg + ') return ' + failure + ';\n', 1))
    source = function(source, 'esp_wifi_nan_datapath_resp', lambda s: replace(s,
        '            NAN_DATA_UNLOCK();\n            goto fail;',
        '            NAN_DATA_UNLOCK();\n            return err;'))

    source += '''
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
esp_err_t esp32_mquickjs_wifi_nan_sdk_send(wifi_nan_followup_params_t *params, uint32_t *context)
{
    if (!params || !context || *context || !params->inst_id || !params->peer_inst_id ||
        (params->peer_mac[0] & 1) || MACADDR_EQUAL(params->peer_mac, null_mac) ||
        params->ssi_len > ESP_WIFI_MAX_FUP_SSI_LEN || (params->ssi_len && !params->ssi))
        return ESP_ERR_INVALID_ARG;
    if (params->vendor_ie && (params->vendor_ie->body_len > NAN_VENDOR_IE_MAX_BODY_LEN ||
        (params->vendor_ie->body_len && !params->vendor_ie->body))) return ESP_ERR_INVALID_ARG;
    if (s_usd_in_progress || !s_nan_data_lock || !nan_event_group) return ESP_ERR_INVALID_STATE;
    NAN_DATA_LOCK();
    bool valid = nan_find_own_svc(params->inst_id) &&
        nan_find_peer_svc(params->inst_id, params->peer_inst_id, params->peer_mac);
    NAN_DATA_UNLOCK();
    if (!valid) return ESP_ERR_NOT_FOUND;
    /* The exact framework owner retains these inputs. Native completion is
     * tracked around the real callback and recycler, not s_fup_context or
     * shared event bits. Radio serializes this ioctl with service mutations. */
    return esp_nan_internal_send_followup(params, context, NULL);
}
#endif
'''

    source = datapath_control(source)
    source = pairing_cache_control(source)
    source += '\n#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_NAN_SYNC_ENABLE\n' \
        '#include "esp32_mquickjs_wifi_nan_sdk_ndp.inc"\n#endif\n'

    # Keys remain subject to the original authentication/install checks, but
    # must never be exported by an SDK debug log when NAN is enabled.
    source, count = re.subn(r'^\s*ESP_LOG_BUFFER_HEXDUMP\("(?:ND-TK|ND-GTK|PEER ND-IGTK|PEER ND-BIGTK)"[^\n]*\n', '\n', source, flags=re.M)
    if count != 4:
        raise ValueError('Unexpected NAN key logging sites')
    return source


def pairing_cache_control(source: str) -> str:
    # Explicit service policy must not silently disable setup because a cache
    # happens to contain credentials for the same service hash.
    source = replace(source, "    if (cfg->pairing && cfg->pairing->npk_nik_caching && nan_check_paired_service_hash(service_id)) {\n"
        "        cfg->pairing->pairing_setup = false;\n    }\n",
        "    /* Keep the caller's explicit pairing setup policy. */\n", count=2)
    source = function(source, 'nan_app_find_paired_slot_locked', lambda s: '''static struct nan_paired_peer *nan_app_find_paired_slot_locked(const uint8_t *peer_nmi)
{
    uint8_t service_id = 0;
    if (!esp32_mquickjs_wifi_nan_pairing_services(peer_nmi, &service_id, NULL)) return NULL;
    for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        struct nan_paired_peer *record = &s_nan_ctx.paired_peers[i];
        if (record->valid && esp32_mquickjs_wifi_nan_pairing_cache_service(i) == service_id &&
            !memcmp(record->peer_nmi, peer_nmi, MACADDR_LEN)) return record;
    }
    return NULL;
}
''')
    source = function(source, 'nan_app_alloc_paired_slot_locked', lambda s:
        s[:s.index('    /* Evict oldest entry')] + '    return NULL; /* Do not evict another service or connection. */\n}\n')
    source = function(source, 'nan_app_register_paired_peer', lambda s: replace(replace(s,
        '    NAN_DATA_LOCK();',
        '    uint8_t service_id = 0;\n'
        '    if (!esp32_mquickjs_wifi_nan_pairing_services(peer_nmi, &service_id, NULL)) return ESP_ERR_INVALID_STATE;\n'
        '    NAN_DATA_LOCK();'),
        '    forced_memzero(p->nd_pmk, sizeof(p->nd_pmk));',
        '    if (!p) { NAN_DATA_UNLOCK(); return ESP_ERR_NO_MEM; }\n'
        '    if (!esp32_mquickjs_wifi_nan_pairing_cache_bind((unsigned)(p - s_nan_ctx.paired_peers), service_id)) {\n'
        '        NAN_DATA_UNLOCK(); return ESP_ERR_INVALID_STATE;\n    }\n'
        '    forced_memzero(p->nd_pmk, sizeof(p->nd_pmk));'))
    source = function(source, 'nan_app_remove_paired_peer', lambda s: replace(s,
        '    if (p) {',
        '    if (p) {\n        esp32_mquickjs_wifi_nan_pairing_cache_bind((unsigned)(p - s_nan_ctx.paired_peers), 0);'))
    source = function(source, 'nan_app_clear_paired_peers', lambda s: replace(s,
        '        forced_memzero(s_nan_ctx.paired_peers[i].nd_pmk,',
        '        esp32_mquickjs_wifi_nan_pairing_cache_bind((unsigned)i, 0);\n'
        '        forced_memzero(s_nan_ctx.paired_peers[i].nd_pmk,'))
    return source


def patch_security_source(source: str) -> str:
    source = replace(source, '#include "nan_i.h"',
        '#include "nan_i.h"\n#include "esp32_mquickjs_wifi_nan_ndp.h"\n'
        '#include "esp32_mquickjs_wifi_nan_pasn_sdk.h"')
    source = function(source, 'nan_security_fill_from_paired_cache', lambda s: replace(s,
        'nan_app_find_paired_peer(peer_nmi)', 'esp32_mquickjs_wifi_nan_pairing_cached_peer(ndl->publisher_id, peer_nmi)'))
    source = function(source, 'nan_security_populate_initiator_ndl', lambda s: replace(s,
        'nan_app_find_paired_peer(peer_nmi)', 'esp32_mquickjs_wifi_nan_pairing_cached_peer(own_svc->svc_id, peer_nmi)'))
    # This module logs KCK and group keys as well as complete key descriptors.
    # Keep textual failure diagnostics; no binary security material is logged.
    source, count = re.subn(r'^[ \t]*ESP_LOG_BUFFER_HEXDUMP\([^;]*\);[ \t]*\n', '', source, flags=re.M)
    if count != 17:
        raise ValueError('Unexpected NAN security binary logging sites')
    source = function(source, 'nan_security_install_own_group_integrity_keys', lambda s: '''esp_err_t esp32_mquickjs_wifi_nan_sdk_security_install_group_keys(void)
{
    if (!s_nan_ctx.group_mgmt_prot) return ESP_OK;
    uint8_t own_nmi[6];
    esp_err_t error = esp_wifi_get_mac(WIFI_IF_NAN, own_nmi);
    if (error != ESP_OK) return error;
    error = nan_ensure_own_igtk();
    if (error != ESP_OK) return error;
    if (!s_nan_ctx.own_igtk_set) return ESP_ERR_INVALID_STATE;
    error = esp_wifi_set_nan_key_internal(NAN_WIFI_WPA_ALG_BIP_CMAC_128,
        own_nmi, s_nan_ctx.own_igtk_keyid, 1, s_nan_ctx.own_igtk_ipn, sizeof(s_nan_ctx.own_igtk_ipn),
        s_nan_ctx.own_igtk, NAN_ND_GTK_LEN, NAN_KEY_ND_IGTK);
    if (error != ESP_OK) return error;
    error = nan_ensure_own_bigtk();
    if (error != ESP_OK) return error;
    if (!s_nan_ctx.own_bigtk_set) return ESP_ERR_INVALID_STATE;
    return esp_wifi_set_nan_key_internal(NAN_WIFI_WPA_ALG_BIP_CMAC_128,
        own_nmi, s_nan_ctx.own_bigtk_keyid, 1, s_nan_ctx.own_bigtk_bipn, sizeof(s_nan_ctx.own_bigtk_bipn),
        s_nan_ctx.own_bigtk, NAN_ND_GTK_LEN, NAN_KEY_ND_BIGTK);
}

void nan_security_install_own_group_integrity_keys(void)
{
    (void)esp32_mquickjs_wifi_nan_sdk_security_install_group_keys();
}
''')

    def derive(s):
        s = replace(s, '    esp_wifi_get_mac(WIFI_IF_NAN, r_addr);',
            '    if (esp_wifi_get_mac(WIFI_IF_NAN, r_addr) != ESP_OK) goto fail;')
        s = replace(s, '''            g_wifi_default_wpa_crypto_funcs.hmac_sha256_vector(pmk, ESP_WIFI_NAN_NDP_PMK_LEN,
                                                               4, addr_pmkid, len_pmkid, hash);''',
            '''            if (g_wifi_default_wpa_crypto_funcs.hmac_sha256_vector(pmk, ESP_WIFI_NAN_NDP_PMK_LEN,
                                                               4, addr_pmkid, len_pmkid, hash) != 0) goto fail;''')
        s = replace(s, '''    memset(out_derived, 0,
           sizeof(*out_derived) * ESP_WIFI_NAN_MAX_CREDS_PER_SVC);''',
            '''    forced_memzero(out_derived,
           sizeof(*out_derived) * ESP_WIFI_NAN_MAX_CREDS_PER_SVC);''', count=2)
        s = replace(s, '    return ESP_OK;',
            '    forced_memzero(hash, sizeof(hash));\n    return ESP_OK;')
        s = replace(s, 'fail:\n    forced_memzero(pmk, sizeof(pmk));',
            'fail:\n    forced_memzero(hash, sizeof(hash));\n    forced_memzero(pmk, sizeof(pmk));')
        return s
    source = function(source, 'nan_derive_security_params', derive)

    def match(s):
        s = replace(s, '''        g_wifi_default_wpa_crypto_funcs.hmac_sha256_vector(pmk, ESP_WIFI_NAN_NDP_PMK_LEN,
                                                           4, addr_pmkid, len_pmkid, hash);''',
            '''        if (g_wifi_default_wpa_crypto_funcs.hmac_sha256_vector(pmk, ESP_WIFI_NAN_NDP_PMK_LEN,
                                                           4, addr_pmkid, len_pmkid, hash) != 0) goto done;''')
        s = replace(s, 'done:\n    forced_memzero(pmk, sizeof(pmk));',
            'done:\n    forced_memzero(hash, sizeof(hash));\n    forced_memzero(pmkid, sizeof(pmkid));\n    forced_memzero(pmk, sizeof(pmk));')
        return s
    source = function(source, 'nan_security_service_match', match)
    def group_kdes(s):
        s = replace(s, '''    if (!want_gtk && !want_igtk && !want_bigtk) {
        return NAN_KEY_DESC_MIN_LEN;
    }''', '''    if (!want_gtk && !want_igtk && !want_bigtk) {
        return NAN_KEY_DESC_MIN_LEN; /* No group protection was negotiated. */
    }''')
        start = s.index('    if (!ndl->ptk_set)')
        s = s[:start] + replace(s[start:], 'return NAN_KEY_DESC_MIN_LEN;', 'return -1;', count=4)
        s = replace(s, 'int ret = NAN_KEY_DESC_MIN_LEN;', 'int ret = -1;')
        return s.replace('sending without group keys', 'cannot build negotiated group keys')
    source = function(source, 'nan_append_own_group_kdes', group_kdes)
    source = replace(source, '    n = nan_append_own_group_kdes(ndl, key_desc, buf_len - 4);',
        '''    n = nan_append_own_group_kdes(ndl, key_desc, buf_len - 4);
    if (n < 0) {
        esp32_mquickjs_wifi_nan_sdk_notice_t failure = {
            .kind = ESP32_MQUICKJS_NAN_SDK_NDP_FAILED, .ndp_id = ndp_id, .status = ESP_FAIL,
        };
        memcpy(failure.peer, ndl->peer_nmi, sizeof(failure.peer));
        NAN_DATA_UNLOCK();
        esp32_mquickjs_wifi_nan_ndp_notice(&failure);
        return 0;
    }''', count=2)
    def pending(s):
        s = replace(s, '\n{\n\n', '\n{\n    bool pmk_resolved = false;\n')
        s = replace(s, '            bool pmk_resolved = false;\n', '')
        s = replace(s, '    if (s_pending_m1.valid && s_pending_m1.pub_id == pub_id && ndl) {',
            '''    if (ndl && p_own_svc && p_own_svc->user_cfg.num_credentials && !pmk_resolved) {
        forced_memzero(&ndl->security_ctx, sizeof(ndl->security_ctx));
        return;
    }
    if (s_pending_m1.valid && s_pending_m1.pub_id == pub_id && ndl) {''')
        return s
    source = function(source, 'nan_security_apply_pending', pending)
    source = replace(source, '} s_pending_scia;', '''} s_pending_scia;

/* Pending parse material belongs to one serialized NAF receive call, never
 * the next frame or a later Session using the same raw publisher ID. */
void esp32_mquickjs_wifi_nan_sdk_security_clear_pending(void)
{
    forced_memzero(&s_pending_m1, sizeof(s_pending_m1));
    forced_memzero(&s_pending_scia, sizeof(s_pending_scia));
}''')
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--component', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    component, output = args.component.resolve(), args.output.resolve()
    if output.is_relative_to(component):
        parser.error('Output must be outside the shared SDK component')
    for name, digest in REVIEWED.items():
        if hashlib.sha256((component / name).read_bytes()).hexdigest() != digest:
            parser.error('Unreviewed NAN input: ' + name)
    result = patch_source((component / 'wifi_apps/nan_app/src/nan_app.c').read_text())
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != result:
        output.write_text(result)
    security_output = output.with_name('nan_security.c')
    security_result = patch_security_source((component / 'wifi_apps/nan_app/src/nan_security.c').read_text())
    if not security_output.exists() or security_output.read_text() != security_result:
        security_output.write_text(security_result)
    print('ESP32QJS NAN native control prepared; Session/Radio integration and runtime qualification remain required')


if __name__ == '__main__':
    main()
