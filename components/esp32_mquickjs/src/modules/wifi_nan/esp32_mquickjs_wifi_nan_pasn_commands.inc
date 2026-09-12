/* Replace deferred pointer-carrying initialization with one checked Wi-Fi
 * task command. The pinned blocking eloop helper either completes dispatch or
 * revokes it before returning, so the stack request and PIN stay owned here. */
typedef enum {
    PASN_START_INITIATOR, PASN_START_RESPONDER, PASN_START_VERIFY, PASN_START_RESPONDER_VERIFY,
    PASN_SNAPSHOT, PASN_CLOSE, PASN_SHUTDOWN
} esp32qjs_pasn_command_kind_t;
typedef struct {
    esp32qjs_pasn_command_kind_t kind;
    uint8_t peer[6];
    uint32_t pincode, lifetime, identity;
    esp_nan_pairing_key_installed_cb_t callback;
    esp32_mquickjs_wifi_nan_pasn_status_t *output;
} esp32qjs_pasn_command_t;

static int esp32qjs_pasn_command_run(void *opaque, void *unused)
{
    (void)unused;
    esp32qjs_pasn_command_t *command = opaque;
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    struct nan_pasn_data *pd = esp_nan_app_get_pasn_data();
    if (command->kind == PASN_SNAPSHOT) {
        *command->output = s_esp32qjs_pasn_status;
        int64_t tx_completed_us = 0;
        esp_err_t tx_error = ESP_OK;
        bool tx_settled = esp32_mquickjs_wifi_nan_tx_pairing_settled(command->output->identity,
            &tx_error, &tx_completed_us);
        command->output->traffic_pending = esp32_mquickjs_wifi_nan_pairing_followup_pending() || !tx_settled;
        if (!command->output->error) command->output->error = tx_error;
        if (tx_completed_us > command->output->completed_us) command->output->completed_us = tx_completed_us;
        return ESP_OK;
    }
    if (command->kind == PASN_CLOSE || command->kind == PASN_SHUTDOWN) {
        if (command->kind == PASN_CLOSE && (!command->identity ||
            command->identity != s_esp32qjs_pasn_status.identity)) return ESP_ERR_INVALID_STATE;
        s_esp32qjs_pasn_status.closing = true;
        esp_err_t error = esp32_mquickjs_wifi_nan_pairing_cancel_pending();
        if (error != ESP_OK) return error;
        if (command->kind == PASN_CLOSE &&
            !esp32_mquickjs_wifi_nan_tx_pairing_drained(s_esp32qjs_pasn_status.identity)) return ESP_ERR_TIMEOUT;
        /* Parent shutdown has sealed the shared TX pool and owns STOP plus
         * its drain. The reviewed producer copies all frame bytes before
         * returning: no queued EB retains pd/config. Retire PASN callbacks
         * here so STOP can drain queued EBs; never grant a new pairing lane
         * from this parent-only operation while that Radio is closing. */
        if (pd) nan_pasn_data_deinit(pd);
        nan_pasn_clear_saved_keys();
        forced_memzero(&pasn_nd_pmk_global, sizeof(pasn_nd_pmk_global));
        esp32_mquickjs_wifi_nan_pairing_release_binding();
        return ESP_OK;
    }
    /* Even same-peer initialization is a new operation. It cannot overwrite
     * an accepted PIN or delete the context that an earlier operation owns. */
    if (pd || (s_esp32qjs_pasn_status.identity && !s_esp32qjs_pasn_status.closing)) return ESP_ERR_INVALID_STATE;
    bool verification = command->kind == PASN_START_VERIFY || command->kind == PASN_START_RESPONDER_VERIFY;
    if (!esp32qjs_pasn_peer_valid(command->peer) || !command->callback ||
        (!verification && command->pincode > 999999)) return ESP_ERR_INVALID_ARG;
    if (!esp32_mquickjs_wifi_nan_pairing_services(command->peer, NULL, NULL)) return ESP_ERR_INVALID_STATE;
    pd = nan_pasn_data_init();
    if (!pd) return s_esp32qjs_pasn_create_error;
    esp_nan_app_set_pasn_data(pd);
    memcpy(s_esp32qjs_pasn_status.peer, command->peer, 6);
    pd->pairing_key_installed_cb = command->callback;
    pd->nik_lifetime_sec = command->lifetime;
    pd->pairing_verification = verification;
    char pin_digits[16] __attribute__((cleanup(esp32qjs_pasn_pin_clear))) = {0};
    int result = ESP_FAIL;
    if (!verification) {
        int n = os_snprintf(pin_digits, sizeof(pin_digits), "%06u", (unsigned)command->pincode);
        if (os_snprintf_error(sizeof(pin_digits), n) || nan_set_dev_sae_pin(pd, pin_digits)) goto failed;
    }
    if (command->kind == PASN_START_RESPONDER || command->kind == PASN_START_RESPONDER_VERIFY) {
        pd->dev_role = NAN_ROLE_PAIRING_RESPONDER;
        result = nan_pasn_initialize(pd, command->peer);
        if (result != ESP_OK) goto failed;
        result = ESP_FAIL;
        if (!pd->pasn || nan_prepare_pasn_extra_ie(pd, pd->pasn, NULL, verification)) goto failed;
        /* Also bounds the accepted responder while no Auth1 arrives. */
        if (nan_pasn_auth_timeout_arm(pd, NAN_ROLE_PAIRING_RESPONDER)) goto failed;
        result = ESP_OK;
    } else if (command->kind == PASN_START_VERIFY) {
        result = nan_initiate_pasn_verify(pd, command->peer, 0, NULL, NULL, 0) ? ESP_FAIL : ESP_OK;
    } else {
        result = nan_pasn_auth_initiate(pd, command->peer) ? ESP_FAIL : ESP_OK;
    }
    if (result == ESP_OK) return ESP_OK;
failed:
    esp32qjs_pasn_failure(pd, result);
    nan_pasn_data_deinit(pd);
    return result;
}

