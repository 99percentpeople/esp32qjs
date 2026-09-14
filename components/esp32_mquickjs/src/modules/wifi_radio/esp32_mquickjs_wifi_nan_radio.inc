#include "esp32_mquickjs_wifi_netif.h"
#include "esp32_mquickjs_wifi_nan_tx.h"
#include "esp_netif.h"
#include "esp_wifi_default.h"
#include "esp_nan.h"
#include "esp32_mquickjs_wifi_nan_usd_sdk.h"
#include "esp32_mquickjs_wifi_nan_pasn_sdk.h"

/* Pinned wifi_default.c exports this checked attach path but omits its
 * declaration from esp_wifi_default.h. Avoid the assert-based default factory. */
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
extern esp_err_t esp_netif_attach_wifi_nan(esp_netif_t *netif);
#endif

_Static_assert(WIFI_MODE_NAN == 4, "NAN lifecycle event mask requires bit 2");

/* All fields belong to the Radio mutation mutex. SDK observers never reference
 * this binding. The caller retains its own observer storage until close. */
struct wifi_radio_nan {
    esp32_mquickjs_wifi_radio_lease_t lease;
    esp32_mquickjs_wifi_nan_radio_status_t status;
    esp_netif_t *netif;
    uint32_t observer;
    esp_err_t detach_error, mode_restore_error, storage_restore_error;
    wifi_mode_t saved_mode;
    wifi_storage_t saved_storage;
    bool saved, storage_attempted, prepare_attempted, mode_restore_attempted;
    bool storage_restore_attempted, stop_fenced, tx_owned;
#if CONFIG_ESP_WIFI_NAN_PAIRING
    bool pairing_retired;
#endif
};

static bool wifi_radio_nan_exact_locked(const esp32_mquickjs_wifi_radio_operation_t *token)
{
    return token && token->identity && s_nan_radio &&
        token->kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN &&
        token->kind == s_radio.operation.kind && token->identity == s_radio.operation.identity &&
        token->generation == s_radio.generation && token->generation == s_radio.operation.generation &&
        token->lease_identity == s_radio.operation.lease_identity &&
        token->lease_identity == s_nan_radio->lease.identity && wifi_radio_lease_valid(&s_nan_radio->lease);
}

static esp_err_t wifi_radio_nan_snapshot_locked(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_nan_radio_status_t *status)
{
    if (!wifi_radio_nan_exact_locked(token)) return ESP_ERR_INVALID_STATE;
    *status = s_nan_radio->status;
    return ESP_OK;
}

#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
esp_err_t esp32_mquickjs_wifi_radio_nan_query(const esp32_mquickjs_wifi_radio_operation_t *token,
    const esp32_mquickjs_wifi_nan_query_t *query, esp32_mquickjs_wifi_nan_query_result_t *out)
{
    if (!query || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_nan_exact_locked(token) && s_nan_radio->status.ready && !s_nan_radio->status.closing) {
        error = s_nan_radio->status.usd ? ESP_ERR_NOT_SUPPORTED :
            esp32_mquickjs_wifi_nan_sdk_query(query, out);
    }
    wifi_radio_operation_unlock();
    return error;
}
#endif

