/* Shares the Radio operation mutex and boot-scoped WPS fence with Station.
 * The two roles cannot coexist; their distinct bindings own distinct workers. */
static bool wifi_radio_wps_ap_exact_locked(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    return token && token->identity && s_wps_ap_radio && s_radio.driver_owned &&
        token->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_WPS &&
        token->identity == s_radio.operation.identity && token->generation == s_radio.generation &&
        token->generation == s_radio.operation.generation && token->lease_identity == s_radio.operation.lease_identity &&
        token->kind == s_radio.operation.kind;
}

static esp_err_t wifi_radio_wps_ap_snapshot_locked(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status)
{
    *status = (esp32_mquickjs_wifi_wps_ap_radio_status_t){.error = ESP_ERR_INVALID_STATE,
        .stage = "wps-ap-radio-identity"};
    if (!wifi_radio_wps_ap_exact_locked(token)) return ESP_ERR_INVALID_STATE;
    status->operation = *token;
    status->event_fenced = wifi_radio_wps_fenced_locked(token);
    int error = esp32_mquickjs_wifi_wps_ap_worker_status(s_wps_ap_radio->worker, &status->worker);
    status->reserved_bytes = sizeof(*s_wps_ap_radio) + status->worker.reserved_bytes;
    status->error = error != ESP_OK ? error : s_wps_ap_radio->error != ESP_OK ?
        s_wps_ap_radio->error : status->worker.error;
    status->stage = s_wps_ap_radio->stage ? s_wps_ap_radio->stage : status->worker.stage;
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_ap_begin(const esp32_mquickjs_wifi_radio_lease_t owners[3],
    const esp_wps_config_t *config, esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status)
{
    if (!owners || !config || !token || !status || token->identity || token->generation ||
        token->lease_identity || token->kind) return ESP_ERR_INVALID_ARG;
    *status = (esp32_mquickjs_wifi_wps_ap_radio_status_t){.stage = "wps-ap-radio-admission"};
    esp32_mquickjs_wifi_wps_ap_worker_t *worker = NULL;
    int error = esp32_mquickjs_wifi_wps_ap_worker_create(config, &worker);
    if (error != ESP_OK) { status->error = error; status->stage = "wps-ap-options"; return error; }
    wifi_radio_operation_lock();
    wifi_radio_wps_ap_t *binding = NULL;
    if (WIFI_RADIO_WPS_PENDING || WIFI_RADIO_SMARTCONFIG_PENDING || WIFI_RADIO_EAP_PENDING ||
        s_interval.owner.identity || s_interval.uncertain) { error = ESP_ERR_INVALID_STATE; goto done; }
    error = wifi_radio_connection_owner_locked(&owners[0], &owners[1], &owners[2], WIFI_IF_AP, false);
    if (error != ESP_OK) goto done;
    /* Any active Station also needs its exact helper pinned. A connected STA
     * remains connected; no enrollee/DHCP cleanup is borrowed for registrar. */
    if ((s_radio.effective_mode & WIFI_MODE_STA) && !owners[1].acquired) {
        error = ESP_ERR_INVALID_STATE; goto done;
    }
    for (size_t i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity && s_radio.leases[i].fixed_channel) {
            error = ESP_ERR_INVALID_STATE; goto done;
        }
    uint8_t primary;
    wifi_second_chan_t secondary;
    uint32_t generation;
    error = wifi_radio_get_channel_locked(&primary, &secondary, &generation);
    /* The build-local registrar reads its actual RF band at initialization
     * and for each M2/M2D. Country and target policy still gate this AP. */
    if (error == ESP_OK) error = wifi_radio_validate_regulatory_channel(primary);
    if (error != ESP_OK) goto done;
    if (!s_radio.next_operation_identity) { error = ESP_ERR_NO_MEM; goto done; }
    binding = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*binding), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!binding) { error = ESP_ERR_NO_MEM; goto done; }
    for (unsigned i = 0; i < 3; ++i) binding->owners[i] = owners[i].acquired ? owners[i].identity : 0;
    binding->worker = worker;
    s_wps_ap_radio = binding;
    worker = NULL;
    taskENTER_CRITICAL(&s_radio.lock);
    s_wps_fence_posted = s_wps_fence_seen = 0;
    s_radio.operation = (esp32_mquickjs_wifi_radio_operation_t){.generation = s_radio.generation,
        .identity = s_radio.next_operation_identity++, .lease_identity = owners[2].identity,
        .kind = ESP32_MQUICKJS_WIFI_RADIO_OPERATION_WPS};
    *token = s_radio.operation;
    taskEXIT_CRITICAL(&s_radio.lock);
    binding->stage = "wps-ap-native-enable";
    error = esp32_mquickjs_wifi_wps_ap_worker_prepare(binding->worker);
    if (error == ESP_OK) {
        binding->stage = "wps-ap-native-start";
        error = esp32_mquickjs_wifi_wps_ap_worker_start(binding->worker);
    }
    binding->error = error;
    if (error == ESP_OK) binding->stage = NULL;
    (void)wifi_radio_wps_ap_snapshot_locked(token, status);
