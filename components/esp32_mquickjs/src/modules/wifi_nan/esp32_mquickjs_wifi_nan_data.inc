/* The pinned output/API objects transfer NAN data EBs to nan_dp_post_tx. Its
 * per-peer/multicast queues and direct ppTxPkt path bypass ic_tx_pkt entirely.
 * Track before that transfer; native queues/PP retain all physical ownership. */
#define NAN_DATA_CAPACITY (NAN_TX_CAPACITY - 8U)
extern int __real_nan_dp_post_tx(void *node, void *buffer);
extern void ic_ebuf_recycle_tx(void *buffer);

static esp_err_t nan_data_admit(void *node, void *buffer, uint32_t *ticket, bool *dispose)
{
    if (!buffer) { *dispose = false; return ESP_ERR_INVALID_ARG; }
    const uint8_t *descriptor, *frame;
    uint16_t flags, length;
    memcpy(&descriptor, (const uint8_t *)buffer + 4, sizeof(descriptor));
    memcpy(&flags, (const uint8_t *)buffer + 40, sizeof(flags));
    memcpy(&length, (const uint8_t *)buffer + 20, sizeof(length));
    if (!descriptor || length < 10) return ESP_ERR_INVALID_RESPONSE;
    memcpy(&frame, descriptor + 4, sizeof(frame));
    if (!frame) return ESP_ERR_INVALID_RESPONSE;
    if (flags & UINT16_C(0x2000)) frame += 8;
    bool group = (frame[4] & 1) != 0;
    void *ndl = !group && node ? nan_dp_get_ndl_by_bss(node) : NULL;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_tx_entry_t *slot = NULL;
    unsigned data = 0;
    if (s_nan_tx) {
        for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
            nan_tx_entry_t *entry = &s_nan_tx->entries[i];
            if (entry->ticket && entry->data) ++data;
            if (!entry->ticket && !slot) slot = entry;
            if (entry->ticket && entry->buffer == buffer && !entry->recycling && !entry->recycled) {
                /* This address is still owned. A duplicate must never recycle
                 * another in-flight submission, including on admission error. */
                *dispose = false;
                if (!s_nan_tx->error) s_nan_tx->error = ESP_ERR_INVALID_RESPONSE;
                slot = NULL;
                break;
            }
        }
        nan_ndp_entry_t *operation = ndl ? nan_ndp_peer_locked(0, ndl) : NULL;
        bool exact = !ndl || (operation && operation->native_ndl == (uintptr_t)ndl &&
            !operation->status.native_deleted && operation->status.native_bound);
        if (slot && !s_nan_tx->closing && !s_nan_tx->error && exact) {
            error = ESP_ERR_NO_MEM;
            if (data < NAN_DATA_CAPACITY && s_nan_tx_next_ticket) {
                *slot = (nan_tx_entry_t){.ticket = s_nan_tx_next_ticket++, .buffer = buffer,
                    .datapath = true, .data = true, .callbacks = 1}; /* complete post body */
                if (operation) {
                    slot->ndp_identity = operation->status.identity;
                    slot->ndp_id = operation->status.ndp_id;
                    memcpy(slot->peer, operation->status.peer, sizeof(slot->peer));
                } else memcpy(slot->peer, frame + 4, sizeof(slot->peer));
                *ticket = slot->ticket;
                if (s_nan_tx->submissions != UINT32_MAX) ++s_nan_tx->submissions;
                error = ESP_OK;
            }
        } else if (!slot && !s_nan_tx->closing && !s_nan_tx->error) error = ESP_ERR_NO_MEM;
        if (error && s_nan_tx->rejected != UINT32_MAX) ++s_nan_tx->rejected;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return error;
}

int __wrap_nan_dp_post_tx(void *node, void *buffer)
{
    uint32_t ticket = 0;
    bool dispose = true;
    esp_err_t error = nan_data_admit(node, buffer, &ticket, &dispose);
    if (error != ESP_OK) {
        if (dispose) ic_ebuf_recycle_tx(buffer);
        return error;
    }
    int result = __real_nan_dp_post_tx(node, buffer);
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
        nan_tx_entry_t *entry = &s_nan_tx->entries[i];
        if (entry->ticket == ticket) {
            --entry->callbacks;
            nan_tx_entry_retire(entry);
            break;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    /* An error return is not a recycle acknowledgement. The real caller/PP
     * still decides when the physical EB may be returned. */
    return result;
}

bool esp32_mquickjs_wifi_nan_tx_callback_allowed(uint32_t ticket)
{
    bool allowed = false;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (ticket && s_nan_tx && !s_nan_tx->closing) for (unsigned i = 0; i < NAN_TX_CAPACITY; ++i) {
        const nan_tx_entry_t *entry = &s_nan_tx->entries[i];
        if (entry->ticket != ticket || !entry->callbacks) continue;
        nan_ndp_entry_t *operation = nan_ndp_find_locked(entry->ndp_identity);
        allowed = entry->ndp_identity ? operation && !operation->status.native_deleted : !entry->data;
        break;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return allowed;
}
