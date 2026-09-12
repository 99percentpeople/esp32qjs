/* The public adapter will pass its retained Service identity and exact native
 * IDs here. No peer-only or first-cache fallback can establish this binding. */
typedef struct {
    esp32_mquickjs_wifi_nan_pairing_config_t config;
    esp32_mquickjs_wifi_nan_pasn_status_t *status;
} esp32qjs_pairing_start_t;

static int esp32qjs_pairing_bound_start(void *opaque, void *unused)
{
    (void)unused;
    esp32qjs_pairing_start_t *command = opaque;
    const esp32_mquickjs_wifi_nan_pairing_config_t *config = &command->config;
    if (s_esp32qjs_pairing_binding || esp_nan_app_get_pasn_data() ||
        esp32_mquickjs_wifi_nan_pasn_identity(NULL) || !s_nan_data_lock) return ESP_ERR_INVALID_STATE;
    if (esp32_mquickjs_wifi_nan_ndp_peer_busy(config->peer)) return ESP_ERR_INVALID_STATE;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    esp32qjs_pairing_binding_t *binding = NULL;
    NAN_DATA_LOCK();
    struct own_svc_info *own = nan_find_own_svc(config->service_id);
    struct peer_svc_info *peer = nan_find_peer_svc_exact(config->service_id,
        config->peer_service_id, config->peer);
    const wifi_nan_peer_creds_t *credential = NULL;
    int64_t credential_expires_us = 0;
    if (!own || !peer || (config->verification ? !own->pairing.pairing_verification : !own->pairing.pairing_setup))
        goto unlock;
    if (config->verification) {
        for (unsigned i = 0; i < s_nan_ctx.num_peer_creds; ++i) {
            esp32_mquickjs_wifi_nan_credential_info_t info;
            if (esp32_mquickjs_wifi_nan_credential_info(i, &info) && info.identity == config->credential_id) {
                credential = &s_nan_ctx.peer_creds[i]; credential_expires_us = info.expires_us; break;
            }
        }
        if (!credential) { error = ESP_ERR_INVALID_ARG; goto unlock; }
        if (!nan_peer_cred_npk_present(credential) ||
            !memcmp(credential->peer_nik, (uint8_t[ESP_WIFI_NAN_NIK_LEN]){0}, ESP_WIFI_NAN_NIK_LEN) ||
            memcmp(credential->service_hash, own->svc_hash, 6)) {
            error = ESP_ERR_INVALID_ARG;
            goto unlock;
        }
    }
    binding = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*binding),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!binding) { error = ESP_ERR_NO_MEM; goto unlock; }
    binding->config = *config;
    memcpy(binding->service_hash, own->svc_hash, 6);
    if (credential) { binding->credential = *credential; binding->credential_expires_us = credential_expires_us; }
    s_esp32qjs_pairing_binding = binding;
    if (!config->verification) {
        peer->has_nik = false;
        forced_memzero(peer->peer_nik, sizeof(peer->peer_nik));
    } else {
        /* The selected NIK remains provisional until this operation's NIRA
         * and PASN checks succeed. It is never an admission shortcut. */
        memcpy(peer->peer_nik, credential->peer_nik, sizeof(peer->peer_nik));
        peer->has_nik = true;
    }
    error = ESP_OK;
unlock:
    NAN_DATA_UNLOCK();
    if (error != ESP_OK) return error;
    esp32_mquickjs_wifi_nan_pasn_status_t before;
    esp32_mquickjs_wifi_nan_pasn_status(&before);
    wifi_nan_pairing_config_t native = {.peer_svc_id = config->peer_service_id,
        .self_role = config->initiator ? NAN_PAIRING_ROLE_INITIATOR : NAN_PAIRING_ROLE_RESPONDER,
        .pairing_verification = config->verification, .cred.pincode = config->pincode};
    memcpy(native.peer_nmi, config->peer, 6);
    error = esp32qjs_pairing_start_native(&native);
    forced_memzero(&native, sizeof(native));
    /* The PASN config owns the PIN now; the binding only needs identities. */
    binding->config.pincode = 0;
    esp32_mquickjs_wifi_nan_pasn_status(command->status);
    if (command->status->identity == before.identity)
        *command->status = (esp32_mquickjs_wifi_nan_pasn_status_t){0};
    binding->identity = command->status->identity;
    if (error != ESP_OK && !esp32_mquickjs_wifi_nan_pasn_identity(config->peer))
        esp32_mquickjs_wifi_nan_pairing_release_binding();
    return error;
}

esp_err_t esp32_mquickjs_wifi_nan_pairing_start(
    const esp32_mquickjs_wifi_nan_pairing_config_t *config,
    esp32_mquickjs_wifi_nan_pasn_status_t *status)
{
    if (!config || !status) return ESP_ERR_INVALID_ARG;
    *status = (esp32_mquickjs_wifi_nan_pasn_status_t){0};
    if (!config->service_identity || !config->service_id || !config->peer_service_id ||
        (config->peer[0] & 1) || !memcmp(config->peer, (uint8_t[6]){0}, 6) ||
        (config->verification ? !config->credential_id || config->pincode != 0 :
            config->credential_id != 0 || config->pincode > NAN_PAIRING_PINCODE_MAX))
        return ESP_ERR_INVALID_ARG;
    esp32qjs_pairing_start_t command = {.config = *config, .status = status};
    esp_err_t error = current_task_is_wifi_task() ? esp32qjs_pairing_bound_start(&command, NULL) :
        eloop_register_timeout_blocking(esp32qjs_pairing_bound_start, &command, NULL);
    forced_memzero(&command, sizeof(command));
    return error;
}