done:
    if (worker) {
        (void)esp32_mquickjs_wifi_wps_ap_worker_close(worker);
        (void)esp32_mquickjs_wifi_wps_ap_worker_release(&worker);
        esp32_mquickjs_memory_payload_free(binding);
    }
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_ap_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    int error = wifi_radio_wps_ap_snapshot_locked(token, status);
    wifi_radio_operation_unlock();
    return error;
}

static esp_err_t wifi_radio_wps_ap_drain_locked(const esp32_mquickjs_wifi_radio_operation_t *token, bool close)
{
    s_wps_ap_radio->stage = close ? "wps-ap-native-close" : "wps-ap-native-capture";
    int error = close ? esp32_mquickjs_wifi_wps_ap_worker_close(s_wps_ap_radio->worker) :
        esp32_mquickjs_wifi_wps_ap_worker_finish_capture(s_wps_ap_radio->worker);
    if (error == ESP_OK) {
        s_wps_ap_radio->stage = "wps-ap-event-fence";
        error = wifi_radio_wps_fence_locked(token);
    }
    if (error == ESP_OK) s_wps_ap_radio->stage = NULL;
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_ap_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    int error = wifi_radio_wps_ap_exact_locked(token) ? wifi_radio_wps_ap_drain_locked(token, false) : ESP_ERR_INVALID_STATE;
    (void)wifi_radio_wps_ap_snapshot_locked(token, status);
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_ap_pin(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint8_t pin[8], bool commit)
{
    if ((!commit && !pin) || (commit && pin)) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    int error = wifi_radio_wps_ap_exact_locked(token) ?
        esp32_mquickjs_wifi_wps_ap_worker_pin(s_wps_ap_radio->worker, pin, commit) : ESP_ERR_INVALID_STATE;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_wps_ap_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status)
{
    if (!token || !status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    int error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_wps_ap_exact_locked(token)) {
        error = wifi_radio_wps_ap_drain_locked(token, true);
        (void)wifi_radio_wps_ap_snapshot_locked(token, status);
        if (error == ESP_OK) {
            error = esp32_mquickjs_wifi_wps_ap_worker_release(&s_wps_ap_radio->worker);
            if (error == ESP_OK) {
                esp32_mquickjs_memory_payload_free(s_wps_ap_radio); s_wps_ap_radio = NULL;
                taskENTER_CRITICAL(&s_radio.lock);
                memset(&s_radio.operation, 0, sizeof(s_radio.operation));
                memset(token, 0, sizeof(*token));
                taskEXIT_CRITICAL(&s_radio.lock);
            } else status->stage = "wps-ap-worker-release";
        }
    } else (void)wifi_radio_wps_ap_snapshot_locked(token, status);
    status->error = error;
    wifi_radio_operation_unlock();
    return error;
}
