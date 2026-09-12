/* Included after the SDK API mutex helpers, before its asynchronous callers.
 * The eloop receives numeric tickets only. Cancellation of an already detached
 * callback invalidates its admission while keeping its owned payload alive.
 * This ledger does not prove RF/driver or default-event-loop retirement. */
#define ESP32QJS_DPP_ASYNC_MAX 32U
static void esp_dpp_rx_action(void *, void *);
static void esp_dpp_bootstrap_gen(void *, void *);
static void esp32qjs_dpp_async_dispatch(void *, void *);

struct esp32qjs_dpp_async {
    struct esp32qjs_dpp_async *next;
    uint64_t ticket;
    uint32_t generation;
    eloop_timeout_handler handler;
    void *data, *user;
    bool cancelled, executing;
};
static struct esp32qjs_dpp_async *s_dpp_async;
static uint64_t s_dpp_async_last_ticket;
static uint32_t s_dpp_generation;
static unsigned s_dpp_async_count, s_dpp_async_active;

static void esp32qjs_dpp_async_free(struct esp32qjs_dpp_async *entry, bool drop)
{
    struct esp32qjs_dpp_async **link = &s_dpp_async;
    while (*link && *link != entry) link = &(*link)->next;
    if (!*link) return;
    *link = entry->next;
    --s_dpp_async_count;
    if (drop) {
        if (entry->handler == esp_dpp_rx_action) {
            struct action_rx_param *rx = entry->data;
            if (rx) bin_clear_free(rx, sizeof(*rx) + rx->frm_len);
        }
        /* Bootstrap owns a separate scrubbed job, cancelled explicitly before
         * draining the rest of the ledger. Its arguments are numeric too. */
    }
    bin_clear_free(entry, sizeof(*entry));
}

static int esp32qjs_dpp_async_register(unsigned secs, unsigned usecs,
    eloop_timeout_handler handler, void *data, void *user)
{
    if (dpp_api_lock() != ESP_OK) return -1;
    if (!handler || !atomic_load(&s_dpp_init_done) || atomic_load(&dpp_shutting_down) ||
        s_dpp_ctx.dpp_deinit_pending || s_dpp_async_count >= ESP32QJS_DPP_ASYNC_MAX ||
        s_dpp_async_last_ticket == UINT64_MAX) {
        dpp_api_unlock(); return -1;
    }
    struct esp32qjs_dpp_async *entry = os_zalloc(sizeof(*entry));
    if (!entry) { dpp_api_unlock(); return -1; }
    entry->ticket = ++s_dpp_async_last_ticket;
    entry->generation = s_dpp_generation;
    entry->handler = handler; entry->data = data; entry->user = user;
    entry->next = s_dpp_async; s_dpp_async = entry; ++s_dpp_async_count;
    int ret = eloop_register_timeout(secs, usecs, esp32qjs_dpp_async_dispatch,
        (void *)(uintptr_t)(entry->ticket >> 32), (void *)(uintptr_t)(uint32_t)entry->ticket);
    if (ret != 0) esp32qjs_dpp_async_free(entry, false); /* Caller still owns args. */
    dpp_api_unlock();
    return ret;
}

static bool esp32qjs_dpp_async_matches(const struct esp32qjs_dpp_async *entry,
    eloop_timeout_handler handler, void *data, void *user)
{
    return (!handler || entry->handler == handler) &&
        (data == ELOOP_ALL_CTX || entry->data == data) &&
        (user == ELOOP_ALL_CTX || entry->user == user);
}

static int esp32qjs_dpp_async_cancel(eloop_timeout_handler handler, void *data, void *user)
{
    if (dpp_api_lock() != ESP_OK) return 0;
    int removed = 0;
    struct esp32qjs_dpp_async *entry = s_dpp_async;
    while (entry) {
        struct esp32qjs_dpp_async *next = entry->next;
        if (!entry->executing && esp32qjs_dpp_async_matches(entry, handler, data, user)) {
            entry->cancelled = true;
            int n = eloop_cancel_timeout(esp32qjs_dpp_async_dispatch,
                (void *)(uintptr_t)(entry->ticket >> 32), (void *)(uintptr_t)(uint32_t)entry->ticket);
            if (n == 1) { esp32qjs_dpp_async_free(entry, true); ++removed; }
            /* Zero means potentially dispatched and waiting for this mutex.
             * Keep the record. It must not execute its original handler. */
        }
        entry = next;
    }
    dpp_api_unlock();
    return removed;
}

static void esp32qjs_dpp_async_dispatch(void *high, void *low)
{
    uint64_t ticket = ((uint64_t)(uintptr_t)high << 32) | (uint32_t)(uintptr_t)low;
    if (!ticket || dpp_api_lock() != ESP_OK) return;
    struct esp32qjs_dpp_async *entry = s_dpp_async;
    while (entry && entry->ticket != ticket) entry = entry->next;
    if (!entry || entry->executing) { dpp_api_unlock(); return; }
    if (entry->cancelled || entry->generation != s_dpp_generation ||
        !atomic_load(&s_dpp_init_done) || atomic_load(&dpp_shutting_down) || s_dpp_ctx.dpp_deinit_pending) {
        /* Bootstrap must run its own guarded cleanup even after close was
         * requested; it cannot create a URI in that state. */
        if (entry->handler != esp_dpp_bootstrap_gen) {
            esp32qjs_dpp_async_free(entry, true); dpp_api_unlock(); return;
        }
    }
    entry->executing = true; ++s_dpp_async_active;
    eloop_timeout_handler handler = entry->handler;
    void *data = entry->data, *user = entry->user;
    dpp_api_unlock();
    handler(data, user); /* SDK handler owns and frees any payload from here. */
    if (dpp_api_lock() != ESP_OK) return;
    --s_dpp_async_active;
    esp32qjs_dpp_async_free(entry, false);
    dpp_api_unlock();
}

/* Init/deinit execute on the same Wi-Fi task as eloop callbacks. An executing
 * callback cannot be retired from a nested native command. Other tasks may
 * only invalidate queued work; their zero cancellation count is not a fence. */
static esp_err_t esp32qjs_dpp_async_drain_locked(void)
{
    if (s_dpp_async_active) return ESP_ERR_NOT_FINISHED;
    esp32qjs_dpp_async_cancel(NULL, ELOOP_ALL_CTX, ELOOP_ALL_CTX);
    return s_dpp_async_count ? ESP_ERR_NOT_FINISHED : ESP_OK;
}
