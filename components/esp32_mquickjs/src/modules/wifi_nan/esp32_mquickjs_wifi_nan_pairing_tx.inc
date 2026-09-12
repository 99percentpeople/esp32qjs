/* The pinned C5 mgmt producer calls ieee80211_mgmt_output(node, EB, subtype)
 * before NAN/home-channel queues take the EB. Its NAN Auth branch has no
 * service-specific completion; actual recycling retires the frame. Keep the
 * entire producer and any entered callback alive even if recycling is nested.
 * The supplying PASN command runs on the Wi-Fi task, where ieee80211_ioctl
 * executes synchronously. This scope never matches another task's traffic. */
extern bool current_task_is_wifi_task(void);
extern int __real_ieee80211_mgmt_output(void *node, void *buffer, uintptr_t subtype);
extern void ic_ebuf_recycle_tx(void *buffer);

uint8_t esp32_mquickjs_wifi_nan_pairing_cache_service(unsigned slot)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    uint8_t service = s_nan_tx && slot < ESP_WIFI_NAN_DATAPATH_MAX_PEERS ? s_nan_tx->paired_cache_services[slot] : 0;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return service;
}

bool esp32_mquickjs_wifi_nan_pairing_cache_bind(unsigned slot, uint8_t service_id)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool valid = s_nan_tx && slot < ESP_WIFI_NAN_DATAPATH_MAX_PEERS;
    if (valid) s_nan_tx->paired_cache_services[slot] = service_id;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return valid;
}

esp_err_t esp32_mquickjs_wifi_nan_tx_pairing_enter(uint32_t identity, uint8_t service_id, bool authentication)
{
    if (!identity || !service_id || !current_task_is_wifi_task()) return ESP_ERR_INVALID_ARG;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool admitted = s_nan_tx && !s_nan_tx->closing && !s_nan_tx->error && !s_nan_tx->pairing.task;
    if (admitted) {
        if (s_nan_tx->pairing_result.identity != identity) {
            /* Binding admission excludes overlapping PASN operations. */
            memset(&s_nan_tx->pairing_result, 0, sizeof(s_nan_tx->pairing_result));
            s_nan_tx->pairing_result.identity = identity;
        }
        s_nan_tx->pairing.task = task;
        s_nan_tx->pairing.identity = identity;
        s_nan_tx->pairing.service_id = service_id;
        s_nan_tx->pairing.authentication = authentication;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return admitted ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void esp32_mquickjs_wifi_nan_tx_pairing_leave(uint32_t identity)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx && s_nan_tx->pairing.task == task && s_nan_tx->pairing.identity == identity)
        memset(&s_nan_tx->pairing, 0, sizeof(s_nan_tx->pairing));
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

bool esp32_mquickjs_wifi_nan_tx_pairing_settled(uint32_t identity, esp_err_t *error, int64_t *completed_us)
{
    bool settled = true;
    *error = ESP_OK; *completed_us = 0;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (identity && s_nan_tx) {
        if (s_nan_tx->pairing_result.identity == identity) {
            *error = s_nan_tx->pairing_result.error;
            *completed_us = s_nan_tx->pairing_result.completed_us;
            if (s_nan_tx->pairing_result.followups_pending) settled = false;
        }
        if (s_nan_tx->error) { *error = s_nan_tx->error; settled = false; }
        if (s_nan_tx->pairing.task && s_nan_tx->pairing.identity == identity) settled = false;
        for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i)
            if (s_nan_tx->entries[i].ticket && s_nan_tx->entries[i].pairing_identity == identity) settled = false;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return settled;
}

bool esp32_mquickjs_wifi_nan_tx_pairing_drained(uint32_t identity)
{
    /* Parent shutdown also calls this when no PASN operation ever started. */
    if (!identity) return true;
    bool drained = true;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx) {
        if (s_nan_tx->error || (s_nan_tx->pairing.task && s_nan_tx->pairing.identity == identity)) drained = false;
        for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i)
            if (s_nan_tx->entries[i].ticket && s_nan_tx->entries[i].pairing_identity == identity) drained = false;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return drained;
}

int __wrap_ieee80211_mgmt_output(void *node, void *buffer, uintptr_t subtype)
{
    if (!buffer) return __real_ieee80211_mgmt_output(node, buffer, subtype);
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    nan_tx_entry_t *entry = NULL;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool scoped = s_nan_tx && s_nan_tx->pairing.task == task && s_nan_tx->pairing.authentication;
    if (scoped && subtype == 0xb0 && !s_nan_tx->closing && !s_nan_tx->error && s_nan_tx_next_ticket) {
        for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
            if (s_nan_tx->entries[i].ticket) continue;
            entry = &s_nan_tx->entries[i];
            *entry = (nan_tx_entry_t){.buffer = buffer, .ticket = s_nan_tx_next_ticket++,
                .pairing_identity = s_nan_tx->pairing.identity, .service_id = s_nan_tx->pairing.service_id,
                .authentication = true, .callbacks = 1};
            break;
        }
    }
    if (scoped && !entry && s_nan_tx->rejected != UINT32_MAX) ++s_nan_tx->rejected;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (!scoped) return __real_ieee80211_mgmt_output(node, buffer, subtype);
    if (!entry) {
        /* The native caller relinquishes this EB to mgmt_output even when it
         * fails. Return it through the same native recycler exactly once. */
        ic_ebuf_recycle_tx(buffer);
        return subtype == 0xb0 ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_ARG;
    }
    int result = __real_ieee80211_mgmt_output(node, buffer, subtype);
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    --entry->callbacks;
    nan_tx_entry_retire(entry);
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return result;
}
static uint32_t s_nan_credential_next_identity = 1;

bool esp32_mquickjs_wifi_nan_credential_info(unsigned slot, esp32_mquickjs_wifi_nan_credential_info_t *out)
{
    if (!out) return false;
    *out = (esp32_mquickjs_wifi_nan_credential_info_t){0};
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool valid = s_nan_tx && !s_nan_tx->closing && slot < ESP32_MQUICKJS_NAN_CACHED_PAIRINGS;
    if (valid) *out = s_nan_tx->credentials[slot];
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return valid && out->identity && (!out->expires_us || esp_timer_get_time() < out->expires_us);
}

esp_err_t esp32_mquickjs_wifi_nan_credential_replace(unsigned slot, const uint8_t peer[6], int64_t expires_us, uint32_t *identity)
{
    if (!peer || !identity || slot >= ESP32_MQUICKJS_NAN_CACHED_PAIRINGS) return ESP_ERR_INVALID_ARG;
    *identity = 0;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    esp_err_t error = !s_nan_tx || s_nan_tx->closing || s_nan_tx->error ? ESP_ERR_INVALID_STATE :
        !s_nan_credential_next_identity ? ESP_ERR_NO_MEM : ESP_OK;
    if (!error) {
        esp32_mquickjs_wifi_nan_credential_info_t *info = &s_nan_tx->credentials[slot];
        *identity = info->identity = s_nan_credential_next_identity++;
        memcpy(info->peer, peer, 6); info->expires_us = expires_us;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return error;
}
