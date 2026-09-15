"""Compose fixed-SDK EAP control, worker retirement and credential cleanup.

Tracks completed driver/callback/method steps. Framework profile ownership and
Radio admission are still required before exposing an enterprise JS API.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
from sdk_patches.common.source import function, replace
from sdk_patches.wpa.eap.lifecycle import REVIEWED, EXTRA_REVIEWED, patch_source as patch_lifecycle

CONTROL_REVIEWED = {'esp_supplicant/src/esp_wpa_main.c': 'c4c6ff42611816ef389501174bf84906585f1e81e9ef2369bca0b79d02487d17'}

LOCK = '''static esp_err_t wpa2_api_lock(void)
{
    /* Public entry points dispatch here before checking/creating the lock.
     * Lazy initialization therefore runs on one Wi-Fi task, never racing two callers. */
    if (!s_wpa2_api_lock) {
        s_wpa2_api_lock = os_recursive_mutex_create();
        if (!s_wpa2_api_lock) return ESP_ERR_NO_MEM;
    }
    return os_mutex_lock(s_wpa2_api_lock) == TRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}
'''

ENABLE_CALLBACK = '''static esp_err_t esp_client_enable_fn(void *arg)
{
    (void)arg;
    /* Reviewed driver process writes the enable byte before invoking us. */
    esp32qjs_eap_driver_state = 1;
    if (gEapSm || esp32qjs_eap_retiring || esp32qjs_eap_callbacks ||
        esp32qjs_eap_callback_quarantine || esp32qjs_eap_methods) return ESP_ERR_INVALID_STATE;
    struct wpa2_funcs *cb = os_zalloc(sizeof(*cb));
    if (!cb) return ESP_ERR_NO_MEM;
    cb->wpa2_sm_rx_eapol = wpa2_ent_rx_eapol;
    cb->wpa2_start = eap_start_eapol_timer;
    cb->wpa2_init = eap_peer_sm_init;
    cb->wpa2_deinit = eap_peer_sm_deinit;
#ifdef EAP_PEER_METHOD
    if (eap_peer_register_methods() != 0) {
        eap_peer_unregister_methods(); /* Partial registration owns a list too. */
        os_free(cb);
        return ESP_FAIL;
    }
#endif
    esp32qjs_eap_methods = true;
    int result = esp_wifi_register_wpa2_cb_internal(cb);
    if (result != ESP_OK) {
        /* The reviewed C5 implementation always transfers and returns zero.
         * Unexpected returns cannot tell us whether cb was adopted. Never
         * free/reuse that pointer speculatively; keep one bounded quarantine. */
        esp32qjs_eap_callback_quarantine = cb;
        esp32qjs_eap_callbacks = true; /* Unregister is still pending. */
        return result;
    }
    esp32qjs_eap_callbacks = true;
    g_wpa_config_changed = true;
    esp_wifi_set_okc_support(true);
    return ESP_OK;
}
'''

ENABLE = '''static int esp32qjs_eap_enable_dispatch(void *opaque, void *unused)
{
    (void)opaque; (void)unused;
    esp_err_t ret = wpa2_api_lock();
    if (ret != ESP_OK) {
        esp32qjs_eap_control_error = ret;
        return ret;
    }
    if (wpa2_is_enabled()) {
        ret = esp32qjs_eap_driver_state == 1 && esp32qjs_eap_callbacks &&
            esp32qjs_eap_methods && !esp32qjs_eap_retiring &&
            !esp32qjs_eap_callback_quarantine ? ESP_OK : ESP_ERR_INVALID_STATE;
        goto done;
    }
    if (gEapSm || esp32qjs_eap_retiring || esp32qjs_eap_callbacks ||
        esp32qjs_eap_callback_quarantine || esp32qjs_eap_methods ||
        esp32qjs_eap_driver_state == 1 ||
        (esp32qjs_eap_driver_state < 0 && gWpaSm.wpa_sm_eap_disable)) {
        ret = ESP_ERR_INVALID_STATE;
        goto done;
    }
    /* Preserve cleanup reachability even if callback allocation/registration
     * fails after the driver enable byte was written. */
    gWpaSm.wpa_sm_eap_disable = esp_wifi_sta_enterprise_disable;
    esp32qjs_eap_driver_state = -1;
    wifi_wpa2_param_t param = {.fn = (wifi_wpa2_fn_t)esp_client_enable_fn, .param = NULL};
    ret = esp_wifi_sta_wpa2_ent_enable_internal(&param);
    if (ret == ESP_OK && esp32qjs_eap_driver_state == 1 && esp32qjs_eap_callbacks && esp32qjs_eap_methods)
        wpa2_set_state(WPA2_STATE_ENABLED);
    else if (ret == ESP_OK) ret = ESP_ERR_INVALID_STATE;
done:
    esp32qjs_eap_control_error = ret;
    wpa2_api_unlock();
    return ret;
}

esp_err_t esp_wifi_sta_enterprise_enable(void)
{
    if (s_wpa2_task_hdl && os_task_get_current_task() == s_wpa2_task_hdl) return ESP_ERR_INVALID_STATE;
    if (current_task_is_wifi_task()) return esp32qjs_eap_enable_dispatch(NULL, NULL);
    return eloop_register_timeout_blocking(esp32qjs_eap_enable_dispatch, NULL, NULL);
}
'''

DISABLE_CALLBACK = '''static esp_err_t eap_client_disable_fn(void *param)
{
    (void)param;
    /* Called either by the reviewed disable process, or as an unfinished
     * cleanup suffix after that exact driver write was already confirmed. */
    esp32qjs_eap_driver_state = 0;
    if (gEapSm) {
        eap_peer_sm_deinit();
        if (gEapSm) return esp32qjs_eap_cleanup_error != ESP_OK ? esp32qjs_eap_cleanup_error : ESP_ERR_INVALID_STATE;
    }
    if (esp32qjs_eap_callbacks) {
        int result = esp_wifi_unregister_wpa2_cb_internal();
        if (result != ESP_OK) return result;
        esp32qjs_eap_callbacks = false;
    }
    /* Cannot distinguish never-adopted vs already-freed candidate after an
     * unexpected registration result. Requires reboot, not repeated unregister. */
    if (esp32qjs_eap_callback_quarantine) return ESP_ERR_INVALID_STATE;
    eap_globals_reset();
#ifdef EAP_PEER_METHOD
    if (esp32qjs_eap_methods) eap_peer_unregister_methods();
#endif
    esp32qjs_eap_methods = false;
    gWpaSm.wpa_sm_eap_disable = NULL;
    g_wpa_config_changed = true;
    return ESP_OK;
}
'''

DISABLE = '''static int esp32qjs_eap_disable_dispatch(void *opaque, void *unused)
{
    (void)opaque; (void)unused;
    esp_err_t ret = wpa2_api_lock();
    if (ret != ESP_OK) {
        esp32qjs_eap_control_error = ret;
        return ret;
    }
    gWpaSm.wpa_sm_eap_disable = esp_wifi_sta_enterprise_disable;
    if (esp32qjs_eap_driver_state == 0) {
        ret = eap_client_disable_fn(NULL); /* Retry only the unfinished suffix. */
    } else {
        esp32qjs_eap_driver_state = -1;
        wifi_wpa2_param_t param = {.fn = (wifi_wpa2_fn_t)eap_client_disable_fn, .param = NULL};
        ret = esp_wifi_sta_wpa2_ent_disable_internal(&param);
    }
    if (ret == ESP_OK && esp32qjs_eap_driver_state == 0 && !gEapSm && !esp32qjs_eap_callbacks &&
        !esp32qjs_eap_methods && !esp32qjs_eap_callback_quarantine)
        wpa2_set_state(WPA2_STATE_DISABLED);
    else if (ret == ESP_OK) ret = ESP_ERR_INVALID_STATE;
    esp32qjs_eap_control_error = ret;
    wpa2_api_unlock();
    return ret;
}

esp_err_t esp_wifi_sta_enterprise_disable(void)
{
    if (s_wpa2_task_hdl && os_task_get_current_task() == s_wpa2_task_hdl) return ESP_ERR_INVALID_STATE;
    if (current_task_is_wifi_task()) return esp32qjs_eap_disable_dispatch(NULL, NULL);
    return eloop_register_timeout_blocking(esp32qjs_eap_disable_dispatch, NULL, NULL);
}
'''

HELPERS = '''
bool esp32qjs_eap_on_worker(void)
{
    return s_wpa2_task_hdl && os_task_get_current_task() == s_wpa2_task_hdl;
}

/* Only the Wi-Fi task may use this admission observation. A fresh driver has
 * no confirmed write yet; an unknown write with a cleanup hook is a fault. */
bool esp32qjs_eap_configuration_idle(void)
{
    unsigned resources = esp32qjs_eap_native_resources();
    return current_task_is_wifi_task() && !gWpaSm.wpa_sm_eap_disable &&
        (resources == 0 || resources == 512U);
}

int esp32qjs_eap_native_control_error(void)
{
    return esp32qjs_eap_control_error;
}

/* Finalize a future framework profile install on this task. The public SDK
 * setters enqueue a best-effort notification; a complete install must not
 * depend on allocating that notification. No borrowed pointer is queued. */
void esp32qjs_eap_configuration_changed(void)
{
    eloop_cancel_timeout(config_changed_handler, NULL, NULL);
    g_wpa_config_changed = true;
}
'''


def patch_source(relative: str, source: bytes) -> bytes:
    if relative == 'esp_supplicant/src/esp_wpa_main.c':
        if hashlib.sha256(source).hexdigest() != CONTROL_REVIEWED[relative]:
            raise ValueError('Unreviewed SDK WPA deattach source')
        text = source.decode()
        old = '''#ifdef CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (sm->wpa_sm_eap_disable) {
        sm->wpa_sm_eap_disable();
    }
#endif
'''
        body = text[text.index('bool wpa_deattach(void)'):]
        body = body[:body.index('\n}\n')+3]
        new = replace(body, old, '')
        new = replace(new, '    struct wpa_sm *sm = &gWpaSm;\n', '''    struct wpa_sm *sm = &gWpaSm;
#ifdef CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    if (sm->wpa_sm_eap_disable && sm->wpa_sm_eap_disable() != ESP_OK) return false;
#endif
''')
        return replace(text, body, new).encode()
    text = patch_lifecycle(relative, source).decode()
    if relative != 'esp_supplicant/src/esp_eap_client.c':
        return text.encode()
    text = replace(text, 'static bool esp32qjs_eap_task_started;', '''static bool esp32qjs_eap_task_started;
static int esp32qjs_eap_driver_state = -1; /* Last confirmed write; not a live getter. */
static bool esp32qjs_eap_callbacks;
static bool esp32qjs_eap_methods;
static void *esp32qjs_eap_callback_quarantine;
static esp_err_t esp32qjs_eap_control_error;
bool current_task_is_wifi_task(void);''')
    text = replace(text, function(text, 'wpa2_api_lock'), LOCK)
    text = replace(text, function(text, 'esp_client_enable_fn'), ENABLE_CALLBACK)
    text = replace(text, function(text, 'esp_wifi_sta_enterprise_enable'), ENABLE)
    text = replace(text, function(text, 'eap_client_disable_fn'), DISABLE_CALLBACK)
    text = replace(text, function(text, 'esp_wifi_sta_enterprise_disable'), DISABLE)
    resource = text[text.index('unsigned esp32qjs_eap_native_resources(void)'):]
    resource = resource[:resource.index('\n}\n')+3]
    check = '''    bool globals = g_wpa_anonymous_identity || g_wpa_username || g_wpa_password || g_wpa_new_password ||
        g_wpa_client_cert || g_wpa_private_key || g_wpa_private_key_passwd || g_wpa_ca_cert ||
        g_wpa_phase1_options || g_wpa_pac_file;
#ifndef CONFIG_TLS_INTERNAL_CLIENT
    globals = globals || g_wpa_domain_match;
#endif
'''
    revised = resource.replace('{\n', '{\n'+check, 1)
    revised = replace(revised, '| (esp32qjs_eap_task_started ? 256U : 0U);', '''| (esp32qjs_eap_task_started ? 256U : 0U)
        | (esp32qjs_eap_driver_state < 0 ? 512U : 0U) | (esp32qjs_eap_driver_state == 1 ? 1024U : 0U)
        | (esp32qjs_eap_callbacks ? 2048U : 0U) | (esp32qjs_eap_methods ? 4096U : 0U)
        | (globals ? 8192U : 0U) | (esp32qjs_eap_callback_quarantine ? 16384U : 0U);''')
    text = replace(text, resource, revised)
    return (text + HELPERS).encode()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--component', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.output_dir.resolve().is_relative_to(args.component.resolve()):
        parser.error('output must be outside the shared SDK component')
    try:
        for relative, digest in (REVIEWED | EXTRA_REVIEWED | CONTROL_REVIEWED).items():
            if hashlib.sha256((args.component / relative).read_bytes()).hexdigest() != digest:
                raise ValueError('Unreviewed SDK EAP control dependency: ' + relative)
        outputs = {name: patch_source(relative, (args.component / relative).read_bytes()) for name, relative in (
            ('esp_eap_client.c', 'esp_supplicant/src/esp_eap_client.c'), ('eap.c', 'src/eap_peer/eap.c'),
            ('esp_wpa_main.c', 'esp_supplicant/src/esp_wpa_main.c'))}
    except ValueError as error:
        parser.error(str(error))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, source in outputs.items():
        output = args.output_dir / name
        if not output.exists() or output.read_bytes() != source:
            output.write_bytes(source)
        print('ESP32QJS EAP control/retirement: reviewed build-local ' + name + ' ' + hashlib.sha256(source).hexdigest())


if __name__ == '__main__':
    main()
