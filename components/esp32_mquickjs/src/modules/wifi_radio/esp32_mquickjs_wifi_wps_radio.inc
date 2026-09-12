/* Included in Radio so all admission, driver writes and release share its
 * operation mutex. No driver call executes inside the snapshot critical section. */
static bool wifi_radio_wps_exact_locked(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    return token && token->identity && s_wps_radio && s_radio.driver_owned &&
        token->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_WPS &&
        token->identity == s_radio.operation.identity && token->generation == s_radio.generation &&
        token->generation == s_radio.operation.generation && token->lease_identity == s_radio.operation.lease_identity &&
        token->kind == s_radio.operation.kind;
}

static bool wifi_radio_wps_fenced_locked(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    taskENTER_CRITICAL(&s_radio.lock);
    bool ready = token->identity && s_wps_fence_seen == token->identity;
    taskEXIT_CRITICAL(&s_radio.lock);
    return ready;
}

static esp_err_t wifi_radio_wps_snapshot_locked(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status)
{
    *status = (esp32_mquickjs_wifi_wps_radio_status_t){.error = ESP_ERR_INVALID_STATE,
        .stage = "wps-radio-identity"};
    if (!wifi_radio_wps_exact_locked(token)) return ESP_ERR_INVALID_STATE;
    status->operation = *token;
    status->config_restored = s_wps_radio->config_restored;
    status->channel_restored = s_wps_radio->channel_restored;
    status->storage_restored = s_wps_radio->storage_restored;
    status->event_fenced = wifi_radio_wps_fenced_locked(token);
    esp_err_t error = esp32_mquickjs_wifi_wps_worker_status(s_wps_radio->worker, &status->worker);
    status->reserved_bytes = sizeof(*s_wps_radio) + status->worker.reserved_bytes;
    status->error = error != ESP_OK ? error : s_wps_radio->error != ESP_OK ?
        s_wps_radio->error : status->worker.error;
    status->stage = s_wps_radio->stage ? s_wps_radio->stage : status->worker.stage;
    return error;
}

static esp_err_t wifi_radio_wps_restore_locked(void)
{
    wifi_radio_wps_t *binding = s_wps_radio;
    esp32_mquickjs_wifi_wps_worker_status_t worker;
    esp_err_t error = esp32_mquickjs_wifi_wps_worker_status(binding->worker, &worker);
    if (error != ESP_OK) return error;
    if (worker.handoff_unknown || !worker.capture_retired) return ESP_ERR_INVALID_STATE;
    /* Failed foreign-native admission must not write another owner's config or
     * channel. Native enable alone doesn't submit a scan/Station config. */
    if (!binding->start_attempted) binding->config_restored = binding->channel_restored = true;
    if (!binding->storage_attempted) binding->storage_restored = true;
    if (!s_radio.started || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED)
        return ESP_ERR_INVALID_STATE;
    if (!binding->config_restored || !binding->channel_restored) {
        wifi_ap_record_t ap = {0};
        binding->stage = "wps-disconnect-readback";
        error = esp_wifi_sta_get_ap_info(&ap);
        esp32_mquickjs_wireless_secure_zero(&ap, sizeof(ap));
        if (error != ESP_ERR_WIFI_NOT_CONNECT) return error == ESP_OK ? ESP_ERR_NOT_FINISHED : error;
    }
    if (!binding->config_restored) {
        /* Never restore credentials while storage is unknown or FLASH. */
        if (!s_radio.storage_configured || s_radio.storage != WIFI_STORAGE_RAM) return ESP_ERR_INVALID_STATE;
        binding->stage = "wps-config-readback";
        error = esp_wifi_get_config(WIFI_IF_STA, &binding->scratch);
        bool matches = error == ESP_OK && wifi_radio_config_equal(WIFI_IF_STA, &binding->saved, &binding->scratch);
        esp32_mquickjs_wireless_secure_zero(&binding->scratch, sizeof(binding->scratch));
        if (error != ESP_OK) return error;
        if (!matches) {
            binding->stage = "wps-config-restore";
            error = esp_wifi_set_config(WIFI_IF_STA, &binding->saved);
            if (error != ESP_OK) return error;
            binding->stage = "wps-config-verify";
            error = esp_wifi_get_config(WIFI_IF_STA, &binding->scratch);
            matches = error == ESP_OK && wifi_radio_config_equal(WIFI_IF_STA, &binding->saved, &binding->scratch);
            esp32_mquickjs_wireless_secure_zero(&binding->scratch, sizeof(binding->scratch));
            if (error != ESP_OK) return error;
            if (!matches) return ESP_ERR_INVALID_RESPONSE;
        }
        binding->config_restored = true;
    }
    if (!binding->channel_restored) {
        uint8_t primary;
        wifi_second_chan_t secondary;
        uint32_t generation;
        binding->stage = "wps-channel-readback";
        error = wifi_radio_get_channel_locked(&primary, &secondary, &generation);
        if (error != ESP_OK) return error;
        if (primary != binding->home_primary || secondary != binding->home_secondary) {
            binding->stage = "wps-channel-restore";
            error = wifi_radio_validate_regulatory_channel(binding->home_primary);
            if (error == ESP_OK) error = esp_wifi_set_channel(binding->home_primary, binding->home_secondary);
            if (error == ESP_OK) error = wifi_radio_get_channel_locked(&primary, &secondary, &generation);
            if (error != ESP_OK) return error;
            if (primary != binding->home_primary || secondary != binding->home_secondary)
                return ESP_ERR_INVALID_RESPONSE;
        }
        binding->channel_restored = true;
    }
    if (!binding->storage_restored) {
        binding->stage = "wps-storage-restore";
        if (!s_radio.storage_configured || s_radio.storage != binding->storage) {
            error = esp_wifi_set_storage(binding->storage);
            taskENTER_CRITICAL(&s_radio.lock);
            s_radio.storage_configured = error == ESP_OK;
            if (error == ESP_OK) s_radio.storage = binding->storage;
            taskEXIT_CRITICAL(&s_radio.lock);
            if (error != ESP_OK) return error;
        }
        binding->storage_restored = true;
    }
    binding->stage = NULL;
    return ESP_OK;
}

