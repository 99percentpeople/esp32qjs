#include "esp32_mquickjs_wifi_smartconfig_decoder.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp32_mquickjs_wifi_smartconfig_sdk.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include <stdatomic.h>
#include <string.h>

/* Fixed-SDK native IPC ABI (esp_supplicant/src/esp_wifi_driver.h), with the
 * C3/S3/C5 archives pinned by patch_idf_smartconfig.py. The public SmartConfig
 * entry points use these same local functions on this task. Calling them in a
 * single IPC also makes preflight/claim/start indivisible to other native work. */
typedef struct { int (*fn)(void *); void *arg; uint32_t arg_size; } sc_ipc_config_t;
extern int esp_wifi_ipc_internal(sc_ipc_config_t *config, bool sync);
extern int smartconfig_get_status(void);
extern int esp_smartconfig_start_local(void *config);
extern int esp_smartconfig_stop_local(void *unused);
extern int esp_smartconfig_set_type_local(void *type);
extern int esp_smartconfig_fast_mode_local(void *enabled);
extern int esp_esptouch_set_timeout_local(void *seconds);
extern int sc_aes_crypt_init(void);
extern ETSTimer channel_timer, Restart_delay_timer, TouchRestart_ht20_timer,
    TouchRestart_ht40_timer, TouchUdpTimer, KissRes_ht20_timer, weixin_timer,
    restart_ht20_timer, restart_ht40_timer;
_Static_assert(sizeof(sc_ipc_config_t) == 12, "review SmartConfig native IPC ABI");

enum { SC_COMMAND_START, SC_COMMAND_STOP, SC_COMMAND_FENCE };
struct esp32_mquickjs_wifi_smartconfig_decoder {
    esp32_mquickjs_wifi_smartconfig_token_t token;
    uint32_t generation;
    smartconfig_type_t type;
    uint8_t channel_timeout_s;
    bool fast_mode;
    char key[17];
    smartconfig_start_config_t config;
    sc_ipc_config_t ipc;
    int command;
    atomic_bool command_completed;
    esp_err_t command_result, error, cleanup_error;
    const char *stage;
    uint64_t ack_identity;
    smartconfig_type_t ack_type;
    uint8_t ack_phone[4], ack_token;
    bool ack_metadata_valid;
    bool start_attempted, decoder_owned, timers_owned, ack_owned, ack_attempted;
    bool scan_stopped, ap_list_cleared;
    bool started, capture_stopped, closing, stop_done, fence_done, key_cleared, retired, handoff_unknown;
};
static portMUX_TYPE s_sc_decoder_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_sc_decoder_reserved;

