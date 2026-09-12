"""Retain partial registrar state until the checked close suffix succeeds."""
from patch_idf_wps import function, replace

FAILURE = '''static void esp32qjs_wps_ap_init_failed(struct hostapd_data *hapd, int error)
{
    uint32_t identity = esp32qjs_wps_ap_result_identity(hapd);
    uint8_t no_peer[ETH_ALEN] = {0};
    if (!identity) return;
    /* Save the original operation error before cleanup, including failures
     * before SDK heap allocation. Queue publication is only observation. */
    esp32qjs_wps_ap_result_peer_error(identity, error, no_peer);
    if (esp32qjs_wps_ap_result_prepare_close(hapd) != ESP_OK) return;
    if (!esp32qjs_wps_ap_result_deinit_allowed(hapd)) return;
    /* Managed owners remain retained. The ordinary SDK path may release only
     * after the same checked prefix and child drain used by explicit disable. */
    (void)wifi_ap_wps_deinit();
}
'''


def patch_ap_init_cleanup(relative: str, text: str) -> str:
    if relative == 'src/ap/wps_hostapd.c':
        anchor = 'static int hostapd_wps_set_ie_cb('
        pos = text.index(anchor)
        text = text[:pos] + '''/* Native Wi-Fi task only. Registration ownership is independent of
 * registrar allocation: a failed init must not unregister another owner. */
static uint32_t esp32qjs_wps_host_methods_identity;
static int esp32qjs_wps_host_ie_error;

''' + text[pos:]
        body = function(text, 'hostapd_wps_set_ie_cb')
        fixed = replace(body, '\treturn ret;', '''    if (ret != ESP_OK && !esp32qjs_wps_host_ie_error) {
        esp32qjs_wps_host_ie_error = ret;
    }
    return ret;''')
        text = replace(text, body, fixed)
        body = function(text, 'hostapd_init_wps')
        fixed = replace(body, '    if (!result_identity) return ESP_ERR_INVALID_STATE;', '''    if (!result_identity || esp32qjs_wps_host_methods_identity) return ESP_ERR_INVALID_STATE;
    esp32qjs_wps_host_ie_error = ESP_OK;''')
        fixed = replace(fixed, '    if (!wps->registrar) { ret = ESP_FAIL; goto fail; }', '''    if (!wps->registrar) {
        ret = esp32qjs_wps_host_ie_error ? esp32qjs_wps_host_ie_error : ESP_FAIL;
        goto fail;
    }''')
        fixed = replace(fixed, '    identity_registered = 1;', '''    identity_registered = 1;
    esp32qjs_wps_host_methods_identity = result_identity;''')
        fixed = replace(fixed, '    if (identity_registered) eap_server_unregister_methods();', '''    if (identity_registered) {
        eap_server_unregister_methods();
        esp32qjs_wps_host_methods_identity = 0;
    }''')
        fixed = replace(fixed, '    hostapd_wps_clear_ies(hapd, 1);\n', '')
        text = replace(text, body, fixed)
        body = function(text, 'esp32qjs_wps_ap_hostap_release')
        fixed = replace(body, '        eap_server_unregister_methods();\n', '')
        fixed = replace(fixed, '    return ESP_OK;', '''    if (esp32qjs_wps_host_methods_identity == identity) {
        eap_server_unregister_methods();
        esp32qjs_wps_host_methods_identity = 0;
    }
    return ESP_OK;''')
        text = replace(text, body, fixed)
        return replace(text, function(text, 'hostapd_wps_clear_ies'), '')
    if relative != 'esp_supplicant/src/esp_hostpad_wps.c': return text
    body = function(text, 'wifi_ap_wps_init')
    fixed = replace(body, '    bool result_bound = false;\n', '')
    fixed = replace(fixed, '''    ret = esp32qjs_wps_ap_result_bind(hapd);
    if (ret != ESP_OK) goto _out;
    result_bound = true;''', '''    /* enable already reserved the AP owner before its first mutation. */
    if (!esp32qjs_wps_ap_result_identity(hapd)) return ESP_ERR_INVALID_STATE;''')
    start = fixed.index('_err:\n')
    fixed = fixed[:start] + '''_err:
_out:
    /* Preserve every allocation until enable's checked cleanup has completed.
     * This also covers an IE clear/type/status failure after constructor error. */
    forced_memzero(&cfg, sizeof(cfg));
    return ret;
}
'''
    text = replace(text, body, fixed)
    body = function(text, 'wifi_ap_wps_deinit')
    fixed = replace(body, '''    if (gWpsSm == NULL) {
        return ESP_FAIL;
    }

''', '')
    fixed = replace(fixed, '    if (release_error != ESP_OK) return release_error;', '''    if (release_error != ESP_OK) {
        esp32qjs_wps_ap_result_cleanup_error(hostapd_get_hapd_data(), release_error);
        return release_error;
    }
    if (!sm) {
        esp32qjs_wps_ap_result_detach(hostapd_get_hapd_data(), ESP_OK);
        return ESP_OK;
    }''')
    text = replace(text, body, fixed)
    body = function(text, 'wifi_ap_wps_enable_internal')
    fixed = replace(body, '    if (!esp32qjs_wps_ap_result_can_bind()) return ESP_ERR_INVALID_STATE;', '''    struct hostapd_data *hapd = hostapd_get_hapd_data();
    if (!hapd || hapd->wps || hapd->eapol_auth || !esp32qjs_wps_ap_result_can_bind())
        return ESP_ERR_INVALID_STATE;
    ret = esp32qjs_wps_ap_result_bind(hapd);
    if (ret != ESP_OK) return ret;''')
    fixed = replace(fixed, '''    ret = wps_set_factory_info(config);
    if (ret != ESP_OK) {
        return ret;
    }''', '''    ret = wps_set_factory_info(config);
    if (ret != ESP_OK) goto _err;''')
    fixed = replace(fixed, '''    wps_set_type(WPS_TYPE_DISABLE);
    wps_set_status(WPS_STATUS_DISABLE);

    return ret;''', '''    esp32qjs_wps_ap_init_failed(hapd, ret);
    return ret;''')
    return replace(text, body, FAILURE + '\n' + fixed)
