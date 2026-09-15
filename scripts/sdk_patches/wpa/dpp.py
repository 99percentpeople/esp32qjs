"""Build-local fixed-SDK DPP bootstrap ownership and credential transactions.

No project public callback/API is introduced. Dynamic fixtures are deferred.
"""
from __future__ import annotations
import argparse
from sdk_patches.common.source import function, replace
import hashlib
import re
from pathlib import Path
from tool_paths import ROOT as FIRMWARE_ROOT

REVIEWED = {'esp_supplicant/src/esp_dpp.c': '8e449c83709962170e19aca805b01d3c4c38dd63d54068ea39d5bde8b470f61a', 'esp_supplicant/src/esp_dpp_i.h': '91eaba7b7567082ef893ffa2afb3e646cee8d1a3e927763e8876300303ea2f47', 'esp_supplicant/include/esp_dpp.h': '375c8b6bf1ac409d7c313875d316da68d655d5c6fee4822ff9b8ef71611e41b3', 'src/common/dpp.c': 'd25b604c48a7bd33092ad638ddbf09e5b626646364efd821a1c8ca8e9de12f4e', 'src/common/dpp.h': 'c82b471e4481ca15746d66d7bb68c23e1f3b64e09fa72c42ec8e839fdc41e9df', 'src/utils/common.c': 'b99ef519c251d958c937fbf67f6df39721ee2f1e2cc9ed008300f59b6050961f', 'port/eloop.c': '2decdfef8e932070a791d7fd4dfa85251fedbfd6ad06f5a0da22f4b1c6eab9ac', 'src/utils/eloop.h': '6ae5824fffc1afd3ab8afae0c9a6f948543828cec0b2ce323dee0fb88c8d6033', '../esp_wifi/include/esp_wifi_types_generic.h': '8e398a8d22b18c199ea6d48e42784e8a897c22000a09ee9ac7d22cf0f5110e5c'}
REVIEWED.update({'src/common/dpp_crypto.c': '02fada2c38a2bc5d3235737754db1e7fd034ff8f536d12b0f1dd2e196873562c', '../../examples/wifi/wifi_easy_connect/dpp-enrollee/main/dpp_enrollee_main.c': '82e5cf1e4393bad70ed6ee217d610021e180c87a96cceb2cd122d1d2605624b9'})
ROOT = FIRMWARE_ROOT
PARTS = ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp'
OUTPUTS = {'esp_dpp.c': 'esp_supplicant/src/esp_dpp.c', 'dpp.c': 'src/common/dpp.c'}






def part(name):
    return (PARTS / ('esp32_mquickjs_dpp_' + name + '.inc')).read_text()


