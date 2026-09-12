#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_err.h"
#include "esp_wifi_he_types.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    ESP32_MQUICKJS_WIFI_BTWT_TIMER_ARGUMENT = 1,
    ESP32_MQUICKJS_WIFI_BTWT_TIMER_IDENTITY,
    ESP32_MQUICKJS_WIFI_BTWT_TIMER_ALLOCATE,
    ESP32_MQUICKJS_WIFI_BTWT_TIMER_CREATE,
    ESP32_MQUICKJS_WIFI_BTWT_TIMER_START,
    ESP32_MQUICKJS_WIFI_BTWT_TIMER_STOP,
    ESP32_MQUICKJS_WIFI_BTWT_TIMER_DELETE,
    ESP32_MQUICKJS_WIFI_BTWT_TIMER_POST,
    ESP32_MQUICKJS_WIFI_BTWT_TIMER_NATIVE,
} esp32_mquickjs_wifi_btwt_timer_stage_t;
typedef struct {
    uintptr_t node; /* Compared only, never dereferenced after capture. */
    uint8_t parameter[17];
} esp32_mquickjs_wifi_btwt_timer_identity_t;
/* Value-only setup outcome. identity is the boot-unique TX request revision,
 * assigned BEFORE output (zero for a timer with no tracked setup). It survives
 * response -> dwell timer replacement; timer_identity names only the current
 * timer. complete is NOT TX/timer/Agreement retirement. No result is freed or
 * reused by a reader. A managed dispatch holds this result until the Agreement owner supplies
 * its native retirement proof; this is not that retirement coordinator. */
typedef struct {
    wifi_event_sta_btwt_setup_t event;
    uint32_t identity, timer_identity;
    esp_err_t native_error, observation_error, submit_error;
    uint32_t fence_sequence;
    bool seen : 1, complete : 1, ambiguous : 1, publishing : 1, tx_busy : 1, held : 1, cancel_requested : 1, cancelled : 1;
    bool fence_pending : 1, fence_posted : 1, fence_seen : 1, native_closed : 1;
} esp32_mquickjs_wifi_btwt_timer_result_t;
typedef struct {
    uint32_t last_identity, revision, reserved_bytes;
    uint32_t active_mask, fired_mask, busy_mask, fault_mask, cleanup_mask;
    uint32_t complete_mask, observation_error_mask;
    esp_err_t fault;
    uint8_t fault_slot, fault_stage;
} esp32_mquickjs_wifi_btwt_timer_snapshot_t;
/* True means the timer address belongs to this adapter, including failure.
 * Never fall back to the legacy borrowed-pointer/abort implementation. */
bool esp32_mquickjs_wifi_btwt_timer_setfn(void *timer, void *callback, void *argument);
bool esp32_mquickjs_wifi_btwt_timer_disarm(void *timer);
bool esp32_mquickjs_wifi_btwt_timer_done(void *timer);
bool esp32_mquickjs_wifi_btwt_timer_arm(void *timer, uint64_t us, bool repeat);
void esp32_mquickjs_wifi_btwt_timer_snapshot(esp32_mquickjs_wifi_btwt_timer_snapshot_t *);
esp_err_t esp32_mquickjs_wifi_btwt_timer_error(void);
/* Native task only; includes retained handles and publication pins. */
esp_err_t esp32_mquickjs_wifi_btwt_timer_available_native(unsigned slot);
/* Native TX completion binds the echoed dialog after SDK timer installation.
 * RX consumes that permission before deleting/replacing the response timer. */
void esp32_mquickjs_wifi_btwt_timer_bind_response_native(uintptr_t node, uint8_t dialog, const uint8_t parameter[17]);
int esp32_mquickjs_wifi_btwt_response_native(uintptr_t node, const uint8_t body[20]);
/* Native task only. Reserve value storage before handing an EB to output.
 * TX identity comes from the non-reused management ledger, never from events.
 * No SDK/heap call while locked. A failed allocation prevents RF submission. */
esp_err_t esp32_mquickjs_wifi_btwt_setup_begin_native(unsigned slot, uint32_t identity,
    uintptr_t node, const uint8_t parameter[17]);