static esp_err_t esp32qjs_pasn_command(esp32qjs_pasn_command_t *command)
{
    esp_err_t error = current_task_is_wifi_task() ? esp32qjs_pasn_command_run(command, NULL) :
        eloop_register_timeout_blocking(esp32qjs_pasn_command_run, command, NULL);
    forced_memzero(command, sizeof(*command));
    return error;
}

static int esp32qjs_pasn_start(esp32qjs_pasn_command_kind_t kind, const uint8_t *peer,
    uint32_t pin, uint32_t lifetime, esp_nan_pairing_key_installed_cb_t callback)
{
    /* Reject before allocation, queuing or disclosing a credential in logs. */
    if (!esp32qjs_pasn_peer_valid(peer) || !callback ||
        (kind != PASN_START_VERIFY && kind != PASN_START_RESPONDER_VERIFY && pin > 999999)) return -1;
    esp32qjs_pasn_command_t command = {.kind = kind, .pincode = pin,
        .lifetime = lifetime, .callback = callback};
    memcpy(command.peer, peer, 6);
    return esp32qjs_pasn_command(&command) == ESP_OK ? 0 : -1;
}

int esp_nan_supp_pasn_initiator_auth(const uint8_t *peer, uint32_t pin,
    uint32_t lifetime, esp_nan_pairing_key_installed_cb_t callback)
{ return esp32qjs_pasn_start(PASN_START_INITIATOR, peer, pin, lifetime, callback); }

int esp_nan_supp_pasn_responder_init(const uint8_t *peer, uint32_t pin,
    uint32_t lifetime, esp_nan_pairing_key_installed_cb_t callback)
{ return esp32qjs_pasn_start(PASN_START_RESPONDER, peer, pin, lifetime, callback); }

int esp_nan_supp_pasn_initiator_verify(const uint8_t *peer, esp_nan_pairing_key_installed_cb_t callback)
{ return esp32qjs_pasn_start(PASN_START_VERIFY, peer, 0, 0, callback); }

esp_err_t esp32_mquickjs_wifi_nan_pasn_verify_responder(const uint8_t peer[6])
{
    return esp32qjs_pasn_start(PASN_START_RESPONDER_VERIFY, peer, 0, 0,
        esp_nan_pairing_get_key_installed_cb()) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t esp32_mquickjs_wifi_nan_pasn_status(esp32_mquickjs_wifi_nan_pasn_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    esp32qjs_pasn_command_t command = {.kind = PASN_SNAPSHOT, .output = status};
    return esp32qjs_pasn_command(&command);
}

esp_err_t esp32_mquickjs_wifi_nan_pasn_close(uint32_t identity)
{
    if (!identity) return ESP_ERR_INVALID_ARG;
    esp32qjs_pasn_command_t command = {.kind = PASN_CLOSE, .identity = identity};
    return esp32qjs_pasn_command(&command);
}

esp_err_t esp32_mquickjs_wifi_nan_pasn_shutdown(void)
{
    esp32qjs_pasn_command_t command = {.kind = PASN_SHUTDOWN};
    return esp32qjs_pasn_command(&command);
}
