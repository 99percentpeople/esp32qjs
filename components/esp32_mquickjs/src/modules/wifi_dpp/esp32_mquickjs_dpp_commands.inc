esp_err_t esp32qjs_dpp_native_begin(uint64_t *identity)
{
    if (!identity || *identity) return ESP_ERR_INVALID_ARG;
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    wifi_mode_t mode;
    esp_err_t error = esp_wifi_get_mode(&mode);
    if (error != ESP_OK) return error;
    if ((mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) || wps_get_owner() != WPS_OWNER_NONE)
        return ESP_ERR_INVALID_STATE;
    esp32qjs_wifi_chm_timer_status_t chm;
    esp32qjs_wifi_chm_timer_status(&chm);
    if (chm.error != ESP_OK) return chm.error;
    error = esp32qjs_dpp_result_reserve(identity);
    if (error != ESP_OK) return error;
    return esp_dpp_init(esp32qjs_dpp_ticket_high(*identity), esp32qjs_dpp_ticket_low(*identity));
}

esp_err_t esp32qjs_dpp_native_bootstrap(uint64_t identity, const char *channels,
    const char *private_key_hex_der, const char *info)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity) || s_dpp_result->status.closing ||
        !s_dpp_result->status.attached || s_dpp_result->status.connection_mode || s_dpp_command_identity)
        error = ESP_ERR_INVALID_STATE;
    else {
        s_dpp_command_identity = identity;
        error = esp_supp_dpp_bootstrap_gen(channels, DPP_BOOTSTRAP_QR_CODE, private_key_hex_der, info);
        s_dpp_command_identity = 0;
    }
    dpp_api_unlock(); return error;
}

esp_err_t esp32qjs_dpp_native_listen(uint64_t identity)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity) || !atomic_load(&s_dpp_init_done) ||
        s_dpp_result->status.closing || s_dpp_result->status.terminal || s_dpp_result->status.connection_mode || !s_dpp_ctx.bootstrap_done ||
        s_dpp_ctx.dpp_auth || s_dpp_ctx.dpp_listen_ongoing || atomic_load(&roc_in_progress) ||
        esp_wifi_get_user_init_flag_internal() == 0) error = ESP_ERR_INVALID_STATE;
    else {
        dpp_listen_start(NULL, NULL);
        if (s_dpp_result->status.terminal) error = s_dpp_result->status.error;
    }
    dpp_api_unlock(); return error;
}

esp_err_t esp32qjs_dpp_native_select(uint64_t identity, const esp_dpp_config_data_t *row)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    if (row && !esp32qjs_dpp_config_valid(row)) return ESP_ERR_INVALID_ARG;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity) || !s_dpp_result->status.retained ||
        !s_dpp_result->status.attached || s_dpp_result->status.closing || s_dpp_result->status.terminal ||
        s_dpp_result->status.connection_mode || !atomic_load(&s_dpp_init_done) ||
        s_dpp_ctx.bootstrap_done || s_dpp_ctx.dpp_auth || s_dpp_ctx.dpp_listen_ongoing || s_dpp_command_identity)
        error = ESP_ERR_INVALID_STATE;
    else {
        s_dpp_command_identity = identity;
        error = esp_supp_dpp_set_config(row);
        s_dpp_command_identity = 0;
        if (error == ESP_OK) {
            s_dpp_result->status.connection_mode = true;
            s_dpp_result->status.configuration_installed = row != NULL;
        }
    }
    dpp_api_unlock();
    return error;
}

esp_err_t esp32qjs_dpp_native_close(uint64_t identity)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity)) { dpp_api_unlock(); return ESP_ERR_INVALID_STATE; }
    if (!s_dpp_result->ever_bound) {
        s_dpp_result->status.closing = true;
        s_dpp_result->status.sdk_retired = true;
        s_dpp_result->status.driver_retired = true;
        s_dpp_result->status.event_fenced = true;
        dpp_api_unlock(); return ESP_OK;
    }
    dpp_api_unlock();
    return esp_dpp_deinit(esp32qjs_dpp_ticket_high(identity), esp32qjs_dpp_ticket_low(identity));
}

esp_err_t esp32qjs_dpp_native_check_connection(uint64_t identity, uint8_t expected_akm, const uint8_t bssid[6])
{
    if (!bssid || (expected_akm != ESP_DPP_AKM_DPP && expected_akm != ESP_DPP_AKM_PSK && expected_akm != ESP_DPP_AKM_SAE))
        return ESP_ERR_INVALID_ARG;
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (!esp32qjs_dpp_result_exact(identity) || !s_dpp_result->status.connection_mode ||
        s_dpp_result->status.closing || s_dpp_result->status.terminal || !atomic_load(&s_dpp_init_done))
        error = ESP_ERR_INVALID_STATE;
    else if (gWpaSm.wpa_state != WPA_COMPLETED) error = ESP_ERR_NOT_FINISHED;
    else if (os_memcmp(gWpaSm.bssid, bssid, ETH_ALEN)) error = ESP_ERR_INVALID_RESPONSE;
    else {
        bool match = expected_akm == ESP_DPP_AKM_DPP ? gWpaSm.key_mgmt == WPA_KEY_MGMT_DPP :
            expected_akm == ESP_DPP_AKM_SAE ? gWpaSm.key_mgmt == WPA_KEY_MGMT_SAE :
            (gWpaSm.key_mgmt == WPA_KEY_MGMT_PSK || gWpaSm.key_mgmt == WPA_KEY_MGMT_PSK_SHA256);
        error = match ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    }
    dpp_api_unlock(); return error;
}
