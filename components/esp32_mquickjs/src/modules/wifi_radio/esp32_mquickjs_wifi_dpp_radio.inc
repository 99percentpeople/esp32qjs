/* Shares Radio's operation mutex with every driver mutation and lease release.
 * No JS value, callback-owned pointer, or credential enters this binding. */
static bool wifi_radio_dpp_exact_locked(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    return token && token->identity && s_dpp_radio && s_radio.driver_owned &&
        token->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP && token->kind == s_radio.operation.kind &&
        token->identity == s_radio.operation.identity && token->generation == s_radio.generation &&
        token->generation == s_radio.operation.generation && token->lease_identity == s_radio.operation.lease_identity;
}

static esp_err_t wifi_radio_dpp_snapshot_locked(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status)
{
    *status = (esp32_mquickjs_wifi_dpp_radio_status_t){.error = ESP_ERR_INVALID_STATE, .stage = "dpp-radio-identity"};
    if (!wifi_radio_dpp_exact_locked(token)) return ESP_ERR_INVALID_STATE;
    status->operation = *token;
    status->channel_restored = s_dpp_radio->channel_restored;
    status->config_restored = s_dpp_radio->config_restored;
    status->storage_restored = s_dpp_radio->storage_restored;
    status->connection_borrowed = s_dpp_radio->connection_borrowed;
    status->restore_requires_restart = s_dpp_radio->restore_requires_restart;
    status->restore_stopped = s_dpp_radio->restore_stopped;
    status->restore_started = s_dpp_radio->restore_started;
    status->restore_start_failed = s_dpp_radio->restore_start_attempted && !s_dpp_radio->restore_start_accepted;
    status->restore_start_error = s_dpp_radio->restore_start_error;
    status->restore_recovery_pending = s_dpp_radio->restore_recovery_pending;
    status->restore_recovery_attempts = s_dpp_radio->restore_recovery_attempts;
    esp_err_t error = esp32_mquickjs_wifi_dpp_worker_status(s_dpp_radio->worker, &status->worker);
    status->reserved_bytes = sizeof(*s_dpp_radio) + status->worker.reserved_bytes;
    status->error = error != ESP_OK ? error : s_dpp_radio->error != ESP_OK ? s_dpp_radio->error : status->worker.error;
    if (status->error || status->worker.handoff_unknown || status->worker.closing)
        s_dpp_radio->connection_ready = false;
    status->stage = s_dpp_radio->stage ? s_dpp_radio->stage : status->worker.stage;
    return error;
}

/* The shared STOP executor admits this exact, already retired protocol owner.
 * All helper leases stay registered across STOP/START; no zero-owner gap. */
static bool wifi_radio_dpp_stop_owner_locked(uint32_t identity)
{
    wifi_radio_dpp_t *binding = s_dpp_radio;
    if (!binding || identity != s_radio.operation.identity ||
        !wifi_radio_dpp_exact_locked(&s_radio.operation) || binding->connection_borrowed ||
        !binding->restore_requires_restart || !binding->restore_native_retired ||
        s_radio.lifecycle.identity || s_radio.effective_mode != binding->restore_mode ||
        ((binding->restore_mode & WIFI_MODE_AP) && !binding->allow_ap_restart)) return false;
    const esp32_mquickjs_wifi_radio_client_t roles[3] = {ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,
        ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA, ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP};
    unsigned found = 0, expected = 0;
    for (unsigned i = 0; i < 3; ++i) if (binding->owners[i]) expected |= 1U << i;
    for (unsigned i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) {
        const wifi_radio_live_lease_t *lease = &s_radio.leases[i];
        if (!lease->identity) continue;
        unsigned owner;
        for (owner = 0; owner < 3; ++owner) if (binding->owners[owner] == lease->identity) break;
        if (owner == 3 || lease->client != roles[owner] || lease->fixed_channel) return false;
        found |= 1U << owner;
    }
    return found == expected && (found & 2U) &&
        s_radio.operation.lease_identity == binding->owners[1];
}

