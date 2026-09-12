#pragma once
#include "esp32_mquickjs_wifi_action_lane.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_wifi.h"
#include "esp32_mquickjs_wifi_radio.h"
#include <stddef.h>
/* Native stable requests only. No implicit init/start/mode/channel configuration.
 * Caller owns its span for the entire synchronous submission; SDK copies Action
 * bytes. Input rx/done callback, type and op_id must have the values below.
 * Successful admission returns an exact token even when SDK submission fails;
 * it remains owned until explicit cancellation/termination and retire.
 * These are internal APIs; public JS capture and lifecycle adapters follow. */
esp_err_t esp32_mquickjs_wifi_radio_action_send(wifi_action_tx_req_t *request,
    size_t request_bytes, esp32_mquickjs_wifi_action_token_t *token);
esp_err_t esp32_mquickjs_wifi_radio_action_roc(wifi_roc_req_t *request,
    esp32_mquickjs_wifi_action_token_t *token);
esp_err_t esp32_mquickjs_wifi_radio_action_cancel(const esp32_mquickjs_wifi_action_token_t *token);
/* Nonblocking retirement step. ESP_ERR_TIMEOUT means native terminal/fence is
 * still pending. out captures exact state before retirement; successful retire
 * releases the protected Radio lease and zeroes token. No retry of SDK submit. */
esp_err_t esp32_mquickjs_wifi_radio_action_retire(esp32_mquickjs_wifi_action_token_t *token,
    esp32_mquickjs_wifi_action_lane_t *out);
bool esp32_mquickjs_wifi_radio_action_status(const esp32_mquickjs_wifi_action_token_t *token,
    esp32_mquickjs_wifi_action_lane_t *out);
/* Internal physical recovery phases; no public entry or restore policy yet.
 * Admission requires the exact submitted operation and all managed Wi-Fi leases;
 * every unrelated owner, borrowed setting, wake lock and competing lifecycle is
 * rejected under the mutation mutex. It reserves lifecycle without SDK mutation.
 * Caller must preserve configuration intent, stop pending helper operations and
 * release managed Wi-Fi leases before STOP, then retire helpers/netifs before
 * shutdown. These functions cannot validate helper lifetime themselves.
 *
 * STOP success alone never terminates Action storage. Only successful deinit
 * after channel callback unregister/drain marks the exact operation terminated.
 * Shutdown returns TIMEOUT while that owner remains; its native worker must
 * retire the token outside this call. Generation stays unchanged until then.
 * Retry continues the suffix without repeating accepted STOP/deinit. Finish the
 * lifecycle or perform explicit validated restoration before publishing owners.
 * No force-release, implicit reconnect, configuration defaults or runtime reset. */
esp_err_t esp32_mquickjs_wifi_radio_begin_action_recovery(
    const esp32_mquickjs_wifi_radio_lease_t *application,
    const esp32_mquickjs_wifi_radio_lease_t *station,
    const esp32_mquickjs_wifi_radio_lease_t *access_point,
    const esp32_mquickjs_wifi_action_token_t *operation,
    esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t *mode);
/* Capture before STOP while the driver is healthy/readable and managed helper
 * transitions have quiesced. All managed leases must already be released. Uses
 * SDK home channel and complete saved PHY policy without starting/switching
 * bands. Reuses the existing secret checkpoint and policy record; failed reads
 * do not mutate the driver or discard an already frozen checkpoint. */
esp_err_t esp32_mquickjs_wifi_radio_checkpoint_action_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token, wifi_mode_t mode);
esp_err_t esp32_mquickjs_wifi_radio_stop_action_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_shutdown_action_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
/* After physical deinit and original owner retirement, remove only the recovery
 * exception. Keep the exact lifecycle/checkpoint for ordinary rebuild/replay.
 * Subsequent failures use ordinary central cleanup, including failed init. */
esp_err_t esp32_mquickjs_wifi_radio_finish_action_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
/* Runtime-task helper retirement only. This predicate preserves the exact
 * Action/ROC owner after proven STOP, including deinit/owner-drain retries.
 * It must never authorize configuration writes, helper creation or START. */
bool esp32_mquickjs_wifi_radio_action_recovery_active(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
esp_err_t esp32_mquickjs_wifi_radio_check_stopped_action_recovery(
    const esp32_mquickjs_wifi_radio_lifecycle_t *token);
/* Read-only global snapshot; no operation identity is granted to the caller. */
void esp32_mquickjs_wifi_radio_action_snapshot(esp32_mquickjs_wifi_action_lane_t *out);
#endif
