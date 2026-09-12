/* One bounded request, with a non-reused ticket in the eloop queue. A late
 * callback cannot dereference an old allocation or change a newer bootstrap. */
#define ESP32QJS_DPP_CHANNEL_TEXT_MAX (ESP_DPP_MAX_CHAN_COUNT * 8U)
struct esp32qjs_dpp_bootstrap_job {
    uint64_t identity;
    struct dpp_bootstrap_params_t params;
    char channels[ESP32QJS_DPP_CHANNEL_TEXT_MAX];
    char info[ESP32QJS_DPP_INFO_MAX + 1];
    uint8_t private_key[ESP32QJS_DPP_KEY_DER_MAX];
    uint16_t private_key_length;
    bool executing;
};
static struct esp32qjs_dpp_bootstrap_job *s_dpp_bootstrap_job;
static uint64_t s_dpp_bootstrap_last_identity;
_Static_assert(sizeof(uintptr_t) == 4, "review DPP eloop ticket encoding");
extern int esp32qjs_dpp_bootstrap_generate(struct dpp_global *, const char *,
    const u8 *, const u8 *, size_t, const char *);
static void esp_dpp_bootstrap_gen(void *, void *);

static void *esp32qjs_dpp_ticket_high(uint64_t identity)
{ return (void *)(uintptr_t)(identity >> 32); }
static void *esp32qjs_dpp_ticket_low(uint64_t identity)
{ return (void *)(uintptr_t)(uint32_t)identity; }

static esp_err_t esp32qjs_dpp_bootstrap_parse(const char *channel_list,
    esp_supp_dpp_bootstrap_t type, const char *key, const char *info,
    struct esp32qjs_dpp_bootstrap_job *job)
{
    if (type != DPP_BOOTSTRAP_QR_CODE) return ESP_ERR_NOT_SUPPORTED;
    if (!channel_list || !*channel_list ||
        os_strnlen(channel_list, ESP32QJS_DPP_CHANNEL_LIST_MAX) >= ESP32QJS_DPP_CHANNEL_LIST_MAX)
        return ESP_ERR_DPP_INVALID_LIST;
    size_t used = 0;
    const char *p = channel_list;
    while (*p) {
        unsigned channel = 0, digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 3) return ESP_ERR_DPP_INVALID_LIST;
            channel = channel * 10 + (*p++ - '0');
        }
        if (!digits || !channel || channel > 255 || (*p && *p != ',') ||
            job->params.num_chan >= ESP_DPP_MAX_CHAN_COUNT || !CHANNEL_TO_BIT_NUMBER(channel))
            return ESP_ERR_DPP_INVALID_LIST;
        u8 op_class = get_operating_class(channel, 0);
        if (!op_class) return ESP_ERR_DPP_INVALID_LIST;
        for (unsigned i = 0; i < job->params.num_chan; ++i)
            if (job->params.chan_list[i] == channel) return ESP_ERR_DPP_INVALID_LIST;
        int n = os_snprintf(job->channels + used, sizeof(job->channels) - used,
            "%s%u/%u", used ? "," : "", op_class, channel);
        if (n < 0 || (size_t)n >= sizeof(job->channels) - used) return ESP_ERR_DPP_INVALID_LIST;
        used += n;
        job->params.chan_list[job->params.num_chan++] = channel;
        if (*p && !*++p) return ESP_ERR_DPP_INVALID_LIST;
    }
    if (key) {
        /* The reviewed SDK parser consumes hexadecimal DER, despite the
         * public header's raw-key wording. Its example wraps the P-256 scalar
         * in SEC1 DER. Preserve that actual ABI; crypto validates the key. */
        size_t length = os_strnlen(key, 2U * sizeof(job->private_key) + 1U);
        if (!length || (length & 1U) || length > 2U * sizeof(job->private_key) ||
            hexstr2bin(key, job->private_key, length / 2U) < 0)
            return ESP_ERR_INVALID_ARG;
        job->private_key_length = length / 2U;
    }
    if (info) {
        size_t length = os_strnlen(info, sizeof(job->info));
        if (length >= sizeof(job->info)) return ESP_ERR_INVALID_ARG;
        for (size_t i = 0; i < length; ++i)
            if ((unsigned char)info[i] < 0x20 || (unsigned char)info[i] > 0x7e || info[i] == ';')
                return ESP_ERR_INVALID_ARG;
        os_memcpy(job->info, info, length);
    }
    job->params.type = type;
    return ESP_OK;
}

esp_err_t esp32qjs_dpp_bootstrap_validate(const char *channels, const char *key, const char *info)
{
    struct esp32qjs_dpp_bootstrap_job input = {0};
    esp_err_t error = esp32qjs_dpp_bootstrap_parse(channels, DPP_BOOTSTRAP_QR_CODE, key, info, &input);
    forced_memzero(&input, sizeof(input));
    return error;
}

/* Wi-Fi/eloop task, under the DPP API mutex. eloop_run executes one callback
 * at a time; cancel removes the queued numeric ticket before job storage dies. */
static esp_err_t esp32qjs_dpp_bootstrap_cancel_locked(void)
{
    struct esp32qjs_dpp_bootstrap_job *job = s_dpp_bootstrap_job;
    if (!job) return ESP_OK;
    if (job->executing) return ESP_ERR_NOT_FINISHED;
    int cancelled = eloop_cancel_timeout(esp_dpp_bootstrap_gen,
        esp32qjs_dpp_ticket_high(job->identity), esp32qjs_dpp_ticket_low(job->identity));
    if (cancelled != 1) return ESP_ERR_INVALID_STATE;
    s_dpp_bootstrap_job = NULL;
    bin_clear_free(job, sizeof(*job));
    return ESP_OK;
}