static esp_err_t wifi_radio_dpp_restore_start_locked(wifi_radio_dpp_t *binding)
{
    if (binding->restore_started) return ESP_OK;
    if (!binding->restore_stopped || !binding->config_restored) return ESP_ERR_INVALID_STATE;
    esp_err_t error;
    if (!binding->restore_start_attempted) {
        binding->stage = "dpp-restore-start-events";
        error = wifi_radio_begin_events(RADIO_EVENTS_START, binding->restore_mode);
        if (error != ESP_OK) return error;
        binding->restore_start_attempted = true;
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.stop_required = true;
        s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_STARTING;
        taskEXIT_CRITICAL(&s_radio.lock);
        binding->stage = "dpp-restore-start";
        error = esp_wifi_start();
        binding->restore_start_error = error;
        if (error != ESP_OK) {
            binding->restore_recovery_pending = false;
            return wifi_radio_record_fault(binding->stage, error);
        }
        binding->restore_start_accepted = true;
        taskENTER_CRITICAL(&s_radio.lock); s_radio.started = true; taskEXIT_CRITICAL(&s_radio.lock);
    }
    if (!binding->restore_start_accepted) {
        binding->stage = "dpp-restore-start";
        return binding->restore_start_error;
    }
    /* An accepted START is never resubmitted because a callback or marker is
     * late. The existing event identity/revision owns the remaining fence. */
    binding->stage = "dpp-restore-start-events";
    error = wifi_radio_wait_events_inner(NULL);
    if (error != ESP_OK) return error;
    binding->restore_started = true;
    wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STARTED);
    return ESP_OK;
}

