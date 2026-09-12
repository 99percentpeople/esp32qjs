/* Included by the reviewed build-local nan_app.c. No framework heap or runtime
 * pointer is kept by this boot-scoped bridge; the registered native owner keeps
 * opaque alive until unobserve acknowledges all entered callbacks. */
#include "esp32_mquickjs_wifi_nan_sdk.h"
#include "esp32_mquickjs_wifi_nan_tx.h"
#include "esp32_mquickjs_wifi_nan_ndp.h"
#include "esp32_mquickjs_wifi_nan_timer.h"
#include "esp32_mquickjs_wifi_nan_usd_sdk.h"
#include "esp32_mquickjs_wifi_nan_pasn_sdk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <string.h>
#include "esp32_mquickjs_wifi_nan_query_sdk.inc"

#if CONFIG_ESP_WIFI_NAN_PAIRING && CONFIG_ESP_WIFI_NAN_SECURITY
/* NAN_DATA_LOCK is held by the native reset/crypto caller. The service tags
 * live in the already-budgeted Session pool; SDK key record layout is intact. */
static void esp32qjs_nan_pairing_reset_service(uint8_t service_id)
{
    if (!service_id) return;
    for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        if (esp32_mquickjs_wifi_nan_pairing_cache_service(i) != service_id) continue;
        forced_memzero(&s_nan_ctx.paired_peers[i], sizeof(s_nan_ctx.paired_peers[i]));
        esp32_mquickjs_wifi_nan_pairing_cache_bind(i, 0);
    }
}

const struct nan_paired_peer *esp32_mquickjs_wifi_nan_pairing_cached_peer(
    uint8_t service_id, const uint8_t peer[6])
{
    if (!service_id || !peer) return NULL;
    for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        const struct nan_paired_peer *record = &s_nan_ctx.paired_peers[i];
        if (!record->valid || esp32_mquickjs_wifi_nan_pairing_cache_service(i) != service_id ||
            memcmp(record->peer_nmi, peer, 6)) continue;
        int64_t elapsed = esp_timer_get_time() - record->established_us;
        if (record->lifetime_sec && (elapsed < 0 || elapsed >= (int64_t)record->lifetime_sec * 1000000)) return NULL;
        return record;
    }
    return NULL;
}
#endif

#define ESP32QJS_NAN_LIFECYCLE_WAIT_MS 2000U
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
/* Prepared on the exclusive Radio worker, written on default-loop START,
 * read only after that loop's lifecycle fence. No JS/runtime pointer. */
static esp_err_t s_esp32qjs_nan_start_error;
#endif
static portMUX_TYPE s_esp32qjs_nan_observer_lock = portMUX_INITIALIZER_UNLOCKED;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
static uint32_t s_esp32qjs_nan_service_callbacks;
static uint8_t s_esp32qjs_nan_service_frozen[32];
static uint8_t s_esp32qjs_nan_discovery_only[32];
static bool s_esp32qjs_nan_services_frozen;
typedef struct { bool entered; } esp32qjs_nan_service_guard_t;

static void esp32qjs_nan_set_discovery_only(uint8_t id, bool discovery_only)
{
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    uint8_t bit = (uint8_t)(1U << (id & 7));
    if (discovery_only) s_esp32qjs_nan_discovery_only[id >> 3] |= bit;
    else s_esp32qjs_nan_discovery_only[id >> 3] &= (uint8_t)~bit;
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
}

static bool esp32qjs_nan_is_discovery_only(uint8_t id)
{
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    bool value = (s_esp32qjs_nan_discovery_only[id >> 3] & (1U << (id & 7))) != 0;
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
    return value;
}

static esp32qjs_nan_service_guard_t esp32qjs_nan_service_enter(uint8_t id)
{
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    bool entered = id && !s_esp32qjs_nan_services_frozen &&
        !(s_esp32qjs_nan_service_frozen[id >> 3] & (1U << (id & 7))) &&
        s_esp32qjs_nan_service_callbacks != UINT32_MAX;
    if (entered) ++s_esp32qjs_nan_service_callbacks;
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
    return (esp32qjs_nan_service_guard_t){.entered = entered};
}

static void esp32qjs_nan_service_leave(esp32qjs_nan_service_guard_t *guard)
{
    if (!guard->entered) return;
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    --s_esp32qjs_nan_service_callbacks;
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
}

