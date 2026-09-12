/* Included in the NAN control pool. Only scalar identities travel through
 * ESP_TIMER_TASK and the native Wi-Fi queue. Mutations run on the Wi-Fi task;
 * final cleanup runs after Radio STOP and callback quiescence. */
extern uint8_t s_dp[1324];
extern void esp32qjs_nan_setup_timer_callback(void *argument);
extern void esp32qjs_nan_inactivity_timer_callback(void *argument);
extern void *nan_dp_get_ndl_by_bss(void *node);
extern int ieee80211_timer_process(int signal, int operation, void *argument);
_Static_assert(sizeof(ETSTimer) == 20 && offsetof(ETSTimer, timer_arg) == 16,
    "reviewed C5 NAN timer layout");

static int IRAM_ATTR nan_timer_slot_locked(const void *timer)
{
    if (timer == s_dp + 1288) return 0;
    if (timer && s_nan_tx) for (unsigned i = 1; i < 3; ++i)
        if (s_nan_tx->timers[i].timer == (uintptr_t)timer) return i;
    return -1;
}

static void IRAM_ATTR nan_timer_failed(esp_err_t error)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx && !s_nan_tx->error) s_nan_tx->error = error;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

/* Retain the actual handle in our pool even if SDK subsequently frees/memsets
 * its ETSTimer. Final cleanup never dereferences that retired SDK storage. */
static esp_err_t nan_timer_cleanup(unsigned slot)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    nan_timer_entry_t saved = s_nan_tx ? s_nan_tx->timers[slot] : (nan_timer_entry_t){0};
    if (s_nan_tx) s_nan_tx->timers[slot].active = false;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (!saved.handle) return ESP_OK;
    if (!saved.stopped) {
        esp_err_t error = esp_timer_stop(saved.handle);
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
        portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
        s_nan_tx->timers[slot].stopped = true;
        portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    }
    esp_err_t error = esp_timer_delete(saved.handle);
    if (error != ESP_OK) return error;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    s_nan_tx->timers[slot].handle = NULL;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    return ESP_OK;
}