#if CONFIG_ESP_WIFI_NAN_PAIRING
esp_err_t esp32_mquickjs_wifi_radio_nan_bootstrap_send(const esp32_mquickjs_wifi_radio_operation_t *token,
    wifi_nan_followup_params_t *params, uint32_t identity, uint32_t *context, bool response, bool accept)
{
    if (!params || !identity || !context || *context) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_nan_exact_locked(token) && s_nan_radio->status.ready &&
        !s_nan_radio->status.closing && !s_nan_radio->status.usd) {
        error = esp32_mquickjs_wifi_nan_tx_message_begin(identity, context);
        if (!error) error = esp32_mquickjs_wifi_nan_sdk_bootstrap_send(params, context, response, accept);
    }
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_credentials(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint8_t service_id, esp32_mquickjs_wifi_nan_credentials_t *out)
{
    if (!service_id || !out) return ESP_ERR_INVALID_ARG;
    *out = (esp32_mquickjs_wifi_nan_credentials_t){0};
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_nan_exact_locked(token) && s_nan_radio->status.ready &&
        !s_nan_radio->status.closing && !s_nan_radio->status.usd)
        error = esp32_mquickjs_wifi_nan_pairing_credentials(service_id, out);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_commit(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint32_t identity, uint32_t *credential_id)
{
    if (!identity || !credential_id) return ESP_ERR_INVALID_ARG;
    *credential_id = 0;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_nan_exact_locked(token) && s_nan_radio->status.ready &&
        !s_nan_radio->status.closing && !s_nan_radio->status.usd)
        error = esp32_mquickjs_wifi_nan_pairing_commit(identity, credential_id);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_start(const esp32_mquickjs_wifi_radio_operation_t *token,
    const esp32_mquickjs_wifi_nan_pairing_config_t *config, esp32_mquickjs_wifi_nan_pasn_status_t *status)
{
    if (!config || !status) return ESP_ERR_INVALID_ARG;
    *status = (esp32_mquickjs_wifi_nan_pasn_status_t){0};
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_nan_exact_locked(token) && s_nan_radio->status.ready &&
        !s_nan_radio->status.closing && !s_nan_radio->status.usd)
        error = esp32_mquickjs_wifi_nan_pairing_start(config, status);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_poll(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint32_t identity, bool close, esp32_mquickjs_wifi_nan_pasn_status_t *status)
{
    if (!identity || !status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_nan_exact_locked(token) || s_nan_radio->status.usd) goto done;
    esp32_mquickjs_wifi_nan_pasn_status_t snapshot;
    error = esp32_mquickjs_wifi_nan_pasn_status(&snapshot);
    if (error) goto done;
    if (snapshot.identity != identity) { error = ESP_ERR_INVALID_STATE; goto done; }
    *status = snapshot;
    if (close) {
        error = esp32_mquickjs_wifi_nan_pasn_close(identity);
        esp32_mquickjs_wifi_nan_pasn_status_t after;
        esp_err_t observed = esp32_mquickjs_wifi_nan_pasn_status(&after);
        if (!observed && after.identity == identity) *status = after;
        else if (!error) error = observed ? observed : ESP_ERR_INVALID_STATE;
    }
done:
    wifi_radio_operation_unlock();
    return error;
}
#endif

esp_err_t esp32_mquickjs_wifi_radio_nan_service_start(const esp32_mquickjs_wifi_radio_operation_t *token,
    const wifi_nan_publish_cfg_t *publish, const wifi_nan_subscribe_cfg_t *subscribe, uint8_t *id)
{
    if ((!publish == !subscribe) || !id || *id) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_nan_exact_locked(token) && s_nan_radio->status.ready && !s_nan_radio->status.closing) {
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
        if (s_nan_radio->status.usd)
            error = esp32_mquickjs_wifi_nan_usd_service_start(publish, subscribe, id);
        else
#endif
        {
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
            error = publish ? esp32_mquickjs_wifi_nan_sdk_publish(publish, id) :
                esp32_mquickjs_wifi_nan_sdk_subscribe(subscribe, id);
#else
            error = ESP_ERR_NOT_SUPPORTED;
#endif
        }
    }
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_nan_service_close(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint8_t id, bool *cancelled)
{
    if (!id || !cancelled) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!wifi_radio_nan_exact_locked(token) || s_nan_radio->status.closing) goto done;
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    if (s_nan_radio->status.usd) {
        error = esp32_mquickjs_wifi_nan_usd_service_close(id, cancelled);
        goto done;
    }
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (!*cancelled) {
        error = esp32_mquickjs_wifi_nan_sdk_cancel_service(id);
        if (error != ESP_OK) goto done;
        *cancelled = true;
    }
    error = ESP_ERR_TIMEOUT;
    if (esp32_mquickjs_wifi_nan_tx_service_drained(id))
        error = esp32_mquickjs_wifi_nan_sdk_service_resume(id);
#else
    error = ESP_ERR_NOT_SUPPORTED;
