/* All engine mutations that can submit Action/ROC run on the Wi-Fi task.
 * Waiting there while another thread holds the USD mutex would deadlock RX.
 * The pinned blocking eloop call retains the waiting caller until completion
 * or revocation before dispatch, so these borrowed inputs stay live. The
 * framework caller must be its native worker, never the JS/default-loop task. */
extern bool current_task_is_wifi_task(void);
typedef enum { USD_PUBLISH, USD_SUBSCRIBE, USD_UPDATE, USD_CANCEL_PUBLISH,
    USD_CANCEL_SUBSCRIBE, USD_CANCEL_SERVICE, USD_TRANSMIT, USD_SERVICE_DRAIN } esp32qjs_usd_command_kind_t;
typedef struct {
    esp32qjs_usd_command_kind_t kind;
    uint32_t identity, message_identity;
    const void *config;
    const uint8_t *ssi, *peer;
    uint16_t length;
    int service;
    uint8_t peer_service;
} esp32qjs_usd_command_t;

static int esp32qjs_nan_usd_command_run(void *opaque, void *unused)
{
    (void)unused;
    const esp32qjs_usd_command_t *c = opaque;
    int result = c->kind <= USD_SUBSCRIBE ? -1 : ESP_ERR_INVALID_STATE;
    if (!s_nan_usd_data_lock) return result;
    NAN_USD_DATA_LOCK();
    if (!g_nan_de || !c->identity || c->identity != s_esp32qjs_usd_identity ||
        s_esp32qjs_usd_closing || s_esp32qjs_usd_error) goto done;
    switch (c->kind) {
    case USD_PUBLISH: result = esp32qjs_usd_publish_native(c->config); break;
    case USD_SUBSCRIBE: result = esp32qjs_usd_subscribe_native(c->config); break;
    case USD_UPDATE: result = esp32qjs_usd_update_publish_native(c->service, (uint8_t *)c->ssi, c->length); break;
    case USD_CANCEL_PUBLISH: result = esp32qjs_usd_cancel_publish_native(c->service); break;
    case USD_CANCEL_SUBSCRIBE: result = esp32qjs_usd_cancel_subscribe_native(c->service); break;
    case USD_CANCEL_SERVICE: result = esp32qjs_usd_cancel_service_native(c->service); break;
    case USD_SERVICE_DRAIN: result = esp32qjs_nan_usd_service_drain(c->service); break;
    case USD_TRANSMIT: {
        esp32qjs_usd_transport_t *t = s_esp32qjs_usd_transport;
        if (!t || (c->message_identity && t->message.identity)) break;
        if (c->message_identity) {
            t->message = (esp32_mquickjs_wifi_nan_message_tx_status_t){
                .identity = c->message_identity, .entered = true, .buffer_retired = true};
            t->message_scope = true;
        }
        result = esp32qjs_usd_transmit_native(c->service, c->ssi, c->length, c->peer, c->peer_service);
        t->message_scope = false;
        if (result && t->error) result = t->error;
        break;
    }
    }
    if (c->kind <= USD_SUBSCRIBE && result > 0)
        esp32qjs_nan_usd_control(ESP32_MQUICKJS_NAN_SDK_SERVICE_BOUND,
            (uint8_t)result, ESP_OK, c->kind == USD_PUBLISH ? 2 : 1);
done:
    NAN_USD_DATA_UNLOCK();
    return result;
}

static int esp32qjs_nan_usd_command(esp32qjs_usd_command_t *command)
{
    esp32_mquickjs_wifi_nan_usd_sdk_status_t status;
    esp32_mquickjs_wifi_nan_usd_sdk_status(&status);
    if (!status.initialized || status.closing || status.error)
        return command->kind <= USD_SUBSCRIBE ? -1 : ESP_ERR_INVALID_STATE;
    command->identity = status.identity;
    if (current_task_is_wifi_task()) return esp32qjs_nan_usd_command_run(command, NULL);
    return eloop_register_timeout_blocking(esp32qjs_nan_usd_command_run, command, NULL);
}

static bool esp32qjs_usd_config_valid(const wifi_nan_usd_config_t *config)
{
    if (config->usd_default_channel && esp_nan_chan_to_freq(config->usd_default_channel) < 0) return false;
    if (config->usd_chan_bitmap.ghz_2_channels & ~UINT32_C(0x7ffe)) return false;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    if (config->usd_chan_bitmap.ghz_5_channels & ~UINT32_C(0x1ffffffe)) return false;
#else
    if (config->usd_chan_bitmap.ghz_5_channels || config->usd_default_channel > 14) return false;
#endif
    unsigned n_min = config->n_min ? config->n_min : 5, n_max = config->n_max ? config->n_max : 10;
    unsigned m_min = config->m_min ? config->m_min : 5, m_max = config->m_max ? config->m_max : 10;
    return n_min <= n_max && m_min <= m_max;
}

