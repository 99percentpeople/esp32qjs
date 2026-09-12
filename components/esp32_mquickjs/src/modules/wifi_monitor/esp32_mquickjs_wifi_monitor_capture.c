#include "esp32_mquickjs_wifi_monitor_capture.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO

static void monitor_capture_sink(void *opaque, const esp32_mquickjs_wifi_rx_target_view_t *view,
    esp32_mquickjs_wifi_rx_filter_result_t result, uint64_t now)
{
    esp32_mquickjs_wifi_monitor_capture_t *capture = opaque;
    if (atomic_load_explicit(&capture->stop_requested, memory_order_acquire) ||
        atomic_load_explicit(&capture->close_requested, memory_order_acquire)) {
        (void)esp32_mquickjs_wifi_monitor_resources_set_accepting(capture->resources, false);
    }
    if (capture->options.channel != 0) {
        esp32_mquickjs_wifi_radio_channel_status_t channel;
        if (esp32_mquickjs_wifi_radio_lease_channel_status(&capture->radio, &channel) != ESP_OK ||
            channel.conflicted || channel.primary != capture->options.channel ||
            (view != NULL && view->metadata.available && view->metadata.primary != 0 &&
             view->metadata.primary != capture->options.channel)) {
            atomic_store_explicit(&capture->channel_conflicted, true, memory_order_release);
            esp32_mquickjs_wifi_monitor_capture_request_stop(capture, false);
        }
    }
    (void)esp32_mquickjs_wifi_monitor_publish(capture->resources, view, result, now);
}

esp_err_t esp32_mquickjs_wifi_monitor_capture_init(esp32_mquickjs_wifi_monitor_capture_t *capture,
    esp32_mquickjs_wifi_monitor_resources_t *resources, esp32_mquickjs_wifi_monitor_queue_t *bridge,
    const esp32_mquickjs_wifi_monitor_capture_options_t *options)
{
    if (capture == NULL || resources == NULL || bridge == NULL || options == NULL ||
        !esp32_mquickjs_wifi_rx_filter_valid(&options->filter)) return ESP_ERR_INVALID_ARG;
    if (capture->initialized || bridge->resources != resources || bridge->queue == NULL ||
        esp32_mquickjs_event_queue_is_closed(bridge->queue)) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_monitor_snapshot_t snapshot;
    esp32_mquickjs_wifi_monitor_resources_snapshot(resources, &snapshot);
    if (!snapshot.initialized || snapshot.accepting || snapshot.counters.publishers != 0 ||
        snapshot.counters.leased_frames != 0 || snapshot.identity_exhausted) return ESP_ERR_INVALID_STATE;
    capture->resources = resources;
    capture->bridge = bridge;
    capture->options = *options;
    capture->state = ESP32_MQUICKJS_WIFI_MONITOR_STOPPED;
    atomic_init(&capture->stop_requested, false);
    atomic_init(&capture->close_requested, false);
    atomic_init(&capture->channel_conflicted, false);
    capture->initialized = true;
    return ESP_OK;
}

void esp32_mquickjs_wifi_monitor_capture_request_stop(esp32_mquickjs_wifi_monitor_capture_t *capture, bool close)
{
    if (capture == NULL || !capture->initialized) return;
    if (close) atomic_store_explicit(&capture->close_requested, true, memory_order_release);
    atomic_store_explicit(&capture->stop_requested, true, memory_order_release);
    (void)esp32_mquickjs_wifi_monitor_resources_set_accepting(capture->resources, false);
    if (capture->notify_stop != NULL) capture->notify_stop(capture->notify_opaque);
}

static esp_err_t monitor_capture_cleanup_pending(esp32_mquickjs_wifi_monitor_capture_t *capture, const char *stage)
{
    esp32_mquickjs_wifi_radio_status_t status;
    capture->cleanup_stage = NULL;
    capture->cleanup_error = ESP_OK;
    esp_err_t err = esp32_mquickjs_wifi_radio_get_status(&status);
    if (err != ESP_OK) {
        capture->cleanup_stage = stage;
        capture->cleanup_error = err;
    } else if (status.cleanup_error != ESP_OK) {
        capture->cleanup_stage = status.cleanup_stage != NULL ? status.cleanup_stage : stage;
        capture->cleanup_error = status.cleanup_error;
    }
    return capture->cleanup_error != ESP_OK ? capture->cleanup_error : ESP_ERR_TIMEOUT;
}