#endif
done:
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_nan_message_send(const esp32_mquickjs_wifi_radio_operation_t *token,
    wifi_nan_followup_params_t *params, uint32_t identity, uint32_t *context)
{
    if (!params || !identity || !context || *context) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_nan_exact_locked(token) && s_nan_radio->status.ready && !s_nan_radio->status.closing) {
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
        if (s_nan_radio->status.usd) error = esp32_mquickjs_wifi_nan_usd_sdk_send(params, identity);
        else {
#endif
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
        error = esp32_mquickjs_wifi_nan_tx_message_begin(identity, context);
        if (!error) error = esp32_mquickjs_wifi_nan_sdk_send(params, context);
#else
        error = ESP_ERR_NOT_SUPPORTED;
#endif
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
        }
#endif
    }
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_nan_poll(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_nan_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = wifi_radio_nan_snapshot_locked(token, status);
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    if (!error && s_nan_radio->status.usd && !s_nan_radio->status.closing) {
        error = esp32_mquickjs_wifi_nan_usd_sdk_poll(false);
        if (error == ESP_ERR_TIMEOUT || error == ESP_ERR_NOT_FINISHED) error = ESP_OK;
        esp32_mquickjs_wifi_nan_usd_sdk_status_t usd;
        esp32_mquickjs_wifi_nan_usd_sdk_status(&usd);
        s_nan_radio->status.reserved_bytes = sizeof(*s_nan_radio) + usd.reserved_bytes;
        if (!error) error = usd.error;
        if (!error && (!usd.initialized || usd.closing)) error = ESP_ERR_INVALID_STATE;
        if (error) {
            s_nan_radio->status.error = error;
            s_nan_radio->status.stage = "nan-usd-transport";
            s_nan_radio->status.closing = true;
            s_nan_radio->status.ready = false;
            (void)wifi_radio_record_fault(s_nan_radio->status.stage, error);
        }
        *status = s_nan_radio->status;
    }
#endif
    wifi_radio_operation_unlock();
    return error;
}

#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_request(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint8_t service_id, wifi_nan_datapath_req_t *request, uint32_t *identity)
{
    if (!service_id || !request || !identity || *identity) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_nan_exact_locked(token) && s_nan_radio->status.ready && !s_nan_radio->status.closing)
        error = esp32_mquickjs_wifi_nan_sdk_ndp_request(service_id, request, identity);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_response(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint32_t identity, bool accept, const uint8_t *ssi, uint16_t length)
{
    if (!identity || (length && !ssi)) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_nan_exact_locked(token) && s_nan_radio->status.ready && !s_nan_radio->status.closing)
        error = esp32_mquickjs_wifi_nan_sdk_ndp_response(identity, accept, ssi, length);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_end(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint32_t identity)
{
    if (!identity) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_nan_exact_locked(token) && s_nan_radio->status.ready && !s_nan_radio->status.closing)
        error = esp32_mquickjs_wifi_nan_sdk_ndp_end(identity);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_nan_ndp_release(const esp32_mquickjs_wifi_radio_operation_t *token,
    uint32_t identity, esp32_mquickjs_wifi_nan_ndp_status_t *status)
{
    if (!identity || !status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_nan_exact_locked(token) && s_nan_radio->status.ready && !s_nan_radio->status.closing)
        error = esp32_mquickjs_wifi_nan_sdk_ndp_release(identity, status);
    wifi_radio_operation_unlock();
    return error;
}

#endif

