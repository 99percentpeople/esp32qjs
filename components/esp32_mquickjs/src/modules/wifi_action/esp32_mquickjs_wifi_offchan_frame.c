#include "esp32_mquickjs_wifi_offchan_frame.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_wifi.h"
#if CONFIG_ESP_WIFI_DPP_SUPPORT
#include "esp32_mquickjs_wifi_dpp_result.h"
#endif
#include "esp32_mquickjs_wifi_nan_usd_sdk.h"
#include "freertos/FreeRTOS.h"

/* Hash-gated C3/S3/C5 SDK layout. Only wl_offchan.o's own post call enters
 * frame_post; ordinary management/Raw TX callers retain the real output path.
 * Every native off-channel record memset invalidates this binding, including
 * cancel, silent failure, ROC and new admission before deferred PM output.
 * The native recycler invalidates it before the EB address can be reused. */
extern uint8_t g_offchan_ctx[28];
int ieee80211_post_hmac_tx(void *buffer);
void offchan_send_action_tx_status(uint8_t interface, int status);
int wifi_event_post(int event_id, void *data, size_t size);
int pp_post(int signal, void *message);

static bool offchan_managed_context(uint32_t context)
{
#if CONFIG_ESP_WIFI_DPP_SUPPORT
    if (esp32qjs_dpp_is_action_callback(context)) return true;
#endif
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    if (esp32qjs_nan_usd_is_action_callback(context)) return true;
#endif
    return false;
}

int esp32qjs_wifi_offchan_event_post(int event_id, void *data, size_t size)
{
    if (event_id == WIFI_EVENT_ACTION_TX_STATUS && data &&
        size == sizeof(wifi_event_action_tx_status_t)) {
        wifi_event_action_tx_status_t event;
        memcpy(&event, data, sizeof(event));
        if (offchan_managed_context(event.context)) {
            /* Capture before the fallible observer queue. This hook runs in
             * the native publisher, never in the delayed default event loop. */
#if CONFIG_ESP_WIFI_DPP_SUPPORT
            if (esp32qjs_dpp_is_action_callback(event.context)) esp32qjs_dpp_tx_status_capture(&event);
#endif
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
            if (esp32qjs_nan_usd_is_action_callback(event.context)) esp32qjs_nan_usd_tx_status_capture(&event);
#endif
            return esp_event_post(WIFI_EVENT, event_id, &event, sizeof(event), 0);
        }
    }
    if (event_id == WIFI_EVENT_ROC_DONE && data && size == sizeof(wifi_event_roc_done_t)) {
        wifi_event_roc_done_t event;
        memcpy(&event, data, sizeof(event));
        if (offchan_managed_context(event.context))
            return esp_event_post(WIFI_EVENT, event_id, &event, sizeof(event), 0);
    }
    return wifi_event_post(event_id, data, size);
}

static DRAM_ATTR struct {
    void *buffer;
    uint32_t context, interface;
    uint8_t operation, channel;
} s_offchan_frame;
static DRAM_ATTR struct {
    esp32qjs_wifi_offchan_tx_status_t status;
    void *buffer;
    uint32_t context;
    uint8_t channel;
} s_offchan_tx;
static DRAM_ATTR uint64_t s_offchan_tx_last_ticket;
static DRAM_ATTR uint64_t s_offchan_tx_allocated_ticket;
static DRAM_ATTR portMUX_TYPE s_offchan_frame_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t esp32qjs_wifi_offchan_tx_allocate_ticket(uint64_t *ticket)
{
    if (!ticket || *ticket) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_offchan_frame_lock);
    esp_err_t error = s_offchan_tx_allocated_ticket == UINT64_MAX ? ESP_ERR_NO_MEM : ESP_OK;
    if (!error) *ticket = ++s_offchan_tx_allocated_ticket;
    portEXIT_CRITICAL_SAFE(&s_offchan_frame_lock);
    return error;
}

esp_err_t esp32qjs_wifi_offchan_tx_reserve(uint64_t ticket, uint32_t context, uint8_t channel)
{
    if (!ticket || !context || !channel) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_offchan_frame_lock);
    esp_err_t error = ESP_OK;
    if (s_offchan_tx.status.held || s_offchan_tx.status.unknown_frames || ticket <= s_offchan_tx_last_ticket)
        error = ESP_ERR_INVALID_STATE;
    else {
        s_offchan_tx_last_ticket = ticket;
        if (s_offchan_tx_allocated_ticket < ticket) s_offchan_tx_allocated_ticket = ticket;
        memset(&s_offchan_tx, 0, sizeof(s_offchan_tx));
        s_offchan_tx.status.ticket = ticket;
        s_offchan_tx.status.held = true;
        s_offchan_tx.context = context; s_offchan_tx.channel = channel;
    }
    portEXIT_CRITICAL_SAFE(&s_offchan_frame_lock);
    return error;
}