static esp_err_t sc_decoder_native_start(esp32_mquickjs_wifi_smartconfig_decoder_t *d)
{
    d->stage = "smartconfig-preflight";
    wifi_mode_t mode;
    bool promiscuous = false;
    esp_err_t error = esp_wifi_get_mode(&mode);
    if (error != ESP_OK) return error;
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) return ESP_ERR_INVALID_STATE;
    if (smartconfig_get_status() != 0) return ESP_ERR_INVALID_STATE;
    error = esp_wifi_get_promiscuous(&promiscuous);
    if (error != ESP_OK) return error;
    if (promiscuous) return ESP_ERR_INVALID_STATE;
    /* Reserve only after native preflight: a rejected foreign decoder must not
     * have its credentials intercepted, ACK stopped, or driver state changed. */
    d->stage = "smartconfig-events-reserve";
    error = esp32_mquickjs_wifi_smartconfig_events_begin(d->generation, &d->token);
    if (error != ESP_OK) return error;
    d->stage = "smartconfig-ack-reserve";
    error = esp32qjs_smartconfig_ack_reserve(d->token.identity);
    if (error != ESP_OK) return error;
    d->ack_owned = true;
    ETSTimer *const timers[ESP32_MQUICKJS_SMARTCONFIG_TIMERS] = {
        &channel_timer, &Restart_delay_timer, &TouchRestart_ht20_timer,
        &TouchRestart_ht40_timer, &TouchUdpTimer, &KissRes_ht20_timer,
        &weixin_timer, &restart_ht20_timer, &restart_ht40_timer,
    };
    d->stage = "smartconfig-timers-reserve";
    error = esp32_mquickjs_wifi_smartconfig_timers_begin(&d->token, timers);
    if (error != ESP_OK) return error;
    d->timers_owned = true;
    d->stage = "smartconfig-type";
    error = esp_smartconfig_set_type_local((void *)(uintptr_t)d->type);
    if (error != ESP_OK) return error;
    d->stage = "smartconfig-fast-mode";
    error = esp_smartconfig_fast_mode_local((void *)(uintptr_t)d->fast_mode);
    if (error != ESP_OK) return error;
    d->stage = "smartconfig-channel-timeout";
    error = esp_esptouch_set_timeout_local((void *)(uintptr_t)d->channel_timeout_s);
    if (error != ESP_OK) return error;
    d->stage = "smartconfig-start";
    d->decoder_owned = true; /* Partial native allocation also needs stop. */
    error = esp_smartconfig_start_local(&d->config);
    if (error != ESP_OK) return error;
    /* SDK init can return success after scheduling its own OOM restart. Keep
     * the exact owner for cleanup, but never publish that as a started decoder. */
    esp32_mquickjs_wifi_smartconfig_events_status_t events;
    error = esp32_mquickjs_wifi_smartconfig_events_status(&d->token, &events);
    if (error != ESP_OK) { d->stage = "smartconfig-events-status"; return error; }
    if (events.allocation_error != ESP_OK) {
        d->stage = "smartconfig-allocation";
        return events.allocation_error;
    }
    esp32_mquickjs_wifi_smartconfig_timer_status_t timers_status;
    error = esp32_mquickjs_wifi_smartconfig_timers_status(&d->token, &timers_status);
    if (error == ESP_OK) error = timers_status.error;
    if (error == ESP_OK) { d->started = true; d->stage = NULL; }
    return error;
}

static esp_err_t sc_decoder_native_stop(esp32_mquickjs_wifi_smartconfig_decoder_t *d)
{
    if (!d->decoder_owned || d->stop_done) return ESP_OK;
    /* These return values are ignored by the binary decoder's stop helper.
     * Require them before allowing it to free its RX/scan buffers. Timers have
     * already been revoked/drained; this IPC follows their queued native work. */
    esp_err_t error;
    if (!d->scan_stopped) {
        d->stage = "smartconfig-scan-stop";
        error = esp_wifi_scan_stop();
        if (error != ESP_OK) return error;
        d->scan_stopped = true;
    }
    if (!d->ap_list_cleared) {
        d->stage = "smartconfig-scan-list-clear";
        error = esp_wifi_clear_ap_list();
        if (error != ESP_OK) return error;
        d->ap_list_cleared = true;
    }
    /* Check RX immediately before native free on every stop attempt. A failed
     * earlier attempt is not a lasting proof that RX stayed disabled. */
    d->stage = "smartconfig-rx-stop";
    error = esp_wifi_set_promiscuous(false);
    if (error != ESP_OK) return error;
    d->stage = "smartconfig-stop";
    error = esp_smartconfig_stop_local(NULL);
    if (error != ESP_OK) return error;
    d->stop_done = true;
    d->started = false;
    return ESP_OK;
}

static int sc_decoder_command(void *argument)
{
    esp32_mquickjs_wifi_smartconfig_decoder_t *d = argument;
    esp_err_t error;
    if (d->command == SC_COMMAND_START) error = sc_decoder_native_start(d);
    else if (d->command == SC_COMMAND_STOP) error = sc_decoder_native_stop(d);
    else {
        d->stage = "smartconfig-native-fence";
        error = smartconfig_get_status() == 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
        bool promiscuous = false;
        if (error == ESP_OK) error = esp_wifi_get_promiscuous(&promiscuous);
        if (error == ESP_OK && promiscuous) error = ESP_ERR_INVALID_STATE;
        /* The reviewed init zeroes the decoder's entire 32-byte static crypto
         * record. Only do this after its native stop and queue fence. */
        if (error == ESP_OK && !d->key_cleared) {
            d->stage = "smartconfig-key-clear";
            error = sc_aes_crypt_init();
            if (error == ESP_OK) d->key_cleared = true;
        }
    }
    d->command_result = error;
    atomic_store_explicit(&d->command_completed, true, memory_order_release);
    return error;
}