static void nan_timer_callback(void *argument)
{
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    uint8_t phase = 0;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (identity && s_nan_tx && !s_nan_tx->closing) for (unsigned i = 0; i < 3; ++i) {
        nan_timer_entry_t *entry = &s_nan_tx->timers[i];
        if (entry->identity == identity && entry->active && !entry->fired) {
            entry->fired = true;
            phase = entry->phase;
            ++s_nan_tx->timer_callbacks;
            break;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (!phase) return;
    int error = ieee80211_timer_process(7, phase, argument);
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    /* This entered callback retains the pool. The Wi-Fi task may already
     * have consumed/rearmed the slot while the post was returning. */
    --s_nan_tx->timer_callbacks;
    if (error) for (unsigned i = 0; i < 3; ++i) {
        nan_timer_entry_t *entry = &s_nan_tx->timers[i];
        if (entry->identity == identity && entry->active) {
            entry->active = false;
            if (!s_nan_tx->error) s_nan_tx->error = error;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

bool esp32_mquickjs_wifi_nan_timer_setfn(void *timer, void *callback, void *argument)
{
    uint8_t phase = callback == (void *)esp32qjs_nan_setup_timer_callback ? 42 :
        callback == (void *)esp32qjs_nan_inactivity_timer_callback ? 43 : 0;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    int slot = nan_timer_slot_locked(timer);
    if (!phase && slot < 0) { portEXIT_CRITICAL_SAFE(&s_nan_tx_lock); return false; }
    bool ready = s_nan_tx && !s_nan_tx->closing && !s_nan_tx->error;
    if (ready && phase == 43 && slot < 0) for (unsigned i = 1; i < 3; ++i)
        if (!s_nan_tx->timers[i].timer) { slot = i; break; }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (!ready) return true;
    if (!timer || !argument || (phase == 42 ? slot != 0 :
        phase != 43 || slot < 1 || (uintptr_t)timer != (uintptr_t)argument + 320U)) {
        nan_timer_failed(ESP_ERR_INVALID_STATE); return true;
    }
    void *ndl = phase == 42 ? argument : nan_dp_get_ndl_by_bss(argument);
    if (!ndl) { nan_timer_failed(ESP_ERR_INVALID_STATE); return true; }
    ETSTimer *legacy = timer;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    bool foreign = legacy->timer_arg && legacy->timer_arg != s_nan_tx->timers[slot].handle;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (foreign) { nan_timer_failed(ESP_ERR_INVALID_STATE); return true; }
    esp_err_t error = nan_timer_cleanup(slot);
    if (error != ESP_OK) { nan_timer_failed(error); return true; }
    legacy->timer_arg = NULL;
    legacy->timer_expire = 0;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    uint32_t identity = s_nan_timer_next_identity;
    if (identity) {
        ++s_nan_timer_next_identity;
        s_nan_tx->timers[slot] = (nan_timer_entry_t){.identity = identity,
            .timer = (uintptr_t)timer, .argument = (uintptr_t)argument, .ndl = (uintptr_t)ndl, .phase = phase};
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (!identity) { nan_timer_failed(ESP_ERR_NO_MEM); return true; }
    esp_timer_handle_t handle = NULL;
    const esp_timer_create_args_t args = {.callback = nan_timer_callback,
        .arg = (void *)(uintptr_t)identity, .dispatch_method = ESP_TIMER_TASK, .name = "NAN NDP"};
    error = esp_timer_create(&args, &handle);
    if (error != ESP_OK) { nan_timer_failed(error); return true; }
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    s_nan_tx->timers[slot].handle = handle;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    memset(legacy, 0, sizeof(*legacy));
    legacy->timer_expire = UINT32_C(0x12121212);
    legacy->timer_arg = handle;
    return true;
}

bool IRAM_ATTR esp32_mquickjs_wifi_nan_timer_disarm(void *timer)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    int slot = nan_timer_slot_locked(timer);
    nan_timer_entry_t *entry = slot >= 0 && s_nan_tx ? &s_nan_tx->timers[slot] : NULL;
    /* Native peer deletion unconditionally disarms the shared setup timer.
     * Preserve another peer's current handshake when deleting this peer. */
    bool foreign = entry && slot == 0 && s_nan_tx->deleting_ndl && entry->ndl != s_nan_tx->deleting_ndl;
    esp_timer_handle_t handle = entry && !foreign ? entry->handle : NULL;
    if (entry && !foreign) entry->active = false;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (slot < 0) return false;
    if (handle) {
        esp_err_t error = esp_timer_stop(handle);
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) nan_timer_failed(error);
        else {
            portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
            s_nan_tx->timers[slot].stopped = true;
            portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
        }
    }
    return true;
}

bool esp32_mquickjs_wifi_nan_timer_done(void *timer)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    int slot = nan_timer_slot_locked(timer);
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (slot < 0) return false;
    esp_err_t error = nan_timer_cleanup(slot);
    if (error != ESP_OK) nan_timer_failed(error);
    else {
        portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
        if (s_nan_tx) s_nan_tx->timers[slot] = (nan_timer_entry_t){0};
        portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    }
    /* A failed delete is retained in our ledger, never in freed node storage. */
    ((ETSTimer *)timer)->timer_arg = NULL;
    ((ETSTimer *)timer)->timer_expire = 0;
    return true;
}

bool IRAM_ATTR esp32_mquickjs_wifi_nan_timer_arm(void *timer, uint64_t us, bool repeat)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    int slot = nan_timer_slot_locked(timer);
    nan_timer_entry_t *entry = slot >= 0 && s_nan_tx ? &s_nan_tx->timers[slot] : NULL;
    bool valid = entry && entry->identity && entry->handle && !entry->armed && !repeat &&
        !s_nan_tx->closing && !s_nan_tx->error;
    esp_timer_handle_t handle = valid ? entry->handle : NULL;
    if (valid) { entry->active = entry->armed = true; entry->stopped = false; }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (slot < 0) return false;
    if (!valid) { nan_timer_failed(ESP_ERR_INVALID_STATE); return true; }
    esp_err_t error = esp_timer_start_once(handle, us);
    if (error != ESP_OK) {
        portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
        s_nan_tx->timers[slot].active = false;
        portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
        nan_timer_failed(error);
    }
    return true;
}

void esp32_mquickjs_wifi_nan_timer_peer_delete(const void *ndl, bool entering)
{
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (s_nan_tx) {
        if (entering) {
            s_nan_tx->deleting_ndl = (uintptr_t)ndl;
            for (unsigned i = 0; i < 3; ++i)
                if (s_nan_tx->timers[i].ndl == (uintptr_t)ndl) s_nan_tx->timers[i].active = false;
        } else if (s_nan_tx->deleting_ndl == (uintptr_t)ndl) s_nan_tx->deleting_ndl = 0;
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}

static void nan_timer_process(void *argument, uint8_t phase)
{
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    void *native = NULL;
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    if (identity && s_nan_tx && !s_nan_tx->closing && !s_nan_tx->error) for (unsigned i = 0; i < 3; ++i) {
        nan_timer_entry_t *entry = &s_nan_tx->timers[i];
        if (entry->identity == identity && entry->phase == phase && entry->active && entry->fired) {
            native = (void *)entry->argument;
            entry->active = false;
            ++s_nan_tx->timer_callbacks;
            break;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    if (!native) return;
    esp32_mquickjs_wifi_nan_sdk_timer_process(phase, native);
    portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
    --s_nan_tx->timer_callbacks;
    portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
}
void __wrap_nan_ndp_setup_timeout_process(void *argument) { nan_timer_process(argument, 42); }
void __wrap_nan_ndp_inactivity_timeout_process(void *argument) { nan_timer_process(argument, 43); }

static esp_err_t nan_timers_close(void)
{
    for (unsigned i = 0; i < 3; ++i) {
        esp_err_t error = nan_timer_cleanup(i);
        if (error != ESP_OK) { nan_timer_failed(error); return error; }
    }
    return ESP_OK;
}

/* Wi-Fi task only: no native peer parser/deletion can be in progress here.
 * Queued timer messages carry revoked scalar IDs, never this retired node. */
static esp_err_t nan_timer_retire_peer_native(uintptr_t ndl)
{
    if (!ndl) return ESP_OK;
    for (unsigned i = 0; i < 3; ++i) {
        portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
        nan_timer_entry_t saved = s_nan_tx ? s_nan_tx->timers[i] : (nan_timer_entry_t){0};
        bool deleting = s_nan_tx && s_nan_tx->deleting_ndl == ndl;
        portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
        if (saved.ndl != ndl) continue;
        if (saved.active || deleting) return ESP_ERR_TIMEOUT;
        esp_err_t error = nan_timer_cleanup(i);
        if (error != ESP_OK) return error;
        /* Only the shared setup ETSTimer has persistent storage. Dynamic
         * inactivity ETSTimers belonged to nodes already freed by the SDK. */
        if (i == 0) {
            ETSTimer *legacy = (ETSTimer *)(s_dp + 1288);
            if (legacy->timer_arg == saved.handle) {
                legacy->timer_arg = NULL;
                legacy->timer_expire = 0;
            }
        }
        portENTER_CRITICAL_SAFE(&s_nan_tx_lock);
        s_nan_tx->timers[i] = (nan_timer_entry_t){0};
        portEXIT_CRITICAL_SAFE(&s_nan_tx_lock);
    }
    return ESP_OK;
}