static esp_err_t wifi_radio_wps_fence_locked(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    taskENTER_CRITICAL(&s_radio.lock);
    bool ready = s_wps_fence_seen == token->identity;
    bool post = !ready && s_wps_fence_posted != token->identity;
    if (post) s_wps_fence_posted = token->identity;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (post) {
        esp_err_t error = esp_event_post(ESP32QJS_WIFI_RADIO_CONTROL_EVENT, WIFI_RADIO_WPS_FENCE_EVENT,
            token, sizeof(*token), 0);
        if (error != ESP_OK) {
            taskENTER_CRITICAL(&s_radio.lock);
            s_wps_fence_posted = 0;
            taskEXIT_CRITICAL(&s_radio.lock);
            return error;
        }
    }
    return ready ? ESP_OK : ESP_ERR_NOT_FINISHED;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_begin(
    const esp32_mquickjs_wifi_radio_lease_t owners[3], bool allow_ap_channel_change,
    const esp_wps_config_t *config, esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status)
{
    if (!owners || !config || !token || !status || token->identity || token->generation || token->lease_identity || token->kind)
        return ESP_ERR_INVALID_ARG;
    *status = (esp32_mquickjs_wifi_wps_radio_status_t){.stage = "wps-radio-admission"};
    /* create validates all input before any driver query or mutation. */
    esp32_mquickjs_wifi_wps_worker_t *worker = NULL;
    esp_err_t error = esp32_mquickjs_wifi_wps_worker_create(config, &worker);
    if (error != ESP_OK) { status->error = error; status->stage = "wps-options"; return error; }
    wifi_radio_operation_lock();
    wifi_radio_wps_t *binding = NULL;
    if (WIFI_RADIO_DPP_PENDING || WIFI_RADIO_WPS_PENDING || WIFI_RADIO_SMARTCONFIG_PENDING || WIFI_RADIO_EAP_PENDING) {
        error = ESP_ERR_INVALID_STATE; goto done;
    }
    error = wifi_radio_connection_owner_locked(&owners[0], &owners[1], &owners[2], WIFI_IF_STA, false);
    if (error != ESP_OK) goto done;
    if (s_interval.owner.identity || s_interval.uncertain) { error = ESP_ERR_INVALID_STATE; goto done; }
    if ((s_radio.effective_mode & WIFI_MODE_AP) && (!allow_ap_channel_change || !owners[2].acquired)) {
        error = ESP_ERR_INVALID_STATE; goto done;
    }
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity && s_radio.leases[i].fixed_channel) { error = ESP_ERR_INVALID_STATE; goto done; }
    wifi_ap_record_t ap = {0};
    error = esp_wifi_sta_get_ap_info(&ap);
    esp32_mquickjs_wireless_secure_zero(&ap, sizeof(ap));
    if (error != ESP_ERR_WIFI_NOT_CONNECT) { if (error == ESP_OK) error = ESP_ERR_INVALID_STATE; goto done; }
    if (!s_radio.next_operation_identity) { error = ESP_ERR_NO_MEM; goto done; }
    binding = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*binding), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!binding) { error = ESP_ERR_NO_MEM; goto done; }
    uint32_t generation;
    error = wifi_radio_get_channel_locked(&binding->home_primary, &binding->home_secondary, &generation);
    if (error == ESP_OK) error = wifi_radio_validate_regulatory_channel(binding->home_primary);
    if (error == ESP_OK) error = esp_wifi_get_config(WIFI_IF_STA, &binding->saved);
    if (error != ESP_OK) goto done;
    binding->storage = s_radio.storage;
    for (unsigned i = 0; i < 3; ++i) binding->owners[i] = owners[i].acquired ? owners[i].identity : 0;
    binding->worker = worker;
    s_wps_radio = binding;
    worker = NULL;
    taskENTER_CRITICAL(&s_radio.lock);
    s_wps_fence_posted = s_wps_fence_seen = 0;
    s_radio.operation = (esp32_mquickjs_wifi_radio_operation_t){.generation = s_radio.generation,
        .identity = s_radio.next_operation_identity++, .lease_identity = owners[1].identity,
        .kind = ESP32_MQUICKJS_WIFI_RADIO_OPERATION_WPS};
    *token = s_radio.operation;
    taskEXIT_CRITICAL(&s_radio.lock);
    binding->stage = "wps-native-enable";
    error = esp32_mquickjs_wifi_wps_worker_prepare(binding->worker);
    if (error == ESP_OK) {
        binding->stage = "wps-storage-ram";
        binding->storage_attempted = true;
        if (s_radio.storage != WIFI_STORAGE_RAM) {
            error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
            taskENTER_CRITICAL(&s_radio.lock);
            s_radio.storage_configured = error == ESP_OK;
            if (error == ESP_OK) s_radio.storage = WIFI_STORAGE_RAM;
            taskEXIT_CRITICAL(&s_radio.lock);
        }
    }
    if (error == ESP_OK) {
        binding->stage = "wps-native-start";
        binding->start_attempted = true;
        error = esp32_mquickjs_wifi_wps_worker_start(binding->worker);
    }
    binding->error = error;
    if (error == ESP_OK) binding->stage = NULL;
    (void)wifi_radio_wps_snapshot_locked(token, status);
