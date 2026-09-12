/* Radio serializes mutations; this additional gate also protects the SDK's
 * own STA_STOP callback and native callers during partial startup/cleanup. */
esp_err_t esp_nan_usd_deinit(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_esp32qjs_usd_lifecycle_busy, &expected, true))
        return ESP_ERR_INVALID_STATE;
    esp_err_t error = ESP_OK;
    if (!s_nan_usd_data_lock) goto done;
    NAN_USD_DATA_LOCK();
    s_esp32qjs_usd_closing = true;
    NAN_USD_DATA_UNLOCK();
    error = esp32_mquickjs_wifi_nan_usd_sdk_poll(true);
    if (error != ESP_OK) goto done;
    /* Never unregister while holding the engine lock: a default-loop callback
     * may already be waiting on it. Successful prefixes are not repeated. */
    const int32_t events[] = {WIFI_EVENT_STA_STOP};
    for (unsigned i = 0; i < 1; ++i) {
        NAN_USD_DATA_LOCK();
        bool registered = (s_esp32qjs_usd_handlers & (1U << i)) != 0;
        NAN_USD_DATA_UNLOCK();
        if (!registered) continue;
        error = esp_event_handler_unregister(WIFI_EVENT, events[i], nan_sta_stop_handler);
        if (error != ESP_OK) goto done;
        NAN_USD_DATA_LOCK();
        s_esp32qjs_usd_handlers &= ~(1U << i);
        NAN_USD_DATA_UNLOCK();
    }
    NAN_USD_DATA_LOCK();
    error = esp32qjs_nan_usd_transport_free();
    if (error) { NAN_USD_DATA_UNLOCK(); goto done; }
    /* Entered timers have returned before this lock is acquired. Already
     * dispatched timers keep only a revoked numeric identity and cannot enter. */
    if (g_nan_de) {
        nan_de_deinit(g_nan_de);
        g_nan_de = NULL;
    }
    s_esp32qjs_usd_identity = 0;
    esp32qjs_nan_usd_control(ESP32_MQUICKJS_NAN_SDK_STOPPED, 0, ESP_OK, 0);
    NAN_USD_DATA_UNLOCK();
done:
    if (error && s_nan_usd_data_lock) {
        NAN_USD_DATA_LOCK(); esp32qjs_nan_usd_record_error(error); NAN_USD_DATA_UNLOCK();
    }
    atomic_store(&s_esp32qjs_usd_lifecycle_busy, false);
    return error;
}

esp_err_t esp_nan_usd_init(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_esp32qjs_usd_lifecycle_busy, &expected, true))
        return ESP_ERR_INVALID_STATE;
    esp_err_t error = ESP_OK;
    if (!s_nan_usd_data_lock) {
        s_nan_usd_data_lock = os_recursive_mutex_create();
        if (!s_nan_usd_data_lock) { error = ESP_ERR_NO_MEM; goto done; }
    }
    NAN_USD_DATA_LOCK();
    if (g_nan_de || s_esp32qjs_usd_handlers || s_esp32qjs_usd_identity) {
        NAN_USD_DATA_UNLOCK(); error = ESP_ERR_INVALID_STATE; goto done;
    }
    if (!s_esp32qjs_usd_next_identity) {
        NAN_USD_DATA_UNLOCK(); error = ESP_ERR_NO_MEM; goto done;
    }
    s_esp32qjs_usd_identity = s_esp32qjs_usd_next_identity++;
    s_esp32qjs_usd_closing = true;
    s_esp32qjs_usd_error = ESP_OK;
    s_esp32qjs_usd_drops = 0;
    NAN_USD_DATA_UNLOCK();
    /* The MAC getter may wait on the Wi-Fi task. That task must remain free
     * to reject a late RX callback under the engine mutex. */
    uint8_t mac[ETH_ALEN];
    error = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (error != ESP_OK) goto failed;
    struct nan_callbacks cb = {
        .tx = esp_nan_de_tx, .listen = esp_nan_de_listen,
        .discovery_result = esp_nan_de_discovery_result, .replied = esp_nan_de_replied,
        .publish_terminated = esp_nan_de_publish_terminated,
        .subscribe_terminated = esp_nan_de_subscribe_terminated, .receive = esp_nan_de_receive};
    NAN_USD_DATA_LOCK();
    error = esp32qjs_nan_usd_transport_open();
    if (!error) {
        g_nan_de = nan_de_init(mac, false, false, 1000, &cb);
        if (!g_nan_de) error = ESP_ERR_NO_MEM;
    }
    NAN_USD_DATA_UNLOCK();
    if (error) goto failed;
    const int32_t events[] = {WIFI_EVENT_STA_STOP};
    for (unsigned i = 0; i < 1; ++i) {
        error = esp_event_handler_register(WIFI_EVENT, events[i], nan_sta_stop_handler, NULL);
        if (error != ESP_OK) goto failed;
        NAN_USD_DATA_LOCK(); s_esp32qjs_usd_handlers |= 1U << i; NAN_USD_DATA_UNLOCK();
    }
    NAN_USD_DATA_LOCK();
    s_esp32qjs_usd_closing = false;
    esp32qjs_nan_usd_control(ESP32_MQUICKJS_NAN_SDK_STARTED, 0, ESP_OK, 0);
    NAN_USD_DATA_UNLOCK();
    goto done;
failed:
    NAN_USD_DATA_LOCK(); esp32qjs_nan_usd_record_error(error); NAN_USD_DATA_UNLOCK();
    /* Preserve the partial engine and exact registered suffix until the
     * caller performs checked deinit. Another init cannot overwrite them. */
done:
    atomic_store(&s_esp32qjs_usd_lifecycle_busy, false);
    return error;
}