/* NDP completion has an NDP ID rather than a service ID. Freeze it only at
 * the global STOP boundary; a service-specific freeze must still allow an
 * already accepted datapath to finish native work. Share the existing entered
 * callback count so the global Radio shutdown cannot miss these scopes. */
static esp32qjs_nan_service_guard_t esp32qjs_nan_datapath_enter(void)
{
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    bool entered = !s_esp32qjs_nan_services_frozen && s_esp32qjs_nan_service_callbacks != UINT32_MAX;
    if (entered) ++s_esp32qjs_nan_service_callbacks;
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
    return (esp32qjs_nan_service_guard_t){.entered = entered};
}

#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE && CONFIG_IDF_TARGET_ESP32C5
/* The hash-pinned common object tail-calls this one-argument, void callback.
 * Its SD branch dereferences a peer again AFTER nan_app_replied_cb returns.
 * Hold the same freeze guard around that complete native function. The EB and
 * metadata are still owned by the caller; this wrapper never recycles them.
 * Subtype 2 is the SDK's action completion (including follow-up), not a service
 * lookup. Preserve it even while SD callbacks are frozen so native operations
 * can terminate. The reviewed subtype 1 branch performs no service mutation. */
_Static_assert(sizeof(uintptr_t) == 4, "reviewed C5 NAN TX callback layout");
extern void __real_nan_sdf_txcb(void *buffer);
void __wrap_nan_sdf_txcb(void *buffer)
{
    if (!buffer) return;
    const uint8_t *metadata;
    memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
    if (!metadata) return;
    if ((metadata[9] & 0x7f) == 2) {
        uint32_t ticket = esp32_mquickjs_wifi_nan_tx_callback_enter(buffer);
        bool success = metadata[19] == 1;
        __real_nan_sdf_txcb(buffer);
        esp32_mquickjs_wifi_nan_tx_callback_leave(ticket, success);
        return;
    }
    if (metadata[9] & 0x7f) {
        __real_nan_sdf_txcb(buffer);
        return;
    }
    esp32qjs_nan_service_guard_t guard __attribute__((cleanup(esp32qjs_nan_service_leave))) =
        esp32qjs_nan_service_enter(metadata[8]);
    if (guard.entered) __real_nan_sdf_txcb(buffer);
}

/* The pinned common object tail-calls this with its still-owned EB. Native
 * NAF completion can resend, delete a peer and invoke app/security callbacks;
 * protect its entire body, including the tail after the app callback returns. */
extern void __real_nan_naf_txcb(void *buffer);
void __wrap_nan_naf_txcb(void *buffer)
{
    if (!buffer) return;
    const uint8_t *metadata;
    memcpy(&metadata, (const uint8_t *)buffer + 56, sizeof(metadata));
    if (!metadata) return;
    uint32_t ticket = esp32_mquickjs_wifi_nan_tx_callback_enter(buffer);
    bool success = metadata[19] == 1;
    esp32qjs_nan_service_guard_t guard __attribute__((cleanup(esp32qjs_nan_service_leave))) =
        esp32qjs_nan_datapath_enter();
    if (guard.entered && esp32_mquickjs_wifi_nan_tx_callback_allowed(ticket)) __real_nan_naf_txcb(buffer);
    esp32_mquickjs_wifi_nan_tx_callback_leave(ticket, success);
}

/* nan_input transfers this RX EB to nan_rx_naf and then returns. The frozen
 * path still returns that EB; the active path retains the whole native parser,
 * security/app callbacks and recycler inside the same shutdown barrier. */
extern void __real_nan_rx_naf(void *buffer, void *frame, void *metadata);
extern void ic_ebuf_recycle_rx(void *buffer);
void __wrap_nan_rx_naf(void *buffer, void *frame, void *metadata)
{
    if (!buffer) return;
    esp32qjs_nan_service_guard_t guard __attribute__((cleanup(esp32qjs_nan_service_leave))) =
        esp32qjs_nan_datapath_enter();
    if (guard.entered) {
#if CONFIG_ESP_WIFI_NAN_SECURITY
        esp32_mquickjs_wifi_nan_sdk_security_clear_pending();
#endif
        __real_nan_rx_naf(buffer, frame, metadata);
#if CONFIG_ESP_WIFI_NAN_SECURITY
        esp32_mquickjs_wifi_nan_sdk_security_clear_pending();
#endif
    } else ic_ebuf_recycle_rx(buffer);
}