esp_err_t esp32qjs_wifi_offchan_tx_status(uint64_t ticket, esp32qjs_wifi_offchan_tx_status_t *out)
{
    if (!out || !ticket) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_offchan_frame_lock);
    bool exact = s_offchan_tx.status.held && s_offchan_tx.status.ticket == ticket;
    if (exact) *out = s_offchan_tx.status;
    portEXIT_CRITICAL_SAFE(&s_offchan_frame_lock);
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32qjs_wifi_offchan_tx_poll_native(uint64_t ticket)
{
    esp32qjs_wifi_offchan_tx_status_t status;
    esp_err_t error = esp32qjs_wifi_offchan_tx_status(ticket, &status);
    if (error != ESP_OK || !status.wake_pending || !status.buffer_present || status.recycling) return error;
    /* The reviewed post_hmac_tx appends EB FIRST, then pp_post(5, NULL) only
     * wakes its consumer. Failure cannot authorize a second append/free.
     * This is the same generic wake, with no packet or operation payload. An
     * extra empty wake after concurrent recycling cannot repeat an RF frame. */
    int result = pp_post(5, NULL);
    portENTER_CRITICAL_SAFE(&s_offchan_frame_lock);
    if (s_offchan_tx.status.held && s_offchan_tx.status.ticket == ticket) {
        if (s_offchan_tx.status.wake_retries != UINT32_MAX) ++s_offchan_tx.status.wake_retries;
        if (!result) s_offchan_tx.status.wake_pending = false;
    }
    portEXIT_CRITICAL_SAFE(&s_offchan_frame_lock);
    return ESP_OK;
}

esp_err_t esp32qjs_wifi_offchan_tx_release(uint64_t ticket)
{
    portENTER_CRITICAL_SAFE(&s_offchan_frame_lock);
    esp_err_t error = ESP_OK;
    if (!ticket || !s_offchan_tx.status.held || s_offchan_tx.status.ticket != ticket) error = ESP_ERR_INVALID_STATE;
    else if (s_offchan_tx.status.unknown_frames) error = ESP_ERR_INVALID_STATE;
    else if (s_offchan_tx.buffer || s_offchan_tx.status.recycling) error = ESP_ERR_NOT_FINISHED;
    else memset(&s_offchan_tx, 0, sizeof(s_offchan_tx));
    portEXIT_CRITICAL_SAFE(&s_offchan_frame_lock);
    return error;
}

int esp32qjs_wifi_offchan_frame_post(void *buffer)
{
    uint32_t context, interface;
    memcpy(&context, g_offchan_ctx + 4, sizeof(context));
    memcpy(&interface, g_offchan_ctx + 8, sizeof(interface));
    bool managed = offchan_managed_context(context);
    uint64_t ticket = 0;
    portENTER_CRITICAL_SAFE(&s_offchan_frame_lock);
    if (managed) {
        bool exact = buffer && s_offchan_tx.status.held && !s_offchan_tx.status.posted &&
            s_offchan_tx.context == context && !interface && s_offchan_tx.channel == g_offchan_ctx[2];
        if (!exact) {
            /* An unexpected native producer is not silently forgotten. Keep
             * driver cleanup behavior, but never attest full retirement from
             * our single entry after this ownership invariant was violated. */
            s_offchan_tx.status.unknown_frames = true;
            s_offchan_tx.status.error = ESP_ERR_INVALID_STATE;
        } else {
            ticket = s_offchan_tx.status.ticket;
            s_offchan_tx.buffer = buffer;
            s_offchan_tx.status.posted = true;
            s_offchan_tx.status.buffer_present = true;
        }
    }
    s_offchan_frame.buffer = buffer;
    s_offchan_frame.context = context;
    s_offchan_frame.interface = interface;
    s_offchan_frame.operation = g_offchan_ctx[12];
    s_offchan_frame.channel = g_offchan_ctx[2];
    portEXIT_CRITICAL_SAFE(&s_offchan_frame_lock);
    /* The SDK may complete/recycle synchronously. Do not read the EB or restore
     * this binding after output returns, including on output failure. */
    int result = ieee80211_post_hmac_tx(buffer);
    if (ticket) {
        portENTER_CRITICAL_SAFE(&s_offchan_frame_lock);
        if (s_offchan_tx.status.held && s_offchan_tx.status.ticket == ticket) {
            s_offchan_tx.status.post_error = result;
            s_offchan_tx.status.wake_pending = result == ESP_ERR_WIFI_POST && s_offchan_tx.buffer != NULL;
            if (result && result != ESP_ERR_WIFI_POST && s_offchan_tx.status.error == ESP_OK)
                s_offchan_tx.status.error = result;
        }
        portEXIT_CRITICAL_SAFE(&s_offchan_frame_lock);
    }
    return result;
}

