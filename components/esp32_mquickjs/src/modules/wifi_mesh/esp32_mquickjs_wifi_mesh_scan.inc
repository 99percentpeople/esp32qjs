/* SDK manual scan ownership. Blocking scan completion belongs to the worker,
 * not a JS waiter or a lossy observation. Unknown completion holds the lane
 * and captured scan arguments through physical STOP and native owner release. */
static esp_err_t mesh_scan_admit(wifi_mesh_owner_t *owner, esp32_mquickjs_wifi_mesh_control_kind_t kind)
{
    portENTER_CRITICAL(&s_mesh_lock);
    bool held = owner->status.scan.identity != 0;
    portEXIT_CRITICAL(&s_mesh_lock);
    if (!held) return ESP_OK;
    switch (kind) {
    case ESP32_MQUICKJS_MESH_ROUTING_TABLE: case ESP32_MQUICKJS_MESH_GROUP_LIST:
    case ESP32_MQUICKJS_MESH_CONFIGURATION: case ESP32_MQUICKJS_MESH_SUBNET:
    case ESP32_MQUICKJS_MESH_HAS_GROUP: case ESP32_MQUICKJS_MESH_UPSTREAM_CAPACITY:
    case ESP32_MQUICKJS_MESH_POWER_STATUS: case ESP32_MQUICKJS_MESH_TSF_TIME:
    case ESP32_MQUICKJS_MESH_SCAN_NEXT: case ESP32_MQUICKJS_MESH_SCAN_FLUSH:
        return ESP_OK;
    default: return ESP_ERR_INVALID_STATE;
    }
}

static esp_err_t mesh_scan_validate(const esp32_mquickjs_wifi_mesh_scan_config_t *s)
{
    const wifi_scan_config_t *c = &s->config;
    if (c->channel > 14 || (c->scan_type != WIFI_SCAN_TYPE_ACTIVE && c->scan_type != WIFI_SCAN_TYPE_PASSIVE) ||
        (c->channel_bitmap.ghz_2_channels & 0x8001U) || c->channel_bitmap.ghz_5_channels != 1 ||
        (c->channel && c->channel_bitmap.ghz_2_channels) || c->home_chan_dwell_time < 30 || c->home_chan_dwell_time > 150 ||
        (s->has_ssid && (!s->ssid[0] || s->ssid[32])) ||
        (s->has_bssid && (!mesh_nonzero_address(s->bssid) || (s->bssid[0] & 1U)))) return ESP_ERR_INVALID_ARG;
    if (c->scan_type == WIFI_SCAN_TYPE_ACTIVE) {
        if (!c->scan_time.active.max || c->scan_time.active.max > 1500 ||
            c->scan_time.active.min > c->scan_time.active.max) return ESP_ERR_INVALID_ARG;
    } else if (!c->scan_time.passive || c->scan_time.passive > 1500) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static esp_err_t mesh_scan_start(wifi_mesh_owner_t *owner, esp32_mquickjs_wifi_mesh_control_data_t *d)
{
    d->stage = "mesh-scan-manual-required";
    if (esp_mesh_get_self_organized()) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_mesh_lock);
    esp_err_t error = owner->status.scan.identity ? ESP_ERR_INVALID_STATE : !owner->next_sequence ? ESP_ERR_NO_MEM : ESP_OK;
    if (!error) owner->status.scan = (esp32_mquickjs_wifi_mesh_scan_status_t){.identity = owner->next_sequence++, .running = true};
    portEXIT_CRITICAL(&s_mesh_lock);
    if (error) return error;
    owner->scan_config = d->data.scan;
    owner->scan_config.config.ssid = owner->scan_config.has_ssid ? owner->scan_config.ssid : NULL;
    owner->scan_config.config.bssid = owner->scan_config.has_bssid ? owner->scan_config.bssid : NULL;
    d->stage = "mesh-scan";
    error = esp_wifi_scan_start(&owner->scan_config.config, true);
    bool completed = !error;
    uint16_t count = 0;
    if (!error) {
        ++d->completed_steps; d->stage = "mesh-scan-count";
        error = esp_wifi_scan_get_ap_num(&count);
    }
    /* On an ambiguous return the driver may still refer to SSID/BSSID. */
    if (completed) esp32_mquickjs_wireless_secure_zero(&owner->scan_config, sizeof(owner->scan_config));
    esp32_mquickjs_wireless_secure_zero(&d->data, sizeof(d->data));
    portENTER_CRITICAL(&s_mesh_lock);
    owner->status.scan.running = false;
    owner->status.scan.completed = completed;
    owner->status.scan.uncertain = !completed;
    owner->status.scan.error = error;
    owner->status.scan.total = owner->status.scan.remaining = count;
    d->data.scan_result = owner->status.scan;
    portEXIT_CRITICAL(&s_mesh_lock);
    return error;
}

