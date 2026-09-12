/* Read requests coalesce on their retained Service. A revision only counts
 * completed reads; a Future cancellation never cancels another reader. */
esp_err_t esp32_mquickjs_wifi_nan_discovery_credentials_request(esp32_mquickjs_wifi_nan_discovery_t *d,
    uint32_t *revision)
{
    if (!d || !revision) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_nan_session_lock);
    esp_err_t error = !d->parent || !d->status.ready || d->status.closing || !d->status.pairing_enabled ||
        d->parent->status.closing ? ESP_ERR_INVALID_STATE : d->credentials_revision == UINT32_MAX ? ESP_ERR_NO_MEM : ESP_OK;
    if (!error) {
        *revision = d->credentials_revision;
        d->credentials_requested = true; d->parent->next_retry_us = 0;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_nan_discovery_credentials_read(esp32_mquickjs_wifi_nan_discovery_t *d,
    uint32_t revision, bool *ready, esp32_mquickjs_wifi_nan_credentials_t *out)
{
    if (!d || !ready || !out) return ESP_ERR_INVALID_ARG;
    *ready = false; *out = (esp32_mquickjs_wifi_nan_credentials_t){0};
    portENTER_CRITICAL(&s_nan_session_lock);
    esp_err_t error = !d->parent || !d->status.ready || d->status.closing || d->parent->status.closing ?
        ESP_ERR_INVALID_STATE : ESP_OK;
    if (!error && d->credentials_revision != revision) {
        *ready = true; *out = d->credentials; error = d->credentials_error;
    }
    portEXIT_CRITICAL(&s_nan_session_lock);
    return error;
}