static esp_err_t wifi_radio_dpp_restore_policy_locked(wifi_radio_dpp_t *binding)
{
    /* RAM remains selected until every readback succeeds. Each accepted
     * policy write is skipped on retry if its actual value already matches. */
    int8_t power;
    wifi_ps_type_t power_save;
    binding->stage = "dpp-restore-power-save";
    esp_err_t error = esp_wifi_get_ps(&power_save);
    if (error == ESP_OK && power_save != binding->restore_power_save) {
        error = esp_wifi_set_ps(binding->restore_power_save);
        if (error == ESP_OK) error = esp_wifi_get_ps(&power_save);
    }
    if (error != ESP_OK) return error;
    if (power_save != binding->restore_power_save) return ESP_ERR_INVALID_RESPONSE;
    binding->stage = "dpp-restore-tx-power";
    error = esp_wifi_get_max_tx_power(&power);
    if (error == ESP_OK && power != binding->restore_tx_power) {
        error = esp_wifi_set_max_tx_power(binding->restore_tx_power);
        if (error == ESP_OK) error = esp_wifi_get_max_tx_power(&power);
    }
    if (error != ESP_OK) return error;
    if (power != binding->restore_tx_power) return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i = 0; i < 2; ++i) {
        if (!(binding->restore_mode & (1U << i))) continue;
        wifi_interface_t interface = i == 0 ? WIFI_IF_STA : WIFI_IF_AP;
        binding->stage = i == 0 ? "dpp-restore-station-inactive" : "dpp-restore-ap-inactive";
        uint16_t inactive = 0;
        error = esp_wifi_get_inactive_time(interface, &inactive);
        if (error == ESP_OK && inactive != binding->restore_inactive[i]) {
            error = esp_wifi_set_inactive_time(interface, binding->restore_inactive[i]);
            if (error == ESP_OK) error = esp_wifi_get_inactive_time(interface, &inactive);
        }
        wifi_radio_inactive_history_record(i, inactive, error);
        if (error != ESP_OK) return error;
        if (inactive != binding->restore_inactive[i]) return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t wifi_radio_dpp_restore_locked(void)
{
    wifi_radio_dpp_t *binding = s_dpp_radio;
    esp32_mquickjs_wifi_dpp_worker_status_t worker;
    esp_err_t error = esp32_mquickjs_wifi_dpp_worker_status(binding->worker, &worker);
    if (error != ESP_OK) return error;
    if (worker.handoff_unknown || (!worker.capture_retired && !worker.retired)) return ESP_ERR_INVALID_STATE;
    /* Bootstrap generation alone does not tune the driver. Failed foreign
     * admission must never restore over another native owner's channel. */
    if (!binding->listen_attempted) binding->channel_restored = true;
    if (!binding->station_mutated) binding->config_restored = true;
    if (!binding->storage_attempted) binding->storage_restored = true;
    bool restart = binding->station_mutated && binding->restore_requires_restart;
    if (binding->channel_restored && binding->config_restored && binding->storage_restored &&
        (!restart || binding->restore_started)) return ESP_OK;
    if (!restart && (!s_radio.started || s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED))
        return ESP_ERR_INVALID_STATE;
    /* An explicitly admitted failed-START recovery must retire the partial
     * driver through STOP/fence. Its Station interface may not exist yet, so
     * association readback cannot gate that cleanup. Ordinary close still
     * requires disconnection before restoring the caller's configuration. */
    if (!binding->restore_stopped && !(restart &&
        (binding->restore_recovery_pending || s_radio.event_phase == RADIO_EVENTS_STOP))) {
        wifi_ap_record_t ap = {0};
        binding->stage = "dpp-disconnect-readback";
        error = esp_wifi_sta_get_ap_info(&ap);
        esp32_mquickjs_wireless_secure_zero(&ap, sizeof(ap));
        if (error != ESP_ERR_WIFI_NOT_CONNECT) return error == ESP_OK ? ESP_ERR_NOT_FINISHED : error;
    }
    if (restart && !binding->restore_stopped) {
        binding->restore_native_retired = worker.retired;
        binding->stage = "dpp-restore-stop";
        error = wifi_radio_stop_owners_locked(NULL, false, s_radio.operation.identity);
        if (error != ESP_OK) return error;
        binding->restore_stopped = true;
    }
    if (binding->restore_recovery_reset) {
        /* Only a completed STOP/fence retires the failed START. Keep the
         * original fault until this point and never clear another source. */
        if (!binding->restore_recovery_pending || !binding->restore_stopped ||
            s_radio.cleanup_stage || !s_radio.fault_stage ||
            strcmp(s_radio.fault_stage, "dpp-restore-start") ||
            s_radio.fault_error != binding->restore_start_error) return ESP_ERR_INVALID_STATE;
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.fault_stage = NULL;
        s_radio.fault_error = ESP_OK;
        s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        taskEXIT_CRITICAL(&s_radio.lock);
        binding->restore_start_attempted = binding->restore_start_accepted = false;
        binding->config_restored = binding->config_restore_written = false;
        binding->restore_policies_done = binding->channel_restored = false;
        binding->restore_recovery_reset = false;
    }
    if (!binding->config_restored) {
        if (!s_radio.storage_configured || s_radio.storage != WIFI_STORAGE_RAM) return ESP_ERR_INVALID_STATE;
        binding->stage = "dpp-config-readback";
        error = esp_wifi_get_config(WIFI_IF_STA, &binding->scratch);
        bool matches = error == ESP_OK && wifi_radio_config_equal(WIFI_IF_STA, &binding->saved, &binding->scratch);
        esp32_mquickjs_wireless_secure_zero(&binding->scratch, sizeof(binding->scratch));
        if (error != ESP_OK) return error;
        if (!matches) {
            binding->stage = "dpp-config-restore";
            if (!binding->config_restore_written) {
                error = esp_wifi_set_config(WIFI_IF_STA, &binding->saved);
                if (error != ESP_OK) return error;
                binding->config_restore_written = true;
            }
            binding->stage = "dpp-config-pmf-restore";
            error = wifi_radio_restore_disabled_pmf(WIFI_IF_STA, &binding->saved);
            if (error != ESP_OK) return error;
            error = esp_wifi_get_config(WIFI_IF_STA, &binding->scratch);
            matches = error == ESP_OK && wifi_radio_config_equal(WIFI_IF_STA, &binding->saved, &binding->scratch);
            esp32_mquickjs_wireless_secure_zero(&binding->scratch, sizeof(binding->scratch));
            if (error != ESP_OK) return error;
            if (!matches) return ESP_ERR_INVALID_RESPONSE;
        }
        binding->config_restored = true;
    }
    if (restart) {
        error = wifi_radio_dpp_restore_start_locked(binding);
        if (error != ESP_OK) return error;
        if (!binding->restore_policies_done) {
            if (!s_radio.storage_configured || s_radio.storage != WIFI_STORAGE_RAM) return ESP_ERR_INVALID_STATE;
            error = wifi_radio_dpp_restore_policy_locked(binding);
            if (error != ESP_OK) return error;
            binding->stage = "dpp-config-post-start-readback";
            error = esp_wifi_get_config(WIFI_IF_STA, &binding->scratch);
            bool matches = error == ESP_OK && wifi_radio_config_equal(WIFI_IF_STA, &binding->saved, &binding->scratch);
            esp32_mquickjs_wireless_secure_zero(&binding->scratch, sizeof(binding->scratch));
            if (error != ESP_OK) return error;
            if (!matches) return ESP_ERR_INVALID_RESPONSE;
            binding->restore_policies_done = true;
        }
    }
    uint8_t primary;
    wifi_second_chan_t secondary;
    uint32_t generation;
    binding->stage = "dpp-channel-readback";
    error = wifi_radio_get_channel_locked(&primary, &secondary, &generation);
    if (error != ESP_OK) return error;
    if (primary != binding->home_primary || secondary != binding->home_secondary) {
        binding->stage = "dpp-channel-restore";
        error = wifi_radio_validate_regulatory_channel(binding->home_primary);
        if (error == ESP_OK) error = esp_wifi_set_channel(binding->home_primary, binding->home_secondary);
        if (error == ESP_OK) error = wifi_radio_get_channel_locked(&primary, &secondary, &generation);
        if (error != ESP_OK) return error;
        if (primary != binding->home_primary || secondary != binding->home_secondary) return ESP_ERR_INVALID_RESPONSE;
    }
    binding->channel_restored = true;
    if (!binding->storage_restored) {
        binding->stage = "dpp-storage-restore";
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
    binding->restore_recovery_pending = false;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_begin(
    const esp32_mquickjs_wifi_radio_lease_t owners[3], bool allow_ap_channel_change,
    const esp32_mquickjs_wifi_dpp_worker_options_t *options,
    esp32_mquickjs_wifi_radio_operation_t *token, esp32_mquickjs_wifi_dpp_radio_status_t *status)
{
    if (!owners || !options || !token || !status || token->identity || token->generation || token->lease_identity || token->kind)
        return ESP_ERR_INVALID_ARG;
    *status = (esp32_mquickjs_wifi_dpp_radio_status_t){.stage = "dpp-options"};
    esp32_mquickjs_wifi_dpp_worker_t *worker = NULL;
    esp_err_t error = esp32_mquickjs_wifi_dpp_worker_create(options, &worker);
    if (error != ESP_OK) { status->error = error; return error; }
    wifi_radio_operation_lock();
    wifi_radio_dpp_t *binding = NULL;
    status->stage = "dpp-radio-admission";
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
    /* Syntax was validated by create. Validate every advertised listen channel
     * against current Radio country/target policy before publishing ownership. */
    for (const char *p = options->channels; *p;) {
        unsigned channel = 0;
        while (*p >= '0' && *p <= '9') channel = channel * 10U + (unsigned)(*p++ - '0');
        error = wifi_radio_validate_regulatory_channel((uint8_t)channel);
        if (error != ESP_OK) goto done;
        if (*p == ',') ++p;
    }
    if (!s_radio.next_operation_identity) { error = ESP_ERR_NO_MEM; goto done; }
    binding = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*binding), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!binding) { error = ESP_ERR_NO_MEM; goto done; }
    uint32_t generation;
    error = wifi_radio_get_channel_locked(&binding->home_primary, &binding->home_secondary, &generation);
    if (error == ESP_OK) error = wifi_radio_validate_regulatory_channel(binding->home_primary);
    if (error != ESP_OK) goto done;
    for (unsigned i = 0; i < 3; ++i) binding->owners[i] = owners[i].acquired ? owners[i].identity : 0;
    binding->worker = worker;
    s_dpp_radio = binding;
    worker = NULL;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.operation = (esp32_mquickjs_wifi_radio_operation_t){.generation = s_radio.generation,
        .identity = s_radio.next_operation_identity++, .lease_identity = owners[1].identity,
        .kind = ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP};
    *token = s_radio.operation;
    taskEXIT_CRITICAL(&s_radio.lock);
    binding->stage = "dpp-native-bootstrap";
    error = esp32_mquickjs_wifi_dpp_worker_prepare(binding->worker);
    binding->error = error;
    if (error == ESP_OK) binding->stage = NULL;
    (void)wifi_radio_dpp_snapshot_locked(token, status);
done:
    if (worker) {
        (void)esp32_mquickjs_wifi_dpp_worker_close(worker);
        (void)esp32_mquickjs_wifi_dpp_worker_release(&worker);
        if (binding) { esp32_mquickjs_wireless_secure_zero(binding, sizeof(*binding)); esp32_mquickjs_memory_payload_free(binding); }
    }
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_dpp_snapshot_locked(token, status);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_listen(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_dpp_exact_locked(token) && !s_dpp_radio->channel_restored) {
        s_dpp_radio->listen_attempted = true;
        error = esp32_mquickjs_wifi_dpp_worker_listen(s_dpp_radio->worker);
    }
    (void)wifi_radio_dpp_snapshot_locked(token, status);
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

static esp_err_t wifi_radio_dpp_drain_locked(const esp32_mquickjs_wifi_radio_operation_t *token,
    bool close, esp32_mquickjs_wifi_dpp_radio_status_t *status)
{
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_dpp_exact_locked(token) && !s_dpp_radio->connection_borrowed) {
        s_dpp_radio->stage = close ? "dpp-native-close" : "dpp-native-capture";
        error = close ? esp32_mquickjs_wifi_dpp_worker_close(s_dpp_radio->worker) :
            esp32_mquickjs_wifi_dpp_worker_finish_capture(s_dpp_radio->worker);
        if (error == ESP_OK) error = wifi_radio_dpp_restore_locked();
        if (error == ESP_OK) s_dpp_radio->stage = NULL;
    }
    (void)wifi_radio_dpp_snapshot_locked(token, status);
    status->error = error;
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_dpp_drain_locked(token, false, status);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_prepare_close(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_dpp_drain_locked(token, true, status);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_recover(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_dpp_exact_locked(token)) goto done;
    wifi_radio_dpp_t *binding = s_dpp_radio;
    if (binding->restore_recovery_pending) { error = ESP_OK; goto done; }
    if (!binding->restore_start_attempted || binding->restore_start_accepted ||
        !binding->restore_stopped || binding->restore_started || !binding->config_restored ||
        binding->restore_start_error == ESP_OK || !s_radio.fault_stage ||
        strcmp(s_radio.fault_stage, "dpp-restore-start") ||
        s_radio.fault_error != binding->restore_start_error || s_radio.cleanup_stage ||
        s_radio.restart_required || s_radio.started || !s_radio.stop_required ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_FAULTED ||
        !s_radio.storage_configured || s_radio.storage != WIFI_STORAGE_RAM ||
        !wifi_radio_dpp_stop_owner_locked(token->identity)) goto done;
    esp32_mquickjs_wifi_dpp_worker_status_t worker;
    error = esp32_mquickjs_wifi_dpp_worker_status(binding->worker, &worker);
    if (error != ESP_OK) goto done;
    if (!worker.retired || worker.handoff_unknown) { error = ESP_ERR_INVALID_STATE; goto done; }
    if (binding->restore_recovery_attempts == UINT32_MAX) { error = ESP_ERR_NO_MEM; goto done; }
    ++binding->restore_recovery_attempts;
    binding->restore_recovery_pending = binding->restore_recovery_reset = true;
    binding->restore_stopped = false;
    /* No SDK mutation at admission. The same close executor first performs
     * STOP/fence, then revalidates configuration before one authorized START. */
    error = ESP_OK;
done:
    (void)wifi_radio_dpp_snapshot_locked(token, status);
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_radio_status_t *status)
{
    if (!token || !status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_dpp_drain_locked(token, true, status);
    if (error == ESP_OK) {
        error = esp32_mquickjs_wifi_dpp_worker_release(&s_dpp_radio->worker);
        if (error == ESP_OK) {
            esp32_mquickjs_wireless_secure_zero(s_dpp_radio, sizeof(*s_dpp_radio));
            esp32_mquickjs_memory_payload_free(s_dpp_radio); s_dpp_radio = NULL;
            taskENTER_CRITICAL(&s_radio.lock);
            memset(&s_radio.operation, 0, sizeof(s_radio.operation));
            memset(token, 0, sizeof(*token));
            taskEXIT_CRITICAL(&s_radio.lock);
        } else status->stage = "dpp-worker-release";
    }
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_uri(const esp32_mquickjs_wifi_radio_operation_t *token,
    char *uri, size_t capacity, bool commit)
{
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_dpp_exact_locked(token) ?
        esp32_mquickjs_wifi_dpp_worker_uri(s_dpp_radio->worker, uri, capacity, commit) : ESP_ERR_INVALID_STATE;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_config(const esp32_mquickjs_wifi_radio_operation_t *token,
    unsigned index, esp_dpp_config_data_t *config)
{
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_dpp_exact_locked(token) && s_dpp_radio->channel_restored ?
        esp32_mquickjs_wifi_dpp_worker_config(s_dpp_radio->worker, index, config) : ESP_ERR_INVALID_STATE;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_configs_commit(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_dpp_exact_locked(token) && s_dpp_radio->channel_restored ?
        esp32_mquickjs_wifi_dpp_worker_configs_commit(s_dpp_radio->worker) : ESP_ERR_INVALID_STATE;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_select(const esp32_mquickjs_wifi_radio_operation_t *token,
    const esp_dpp_config_data_t *row, esp32_mquickjs_wifi_dpp_auth_t requested, bool allow_ap_restart,
    esp32_mquickjs_wifi_dpp_auth_t *selected, wifi_config_t *station,
    esp32_mquickjs_wifi_dpp_radio_status_t *status)
{
    if (!selected || !station || !status) return ESP_ERR_INVALID_ARG;
    esp_err_t error = esp32_mquickjs_wifi_dpp_connection_prepare(row, requested, selected, station);
    esp32_mquickjs_wireless_secure_zero(station, sizeof(*station));
    *selected = ESP32_MQUICKJS_DPP_AUTH_DEFAULT;
    if (error != ESP_OK) return error;
    wifi_radio_operation_lock();
    if (!wifi_radio_dpp_exact_locked(token) || !s_dpp_radio->channel_restored ||
        s_dpp_radio->selection_attempted || !s_radio.storage_configured || !s_radio.started || s_radio.fault_stage) {
        error = ESP_ERR_INVALID_STATE; goto done;
    }
    wifi_radio_dpp_t *binding = s_dpp_radio;
    binding->stage = "dpp-config-snapshot";
    error = esp_wifi_get_config(WIFI_IF_STA, &binding->saved);
    if (error != ESP_OK) goto done;
    binding->restore_requires_restart = !binding->saved.sta.pmf_cfg.capable;
    if (binding->restore_requires_restart) {
        binding->stage = "dpp-config-pmf-restoration";
        if (binding->saved.sta.pmf_cfg.required ||
            !esp32_mquickjs_wifi_radio_pmf_disable_allowed(WIFI_IF_STA, &binding->saved)) {
            error = ESP_ERR_NOT_SUPPORTED; goto done;
        }
        binding->stage = "dpp-ap-restart-consent";
        if ((s_radio.effective_mode & WIFI_MODE_AP) && !allow_ap_restart) { error = ESP_ERR_INVALID_STATE; goto done; }
        binding->restore_mode = s_radio.effective_mode;
        binding->allow_ap_restart = allow_ap_restart;
        binding->stage = "dpp-restore-policy-snapshot";
        error = esp_wifi_get_ps(&binding->restore_power_save);
        if (error == ESP_OK) error = esp_wifi_get_max_tx_power(&binding->restore_tx_power);
        if (error != ESP_OK) goto done;
        if (binding->restore_tx_power < 8 || binding->restore_tx_power > 80 ||
            s_radio.wake_locks || (s_inactive_history.pending & (uint8_t)binding->restore_mode)) {
            error = ESP_ERR_INVALID_STATE; goto done;
        }
        for (unsigned i = 0; i < 2; ++i) {
            if (!(binding->restore_mode & (1U << i))) continue;
            error = esp_wifi_get_inactive_time(i == 0 ? WIFI_IF_STA : WIFI_IF_AP, &binding->restore_inactive[i]);
            if (error != ESP_OK) goto done;
            if (binding->restore_inactive[i] < (i == 0 ? 3 : 10)) { error = ESP_ERR_INVALID_RESPONSE; goto done; }
        }
    }
    binding->storage = s_radio.storage;
    binding->selection_attempted = binding->storage_attempted = true;
    binding->storage_restored = false;
    binding->stage = "dpp-storage-ram";
    if (s_radio.storage != WIFI_STORAGE_RAM) {
        error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.storage_configured = error == ESP_OK;
        if (error == ESP_OK) s_radio.storage = WIFI_STORAGE_RAM;
        taskEXIT_CRITICAL(&s_radio.lock);
        if (error != ESP_OK) goto done;
    }
    binding->stage = "dpp-selection";
    error = esp32_mquickjs_wifi_dpp_worker_select(binding->worker, row, requested, selected, station);
    if (error == ESP_OK) { binding->stage = NULL; binding->connection_ready = true; }
done:
    (void)wifi_radio_dpp_snapshot_locked(token, status);
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_connection_begin(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_dpp_exact_locked(token) && s_dpp_radio->connection_ready &&
        !s_dpp_radio->connection_used && s_radio.started && !s_radio.fault_stage &&
        s_radio.storage_configured && s_radio.storage == WIFI_STORAGE_RAM) {
        /* Background selection/status publishes readiness under this mutex.
         * Runtime admission must not perform a synchronous native IPC. */
        {
            wifi_ap_record_t ap = {0};
            error = esp_wifi_sta_get_ap_info(&ap);
            esp32_mquickjs_wireless_secure_zero(&ap, sizeof(ap));
            error = error == ESP_ERR_WIFI_NOT_CONNECT ? ESP_OK : error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
        }
        if (error == ESP_OK) {
            s_dpp_radio->connection_borrowed = s_dpp_radio->connection_used = true;
            s_dpp_radio->station_mutated = true;
            s_dpp_radio->config_restored = s_dpp_radio->channel_restored = false;
        }
    }
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_connection_end(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_dpp_exact_locked(token) && s_dpp_radio->connection_borrowed) {
        s_dpp_radio->connection_borrowed = false;
        error = ESP_OK;
    }
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_dpp_check_connection(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_dpp_auth_t selected, const uint8_t bssid[6])
{
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_dpp_exact_locked(token) && s_dpp_radio->connection_borrowed ?
        esp32_mquickjs_wifi_dpp_worker_check_connection(s_dpp_radio->worker, selected, bssid) : ESP_ERR_INVALID_STATE;
    wifi_radio_operation_unlock();
    return error;
}
