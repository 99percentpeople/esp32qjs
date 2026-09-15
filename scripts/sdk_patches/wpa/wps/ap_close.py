"""Checked AP WPS close prefix; native EAP/queue retirement is separate."""
from sdk_patches.common.source import function, replace

HOST_STEPS = '''int esp32qjs_wps_registrar_cancel_timers(struct wps_registrar *reg);

esp_err_t esp32qjs_wps_ap_cleanup_step(void *context, unsigned stage)
{
    struct hostapd_data *hapd = context;
    int ret;
    if (!hapd || !esp32qjs_wps_ap_result_identity(hapd)) return ESP_ERR_INVALID_STATE;
    switch (stage) {
    case ESP32QJS_WPS_AP_CLOSE_TYPE:
        return wps_set_type(WPS_TYPE_DISABLE);
    case ESP32QJS_WPS_AP_CLOSE_STATUS:
        return wps_set_status(WPS_STATUS_DISABLE);
    case ESP32QJS_WPS_AP_CLOSE_REENABLE_TIMER:
        ret = eloop_cancel_timeout(hostapd_wps_reenable_ap_pin, hapd, NULL);
        return ret < 0 ? ret : ESP_OK;
    case ESP32QJS_WPS_AP_CLOSE_PIN_TIMER:
        ret = eloop_cancel_timeout(hostapd_wps_ap_pin_timeout, hapd, NULL);
        return ret < 0 ? ret : ESP_OK;
    case ESP32QJS_WPS_AP_CLOSE_REGISTRAR_TIMERS:
        return hapd->wps ? esp32qjs_wps_registrar_cancel_timers(hapd->wps->registrar) : ESP_OK;
    case ESP32QJS_WPS_AP_CLOSE_EAP_TIMERS:
        return esp32qjs_wps_ap_eapol_stop(hapd, esp32qjs_wps_ap_result_identity(hapd));
    case ESP32QJS_WPS_AP_CLOSE_PEER_DELAYS:
        return esp32qjs_wps_ap_peer_delays_stop(hapd, esp32qjs_wps_ap_result_identity(hapd));
    case ESP32QJS_WPS_AP_CLOSE_BEACON_IE:
        return esp_wifi_unset_appie_internal(WIFI_APPIE_RAM_BEACON);
    case ESP32QJS_WPS_AP_CLOSE_PROBE_IE:
        return esp_wifi_unset_appie_internal(WIFI_APPIE_RAM_PROBE_RSP);
    default:
        return ESP_ERR_INVALID_ARG;
    }
}
'''

DISABLE = '''int wifi_ap_wps_disable_internal(void)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    enum wps_owner owner = wps_get_owner();
    struct hostapd_data *hapd = hostapd_get_hapd_data();
    if (owner == WPS_OWNER_ENROLLEE) return ESP_ERR_WIFI_MODE;
    if (owner == WPS_OWNER_NONE && !esp32qjs_wps_ap_result_identity(hapd)) return ESP_OK;
    int ret = esp32qjs_wps_ap_result_prepare_close(hapd);
    if (ret != ESP_OK) return ret;
    /* Retained managed state requires the forthcoming native drain. Never
     * mistake completed advertisement cleanup for permission to free EAP. */
    if (!esp32qjs_wps_ap_result_deinit_allowed(hapd)) return ESP_ERR_INVALID_STATE;
    ret = wifi_ap_wps_deinit();
    if (ret != ESP_OK) return ret;
    wps_set_owner(WPS_OWNER_NONE);
    return ESP_OK;
}
'''

HOST_CLOSE = '''#ifdef CONFIG_WPS_REGISTRAR
static int esp32qjs_hostap_close_wps(struct hostapd_data *hapd)
{
    /* AP shutdown must not disable an unrelated Station enrollee. */
    if (wps_get_owner() != WPS_OWNER_REGISTRAR &&
        !esp32qjs_wps_ap_result_identity(hapd)) return ESP_OK;
    if (hapd != hostapd_get_hapd_data()) return ESP_ERR_INVALID_STATE;
    return wifi_ap_wps_disable_internal();
}
#endif
'''