static esp_err_t mesh_scan_next(wifi_mesh_owner_t *owner, esp32_mquickjs_wifi_mesh_control_data_t *d)
{
    d->stage = "mesh-scan-read";
    if (!d->scan_record) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_mesh_lock);
    esp32_mquickjs_wifi_mesh_scan_status_t scan = owner->status.scan;
    bool valid = d->scan_identity && d->scan_identity == scan.identity && scan.completed && !scan.uncertain && !scan.error;
    if (valid && scan.retained) *d->scan_record = *owner->scan_record;
    portEXIT_CRITICAL(&s_mesh_lock);
    if (!valid) return ESP_ERR_INVALID_STATE;
    if (scan.retained || !scan.remaining) return ESP_OK;
    if (!owner->scan_record) {
        esp32_mquickjs_wifi_mesh_scan_record_t *record = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, sizeof(*record),
            ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
        if (!record) return ESP_ERR_NO_MEM; /* No destructive read yet. */
        portENTER_CRITICAL(&s_mesh_lock);
        owner->scan_record = record; owner->status.reserved_bytes += sizeof(*record);
        portEXIT_CRITICAL(&s_mesh_lock);
    }
    int length = 0;
    d->stage = "mesh-scan-ie-length";
    esp_err_t error = esp_mesh_scan_get_ap_ie_len(&length);
    if (error) return error;
    if (length < 0 || length > ESP32_MQUICKJS_MESH_SCAN_IE_BYTES) return ESP_ERR_INVALID_SIZE;
    portENTER_CRITICAL(&s_mesh_lock);
    uint32_t sequence = owner->next_sequence ? owner->next_sequence++ : 0;
    portEXIT_CRITICAL(&s_mesh_lock);
    if (!sequence) return ESP_ERR_NO_MEM;
    esp32_mquickjs_wifi_mesh_scan_record_t *record = owner->scan_record;
    memset(record, 0, sizeof(*record));
    d->stage = "mesh-scan-record";
    error = esp_mesh_scan_get_ap_record(&record->ap, record->ie);
    portENTER_CRITICAL(&s_mesh_lock);
    if (error) {
        /* Destructive native read has no rollback contract. Keep this scan
         * faulted until explicit flush/close instead of guessing its cursor. */
        owner->status.scan.error = error;
    } else {
        record->identity = scan.identity; record->sequence = sequence; record->ie_length = length;
        owner->status.scan.retained = true;
        *d->scan_record = *record;
        ++d->completed_steps;
    }
    portEXIT_CRITICAL(&s_mesh_lock);
    return error;
}

static esp_err_t mesh_scan_flush(wifi_mesh_owner_t *owner, esp32_mquickjs_wifi_mesh_control_data_t *d)
{
    d->stage = "mesh-scan-flush";
    portENTER_CRITICAL(&s_mesh_lock);
    bool valid = d->scan_identity && owner->status.scan.identity == d->scan_identity &&
        owner->status.scan.completed && !owner->status.scan.uncertain;
    portEXIT_CRITICAL(&s_mesh_lock);
    if (!valid) return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp_mesh_flush_scan_result();
    uint16_t remaining = UINT16_MAX;
    if (!error) { ++d->completed_steps; error = esp_wifi_scan_get_ap_num(&remaining); }
    if (!error && remaining) error = ESP_ERR_INVALID_RESPONSE;
    if (error) return error;
    portENTER_CRITICAL(&s_mesh_lock);
    esp32_mquickjs_wifi_mesh_scan_record_t *record = owner->scan_record;
    owner->scan_record = NULL;
    owner->status.scan = (esp32_mquickjs_wifi_mesh_scan_status_t){0};
    if (record) owner->status.reserved_bytes -= sizeof(*record);
    portEXIT_CRITICAL(&s_mesh_lock);
    if (record) {
        esp32_mquickjs_wireless_secure_zero(record, sizeof(*record));
        esp32_mquickjs_memory_payload_free(record);
    }
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_scan_commit(const esp32_mquickjs_wifi_mesh_token_t *token,
    uint32_t scan_identity, uint32_t sequence)
{
    portENTER_CRITICAL(&s_mesh_lock);
    bool valid = sequence && scan_identity && mesh_exact(token) && !s_mesh->status.closing &&
        s_mesh->status.scan.identity == scan_identity && s_mesh->status.scan.retained &&
        s_mesh->scan_record && s_mesh->scan_record->sequence == sequence;
    if (valid) {
        s_mesh->status.scan.retained = false;
        --s_mesh->status.scan.remaining;
        esp32_mquickjs_wireless_secure_zero(s_mesh->scan_record, sizeof(*s_mesh->scan_record));
    }
    portEXIT_CRITICAL(&s_mesh_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}