esp_err_t esp32_mquickjs_wifi_monitor_capture_stop(esp32_mquickjs_wifi_monitor_capture_t *capture)
{
    if (capture == NULL || !capture->initialized) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_monitor_capture_request_stop(capture, false);
    if (capture->state == ESP32_MQUICKJS_WIFI_MONITOR_CLOSED) return ESP_OK;
    capture->state = ESP32_MQUICKJS_WIFI_MONITOR_STOPPING;
    if (capture->promiscuous.acquired) {
        esp32_mquickjs_wifi_radio_release_promiscuous(&capture->promiscuous);
        if (capture->promiscuous.acquired) return monitor_capture_cleanup_pending(capture, "promiscuous-release");
    }
    /* Radio returned the acquisition only after all selected sinks finished. */
    if (capture->channel_claimed) {
        esp32_mquickjs_wifi_radio_release_channel(&capture->radio);
        capture->channel_claimed = false;
    }
    if (capture->bridge->queue != NULL) {
        (void)esp32_mquickjs_event_queue_discard_all(capture->bridge->queue);
    }
    capture->cleanup_error = ESP_OK;
    capture->cleanup_stage = NULL;
    capture->state = ESP32_MQUICKJS_WIFI_MONITOR_STOPPED;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_monitor_capture_start(esp32_mquickjs_wifi_monitor_capture_t *capture)
{
    if (capture == NULL || !capture->initialized) return ESP_ERR_INVALID_ARG;
    if (atomic_load_explicit(&capture->close_requested, memory_order_acquire) ||
        capture->bridge->queue == NULL || esp32_mquickjs_event_queue_is_closed(capture->bridge->queue)) return ESP_ERR_INVALID_STATE;
    if (capture->state == ESP32_MQUICKJS_WIFI_MONITOR_RUNNING) {
        return atomic_load_explicit(&capture->stop_requested, memory_order_acquire) ? ESP_ERR_INVALID_STATE : ESP_OK;
    }
    if (capture->state != ESP32_MQUICKJS_WIFI_MONITOR_STOPPED || capture->promiscuous.acquired) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_monitor_snapshot_t snapshot;
    esp32_mquickjs_wifi_monitor_resources_snapshot(capture->resources, &snapshot);
    if (!snapshot.initialized || snapshot.identity_exhausted) return ESP_ERR_INVALID_STATE;
    capture->state = ESP32_MQUICKJS_WIFI_MONITOR_STARTING;
    atomic_store_explicit(&capture->stop_requested, false, memory_order_release);
    atomic_store_explicit(&capture->channel_conflicted, false, memory_order_release);
    const char *stage = "radio-acquire";
    esp_err_t err = ESP_OK;
    if (!capture->radio.acquired) {
        err = esp32_mquickjs_wifi_radio_acquire(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MONITOR, WIFI_MODE_STA, &capture->radio);
        if (err != ESP_OK) goto fail;
    }
    stage = "radio-start";
    err = esp32_mquickjs_wifi_radio_ensure_started(&capture->radio);
    if (err != ESP_OK) goto fail;
    esp32_mquickjs_wifi_radio_status_t radio_status;
    stage = "radio-status";
    err = esp32_mquickjs_wifi_radio_get_status(&radio_status);
    if (err != ESP_OK) goto fail;
    capture->radio_generation = radio_status.generation;
    if (capture->options.require_power_save_none &&
        (!radio_status.power_save_available || radio_status.power_save != WIFI_PS_NONE)) {
        stage = "power-save-policy"; err = ESP_ERR_INVALID_STATE; goto fail;
    }
    if (capture->options.channel != 0) {
        stage = "channel-claim";
        err = esp32_mquickjs_wifi_radio_set_channel(&capture->radio, capture->options.channel, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) goto fail;
        capture->channel_claimed = true;
    }
    stage = "channel-snapshot";
    err = esp32_mquickjs_wifi_radio_get_channel(&capture->effective_channel, &capture->effective_secondary, &capture->channel_generation);
    if (err != ESP_OK) goto fail;
    stage = "pool-admission";
    if (!esp32_mquickjs_wifi_monitor_resources_set_accepting(capture->resources, true)) {
        err = ESP_ERR_INVALID_STATE; goto fail;
    }
    stage = "promiscuous-subscribe";
    if (atomic_load_explicit(&capture->stop_requested, memory_order_acquire) ||
        atomic_load_explicit(&capture->close_requested, memory_order_acquire)) {
        stage = "start-cancelled"; err = ESP_ERR_INVALID_STATE; goto fail;
    }
    err = esp32_mquickjs_wifi_radio_subscribe_promiscuous(&capture->radio, &capture->subscriber,
        &capture->options.filter, monitor_capture_sink, capture, &capture->promiscuous);
    if (err != ESP_OK) goto fail;
    if (atomic_load_explicit(&capture->stop_requested, memory_order_acquire) ||
        atomic_load_explicit(&capture->close_requested, memory_order_acquire)) {
        stage = "start-cancelled"; err = ESP_ERR_INVALID_STATE; goto fail;
    }
    capture->last_error = ESP_OK;
    capture->last_stage = NULL;
    capture->state = ESP32_MQUICKJS_WIFI_MONITOR_RUNNING;
    return ESP_OK;
fail:
    capture->last_error = err;
    capture->last_stage = stage;
    (void)esp32_mquickjs_wifi_monitor_capture_stop(capture);
    return err;
}

esp_err_t esp32_mquickjs_wifi_monitor_capture_close(esp32_mquickjs_wifi_monitor_capture_t *capture)
{
    if (capture == NULL || !capture->initialized) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_monitor_capture_request_stop(capture, true);
    if (capture->state == ESP32_MQUICKJS_WIFI_MONITOR_CLOSED) return ESP_OK;
    esp_err_t err = esp32_mquickjs_wifi_monitor_capture_stop(capture);
    if (err != ESP_OK) return err;
    if (capture->radio.acquired) {
        esp32_mquickjs_wifi_radio_release(&capture->radio);
        if (capture->radio.acquired) return monitor_capture_cleanup_pending(capture, "radio-release");
    }
    esp32_mquickjs_wifi_monitor_queue_detach(capture->bridge);
    capture->cleanup_error = ESP_OK;
    capture->cleanup_stage = NULL;
    capture->state = ESP32_MQUICKJS_WIFI_MONITOR_CLOSED;
    return ESP_OK;
}
#endif
