/* Included in the prepared SDK pairing unit. All commands run on the Wi-Fi
 * task; only committed credentials enter the SDK's two-slot RAM cache. */
_Static_assert(ESP_WIFI_NAN_MAX_PEER_CREDS == ESP32_MQUICKJS_NAN_CACHED_PAIRINGS, "review NAN cache capacity");

typedef struct {
    uint8_t service_id;
    esp32_mquickjs_wifi_nan_credentials_t *out;
} esp32qjs_nan_credentials_command_t;

static int esp32qjs_nan_credentials_command(void *opaque, void *unused)
{
    (void)unused;
    esp32qjs_nan_credentials_command_t *command = opaque;
    if (!s_nan_data_lock) return ESP_ERR_INVALID_STATE;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    NAN_DATA_LOCK();
    const struct own_svc_info *own = nan_find_own_svc(command->service_id);
    if (own && own->pairing.npk_nik_caching) {
        error = ESP_OK;
        for (unsigned i = 0; i < s_nan_ctx.num_peer_creds; ++i) {
            const wifi_nan_peer_creds_t *credential = &s_nan_ctx.peer_creds[i];
            esp32_mquickjs_wifi_nan_credential_info_t info;
            if (nan_peer_cred_npk_present(credential) && !memcmp(credential->service_hash, own->svc_hash, 6) &&
                esp32_mquickjs_wifi_nan_credential_info(i, &info))
                command->out->entries[command->out->count++] = info;
        }
    }
    NAN_DATA_UNLOCK();
    return error;
}

esp_err_t esp32_mquickjs_wifi_nan_pairing_credentials(uint8_t service_id, esp32_mquickjs_wifi_nan_credentials_t *out)
{
    if (!service_id || !out) return ESP_ERR_INVALID_ARG;
    *out = (esp32_mquickjs_wifi_nan_credentials_t){0};
    esp32qjs_nan_credentials_command_t command = {.service_id = service_id, .out = out};
    return current_task_is_wifi_task() ? esp32qjs_nan_credentials_command(&command, NULL) :
        eloop_register_timeout_blocking(esp32qjs_nan_credentials_command, &command, NULL);
}

typedef struct { uint32_t identity; uint32_t *credential_id; } esp32qjs_nan_commit_command_t;
static int esp32qjs_nan_commit_command(void *opaque, void *unused)
{
    (void)unused;
    esp32qjs_nan_commit_command_t *command = opaque;
    esp32qjs_pairing_binding_t *binding = s_esp32qjs_pairing_binding;
    if (!binding || binding->identity != command->identity || !s_nan_data_lock) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_nan_pasn_status_t status;
    esp_err_t error = esp32_mquickjs_wifi_nan_pasn_status(&status);
    if (error) return error;
    if (status.identity != command->identity || !status.active || !status.paired || status.closing ||
        status.traffic_pending || status.error) return status.error ? status.error : ESP_ERR_INVALID_STATE;
    if (binding->committed_credential_id) { *command->credential_id = binding->committed_credential_id; return ESP_OK; }
    if (binding->credential_expires_us && esp_timer_get_time() >= binding->credential_expires_us) return ESP_ERR_TIMEOUT;
    const wifi_nan_peer_creds_t *candidate = &binding->credential;
    if (!nan_peer_cred_npk_present(candidate) ||
        !memcmp(candidate->peer_nik, (uint8_t[ESP_WIFI_NAN_NIK_LEN]){0}, ESP_WIFI_NAN_NIK_LEN)) return ESP_ERR_INVALID_STATE;
    NAN_DATA_LOCK();
    const struct own_svc_info *own = nan_find_own_svc(binding->config.service_id);
    if (!own || !own->pairing.npk_nik_caching || memcmp(own->svc_hash, candidate->service_hash, 6)) {
        error = ESP_ERR_INVALID_STATE; goto done;
    }
    unsigned slot = s_nan_ctx.num_peer_creds;
    for (unsigned i = 0; i < s_nan_ctx.num_peer_creds; ++i)
        if (s_nan_ctx.peer_creds[i].is_valid && !memcmp(s_nan_ctx.peer_creds[i].service_hash, candidate->service_hash, 6) &&
            !memcmp(s_nan_ctx.peer_creds[i].peer_nik, candidate->peer_nik, ESP_WIFI_NAN_NIK_LEN)) { slot = i; break; }
    if (slot == s_nan_ctx.num_peer_creds) {
        for (unsigned i = 0; i < s_nan_ctx.num_peer_creds; ++i) {
            esp32_mquickjs_wifi_nan_credential_info_t info;
            if (!esp32_mquickjs_wifi_nan_credential_info(i, &info)) { slot = i; break; }
        }
    }
    if (slot >= ESP_WIFI_NAN_MAX_PEER_CREDS) { error = ESP_ERR_NO_MEM; goto done; }
    error = esp32_mquickjs_wifi_nan_credential_replace(slot, binding->config.peer,
        binding->credential_expires_us, command->credential_id);
    if (!error) {
        /* Metadata and key bytes are observed together under NAN_DATA_LOCK. */
        forced_memzero(&s_nan_ctx.peer_creds[slot], sizeof(s_nan_ctx.peer_creds[slot]));
        s_nan_ctx.peer_creds[slot] = *candidate;
        if (slot == s_nan_ctx.num_peer_creds) ++s_nan_ctx.num_peer_creds;
        binding->committed_credential_id = *command->credential_id;
    }
done:
    NAN_DATA_UNLOCK();
    return error;
}

esp_err_t esp32_mquickjs_wifi_nan_pairing_commit(uint32_t identity, uint32_t *credential_id)
{
    if (!identity || !credential_id) return ESP_ERR_INVALID_ARG;
    *credential_id = 0;
    esp32qjs_nan_commit_command_t command = {.identity = identity, .credential_id = credential_id};
    return current_task_is_wifi_task() ? esp32qjs_nan_commit_command(&command, NULL) :
        eloop_register_timeout_blocking(esp32qjs_nan_commit_command, &command, NULL);
}