int esp_nan_usd_publish(const wifi_nan_publish_cfg_t *config)
{
    if (!config || !config->service_name[0] || !memchr(config->service_name, 0, sizeof(config->service_name)) ||
        !esp32qjs_usd_config_valid(&config->usd_publish_config) || config->ssi_len > ESP_WIFI_MAX_SVC_SSI_LEN ||
        (config->ssi_len && !config->ssi) || config->ttl > INT32_MAX ||
        (config->type != NAN_PUBLISH_UNSOLICITED && config->type != NAN_PUBLISH_SOLICITED)) return -1;
    esp32qjs_usd_command_t command = {.kind = USD_PUBLISH, .config = config};
    return esp32qjs_nan_usd_command(&command);
}

int esp_nan_usd_subscribe(const wifi_nan_subscribe_cfg_t *config)
{
    if (!config || !config->service_name[0] || !memchr(config->service_name, 0, sizeof(config->service_name)) ||
        !esp32qjs_usd_config_valid(&config->usd_subscribe_config) || config->ssi_len > ESP_WIFI_MAX_SVC_SSI_LEN ||
        (config->ssi_len && !config->ssi) || config->ttl > INT32_MAX ||
        (config->type != NAN_SUBSCRIBE_ACTIVE && config->type != NAN_SUBSCRIBE_PASSIVE)) return -1;
    esp32qjs_usd_command_t command = {.kind = USD_SUBSCRIBE, .config = config};
    return esp32qjs_nan_usd_command(&command);
}

esp_err_t esp_nan_usd_update_publish(int id, uint8_t *ssi, uint16_t length)
{
    if (id <= 0 || length > ESP_WIFI_MAX_SVC_SSI_LEN || (length && !ssi)) return ESP_ERR_INVALID_ARG;
    esp32qjs_usd_command_t command = {.kind = USD_UPDATE, .service = id, .ssi = ssi, .length = length};
    return esp32qjs_nan_usd_command(&command);
}

esp_err_t esp_nan_usd_cancel_publish(int id)
{
    if (id <= 0) return ESP_ERR_INVALID_ARG;
    esp32qjs_usd_command_t command = {.kind = USD_CANCEL_PUBLISH, .service = id};
    return esp32qjs_nan_usd_command(&command);
}

esp_err_t esp_nan_usd_cancel_subscribe(int id)
{
    if (id <= 0) return ESP_ERR_INVALID_ARG;
    esp32qjs_usd_command_t command = {.kind = USD_CANCEL_SUBSCRIBE, .service = id};
    return esp32qjs_nan_usd_command(&command);
}

esp_err_t esp_nan_usd_cancel_service(int id)
{
    if (id <= 0) return ESP_ERR_INVALID_ARG;
    esp32qjs_usd_command_t command = {.kind = USD_CANCEL_SERVICE, .service = id};
    return esp32qjs_nan_usd_command(&command);
}

esp_err_t esp_nan_usd_transmit(int id, const uint8_t *ssi, uint16_t length, const uint8_t *peer, uint8_t peer_id)
{
    if (id <= 0 || !peer_id || !peer || (peer[0] & 1) ||
        !memcmp(peer, (uint8_t[6]){0}, 6) || length > ESP_WIFI_MAX_FUP_SSI_LEN || (length && !ssi))
        return ESP_ERR_INVALID_ARG;
    esp32qjs_usd_command_t command = {.kind = USD_TRANSMIT, .service = id,
        .ssi = ssi, .length = length, .peer = peer, .peer_service = peer_id};
    return esp32qjs_nan_usd_command(&command);
}

esp_err_t esp32_mquickjs_wifi_nan_usd_sdk_send(const wifi_nan_followup_params_t *params, uint32_t identity)
{
    if (!params || !identity || !params->inst_id || !params->peer_inst_id ||
        (params->peer_mac[0] & 1) || !memcmp(params->peer_mac, (uint8_t[6]){0}, 6) ||
        params->ssi_len > ESP_WIFI_MAX_FUP_SSI_LEN || (params->ssi_len && !params->ssi))
        return ESP_ERR_INVALID_ARG;
    if (params->vendor_ie) return ESP_ERR_NOT_SUPPORTED;
    esp32qjs_usd_command_t command = {.kind = USD_TRANSMIT, .message_identity = identity,
        .service = params->inst_id, .ssi = params->ssi, .length = params->ssi_len,
        .peer = params->peer_mac, .peer_service = params->peer_inst_id};
    return esp32qjs_nan_usd_command(&command);
}

esp_err_t esp32_mquickjs_wifi_nan_usd_service_start(const wifi_nan_publish_cfg_t *publish,
    const wifi_nan_subscribe_cfg_t *subscribe, uint8_t *id)
{
    if ((!publish == !subscribe) || !id || *id) return ESP_ERR_INVALID_ARG;
    int result = publish ? esp_nan_usd_publish(publish) : esp_nan_usd_subscribe(subscribe);
    if (result <= 0 || result > UINT8_MAX) return ESP_FAIL;
    *id = result;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_nan_usd_service_close(uint8_t id, bool *cancelled)
{
    if (!id || !cancelled) return ESP_ERR_INVALID_ARG;
    if (!*cancelled) {
        esp_err_t error = esp_nan_usd_cancel_service(id);
        if (error) return error;
        *cancelled = true;
    }
    esp32qjs_usd_command_t command = {.kind = USD_SERVICE_DRAIN, .service = id};
    return esp32qjs_nan_usd_command(&command);
}
