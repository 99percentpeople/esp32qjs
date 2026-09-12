#include "esp32_mquickjs_wifi_action_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "esp32_mquickjs_wifi_twt_sdk.h"
#include "esp32_mquickjs_wifi_twt_information.h"
#include "esp32_mquickjs_wifi_nan_usd_sdk.h"
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
#include "esp32_mquickjs_wifi_nan_ndp.h"
#define ACTION_SDK_NAN_RELEASE_TYPE ((wifi_action_tx_t)(INT32_MAX - 10))
typedef struct {
    wifi_action_tx_req_t request;
    uint32_t identity;
    esp32_mquickjs_wifi_nan_ndp_status_t result;
} action_sdk_nan_release_t;
#endif
#if CONFIG_ESP_WIFI_DPP_SUPPORT
#include "esp32_mquickjs_wifi_dpp_result.h"
#endif

/* Exact fff9895c82 C3/S3/C5 archive layout, verified by the build-local SDK
 * archive hash gate in patch_idf_vendor_ie_context.cmake. Do not generalize to
 * an unreviewed SDK. The wrappers run in the native ioctl execution context,
 * immediately before cancellation, not on the JS/default event-loop task. */
_Static_assert(sizeof(void *) == 4, "reviewed SDK ioctl pointer width");
_Static_assert(offsetof(wifi_action_tx_req_t, rx_cb) == 32 &&
    offsetof(wifi_action_tx_req_t, op_id) == 36 && sizeof(wifi_action_tx_req_t) == 48, "reviewed Action request layout");
_Static_assert(offsetof(wifi_roc_req_t, rx_cb) == 20 &&
    offsetof(wifi_roc_req_t, op_id) == 24 && offsetof(wifi_roc_req_t, done_cb) == 28 &&
    sizeof(wifi_roc_req_t) == 36, "reviewed ROC request layout");
#define ACTION_SDK_FENCE_TYPE ((wifi_action_tx_t)INT32_MAX)
#define ACTION_SDK_QUIESCENT_TYPE ((wifi_action_tx_t)(INT32_MAX - 1))
#define ACTION_SDK_SAVED_PHY_TYPE ((wifi_action_tx_t)(INT32_MAX - 2))
#if CONFIG_SOC_WIFI_HE_SUPPORT
#define ACTION_SDK_TWT_SNAPSHOT_TYPE ((wifi_action_tx_t)(INT32_MAX - 3))
#define ACTION_SDK_TWT_PROBE_CANCEL_TYPE ((wifi_action_tx_t)(INT32_MAX - 4))
typedef struct {
    wifi_action_tx_req_t request;
    esp32_mquickjs_wifi_twt_sdk_snapshot_t result;
} action_sdk_twt_request_t;
typedef struct {
    wifi_action_tx_req_t request;
    uint32_t identity;
} action_sdk_twt_probe_cancel_request_t;
#if CONFIG_IDF_TARGET_ESP32C5
#define ACTION_SDK_TWT_PROBE_CONTROL_TYPE ((wifi_action_tx_t)(INT32_MAX - 5))
#define ACTION_SDK_TWT_SETUP_CANCEL_TYPE ((wifi_action_tx_t)(INT32_MAX - 6))
#define ACTION_SDK_TWT_SETUP_CONTROL_TYPE ((wifi_action_tx_t)(INT32_MAX - 7))
#define ACTION_SDK_TWT_BROADCAST_SNAPSHOT_TYPE ((wifi_action_tx_t)(INT32_MAX - 8))
#define ACTION_SDK_BTWT_CONTROL_TYPE ((wifi_action_tx_t)(INT32_MAX - 9))
enum { BTWT_CANCEL, BTWT_QUIESCENT, BTWT_RELEASE, BTWT_TEARDOWN };
typedef struct {
    wifi_action_tx_req_t request;
    uint32_t command, slot, identity, sequence;
    esp32_mquickjs_wifi_btwt_cut_t cut;
} action_sdk_btwt_control_t;
typedef struct {
    wifi_action_tx_req_t request;
    esp32_mquickjs_wifi_twt_broadcast_snapshot_t result;
} action_sdk_twt_broadcast_request_t;
enum { TWT_SETUP_QUIESCENT, TWT_SETUP_RELEASE, TWT_SETUP_TEARDOWN, TWT_TEARDOWN_TX_QUIESCENT,
    TWT_TEARDOWN_TX_RELEASE, TWT_INFORMATION_SUBMIT, TWT_INFORMATION_REAP, TWT_INFORMATION_RESUME };