static esp_err_t wifi_radio_nan_begin(const wifi_nan_sync_config_t *config, bool usd,
    esp32_mquickjs_wifi_nan_sdk_observer_fn observer, void *opaque,
    esp32_mquickjs_wifi_radio_operation_t *token, esp32_mquickjs_wifi_nan_radio_status_t *status)
{
    if (!config || !observer || !token || token->identity || !status) return ESP_ERR_INVALID_ARG;
    *status = (esp32_mquickjs_wifi_nan_radio_status_t){.stage = "nan-admission"};
    /* Preserve the SDK uint8 channel range. The configured target's driver
     * validates its NAN channels; do not infer a 2.4 GHz-only capability here. */
    if (!config->op_channel) return status->error = ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (!usd) return status->error = ESP_ERR_NOT_SUPPORTED;
#endif
#if !CONFIG_ESP_WIFI_NAN_USD_ENABLE
    if (usd) return status->error = ESP_ERR_NOT_SUPPORTED;
#endif
#if !CONFIG_ESP_WIFI_NAN_SECURITY
    if (config->reset_current_nvs_creds || config->use_nvs_for_caching || config->group_mgmt_prot)
        return status->error = ESP_ERR_NOT_SUPPORTED;
#endif
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    wifi_radio_nan_t *binding = NULL;
    if (WIFI_RADIO_NAN_PENDING || WIFI_RADIO_DPP_PENDING || WIFI_RADIO_WPS_PENDING ||
        WIFI_RADIO_SMARTCONFIG_PENDING || WIFI_RADIO_EAP_PENDING || s_radio.operation.identity ||
        s_radio.lifecycle.identity || s_radio.wake_locks || s_radio.promiscuous_claimed ||
        s_radio.started || s_radio.stop_required || s_radio.stop_submitted || s_radio.restart_required ||
        s_radio.event_live || s_radio.event_phase != RADIO_EVENTS_IDLE || s_radio.cleanup_stage || s_radio.fault_stage ||
        (s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED &&
         s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STOPPED)) goto done;
    for (unsigned i = 0; i < WIFI_RADIO_MAX_LEASES; ++i) if (s_radio.leases[i].identity) goto done;
    if (s_interval.owner.identity || s_interval.uncertain || s_tx_rate_lease.identity) goto done;
    if (!s_radio.next_lease_identity || !s_radio.next_operation_identity) { error = ESP_ERR_NO_MEM; goto done; }
    /* Reject a foreign default interface before initialization or driver writes. */
    if (!usd && esp_netif_get_handle_from_ifkey("WIFI_NAN_DEF")) goto done;
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    esp32_mquickjs_wifi_nan_usd_sdk_status_t usd_status;
    esp32_mquickjs_wifi_nan_usd_sdk_status(&usd_status);
    if (usd_status.identity || usd_status.initialized || usd_status.registered_handlers || usd_status.transport_held) goto done;
#endif
    binding = esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, sizeof(*binding),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!binding) { error = ESP_ERR_NO_MEM; goto done; }
    s_nan_radio = binding;
    taskENTER_CRITICAL(&s_radio.lock);
    binding->lease = (esp32_mquickjs_wifi_radio_lease_t){.generation = s_radio.generation,
        .identity = s_radio.next_lease_identity++, .client = ESP32_MQUICKJS_WIFI_RADIO_CLIENT_NAN, .acquired = true};
    s_radio.leases[0] = (wifi_radio_live_lease_t){.identity = binding->lease.identity,
        .client = binding->lease.client, .required_mode = usd ? WIFI_MODE_STA : WIFI_MODE_NAN};
    ++s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_NAN];
    s_radio.operation = (esp32_mquickjs_wifi_radio_operation_t){.generation = s_radio.generation,
        .identity = s_radio.next_operation_identity++, .lease_identity = binding->lease.identity,
        .kind = ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN};
    *token = s_radio.operation;
    taskEXIT_CRITICAL(&s_radio.lock);
    binding->status.operation = *token;
    binding->status.usd = usd;
    binding->status.reserved_bytes = sizeof(*binding);
    binding->status.stage = "nan-radio-initialize";
    error = wifi_radio_initialize();
    if (error != ESP_OK) goto done;
    binding->status.stage = "nan-mode-snapshot";
    error = esp_wifi_get_mode(&binding->saved_mode);
    if (error != ESP_OK) goto done;
    if (binding->saved_mode != WIFI_MODE_NULL && binding->saved_mode != WIFI_MODE_STA &&
        binding->saved_mode != WIFI_MODE_AP && binding->saved_mode != WIFI_MODE_APSTA) {
        error = ESP_ERR_INVALID_STATE; goto done;
    }
    binding->saved_storage = s_radio.storage;
    binding->saved = true;
    binding->status.stage = "nan-observer";
    error = esp32_mquickjs_wifi_nan_sdk_observe(observer, opaque, &binding->observer);
    if (error != ESP_OK) goto done;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (!usd) {
        binding->status.stage = "nan-tx-prepare";
        error = esp32_mquickjs_wifi_nan_tx_open();
        if (error != ESP_OK) goto done;
        binding->tx_owned = true;
        esp32_mquickjs_wifi_nan_tx_status_t tx;
        esp32_mquickjs_wifi_nan_tx_status(&tx);
        binding->status.reserved_bytes += tx.reserved_bytes;
    }
    if (!usd) {
        error = esp32_mquickjs_wifi_nan_sdk_service_resume(0);
        if (error != ESP_OK) goto done;
        binding->status.stage = "nan-netif-initialize";
        error = esp_netif_init();
        if (error != ESP_OK) goto done;
        binding->status.stage = "nan-netif-allocate";
        esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_NAN();
        binding->netif = esp_netif_new(&netif_config);
        if (!binding->netif) { error = ESP_ERR_NO_MEM; goto done; }
        /* The pinned attach installs the global netif pointer even when its driver
         * allocation fails. Keep this netif for the same checked detach suffix. */
        binding->status.stage = "nan-netif-attach";
        error = esp_netif_attach_wifi_nan(binding->netif);
        if (error != ESP_OK) goto done;
        binding->status.stage = "nan-netif-handlers";
        error = esp_wifi_set_default_wifi_nan_handlers();
        if (error != ESP_OK) goto done;
    }