void esp32_mquickjs_wifi_btwt_setup_submitted_native(unsigned slot, uint32_t identity, esp_err_t error);
void esp32_mquickjs_wifi_btwt_setup_tx_complete_native(unsigned slot, uint32_t identity,
    uintptr_t node, uint8_t dialog, const uint8_t parameter[17], uint8_t status);
/* Native setup dispatch pins the exact record before output. Held records
 * survive result completion and native connection close. Release will require
 * the joint retirement coordinator, not timeout or a bitmap observation. */
esp_err_t esp32_mquickjs_wifi_btwt_setup_hold_native(unsigned slot, uint32_t identity);
bool esp32_mquickjs_wifi_btwt_setup_held(unsigned slot);
/* Native queue: only the exact held, successfully established setup may
 * submit/control teardown. A connection close permanently revokes its node,
 * even if a later association reuses the same native address. */
bool esp32_mquickjs_wifi_btwt_setup_owner_native(unsigned slot, uint32_t identity, uintptr_t *node);
typedef struct { uint32_t tx_revision, timer_revision, teardown_revision; } esp32_mquickjs_wifi_btwt_cut_t;
/* Native queue only. Cancels a held pending setup, or retires a completed
 * teardown once its native result and TX/PM quiescence are known. Never sends
 * teardown or treats an established agreement as cancelled. Revokes local
 * permissions before stop/delete and retains storage on failure. A cancelled
 * result still needs the joint timer/native/event ordering proof. */
esp_err_t esp32_mquickjs_wifi_btwt_setup_cancel_native(unsigned slot, uint32_t identity);
esp_err_t esp32_mquickjs_wifi_btwt_setup_quiescent_native(unsigned slot, uint32_t identity,
    esp32_mquickjs_wifi_btwt_cut_t *out);
typedef struct {
    uint32_t slot, identity, sequence;
    esp32_mquickjs_wifi_btwt_cut_t cut;
} esp32_mquickjs_wifi_btwt_event_fence_t;
#define ESP32_MQUICKJS_WIFI_BTWT_FENCE_EVENT 6
/* After timer/native ordering and an exact quiet cut. Copied numbers only;
 * the boot Radio event handler never receives owner/runtime pointers. */
esp_err_t esp32_mquickjs_wifi_btwt_setup_post_fence(unsigned slot, uint32_t identity,
    const esp32_mquickjs_wifi_btwt_cut_t *cut, uint32_t *sequence);
void esp32_mquickjs_wifi_btwt_setup_observe_fence(const esp32_mquickjs_wifi_btwt_event_fence_t *);
/* Native proof consumer. Caller supplies prior timer/native ordering. Checks
 * setup/TX/teardown revisions and event marker before releasing teardown then
 * held. If state changes between those releases, held remains for a fresh cut.
 * Diagnostic setup values remain until a successor; the caller must retain a
 * teardown outcome snapshot before allowing its singleton to be released. */
esp_err_t esp32_mquickjs_wifi_btwt_setup_release_native(unsigned slot, uint32_t identity,
    const esp32_mquickjs_wifi_btwt_cut_t *cut, uint32_t sequence);
/* Worker-safe exact request lookup, unchanged output on mismatch. A successor
 * never makes an old token observe its result, including same broadcast ID. */
bool esp32_mquickjs_wifi_btwt_setup_result(unsigned slot, uint32_t identity,
    esp32_mquickjs_wifi_btwt_timer_result_t *out);
/* Copies under the timer lock; leaves output unchanged on identity mismatch. */
bool esp32_mquickjs_wifi_btwt_timer_result(unsigned slot, uint32_t identity,
    esp32_mquickjs_wifi_btwt_timer_result_t *out);
/* Native task only. Revokes the exact numeric callback before stop/delete.
 * Cleanup can retry its incomplete suffix. This is not TX/RF/event retirement. */
esp_err_t esp32_mquickjs_wifi_btwt_timer_cancel_native(unsigned slot, uint32_t identity);
bool esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(unsigned slot, uint8_t phase,
    const void *argument, esp32_mquickjs_wifi_btwt_timer_identity_t *out);
bool esp32_mquickjs_wifi_twt_sdk_broadcast_timer_matches_native(unsigned slot, uint8_t phase,
    const esp32_mquickjs_wifi_btwt_timer_identity_t *identity);
#endif