extern void __real_nan_nulldata_txcb(void *buffer);
void __wrap_nan_nulldata_txcb(void *buffer)
{
    if (!buffer) return;
    uint32_t ticket = esp32_mquickjs_wifi_nan_tx_callback_enter(buffer);
    esp32qjs_nan_service_guard_t guard __attribute__((cleanup(esp32qjs_nan_service_leave))) =
        esp32qjs_nan_datapath_enter();
    if (guard.entered && esp32_mquickjs_wifi_nan_tx_callback_allowed(ticket)) __real_nan_nulldata_txcb(buffer);
    esp32_mquickjs_wifi_nan_tx_callback_leave(ticket, false);
}

extern void __real_nan_ndp_setup_timeout_process(void *argument);
extern void __real_nan_ndp_inactivity_timeout_process(void *argument);
void esp32_mquickjs_wifi_nan_sdk_timer_process(uint8_t phase, void *argument)
{
    esp32qjs_nan_service_guard_t guard __attribute__((cleanup(esp32qjs_nan_service_leave))) =
        esp32qjs_nan_datapath_enter();
    if (!guard.entered) return;
    if (phase == 42) __real_nan_ndp_setup_timeout_process(argument);
    else if (phase == 43) __real_nan_ndp_inactivity_timeout_process(argument);
}
#endif

