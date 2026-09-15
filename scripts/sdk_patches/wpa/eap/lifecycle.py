"""Compose EAP secret cleanup with fixed-SDK worker retirement protection.

This guards SM-owned resources. SDK callback registration/driver ownership and
framework Radio admission remain separate obligations for the enterprise bridge.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import re
from sdk_patches.wpa.eap.secrets import REVIEWED, patch_source as patch_secrets
from sdk_patches.common.source import function, replace

EXTRA_REVIEWED = {'port/eloop.c': '2decdfef8e932070a791d7fd4dfa85251fedbfd6ad06f5a0da22f4b1c6eab9ac', 'esp_supplicant/src/esp_wifi_driver.h': 'b11d232c5f41a83ecaa19691e8c081e42530da3cea70a5f87462a8ced7b688cb', 'src/eap_peer/eap_i.h': '098a284159b37f7e024d3e3f3cf6a5b6ba56d423964d38631e20939a64262b15'}

DELETE = '''static inline esp_err_t wpa2_task_delete(void *arg)
{
    (void)arg;
    if (!esp32qjs_eap_task_started) return ESP_OK; /* Failure before task creation. */
    if ((s_wpa2_task_hdl && os_task_get_current_task() == s_wpa2_task_hdl) ||
        !s_wpa2_data_lock || !s_wifi_wpa2_sync_sem || !esp32qjs_eap_exit_sem)
        return ESP_ERR_INVALID_STATE;
    int ret = s_wpa2_task_hdl ? wpa2_post(SIG_WPA2_TASK_DEL, 0) :
        (os_semphr_take(esp32qjs_eap_exit_sem, OS_BLOCK) == TRUE ? ESP_OK : ESP_FAIL);
    if (ret != ESP_OK) return ret;
    /* Dedicated exit acknowledgement is sent only after the task's final
     * access to SM/queue. It cannot consume a prior RX/START acknowledgement. */
    if (s_wpa2_task_hdl || s_wpa2_queue) return ESP_ERR_INVALID_STATE;
    esp32qjs_eap_task_started = false;
    return ESP_OK;
}
'''

DEINIT = '''static void eap_peer_sm_deinit(void)
{
    struct eap_sm *sm = gEapSm;
    if (!sm) return;
    /* This callback executes on the Wi-Fi/supplicant task. Cancel its exact
     * future start before stopping the worker; no new SM may replace this one. */
    esp32qjs_eap_retiring = true;
    eloop_cancel_timeout(eap_start_eapol, NULL, NULL);
#ifdef USE_WPA2_TASK
    esp32qjs_eap_cleanup_error = wpa2_task_delete(NULL);
    if (esp32qjs_eap_cleanup_error != ESP_OK) return;
#endif
    /* No worker can read these objects after its dedicated exit acknowledgement. */
    eap_deinit_prev_method(sm, "EAP deinit");
    eap_sm_abort(sm);
    eap_peer_config_deinit(sm);
    eap_peer_blob_deinit(sm);
    if (sm->ssl_ctx) tls_deinit(sm->ssl_ctx);
    if (STAILQ_FIRST(&s_wpa2_rxq) != NULL) wpa2_rxq_deinit();
    if (esp32qjs_eap_exit_sem) {
        os_semphr_delete(esp32qjs_eap_exit_sem);
        esp32qjs_eap_exit_sem = NULL;
    }
    if (s_wifi_wpa2_sync_sem) {
        os_semphr_delete(s_wifi_wpa2_sync_sem);
        s_wifi_wpa2_sync_sem = NULL;
    }
    if (s_wpa2_data_lock) {
        os_mutex_delete(s_wpa2_data_lock);
        s_wpa2_data_lock = NULL;
    }
    if (s_wpa2_queue) {
        os_queue_delete(s_wpa2_queue); /* Allocated queue, but task never started. */
        s_wpa2_queue = NULL;
    }
    os_free(sm);
    gEapSm = NULL;
    esp32qjs_eap_cleanup_error = ESP_OK;
    esp32qjs_eap_retiring = false;
}
'''

HELPERS = '''
/* Internal observations: invoke only on the Wi-Fi/supplicant task after an
 * ordered dispatch. These expose no pointer or credential bytes. They are not
 * proof of callback-table/driver retirement or ownership by a caller. */
unsigned esp32qjs_eap_native_resources(void)
{
    return (wpa2_is_enabled() ? 1U : 0U) | (gEapSm ? 2U : 0U)
        | (s_wpa2_task_hdl ? 4U : 0U) | (s_wpa2_queue ? 8U : 0U)
        | (s_wpa2_data_lock ? 16U : 0U) | (s_wifi_wpa2_sync_sem ? 32U : 0U)
        | (esp32qjs_eap_exit_sem ? 64U : 0U) | (esp32qjs_eap_retiring ? 128U : 0U)
        | (esp32qjs_eap_task_started ? 256U : 0U);
}

int esp32qjs_eap_native_cleanup_error(void)
{
    return esp32qjs_eap_cleanup_error;
}
'''


def patch_source(relative: str, source: bytes) -> bytes:
    text = patch_secrets(relative, source).decode()
    if relative != 'esp_supplicant/src/esp_eap_client.c':
        return text.encode()
    text = replace(text, 'static void *s_wifi_wpa2_sync_sem = NULL;',
                   'static void *s_wifi_wpa2_sync_sem = NULL;\nstatic void *esp32qjs_eap_exit_sem;\nstatic esp_err_t esp32qjs_eap_cleanup_error;\nstatic bool esp32qjs_eap_retiring;\nstatic bool esp32qjs_eap_task_started;')
    start = text.index('static inline void wpa2_task_delete(')
    end = text.index('\n}\n', start) + 3
    text = replace(text, text[start:end], DELETE)
    # Stop acknowledges on its own semaphore, never the shared RX/START lane.
    task = function(text, 'wpa2_task')
    tail = '''    if (s_wifi_wpa2_sync_sem) {
        wpa_printf(MSG_DEBUG, "EAP: wifi->EAP api completed");
        os_semphr_give(s_wifi_wpa2_sync_sem);
    } else {
        wpa_printf(MSG_ERROR, "EAP: null wifi->EAP sync sem");
    }

    /* At this point, we completed */'''
    task_new = replace(task, tail, '''    s_wpa2_task_hdl = NULL;
    /* Last shared-object access before the dedicated retirement acknowledgement. */
    os_semphr_give(esp32qjs_eap_exit_sem);

    /* At this point, we completed */''')
    text = replace(text, task, task_new)
    post = function(text, 'wpa2_post')
    post_new = replace(post, '''    if (!sm) {
        return ESP_FAIL;
    }''', '''    if (!sm || sig >= SIG_WPA2_MAX || !s_wpa2_queue || !s_wpa2_data_lock ||
        !s_wifi_wpa2_sync_sem || !esp32qjs_eap_exit_sem ||
        (esp32qjs_eap_retiring && sig != SIG_WPA2_TASK_DEL)) return ESP_ERR_INVALID_STATE;''')
    post_new = replace(post_new, '''        return ESP_OK;
    }
    sm->wpa2_sig_cnt[sig]++;''', '''        return sig == SIG_WPA2_TASK_DEL ? ESP_ERR_INVALID_STATE : ESP_OK;
    }
    sm->wpa2_sig_cnt[sig]++;''')
    post_new = replace(post_new, '''    if (s_wifi_wpa2_sync_sem) {
        os_semphr_take(s_wifi_wpa2_sync_sem, OS_BLOCK);''', '''    if (sig == SIG_WPA2_TASK_DEL) {
        if (os_semphr_take(esp32qjs_eap_exit_sem, OS_BLOCK) != TRUE) return ESP_FAIL;
    } else if (s_wifi_wpa2_sync_sem) {
        os_semphr_take(s_wifi_wpa2_sync_sem, OS_BLOCK);''')
    text = replace(text, post, post_new)
    init = function(text, 'eap_peer_sm_init')
    init_new = replace(init, '        eap_peer_sm_deinit();',
                       '        eap_peer_sm_deinit();\n        if (gEapSm) return ESP_ERR_INVALID_STATE;')
    init_new = replace(init_new, '''    gEapSm = sm;
    s_wpa2_data_lock''', '''    gEapSm = sm;
    esp32qjs_eap_retiring = false;
    esp32qjs_eap_cleanup_error = ESP_OK;
    s_wpa2_data_lock''')
    before_task = '''    s_wpa2_queue = os_queue_create(SIG_WPA2_MAX, sizeof(ETSEvent));
    ret = os_task_create'''
    init_new = replace(init_new, before_task, '''    s_wpa2_queue = os_queue_create(SIG_WPA2_MAX, sizeof(ETSEvent));
    if (!s_wpa2_queue) { ret = ESP_ERR_NO_MEM; goto _err; }
    s_wifi_wpa2_sync_sem = os_semphr_create(1, 0);
    if (!s_wifi_wpa2_sync_sem) { ret = ESP_ERR_NO_MEM; goto _err; }
    esp32qjs_eap_exit_sem = os_semphr_create(1, 0);
    if (!esp32qjs_eap_exit_sem) { ret = ESP_ERR_NO_MEM; goto _err; }
    sm->workaround = 1;
    sm->eap_process_started = false;
    s_wpa2_task_hdl = NULL;
    ret = os_task_create''')
    late_sem = '''    s_wifi_wpa2_sync_sem = os_semphr_create(1, 0);
    if (!s_wifi_wpa2_sync_sem) {
        wpa_printf(MSG_ERROR, "EAP: failed create wifi EAP task sync sem");
        ret = ESP_FAIL;
        goto _err;
    }
'''
    init_new = replace(init_new, late_sem, '    esp32qjs_eap_task_started = true;\n')
    text = replace(text, init, init_new)
    text = replace(text, function(text, 'eap_peer_sm_deinit'), DEINIT)
    # The void driver callback cannot report failure, so retain the SM and check
    # it before the public disable callback resets globals or unregisters methods.
    disable = function(text, 'eap_client_disable_fn')
    disable_new = replace(disable, '    esp_wifi_unregister_wpa2_cb_internal();\n', '')
    disable_new = replace(disable_new, '''        eap_peer_sm_deinit();
    }

    eap_globals_reset();''', '''        eap_peer_sm_deinit();
        if (gEapSm) return esp32qjs_eap_cleanup_error != ESP_OK ? esp32qjs_eap_cleanup_error : ESP_ERR_INVALID_STATE;
    }
    int unregister_error = esp_wifi_unregister_wpa2_cb_internal();
    if (unregister_error != ESP_OK) return unregister_error;

    eap_globals_reset();''')
    text = replace(text, disable, disable_new)
    public_disable = function(text, 'esp_wifi_sta_enterprise_disable')
    text = replace(text, public_disable, replace(public_disable, '    if (wpa2_is_disabled()) {',
                   '    if (wpa2_is_disabled() && !gEapSm) {'))
    # Reject new work before queueing data/timers into a retained failed retirement.
    rx = function(text, 'eap_sm_rx_eapol')
    text = replace(text, rx, replace(rx, '    if (!sm) {', '    if (!sm || esp32qjs_eap_retiring) {'))
    start_timer = function(text, 'eap_start_eapol_timer')
    replacement = start_timer.replace('{\n', '{\n    if (!gEapSm || esp32qjs_eap_retiring) return ESP_ERR_INVALID_STATE;\n', 1)
    text = replace(text, start_timer, replacement)
    return (text + HELPERS).encode()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--component', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.output_dir.resolve().is_relative_to(args.component.resolve()):
        parser.error('output must be outside the shared SDK component')
    try:
        for relative, digest in (REVIEWED | EXTRA_REVIEWED).items():
            if hashlib.sha256((args.component / relative).read_bytes()).hexdigest() != digest:
                raise ValueError('Unreviewed SDK EAP lifecycle dependency: ' + relative)
        outputs = {name: patch_source(relative, (args.component / relative).read_bytes()) for name, relative in (
            ('esp_eap_client.c', 'esp_supplicant/src/esp_eap_client.c'), ('eap.c', 'src/eap_peer/eap.c'))}
    except ValueError as error:
        parser.error(str(error))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, source in outputs.items():
        output = args.output_dir / name
        if not output.exists() or output.read_bytes() != source:
            output.write_bytes(source)
        print('ESP32QJS EAP cleanup/worker retirement: reviewed build-local ' + name + ' ' + hashlib.sha256(source).hexdigest())


if __name__ == '__main__':
    main()
