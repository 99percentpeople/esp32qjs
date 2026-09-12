/* Shares the TX pool and its lock. Only scalar identities cross callbacks;
 * native addresses are values used to match current driver-owned objects. */
extern uint8_t s_dp[1324];

static nan_ndp_entry_t *nan_ndp_find_locked(uint32_t identity)
{
    if (identity && s_nan_tx) for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i)
        if (s_nan_tx->datapaths[i].status.identity == identity) return &s_nan_tx->datapaths[i];
    return NULL;
}

static nan_ndp_entry_t *nan_ndp_peer_locked(uint8_t ndp_id, const uint8_t peer[6])
{
    if (s_nan_tx && peer) for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        nan_ndp_entry_t *entry = &s_nan_tx->datapaths[i];
        if (entry->status.identity && (!ndp_id || entry->status.ndp_id == ndp_id) &&
            !memcmp(entry->status.peer, peer, 6)) return entry;
    }
    return NULL;
}

bool esp32_mquickjs_wifi_nan_ndp_service_busy(uint8_t service_id)
{
    bool busy = false;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx) for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        const esp32_mquickjs_wifi_nan_ndp_status_t *status = &s_nan_tx->datapaths[i].status;
        if (status->identity && (!status->service_id || status->service_id == service_id)) busy = true;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return busy;
}

bool esp32_mquickjs_wifi_nan_ndp_peer_busy(const uint8_t peer[6])
{
    if (!peer) return true;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool busy = nan_ndp_peer_locked(0, peer) != NULL;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return busy;
}

static nan_ndp_entry_t *nan_ndp_allocate_locked(void)
{
    if (!s_nan_tx || s_nan_tx->closing || s_nan_tx->error || !s_nan_ndp_next_identity) return NULL;
    /* The pinned driver has one shared setup timer. Do not overwrite an
     * in-flight handshake, even though two established peers are supported. */
    for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        const esp32_mquickjs_wifi_nan_ndp_status_t *s = &s_nan_tx->datapaths[i].status;
        if (s->identity && !s->accepted && !s->native_deleted) return NULL;
    }
    for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        nan_ndp_entry_t *entry = &s_nan_tx->datapaths[i];
        if (!entry->status.identity) {
            entry->status.identity = s_nan_ndp_next_identity++;
            entry->status.started_us = esp_timer_get_time();
            return entry;
        }
    }
    return NULL;
}

esp_err_t esp32_mquickjs_wifi_nan_ndp_begin(uint8_t service_id,
    wifi_nan_datapath_req_t *request, uint32_t *identity)
{
    static const uint8_t zero[6];
    if (!service_id || !request || !identity || *identity || !request->pub_id ||
        (request->peer_mac[0] & 1) || !memcmp(request->peer_mac, zero, 6)) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = nan_ndp_peer_locked(0, request->peer_mac) ? NULL : nan_ndp_allocate_locked();
    esp_err_t error = entry ? ESP_OK : !s_nan_ndp_next_identity ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE;
    if (entry) {
        entry->status.service_id = service_id;
        entry->status.publisher_id = request->pub_id;
        entry->status.outgoing = true;
        memcpy(entry->status.peer, request->peer_mac, 6);
        entry->request = request;
        *identity = entry->status.identity;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return error;
}

static bool nan_ndp_frames_retired_locked(const esp32_mquickjs_wifi_nan_ndp_status_t *status)
{
    if (s_nan_tx->error) return false;
    for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
        const nan_tx_entry_t *tx = &s_nan_tx->entries[i];
        if (tx->ticket && (tx->ndp_identity == status->identity ||
            (!tx->data && tx->datapath && !memcmp(tx->peer, status->peer, 6) &&
             (!tx->ndp_id || tx->ndp_id == status->ndp_id)))) return false;
    }
    return true;
}