esp_err_t esp32_mquickjs_wifi_nan_sdk_service_quiesce(uint8_t id)
{
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    if (id) s_esp32qjs_nan_service_frozen[id >> 3] |= (uint8_t)(1U << (id & 7));
    else s_esp32qjs_nan_services_frozen = true;
    bool drained = !s_esp32qjs_nan_service_callbacks;
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
    return drained ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t esp32_mquickjs_wifi_nan_sdk_service_resume(uint8_t id)
{
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    bool drained = !s_esp32qjs_nan_service_callbacks;
    if (drained) {
        if (id) s_esp32qjs_nan_service_frozen[id >> 3] &= (uint8_t)~(1U << (id & 7));
        else {
            memset(s_esp32qjs_nan_service_frozen, 0, sizeof(s_esp32qjs_nan_service_frozen));
            s_esp32qjs_nan_services_frozen = false;
        }
    }
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
    return drained ? ESP_OK : ESP_ERR_INVALID_STATE;
}
#endif
static struct {
    esp32_mquickjs_wifi_nan_sdk_observer_fn callback;
    void *opaque;
    uint32_t identity, next_identity, callbacks, dropped_events;
    bool closing;
} s_esp32qjs_nan_observer = {.next_identity = 1};

esp_err_t esp32_mquickjs_wifi_nan_sdk_observe(
    esp32_mquickjs_wifi_nan_sdk_observer_fn callback, void *opaque, uint32_t *identity)
{
    if (callback == NULL || identity == NULL || *identity != 0) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (s_esp32qjs_nan_observer.identity == 0) {
        error = ESP_ERR_NO_MEM;
        if (s_esp32qjs_nan_observer.next_identity != 0) {
            s_esp32qjs_nan_observer.identity = s_esp32qjs_nan_observer.next_identity++;
            s_esp32qjs_nan_observer.callback = callback;
            s_esp32qjs_nan_observer.opaque = opaque;
            s_esp32qjs_nan_observer.closing = false;
            *identity = s_esp32qjs_nan_observer.identity;
            error = ESP_OK;
        }
    }
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_nan_sdk_unobserve(uint32_t *identity)
{
    if (identity == NULL || *identity == 0) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (*identity == s_esp32qjs_nan_observer.identity) {
        s_esp32qjs_nan_observer.closing = true;
        error = ESP_ERR_TIMEOUT;
        if (s_esp32qjs_nan_observer.callbacks == 0) {
            s_esp32qjs_nan_observer.callback = NULL;
            s_esp32qjs_nan_observer.opaque = NULL;
            s_esp32qjs_nan_observer.identity = 0;
            s_esp32qjs_nan_observer.closing = false;
            *identity = 0;
            error = ESP_OK;
        }
    }
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
    return error;
}

void esp32_mquickjs_wifi_nan_sdk_observer_status(
    esp32_mquickjs_wifi_nan_sdk_observer_status_t *output)
{
    if (output == NULL) return;
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    *output = (esp32_mquickjs_wifi_nan_sdk_observer_status_t){
        .identity = s_esp32qjs_nan_observer.identity,
        .callbacks = s_esp32qjs_nan_observer.callbacks,
        .dropped_events = s_esp32qjs_nan_observer.dropped_events,
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
        .service_callbacks = s_esp32qjs_nan_service_callbacks,
#endif
        .closing = s_esp32qjs_nan_observer.closing,
    };
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
}

static void esp32qjs_nan_notice(const esp32_mquickjs_wifi_nan_sdk_notice_t *notice)
{
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    esp32_mquickjs_wifi_nan_ndp_notice(notice);
#endif
    esp32_mquickjs_wifi_nan_sdk_observer_fn callback = NULL;
    void *opaque = NULL;
    uint32_t identity = 0;
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    if (s_esp32qjs_nan_observer.identity != 0 && !s_esp32qjs_nan_observer.closing &&
        s_esp32qjs_nan_observer.callbacks != UINT32_MAX) {
        ++s_esp32qjs_nan_observer.callbacks;
        callback = s_esp32qjs_nan_observer.callback;
        opaque = s_esp32qjs_nan_observer.opaque;
        identity = s_esp32qjs_nan_observer.identity;
    }
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
    if (callback == NULL) return;
    callback(identity, notice, opaque);
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    --s_esp32qjs_nan_observer.callbacks;
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
}

static void esp32qjs_nan_control(esp32_mquickjs_wifi_nan_sdk_notice_kind_t kind,
    uint8_t service_id, uint8_t ndp_id, int32_t status, uint32_t context, const uint8_t *peer)
{
    esp32_mquickjs_wifi_nan_sdk_notice_t notice = {
        .kind = kind, .service_id = service_id, .ndp_id = ndp_id, .status = status, .context = context,
    };
    if (peer != NULL) memcpy(notice.peer, peer, sizeof(notice.peer));
    esp32qjs_nan_notice(&notice);
}

void esp32_mquickjs_wifi_nan_sdk_notice(const esp32_mquickjs_wifi_nan_sdk_notice_t *notice)
{
    if (notice) esp32qjs_nan_notice(notice);
}

static void esp32qjs_nan_ndp_control(esp32_mquickjs_wifi_nan_sdk_notice_kind_t kind,
    uint8_t service_id, uint8_t ndp_id, int32_t status, const uint8_t peer[6],
    const uint8_t peer_ndi[6], const uint8_t own_ndi[6], const uint8_t ipv6_identifier[8],
    const uint8_t *ssi, uint16_t ssi_len)
{
    esp32_mquickjs_wifi_nan_sdk_ndp_data_t data = {.ssi = ssi, .ssi_len = ssi ? ssi_len : 0};
    if (peer_ndi) memcpy(data.peer_ndi, peer_ndi, sizeof(data.peer_ndi));
    if (own_ndi) memcpy(data.own_ndi, own_ndi, sizeof(data.own_ndi));
    if (ipv6_identifier) memcpy(data.ipv6_identifier, ipv6_identifier, sizeof(data.ipv6_identifier));
    esp32_mquickjs_wifi_nan_sdk_notice_t notice = {
        .kind = kind, .service_id = service_id, .ndp_id = ndp_id, .status = status,
        .data = &data, .size = sizeof(data),
    };
    if (peer) memcpy(notice.peer, peer, sizeof(notice.peer));
    esp32qjs_nan_notice(&notice);
}

static void esp32qjs_nan_event(int32_t event_id, const void *data, size_t size)
{
    const esp32_mquickjs_wifi_nan_sdk_notice_t notice = {
        .kind = ESP32_MQUICKJS_NAN_SDK_EVENT, .event_id = event_id, .data = data, .size = size,
    };
    esp32qjs_nan_notice(&notice);
}

static void esp32qjs_nan_event_dropped(void)
{
    portENTER_CRITICAL(&s_esp32qjs_nan_observer_lock);
    if (s_esp32qjs_nan_observer.dropped_events != UINT32_MAX)
        ++s_esp32qjs_nan_observer.dropped_events;
    portEXIT_CRITICAL(&s_esp32qjs_nan_observer_lock);
}