def patch_source(relative, data):
    if hashlib.sha256(data).hexdigest() != REVIEWED[relative]:
        raise ValueError('Unreviewed SDK DPP source: ' + relative)
    source = data.decode()
    if relative == 'src/common/dpp.c':
        source = replace(source, '\twpa_hexdump(MSG_DEBUG, "private key", privkey, privkey_len);\n', '')
        offset = source.index('int dpp_bootstrap_gen(')
        source = source[:offset] + part('bootstrap_common') + '\n' + source[offset:]
        return source.encode()
    if relative != 'esp_supplicant/src/esp_dpp.c':
        raise ValueError('Unsupported DPP source: ' + relative)
    source = replace(source, function(source, 'esp_dpp_bootstrap_gen'), part('bootstrap'))
    source = replace(source, function(source, 'esp_dpp_parse_chan_list'), '')
    # Select only the old SDK public definition, not the replacement just inserted.
    old_public = function(data.decode(), 'esp_supp_dpp_bootstrap_gen')
    source = replace(source, old_public, '')
    old = function(source, 'esp_dpp_deinit')
    new = replace(old, '    dpp_stop_internal();',
        '    ret = esp32qjs_dpp_bootstrap_cancel_locked();\n'
        '    if (ret != ESP_OK) { dpp_api_unlock(); return ret; }\n\n    dpp_stop_internal();')
    source = replace(source, old, new)
    old = function(source, 'esp_dpp_init')
    source = replace(source, old, replace(old,
        '    os_bzero(&s_dpp_ctx, sizeof(s_dpp_ctx));',
        '    if (s_dpp_bootstrap_job) { dpp_api_unlock(); return ESP_ERR_INVALID_STATE; }\n'
        '    os_bzero(&s_dpp_ctx, sizeof(s_dpp_ctx));\n    s_dpp_ctx.id = -1;'))
    # Validate full rows and reject truncation before any credential delivery.
    old = function(source, 'esp_dpp_handle_config_obj')
    new = replace(old, '{\n    forced_memzero(config_data, sizeof(*config_data));',
        '{\n    forced_memzero(config_data, sizeof(*config_data));\n'
        '    if (!auth || !conf || !conf->ssid_len || conf->ssid_len > sizeof(config_data->ssid) ||\n'
        '        conf->akm < DPP_AKM_DPP || conf->akm > DPP_AKM_PSK_SAE_DPP ||\n'
        '        (dpp_akm_dpp(conf->akm) && auth->net_access_key && wpabuf_len(auth->net_access_key) > ESP_DPP_MAX_KEY_LEN) ||\n'
        '        (conf->c_sign_key && wpabuf_len(conf->c_sign_key) > ESP_DPP_MAX_KEY_LEN)) return -1;')
    new = replace(new, '        wpa_printf(MSG_INFO, DPP_EVENT_CONNECTOR "%s",\n                   conf->connector);\n', '')
    clamp = '        if (key_len > ESP_DPP_MAX_KEY_LEN) {\n            key_len = ESP_DPP_MAX_KEY_LEN;\n        }\n'
    if new.count(clamp) != 2: raise ValueError('DPP key clamps changed')
    new = new.replace(clamp, '')
    new = replace(new, '    return 0;',
        '    if (dpp_akm_dpp(conf->akm) && !esp32qjs_dpp_config_valid(config_data)) {\n'
        '        forced_memzero(config_data, sizeof(*config_data)); return -1;\n    }\n    return 0;')
    source = replace(source, old, new)
    source = replace(source, 'static bool s_dpp_init_pending;',
        'static bool s_dpp_init_pending;\nstatic bool s_dpp_deinit_dispatch_pending;\n'
        'static bool esp32qjs_dpp_config_valid(const esp_dpp_config_data_t *);\n'
        'static uint64_t s_dpp_config_revision; /* Preserved across unlocked event publication. */')
    source = replace(source, function(source, 'esp_dpp_stored_conf_matches_row'), part('config'))
    old = function(source, 'esp_dpp_conf_alloc_from_config_data')
    new = replace(old, '    if (!config) {', '    if (!esp32qjs_dpp_config_valid(config)) {')
    source = replace(source, old, new)
    old = function(source, 'esp_supp_dpp_set_config')
    new = replace(old, '    if (!atomic_load(&s_dpp_init_done)) {',
        '    if (!atomic_load(&s_dpp_init_done) || atomic_load(&dpp_shutting_down) || s_dpp_ctx.dpp_deinit_pending) {')
    new = replace(new, '    if (!config) {',
        '    if ((!config && dc->conf) || (config && !esp_dpp_stored_conf_matches_row(dc, config))) {\n'
        '        if (s_dpp_config_revision == UINT64_MAX) { dpp_api_unlock(); return ESP_ERR_NO_MEM; }\n    }\n'
        '    if (!config) {')
    new = replace(new, '        if (dc->conf) {\n', '        if (dc->conf) {\n            ++s_dpp_config_revision;\n')
    failure = """    if (err != ESP_OK) {
        dpp_clear_confs(dc->conf);
        dc->conf = NULL;
        dpp_api_unlock();
        esp_wifi_sta_notify_dpp_config_set_internal(false);
        return err;
    }"""
    new = replace(new, failure, '    if (err != ESP_OK) { dpp_api_unlock(); return err; }')
    new = replace(new, '    dc->conf = new_conf;', '    dc->conf = new_conf;\n    ++s_dpp_config_revision;')
    for value in ('true', 'false'):
        pair = '        dpp_api_unlock();\n        esp_wifi_sta_notify_dpp_config_set_internal(' + value + ');'
        new = new.replace(pair, '        esp_wifi_sta_notify_dpp_config_set_internal(' + value + ');\n        dpp_api_unlock();')
    new = replace(new, '    dpp_api_unlock();\n    esp_wifi_sta_notify_dpp_config_set_internal(true);',
        '    esp_wifi_sta_notify_dpp_config_set_internal(true);\n    dpp_api_unlock();')
    source = replace(source, old, new)
    old = function(source, 'gas_process_complete_resp')
    new = replace(old, '    struct dpp_conf *new_conf_pending = NULL;',
        '    struct dpp_conf *new_conf_pending = NULL;\n    bool clear_conf_pending = false;')
    new = replace(new, '                    if (autostore != ESP_OK) {',
        '                    if (autostore != ESP_OK) {\n                        ret = autostore;')
    eager = """            dpp_clear_confs(s_dpp_ctx.dpp_config_store->conf);
            s_dpp_ctx.dpp_config_store->conf = NULL;
            esp_wifi_sta_notify_dpp_config_set_internal(false);"""
    new = replace(new, eager, '            clear_conf_pending = true;')
    new = replace(new, '    struct dpp_conf *old_conf = s_dpp_ctx.dpp_config_store ? s_dpp_ctx.dpp_config_store->conf : NULL;',
        '    if ((new_conf_pending || clear_conf_pending) && s_dpp_config_revision == UINT64_MAX) {\n'
        '        ret = ESP_ERR_NO_MEM; goto out;\n    }\n    uint64_t old_revision = s_dpp_config_revision;')
    new = replace(new, '            if (dc->conf != old_conf) {', '            if (s_dpp_config_revision != old_revision) {')
    new = replace(new, '                dc->conf = new_conf_pending;', '                dc->conf = new_conf_pending;\n                ++s_dpp_config_revision;')
    new = replace(new, '    ret = ESP_OK;\n\nout:',
        '    if (clear_conf_pending && s_dpp_config_revision == old_revision && s_dpp_ctx.dpp_config_store) {\n'
        '        dpp_clear_confs(s_dpp_ctx.dpp_config_store->conf);\n'
        '        s_dpp_ctx.dpp_config_store->conf = NULL;\n        ++s_dpp_config_revision;\n'
        '        esp_wifi_sta_notify_dpp_config_set_internal(false);\n    }\n    ret = ESP_OK;\n\nout:')
    source = replace(source, old, new)
    # Action input is sized before addition and uint32_t narrowing.
    source = replace(source, '    if (!hdr || !payload || len < 2) {',
        '    if (!hdr || !payload || len < 2 || len > UINT32_MAX || len > SIZE_MAX - sizeof(*rx_param)) {')
    source = patch_delivery_and_lifetime(source)
    return source.encode()


