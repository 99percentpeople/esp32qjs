esp_err_t esp_supp_dpp_deinit(void)
{
    if (current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    esp_err_t error = dpp_api_lock();
    if (error != ESP_OK) return error;
    if (s_dpp_init_pending || s_dpp_deinit_dispatch_pending) {
        dpp_api_unlock(); return ESP_ERR_NOT_FINISHED;
    }
    if (!atomic_load(&s_dpp_init_done) && (!s_dpp_result || !s_dpp_result->status.attached)) {
        dpp_api_unlock(); return ESP_OK;
    }
    /* Managed reservation requires its exact command bridge; an old SDK
     * deinit must not take over a newly reserved framework operation. */
    if (s_dpp_result && s_dpp_result->status.retained) {
        dpp_api_unlock(); return ESP_ERR_INVALID_STATE;
    }
    s_dpp_deinit_dispatch_pending = true;
    s_dpp_ctx.dpp_deinit_pending = true;
    dpp_api_unlock();
    int ret = eloop_register_timeout_blocking(esp_dpp_deinit, NULL, NULL);
    if (dpp_api_lock() != ESP_OK) return ESP_ERR_INVALID_STATE;
    s_dpp_deinit_dispatch_pending = false;
    error = ret == -1 ? ESP_ERR_NO_MEM : ret;
    if (error != ESP_OK && s_dpp_result) s_dpp_result->status.cleanup_error = error;
    /* Keep closing admission after failure. Retrying runs only unfinished
     * unregister/free obligations; it does not restart protocol input. */
    dpp_api_unlock(); return error;
}
