#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_err.h"
#include "esp_wifi_he_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING = 1U << 0,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTED = 1U << 1,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN = 1U << 2,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS = 1U << 3,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_PENDING = 1U << 4,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_POSTED = 1U << 5,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_SEEN = 1U << 6,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED = 1U << 7,
    ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED = 1U << 8,
    ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING = 1U << 9,
    ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SEEN = 1U << 10,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED = 1U << 11,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_CLOSE_REQUESTED = 1U << 12,
};
typedef struct {
    wifi_event_sta_itwt_setup_t event;
    uint32_t identity, revision, observation_calls, fence_sequence;
    esp_err_t submit_error, observation_error, teardown_error;
    int16_t request_id;
    uint16_t flags;
    uint8_t cancelled_flows, teardown_flow, teardown_status;
    esp_err_t teardown_observation_error;
} esp32_mquickjs_wifi_twt_setup_result_t;
typedef struct {
    uint32_t last_identity, reserved_bytes;
    int32_t last_request_id;
    esp_err_t fault;
    uint8_t owned_mask;
} esp32_mquickjs_wifi_twt_setup_results_snapshot_t;

/* Native control path: worker/Wi-Fi task safe value operations under a lock.
 * Reserve before SDK mutation. Driver/Radio admission is the caller's duty.
 * Request IDs must form
 * a strictly increasing nonnegative prefix, including failed submissions;
 * freeing a slot never permits an old SDK event ID to identify a successor.
 * Both counters and the bounded lazy ledger survive runtime/driver restart. */
esp_err_t esp32_mquickjs_wifi_twt_setup_result_begin_native(int16_t request_id, uint32_t *identity);
void esp32_mquickjs_wifi_twt_setup_result_submitted_native(uint32_t identity, esp_err_t error);
/* Native queue only, after timer/pending cleanup; retain flow evidence across
 * native table clearing. A subsequent setup observation revokes cancellation. */
bool esp32_mquickjs_wifi_twt_setup_result_cancelled_native(uint32_t identity, uint8_t flows);
/* Native task, after exact established-ID/flow admission. One teardown
 * attempt until independent retirement; a driver error does not authorize
 * retry. Cookie-less observations are retained, not an RF identity proof. */
esp_err_t esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(uint32_t identity, uint8_t flow);
void esp32_mquickjs_wifi_twt_setup_result_teardown_submitted_native(uint32_t identity, esp_err_t error);
esp_err_t esp32_mquickjs_wifi_twt_setup_result_teardown_post(const void *data, size_t size);
/* Native connection close only: revoke every currently reserved request before
 * SDK clears flow IDs/PM. This is permanent association authority revocation,
 * not TX/timer/event retirement or an invented successful teardown event.
 * A queued submit reserved before this boundary must also be rejected. */
void esp32_mquickjs_wifi_twt_setup_results_connection_closed_native(void);
bool esp32_mquickjs_wifi_twt_setup_request_closed_native(int16_t request_id);
/* Exact owner consent, not native termination. Monotonic until result release;
 * neither a late observation nor a failed teardown revokes the close request.
 * Missing/unmanaged request IDs never grant consent to cancel a shared timer. */
void esp32_mquickjs_wifi_twt_setup_result_request_close(uint32_t identity);
bool esp32_mquickjs_wifi_twt_setup_request_closing(int16_t request_id);
/* Value copies only, valid from a worker/JS thread. Error leaves output intact. */
esp_err_t esp32_mquickjs_wifi_twt_setup_result_read(uint32_t identity,
    esp32_mquickjs_wifi_twt_setup_result_t *out);
void esp32_mquickjs_wifi_twt_setup_results_snapshot(esp32_mquickjs_wifi_twt_setup_results_snapshot_t *out);
/* Proof consumer, NOT a retirement probe. The native owner may call this only
 * after its independently established TX/timer/native/event/HW retirement.
 * Exact revision must still match and no post/submission may be in flight.
 * A setup result, timeout or an empty pending bitmap alone is insufficient. */
bool esp32_mquickjs_wifi_twt_setup_result_release_native(uint32_t identity, uint32_t revision,
    uint32_t sequence);
typedef struct { uint32_t identity, sequence; } esp32_mquickjs_wifi_twt_setup_event_fence_t;
#define ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_EVENT 5
/* After independently establishing TX/timer/native/HW quiescence, post a
 * copied identity/sequence to the boot-owned Radio event handler. A later
 * setup observation invalidates this proof, even if the fence was delivered.
 * ESP_OK acknowledges posting only. Read the latest revision and fence flags
 * before release; output is unchanged on error. No owner/runtime pointers. */
esp_err_t esp32_mquickjs_wifi_twt_setup_result_post_fence(uint32_t identity, uint32_t revision,
    uint32_t *sequence);
void esp32_mquickjs_wifi_twt_setup_result_observe_fence(const esp32_mquickjs_wifi_twt_setup_event_fence_t *event);
/* Called only for the reviewed WIFI_EVENT_ITWT_SETUP native payload. Stores
 * control data before best-effort zero-wait publication; never borrows data. */
esp_err_t esp32_mquickjs_wifi_twt_setup_result_post(const void *data, size_t size);
#endif
