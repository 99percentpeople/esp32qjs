/* SDK heap teardown. Driver TX/ROC and queued default events require a later
 * physical fence before a managed result/Radio lease may be released. */
static int esp_dpp_deinit(void *data, void *user_ctx)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    uint64_t identity = ((uint64_t)(uintptr_t)data << 32) | (uint32_t)(uintptr_t)user_ctx;
    if (s_dpp_result && s_dpp_result->status.retained && !esp32qjs_dpp_result_exact(identity)) {
        dpp_api_unlock(); return ESP_ERR_INVALID_STATE;
    }
    if (s_dpp_result && s_dpp_result->status.sdk_retired) { dpp_api_unlock(); return ESP_OK; }
    if (s_dpp_async_active) { dpp_api_unlock(); return ESP_ERR_NOT_FINISHED; }
    atomic_store(&dpp_shutting_down, true);
    s_dpp_ctx.dpp_deinit_pending = true;
    if (s_dpp_result) s_dpp_result->status.closing = true;
    error = esp32qjs_dpp_bootstrap_cancel_locked();
    if (error != ESP_OK) goto fail;
    esp_dpp_cancel_timeouts();
    error = esp32qjs_dpp_async_drain_locked();
    if (error != ESP_OK) goto fail;
    /* Requests below include the exact recorded owner fields. The ioctl shim
     * checks them against the physical record immediately before cancellation;
     * a foreign Action/ROC owner is never cancelled by DPP cleanup. */
    /* TX can be the current owner after a previously cancelled ROC. Attempt
     * both exact cancellation suffixes before demanding global quiescence. */
    esp32qjs_dpp_roc_cancel_locked();
    esp32qjs_dpp_tx_cancel_locked();
    if (s_dpp_tx_submitted || s_dpp_roc_submitted || esp32qjs_dpp_roc_held() || esp32qjs_dpp_tx_held()) {
        error = esp32_mquickjs_wifi_action_sdk_quiescent();
        if (error != ESP_OK) goto fail;
        error = esp32qjs_dpp_roc_cancel_locked();
        if (error != ESP_OK) goto fail;
        error = esp32qjs_dpp_tx_cancel_locked();
        if (error != ESP_OK) goto fail;
        s_dpp_tx_submitted = false; s_dpp_roc_submitted = false;
        s_dpp_tx_cancelled = false; s_dpp_roc_cancelled = false;
    }
    atomic_store(&roc_in_progress, false);
    s_dpp_ctx.dpp_listen_ongoing = false;
    if (s_dpp_result) s_dpp_result->status.driver_retired = true;
    dpp_stop_internal();
    atomic_store(&s_dpp_init_done, false);
    dpp_api_unlock();

    /* TX/ROC have no default event consumers. Keep the existing close fence
     * outside the API mutex while its private registration is retired. */
    if (dpp_api_lock() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (error != ESP_OK) goto fail;
    if (s_dpp_result && !s_dpp_result->status.event_fenced) {
        if (!s_dpp_fence_registered) {
            dpp_api_unlock();
            error = esp_event_handler_register(ESP32QJS_DPP_PRIVATE_EVENT, 0, esp32qjs_dpp_fence_handler, NULL);
            if (dpp_api_lock() != ESP_OK) return ESP_ERR_INVALID_STATE;
            if (error != ESP_OK) goto fail;
            s_dpp_fence_registered = true;
        }
        if (!s_dpp_fence_posted) {
            s_dpp_fence_posted = true;
            error = esp_event_post(ESP32QJS_DPP_PRIVATE_EVENT, 0,
                &s_dpp_result->status.identity, sizeof(s_dpp_result->status.identity), 0);
            if (error != ESP_OK) { s_dpp_fence_posted = false; goto fail; }
        }
        error = ESP_ERR_NOT_FINISHED;
        goto fail;
    }
    if (s_dpp_fence_registered) {
        dpp_api_unlock();
        error = esp_event_handler_unregister(ESP32QJS_DPP_PRIVATE_EVENT, 0, esp32qjs_dpp_fence_handler);
        if (dpp_api_lock() != ESP_OK) return ESP_ERR_INVALID_STATE;
        if (error != ESP_OK) goto fail;
        s_dpp_fence_registered = false; s_dpp_fence_posted = false;
    }
    if (s_dpp_ctx.dpp_global) {
        dpp_global_deinit(s_dpp_ctx.dpp_global);
        s_dpp_ctx.dpp_global = NULL;
    }
    if (s_dpp_ctx.dpp_config_store) {
        dpp_config_store_deinit(s_dpp_ctx.dpp_config_store);
        s_dpp_ctx.dpp_config_store = NULL;
        esp_wifi_sta_notify_dpp_config_set_internal(false);
    }
    os_bzero(&s_dpp_ctx, sizeof(s_dpp_ctx));
    if (s_dpp_result) s_dpp_result->status.cleanup_error = ESP_OK;
    esp32qjs_dpp_result_detach_locked();
    dpp_api_unlock();
    return ESP_OK;
fail:
    if (s_dpp_result) s_dpp_result->status.cleanup_error = error;
    dpp_api_unlock();
    return error;
}
