"""Checked WPS initialization/driver errors and peer validation, build-local.

Called after credential/native guards and before callback wrappers. All source
anchors are checked against the pinned SDK by the parent production generator.
"""
from patch_idf_wps import function, replace


def checked(call, stage):
    return f'esp32qjs_wps_record_error({call}, ESP32QJS_WPS_STAGE_{stage})'


def patch_errors(text: str) -> str:
    old = function(text, 'wps_build_ic_appie_wps_pr')
    new = old.replace('static void ', 'static int ', 1)
    new = replace(new, '        return;', '        return ESP_ERR_INVALID_STATE;', 3)
    new = replace(new, '''    if (!wps_ie) {
        return ESP_ERR_INVALID_STATE;
    }''', '''    if (!wps_ie) return ESP_ERR_NO_MEM;''')
    new = replace(new, '''        wpabuf_free(wps_ie);
        return ESP_ERR_INVALID_STATE;''', '''        wpabuf_free(wps_ie);
        wpabuf_free(extra_ie);
        return ESP_ERR_NO_MEM;''')
    new = replace(new, '    esp_wifi_set_appie_internal(', '    int error = esp_wifi_set_appie_internal(')
    new = replace(new, '    wpabuf_free(extra_ie);\n}', '    wpabuf_free(extra_ie);\n    return error;\n}')
    text = replace(text, old, new)
    old = function(text, 'wps_build_ic_appie_wps_ar')
    new = old.replace('static void ', 'static int ', 1)
    new = replace(new, '''    if (buf) {
        esp_wifi_set_appie_internal(WIFI_APPIE_WPS_AR, (uint8_t *)wpabuf_head(buf), buf->used, 0);
        wpabuf_free(buf);
    }''', '''    if (!buf) return ESP_ERR_NO_MEM;
    int error = esp_wifi_set_appie_internal(WIFI_APPIE_WPS_AR, (uint8_t *)wpabuf_head(buf), buf->used, 0);
    wpabuf_free(buf);
    return error;''')
    text = replace(text, old, new)

    old = function(text, 'wifi_station_wps_init')
    new = replace(old, '    struct wps_config cfg = {0};', '    struct wps_config cfg = {0};\n    int init_error = ESP_FAIL;')
    for condition, stage, label in (
        ('!gWpsSm', 'CONTEXT', '_out'), ('!sm->wps_ctx', 'CONTEXT', '_err'),
        ('wps_cb == NULL', 'CALLBACKS', '_err'), ('!s_wps_sm_cb', 'CALLBACKS', '_err'),
    ):
        new = replace(new, f'    if ({condition}) {{\n        goto {label};\n    }}',
                      f'    if ({condition}) {{\n        init_error = '+checked('ESP_ERR_NO_MEM', stage)+f';\n        goto {label};\n    }}')
    for call, stage, anchor in (
        ('esp_wifi_get_macaddr_internal(WIFI_IF_STA, sm->ownaddr)', 'MAC',
         '    esp_wifi_get_macaddr_internal(WIFI_IF_STA, sm->ownaddr);'),
        ('wps_dev_init()', 'DEVICE', '    if (wps_dev_init() != 0) {\n        goto _err;\n    }'),
        ('wps_init_cfg_pin(&cfg)', 'PIN', '    if (wps_init_cfg_pin(&cfg) < 0) {\n        goto _err;\n    }'),
        ('esp_wifi_set_wps_cb_internal(wps_cb)', 'CALLBACKS',
         '    if (esp_wifi_set_wps_cb_internal(wps_cb) != ESP_OK) {\n        goto _err;\n    }'),
    ):
        new = replace(new, anchor, '    init_error = '+checked(call, stage)+';\n    if (init_error != ESP_OK) goto _err;')
    new = replace(new, '''    if ((sm->wps = wps_init(&cfg)) == NULL) {         /* alloc wps_data */
        goto _err;
    }''', '''    if ((sm->wps = wps_init(&cfg)) == NULL) {
        init_error = esp32qjs_wps_record_error(ESP_FAIL, ESP32QJS_WPS_STAGE_PROTOCOL);
        goto _err;
    }''')
    for suffix,stage in (('pr','PROBE_IE'),('ar','ASSOC_IE')):
        new = replace(new, f'        wps_build_ic_appie_wps_{suffix}();',
                      '        init_error = '+checked(f'wps_build_ic_appie_wps_{suffix}()',stage)+';\n        if (init_error != ESP_OK) goto _err;')
    # A successful earlier call must not hide the later protocol/allocation failure.
    new = new.replace('return ESP_FAIL;', 'return init_error == ESP_OK ? ESP_FAIL : init_error;')
    text = replace(text, old, new)

    old = function(text, 'wps_dev_init')
    new = old
    for name in ('manufacturer','model_name','model_number','device_name','serial_number'):
        new = replace(new, f'    if (!dev->{name}) {{\n        ret = ESP_FAIL;',
                      f'    if (!dev->{name}) {{\n        ret = ESP_ERR_NO_MEM;')
    text = replace(text, old, new)

    old = function(text, 'wifi_wps_enable_internal')
    new = replace(old, '''    if (wps_set_type(config->wps_type) != ESP_OK) {
        return ESP_FAIL;
    }
    if (wps_set_status(WPS_STATUS_DISABLE) != ESP_OK) {
        wps_set_type(WPS_TYPE_DISABLE);
        return ESP_FAIL;
    }''', '''    ret = esp32qjs_wps_record_error(wps_set_type(config->wps_type), ESP32QJS_WPS_STAGE_TYPE);
    if (ret != ESP_OK) return ret;
    ret = esp32qjs_wps_record_error(wps_set_status(WPS_STATUS_DISABLE), ESP32QJS_WPS_STAGE_STATUS);
    if (ret != ESP_OK) {
        if (!s_wps_native) wps_set_type(WPS_TYPE_DISABLE);
        return ret;
    }''')
    new = replace(new, '''        wps_set_type(WPS_TYPE_DISABLE);
        wps_set_status(WPS_STATUS_DISABLE);
        return ESP_FAIL;''', '''        if (!s_wps_native) {
            wps_set_type(WPS_TYPE_DISABLE);
            wps_set_status(WPS_STATUS_DISABLE);
        }
        return ret;''')
    text = replace(text, old, new)
    old = function(text, 'wps_check_wifi_mode')
    text = replace(text, old, replace(old, '        return ESP_FAIL;',
                   '        return esp32qjs_wps_record_error(ret, ESP32QJS_WPS_STAGE_MODE);'))

    old = function(text, 'wifi_station_wps_start')
    new = old
    timer = '    esp32qjs_wps_register_timeout(WPS_TOTAL_TIMEOUT_SECS, 0, wifi_station_wps_timeout);'
    new = replace(new, timer, timer+'\n    if (s_wps_native && s_wps_native->status.error != ESP_OK) return s_wps_native->status.error;', 2)
    new = replace(new, '        '+timer.strip()+'\n    if (s_wps_native',
                  '        '+timer.strip()+'\n        if (s_wps_native')
    new = replace(new, '        wps_build_public_key(sm->wps, NULL);',
                  '        int error = '+checked('wps_build_public_key(sm->wps, NULL)', 'DH')+';\n        if (error != ESP_OK) return error;')
    # The framework retains/restores Station configuration. Managed provisioning
    # must not secretly invoke the SDK's previous-AP auto reconnect policy.
    new = replace(new, '#ifdef CONFIG_WPS_RECONNECT_ON_FAIL\n', '#ifdef CONFIG_WPS_RECONNECT_ON_FAIL\n        if (!s_wps_native) {\n')
    new = replace(new, '#endif\n        esp_wifi_disconnect();', '#endif\n        error = '+checked('esp_wifi_disconnect()', 'DISCONNECT')+';\n        if (error != ESP_OK) return error;')
    new = replace(new, '#endif\n        error =', '        }\n#endif\n        error =')
    scan = '        wifi_wps_scan(esp32qjs_wps_timer_high(), esp32qjs_wps_timer_low());'
    new = replace(new, scan, scan+'\n        if (s_wps_native && s_wps_native->status.terminal_seen) return s_wps_native->status.error;')
    new = replace(new, '    esp_wifi_set_wps_start_flag_internal(true);\n    return ESP_OK;',
                  '    return '+checked('esp_wifi_set_wps_start_flag_internal(true)', 'START_FLAG')+';')
    text = replace(text, old, new)

    old = function(text, 'wifi_wps_scan')
    text = replace(text, old, replace(old, '    esp_wifi_promiscuous_scan_start(NULL, wifi_wps_scan_done);',
                   '    '+checked('esp_wifi_promiscuous_scan_start(NULL, wifi_wps_scan_done)', 'SCAN')+';'))
    old = function(text, 'wifi_wps_scan_done')
    new = replace(old, '''    if (sm->discover_ssid_cnt == 1) {''', '''    if (s_wps_native && status != 0) {
        esp32qjs_wps_record_error((int)status, ESP32QJS_WPS_STAGE_SCAN_DONE);
        return;
    }
    if (sm->discover_ssid_cnt == 1) {''')
    for status in ('PENDING','SCANNING'):
        call = f'wps_set_status(WPS_STATUS_{status})'
        anchor = f'        {call};'
        new = replace(new, anchor, '        if ('+checked(call,'STATUS')+' != ESP_OK) return;', 2 if status=='PENDING' else 1)
    new = replace(new, '        esp_wifi_set_config(0, &wifi_config);',
                  '        wifi_config.sta.failure_retry_cnt = 2;\n        if ('+checked('esp_wifi_set_config(WIFI_IF_STA, &wifi_config)','CONFIG')+' != ESP_OK) return;')
    new = replace(new, '''        wifi_config.sta.failure_retry_cnt = 2;
        esp_wifi_connect();
        sm->state = WAIT_START;''', '''        sm->state = WAIT_START;
        if (esp32qjs_wps_record_error(esp_wifi_connect(), ESP32QJS_WPS_STAGE_CONNECT) != ESP_OK) return;''')
    text = replace(text, old, new)

    old = function(text, 'is_ap_supports_sae')
    new = replace(old, '    struct wpa_ie_data rsn_info;', '    struct wpa_ie_data rsn_info = {0};')
    new = replace(new, '    wpa_parse_wpa_ie_rsn(scan->rsn, scan->rsn[1] + 2, &rsn_info);',
                  '    if (wpa_parse_wpa_ie_rsn(scan->rsn, scan->rsn[1] + 2, &rsn_info) < 0) return false;')
    text = replace(text, old, new)
    old = function(text, 'wps_parse_scan_result')
    new = replace(old, '    if (!sm) {', '    if (!sm || !scan) {')
    new = replace(new, '    esp_wifi_get_mode(&op_mode);', '    if ('+checked('esp_wifi_get_mode(&op_mode)','MODE')+' != ESP_OK) return false;')
    new = replace(new, '            wpa_printf(MSG_DEBUG, "WPS: Failed to copy WPS IE");',
                  '            esp32qjs_wps_record_error(ESP_ERR_NO_MEM, ESP32QJS_WPS_STAGE_SCAN);')
    new = replace(new, '                    wps_build_ic_appie_wps_ar();', '''                    if (esp32qjs_wps_record_error(wps_build_ic_appie_wps_ar(), ESP32QJS_WPS_STAGE_ASSOC_IE) != ESP_OK) {
                        wpabuf_free(buf);
                        return false;
                    }''')
    new = replace(new, '            wpa_printf(MSG_INFO, "WPS AP discovered: %s", (char *)sm->creds[0].ssid);',
                  '            wpa_printf(MSG_INFO, "WPS AP discovered");')
    text = replace(text, old, new)

    # BSSID lookup/TX errors keep their original codes; retained result records
    # make it safe to stop publication without freeing under this call stack.
    text = replace(text, '''        wpa_printf(MSG_ERROR, "WPS: BSSID is empty, cannot send EAPOL frame");
        return -1;''', '''        wpa_printf(MSG_ERROR, "WPS: BSSID is empty, cannot send EAPOL frame");
        return esp32qjs_wps_record_error(ret, ESP32QJS_WPS_STAGE_EAPOL_BSSID);''')
    text = replace(text, '    return wpa_ether_send(sm, bssid, proto, data, data_len);',
                  '    return '+checked('wpa_ether_send(sm, bssid, proto, data, data_len)','EAPOL_TX')+';')
    old = function(text, 'wps_send_eapol_frame')
    new = replace(old, '    int len;', '    size_t len = 0;')
    new = replace(new, '    buf = wps_sm_alloc_eapol(sm, eapol_type, payload, payload_len, (size_t *)&len, NULL);', '''    if (payload_len > UINT16_MAX || (!payload && payload_len))
        return esp32qjs_wps_record_error(ESP_ERR_INVALID_SIZE, ESP32QJS_WPS_STAGE_EAPOL_TX);
    buf = wps_sm_alloc_eapol(sm, eapol_type, payload, payload_len, &len, NULL);''')
    new = replace(new, '        return ESP_ERR_NO_MEM;', '        return '+checked('ESP_ERR_NO_MEM','EAPOL_TX')+';')
    new = replace(new, '    wps_sm_free_eapol(buf);', '    forced_memzero(buf, len);\n    wps_sm_free_eapol(buf);')
    new = replace(new, '''        wpa_printf(MSG_ERROR, "WPS: EAPOL send failed (ret=%d)", ret);
        return ESP_FAIL;''', '''        wpa_printf(MSG_ERROR, "WPS: EAPOL send failed (ret=%d)", ret);
        return ret;''')
    text = replace(text, old, new)
    old = function(text, 'wps_tx_start')
    new = replace(old, '''    if (wps_send_eapol_frame(IEEE802_1X_TYPE_EAPOL_START, (u8 *)"", 0) != ESP_OK) {
        return ESP_FAIL;
    }''', '''    int error = wps_send_eapol_frame(IEEE802_1X_TYPE_EAPOL_START, (u8 *)"", 0);
    if (error != ESP_OK) return error;''')
    new = replace(new, '    esp32qjs_wps_register_timeout(EAPOL_START_HANDLE_TIMEOUT_SECS, 0, wifi_station_wps_eapol_start_handle);\n\n    return ESP_OK;',
                  '    return esp32qjs_wps_register_timeout(EAPOL_START_HANDLE_TIMEOUT_SECS, 0, wifi_station_wps_eapol_start_handle);')
    text = replace(text, old, new)
    old = function(text, 'wps_sm_rx_eapol')
    new = replace(old, '    if (len < sizeof(*hdr) + sizeof(*ehdr)) {', '''    if (!src_addr || !buf) return ESP_OK;
    if (s_wps_native && !esp32qjs_wps_rx_peer(sm, src_addr)) return ESP_OK;
    if (len < sizeof(*hdr) + sizeof(*ehdr)) {''')
    new = replace(new, '    wpa_hexdump(MSG_MSGDUMP, "WPS: RX EAPOL-EAP Packet", tmp, len);', '')
    new = replace(new, '        wps_handle_failure(WPS_FAIL_REASON_NORMAL);',
                  '        esp32qjs_wps_record_error(ret ? ret : ESP_FAIL, ESP32QJS_WPS_STAGE_EAPOL_RX);\n        wps_handle_failure(WPS_FAIL_REASON_NORMAL);')
    text = replace(text, old, new)

    for name in ('wps_eap_wsc_prepare_rx_buf','wps_process_wps_mX_req',
                 'wps_send_eap_identity_rsp','wps_send_frag_ack','wps_send_wps_mX_rsp'):
        old = function(text, name)
        text = replace(text, old, old.replace('wpabuf_free(', 'wpabuf_clear_free('))
    old = function(text, 'wps_eap_wsc_process_fragment')
    new = replace(old, '''            wpa_printf(MSG_DEBUG, "EAP-WSC: No memory for message");
            return -1;''', '''            return esp32qjs_wps_record_error(ESP_ERR_NO_MEM, ESP32QJS_WPS_STAGE_FRAGMENT);''')
    # Caller tests < 0, so preserve that ABI while retaining the real OOM code.
    new = replace(new, 'return esp32qjs_wps_record_error(ESP_ERR_NO_MEM, ESP32QJS_WPS_STAGE_FRAGMENT);',
                  'esp32qjs_wps_record_error(ESP_ERR_NO_MEM, ESP32QJS_WPS_STAGE_FRAGMENT);\n            return -1;')
    return replace(text, old, new)
