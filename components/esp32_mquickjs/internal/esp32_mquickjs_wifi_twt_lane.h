#pragma once
#include "esp32_mquickjs_wifi_twt_options.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT

typedef enum {
    ESP32_MQUICKJS_WIFI_TWT_INDIVIDUAL = 1,
    ESP32_MQUICKJS_WIFI_TWT_BROADCAST = 2,
} esp32_mquickjs_wifi_twt_kind_t;
typedef struct { uint64_t identity; uint32_t generation; } esp32_mquickjs_wifi_twt_token_t;
/* One domain shared by ALL TWT records, initialized once per boot with
 * next_identity=1. Never reset on runtime/driver restart. Requested connection
 * IDs consume the monotonic prefix too; neither counter wraps or reuses IDs. */
typedef struct { uint64_t next_identity; uint32_t next_connection_id; } esp32_mquickjs_wifi_twt_identity_t;
typedef union {
    wifi_itwt_setup_config_t individual;
    wifi_btwt_setup_config_t broadcast;
} esp32_mquickjs_wifi_twt_config_t;
typedef union {
    wifi_event_sta_itwt_setup_t individual;
    wifi_event_sta_btwt_setup_t broadcast;
} esp32_mquickjs_wifi_twt_setup_t;
/* Owner serializes every access. No SDK calls, allocation, JS pointers or
 * observation queue in this record. The original request, SDK writeback and
 * AP response are separate: a requested flow is not an Agreement identity.
 * This helper does not establish Radio admission or SDK retirement proof. */
typedef struct {
    esp32_mquickjs_wifi_twt_token_t token;
    esp32_mquickjs_wifi_twt_config_t requested, dispatched;
    esp32_mquickjs_wifi_twt_setup_t setup;
    uint32_t revision, timeout_ms;
    esp_err_t submit_error, teardown_error;
    uint8_t kind, agreement_id;
    int teardown_status;
    bool dispatching, submitted, setup_seen, setup_success, close_requested;
    bool teardown_busy, teardown_written, teardown_seen, teardown_success;
    bool teardown_attempted, teardown_sdk_quiescent, teardown_event_fenced;
    bool ambiguous, sdk_retired, event_fenced, physical_termination;
} esp32_mquickjs_wifi_twt_lane_t;

esp_err_t esp32_mquickjs_wifi_twt_reserve_individual(esp32_mquickjs_wifi_twt_identity_t *domain,
    esp32_mquickjs_wifi_twt_lane_t *lane, uint32_t generation,
    const esp32_mquickjs_wifi_itwt_options_t *options, esp32_mquickjs_wifi_twt_token_t *token);
esp_err_t esp32_mquickjs_wifi_twt_reserve_broadcast(esp32_mquickjs_wifi_twt_identity_t *domain,
    esp32_mquickjs_wifi_twt_lane_t *lane, uint32_t generation,
    const esp32_mquickjs_wifi_btwt_options_t *options, esp32_mquickjs_wifi_twt_token_t *token);
/* Copy requested before releasing the owner lock; call SDK with that private
 * copy. An SDK error after begin_submit is NOT evidence of native retirement. */
bool esp32_mquickjs_wifi_twt_begin_submit(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token);
bool esp32_mquickjs_wifi_twt_submitted(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, esp_err_t error,
    const esp32_mquickjs_wifi_twt_config_t *writeback);
/* Deliver native control events before publishing observer events. iTWT setup
 * matches its non-reused connection ID. bTWT has NO cookie: the caller must own
 * an exclusive native setup lane and prove prior native/event retirement.
 * A token invented at event arrival is not such proof. */
bool esp32_mquickjs_wifi_twt_observe_individual(esp32_mquickjs_wifi_twt_lane_t *lane,
    const wifi_event_sta_itwt_setup_t *event);
bool esp32_mquickjs_wifi_twt_observe_broadcast(esp32_mquickjs_wifi_twt_lane_t *lane,
    const wifi_event_sta_btwt_setup_t *event);
bool esp32_mquickjs_wifi_twt_observe_teardown(esp32_mquickjs_wifi_twt_lane_t *lane,
    esp32_mquickjs_wifi_twt_kind_t kind, unsigned agreement_id, unsigned status);
bool esp32_mquickjs_wifi_twt_request_close(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token);
bool esp32_mquickjs_wifi_twt_begin_teardown(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token);
bool esp32_mquickjs_wifi_twt_teardown_submitted(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, esp_err_t error);
/* Retry of a cookie-less teardown needs its own native TX/timer/callback drain,
 * followed by event drain at the same revision. The Agreement may remain live.
 * These consume proof; neither a failed event nor an SDK error supplies it. */
bool esp32_mquickjs_wifi_twt_teardown_quiescent(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision);
bool esp32_mquickjs_wifi_twt_teardown_fenced(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision);
/* These are proof consumers, not probes. A successful setup/teardown event,
 * elapsed timeout, disconnected flag or empty established-flow bitmap does
 * not prove absence of native pending operations/timers/callbacks. The Radio
 * adapter must supply an independently reviewed SDK retirement boundary, then
 * an exact default-event-loop fence. Any relevant delivery invalidates both. */
bool esp32_mquickjs_wifi_twt_retirement_revision(const esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t *revision);
bool esp32_mquickjs_wifi_twt_sdk_retired(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision);
bool esp32_mquickjs_wifi_twt_event_fenced(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision);
/* Only after actual driver deinit AND event drain, with no in-flight SDK call. */
bool esp32_mquickjs_wifi_twt_terminated(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token);
bool esp32_mquickjs_wifi_twt_release(esp32_mquickjs_wifi_twt_lane_t *lane,
    esp32_mquickjs_wifi_twt_token_t *token);
#endif