static void esp32qjs_dpp_remove_bootstrap(int id)
{
    if (id < 0 || !s_dpp_ctx.dpp_global) return;
    char id_text[16];
    os_snprintf(id_text, sizeof(id_text), "%u", (unsigned)id);
    dpp_bootstrap_remove(s_dpp_ctx.dpp_global, id_text);
}

static void esp_dpp_bootstrap_gen(void *high, void *low)
{
    uint64_t identity = ((uint64_t)(uintptr_t)high << 32) | (uint32_t)(uintptr_t)low;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return; /* Retain job; a missing lock is not retirement. */
    struct esp32qjs_dpp_bootstrap_job *job = s_dpp_bootstrap_job;
    if (!identity || !job || job->identity != identity || job->executing) {
        dpp_api_unlock(); return;
    }
    job->executing = true;
    wifi_event_dpp_uri_ready_t *event = NULL;
    int new_id = -1;
    if (!atomic_load(&s_dpp_init_done) || atomic_load(&dpp_shutting_down) ||
        s_dpp_ctx.dpp_deinit_pending || !s_dpp_ctx.dpp_global) {
        error = ESP_ERR_INVALID_STATE; goto out;
    }
    new_id = esp32qjs_dpp_bootstrap_generate(s_dpp_ctx.dpp_global, job->channels,
        job->params.mac, job->private_key_length ? job->private_key : NULL,
        job->private_key_length, job->info[0] ? job->info : NULL);
    if (new_id < 0) { error = ESP_ERR_DPP_FAILURE; goto out; }
    const char *uri = dpp_bootstrap_get_uri(s_dpp_ctx.dpp_global, new_id);
    if (!uri) { error = ESP_ERR_DPP_FAILURE; goto out; }
    size_t length = os_strlen(uri);
    if (length > UINT32_MAX - sizeof(*event) - 1U || length > SIZE_MAX - sizeof(*event) - 1U) {
        error = ESP_ERR_INVALID_SIZE; goto out;
    }
    error = esp32qjs_dpp_result_uri_locked(uri, length);
    if (error != ESP_OK) goto out;
    int old_id = s_dpp_ctx.id;
    s_dpp_ctx.id = new_id; s_dpp_ctx.bootstrap_done = true;
    s_dpp_ctx.bootstrap_params = job->params;
    /* URI and native state are ready before any observation. Managed callers
     * copy/commit directly; no URI/config secret is queued for observers. */
    if (!s_dpp_result->status.retained) {
        event = os_malloc(sizeof(*event) + length + 1U);
        if (event) {
            event->uri_data_len = length + 1U;
            os_memcpy(event->uri, uri, length + 1U);
        }
        if (!event || esp_event_post(WIFI_EVENT, WIFI_EVENT_DPP_URI_READY, event,
                sizeof(*event) + length + 1U, 0) != ESP_OK)
            esp32qjs_dpp_result_count(&s_dpp_result->status.observation_drops);
    }
    esp32qjs_dpp_remove_bootstrap(old_id);
    new_id = -1;
out:
    esp32qjs_dpp_remove_bootstrap(new_id);
    os_free(event);
    s_dpp_bootstrap_job = NULL;
    bin_clear_free(job, sizeof(*job));
    bool report = error != ESP_OK && !atomic_load(&dpp_shutting_down) && !s_dpp_ctx.dpp_deinit_pending;
    dpp_api_unlock();
    if (report) dpp_post_dpp_failed_event(error);
}

esp_err_t esp_supp_dpp_bootstrap_gen(const char *channel_list, esp_supp_dpp_bootstrap_t type,
    const char *key, const char *info)
{
    struct esp32qjs_dpp_bootstrap_job input = {0};
    esp_err_t error = esp32qjs_dpp_bootstrap_parse(channel_list, type, key, info, &input);
    if (error != ESP_OK) goto done;
    error = dpp_api_lock();
    if (error != ESP_OK) goto done;
    if (!atomic_load(&s_dpp_init_done) || atomic_load(&dpp_shutting_down) ||
        s_dpp_ctx.dpp_deinit_pending || s_dpp_bootstrap_job || s_dpp_ctx.dpp_auth ||
        s_dpp_ctx.dpp_listen_ongoing || atomic_load(&roc_in_progress)) {
        error = ESP_ERR_INVALID_STATE; goto unlock;
    }
    if (s_dpp_bootstrap_last_identity == UINT64_MAX) { error = ESP_ERR_NO_MEM; goto unlock; }
    error = esp_wifi_get_mac(WIFI_IF_STA, input.params.mac);
    if (error != ESP_OK) goto unlock;
    struct esp32qjs_dpp_bootstrap_job *job = os_zalloc(sizeof(*job));
    if (!job) { error = ESP_ERR_NO_MEM; goto unlock; }
    input.identity = ++s_dpp_bootstrap_last_identity;
    *job = input;
    s_dpp_bootstrap_job = job;
    if (eloop_register_timeout(0, 0, esp_dpp_bootstrap_gen,
        esp32qjs_dpp_ticket_high(job->identity), esp32qjs_dpp_ticket_low(job->identity)) < 0) {
        s_dpp_bootstrap_job = NULL;
        bin_clear_free(job, sizeof(*job));
        error = ESP_ERR_NO_MEM;
    }
unlock:
    dpp_api_unlock();
done:
    forced_memzero(&input, sizeof(input));
    return error;
}