#endif
    if (binding->saved_storage != WIFI_STORAGE_RAM) {
        binding->status.stage = "nan-storage";
        binding->storage_attempted = true;
        error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (error != ESP_OK) {
            taskENTER_CRITICAL(&s_radio.lock); s_radio.storage = (wifi_storage_t)-1; taskEXIT_CRITICAL(&s_radio.lock);
            goto done;
        }
        taskENTER_CRITICAL(&s_radio.lock); s_radio.storage = WIFI_STORAGE_RAM; taskEXIT_CRITICAL(&s_radio.lock);
    }
    binding->status.stage = "nan-prepare";
    binding->prepare_attempted = true;
    error = ESP_ERR_NOT_SUPPORTED;
    if (usd) error = esp_wifi_set_mode(WIFI_MODE_STA);
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    else error = esp32_mquickjs_wifi_nan_sdk_sync_prepare(config);
#endif
    if (error != ESP_OK) goto done;
    wifi_mode_t mode = usd ? WIFI_MODE_STA : WIFI_MODE_NAN;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = mode; taskEXIT_CRITICAL(&s_radio.lock);
    binding->status.stage = "nan-start-events";
    error = wifi_radio_begin_events(RADIO_EVENTS_START, mode);
    if (error != ESP_OK) goto done;
    binding->status.stage = "nan-start";
    binding->status.start_attempted = true;
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.stop_required = true;
    s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_STARTING;
    taskEXIT_CRITICAL(&s_radio.lock);
    error = esp_wifi_start();
    if (error != ESP_OK) goto done;
    binding->status.start_accepted = true;
    taskENTER_CRITICAL(&s_radio.lock); s_radio.started = true; taskEXIT_CRITICAL(&s_radio.lock);
    binding->status.stage = "nan-start-events";
    error = wifi_radio_wait_events_inner(NULL);
    if (error != ESP_OK) goto done;
    binding->status.stage = "nan-native-ready";
    if (usd) {
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
        error = esp_wifi_nan_usd_start();
        esp32_mquickjs_wifi_nan_usd_sdk_status(&usd_status);
        binding->status.reserved_bytes += usd_status.reserved_bytes;
#endif
    }
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    else error = esp32_mquickjs_wifi_nan_sdk_sync_ready();
#endif
    if (error != ESP_OK) goto done;
    binding->status.stage = "nan-netif-ready";
    if (!usd && !esp_netif_is_netif_up(binding->netif)) { error = ESP_ERR_INVALID_STATE; goto done; }
    binding->status.ready = true;
    binding->status.stage = NULL;
    wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STARTED);
done:
    if (binding) {
        binding->status.error = error;
        if (error != ESP_OK) {
            binding->status.closing = true;
            (void)wifi_radio_record_fault(binding->status.stage, error);
        }
        *status = binding->status;
    } else status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_nan_begin(const wifi_nan_sync_config_t *config,
    esp32_mquickjs_wifi_nan_sdk_observer_fn observer, void *opaque,
    esp32_mquickjs_wifi_radio_operation_t *token, esp32_mquickjs_wifi_nan_radio_status_t *status)
{ return wifi_radio_nan_begin(config, false, observer, opaque, token, status); }