typedef struct {
    wifi_action_tx_req_t request;
    uint32_t command, identity, sequence, flow;
    esp32_mquickjs_wifi_twt_setup_cut_t cut;
} action_sdk_twt_setup_control_t;
enum { TWT_PROBE_SUBMIT, TWT_PROBE_QUIESCENT, TWT_PROBE_RELEASE };
typedef struct {
    wifi_action_tx_req_t request;
    uint32_t command, identity, timeout_ms, sequence;
    esp32_mquickjs_wifi_twt_probe_cut_t cut;
} action_sdk_twt_probe_control_t;
#endif
#endif
extern uint8_t g_offchan_ctx[28];
esp_err_t __real_wifi_action_tx_process(void *message);
esp_err_t __real_wifi_roc_process(void *message);
#if CONFIG_IDF_TARGET_ESP32C5
_Static_assert(sizeof(wifi_protocols_t) == 4 && offsetof(wifi_protocols_t, ghz_5g) == 2 &&
    sizeof(wifi_bandwidth_t) == 4 && sizeof(wifi_bandwidths_t) == 8 &&
    offsetof(wifi_bandwidths_t, ghz_5g) == 4, "reviewed C5 saved PHY output widths");
/* The reviewed handlers read byte 8 (ifx), word 12 (2g output), word 20
 * (5g output). Unlike the public wrappers they do not filter by band mode.
 * They read saved SDK policy and only write the supplied output spans. */
typedef struct {
    uint8_t prefix[8], interface, reserved[3];
    void *ghz_2g;
    uint32_t unused;
    void *ghz_5g;
} action_sdk_phy_message_t;
_Static_assert(offsetof(action_sdk_phy_message_t, interface) == 8 &&
    offsetof(action_sdk_phy_message_t, ghz_2g) == 12 && offsetof(action_sdk_phy_message_t, ghz_5g) == 20 &&
    sizeof(action_sdk_phy_message_t) == 24, "reviewed C5 saved PHY ioctl layout");
/* Like the existing Action options capture, this embeds the native variable-tail
 * request in private fixed-size storage. It is never submitted as an RF frame. */
typedef struct {
    wifi_action_tx_req_t request;
    esp32_mquickjs_wifi_saved_phy_t result;
} action_sdk_phy_request_t;
esp_err_t wifi_get_protocols_process(void *message);
esp_err_t wifi_get_bw_process(void *message);