def patch_delivery_and_lifetime(source):
    # All SDK asynchronous callers, including the bootstrap job, use the same
    # numeric ticket registry. The registry itself still calls the real eloop.
    source = re.sub(r'\beloop_register_timeout\(', 'esp32qjs_dpp_async_register(', source)
    source = re.sub(r'\beloop_cancel_timeout\(', 'esp32qjs_dpp_async_cancel(', source)
    anchor = function(source, 'dpp_api_unlock')
    source = replace(source, anchor, anchor + '\n' + part('async') + '\n' + part('result') + '\n' + part('roc') + '\n' + part('tx'))

    old = function(source, 'dpp_post_dpp_failed_event')
    source = replace(source, old, '''static void dpp_post_dpp_failed_event(uint32_t failure_reason)
{
    if (dpp_api_lock() != ESP_OK) return;
    esp32qjs_dpp_result_failure_locked((esp_err_t)failure_reason);
    if (s_dpp_result && !s_dpp_result->status.retained && !s_dpp_result->status.closing) {
        wifi_event_dpp_failed_t event = {.failure_reason = failure_reason};
        if (esp_event_post(WIFI_EVENT, WIFI_EVENT_DPP_FAILED, &event, sizeof(event), 0) != ESP_OK)
            esp32qjs_dpp_result_count(&s_dpp_result->status.observation_drops);
    }
    dpp_api_unlock();
}
''')
    old = function(source, 'gas_process_complete_resp')
    # A managed Session selects explicitly; credential receipt must not silently
    # install the first row or clear the existing supplicant configuration.
    new = replace(old, '    if (recv->total_conf > 0) {',
        '    if (recv->total_conf > 0 && s_dpp_result && !s_dpp_result->status.retained) {')
    begin = new.index('    uint64_t old_revision')
    end = new.index('    if (new_conf_pending) {', begin)
    new = new[:begin] + '''    ret = esp32qjs_dpp_result_configs_locked(recv);
    if (ret != ESP_OK) goto out;

''' + new[end:]
    begin = new.index('    if (new_conf_pending) {', begin)
    end = new.index('    ret = ESP_OK;\n\nout:', begin)
    new = new[:begin] + '''    if (new_conf_pending) {
        struct dpp_config_store *dc = s_dpp_ctx.dpp_config_store;
        dpp_clear_confs(dc->conf);
        dc->conf = new_conf_pending;
        new_conf_pending = NULL;
        ++s_dpp_config_revision;
        esp_wifi_sta_notify_dpp_config_set_internal(true);
    }
    if (clear_conf_pending) {
        dpp_clear_confs(s_dpp_ctx.dpp_config_store->conf);
        s_dpp_ctx.dpp_config_store->conf = NULL;
        ++s_dpp_config_revision;
        esp_wifi_sta_notify_dpp_config_set_internal(false);
    }
''' + new[end:]
    comment_begin = new.index('    /*\n     * Runs on eloop only.')
    comment_end = new.index('    if (auth->num_conf_obj >', comment_begin)
    new = new[:comment_begin] + '''    /* Native capture and any unmanaged SDK autostore commit share the API
     * mutex. Observers cannot change configuration until both are complete. */
''' + new[comment_end:]
    new = replace(new, '    ret = ESP_OK;\n\nout:', '''    /* Native result and config store are committed before observation. No
     * secret enters the default event queue for a managed Session. */
    if (!s_dpp_result->status.retained &&
        esp_event_post(WIFI_EVENT, WIFI_EVENT_DPP_CFG_RECVD, recv, post_size, 0) != ESP_OK)
        esp32qjs_dpp_result_count(&s_dpp_result->status.observation_drops);
    ret = ESP_OK;

out:''')
    source = replace(source, old, new)

    old = function(source, 'esp_dpp_init')
    new = replace(old, '    if (s_dpp_bootstrap_job) { dpp_api_unlock(); return ESP_ERR_INVALID_STATE; }',
        '''    if (s_dpp_bootstrap_job || s_dpp_async_count || s_dpp_async_active ||
        s_dpp_generation == UINT32_MAX || esp32qjs_dpp_tx_held() || esp32qjs_dpp_roc_held()) {
        dpp_api_unlock(); return ESP_ERR_INVALID_STATE;
    }
    uint64_t identity = ((uint64_t)(uintptr_t)eloop_data << 32) | (uint32_t)(uintptr_t)user_ctx;
    ret = esp32qjs_dpp_result_bind_locked(identity);
    if (ret != ESP_OK) { dpp_api_unlock(); return ret; }
    ++s_dpp_generation;''')
    new = new.replace('        ret = ESP_FAIL;\n', '')
    events_begin = new.index('    ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_ACTION_TX_STATUS,')
    events_end = new.index('    wpa_printf(MSG_INFO, "DPP: dpp init done");', events_begin)
    new = new[:events_begin] + new[events_end:]
    new = new[:new.index('init_fail:')] + '''init_fail:
    esp32qjs_dpp_result_failure_locked(ret);
    dpp_api_unlock();
    /* The same cleanup suffix is used after partial initialization. Failed
     * unregister/cancel retains both the result and SDK storage for retry. */
    esp_dpp_deinit(eloop_data, user_ctx);
    return ret;
}
'''
    source = replace(source, old, new)
    source = replace(source, function(source, 'esp_dpp_deinit'), part('deinit'))
    old = function(source, 'esp_supp_dpp_init')
    source = replace(source, old, replace(old, '    if (s_dpp_init_pending) {',
        '    if (s_dpp_result || s_dpp_init_pending || s_dpp_deinit_dispatch_pending) {'))
    old = function(source, 'esp_supp_dpp_bootstrap_gen')
    source = replace(source, old, replace(old, '    if (!atomic_load(&s_dpp_init_done)',
        '    if (!esp32qjs_dpp_command_allowed_locked() || !atomic_load(&s_dpp_init_done)'))
    old = function(source, 'esp_supp_dpp_set_config')
    source = replace(source, old, replace(old, '    if (!atomic_load(&s_dpp_init_done)',
        '    if (!esp32qjs_dpp_command_allowed_locked() || !atomic_load(&s_dpp_init_done)'))
    old = function(source, 'esp_supp_dpp_start_listen')
    # This blocking SDK wrapper is never the managed native Session entry.
    new = old.replace('if (!atomic_load(&s_dpp_init_done))',
        'if (esp32qjs_dpp_result_held() && current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;\n'
        '    if (!atomic_load(&s_dpp_init_done))', 1)
    new = replace(new, '    if (!s_dpp_ctx.bootstrap_done) {',
        '    uint32_t generation = s_dpp_generation;\n'
        '    if (!esp32qjs_dpp_command_allowed_locked() || !s_dpp_ctx.bootstrap_done) {')
    new = new.replace('if (atomic_load(&dpp_shutting_down) || !atomic_load(&s_dpp_init_done)',
        'if (generation != s_dpp_generation || !esp32qjs_dpp_command_allowed_locked() ||\n'
        '        atomic_load(&dpp_shutting_down) || !atomic_load(&s_dpp_init_done)')
    source = replace(source, old, new)
    old = function(source, 'esp_supp_dpp_stop_listen')
    new = replace(old, 'eloop_register_timeout_blocking(listen_stop_handler, NULL, NULL)',
        'eloop_register_timeout_blocking(esp32qjs_dpp_public_stop_handler, NULL, NULL)')
    source = replace(source, old, '''static int esp32qjs_dpp_public_stop_handler(void *data, void *user)
{
    if (dpp_api_lock() != ESP_OK) return ESP_ERR_INVALID_STATE;
    esp_err_t error = s_dpp_result && s_dpp_result->status.retained ?
        ESP_ERR_INVALID_STATE : listen_stop_handler(data, user);
    dpp_api_unlock(); return error;
}
''' + new)
    source = replace(source, function(source, 'listen_stop_handler'), '''static int listen_stop_handler(void *data, void *user_ctx)
{
    (void)data; (void)user_ctx;
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    esp32qjs_dpp_async_cancel(dpp_listen_next_channel, NULL, NULL);
    s_dpp_ctx.dpp_listen_ongoing = false;
    return esp32qjs_dpp_roc_cancel_locked();
}
''')
    old = function(source, 'esp_dpp_send_action_frame')
    new = replace(old, '    if (ESP_OK != esp_wifi_action_tx_req(req)) {',
        '''    esp_err_t submit_error = esp32qjs_dpp_tx_submit_locked(req, type);
    if (submit_error != ESP_OK) {''')
    new = replace(new, '        os_free(req);\n        return ESP_FAIL;',
        '        os_free(req);\n        return submit_error;')
    new = replace(new, '''    s_dpp_ctx.pending_tx_op.op_id = req->op_id;
    s_dpp_ctx.pending_tx_op.type = type;
    s_dpp_ctx.pending_tx_op_in_progress = true;
''', '')
    new = new.replace('os_free(req);', 'bin_clear_free(req, sizeof(*req) + len);')
    new = replace(new, '    if (len > SIZE_MAX - sizeof(*req)) {',
        '    if (!dest_mac || !buf || !len || len > ESP32QJS_DPP_TX_MAX || len > SIZE_MAX - sizeof(*req)) {')
    new = new.replace('Sent DPP action frame %d', 'Accepted DPP action frame %d')
    source = replace(source, old, new)
    old = function(source, 'dpp_stop_internal')
    source = replace(source, old, replace(old, '    dpp_cancel_auth_gas_eloop_timeouts();',
        '    esp32qjs_dpp_tx_cancel_locked();\n    dpp_cancel_auth_gas_eloop_timeouts();'))
    old = function(source, 'dpp_listen_next_channel')
    new = replace(old, '    ret = esp_wifi_remain_on_channel(&req);',
        '    ret = esp32qjs_dpp_roc_submit_locked(&req);')
    new = replace(new, '        dpp_abort_failure_locked(ESP_ERR_DPP_FAILURE);',
        '        dpp_abort_failure_locked(ret);')
    new = replace(new, '''    if (s_dpp_event_group) {
        os_event_group_clear_bits(s_dpp_event_group, DPP_ROC_EVENT_HANDLED);
    }
    atomic_store(&roc_in_progress, true);
''', '')
    source = replace(source, old, new)
    # A failed native cancellation must not be followed by a new protocol TX.
    old = function(source, 'esp_dpp_rx_action')
    before = '                listen_stop_handler(NULL, NULL);'
    if old.count(before) != 3: raise ValueError('DPP RX ROC cancellation sites changed')
    source = replace(source, old, old.replace(before,
        '                ret = listen_stop_handler(NULL, NULL);\n'
        '                if (ret != ESP_OK) goto fail_unlock;'))

    # Hold the API mutex from RX admission through queue insertion. Merely
    # checking init_done before allocation leaves an old-frame/new-init race.
    old = function(source, 'esp_supp_rx_action')
    new = old.replace('static int esp_supp_rx_action(', 'static int esp32qjs_dpp_rx_locked(', 1)
    new += '''
static int esp_supp_rx_action(uint8_t *hdr, uint8_t *payload, size_t len, uint8_t channel)
{
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!atomic_load(&s_dpp_init_done) || atomic_load(&dpp_shutting_down) || s_dpp_ctx.dpp_deinit_pending)
        error = ESP_ERR_INVALID_STATE;
    else error = esp32qjs_dpp_rx_locked(hdr, payload, len, channel);
    dpp_api_unlock(); return error;
}
'''
    source = replace(source, old, new)
    source = replace(source, function(source, 'tx_status_handler'), '')
    source = replace(source, '''static void tx_status_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data);
''', '')
    old = function(source, 'tx_status_eloop_handler')
    body = old[old.index('    } else if (!auth) {') + len('    } else if (!auth) {'):]
    body = re.sub(r'^\s*os_free\(evt\);\n', '', body, flags=re.M)
    body = body.replace('evt->status', 'status')
    new = '''static void esp32qjs_dpp_tx_protocol_result(enum dpp_tx_frame_type type, wifi_action_tx_status_type_t status)
{
    if (dpp_api_lock() != ESP_OK) return;
    if (atomic_load(&dpp_shutting_down)) { dpp_api_unlock(); return; }
    struct dpp_authentication *auth = s_dpp_ctx.dpp_auth;
    if (!auth && !(type == DPP_TX_PEER_DISCOVERY_REQ && esp32qjs_dpp_connection_locked())) {''' + body
    source = replace(source, old, new)
    # A separately installed Connector must not depend on the old provisioning
    # authentication allocation. Retain the native async ticket/close fences.
    old = function(source, 'peer_disc_timeout')
    source = replace(source, old, replace(old,
        '    if (!s_dpp_ctx.dpp_auth || (s_dpp_ctx.dpp_auth != auth)) {',
        '    if ((!s_dpp_ctx.dpp_auth || s_dpp_ctx.dpp_auth != auth) &&\n'
        '        !(auth == NULL && esp32qjs_dpp_connection_locked())) {'))
    old = function(source, 'esp_dpp_peer_disc_retry')
    source = replace(source, old, replace(old, '    if (!auth || !config_store) {',
        '    if ((!auth && !esp32qjs_dpp_connection_locked()) || !config_store) {'))
    old = function(source, 'esp_dpp_rx_frm')
    source = replace(source, old, replace(old, '    switch (type) {',
        '    if (s_dpp_result && s_dpp_result->status.connection_mode &&\n'
        '        (type != DPP_PA_PEER_DISCOVERY_RESP || !esp32qjs_dpp_connection_locked())) return ESP_OK;\n\n'
        '    switch (type) {'))
    old = function(source, 'esp_dpp_start_net_intro_protocol')
    source = replace(source, old, replace(old, '    if (s_dpp_ctx.dpp_deinit_pending) {',
        '    if (s_dpp_ctx.dpp_deinit_pending ||\n'
        '        (s_dpp_result && s_dpp_result->status.retained && !esp32qjs_dpp_connection_locked())) {'))
    old = function(source, 'esp_dpp_start_net_intro_protocol_internal')
    source = replace(source, old, replace(old, '    curr_chan = config->curr_chan;',
        '    curr_chan = config->curr_chan;\n'
        '    if (esp32qjs_dpp_connection_locked()) {\n'
        '        wifi_second_chan_t secondary;\n'
        '        ret = esp_wifi_get_channel(&curr_chan, &secondary);\n'
        '        if (ret != ESP_OK || !curr_chan) { wpabuf_free(buf); return ret != ESP_OK ? ret : ESP_ERR_INVALID_STATE; }\n'
        '    }'))
    # The native done_cb supplies the retained numeric operation. No default
    # ROC event handler may allocate/relabel an old status as a new operation.
    source = replace(source, function(source, 'roc_status_eloop_handler'), '')
    source = replace(source, function(source, 'roc_status_handler'), '')
    source = replace(source, '''static void roc_status_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
''', '')
    old = function(source, 'is_dpp_enabled')
    source = replace(source, old, replace(old, 'bool is_dpp_enabled(void)\n{',
        'bool is_dpp_enabled(void)\n{\n    if (esp32qjs_dpp_result_held()) return true;'))
    # Partial cleanup is still owned even after init_done was cleared.
    source = replace(source, function(source, 'esp_supp_dpp_deinit'), part('public_deinit'))
    marker = '\n#endif /* CONFIG_DPP */'
    if marker not in source:
        marker = '\n#endif'
        offset = source.rindex(marker)
        source = source[:offset] + '\n' + part('commands') + source[offset:]
    else:
        source = replace(source, marker, '\n' + part('commands') + marker)
    return source


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--component', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    for name, digest in REVIEWED.items():
        if hashlib.sha256((args.component / name).read_bytes()).hexdigest() != digest:
            raise SystemExit('Unreviewed SDK DPP dependency: ' + name)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, relative in OUTPUTS.items():
        output = patch_source(relative, (args.component / relative).read_bytes())
        path = args.output_dir / name
        if not path.exists() or path.read_bytes() != output: path.write_bytes(output)
    print('ESP32QJS DPP bootstrap/config fixes: 2 build-local translation units')


if __name__ == '__main__':
    main()