esp_err_t esp32_mquickjs_wifi_radio_nan_usd_begin(esp32_mquickjs_wifi_nan_sdk_observer_fn observer, void *opaque,
    esp32_mquickjs_wifi_radio_operation_t *token, esp32_mquickjs_wifi_nan_radio_status_t *status)
{
    wifi_nan_sync_config_t config = {.op_channel = 6};
    return wifi_radio_nan_begin(&config, true, observer, opaque, token, status);
}

esp_err_t esp32_mquickjs_wifi_radio_nan_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_nan_radio_status_t *status)
{
    if (!token || !status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    wifi_radio_nan_t *binding = s_nan_radio;
    if (!wifi_radio_nan_exact_locked(token)) goto done;
    binding->status.closing = true;
    binding->status.ready = false;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (binding->tx_owned) esp32_mquickjs_wifi_nan_tx_seal();
    if (!binding->status.usd) {
        binding->status.cleanup_stage = "nan-service-callbacks";
        error = esp32_mquickjs_wifi_nan_sdk_service_quiesce(0);
        if (error != ESP_OK) goto failed;
    }
#endif
#if CONFIG_ESP_WIFI_NAN_PAIRING
    if (!binding->status.usd && !binding->pairing_retired) {
        binding->status.cleanup_stage = "nan-pairing-stop";
        error = esp32_mquickjs_wifi_nan_pasn_shutdown();
        if (error != ESP_OK) goto failed;
        binding->pairing_retired = true;
    }
#endif
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    if (binding->status.usd && !binding->status.native_reset) {
        binding->status.cleanup_stage = "nan-usd-stop";
        error = esp_wifi_nan_usd_stop();
        if (error != ESP_OK) goto failed;
        binding->status.native_reset = true;
        binding->status.reserved_bytes = sizeof(*binding);
    }
#endif
    if (binding->status.start_attempted && !binding->stop_fenced) {
        binding->status.cleanup_stage = "nan-stop-events";
        if (s_radio.event_phase != RADIO_EVENTS_STOP) {
            taskENTER_CRITICAL(&s_radio.lock);
            wifi_mode_t expected = binding->status.start_accepted ?
                (binding->status.usd ? WIFI_MODE_STA : WIFI_MODE_NAN) : s_radio.event_live;
            taskEXIT_CRITICAL(&s_radio.lock);
            error = wifi_radio_begin_events(RADIO_EVENTS_STOP, expected);
            if (error != ESP_OK) goto failed;
        }
        wifi_radio_set_state(ESP32_MQUICKJS_WIFI_RADIO_STOPPING);
        if (!s_radio.stop_submitted) {
#if ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE
            binding->status.cleanup_stage = "nan-he-statistics-retire";
            error = wifi_radio_retire_he_statistics_locked();
            if (error != ESP_OK) goto failed;
#endif
            binding->status.cleanup_stage = "nan-stop";
            error = esp_wifi_stop();
            if (error != ESP_OK && error != ESP_ERR_WIFI_NOT_STARTED) goto failed;
            s_radio.stop_submitted = true;
        }
        binding->status.cleanup_stage = "nan-stop-events";
        error = wifi_radio_wait_events_inner(NULL);
        if (error != ESP_OK) goto failed;
        binding->stop_fenced = true;
        taskENTER_CRITICAL(&s_radio.lock);
        s_radio.started = s_radio.stop_required = s_radio.stop_submitted = false;
        taskEXIT_CRITICAL(&s_radio.lock);
    }
    binding->status.stopped = true;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (binding->tx_owned) {
        binding->status.cleanup_stage = "nan-tx-retire";
        error = esp32_mquickjs_wifi_nan_tx_close();
        if (error != ESP_OK) goto failed;
        binding->tx_owned = false;
        binding->status.reserved_bytes = sizeof(*binding);
    }
#endif
    if (!binding->status.native_reset) {
        binding->status.cleanup_stage = "nan-native-reset";
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
        error = binding->prepare_attempted ? esp32_mquickjs_wifi_nan_sdk_sync_reset() : ESP_OK;
#else
        error = ESP_OK;
#endif
        if (error != ESP_OK) goto failed;
        binding->status.native_reset = true;
    }
    if (!binding->status.netif_retired) {
        binding->status.cleanup_stage = "nan-netif-retire";
        error = esp32_mquickjs_wifi_netif_retire(&binding->netif, &binding->detach_error);
        if (error != ESP_OK) goto failed;
        binding->status.netif_retired = true;
    }
    if (!binding->status.observer_retired) {
        binding->status.cleanup_stage = "nan-observer-retire";
        error = binding->observer ? esp32_mquickjs_wifi_nan_sdk_unobserve(&binding->observer) : ESP_OK;
        if (error != ESP_OK) goto failed;
        binding->status.observer_retired = true;
    }
    if (!binding->status.mode_restored) {
        binding->status.cleanup_stage = "nan-mode-restore";
        if (binding->saved && binding->prepare_attempted) {
            wifi_mode_t actual;
            error = esp_wifi_get_mode(&actual);
            if (error != ESP_OK) goto failed;
            if (actual != binding->saved_mode && !binding->mode_restore_attempted) {
                binding->mode_restore_attempted = true;
                binding->mode_restore_error = esp_wifi_set_mode(binding->saved_mode);
                if (binding->mode_restore_error != ESP_OK) {
                    error = binding->mode_restore_error;
                    goto failed;
                }
                error = esp_wifi_get_mode(&actual);
                if (error != ESP_OK) goto failed;
            }
            if (actual != binding->saved_mode) {
                error = binding->mode_restore_error != ESP_OK ? binding->mode_restore_error : ESP_ERR_INVALID_RESPONSE;
                goto failed;
            }
            taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = actual; taskEXIT_CRITICAL(&s_radio.lock);
        }
        binding->status.mode_restored = true;
    }
    if (!binding->status.storage_restored) {
        binding->status.cleanup_stage = "nan-storage-restore";
        if (binding->storage_attempted) {
            /* SDK has no storage getter: a failed write remains diagnosed,
             * with ownership retained, instead of an implicit repeat. */
            if (!binding->storage_restore_attempted) {
                binding->storage_restore_attempted = true;
                binding->storage_restore_error = esp_wifi_set_storage(binding->saved_storage);
            }
            error = binding->storage_restore_error;
            if (error != ESP_OK) {
                taskENTER_CRITICAL(&s_radio.lock); s_radio.storage = (wifi_storage_t)-1; taskEXIT_CRITICAL(&s_radio.lock);
                goto failed;
            }
            taskENTER_CRITICAL(&s_radio.lock); s_radio.storage = binding->saved_storage; taskEXIT_CRITICAL(&s_radio.lock);
        }
        binding->status.storage_restored = true;
    }
    binding->status.cleanup_error = ESP_OK;
    binding->status.cleanup_stage = NULL;
    *status = binding->status;
    taskENTER_CRITICAL(&s_radio.lock);
    /* These are exact-source faults, retired by the complete STOP/reset/netif/
     * restoration transaction above. Never clear a failed init or foreign fault. */
    if (!s_radio.restart_required && s_radio.fault_stage && strncmp(s_radio.fault_stage, "nan-", 4) == 0) {
        s_radio.fault_stage = NULL; s_radio.fault_error = ESP_OK;
    }
    if (s_radio.cleanup_stage && strncmp(s_radio.cleanup_stage, "nan-", 4) == 0) {
        s_radio.cleanup_stage = NULL; s_radio.cleanup_error = ESP_OK;
    }
    if (!s_radio.fault_stage && !s_radio.restart_required) s_radio.driver_state = s_radio.driver_owned ?
        ESP32_MQUICKJS_WIFI_RADIO_STOPPED : ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;
    memset(&s_radio.operation, 0, sizeof(s_radio.operation));
    taskEXIT_CRITICAL(&s_radio.lock);
    wifi_radio_release_locked(&binding->lease);
    s_nan_radio = NULL;
    esp32_mquickjs_memory_payload_free(binding);
    memset(token, 0, sizeof(*token));
    status->operation = *token;
    status->reserved_bytes = 0;
    error = ESP_OK;
    goto done;
failed:
    binding->status.cleanup_error = error;
    (void)wifi_radio_cleanup_fault(binding->status.cleanup_stage, error);
    *status = binding->status;
done:
    wifi_radio_operation_unlock();
    return error;
}