uint64_t IRAM_ATTR esp32qjs_wifi_offchan_frame_recycle(void *buffer)
{
    uint64_t ticket = 0;
    portENTER_CRITICAL_SAFE(&s_offchan_frame_lock);
    if (s_offchan_frame.buffer == buffer) s_offchan_frame.buffer = NULL;
    if (buffer && s_offchan_tx.status.held && s_offchan_tx.buffer == buffer && !s_offchan_tx.status.recycling) {
        ticket = s_offchan_tx.status.ticket;
        s_offchan_tx.status.recycling = true;
        s_offchan_tx.status.wake_pending = false;
        /* getmgtframe uses pool classes 2/3/4 on all three pinned targets.
         * Those recycler branches return the buffer to its pool or free it.
         * Unknown types can call a no-op handler: do not call that retirement. */
#if CONFIG_IDF_TARGET_ESP32C5
        uint8_t type = ((const uint8_t *)buffer)[30];
#else
        uint8_t type = ((const uint8_t *)buffer)[26];
#endif
        if (type < 2 || type > 4) {
            s_offchan_tx.status.unknown_frames = true;
            s_offchan_tx.status.error = ESP_ERR_INVALID_STATE;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_offchan_frame_lock);
    return ticket;
}

void IRAM_ATTR esp32qjs_wifi_offchan_frame_recycled(uint64_t ticket)
{
    if (!ticket) return;
    portENTER_CRITICAL_SAFE(&s_offchan_frame_lock);
    if (s_offchan_tx.status.held && s_offchan_tx.status.ticket == ticket && s_offchan_tx.status.recycling) {
        s_offchan_tx.buffer = NULL;
        s_offchan_tx.status.buffer_present = false;
        s_offchan_tx.status.recycling = false;
        s_offchan_tx.status.recycled = !s_offchan_tx.status.unknown_frames;
    }
    portEXIT_CRITICAL_SAFE(&s_offchan_frame_lock);
}

void *esp32qjs_wifi_offchan_record_reset(void *destination, int value, size_t size)
{
    if (destination == g_offchan_ctx && size == sizeof(g_offchan_ctx)) {
        portENTER_CRITICAL_SAFE(&s_offchan_frame_lock);
        s_offchan_frame.buffer = NULL;
        portEXIT_CRITICAL_SAFE(&s_offchan_frame_lock);
    }
    return memset(destination, value, size);
}

void esp32qjs_wifi_offchan_frame_done(void *buffer, int status)
{
    /* This call site still owns its EB and has already decoded TX success.
     * Recover exactly the interface bits used by the original SDK caller. */
    void *metadata;
    uint32_t flags, context, interface;
#if CONFIG_IDF_TARGET_ESP32C5
    memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
#else
    memcpy(&metadata, (const uint8_t *)buffer + 44, sizeof(metadata));
#endif
    memcpy(&flags, (const uint8_t *)metadata + 16, sizeof(flags));
#if CONFIG_IDF_TARGET_ESP32C5
    uint8_t frame_interface = (flags >> 18) & 3U;
#else
    uint8_t frame_interface = (flags >> 19) & 1U;
#endif
    memcpy(&context, g_offchan_ctx + 4, sizeof(context));
    memcpy(&interface, g_offchan_ctx + 8, sizeof(interface));
    portENTER_CRITICAL_SAFE(&s_offchan_frame_lock);
    bool exact = buffer == s_offchan_frame.buffer && s_offchan_frame.context == context &&
        s_offchan_frame.interface == interface && interface == frame_interface &&
        s_offchan_frame.operation == g_offchan_ctx[12] && s_offchan_frame.channel == g_offchan_ctx[2];
    if (exact) s_offchan_frame.buffer = NULL;
    portEXIT_CRITICAL_SAFE(&s_offchan_frame_lock);
    /* Duration/cancel statuses in wl_offchan.o still use their original path.
     * Unrelated or retired frames must not be labelled with a current op ID. */
    if (exact) offchan_send_action_tx_status(frame_interface, status);
}

#if !CONFIG_SOC_WIFI_HE_SUPPORT || !CONFIG_IDF_TARGET_ESP32C5
void __real_esf_buf_recycle(void *buffer);
void IRAM_ATTR __wrap_esf_buf_recycle(void *buffer)
{
    uint64_t ticket = esp32qjs_wifi_offchan_frame_recycle(buffer);
    __real_esf_buf_recycle(buffer);
    /* No EB/metadata access after the native call: the address may be reused. */
    esp32qjs_wifi_offchan_frame_recycled(ticket);
}
#endif
#endif
