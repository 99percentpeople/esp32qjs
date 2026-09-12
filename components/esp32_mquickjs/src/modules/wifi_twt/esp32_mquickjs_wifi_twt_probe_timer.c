#include "esp32_mquickjs_wifi_twt_probe_timer.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp32_mquickjs_wifi_twt_sdk.h"
#include "esp32_mquickjs_wifi_smartconfig_timer.h"
#include "esp32_mquickjs_wifi_chm_timer.h"
#include "esp32_mquickjs_wifi_nan_timer.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "rom/ets_sys.h"
#include "esp_private/wifi_os_adapter.h"
#include <stddef.h>
#include <string.h>

extern ETSTimer itwt_probe_timer;
void itwt_probe_timeout_fn(void *argument);
int ieee80211_timer_process(int signal, int operation, void *argument);
void __real_itwt_probe_timeout_fn_process(void *argument);
void __real_esp_coex_common_timer_setfn_wrapper(void *timer, void *callback, void *argument);
void __real_esp_coex_common_timer_disarm_wrapper(void *timer);
void __real_esp_coex_common_timer_done_wrapper(void *timer);
void __real_esp_coex_common_timer_arm_us_wrapper(void *timer, uint32_t us, bool repeat);
void __real_ets_timer_arm(ETSTimer *timer, uint32_t ms, bool repeat);
static DRAM_ATTR struct {
    esp32_mquickjs_wifi_twt_probe_timer_snapshot_t snapshot;
    uintptr_t node_identity; /* Compare only; never dereference this old address. */
} s_probe_timer;
static DRAM_ATTR portMUX_TYPE s_probe_timer_lock = portMUX_INITIALIZER_UNLOCKED;
_Static_assert(sizeof(uintptr_t) == 4, "reviewed C5 numeric timer argument");
_Static_assert(offsetof(wifi_osi_funcs_t, _timer_arm) == 224 && offsetof(wifi_osi_funcs_t, _timer_disarm) == 228 &&
    offsetof(wifi_osi_funcs_t, _timer_done) == 232 && offsetof(wifi_osi_funcs_t, _timer_setfn) == 236 &&
    offsetof(wifi_osi_funcs_t, _timer_arm_us) == 240,
    "reviewed C5 Wi-Fi timer dispatch table");

