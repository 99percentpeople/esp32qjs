/* One pairing operation owns the deferred follow-up. Cancel either removes
 * its exact timer or retains the allocation until the dispatched callback
 * observes the revoked numeric identity. No new owner replaces this pointer. */
static struct nan_pairing_fup_ctx *s_esp32qjs_pairing_pending;
static void nan_app_send_pairing_followup_eloop(void *eloop_data, void *user_data);

static void esp32qjs_pairing_context_free(struct nan_pairing_fup_ctx *ctx)
{
    if (!ctx) return;
    forced_memzero(ctx, sizeof(*ctx) + ctx->shared_key_attr_len);
    esp32_mquickjs_memory_payload_free(ctx);
}

bool esp32_mquickjs_wifi_nan_pairing_followup_pending(void)
{ return s_esp32qjs_pairing_pending != NULL; } /* Wi-Fi task only. */

static int esp32qjs_pairing_schedule(struct nan_pairing_fup_ctx *ctx)
{
    if (!ctx || s_esp32qjs_pairing_pending) return -1;
    ctx->framework_identity = esp32_mquickjs_wifi_nan_pasn_identity(ctx->peer_mac);
    if (!ctx->framework_identity) return -1;
    s_esp32qjs_pairing_pending = ctx;
    if (eloop_register_timeout(0, 0, nan_app_send_pairing_followup_eloop, NULL, ctx)) {
        s_esp32qjs_pairing_pending = NULL;
        esp32_mquickjs_wifi_nan_pasn_result(ctx->peer_mac, ESP_ERR_NO_MEM, false);
        return -1;
    }
    return 0;
}

esp_err_t esp32_mquickjs_wifi_nan_pairing_cancel_pending(void)
{
    extern bool current_task_is_wifi_task(void);
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    struct nan_pairing_fup_ctx *ctx = s_esp32qjs_pairing_pending;
    if (ctx) {
        if (eloop_cancel_timeout(nan_app_send_pairing_followup_eloop, NULL, ctx) == 0)
            return ESP_ERR_TIMEOUT;
        s_esp32qjs_pairing_pending = NULL;
        esp32qjs_pairing_context_free(ctx);
    }
    if (s_nan_data_lock) {
        NAN_DATA_LOCK();
        for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i)
            nan_pairing_cancel_svc_pending(&s_nan_ctx.own_svc[i]);
        NAN_DATA_UNLOCK();
    }
    return ESP_OK;
}

static void esp32qjs_pairing_post(wifi_event_nan_pairing_complete_t *event)
{
    if (!event || !esp32_mquickjs_wifi_nan_pasn_identity(event->peer_nmi)) return;
    esp_err_t error = event->status == WIFI_NAN_PAIRING_STATUS_ACCEPTED && !event->reason_code ? ESP_OK : ESP_FAIL;
    esp32_mquickjs_wifi_nan_pasn_result(event->peer_nmi, error, error == ESP_OK);
    nan_app_post_event(WIFI_EVENT_NAN_PAIRING_CONFIRM, event, sizeof(*event));
}