bool esp32_mquickjs_wifi_nan_ndp_status(uint32_t identity, esp32_mquickjs_wifi_nan_ndp_status_t *status)
{
    if (!status) return false;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = nan_ndp_find_locked(identity);
    if (entry) {
        *status = entry->status;
        status->frames_retired = nan_ndp_frames_retired_locked(status);
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return entry != NULL;
}

uint32_t esp32_mquickjs_wifi_nan_ndp_request_enter(const void *request)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    uint32_t identity = 0;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx && !s_nan_tx->ndp_scope_task && !s_nan_tx->closing) {
        for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
            nan_ndp_entry_t *entry = &s_nan_tx->datapaths[i];
            if (entry->status.identity && entry->request == request && !entry->native_ndl) {
                identity = entry->status.identity;
                s_nan_tx->ndp_scope_identity = identity;
                s_nan_tx->ndp_scope_task = task;
                break;
            }
        }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return identity;
}

unsigned esp32_mquickjs_wifi_nan_ndp_list(esp32_mquickjs_wifi_nan_ndp_status_t status[ESP_WIFI_NAN_DATAPATH_MAX_PEERS])
{
    if (!status) return 0;
    unsigned count = 0;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx) for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        const nan_ndp_entry_t *entry = &s_nan_tx->datapaths[i];
        if (!entry->status.identity) continue;
        status[count] = entry->status;
        status[count].frames_retired = nan_ndp_frames_retired_locked(&entry->status);
        ++count;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return count;
}

bool esp32_mquickjs_wifi_nan_ndp_ssi(uint32_t identity, uint8_t out[ESP_WIFI_MAX_SVC_SSI_LEN], uint16_t *length)
{
    if (!out || !length) return false;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    const nan_ndp_entry_t *entry = nan_ndp_find_locked(identity);
    if (entry) { *length = entry->ssi_len; memcpy(out, entry->ssi, entry->ssi_len); }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return entry != NULL;
}