done:
    if (worker) {
        /* No dispatch before ownership publication; these are local-only frees. */
        (void)esp32_mquickjs_wifi_wps_worker_close(worker);
        (void)esp32_mquickjs_wifi_wps_worker_release(&worker);
        if (binding) { esp32_mquickjs_wireless_secure_zero(binding, sizeof(*binding)); esp32_mquickjs_memory_payload_free(binding); }
    }
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_wps_snapshot_locked(token, status);
    wifi_radio_operation_unlock();
    return error;
}

static esp_err_t wifi_radio_wps_drain_locked(const esp32_mquickjs_wifi_radio_operation_t *token, bool close)
{
    s_wps_radio->stage = close ? "wps-native-close" : "wps-native-capture";
    esp_err_t error = close ? esp32_mquickjs_wifi_wps_worker_close(s_wps_radio->worker) :
        esp32_mquickjs_wifi_wps_worker_finish_capture(s_wps_radio->worker);
    if (error == ESP_OK) error = wifi_radio_wps_restore_locked();
    if (error == ESP_OK) {
        s_wps_radio->stage = "wps-event-fence";
        error = wifi_radio_wps_fence_locked(token);
    }
    if (error == ESP_OK) s_wps_radio->stage = NULL;
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_wps_exact_locked(token)) error = wifi_radio_wps_drain_locked(token, false);
    (void)wifi_radio_wps_snapshot_locked(token, status);
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_pin(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint8_t pin[8], bool commit)
{
    if ((!commit && !pin) || (commit && pin)) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_wps_exact_locked(token) ?
        esp32_mquickjs_wifi_wps_worker_pin(s_wps_radio->worker, pin, commit) : ESP_ERR_INVALID_STATE;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_prepare_close(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_wps_exact_locked(token)) error = wifi_radio_wps_drain_locked(token, true);
    (void)wifi_radio_wps_snapshot_locked(token, status);
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_credentials(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_credentials_t *credentials, bool commit)
{
    if ((!commit && !credentials) || (commit && credentials)) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_wps_exact_locked(token) && s_wps_radio->config_restored &&
        s_wps_radio->channel_restored && s_wps_radio->storage_restored && wifi_radio_wps_fenced_locked(token))
        error = esp32_mquickjs_wifi_wps_worker_credentials(s_wps_radio->worker, credentials, commit);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status)
{
    if (!token || !status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_wps_exact_locked(token)) {
        error = wifi_radio_wps_drain_locked(token, true);
        (void)wifi_radio_wps_snapshot_locked(token, status);
        if (error == ESP_OK) {
            error = esp32_mquickjs_wifi_wps_worker_release(&s_wps_radio->worker);
            if (error == ESP_OK) {
                esp32_mquickjs_wireless_secure_zero(s_wps_radio, sizeof(*s_wps_radio));
                esp32_mquickjs_memory_payload_free(s_wps_radio); s_wps_radio = NULL;
                taskENTER_CRITICAL(&s_radio.lock);
                memset(&s_radio.operation, 0, sizeof(s_radio.operation));
                memset(token, 0, sizeof(*token));
                taskEXIT_CRITICAL(&s_radio.lock);
            } else status->stage = "wps-worker-release";
        }
    } else (void)wifi_radio_wps_snapshot_locked(token, status);
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}