static esp_err_t action_sdk_saved_phy_process(action_sdk_phy_request_t *request)
{
    if (request->request.ifx != WIFI_IF_STA && request->request.ifx != WIFI_IF_AP) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (request->request.ifx == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
    esp32_mquickjs_wifi_saved_phy_t result = {0};
    action_sdk_phy_message_t message = {.interface = (uint8_t)request->request.ifx,
        .ghz_2g = &result.protocols.ghz_2g, .ghz_5g = &result.protocols.ghz_5g};
    esp_err_t err = wifi_get_protocols_process(&message);
    if (err != ESP_OK) return err;
    message.ghz_2g = &result.bandwidths.ghz_2g;
    message.ghz_5g = &result.bandwidths.ghz_5g;
    err = wifi_get_bw_process(&message);
    if (err == ESP_OK) request->result = result;
    return err;
}
#endif


int esp32_mquickjs_wifi_action_receive(uint8_t *header, uint8_t *payload, size_t length, uint8_t channel)
{
    (void)header; (void)payload; (void)length; (void)channel;
    return 0;
}

static bool action_sdk_owner(wifi_action_rx_cb_t callback, wifi_interface_t interface,
    uint8_t channel, wifi_second_chan_t secondary, uint8_t operation_id)
{
    uint32_t current_context, current_interface, current_secondary;
    memcpy(&current_context, g_offchan_ctx + 4, sizeof(current_context));
    memcpy(&current_interface, g_offchan_ctx + 8, sizeof(current_interface));
    memcpy(&current_secondary, g_offchan_ctx + 20, sizeof(current_secondary));
    return current_context == (uint32_t)(uintptr_t)callback && current_interface == (uint32_t)interface &&
        g_offchan_ctx[2] == channel && current_secondary == (uint32_t)secondary && g_offchan_ctx[12] == operation_id;
}

static bool action_sdk_guarded_callback(wifi_action_rx_cb_t callback)
{
    if (callback == esp32_mquickjs_wifi_action_receive) return true;
#if CONFIG_ESP_WIFI_DPP_SUPPORT
    if (esp32qjs_dpp_is_action_callback((uintptr_t)callback)) return true;
#endif
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    if (esp32qjs_nan_usd_is_action_callback((uintptr_t)callback)) return true;
#endif
    return false;
}

esp_err_t __wrap_wifi_action_tx_process(void *message)
{
    wifi_action_tx_req_t *request;
    if (message == NULL) return ESP_ERR_INVALID_ARG;
    memcpy(&request, (const uint8_t *)message + 20, sizeof(request));
    if (request == NULL) return ESP_ERR_INVALID_ARG;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (request->type == ACTION_SDK_NAN_RELEASE_TYPE && request->rx_cb == esp32_mquickjs_wifi_action_receive) {
        if (request->ifx != WIFI_IF_NAN) return ESP_ERR_INVALID_ARG;
        action_sdk_nan_release_t *release = (action_sdk_nan_release_t *)request;
        return esp32_mquickjs_wifi_nan_sdk_ndp_release_native(release->identity, &release->result);
    }
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (request->type == ACTION_SDK_BTWT_CONTROL_TYPE && request->rx_cb == esp32_mquickjs_wifi_action_receive) {
        if (request->ifx != WIFI_IF_STA) return ESP_ERR_INVALID_ARG;
        action_sdk_btwt_control_t *control = (action_sdk_btwt_control_t *)request;
        if (control->command == BTWT_TEARDOWN)
            return esp32_mquickjs_wifi_twt_sdk_broadcast_teardown_native(control->slot, control->identity);
        if (control->command == BTWT_CANCEL)
            return esp32_mquickjs_wifi_btwt_setup_cancel_native(control->slot, control->identity);
        if (control->command == BTWT_QUIESCENT)
            return esp32_mquickjs_wifi_btwt_setup_quiescent_native(control->slot, control->identity, &control->cut);
        if (control->command == BTWT_RELEASE)
            return esp32_mquickjs_wifi_btwt_setup_release_native(control->slot, control->identity, &control->cut, control->sequence);
        return ESP_ERR_INVALID_ARG;
    }
    if (request->type == ACTION_SDK_TWT_BROADCAST_SNAPSHOT_TYPE && request->rx_cb == esp32_mquickjs_wifi_action_receive) {
        if (request->ifx != WIFI_IF_STA) return ESP_ERR_INVALID_ARG;
        return esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot_native(&((action_sdk_twt_broadcast_request_t *)request)->result);
    }
    if (request->type == ACTION_SDK_TWT_SETUP_CONTROL_TYPE && request->rx_cb == esp32_mquickjs_wifi_action_receive) {
        if (request->ifx != WIFI_IF_STA) return ESP_ERR_INVALID_ARG;
        action_sdk_twt_setup_control_t *control = (action_sdk_twt_setup_control_t *)request;
        if (control->command == TWT_INFORMATION_SUBMIT || control->command == TWT_INFORMATION_RESUME)
            return esp32_mquickjs_wifi_twt_sdk_information_submit_native(control->identity, control->flow,
                control->command == TWT_INFORMATION_RESUME, &control->sequence);
        if (control->command == TWT_INFORMATION_REAP)
            return esp32_mquickjs_wifi_twt_information_reap_native(control->identity);
        if (control->command == TWT_SETUP_QUIESCENT)
            return esp32_mquickjs_wifi_twt_sdk_setup_quiescent_native(control->identity, &control->cut);
        if (control->command == TWT_SETUP_RELEASE)
            return esp32_mquickjs_wifi_twt_sdk_setup_release_native(control->identity, &control->cut, control->sequence);
        if (control->command == TWT_SETUP_TEARDOWN) {
            if (control->flow > 7U) return ESP_ERR_INVALID_ARG;
            return esp32_mquickjs_wifi_twt_sdk_setup_teardown_native(control->identity, (uint8_t)control->flow);
        }
        if (control->command == TWT_TEARDOWN_TX_QUIESCENT)
            return esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(control->identity, &control->sequence);
        if (control->command == TWT_TEARDOWN_TX_RELEASE)
            return esp32_mquickjs_wifi_twt_teardown_tx_release_native(control->identity, control->sequence)
                ? ESP_OK : ESP_ERR_NOT_FINISHED;
        return ESP_ERR_INVALID_ARG;
    }
    if (request->type == ACTION_SDK_TWT_SETUP_CANCEL_TYPE && request->rx_cb == esp32_mquickjs_wifi_action_receive) {
        if (request->ifx != WIFI_IF_STA) return ESP_ERR_INVALID_ARG;
        return esp32_mquickjs_wifi_twt_sdk_setup_cancel_native(((action_sdk_twt_probe_cancel_request_t *)request)->identity);
    }
    if (request->type == ACTION_SDK_TWT_PROBE_CONTROL_TYPE && request->rx_cb == esp32_mquickjs_wifi_action_receive) {
        if (request->ifx != WIFI_IF_STA) return ESP_ERR_INVALID_ARG;
        action_sdk_twt_probe_control_t *control = (action_sdk_twt_probe_control_t *)request;
        if (control->command == TWT_PROBE_SUBMIT)
            return esp32_mquickjs_wifi_twt_sdk_probe_submit_native(control->timeout_ms, &control->identity);
        if (control->command == TWT_PROBE_QUIESCENT)
            return esp32_mquickjs_wifi_twt_sdk_probe_quiescent_native(control->identity, &control->cut);
        if (control->command == TWT_PROBE_RELEASE)
            return esp32_mquickjs_wifi_twt_sdk_probe_release_native(control->identity, &control->cut, control->sequence);
        return ESP_ERR_INVALID_ARG;
    }
    if (request->type == ACTION_SDK_TWT_PROBE_CANCEL_TYPE && request->rx_cb == esp32_mquickjs_wifi_action_receive) {
        if (request->ifx != WIFI_IF_STA) return ESP_ERR_INVALID_ARG;
        return esp32_mquickjs_wifi_twt_sdk_probe_cancel_native(((action_sdk_twt_probe_cancel_request_t *)request)->identity);
    }
    if (request->type == ACTION_SDK_TWT_SNAPSHOT_TYPE && request->rx_cb == esp32_mquickjs_wifi_action_receive)
        return esp32_mquickjs_wifi_twt_sdk_snapshot_native(&((action_sdk_twt_request_t *)request)->result);
#endif
#if CONFIG_IDF_TARGET_ESP32C5
    if (request->type == ACTION_SDK_SAVED_PHY_TYPE && request->rx_cb == esp32_mquickjs_wifi_action_receive)
        return action_sdk_saved_phy_process((action_sdk_phy_request_t *)request);
#endif
    if (request->type == ACTION_SDK_FENCE_TYPE && request->rx_cb == esp32_mquickjs_wifi_action_receive) return ESP_OK;
    if (request->type == ACTION_SDK_QUIESCENT_TYPE && request->rx_cb == esp32_mquickjs_wifi_action_receive) {
        /* Reviewed C3/S3/C5 end/failure paths clear all 28 bytes after native
         * release. Queue serialization excludes transient memset during submit.
         * A foreign/nonzero record is not evidence our resources are retired. */
        uint8_t present = 0;
        for (size_t i = 0; i < sizeof(g_offchan_ctx); ++i) present |= g_offchan_ctx[i];
        return present ? ESP_ERR_TIMEOUT : ESP_OK;
    }
    if (request->type == WIFI_OFFCHAN_TX_CANCEL && action_sdk_guarded_callback(request->rx_cb) &&
        !action_sdk_owner(request->rx_cb, request->ifx, request->channel, request->sec_channel, request->op_id))
        return ESP_ERR_INVALID_STATE;
    /* Other modules retain their native behavior. The SDK still checks the
     * current channel-manager operation kind before chm_cancel_op(). */
    return __real_wifi_action_tx_process(message);
}

esp_err_t __wrap_wifi_roc_process(void *message)
{
    wifi_roc_req_t *request;
    if (message == NULL) return ESP_ERR_INVALID_ARG;
    memcpy(&request, (const uint8_t *)message + 20, sizeof(request));
    if (request == NULL) return ESP_ERR_INVALID_ARG;
    if (request->type == WIFI_ROC_CANCEL && action_sdk_guarded_callback(request->rx_cb) &&
        !action_sdk_owner(request->rx_cb, request->ifx, request->channel, request->sec_channel, request->op_id))
        return ESP_ERR_INVALID_STATE;
    return __real_wifi_roc_process(message);
}
esp_err_t esp32_mquickjs_wifi_action_sdk_fence(void)
{
    wifi_action_tx_req_t request = {
        .ifx = WIFI_IF_STA, .type = ACTION_SDK_FENCE_TYPE,
        .rx_cb = esp32_mquickjs_wifi_action_receive,
    };
    /* The API wrapper validates ifx, then dispatches to our native ioctl shim.
     * No global-record read or driver mutation occurs in the sentinel branch. */
    return esp_wifi_action_tx_req(&request);
}

#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_release(uint32_t identity,
    esp32_mquickjs_wifi_nan_ndp_status_t *status)
{
    if (!identity || !status) return ESP_ERR_INVALID_ARG;
    action_sdk_nan_release_t request = {.request = {.ifx = WIFI_IF_NAN,
        .type = ACTION_SDK_NAN_RELEASE_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .identity = identity};
    /* The pinned API validates initialization and interface, then invokes
     * our shim synchronously on the Wi-Fi task. No RF Action is submitted. */
    esp_err_t error = esp_wifi_action_tx_req(&request.request);
    if (error == ESP_OK) *status = request.result;
    return error;
}
#endif

esp_err_t esp32_mquickjs_wifi_action_sdk_quiescent(void)
{
    wifi_action_tx_req_t request = {
        .ifx = WIFI_IF_STA, .type = ACTION_SDK_QUIESCENT_TYPE,
        .rx_cb = esp32_mquickjs_wifi_action_receive,
    };
    return esp_wifi_action_tx_req(&request);
}

esp_err_t esp32_mquickjs_wifi_action_sdk_saved_phy(wifi_interface_t interface,
    esp32_mquickjs_wifi_saved_phy_t *output)
{
    if (output == NULL || (interface != WIFI_IF_STA && interface != WIFI_IF_AP)) return ESP_ERR_INVALID_ARG;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    if (interface == WIFI_IF_AP) return ESP_ERR_NOT_SUPPORTED;
#endif
#if CONFIG_IDF_TARGET_ESP32C5
    action_sdk_phy_request_t request = {.request = {
        .ifx = interface, .type = ACTION_SDK_SAVED_PHY_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive}};
    esp_err_t err = esp_wifi_action_tx_req(&request.request);
    if (err == ESP_OK) *output = request.result;
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#if CONFIG_SOC_WIFI_HE_SUPPORT
#if CONFIG_IDF_TARGET_ESP32C5
static action_sdk_twt_probe_control_t action_sdk_probe_control(unsigned command, uint32_t identity)
{
    return (action_sdk_twt_probe_control_t){.request = {
        .ifx = WIFI_IF_STA, .type = ACTION_SDK_TWT_PROBE_CONTROL_TYPE,
        .rx_cb = esp32_mquickjs_wifi_action_receive}, .command = command, .identity = identity};
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_submit(uint32_t timeout_ms, uint32_t *identity)
{
    if (identity == NULL || *identity != 0U || timeout_ms == 0U || timeout_ms > 60000U) return ESP_ERR_INVALID_ARG;
    action_sdk_twt_probe_control_t control = action_sdk_probe_control(TWT_PROBE_SUBMIT, 0);
    control.timeout_ms = timeout_ms;
    esp_err_t error = esp_wifi_action_tx_req(&control.request);
    /* Accepted native storage must be returned even if submission failed. */
    if (control.identity != 0U) *identity = control.identity;
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_quiescent(uint32_t identity, esp32_mquickjs_wifi_twt_probe_cut_t *cut)
{
    if (identity == 0U || cut == NULL) return ESP_ERR_INVALID_ARG;
    action_sdk_twt_probe_control_t control = action_sdk_probe_control(TWT_PROBE_QUIESCENT, identity);
    esp_err_t error = esp_wifi_action_tx_req(&control.request);
    if (error == ESP_OK) *cut = control.cut;
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_release(uint32_t identity,
    const esp32_mquickjs_wifi_twt_probe_cut_t *cut, uint32_t sequence)
{
    if (identity == 0U || cut == NULL || sequence == 0U) return ESP_ERR_INVALID_ARG;
    action_sdk_twt_probe_control_t control = action_sdk_probe_control(TWT_PROBE_RELEASE, identity);
    control.cut = *cut;
    control.sequence = sequence;
    return esp_wifi_action_tx_req(&control.request);
}
#endif
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_cancel(uint32_t identity)
{
    if (identity == 0U) return ESP_ERR_INVALID_ARG;
#if CONFIG_IDF_TARGET_ESP32C5
    action_sdk_twt_probe_cancel_request_t request = {.request = {
        .ifx = WIFI_IF_STA, .type = ACTION_SDK_TWT_PROBE_CANCEL_TYPE,
        .rx_cb = esp32_mquickjs_wifi_action_receive}, .identity = identity};
    return esp_wifi_action_tx_req(&request.request);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
#if CONFIG_IDF_TARGET_ESP32C5
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_teardown(unsigned slot, uint32_t identity)
{
    if (slot >= 32 || !identity) return ESP_ERR_INVALID_ARG;
    action_sdk_btwt_control_t control = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_BTWT_CONTROL_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .command = BTWT_TEARDOWN, .slot = slot, .identity = identity};
    return esp_wifi_action_tx_req(&control.request);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_cancel(unsigned slot, uint32_t identity)
{
    if (slot >= 32 || !identity) return ESP_ERR_INVALID_ARG;
    action_sdk_btwt_control_t control = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_BTWT_CONTROL_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .command = BTWT_CANCEL, .slot = slot, .identity = identity};
    return esp_wifi_action_tx_req(&control.request);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_quiescent(unsigned slot, uint32_t identity, esp32_mquickjs_wifi_btwt_cut_t *out)
{
    if (slot >= 32 || !identity || out == NULL) return ESP_ERR_INVALID_ARG;
    action_sdk_btwt_control_t control = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_BTWT_CONTROL_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .command = BTWT_QUIESCENT, .slot = slot, .identity = identity};
    esp_err_t error = esp_wifi_action_tx_req(&control.request);
    if (error == ESP_OK) *out = control.cut;
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_release(unsigned slot, uint32_t identity,
    const esp32_mquickjs_wifi_btwt_cut_t *cut, uint32_t sequence)
{
    if (slot >= 32 || !identity || cut == NULL || !sequence) return ESP_ERR_INVALID_ARG;
    action_sdk_btwt_control_t control = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_BTWT_CONTROL_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .command = BTWT_RELEASE, .slot = slot, .identity = identity, .sequence = sequence, .cut = *cut};
    return esp_wifi_action_tx_req(&control.request);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_cancel(uint32_t identity)
{
    if (identity == 0U) return ESP_ERR_INVALID_ARG;
    action_sdk_twt_probe_cancel_request_t request = {.request = {
        .ifx = WIFI_IF_STA, .type = ACTION_SDK_TWT_SETUP_CANCEL_TYPE,
        .rx_cb = esp32_mquickjs_wifi_action_receive}, .identity = identity};
    return esp_wifi_action_tx_req(&request.request);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_quiescent(uint32_t identity, esp32_mquickjs_wifi_twt_setup_cut_t *cut)
{
    if (identity == 0U || cut == NULL) return ESP_ERR_INVALID_ARG;
    action_sdk_twt_setup_control_t control = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_TWT_SETUP_CONTROL_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .command = TWT_SETUP_QUIESCENT, .identity = identity};
    esp_err_t error = esp_wifi_action_tx_req(&control.request);
    if (error == ESP_OK) *cut = control.cut;
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_teardown(uint32_t identity, uint8_t flow)
{
    if (identity == 0U || flow > 7U) return ESP_ERR_INVALID_ARG;
    action_sdk_twt_setup_control_t control = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_TWT_SETUP_CONTROL_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .command = TWT_SETUP_TEARDOWN, .identity = identity, .flow = flow};
    return esp_wifi_action_tx_req(&control.request);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_information_submit(uint32_t setup_identity, uint32_t duration_ms, bool resume, uint32_t *identity)
{
    if (!setup_identity || identity == NULL || (resume && duration_ms) || duration_ms > ESP32_MQUICKJS_WIFI_TWT_SUSPEND_MAX_MS)
        return ESP_ERR_INVALID_ARG;
    action_sdk_twt_setup_control_t control = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_TWT_SETUP_CONTROL_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .command = resume ? TWT_INFORMATION_RESUME : TWT_INFORMATION_SUBMIT, .identity = setup_identity, .flow = duration_ms};
    esp_err_t error = esp_wifi_action_tx_req(&control.request);
    *identity = control.sequence; /* Accepted native failures still retain their operation. */
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_information_reap(uint32_t setup_identity)
{
    action_sdk_twt_setup_control_t control = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_TWT_SETUP_CONTROL_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .command = TWT_INFORMATION_REAP, .identity = setup_identity};
    return esp_wifi_action_tx_req(&control.request);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_teardown_tx_quiescent(uint32_t identity, uint32_t *revision)
{
    if (identity == 0U || revision == NULL) return ESP_ERR_INVALID_ARG;
    action_sdk_twt_setup_control_t control = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_TWT_SETUP_CONTROL_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .command = TWT_TEARDOWN_TX_QUIESCENT, .identity = identity};
    esp_err_t error = esp_wifi_action_tx_req(&control.request);
    if (error == ESP_OK) *revision = control.sequence;
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_teardown_tx_release(uint32_t identity, uint32_t revision)
{
    if (identity == 0U || revision == 0U || revision == UINT32_MAX) return ESP_ERR_INVALID_ARG;
    action_sdk_twt_setup_control_t control = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_TWT_SETUP_CONTROL_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .command = TWT_TEARDOWN_TX_RELEASE, .identity = identity, .sequence = revision};
    return esp_wifi_action_tx_req(&control.request);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_release(uint32_t identity,
    const esp32_mquickjs_wifi_twt_setup_cut_t *cut, uint32_t sequence)
{
    if (identity == 0U || cut == NULL || sequence == 0U) return ESP_ERR_INVALID_ARG;
    action_sdk_twt_setup_control_t control = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_TWT_SETUP_CONTROL_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive},
        .command = TWT_SETUP_RELEASE, .identity = identity, .sequence = sequence, .cut = *cut};
    return esp_wifi_action_tx_req(&control.request);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(esp32_mquickjs_wifi_twt_broadcast_snapshot_t *output)
{
    if (output == NULL) return ESP_ERR_INVALID_ARG;
    action_sdk_twt_broadcast_request_t request = {.request = {.ifx = WIFI_IF_STA,
        .type = ACTION_SDK_TWT_BROADCAST_SNAPSHOT_TYPE, .rx_cb = esp32_mquickjs_wifi_action_receive}};
    esp_err_t error = esp_wifi_action_tx_req(&request.request);
    if (error == ESP_OK) *output = request.result;
    return error;
}
#endif

esp_err_t esp32_mquickjs_wifi_twt_sdk_snapshot(esp32_mquickjs_wifi_twt_sdk_snapshot_t *output)
{
    if (output == NULL) return ESP_ERR_INVALID_ARG;
#if CONFIG_IDF_TARGET_ESP32C5
    action_sdk_twt_request_t request = {.request = {
        .ifx = WIFI_IF_STA, .type = ACTION_SDK_TWT_SNAPSHOT_TYPE,
        .rx_cb = esp32_mquickjs_wifi_action_receive}};
    esp_err_t error = esp_wifi_action_tx_req(&request.request);
    if (error == ESP_OK) *output = request.result;
    return error;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
#endif

#endif