static void IRAM_ATTR probe_timer_invalidate(void)
{
    portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
    s_probe_timer.snapshot.current_identity = 0;
    /* Retain only the address VALUE for exact failure cleanup. It cannot
     * authorize a callback without a nonzero current_identity. */
    portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
}
static void IRAM_ATTR probe_timer_failed_locked(esp_err_t error, esp32_mquickjs_wifi_twt_probe_timer_stage_t stage)
{
    if (s_probe_timer.snapshot.fault == ESP_OK) {
        s_probe_timer.snapshot.fault = error;
        s_probe_timer.snapshot.fault_stage = stage;
        s_probe_timer.snapshot.failed_identity = s_probe_timer.snapshot.last_identity;
    }
    s_probe_timer.snapshot.current_identity = 0;
    s_probe_timer.snapshot.cleanup_pending = s_probe_timer.node_identity != 0U;
    if (stage == ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_STOP || stage == ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_DELETE)
        s_probe_timer.snapshot.cleanup_error = error;
}
static void IRAM_ATTR probe_timer_failed(esp_err_t error, esp32_mquickjs_wifi_twt_probe_timer_stage_t stage)
{
    portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
    probe_timer_failed_locked(error, stage);
    portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
}
static void probe_timer_callback(void *argument)
{
    /* esp_timer and the queued native message carry this number, never an
     * address into a node which may already have been freed/reused. */
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
    bool current = identity != 0U && s_probe_timer.snapshot.current_identity == identity &&
        !s_probe_timer.snapshot.fired;
    if (current) s_probe_timer.snapshot.fired = true;
    portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
    if (!current) return;
    int error = ieee80211_timer_process(7, 28, argument);
    if (error != 0) {
        portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
        if (s_probe_timer.snapshot.current_identity == identity && s_probe_timer.snapshot.post_error == 0) {
            s_probe_timer.snapshot.post_error = error;
            s_probe_timer.snapshot.failed_identity = identity;
            /* Do not mutate SDK state from esp_timer. A native finish/poll
             * observes this failure and may clean the exact pending probe. */
            probe_timer_failed_locked(ESP_FAIL, ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_POST);
        }
        portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
    }
}
void __wrap_esp_coex_common_timer_setfn_wrapper(void *timer, void *callback, void *argument)
{
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (esp32_mquickjs_wifi_nan_timer_setfn(timer, callback, argument)) return;
#endif
    if (timer != &itwt_probe_timer) {
        if (esp32_mquickjs_wifi_twt_setup_timer_setfn(timer, callback, argument)) return;
        if (esp32_mquickjs_wifi_btwt_timer_setfn(timer, callback, argument)) return;
        if (esp32_mquickjs_wifi_twt_information_timer_setfn(timer, callback, argument)) return;
        __real_esp_coex_common_timer_setfn_wrapper(timer, callback, argument);
        return;
    }
    uintptr_t node = 0;
    uint8_t phase = 0;
    bool valid = callback == (void *)itwt_probe_timeout_fn &&
        esp32_mquickjs_wifi_twt_sdk_probe_capture_native(argument, &node, &phase);
    portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
    uint32_t identity = 0;
    s_probe_timer.snapshot.current_identity = 0;
    s_probe_timer.node_identity = valid ? node : 0;
    if (valid) s_probe_timer.snapshot.phase = phase;
    esp_err_t error = s_probe_timer.snapshot.fault;
    esp32_mquickjs_wifi_twt_probe_timer_stage_t stage = ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_ARGUMENT;
    if (error == ESP_OK && (!valid || itwt_probe_timer.timer_arg != NULL)) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK && s_probe_timer.snapshot.post_error != 0) error = ESP_FAIL;
    if (error == ESP_OK && s_probe_timer.snapshot.last_identity == UINT32_MAX) {
        error = ESP_ERR_NO_MEM;
        stage = ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_IDENTITY;
    }
    if (error == ESP_OK) {
        identity = ++s_probe_timer.snapshot.last_identity;
        s_probe_timer.snapshot.current_identity = identity;
        s_probe_timer.snapshot.fired = false;
    }
    portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
    if (error != ESP_OK) {
        probe_timer_failed(error, stage);
        return;
    }
    /* Same TASK timer allocation as the legacy adapter, but no abort and no
     * borrowed node argument. Publish a handle only after create succeeds. */
    esp_timer_handle_t handle = NULL;
    const esp_timer_create_args_t args = {
        .callback = probe_timer_callback, .arg = (void *)(uintptr_t)identity,
        .dispatch_method = ESP_TIMER_TASK, .name = "TWT probe",
    };
    error = esp_timer_create(&args, &handle);
    if (error != ESP_OK) {
        probe_timer_failed(error, ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_CREATE);
        return;
    }
    memset(&itwt_probe_timer, 0, sizeof(itwt_probe_timer));
    itwt_probe_timer.timer_expire = UINT32_C(0x12121212);
    itwt_probe_timer.timer_arg = handle;
}
void IRAM_ATTR __wrap_esp_coex_common_timer_disarm_wrapper(void *timer)
{
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (esp32_mquickjs_wifi_nan_timer_disarm(timer)) return;
#endif
    if (timer != &itwt_probe_timer) {
        if (esp32_mquickjs_wifi_twt_setup_timer_disarm(timer)) return;
        if (esp32_mquickjs_wifi_btwt_timer_disarm(timer)) return;
        if (esp32_mquickjs_wifi_twt_information_timer_disarm(timer)) return;
        __real_esp_coex_common_timer_disarm_wrapper(timer);
        return;
    }
    probe_timer_invalidate();
    esp_timer_handle_t handle = itwt_probe_timer.timer_arg;
    if (handle != NULL) {
        esp_err_t error = esp_timer_stop(handle);
        /* An expired/unarmed TASK timer reports INVALID_STATE. Its callback
         * can still be running; invalidation above removes its authority. */
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE)
            probe_timer_failed(error, ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_STOP);
    }
}
void __wrap_esp_coex_common_timer_done_wrapper(void *timer)
{
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (esp32_mquickjs_wifi_nan_timer_done(timer)) return;
#endif
    if (timer != &itwt_probe_timer) {
        if (esp32_mquickjs_wifi_twt_setup_timer_done(timer)) return;
        if (esp32_mquickjs_wifi_btwt_timer_done(timer)) return;
        if (esp32_mquickjs_wifi_twt_information_timer_done(timer)) return;
        __real_esp_coex_common_timer_done_wrapper(timer);
        return;
    }
    probe_timer_invalidate();
    esp_timer_handle_t handle = itwt_probe_timer.timer_arg;
    if (handle == NULL) return;
    esp_err_t error = esp_timer_delete(handle);
    if (error != ESP_OK) {
        /* Never clear/overwrite a handle whose deletion was rejected. The
         * same disarm/done suffix can retry it; no new timer can replace it. */
        probe_timer_failed(error, ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_DELETE);
        return;
    }
    itwt_probe_timer.timer_arg = NULL;
    itwt_probe_timer.timer_expire = 0;
    portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
    s_probe_timer.snapshot.cleanup_error = ESP_OK;
    portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
}
static void IRAM_ATTR probe_timer_arm(uint64_t us, bool repeat)
{
    esp_timer_handle_t handle = itwt_probe_timer.timer_arg;
    portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
    bool valid = s_probe_timer.snapshot.current_identity != 0U && !s_probe_timer.snapshot.fired &&
        s_probe_timer.snapshot.fault == ESP_OK && s_probe_timer.snapshot.post_error == 0;
    portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
    if (!valid || handle == NULL || repeat) {
        probe_timer_failed(repeat ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE,
            ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_START);
        return;
    }
    esp_err_t error = esp_timer_stop(handle);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        probe_timer_failed(error, ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_STOP);
        return;
    }
    error = esp_timer_start_once(handle, us);
    if (error != ESP_OK) probe_timer_failed(error, ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_START);
}
void IRAM_ATTR __wrap_ets_timer_arm(ETSTimer *timer, uint32_t ms, bool repeat)
{
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (esp32_mquickjs_wifi_nan_timer_arm(timer, (uint64_t)ms * UINT64_C(1000), repeat)) return;
#endif
#if (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
    if (esp32qjs_wifi_chm_timer_arm(timer, (uint64_t)ms * UINT64_C(1000), repeat)) return;
#endif
#if CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
    if (esp32_mquickjs_wifi_smartconfig_timer_arm(timer, (uint64_t)ms * UINT64_C(1000), repeat)) return;
#endif
    if (timer != &itwt_probe_timer) {
        if (!esp32_mquickjs_wifi_twt_setup_timer_arm(timer, (uint64_t)ms * UINT64_C(1000), repeat) &&
            !esp32_mquickjs_wifi_btwt_timer_arm(timer, (uint64_t)ms * UINT64_C(1000), repeat) &&
            !esp32_mquickjs_wifi_twt_information_timer_arm(timer, (uint64_t)ms * UINT64_C(1000), repeat))
            __real_ets_timer_arm(timer, ms, repeat);
    }
    else probe_timer_arm((uint64_t)ms * UINT64_C(1000), repeat);
}
void IRAM_ATTR __wrap_esp_coex_common_timer_arm_us_wrapper(void *timer, uint32_t us, bool repeat)
{
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
    if (esp32_mquickjs_wifi_nan_timer_arm(timer, us, repeat)) return;
#endif
    if (timer != &itwt_probe_timer) {
        if (!esp32_mquickjs_wifi_twt_setup_timer_arm(timer, us, repeat) &&
            !esp32_mquickjs_wifi_btwt_timer_arm(timer, us, repeat) &&
            !esp32_mquickjs_wifi_twt_information_timer_arm(timer, us, repeat))
            __real_esp_coex_common_timer_arm_us_wrapper(timer, us, repeat);
    }
    else probe_timer_arm(us, repeat);
}
esp_err_t esp32_mquickjs_wifi_twt_probe_timer_finish_native(void)
{
    esp32_mquickjs_wifi_twt_probe_wake_snapshot_t wake;
    esp32_mquickjs_wifi_twt_probe_wake_snapshot(&wake);
    if (wake.fault != ESP_OK) {
        /* Ownership is ambiguous: no PM/node cleanup may consume it. */
        esp32_mquickjs_wifi_twt_probe_result_failed_native(wake.fault);
        return wake.fault;
    }
    portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
    esp_err_t error = s_probe_timer.snapshot.fault;
    uintptr_t node = s_probe_timer.node_identity;
    uint8_t phase = s_probe_timer.snapshot.phase;
    bool pending = s_probe_timer.snapshot.cleanup_pending;
    portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
    if (error == ESP_OK) return ESP_OK;
    esp32_mquickjs_wifi_twt_probe_result_failed_native(error);
    /* Native handlers have now finished creating/changing probe state. No
     * SDK mutation is done from esp_timer or a critical section. Numeric
     * callbacks/messages were invalidated before any handle cleanup. */
    __wrap_esp_coex_common_timer_disarm_wrapper(&itwt_probe_timer);
    __wrap_esp_coex_common_timer_done_wrapper(&itwt_probe_timer);
    if (pending && esp32_mquickjs_wifi_twt_sdk_probe_abort_native(node, phase)) {
        portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
        s_probe_timer.snapshot.cleanup_pending = false;
        s_probe_timer.node_identity = 0;
        portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
    }
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_probe_timer_cancel_native(void)
{
    esp32_mquickjs_wifi_twt_probe_wake_snapshot_t wake;
    esp32_mquickjs_wifi_twt_probe_wake_snapshot(&wake);
    if (wake.fault != ESP_OK) return wake.fault;
    if (wake.acquiring || wake.releasing) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
    uintptr_t node = s_probe_timer.node_identity;
    uint8_t phase = s_probe_timer.snapshot.phase;
    portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
    bool active = esp32_mquickjs_wifi_twt_sdk_probe_active_native();
    /* Exact result admission excludes another submit on this native queue.
     * Still require the saved current node/phase AND the independent wake
     * reference before touching active state. No old pointer is dereferenced.
     * A completed/disconnected probe may only leave its timer to delete. */
    if (active != wake.held || (active && !esp32_mquickjs_wifi_twt_sdk_probe_matches_native(node, phase)))
        return ESP_ERR_INVALID_STATE;
    __wrap_esp_coex_common_timer_disarm_wrapper(&itwt_probe_timer);
    __wrap_esp_coex_common_timer_done_wrapper(&itwt_probe_timer);
    if (active && !esp32_mquickjs_wifi_twt_sdk_probe_abort_native(node, phase)) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
    s_probe_timer.node_identity = 0;
    s_probe_timer.snapshot.cleanup_pending = false;
    esp_err_t error = s_probe_timer.snapshot.cleanup_error;
    portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
    /* esp_timer_delete only accepts retirement. Waiting for callback exit in
     * this task can deadlock with a timer posting through wifi_api_lock while
     * the caller holds that lock waiting for this ioctl. The external timer/
     * native fence, TX ledger and event fence remain separate obligations. */
    if (itwt_probe_timer.timer_arg != NULL) return error != ESP_OK ? error : ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
void __wrap_itwt_probe_timeout_fn_process(void *argument)
{
    uint32_t identity = (uint32_t)(uintptr_t)argument;
    portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
    bool current = identity != 0U && s_probe_timer.snapshot.current_identity == identity &&
        s_probe_timer.snapshot.fired;
    uintptr_t node = s_probe_timer.node_identity;
    uint8_t phase = s_probe_timer.snapshot.phase;
    if (current) {
        s_probe_timer.snapshot.current_identity = 0;
        s_probe_timer.node_identity = 0;
    }
    portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
    /* Only the native Wi-Fi task runs this and mutates the associated node.
     * Validate its CURRENT address, pending state and phase before invoking
     * the reviewed handler; a missing/replaced node never reaches its trap.
     * The native handler receives a stack byte, used synchronously only. */
    if (current && esp32_mquickjs_wifi_twt_sdk_probe_matches_native(node, phase))
        __real_itwt_probe_timeout_fn_process(&phase);
}
void esp32_mquickjs_wifi_twt_probe_timer_snapshot(esp32_mquickjs_wifi_twt_probe_timer_snapshot_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL_SAFE(&s_probe_timer_lock);
    *out = s_probe_timer.snapshot;
    portEXIT_CRITICAL_SAFE(&s_probe_timer_lock);
}
#endif
