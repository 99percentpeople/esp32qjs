/* Runtime task only. This is a phase of the same Session, not a second owner
 * registry. No helper callback carries a JS pointer. worker_busy serializes
 * native worker storage with Station calls and close/GC intent. */
static bool dpp_session_progress_connection(int64_t now)
{
    portENTER_CRITICAL(&s_dpp_session_lock);
    esp32_mquickjs_wifi_dpp_session_t *s = s_dpp_active;
    bool run = s && s->status.connection_requested && !s->status.worker_busy &&
        s->references != UINT32_MAX && now >= s->next_retry_us &&
        (s->status.closing ? s->connection_generation != 0 : s->status.connection_prepared);
    uint32_t generation = run ? s->connection_generation : 0;
    bool closing = run && s->status.closing;
    int64_t remaining = run && s->deadline_us ? s->deadline_us - now : 0;
    if (run) { ++s->references; s->status.worker_busy = true; }
    portEXIT_CRITICAL(&s_dpp_session_lock);
    if (!run) return false;
    esp_err_t error;
    const char *stage;
    esp32_mquickjs_wifi_dpp_connection_status_t connection = {0};
    if (closing) {
        stage = "dpp-station-disconnect";
        error = esp32_mquickjs_wifi_dpp_connect_end(&s->operation, generation);
        if (error == ESP_OK) generation = 0;
    } else if (!generation) {
        stage = "dpp-station-connect";
        if (remaining <= 0) error = ESP_ERR_TIMEOUT;
        else error = esp32_mquickjs_wifi_dpp_connect_begin(s->station_identity, &s->operation,
            &s->connection_config, (uint32_t)((remaining + 999) / 1000), &generation);
        if (generation) esp32_mquickjs_wireless_secure_zero(&s->connection_config, sizeof(s->connection_config));
    } else {
        stage = "dpp-station-result";
        error = esp32_mquickjs_wifi_dpp_connect_status(&s->operation, generation, &connection);
        if (error == ESP_OK && connection.terminal &&
            (connection.terminal != ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS || !connection.connected))
            error = connection.terminal == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
        if (error == ESP_OK && connection.terminal == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS &&
            connection.connected && !connection.draining) {
            if (!connection.link.valid) error = ESP_ERR_INVALID_RESPONSE;
            else if (s->status.connection_verified &&
                (memcmp(s->connection_link.bssid, connection.link.bssid, sizeof(connection.link.bssid)) ||
                 s->connection_link.channel != connection.link.channel)) error = ESP_ERR_INVALID_RESPONSE;
        }
    }
    portENTER_CRITICAL(&s_dpp_session_lock);
    s->connection_generation = generation;
    if (generation) s->status.connection_generation = generation;
    if (connection.terminal) s->status.connection_reason = connection.reason;
    if (!closing && error == ESP_OK && connection.terminal == ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS &&
        connection.connected && !connection.draining) {
        s->connection_link = connection.link;
        s->status.connection_established = true;
        if (s->status.connection_verified && !s->status.connected) {
            s->status.connected = true; s->connection_completed_us = now; s->deadline_us = 0;
        }
    }
    if (!closing && error != ESP_OK && error != ESP_ERR_NOT_FINISHED) {
        if (!s->status.error) { s->status.error = error; s->status.stage = stage; }
        dpp_session_close_locked(s, error == ESP_ERR_TIMEOUT);
    }
    s->status.cleanup_error = error;
    s->status.cleanup_stage = error == ESP_OK ? NULL : stage;
    s->status.worker_busy = false;
    if (s->status.closing) dpp_session_scrub(s);
    /* Let the worker perform selection/authentication or the native close
     * suffix immediately. Ordinary background polling remains bounded there. */
    s->next_retry_us = error == ESP_OK ? 0 : now <= INT64_MAX - 100000 ? now + 100000 : INT64_MAX;
    portEXIT_CRITICAL(&s_dpp_session_lock);
    esp32_mquickjs_wifi_dpp_session_release(s);
    return true;
}