def patch_ap_close(relative: str, text: str) -> str:
    if relative == 'src/ap/wps_hostapd.c':
        body = function(text, 'hostapd_wps_set_ie_cb')
        fixed = replace(body, '\tint ret;', '''\tint ret;
    uint32_t identity = esp32qjs_wps_ap_result_identity(ctx);
    if (!identity || !esp32qjs_wps_ap_result_context(identity)) {
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }''')
        text = replace(text, body, fixed)
        body = function(text, 'hostapd_wps_event_cb')
        fixed = replace(body, 'esp32qjs_wps_ap_result_context((uint32_t)(uintptr_t)ctx)',
                        'esp32qjs_wps_ap_result_callback_enter((uint32_t)(uintptr_t)ctx)')
        fixed = fixed[:fixed.rfind('}')] + '    esp32qjs_wps_ap_result_callback_leave((uint32_t)(uintptr_t)ctx);\n}\n'
        text = replace(text, body, fixed)
        body = function(text, 'hostapd_deinit_wps')
        fixed = replace(body, '''    if (!hapd) return;
    eloop_cancel_timeout(hostapd_wps_reenable_ap_pin, hapd, NULL);
    eloop_cancel_timeout(hostapd_wps_ap_pin_timeout, hapd, NULL);''',
                        '    if (!hapd || !esp32qjs_wps_ap_result_deinit_allowed(hapd)) return;')
        fixed = replace(fixed, '        hostapd_wps_clear_ies(hapd, 1);\n', '')
        fixed = replace(fixed, '    hostapd_wps_clear_ies(hapd, 1);\n', '')
        return replace(text, body, HOST_STEPS + '\n' + fixed)
    if relative == 'esp_supplicant/src/esp_hostpad_wps.c':
        text = replace(text, function(text, 'wifi_ap_wps_disable_internal'), DISABLE)
        body = function(text, 'wifi_ap_wps_deinit')
        fixed = replace(body, '\n{\n', '''
{
    if (!esp32qjs_wps_ap_result_deinit_allowed(hostapd_get_hapd_data()))
        return ESP_ERR_INVALID_STATE;
''')
        text = replace(text, body, fixed)
        # A failed close cannot be restarted or reported as enabled again.
        for name in ('wifi_ap_wps_enable_internal', 'wifi_ap_wps_start_internal'):
            body = function(text, name)
            fixed = replace(body, '\n{\n', '''
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    uint32_t identity = esp32qjs_wps_ap_result_identity(hostapd_get_hapd_data());
    if (identity && !esp32qjs_wps_ap_result_context(identity)) return ESP_ERR_INVALID_STATE;
''')
            text = replace(text, body, fixed)
        return text
    if relative == 'esp_supplicant/src/esp_hostap.c':
        text = replace(text, '#include "esp_wps_i.h"', '''#include "esp_wps_i.h"
#ifdef CONFIG_WPS_REGISTRAR
#include "esp32_mquickjs_wifi_wps_ap_result.h"
#endif''')
        body = function(text, 'hostapd_cleanup')
        old = '''#ifdef CONFIG_WPS_REGISTRAR
    if (esp_wifi_get_wps_type_internal() != WPS_TYPE_DISABLE ||
            esp_wifi_get_wps_status_internal() != WPS_STATUS_DISABLE) {
        esp_wifi_ap_wps_disable();
    }
#endif /* CONFIG_WPS_REGISTRAR */
'''
        fixed = replace(body, old, '')
        fixed = replace(fixed, '    if (hapd->wpa_auth) {', '''#ifdef CONFIG_WPS_REGISTRAR
    if (esp32qjs_hostap_close_wps(hapd) != ESP_OK) {
        wpa_printf(MSG_ERROR, "hostapd_cleanup: WPS close incomplete, retaining AP context");
        return;
    }
#endif
    if (hapd->wpa_auth) {''')
        text = replace(text, body, HOST_CLOSE + '\n' + fixed)
        body = function(text, 'hostap_deinit')
        fixed = replace(body, '''#ifdef CONFIG_WPS_REGISTRAR
    wifi_ap_wps_disable_internal();
#endif
''', '')
        fixed = replace(fixed, '    esp_wifi_unset_appie_internal(WIFI_APPIE_WPA);', '''#ifdef CONFIG_WPS_REGISTRAR
    if (esp32qjs_hostap_close_wps(hapd) != ESP_OK) return false;
#endif
    esp_wifi_unset_appie_internal(WIFI_APPIE_WPA);''')
        return replace(text, body, fixed)
    return text
