"""Exact managed AP commands and checked client-state retirement."""
from patch_idf_wps import function, replace


def patch_ap_commands(relative: str, text: str) -> str:
    if relative == 'src/ap/wps_hostapd.c':
        body = function(text, 'ap_sta_server_sm_deinit')
        fixed = replace(body, '\tieee802_1x_free_station(hapd, sta);', '''    (void)ctx;
    /* ap_for_each_sta holds STA_LIST. Never block on SAE or recursively
     * acquire the table lock; a busy peer keeps the parent for retry. */
#ifdef CONFIG_SAE
    if (sta->lock && !os_semphr_take(sta->lock, 0)) return 1;
#endif
    ieee802_1x_free_station(hapd, sta);
    wpabuf_free(sta->wps_ie);
    sta->wps_ie = NULL;
#ifdef CONFIG_SAE
    if (sta->lock) os_semphr_give(sta->lock);
#endif''')
        text = replace(text, body, fixed)
        return replace(text, '    ap_for_each_sta(hapd, ap_sta_server_sm_deinit, NULL);',
                       '    if (ap_for_each_sta(hapd, ap_sta_server_sm_deinit, NULL)) return ESP_ERR_NOT_FINISHED;')
    if relative != 'esp_supplicant/src/esp_hostpad_wps.c':
        return text
    for name, argument in (('wifi_ap_wps_enable_internal', 'const esp_wps_config_t *config'),
                           ('wifi_ap_wps_start_internal', 'const unsigned char *pin')):
        body = function(text, name)
        fixed = replace(body, f'{name}({argument})', f'{name}({argument}, uint32_t command_identity)')
        fixed = replace(fixed, '    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;',
                        '    if (!esp32qjs_wps_ap_command_allowed(command_identity)) return ESP_ERR_INVALID_STATE;')
        text = replace(text, body, fixed)
    # These are the ordinary eloop handler calls. They must remain unprivileged
    # even if a managed reservation was created after command submission.
    text = replace(text, 'wifi_ap_wps_enable_internal(config);', 'wifi_ap_wps_enable_internal(config, 0);')
    text = replace(text, 'wifi_ap_wps_start_internal((const unsigned char *)pin);',
                   'wifi_ap_wps_start_internal((const unsigned char *)pin, 0);')
    body = function(text, 'wifi_ap_wps_disable_internal')
    fixed = replace(body, '    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;',
                    '    if (!esp32qjs_wps_ap_command_allowed(0)) return ESP_ERR_INVALID_STATE;')
    text = replace(text, body, fixed)
    return text + '\n#include "esp32qjs_wps_ap_native.inc"\n'
