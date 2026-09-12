/* Included in the hash-pinned build-local esp_nan_usd.c. The SDK mutex stays
 * boot-scoped: a queued timer/RX must never acquire a deleted/reused mutex. */
static atomic_bool s_esp32qjs_usd_lifecycle_busy;
static uint32_t s_esp32qjs_usd_identity, s_esp32qjs_usd_next_identity = 1;
static uint32_t s_esp32qjs_usd_handlers, s_esp32qjs_usd_drops;
static esp_err_t s_esp32qjs_usd_error;
static bool s_esp32qjs_usd_closing = true;
static void esp32qjs_nan_usd_transport_snapshot(esp32_mquickjs_wifi_nan_usd_sdk_status_t *status);

static void esp32qjs_nan_usd_record_error(esp_err_t error)
{
    if (!s_esp32qjs_usd_error) s_esp32qjs_usd_error = error;
}

void esp32_mquickjs_wifi_nan_usd_sdk_status(esp32_mquickjs_wifi_nan_usd_sdk_status_t *status)
{
    if (!status) return;
    *status = (esp32_mquickjs_wifi_nan_usd_sdk_status_t){.closing = true};
    if (!s_nan_usd_data_lock) return;
    NAN_USD_DATA_LOCK();
    *status = (esp32_mquickjs_wifi_nan_usd_sdk_status_t){
        .identity = s_esp32qjs_usd_identity, .registered_handlers = s_esp32qjs_usd_handlers,
        .dropped_events = s_esp32qjs_usd_drops, .error = s_esp32qjs_usd_error,
        .initialized = g_nan_de != NULL, .closing = s_esp32qjs_usd_closing};
    esp32qjs_nan_usd_transport_snapshot(status);
    NAN_USD_DATA_UNLOCK();
}

bool esp32qjs_nan_usd_timer_enter(struct nan_de *de, uint32_t identity)
{
    if (!identity || !s_nan_usd_data_lock) return false;
    NAN_USD_DATA_LOCK();
    if (s_esp32qjs_usd_closing || s_esp32qjs_usd_error ||
        identity != s_esp32qjs_usd_identity || !g_nan_de || de != g_nan_de) {
        NAN_USD_DATA_UNLOCK();
        return false;
    }
    return true;
}

void esp32qjs_nan_usd_timer_leave(void) { NAN_USD_DATA_UNLOCK(); }

uint32_t esp32qjs_nan_usd_timer_identity(struct nan_de *de)
{
    if (!s_nan_usd_data_lock) return 0;
    NAN_USD_DATA_LOCK();
    uint32_t identity = de && de == g_nan_de ? s_esp32qjs_usd_identity : 0;
    NAN_USD_DATA_UNLOCK();
    return identity;
}

void esp32qjs_nan_usd_timer_failed(struct nan_de *de)
{
    if (!s_nan_usd_data_lock) return;
    NAN_USD_DATA_LOCK();
    if (de && de == g_nan_de) {
        esp32qjs_nan_usd_record_error(ESP_ERR_NO_MEM);
        s_esp32qjs_usd_closing = true;
    }
    NAN_USD_DATA_UNLOCK();
}

/* Observation overflow never blocks the engine while it holds its mutex.
 * Called by serialized engine callbacks, not by arbitrary producers. */
static void esp32qjs_nan_usd_post(int32_t event, const void *data, size_t size)
{
    esp32_mquickjs_wifi_nan_sdk_notice_t notice = {.kind = ESP32_MQUICKJS_NAN_SDK_EVENT,
        .event_id = event, .data = data, .size = size};
    esp32_mquickjs_wifi_nan_sdk_notice(&notice);
    if (esp_event_post(WIFI_EVENT, event, data, size, 0) != ESP_OK && s_esp32qjs_usd_drops != UINT32_MAX)
        ++s_esp32qjs_usd_drops;
}

static void esp32qjs_nan_usd_control(esp32_mquickjs_wifi_nan_sdk_notice_kind_t kind,
    uint8_t service, int32_t status, uint32_t context)
{
    esp32_mquickjs_wifi_nan_sdk_notice_t notice = {.kind = kind,
        .service_id = service, .status = status, .context = context};
    esp32_mquickjs_wifi_nan_sdk_notice(&notice);
}