void esp32_mquickjs_wifi_nan_ndp_request_leave(uint32_t identity, int error)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = nan_ndp_find_locked(identity);
    if (entry && s_nan_tx->ndp_scope_identity == identity) {
        entry->status.submit_error = error;
        s_nan_tx->ndp_scope_identity = 0;
        s_nan_tx->ndp_scope_task = NULL;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

void esp32_mquickjs_wifi_nan_ndp_submitted(uint32_t identity, esp_err_t error)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = nan_ndp_find_locked(identity);
    if (entry) {
        entry->request = NULL;
        entry->status.submitted = error == ESP_OK;
        if (error && !entry->status.error) entry->status.error = error;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

esp_err_t esp32_mquickjs_wifi_nan_ndp_native_claim(void *ndl, uint8_t id, uint32_t *identity)
{
    if (!ndl || !id || !identity || *identity) return ESP_ERR_INVALID_ARG;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = NULL;
    if (s_nan_tx && !s_nan_tx->closing && !s_nan_tx->error) {
        bool address_busy = false;
        for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i)
            if (s_nan_tx->datapaths[i].status.identity && s_nan_tx->datapaths[i].native_ndl == (uintptr_t)ndl)
                address_busy = true;
        if (!address_busy) {
            if (s_nan_tx->ndp_scope_task == task) entry = nan_ndp_find_locked(s_nan_tx->ndp_scope_identity);
            else if (!nan_ndp_peer_locked(0, ndl)) entry = nan_ndp_allocate_locked();
        }
    }
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (entry && !entry->native_ndl && (!entry->status.outgoing || !memcmp(entry->status.peer, ndl, 6))) {
        entry->native_ndl = (uintptr_t)ndl;
        entry->status.ndp_id = id;
        memcpy(entry->status.peer, ndl, 6);
        *identity = entry->status.identity;
        error = ESP_OK;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return error;
}

void esp32_mquickjs_wifi_nan_ndp_native_bound(uint32_t identity, bool bound)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = nan_ndp_find_locked(identity);
    if (entry) entry->status.native_bound = bound;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

uint32_t esp32_mquickjs_wifi_nan_ndp_deleting(const void *ndl)
{
    uint32_t identity = 0;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (ndl && s_nan_tx) for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        nan_ndp_entry_t *entry = &s_nan_tx->datapaths[i];
        if (entry->status.identity && entry->native_ndl == (uintptr_t)ndl && !entry->status.native_deleted) {
            identity = entry->status.identity;
            break;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return identity;
}

void esp32_mquickjs_wifi_nan_ndp_deleted(uint32_t identity)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = nan_ndp_find_locked(identity);
    if (entry) {
        entry->status.native_deleted = true;
        if (!entry->status.completed_us) entry->status.completed_us = now;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

bool esp32_mquickjs_wifi_nan_ndp_peer_service(const uint8_t peer[6], uint8_t publisher_id,
    uint8_t service_id)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = nan_ndp_peer_locked(0, peer);
    bool allowed = !entry || !entry->status.outgoing ||
        (entry->status.publisher_id == publisher_id && entry->status.service_id == service_id &&
         !entry->status.native_deleted);
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return allowed;
}

void esp32_mquickjs_wifi_nan_ndp_notice(const esp32_mquickjs_wifi_nan_sdk_notice_t *notice)
{
    if (!notice || !notice->ndp_id) return;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = nan_ndp_peer_locked(notice->ndp_id, notice->peer);
    if (entry && !entry->status.native_deleted) {
        esp32_mquickjs_wifi_nan_ndp_status_t *s = &entry->status;
        if (notice->kind == ESP32_MQUICKJS_NAN_SDK_NDP_REQUEST && !s->outgoing) {
            s->service_id = s->publisher_id = notice->service_id;
            s->host_bound = true;
        } else if (notice->kind == ESP32_MQUICKJS_NAN_SDK_NDP_BOUND && notice->context == s->identity) {
            s->host_bound = notice->status == ESP_OK;
            if (notice->status && !s->error) s->error = notice->status;
        } else if (notice->kind == ESP32_MQUICKJS_NAN_SDK_NDP_ACCEPTED) {
            s->accepted = true;
            s->native_status = notice->status;
            if (!s->completed_us) s->completed_us = now;
        } else if (notice->kind == ESP32_MQUICKJS_NAN_SDK_NDP_REJECTED) {
            s->rejected = true;
            s->native_status = notice->status;
            if (!s->error) s->error = ESP_FAIL;
            if (!s->completed_us) s->completed_us = now;
        } else if (notice->kind == ESP32_MQUICKJS_NAN_SDK_NDP_TERMINATED) {
            s->terminated = true;
            s->native_status = notice->status;
            if (!s->completed_us) s->completed_us = now;
        } else if (notice->kind == ESP32_MQUICKJS_NAN_SDK_NDP_FAILED && !s->error) s->error = notice->status;
        else if (notice->kind == ESP32_MQUICKJS_NAN_SDK_NDP_CLEANUP_FAILED) s->cleanup_error = notice->status;
        if ((notice->kind == ESP32_MQUICKJS_NAN_SDK_NDP_ACCEPTED ||
                notice->kind == ESP32_MQUICKJS_NAN_SDK_NDP_REQUEST) && notice->data &&
                notice->size == sizeof(esp32_mquickjs_wifi_nan_sdk_ndp_data_t)) {
            const esp32_mquickjs_wifi_nan_sdk_ndp_data_t *data = notice->data;
            memcpy(s->peer_ndi, data->peer_ndi, 6);
            memcpy(s->own_ndi, data->own_ndi, 6);
            memcpy(s->ipv6_identifier, data->ipv6_identifier, 8);
            if (data->ssi_len > sizeof(entry->ssi) || (data->ssi_len && !data->ssi)) {
                if (!s->error) s->error = ESP_ERR_INVALID_RESPONSE;
            } else {
                entry->ssi_len = data->ssi_len;
                if (data->ssi_len) memcpy(entry->ssi, data->ssi, data->ssi_len);
            }
        }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

esp_err_t esp32_mquickjs_wifi_nan_ndp_mutation(uint32_t identity, bool end,
    esp32_mquickjs_wifi_nan_ndp_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = nan_ndp_find_locked(identity);
    bool valid = entry && !s_nan_tx->closing && entry->status.native_bound && !entry->status.native_deleted &&
        (end ? !entry->status.end_attempted : !entry->status.outgoing && entry->status.host_bound &&
         !entry->status.response_attempted && !entry->status.accepted && !entry->status.terminated);
    if (valid) {
        if (end) entry->status.end_attempted = true;
        else entry->status.response_attempted = true;
        *status = entry->status;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_nan_ndp_mutation_done(uint32_t identity, bool end, esp_err_t error)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = nan_ndp_find_locked(identity);
    if (entry) {
        if (end) { entry->status.end_submitted = error == ESP_OK; entry->status.cleanup_error = error; }
        else if (error && !entry->status.error) entry->status.error = error;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

static bool nan_ndp_retired_locked(const nan_ndp_entry_t *entry)
{
    return entry && !s_nan_tx->closing && !entry->request &&
        s_nan_tx->ndp_scope_identity != entry->status.identity &&
        (entry->status.native_deleted || (!entry->native_ndl && entry->status.outgoing &&
         !entry->status.submitted)) && nan_ndp_frames_retired_locked(&entry->status);
}

esp_err_t esp32_mquickjs_wifi_nan_ndp_retire_native(uint32_t identity, bool commit,
    esp32_mquickjs_wifi_nan_ndp_status_t *status)
{
    if (!identity || !status) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_ndp_entry_t *entry = nan_ndp_find_locked(identity);
    esp_err_t error = !entry ? ESP_ERR_INVALID_STATE :
        nan_ndp_retired_locked(entry) ? ESP_OK : ESP_ERR_TIMEOUT;
    uintptr_t ndl = entry ? entry->native_ndl : 0;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (error != ESP_OK) return error;
    error = nan_timer_retire_peer_native(ndl);
    if (error != ESP_OK) return error;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    entry = nan_ndp_find_locked(identity);
    if (!nan_ndp_retired_locked(entry)) error = ESP_ERR_TIMEOUT;
    else {
        *status = entry->status;
        status->frames_retired = true;
        if (commit) memset(entry, 0, sizeof(*entry));
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_nan_ndp_select_native_id(uint8_t current, uint8_t *selected)
{
    if (!selected) return ESP_ERR_INVALID_ARG;
    uint8_t used[32] = {1}; /* native protocol ID zero is never valid */
    /* Native table reads are serialized by the Wi-Fi task. Include peers
     * rejected before framework admission as well as retained framework IDs. */
    for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        uint8_t id = s_dp[104 + 580 * i + 9];
        used[id >> 3] |= (uint8_t)(1U << (id & 7));
    }
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool ready = s_nan_tx && !s_nan_tx->closing && !s_nan_tx->error;
    if (ready) {
        for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
            uint8_t id = s_nan_tx->datapaths[i].status.ndp_id;
            if (s_nan_tx->datapaths[i].status.identity) used[id >> 3] |= (uint8_t)(1U << (id & 7));
        }
        for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
            const nan_tx_entry_t *entry = &s_nan_tx->entries[i];
            if (entry->ticket && entry->datapath)
                used[entry->ndp_id >> 3] |= (uint8_t)(1U << (entry->ndp_id & 7));
        }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (!ready) return ESP_ERR_INVALID_STATE;
    unsigned candidate = current ? current : 1;
    for (unsigned i = 0; i < UINT8_MAX; ++i) {
        if (!(used[candidate >> 3] & (1U << (candidate & 7)))) {
            *selected = (uint8_t)candidate;
            return ESP_OK;
        }
        candidate = candidate == UINT8_MAX ? 1 : candidate + 1;
    }
    return ESP_ERR_NO_MEM;
}