static esp_err_t sc_decoder_dispatch(esp32_mquickjs_wifi_smartconfig_decoder_t *d, int command)
{
    if (d->handoff_unknown) return ESP_ERR_INVALID_STATE;
    d->command = command;
    atomic_store_explicit(&d->command_completed, false, memory_order_relaxed);
    /* Config and key live in the retained owner, never a public Future stack. */
    esp_err_t error = esp_wifi_ipc_internal(&d->ipc, true);
    if (atomic_load_explicit(&d->command_completed, memory_order_acquire))
        return d->command_result == ESP_OK ? error : d->command_result;
    /* Reviewed pre-handoff errors. Any other missing execution receipt keeps
     * the owner for diagnosis/device reboot, rather than claiming restart safe. */
    if (error != ESP_ERR_NO_MEM && error != ESP_ERR_WIFI_NOT_INIT && error != ESP_ERR_INVALID_ARG) {
        d->handoff_unknown = true;
        return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    }
    return error;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_validate_options(
    const esp32_mquickjs_wifi_smartconfig_decoder_options_t *o)
{
    if (!o || (unsigned)o->type > SC_TYPE_ESPTOUCH_V2 ||
        o->channel_timeout_s < 15 || (o->key == NULL) != (o->key_length == 0) ||
        (o->key_length && (o->key_length != 16 || o->type != SC_TYPE_ESPTOUCH_V2))) return ESP_ERR_INVALID_ARG;
    if (o->key_length && memchr(o->key, 0, o->key_length)) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_create(uint32_t generation,
    const esp32_mquickjs_wifi_smartconfig_decoder_options_t *o,
    esp32_mquickjs_wifi_smartconfig_decoder_t **out)
{
    if (!generation || !out || *out) return ESP_ERR_INVALID_ARG;
    esp_err_t error = esp32_mquickjs_wifi_smartconfig_decoder_validate_options(o);
    if (error != ESP_OK) return error;
    portENTER_CRITICAL(&s_sc_decoder_lock);
    bool allowed = !s_sc_decoder_reserved;
    if (allowed) s_sc_decoder_reserved = true;
    portEXIT_CRITICAL(&s_sc_decoder_lock);
    if (!allowed) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_smartconfig_decoder_t *d = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*d), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!d) {
        portENTER_CRITICAL(&s_sc_decoder_lock);
        s_sc_decoder_reserved = false;
        portEXIT_CRITICAL(&s_sc_decoder_lock);
        return ESP_ERR_NO_MEM;
    }
    d->generation = generation; d->type = o->type;
    d->channel_timeout_s = o->channel_timeout_s; d->fast_mode = o->fast_mode;
    if (o->key_length) memcpy(d->key, o->key, o->key_length);
    d->config = (smartconfig_start_config_t){.enable_log = false,
        .esp_touch_v2_enable_crypt = o->key_length != 0,
        .esp_touch_v2_key = o->key_length ? d->key : NULL};
    d->ipc = (sc_ipc_config_t){.fn = sc_decoder_command, .arg = d, .arg_size = 0};
    atomic_init(&d->command_completed, false);
    *out = d;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_start(esp32_mquickjs_wifi_smartconfig_decoder_t *d)
{
    if (!d || d->start_attempted || d->closing) return ESP_ERR_INVALID_STATE;
    d->start_attempted = true;
    d->error = sc_decoder_dispatch(d, SC_COMMAND_START);
    return d->error;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_status(esp32_mquickjs_wifi_smartconfig_decoder_t *d,
    esp32_mquickjs_wifi_smartconfig_decoder_status_t *status)
{
    if (!d || !status) return ESP_ERR_INVALID_ARG;
    /* Unknown handoff may still be writing native fields. Do not read them. */
    *status = (esp32_mquickjs_wifi_smartconfig_decoder_status_t){.reserved_bytes = sizeof(*d),
        .handoff_unknown = d->handoff_unknown, .error = d->error, .cleanup_error = d->cleanup_error};
    if (d->handoff_unknown) { status->stage = "smartconfig-handoff-unknown"; return ESP_OK; }
    status->token = d->token; status->ack_identity = d->ack_identity; status->stage = d->stage;
    status->started = d->started; status->closing = d->closing; status->retired = d->retired;
    status->capture_stopped = d->capture_stopped;
    status->mutation_attempted = d->decoder_owned;
    if (d->token.identity && status->error == ESP_OK) {
        esp32_mquickjs_wifi_smartconfig_events_status_t events;
        esp_err_t error = esp32_mquickjs_wifi_smartconfig_events_status(&d->token, &events);
        if (error != ESP_OK) {
            status->error = error;
            if (!status->stage) status->stage = "smartconfig-events-status";
        } else if (events.allocation_error != ESP_OK) {
            status->error = events.allocation_error;
            if (!status->stage) status->stage = "smartconfig-allocation";
        }
    }
    if (d->timers_owned && status->error == ESP_OK) {
        esp32_mquickjs_wifi_smartconfig_timer_status_t timers;
        esp_err_t error = esp32_mquickjs_wifi_smartconfig_timers_status(&d->token, &timers);
        status->error = error == ESP_OK ? timers.error : error;
        if (status->error != ESP_OK && !status->stage) status->stage = "smartconfig-timer";
    }
    if (d->ack_identity) {
        uint64_t identity = esp32qjs_smartconfig_ack_status(&status->ack_busy, NULL, &status->ack_completed,
            &status->ack_error, &status->ack_observation_error, &status->ack_socket_errno);
        if (identity != d->ack_identity) {
            status->ack_completed = false;
            status->ack_error = ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_ack(esp32_mquickjs_wifi_smartconfig_decoder_t *d)
{
    if (!d || d->handoff_unknown || !d->capture_stopped || d->closing || !d->ack_owned || d->ack_attempted)
        return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_smartconfig_credentials_t credential = {0};
    esp_err_t error = d->ack_metadata_valid ? ESP_OK :
        esp32_mquickjs_wifi_smartconfig_decoder_credentials(d, &credential, false);
    if (error == ESP_OK) {
        d->ack_attempted = true;
        error = esp32qjs_smartconfig_ack_start(d->token.identity, d->ack_type,
            d->ack_token, d->ack_phone, &d->ack_identity);
    }
    esp32_mquickjs_wireless_secure_zero(&credential, sizeof(credential));
    if (d->ack_attempted && error != ESP_OK && d->error == ESP_OK) d->error = error;
    return error;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_credentials(esp32_mquickjs_wifi_smartconfig_decoder_t *d,
    esp32_mquickjs_wifi_smartconfig_credentials_t *credentials, bool commit)
{
    if ((!commit && !credentials) || (commit && credentials)) return ESP_ERR_INVALID_ARG;
    if (!d || d->handoff_unknown || !d->capture_stopped || d->closing) return ESP_ERR_INVALID_STATE;
    if (commit) return d->ack_metadata_valid ?
        esp32_mquickjs_wifi_smartconfig_credentials_commit(&d->token) : ESP_ERR_INVALID_STATE;
    esp_err_t error = esp32_mquickjs_wifi_smartconfig_credentials_copy(&d->token, credentials);
    if (error == ESP_OK) {
        d->ack_type = credentials->network.type; d->ack_token = credentials->network.token;
        memcpy(d->ack_phone, credentials->network.cellphone_ip, sizeof(d->ack_phone));
        d->ack_metadata_valid = true;
    }
    return error;
}

static esp_err_t sc_decoder_drain_capture(esp32_mquickjs_wifi_smartconfig_decoder_t *d)
{
    if (d->capture_stopped) return ESP_OK;
    esp_err_t error;
    if (d->timers_owned) {
        d->stage = "smartconfig-timers-drain";
        error = esp32_mquickjs_wifi_smartconfig_timers_close(&d->token);
        if (error == ESP_OK) error = esp32_mquickjs_wifi_smartconfig_timers_cleanup(&d->token);
        if (error != ESP_OK) return error;
    }
    if (d->decoder_owned && !d->stop_done) {
        error = sc_decoder_dispatch(d, SC_COMMAND_STOP);
        if (error != ESP_OK) return error;
    }
    if (d->decoder_owned && !d->fence_done) {
        error = sc_decoder_dispatch(d, SC_COMMAND_FENCE);
        if (error != ESP_OK) return error;
        d->fence_done = true;
    }
    esp32_mquickjs_wireless_secure_zero(d->key, sizeof(d->key));
    d->config.esp_touch_v2_key = NULL;
    d->capture_stopped = true;
    d->stage = NULL;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(esp32_mquickjs_wifi_smartconfig_decoder_t *d)
{
    if (!d || d->handoff_unknown || !d->start_attempted || d->closing) return ESP_ERR_INVALID_STATE;
    d->cleanup_error = sc_decoder_drain_capture(d);
    return d->cleanup_error;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_close(esp32_mquickjs_wifi_smartconfig_decoder_t *d)
{
    if (!d || d->handoff_unknown) return ESP_ERR_INVALID_STATE;
    if (d->retired) return ESP_OK;
    d->closing = true;
    esp_err_t error = ESP_OK;
    if (d->token.identity) error = esp32_mquickjs_wifi_smartconfig_events_close(&d->token);
    if (error != ESP_OK) goto done;
    if (d->ack_identity) {
        error = esp32qjs_smartconfig_ack_stop(d->token.identity, d->ack_identity);
        if (error != ESP_OK) goto done;
    }
    error = sc_decoder_drain_capture(d);
    if (error != ESP_OK) goto done;
    if (d->ack_owned) {
        d->stage = "smartconfig-ack-drain";
        bool busy;
        uint64_t identity = esp32qjs_smartconfig_ack_status(&busy, NULL, NULL, NULL, NULL, NULL);
        if ((d->ack_identity && identity != d->ack_identity) || busy) {
            error = busy ? ESP_ERR_NOT_FINISHED : ESP_ERR_INVALID_STATE;
            goto done;
        }
    }
    if (d->timers_owned) {
        error = esp32_mquickjs_wifi_smartconfig_timers_release(&d->token);
        if (error != ESP_OK) goto done;
        d->timers_owned = false;
    }
    if (d->ack_owned) {
        error = esp32qjs_smartconfig_ack_release(d->token.identity);
        if (error != ESP_OK) goto done;
        d->ack_owned = false; d->ack_identity = 0;
    }
    if (d->token.identity) {
        error = esp32_mquickjs_wifi_smartconfig_events_release(&d->token);
        if (error != ESP_OK) goto done;
    }
    esp32_mquickjs_wireless_secure_zero(d->key, sizeof(d->key));
    d->config.esp_touch_v2_key = NULL;
    d->retired = true; d->stage = NULL;
    esp32_mquickjs_wireless_secure_zero(d->ack_phone, sizeof(d->ack_phone));
    d->ack_token = 0; d->ack_type = 0; d->ack_metadata_valid = false;
done:
    d->cleanup_error = error;
    return error;
}

esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_release(esp32_mquickjs_wifi_smartconfig_decoder_t **owner)
{
    if (!owner || !*owner || !(*owner)->retired || (*owner)->handoff_unknown) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_smartconfig_decoder_t *d = *owner;
    *owner = NULL;
    esp32_mquickjs_wireless_secure_zero(d, sizeof(*d));
    esp32_mquickjs_memory_payload_free(d);
    portENTER_CRITICAL(&s_sc_decoder_lock);
    s_sc_decoder_reserved = false;
    portEXIT_CRITICAL(&s_sc_decoder_lock);
    return ESP_OK;
}
#endif
