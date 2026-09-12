"""Connect the AP result owner to reviewed SDK initialization and event sites."""
import re
from patch_idf_wps import function, replace


def patch_ap_result(relative: str, text: str) -> str:
    if relative == 'src/ap/wps_hostapd.c':
        text = replace(text, '#include "esp_wps_i.h"',
                       '#include "esp_wps_i.h"\n#include "esp32_mquickjs_wifi_wps_ap_result.h"')
        text = replace(text, '    ret = hostapd_wps_config_ap(hapd, wps_data);', '''    uint32_t result_identity = esp32qjs_wps_ap_result_identity(hapd);
    if (!result_identity) return ESP_ERR_INVALID_STATE;
    ret = hostapd_wps_config_ap(hapd, wps_data);''')
        text = replace(text, '    wps->cb_ctx = hapd;',
                       '    wps->cb_ctx = (void *)(uintptr_t)result_identity;')
        body = function(text, 'hostapd_wps_event_cb')
        fixed = replace(body, '\tstruct hostapd_data *hapd = ctx;', '''    struct hostapd_data *hapd = esp32qjs_wps_ap_result_context((uint32_t)(uintptr_t)ctx);
    if (!hapd) return;''')
        text = replace(text, body, fixed)
        pattern = r'esp_event_post\(WIFI_EVENT,\s*(WIFI_EVENT_AP_WPS_\w+),([\s\S]*?),\s*OS_BLOCK\);'
        text, count = re.subn(pattern, r'esp32qjs_wps_ap_result_event(hapd, \1,\2);', text)
        if count != 6: raise ValueError('Expected six AP WPS event sites')
        text = text.replace('wifi_event_ap_wps_rg_success_t evt;', 'wifi_event_ap_wps_rg_success_t evt = {0};')
        text = text.replace('wifi_event_ap_wps_rg_fail_reason_t evt;', 'wifi_event_ap_wps_rg_fail_reason_t evt = {0};')
        return text
    if relative != 'esp_supplicant/src/esp_hostpad_wps.c': return text
    text = replace(text, 'static int wps_reg_eloop_post_block(uint32_t sig, void *arg);',
                   'static int wps_reg_eloop_post_block(uint32_t sig, void *arg);\n\n#include "esp32qjs_wps_ap_result.inc"')
    body = function(text, 'wifi_ap_wps_init')
    fixed = replace(body, '    int ret = ESP_FAIL;', '    int ret = ESP_FAIL;\n    bool result_bound = false;')
    fixed = replace(fixed, '    wpa_printf(MSG_DEBUG, "wifi wps init");', '''    ret = esp32qjs_wps_ap_result_bind(hapd);
    if (ret != ESP_OK) goto _out;
    result_bound = true;
    wpa_printf(MSG_DEBUG, "wifi wps init");''')
    fixed = replace(fixed, '        esp_event_post(WIFI_EVENT, WIFI_EVENT_AP_WPS_RG_PIN, &evt, sizeof(evt), OS_BLOCK);',
                    '        esp32qjs_wps_ap_result_event(hapd, WIFI_EVENT_AP_WPS_RG_PIN, &evt, sizeof(evt));')
    fixed = replace(fixed, '_out:\n', '_out:\n    if (result_bound) esp32qjs_wps_ap_result_detach(hapd, ret);\n')
    text = replace(text, body, fixed)
    body = function(text, 'wifi_ap_wps_enable_internal')
    fixed = replace(body, '    wpa_printf(MSG_DEBUG, "Set factory information.");',
                    '    if (!esp32qjs_wps_ap_result_can_bind()) return ESP_ERR_INVALID_STATE;\n\n'
                    '    wpa_printf(MSG_DEBUG, "Set factory information.");')
    text = replace(text, body, fixed)
    body = function(text, 'wifi_ap_wps_deinit')
    fixed = replace(body, '    return ESP_OK;',
                    '    esp32qjs_wps_ap_result_detach(hostapd_get_hapd_data(), ESP_OK);\n    return ESP_OK;')
    return replace(text, body, fixed)
