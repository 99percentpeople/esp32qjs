#pragma once
#include "esp32_mquickjs_wifi_monitor_capture.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include "freertos/task.h"

#define ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS 8U
typedef struct {
    uint32_t generation;
    bool closed, close_requested, retirement_blocked;
    esp32_mquickjs_wifi_monitor_snapshot_t resources;
} esp32_mquickjs_wifi_monitor_diagnostic_t;
/* Copies all registered generations, including closed payload owners. The
 * temporary native references are released before returning; no JS/SDK calls. */
size_t esp32_mquickjs_wifi_monitor_diagnostics_snapshot(
    esp32_mquickjs_wifi_monitor_diagnostic_t output[ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS],
    uint32_t *unavailable);
/* Same temporary-reference rules as diagnostics; returns unretainable controls.
 * Only observation history is reset. Active leases/publishers/identity remain. */
uint32_t esp32_mquickjs_wifi_monitor_reset_counters(void);
typedef struct esp32_mquickjs_wifi_monitor_session esp32_mquickjs_wifi_monitor_session_t;
typedef JSValue (*esp32_mquickjs_wifi_monitor_make_frame_fn)(JSContext *ctx,
    const esp32_mquickjs_wifi_monitor_event_t *event, esp32_mquickjs_wifi_monitor_session_t *session);
struct esp32_mquickjs_wifi_monitor_session {
    esp32_mquickjs_wifi_monitor_resources_t resources;
    esp32_mquickjs_wifi_monitor_queue_t bridge;
    esp32_mquickjs_wifi_monitor_capture_t capture;
    esp32_mquickjs_runtime_t *runtime;
    TaskHandle_t task;
    esp32_mquickjs_wifi_monitor_make_frame_fn make_frame;
    _Atomic uint32_t references;
    _Atomic bool close_requested, closed, retirement_blocked;
    bool resources_initialized, cleanup_hold, reaper_registered, reaper_full;
    uint32_t generation;
};

/* Runtime task only. Creates stopped storage + queue without starting Radio.
 * Success returns a JS queue (caller roots it immediately) and one native
 * Session reference. Failure consumes all partial ownership or leaves it in
 * the bounded cleanup registry. No JS roots are kept by this native owner. */
JSValue esp32_mquickjs_wifi_monitor_session_new(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    const esp32_mquickjs_wifi_monitor_capture_options_t *options,
    uint32_t pool_capacity, uint32_t snap_length, bool require_complete, uint32_t queue_capacity,
    esp32_mquickjs_wifi_monitor_make_frame_fn make_frame, esp32_mquickjs_wifi_monitor_session_t **output);
/* Retain only an already-owned pointer. Each Frame/ref/source must retain this
 * context independently, then release its payload owner BEFORE this reference.
 * release may run off the JS task and never dereferences runtime or calls JS. */
bool esp32_mquickjs_wifi_monitor_session_retain(esp32_mquickjs_wifi_monitor_session_t *session);
void esp32_mquickjs_wifi_monitor_session_release(esp32_mquickjs_wifi_monitor_session_t *session);
/* JS finalizer/EventQueue close: only mark/notify; no SDK work here. */
void esp32_mquickjs_wifi_monitor_session_request_close(esp32_mquickjs_wifi_monitor_session_t *session);
esp_err_t esp32_mquickjs_wifi_monitor_session_start(esp32_mquickjs_wifi_monitor_session_t *session);
esp_err_t esp32_mquickjs_wifi_monitor_session_stop(esp32_mquickjs_wifi_monitor_session_t *session);
esp_err_t esp32_mquickjs_wifi_monitor_session_close(esp32_mquickjs_wifi_monitor_session_t *session);
/* Runtime task only. Both Sessions must be stopped, in the same runtime, with
 * no pending cleanup or RX/channel claim. Replacement must never have started.
 * Transfers the exact Radio lease, then closes the old queue/control without
 * driver calls. Old Frame/View owners keep their original pool and generation.
 * Caller owns both references and publishes the JS replacement without allocation
 * after success. Admission failure changes neither Session. */
esp_err_t esp32_mquickjs_wifi_monitor_session_replace(
    esp32_mquickjs_wifi_monitor_session_t *previous,
    esp32_mquickjs_wifi_monitor_session_t *replacement);
/* Run before core teardown waits for reapers. Closes native producers, retaining
 * payloads/contexts for JS/Future finalization. false forbids runtime disposal. */
bool esp32_mquickjs_prepare_wifi_monitor_runtime_destroy(esp32_mquickjs_runtime_t *runtime);
#endif
