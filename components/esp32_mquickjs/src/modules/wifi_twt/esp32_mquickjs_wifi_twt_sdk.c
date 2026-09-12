#include "esp32_mquickjs_wifi_twt_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT
#include "esp32_mquickjs_wifi_twt_options.h"
#include "esp_wifi.h"
#include <stddef.h>
#include <string.h>

#if CONFIG_IDF_TARGET_ESP32C5
#include "esp32_mquickjs_wifi_twt_controls.h"
struct os_reltime;
#include "utils/eloop.h"
bool current_task_is_wifi_task(void);
#include "rom/ets_sys.h"
#include "esp32_mquickjs_wifi_twt_setup_submit.h"
#include "esp32_mquickjs_wifi_twt_broadcast_submit.h"
#include "esp32_mquickjs_wifi_twt_information.h"
/* Pinned fff9895c82 C5 libnet80211.a, gated by the existing build-local archive
 * SHA-256 check. These are exported symbols, not addresses of private pointers.
 * Native structure sizes/offsets are independently checked in the SDK evidence.
 * setup_timer_param: 8 x 17-byte request, 8 dialog bytes, 8 ETSTimer,
 * 8 uint16 timeouts, 8 int16 IDs, 8 opaque uint32 words, pending bitmap. */
extern uint8_t setup_timer_param[372];
extern uint8_t btwt_setup_timer[1248];
extern int16_t s_itwt_id[8], s_tmp_itwt_id[8];
extern uint8_t s_itwt_flow_id_bitmap, s_itwt_suspend_flow_id_bitmap, s_itwt_resume_flow_id_bitmap;
extern uint32_t s_btwt_id_bitmap;
extern ETSTimer itwt_information_timer[8], itwt_probe_timer;
extern uint8_t g_ic[];
extern uint8_t g_pm_cfg[];
void pm_twt_set_config(wifi_twt_config_t *config);
esp_err_t ieee80211_itwt_get_flow_id_status(int *bitmap);
esp_err_t wifi_sta_itwt_set_target_wake_time_offset_process(void *message);
_Static_assert(sizeof(wifi_twt_config_t) == 2 && offsetof(wifi_twt_config_t, twt_enable_keep_alive) == 1,
    "reviewed C5 TWT policy layout");

typedef struct {
    esp32_mquickjs_wifi_twt_control_kind_t kind;
    esp32_mquickjs_wifi_twt_control_t value;
    bool entered;
    esp_err_t error;
} twt_control_command_t;

static int twt_control_dispatch(void *opaque, void *unused)
{
    (void)unused;
    twt_control_command_t *command = opaque;
    command->entered = true;
    command->error = ESP_ERR_INVALID_ARG;
    if (command->kind == ESP32_MQUICKJS_WIFI_TWT_READ_CONFIG ||
        command->kind == ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG) {
        /* Fixed pm_twt_set_config stores exactly these two bytes. A native
         * queue callback must not recursively call the public ioctl wrapper. */
        if (command->kind == ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG)
            pm_twt_set_config(&command->value.config);
        if (g_pm_cfg[84] > 1 || g_pm_cfg[85] > 1) return command->error = ESP_ERR_INVALID_RESPONSE;
        command->value.config = (wifi_twt_config_t){g_pm_cfg[84] != 0, g_pm_cfg[85] != 0};
        return command->error = ESP_OK;
    }
    const uint8_t *station, *node;
    uint32_t state;
    memcpy(&station, g_ic + 16, sizeof(station));
    if (!station) return command->error = ESP_ERR_WIFI_NOT_ASSOC;
    memcpy(&node, station + 228, sizeof(node));
    memcpy(&state, station + 152, sizeof(state));
    if (!node || state != 5) return command->error = ESP_ERR_WIFI_NOT_ASSOC;
    if (command->kind == ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS) {
        int bitmap = 0;
        command->error = ieee80211_itwt_get_flow_id_status(&bitmap);
        if (command->error == ESP_OK && (bitmap < 0 || bitmap > UINT8_MAX))
            command->error = ESP_ERR_INVALID_RESPONSE;
        if (command->error == ESP_OK) command->value.flow_bitmap = (uint8_t)bitmap;
    } else if (command->kind == ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET) {
        if (command->value.offset_us > 102400) return command->error;
        uint32_t message[4] = {0, 0, 0, command->value.offset_us};
        command->error = wifi_sta_itwt_set_target_wake_time_offset_process(message);
        if (command->error == ESP_OK) {
            uint32_t actual;
            memcpy(&actual, node + 1060, sizeof(actual));
            if (actual != command->value.offset_us) command->error = ESP_ERR_INVALID_RESPONSE;
        }
    }
    return command->error;
}

esp_err_t esp32_mquickjs_wifi_twt_sdk_control(esp32_mquickjs_wifi_twt_control_kind_t kind,
    esp32_mquickjs_wifi_twt_control_t *value)
{
    if (!value || (unsigned)kind > ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET ||
        (kind == ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET && value->offset_us > 102400)) return ESP_ERR_INVALID_ARG;
    twt_control_command_t command = {.kind = kind, .value = *value, .error = ESP_ERR_INVALID_STATE};
    int error = current_task_is_wifi_task() ? twt_control_dispatch(&command, NULL) :
        eloop_register_timeout_blocking(twt_control_dispatch, &command, NULL);
    if (!command.entered) return ESP_FAIL;
    if (command.error != ESP_OK) return command.error;
    if (error != ESP_OK) return ESP_FAIL;
    if (kind == ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG &&
        (command.value.config.post_wakeup_event != value->config.post_wakeup_event ||
         command.value.config.twt_enable_keep_alive != value->config.twt_enable_keep_alive))
        return ESP_ERR_INVALID_RESPONSE;
    *value = command.value;
    return ESP_OK;
}
_Static_assert(sizeof(void *) == 4 && sizeof(ETSTimer) == 20 && offsetof(ETSTimer, timer_arg) == 16,
    "reviewed C5 legacy timer layout");
_Static_assert(sizeof(wifi_itwt_setup_config_t) == 16 && offsetof(wifi_itwt_setup_config_t, twt_id) == 10 &&
    offsetof(wifi_itwt_setup_config_t, timeout_time_ms) == 12, "reviewed C5 iTWT setup layout");
_Static_assert(sizeof(wifi_btwt_setup_config_t) == 8 && offsetof(wifi_btwt_setup_config_t, btwt_id) == 4 &&
    offsetof(wifi_btwt_setup_config_t, timeout_time_ms) == 6, "reviewed C5 bTWT setup layout");

static bool twt_timer_present(const void *timer)
{
    uint32_t handle;
    /* Presence only: never dereference an asynchronously retired timer. */
    memcpy(&handle, (const uint8_t *)timer + offsetof(ETSTimer, timer_arg), sizeof(handle));
    return handle != 0U;
}
uint8_t wifi_sta_get_btwt_num(void);
esp_err_t ieee80211_get_btwt_info(uint8_t capacity, esp_wifi_btwt_info_t *output);
_Static_assert(sizeof(esp_wifi_btwt_info_t) == 10 &&
    offsetof(esp_wifi_btwt_info_t, btwt_wake_duration) == 4 &&
    offsetof(esp_wifi_btwt_info_t, btwt_wake_interval_mantissa) == 6,
    "reviewed C5 broadcast discovery output layout");
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot_native(
    esp32_mquickjs_wifi_twt_broadcast_snapshot_t *output)
{
    if (output == NULL) return ESP_ERR_INVALID_ARG;
    const uint8_t *station, *node;
    uint32_t state;
    memcpy(&station, g_ic + 16, sizeof(station));
    if (station == NULL) return ESP_ERR_WIFI_NOT_ASSOC;
    memcpy(&node, station + 228, sizeof(node));
    memcpy(&state, station + 152, sizeof(state));
    if (node == NULL || state != 5U) return ESP_ERR_WIFI_NOT_ASSOC;
    esp32_mquickjs_wifi_twt_broadcast_snapshot_t snapshot = {0};
    uint8_t advertised = wifi_sta_get_btwt_num();
    if (advertised > ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST) return ESP_ERR_INVALID_SIZE;
    if (advertised != 0U) {
        /* Already on the native ioctl task: calling the public get_info here
         * would recursively enter that queue. Its reviewed native handler
         * delegates directly to this same getter. Padding remains zero. */
        esp_err_t error = ieee80211_get_btwt_info(ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST, snapshot.schedules);
        if (error != ESP_OK) return error;
        uint32_t seen = 0;
        for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST; ++i) {
            if (!snapshot.schedules[i].btwt_id_in_use) continue;
            uint32_t bit = UINT32_C(1) << snapshot.schedules[i].btwt_info_id;
            if (seen & bit) return ESP_ERR_INVALID_STATE;
            seen |= bit;
            snapshot.schedules[snapshot.count++] = snapshot.schedules[i];
        }
    }
    snapshot.joined_bitmap = s_btwt_id_bitmap;
    *output = snapshot;
    return ESP_OK;
}
bool esp32_mquickjs_wifi_twt_sdk_information_capture_native(unsigned slot, const void *argument,
    esp32_mquickjs_wifi_twt_information_identity_t *out)
{
    if (slot >= 8U || argument == NULL || out == NULL) return false;
    uint8_t control = *(const uint8_t *)argument;
    if ((control & 7U) != slot) return false;
    const uint8_t *station, *node;
    uint32_t state;
    memcpy(&station, g_ic + 16, sizeof(station));
    if (station == NULL) return false;
    memcpy(&node, station + 228, sizeof(node));
    memcpy(&state, station + 152, sizeof(state));
    if (node == NULL || state != 5U) return false;
    esp32_mquickjs_wifi_twt_information_identity_t result = {.node = (uintptr_t)node, .control = control};
    /* An all-flow information timeout can resume every established agreement.
     * Capture the whole set; a changed set cannot authorize that old action. */
    result.flows = (control & 0x80U) ? s_itwt_flow_id_bitmap : (uint8_t)(1U << slot);
    if (result.flows == 0U || (result.flows & s_itwt_flow_id_bitmap) != result.flows) return false;
    for (unsigned i = 0; i < 8U; ++i) {
        result.request_ids[i] = -1;
        if (!(result.flows & (1U << i))) continue;
        if (s_itwt_id[i] < 0 || s_tmp_itwt_id[i] >= 0 ||
            esp32_mquickjs_wifi_twt_setup_request_closed_native(s_itwt_id[i])) return false;
        result.request_ids[i] = s_itwt_id[i];
    }
    *out = result;
    return true;
}
bool esp32_mquickjs_wifi_twt_sdk_information_matches_native(unsigned slot,
    const esp32_mquickjs_wifi_twt_information_identity_t *identity)
{
    esp32_mquickjs_wifi_twt_information_identity_t current;
    return identity != NULL &&
        esp32_mquickjs_wifi_twt_sdk_information_capture_native(slot, &identity->control, &current) &&
        current.node == identity->node && current.flows == identity->flows &&
        memcmp(current.request_ids, identity->request_ids, sizeof(current.request_ids)) == 0;
}
int wifi_sta_itwt_suspend_process(void *message);
int ieee80211_itwt_information(void *node, uint32_t flow, uint32_t size, uint32_t all, uint32_t timeout);
int pm_get_sleep_type(void);
bool esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(unsigned slot)
{
    return slot < 32 && (s_btwt_id_bitmap & (UINT32_C(1) << slot)) != 0;
}
bool esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native(uintptr_t expected)
{
    const uint8_t *station, *node;
    uint32_t state;
    memcpy(&station, g_ic + 16, sizeof(station));
    if (station == NULL || expected == 0U) return false;
    memcpy(&node, station + 228, sizeof(node));
    memcpy(&state, station + 152, sizeof(state));
    return state == 5U && (uintptr_t)node == expected;
}
int ieee80211_btwt_teardown(void *node, unsigned slot);
void ieee80211_btwt_teardown_txcb(void *node, void *buffer);
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_teardown_native(unsigned slot, uint32_t identity)
{
    if (slot >= 32 || !identity) return ESP_ERR_INVALID_ARG;
    uintptr_t node;
    if (!esp32_mquickjs_wifi_btwt_setup_owner_native(slot, identity, &node)) return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp32_mquickjs_wifi_twt_teardown_tx_begin_broadcast_native(identity, node, slot);
    if (error != ESP_OK) return error;
    error = ieee80211_btwt_teardown((void *)node, slot);
    esp_err_t end_error = esp32_mquickjs_wifi_twt_teardown_tx_end_native(identity, error);
    return error != ESP_OK ? error : end_error;
}
void esp32_mquickjs_wifi_twt_sdk_broadcast_teardown_complete_native(uintptr_t node, uint8_t slot, uint8_t status)
{
    /* The reviewed bTWT callback neither retains nor recycles EB input. Pass
     * values through its fixed 24-byte header parser, including PMF output. */
    _Alignas(uint32_t) uint8_t buffer[64] = {0}, descriptor[4 + sizeof(void *)] = {0};
    uint8_t frame[27] = {0}, metadata[20] = {0};
    const void *pointer = descriptor;
    memcpy(buffer + 4, &pointer, sizeof(pointer));
    pointer = frame;
    memcpy(descriptor + 4, &pointer, sizeof(pointer));
    pointer = metadata;
    memcpy(buffer + 56, &pointer, sizeof(pointer));
    metadata[19] = status;
    frame[24] = 22; frame[25] = 7; frame[26] = 0x60U | slot;
    ieee80211_btwt_teardown_txcb((void *)node, buffer);
}
void ieee80211_btwt_setup_txcb(void *buffer);
void esp32_mquickjs_wifi_twt_sdk_broadcast_tx_complete_native(uintptr_t node,
    uint8_t dialog, const uint8_t parameter[17], uint8_t status)
{
    /* Reviewed callback reads only EB descriptor +4, prefix flags +40 and
     * metadata +56/status +19, then copies its parameter into native storage.
     * It neither retains nor recycles this input. This bounded stack view also
     * fixes its hardcoded 24-byte header on protected setup completions. */
    _Alignas(uint32_t) uint8_t buffer[64] = {0}, descriptor[4 + sizeof(void *)] = {0};
    uint8_t frame[44] = {0}, metadata[20] = {0};
    const void *pointer = descriptor;
    memcpy(buffer + 4, &pointer, sizeof(pointer));
    pointer = frame;
    memcpy(descriptor + 4, &pointer, sizeof(pointer));
    pointer = metadata;
    memcpy(buffer + 56, &pointer, sizeof(pointer));
    metadata[19] = status;
    frame[24] = 22; frame[25] = 6; frame[26] = dialog;
    memcpy(frame + 27, parameter, 12); /* IE header + exactly ten data bytes. */
    ieee80211_btwt_setup_txcb(buffer);
    if (status == 1)
        esp32_mquickjs_wifi_btwt_timer_bind_response_native(node, dialog, parameter);
}
bool esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(unsigned slot, uint8_t phase,
    const void *argument, esp32_mquickjs_wifi_btwt_timer_identity_t *out)
{
    if (slot >= 32U || (phase != 30 && phase != 31) || out == NULL ||
        argument != btwt_setup_timer + 704U + 17U * slot) return false;
    const uint8_t *station, *node;
    uint32_t state;
    memcpy(&station, g_ic + 16, sizeof(station));
    if (station == NULL) return false;
    memcpy(&node, station + 228, sizeof(node));
    memcpy(&state, station + 152, sizeof(state));
    if (node == NULL || state != 5U) return false;
    esp32_mquickjs_wifi_btwt_timer_identity_t native = {.node = (uintptr_t)node};
    memcpy(native.parameter, argument, sizeof(native.parameter));
    const uint8_t *p = native.parameter;
    uint8_t command = (p[3] >> 1) & 7U;
    if (p[0] != 216 || p[1] != 10 || (p[2] & 12U) != 12U || (p[10] >> 3) != slot ||
        (phase == 30 && command > TWT_DEMAND) || (phase == 31 && command != TWT_ACCEPT)) return false;
    if (phase == 31) {
        /* Reviewed dwell does a signed 32-bit interval shift and always uses
         * duration*256. Reject values it cannot represent before any PM/bitmap
         * mutation, rather than silently truncating or changing PS policy. */
        uint16_t mantissa = (uint16_t)p[8] | ((uint16_t)p[9] << 8);
        uint64_t interval = (uint64_t)mantissa << ((p[4] >> 2) & 31U);
        if (pm_get_sleep_type() == 0 || p[7] == 0 || (p[2] & 32U) || interval == 0 || interval > INT32_MAX)
            return false;
        uint32_t table; /* C5 pointer value only; never dereference it here. */
        for (unsigned offset = 1180; offset <= 1188; offset += 4) {
            memcpy(&table, node + offset, sizeof(table));
            if (table == 0U) return false;
        }
    }
    *out = native;
    return true;
}
bool esp32_mquickjs_wifi_twt_sdk_broadcast_timer_matches_native(unsigned slot, uint8_t phase,
    const esp32_mquickjs_wifi_btwt_timer_identity_t *identity)
{
    esp32_mquickjs_wifi_btwt_timer_identity_t current;
    return identity != NULL && slot < 32U &&
        esp32_mquickjs_wifi_twt_sdk_broadcast_timer_capture_native(slot, phase,
            btwt_setup_timer + 704U + 17U * slot, &current) &&
        current.node == identity->node && memcmp(current.parameter, identity->parameter, sizeof(current.parameter)) == 0;
}
bool esp32_mquickjs_wifi_twt_sdk_information_resume_allowed_native(
    const esp32_mquickjs_wifi_twt_information_identity_t *native)
{
    return native != NULL && pm_get_sleep_type() != 0 &&
        esp32_mquickjs_wifi_twt_sdk_information_matches_native(native->control & 7U, native);
}
bool esp32_mquickjs_wifi_twt_sdk_information_resumed_native(
    const esp32_mquickjs_wifi_twt_information_identity_t *native)
{
    return esp32_mquickjs_wifi_twt_sdk_information_resume_allowed_native(native) &&
        !((s_itwt_suspend_flow_id_bitmap | s_itwt_resume_flow_id_bitmap) & native->flows);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_information_submit_native(uint32_t setup_identity,
    uint32_t duration_ms, bool resume, uint32_t *identity)
{
    if (!setup_identity || identity == NULL || (resume && duration_ms) || duration_ms > ESP32_MQUICKJS_WIFI_TWT_SUSPEND_MAX_MS)
        return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_twt_setup_result_t setup;
    esp_err_t error = esp32_mquickjs_wifi_twt_setup_result_read(setup_identity, &setup);
    if (error != ESP_OK) return error;
    if (!(setup.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN) || setup.event.status != 1 ||
        setup.event.config.setup_cmd != TWT_ACCEPT ||
        (setup.flags & (ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING | ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS |
            ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED | ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED |
            ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED)))
        return ESP_ERR_INVALID_STATE;
    unsigned flow = 8;
    for (unsigned i = 0; i < 8; ++i) {
        if (s_itwt_id[i] != setup.request_id) continue;
        if (flow != 8) return ESP_ERR_INVALID_STATE;
        flow = i;
    }
    if (flow == 8) return ESP_ERR_INVALID_STATE;
    bool suspended = ((s_itwt_suspend_flow_id_bitmap | s_itwt_resume_flow_id_bitmap) & (1U << flow)) != 0;
    if (suspended != resume) return ESP_ERR_INVALID_STATE;
    if (!resume && twt_timer_present(&itwt_information_timer[flow])) return ESP_ERR_NOT_FINISHED;
    uint8_t control = (uint8_t)(flow | ((duration_ms || resume) ? 0x60U : 0U));
    esp32_mquickjs_wifi_twt_information_identity_t native;
    if (!esp32_mquickjs_wifi_twt_sdk_information_capture_native(flow, &control, &native)) return ESP_ERR_WIFI_NOT_ASSOC;
    /* Match the public SDK preflight before entering its native operation.
     * No implicit setup, power-save change, all-flow action or reconnect. */
    if (((const uint8_t *)native.node)[1176] != 0U) return ESP_ERR_NOT_SUPPORTED;
    if ((resume || duration_ms) && !esp32_mquickjs_wifi_twt_sdk_information_resume_allowed_native(&native))
        return ESP_ERR_INVALID_STATE;
    for (unsigned i = 0; i < 8; ++i) {
        if (!(setup_timer_param[368] & (1U << i))) continue;
        const uint8_t *frame = setup_timer_param + 17U * i;
        uint8_t pending_flow = (frame[3] >> 7) | ((frame[4] & 3U) << 1);
        if (pending_flow == flow) return ESP_ERR_NOT_FINISHED;
    }
    esp32_mquickjs_wifi_twt_tx_cleanup_native();
    error = esp32_mquickjs_wifi_twt_tx_information_quiescent_native(0);
    if (error != ESP_OK) return error;
    uint32_t revision;
    esp32_mquickjs_wifi_twt_information_timer_snapshot_t timer;
    esp32_mquickjs_wifi_twt_information_timer_snapshot(&timer);
    if (timer.fault != ESP_OK) return timer.fault;
    error = resume ? esp32_mquickjs_wifi_twt_information_timer_replaceable_native(setup.request_id, flow) :
        esp32_mquickjs_wifi_twt_information_timer_quiescent_native(setup.request_id, &revision);
    if (error != ESP_OK) return error;
    (void)esp32_mquickjs_wifi_twt_information_reap_native(0); /* Only already-abandoned TX may retire. */
    error = esp32_mquickjs_wifi_twt_information_begin_native(setup_identity, &native, duration_ms, resume, identity);
    if (error != ESP_OK) return error;
    if (resume) {
        /* The public suspend API treats zero as indefinite. The reviewed
         * producer instead accepts size=3 with delta=0: it sends the earliest
         * negotiated wake TSF. Do not substitute a fabricated 1 ms suspend or
         * change only the local bitmap. Old timers remain until TX succeeds;
         * callback 20 disarms/replaces the exact slot through our timer hooks. */
        error = ieee80211_itwt_information((void *)native.node, flow, 3, 0, 0);
        esp32_mquickjs_wifi_twt_information_submitted_native(*identity, error);
        return error;
    }
    /* Reviewed operation 112: flow/size/all bytes 8/9/10, milliseconds at 12.
     * Retain the native builder, protected frame and completion behavior. */
    uint8_t message[24] = {0};
    message[8] = (uint8_t)flow; message[9] = duration_ms ? 3U : 0U;
    memcpy(message + 12, &duration_ms, sizeof(duration_ms));
    error = wifi_sta_itwt_suspend_process(message);
    esp32_mquickjs_wifi_twt_information_submitted_native(*identity, error);
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_admit_native(void)
{
    esp32_mquickjs_wifi_twt_probe_result_snapshot_t result;
    esp32_mquickjs_wifi_twt_probe_result_snapshot(&result);
    /* Reject before the TX submission scope changes its revision. Another
     * SDK caller cannot keep invalidating a managed owner's retirement cut. */
    if (result.owned) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_twt_probe_wake_snapshot_t wake;
    esp32_mquickjs_wifi_twt_probe_wake_snapshot(&wake);
    if (wake.fault != ESP_OK) return wake.fault;
    if (wake.held || wake.acquiring || wake.releasing) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_twt_probe_timer_snapshot_t timer;
    esp32_mquickjs_wifi_twt_probe_timer_snapshot(&timer);
    if (timer.fault != ESP_OK) return timer.fault;
    if (timer.post_error != 0) return ESP_ERR_INVALID_STATE;
    const uint8_t *station, *node;
    uint32_t state;
    memcpy(&station, g_ic + 16, sizeof(station));
    if (station == NULL) return ESP_ERR_WIFI_NOT_ASSOC;
    memcpy(&node, station + 228, sizeof(node));
    memcpy(&state, station + 152, sizeof(state));
    if (node == NULL || state != 5U) return ESP_ERR_WIFI_NOT_ASSOC;
    if (node[1056] != 0U || twt_timer_present(&itwt_probe_timer)) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
bool esp32_mquickjs_wifi_twt_sdk_setup_timer_capture_native(unsigned slot, uint8_t phase,
    const void *argument, esp32_mquickjs_wifi_twt_setup_timer_identity_t *out)
{
    if (slot >= 8U || out == NULL || (phase != 26 && phase != 27) ||
        argument != setup_timer_param + 136U + slot || !(setup_timer_param[368] & (1U << slot))) return false;
    uint32_t native_phase;
    memcpy(&native_phase, setup_timer_param + 336U + 4U * slot, sizeof(native_phase));
    if ((phase == 26 && native_phase > 1U) || (phase == 27 && native_phase != 2U)) return false;
    const uint8_t *station, *node;
    uint32_t state;
    memcpy(&station, g_ic + 16, sizeof(station));
    if (station == NULL) return false;
    memcpy(&node, station + 228, sizeof(node));
    memcpy(&state, station + 152, sizeof(state));
    if (node == NULL || state != 5U) return false;
    uint8_t dialog = setup_timer_param[136U + slot];
    /* The native handlers search by the dialog byte, not the slot. A duplicate
     * live dialog cannot safely authorize either timeout/dwell operation. */
    for (unsigned i = 0; i < 8U; ++i)
        if (i != slot && (setup_timer_param[368] & (1U << i)) && setup_timer_param[136U + i] == dialog) return false;
    esp32_mquickjs_wifi_twt_setup_timer_identity_t result = {.node = (uintptr_t)node, .dialog = dialog};
    memcpy(&result.request_id, setup_timer_param + 320U + 2U * slot, sizeof(result.request_id));
    if (result.request_id < 0 || esp32_mquickjs_wifi_twt_setup_request_closed_native(result.request_id)) return false;
    const uint8_t *frame = setup_timer_param + 17U * slot;
    result.flow = (frame[3] >> 7) | ((frame[4] & 3U) << 1);
    *out = result;
    return true;
}
bool esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(unsigned slot, uint8_t phase,
    const esp32_mquickjs_wifi_twt_setup_timer_identity_t *identity)
{
    esp32_mquickjs_wifi_twt_setup_timer_identity_t current;
    return identity != NULL && slot < 8U &&
        esp32_mquickjs_wifi_twt_sdk_setup_timer_capture_native(slot, phase, setup_timer_param + 136U + slot, &current) &&
        current.node == identity->node && current.request_id == identity->request_id &&
        current.dialog == identity->dialog && current.flow == identity->flow;
}
bool esp32_mquickjs_wifi_twt_sdk_setup_tx_matches_native(unsigned slot,
    const esp32_mquickjs_wifi_twt_setup_timer_identity_t *identity)
{
    if (!esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(slot, 26, identity)) return false;
    uint32_t phase;
    memcpy(&phase, setup_timer_param + 336U + 4U * slot, sizeof(phase));
    /* The original TX callback indexes temporary IDs by the frame's flow,
     * including its failed-lookup branch. Never let it touch a successor. */
    return phase == 0U && s_tmp_itwt_id[identity->flow] == identity->request_id;
}
static const uint8_t *twt_probe_node_native(void)
{
    const uint8_t *station, *node;
    memcpy(&station, g_ic + 16, sizeof(station));
    if (station == NULL) return NULL;
    memcpy(&node, station + 228, sizeof(node));
    return node;
}
bool esp32_mquickjs_wifi_twt_sdk_probe_active_native(void)
{
    const uint8_t *current = twt_probe_node_native();
    return current != NULL && current[1056] != 0U;
}
bool esp32_mquickjs_wifi_twt_sdk_probe_capture_native(const void *argument, uintptr_t *node, uint8_t *phase)
{
    if (node == NULL || phase == NULL) return false;
    const uint8_t *current = twt_probe_node_native();
    if (current == NULL || argument != current + 1057 || current[1056] == 0U || current[1057] > 1U)
        return false;
    *node = (uintptr_t)current;
    *phase = current[1057];
    return true;
}
bool esp32_mquickjs_wifi_twt_sdk_probe_matches_native(uintptr_t node, uint8_t phase)
{
    if (phase > 1U) return false;
    const uint8_t *current = twt_probe_node_native();
    return current != NULL && (uintptr_t)current == node && current[1056] != 0U && current[1057] == phase;
}
bool esp32_mquickjs_wifi_twt_sdk_probe_abort_native(uintptr_t node, uint8_t phase)
{
    /* The reviewed probe acquires one PM wake reference before TX. Only its
     * exact current active node/phase may release it. Clear before calling
     * SDK, so repeated cleanup and a later TX callback cannot release twice.
     * This does not touch node storage or invent a radio timeout event. */
    if (!esp32_mquickjs_wifi_twt_sdk_probe_matches_native(node, phase)) return false;
    uint8_t *current = (uint8_t *)twt_probe_node_native();
    current[1056] = 0;
    esp32_mquickjs_wifi_twt_probe_wake_done_native();
    return true;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_cancel_native(uint32_t identity)
{
    esp_err_t error = esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(identity);
    if (error != ESP_OK) return error;
    error = esp32_mquickjs_wifi_twt_probe_timer_cancel_native();
    esp32_mquickjs_wifi_twt_probe_result_cancel_end_native(identity, error);
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_cancel_native(uint32_t identity)
{
    esp32_mquickjs_wifi_twt_setup_result_t result;
    esp_err_t error = esp32_mquickjs_wifi_twt_setup_result_read(identity, &result);
    if (error != ESP_OK) return error;
    if (result.flags & (ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING | ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING))
        return ESP_ERR_INVALID_STATE;
    bool teardown = (result.flags & ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED) != 0U;
    if (teardown && !(result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED) &&
        (!(result.flags & ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SEEN) ||
        result.teardown_status != 1U || (result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS)))
        return ESP_ERR_NOT_FINISHED;
    uint8_t pending = 0, flows = result.cancelled_flows;
    for (unsigned i = 0; i < 8U; ++i) {
        if (s_itwt_id[i] == result.request_id) return ESP_ERR_NOT_FINISHED;
        if (s_tmp_itwt_id[i] == result.request_id) flows |= (uint8_t)(1U << i);
        if (!(setup_timer_param[368] & (1U << i))) continue;
        int16_t request_id;
        memcpy(&request_id, setup_timer_param + 320U + 2U * i, sizeof(request_id));
        if (request_id != result.request_id) continue;
        uint32_t phase;
        memcpy(&phase, setup_timer_param + 336U + 4U * i, sizeof(phase));
        if (phase > 2U) return ESP_ERR_INVALID_STATE;
        const uint8_t *frame = setup_timer_param + 17U * i;
        uint8_t flow = (frame[3] >> 7) | ((frame[4] & 3U) << 1);
        flows |= (uint8_t)(1U << flow);
        pending |= (uint8_t)(1U << i);
    }
    if ((pending & (pending - 1U)) != 0U) return ESP_ERR_INVALID_STATE;
    /* The reviewed invalid-AP-parameters path can set a flow bitmap without
     * establishing its ID. Do not erase such state or claim clean cancellation.
     * An AP-selected flow may differ from the originally requested one. */
    if (result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN)
        flows |= (uint8_t)(1U << result.event.config.flow_id);
    if (flows & s_itwt_flow_id_bitmap) return ESP_ERR_INVALID_STATE;
    for (unsigned i = 0; i < 8U; ++i) {
        if ((flows & (1U << i)) && s_tmp_itwt_id[i] >= 0 && s_tmp_itwt_id[i] != result.request_id)
            return ESP_ERR_INVALID_STATE;
        if ((setup_timer_param[368] & (1U << i)) && !(pending & (1U << i))) {
            const uint8_t *frame = setup_timer_param + 17U * i;
            uint8_t flow = (frame[3] >> 7) | ((frame[4] & 3U) << 1);
            if (flows & (1U << flow)) return ESP_ERR_INVALID_STATE;
        }
    }
    error = esp32_mquickjs_wifi_twt_setup_timer_cancel_native(result.request_id, pending);
    if (error != ESP_OK) return error;
    error = esp32_mquickjs_wifi_twt_information_timer_cleanup_native(result.request_id, flows);
    if (error != ESP_OK) return error;
    if (teardown) {
        uint32_t revision;
        error = esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(identity, &revision);
        if (error != ESP_OK) return error;
    }
    /* Timer authority is revoked and its handles deleted first. Keep all
     * pending IDs on any cleanup failure so retries retain exact ownership. */
    setup_timer_param[368] &= (uint8_t)~pending;
    for (unsigned i = 0; i < 8U; ++i) {
        if (pending & (1U << i)) {
            uint32_t phase = 0;
            memcpy(setup_timer_param + 336U + 4U * i, &phase, sizeof(phase));
        }
        if (s_tmp_itwt_id[i] == result.request_id) s_tmp_itwt_id[i] = -1;
    }
    return esp32_mquickjs_wifi_twt_setup_result_cancelled_native(identity, flows) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
bool esp32_mquickjs_wifi_twt_sdk_teardown_tx_matches_native(uint32_t identity, uintptr_t node, uint8_t flow)
{
    if (identity == 0U || node == 0U || flow > 7U || (uintptr_t)twt_probe_node_native() != node) return false;
    const uint8_t *station;
    uint32_t state;
    memcpy(&station, g_ic + 16, sizeof(station));
    if (station == NULL) return false;
    memcpy(&state, station + 152, sizeof(state));
    if (state != 5U) return false;
    esp32_mquickjs_wifi_twt_setup_result_t result;
    return esp32_mquickjs_wifi_twt_setup_result_read(identity, &result) == ESP_OK &&
        (result.flags & ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED) &&
        !(result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED) && result.teardown_flow == flow &&
        s_itwt_id[flow] == result.request_id && (s_itwt_flow_id_bitmap & (1U << flow)) && s_tmp_itwt_id[flow] < 0;
}
int wifi_sta_itwt_teardown_process(void *message);
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_teardown_native(uint32_t identity, uint8_t flow)
{
    if (identity == 0U || flow > 7U) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_twt_setup_result_t result;
    esp_err_t error = esp32_mquickjs_wifi_twt_setup_result_read(identity, &result);
    if (error != ESP_OK) return error;
    if (result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED) return ESP_ERR_INVALID_STATE;
    /* Action's public ioctl entry already checks driver initialization/start.
     * Recheck the current associated node before the original handler, which
     * otherwise dereferences the Station pointer without a null check. */
    const uint8_t *station;
    uint32_t state;
    memcpy(&station, g_ic + 16, sizeof(station));
    if (station == NULL || twt_probe_node_native() == NULL) return ESP_ERR_WIFI_NOT_ASSOC;
    memcpy(&state, station + 152, sizeof(state));
    if (state != 5U) return ESP_ERR_WIFI_NOT_ASSOC;
    if (s_itwt_id[flow] != result.request_id || !(s_itwt_flow_id_bitmap & (1U << flow)) ||
        s_tmp_itwt_id[flow] >= 0) return ESP_ERR_INVALID_STATE;
    for (unsigned i = 0; i < 8U; ++i) {
        if (i != flow && s_itwt_id[i] == result.request_id) return ESP_ERR_INVALID_STATE;
        if (!(setup_timer_param[368] & (1U << i))) continue;
        int16_t pending_id;
        memcpy(&pending_id, setup_timer_param + 320U + 2U * i, sizeof(pending_id));
        const uint8_t *frame = setup_timer_param + 17U * i;
        uint8_t pending_flow = (frame[3] >> 7) | ((frame[4] & 3U) << 1);
        if (pending_id == result.request_id || pending_flow == flow) return ESP_ERR_INVALID_STATE;
    }
    error = esp32_mquickjs_wifi_twt_teardown_tx_begin_native(identity, (uintptr_t)twt_probe_node_native(), flow);
    if (error != ESP_OK) return error;
    error = esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(identity, flow);
    if (error != ESP_OK) {
        esp32_mquickjs_wifi_twt_teardown_tx_abandon_native(identity);
        return error;
    }
    /* Reviewed operation 111 reads only flow byte 8. Keep its native PMF,
     * management-frame construction and asynchronous completion behavior. */
    uint8_t message[24] = {0};
    message[8] = flow;
    error = wifi_sta_itwt_teardown_process(message);
    esp_err_t tx_error = esp32_mquickjs_wifi_twt_teardown_tx_end_native(identity, error);
    esp32_mquickjs_wifi_twt_setup_result_teardown_submitted_native(identity, error);
    return error != ESP_OK ? error : tx_error;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_quiescent_native(uint32_t identity,
    esp32_mquickjs_wifi_twt_setup_cut_t *cut)
{
    if (identity == 0U || cut == NULL) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_twt_setup_result_t result;
    esp_err_t error = esp32_mquickjs_wifi_twt_setup_result_read(identity, &result);
    if (error != ESP_OK) return error;
    bool teardown = (result.flags & ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED) != 0U;
    if (teardown && !(result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED) &&
        (!(result.flags & ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SEEN) ||
        result.teardown_status != 1U || (result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS)))
        return ESP_ERR_NOT_FINISHED;
    if (!(result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED) ||
        (result.flags & (ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING | ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING)) || result.observation_calls)
        return ESP_ERR_NOT_FINISHED;
    uint8_t flows = result.cancelled_flows;
    if (result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN) flows |= (uint8_t)(1U << result.event.config.flow_id);
    for (unsigned i = 0; i < 8U; ++i) {
        if (s_itwt_id[i] == result.request_id || s_tmp_itwt_id[i] == result.request_id) return ESP_ERR_NOT_FINISHED;
        if (setup_timer_param[368] & (1U << i)) {
            int16_t request_id;
            memcpy(&request_id, setup_timer_param + 320U + 2U * i, sizeof(request_id));
            if (request_id == result.request_id) return ESP_ERR_NOT_FINISHED;
            const uint8_t *frame = setup_timer_param + 17U * i;
            uint8_t flow = (frame[3] >> 7) | ((frame[4] & 3U) << 1);
            if (flows & (1U << flow)) return ESP_ERR_INVALID_STATE;
        }
        if ((flows & (1U << i)) && (s_itwt_id[i] >= 0 || s_tmp_itwt_id[i] >= 0 ||
            twt_timer_present(&itwt_information_timer[i]))) return ESP_ERR_INVALID_STATE;
    }
    if (flows & (s_itwt_flow_id_bitmap | s_itwt_suspend_flow_id_bitmap | s_itwt_resume_flow_id_bitmap))
        return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_twt_tx_snapshot_t tx;
    esp32_mquickjs_wifi_twt_tx_cleanup_native();
    esp32_mquickjs_wifi_twt_tx_snapshot(&tx);
    if (tx.fault != ESP32_MQUICKJS_WIFI_TWT_TX_OK) return ESP_ERR_INVALID_STATE;
    if (tx.tracked || tx.output_calls || tx.recycle_calls || tx.probe_calls) return ESP_ERR_NOT_FINISHED;
    if (tx.revision == UINT32_MAX) return ESP_ERR_NO_MEM;
    esp32_mquickjs_wifi_twt_setup_cut_t current = {.tx_revision = tx.revision};
    error = esp32_mquickjs_wifi_twt_setup_timer_quiescent_native(result.request_id, &current.timer_revision);
    if (error != ESP_OK) return error;
    error = esp32_mquickjs_wifi_twt_information_timer_quiescent_native(result.request_id, &current.information_revision);
    if (error != ESP_OK) return error;
    if (teardown) error = esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(identity, &current.teardown_revision);
    if (error == ESP_OK) *cut = current;
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_release_native(uint32_t identity,
    const esp32_mquickjs_wifi_twt_setup_cut_t *cut, uint32_t sequence)
{
    if (cut == NULL || sequence == 0U) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_twt_setup_cut_t current;
    esp_err_t error = esp32_mquickjs_wifi_twt_sdk_setup_quiescent_native(identity, &current);
    if (error != ESP_OK) return error;
    if (current.tx_revision != cut->tx_revision || current.timer_revision != cut->timer_revision ||
        current.information_revision != cut->information_revision || current.teardown_revision != cut->teardown_revision)
        return ESP_ERR_NOT_FINISHED;
    esp32_mquickjs_wifi_twt_setup_result_t result;
    error = esp32_mquickjs_wifi_twt_setup_result_read(identity, &result);
    if (error != ESP_OK) return error;
    return esp32_mquickjs_wifi_twt_setup_result_release_native(identity, result.revision, sequence)
        ? ESP_OK : ESP_ERR_NOT_FINISHED;
}
int __wrap_wifi_sta_itwt_send_probe_req_process(void *message);
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_submit_native(uint32_t timeout_ms, uint32_t *identity)
{
    if (identity == NULL || *identity != 0U || timeout_ms == 0U ||
        timeout_ms > ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_twt_probe_result_snapshot_t before, after;
    esp32_mquickjs_wifi_twt_probe_result_snapshot(&before);
    if (before.owned) return ESP_ERR_INVALID_STATE;
    /* Reviewed handler reads only word 12 from this synchronous message;
     * association/timer/TX admission remains in the production wrapper. Do
     * not call the public locking SDK API from inside its own ioctl task. */
    uint32_t message[4] = {0, 0, 0, timeout_ms};
    esp_err_t error = __wrap_wifi_sta_itwt_send_probe_req_process(message);
    esp32_mquickjs_wifi_twt_probe_result_snapshot(&after);
    if (after.identity != before.identity) {
        *identity = after.identity;
        if (!esp32_mquickjs_wifi_twt_probe_result_claim_native(after.identity)) return ESP_ERR_INVALID_STATE;
    }
    return error;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_quiescent_native(uint32_t identity,
    esp32_mquickjs_wifi_twt_probe_cut_t *cut)
{
    if (identity == 0U || cut == NULL) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_twt_sdk_snapshot_t snapshot;
    esp_err_t error = esp32_mquickjs_wifi_twt_sdk_snapshot_native(&snapshot);
    if (error != ESP_OK) return error;
    if (snapshot.probe_result.identity != identity || !snapshot.probe_result.owned) return ESP_ERR_INVALID_STATE;
    if (snapshot.tx.fault != ESP32_MQUICKJS_WIFI_TWT_TX_OK) return ESP_ERR_INVALID_STATE;
    if (!snapshot.probe_result.cancel_complete || snapshot.probe_result.submitting || snapshot.probe_result.observation_calls ||
        snapshot.probe_active || snapshot.probe_timer_present || snapshot.probe_timer.current_identity ||
        snapshot.probe_timer.cleanup_pending || snapshot.probe_wake.held || snapshot.probe_wake.acquiring ||
        snapshot.probe_wake.releasing || snapshot.probe_wake.fault != ESP_OK ||
        snapshot.tx.probe_calls || snapshot.tx.probe_buffer_present) return ESP_ERR_NOT_FINISHED;
    *cut = (esp32_mquickjs_wifi_twt_probe_cut_t){snapshot.tx.revision, snapshot.probe_timer.last_identity};
    return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_release_native(uint32_t identity,
    const esp32_mquickjs_wifi_twt_probe_cut_t *cut, uint32_t sequence)
{
    if (cut == NULL || sequence == 0U) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_twt_probe_cut_t current;
    esp_err_t error = esp32_mquickjs_wifi_twt_sdk_probe_quiescent_native(identity, &current);
    if (error != ESP_OK) return error;
    if (current.tx_revision != cut->tx_revision || current.timer_identity != cut->timer_identity)
        return ESP_ERR_NOT_FINISHED;
    return esp32_mquickjs_wifi_twt_probe_result_release_native(identity, sequence) ? ESP_OK : ESP_ERR_NOT_FINISHED;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_snapshot_native(esp32_mquickjs_wifi_twt_sdk_snapshot_t *output)
{
    if (output == NULL) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_twt_sdk_snapshot_t snapshot = {
        .individual_flow_bitmap = s_itwt_flow_id_bitmap,
        .individual_pending_mask = setup_timer_param[368],
        .individual_suspend_bitmap = s_itwt_suspend_flow_id_bitmap,
        .individual_resume_bitmap = s_itwt_resume_flow_id_bitmap,
        .broadcast_id_bitmap = s_btwt_id_bitmap,
        .probe_timer_present = twt_timer_present(&itwt_probe_timer),
        .probe_phase = UINT8_MAX,
    };
    const uint8_t *probe_node = twt_probe_node_native();
    if (probe_node != NULL && probe_node[1056] != 0U) {
        snapshot.probe_active = true;
        snapshot.probe_phase = probe_node[1057];
    }
    memcpy(snapshot.individual_ids, s_itwt_id, sizeof(snapshot.individual_ids));
    memcpy(snapshot.individual_temporary_ids, s_tmp_itwt_id, sizeof(snapshot.individual_temporary_ids));
    for (unsigned i = 0; i < 8U; ++i) {
        snapshot.individual_pending_ids[i] = -1;
        snapshot.individual_pending_flows[i] = UINT8_MAX;
        if (snapshot.individual_pending_mask & (1U << i)) {
            memcpy(&snapshot.individual_pending_ids[i], setup_timer_param + 320U + 2U * i, sizeof(int16_t));
            const uint8_t *frame = setup_timer_param + 17U * i;
            snapshot.individual_pending_flows[i] = (frame[3] >> 7) | ((frame[4] & 3U) << 1);
        }
        if (twt_timer_present(setup_timer_param + 144U + 20U * i))
            snapshot.individual_setup_timer_mask |= (uint8_t)(1U << i);
        if (twt_timer_present(&itwt_information_timer[i]))
            snapshot.individual_information_timer_mask |= (uint8_t)(1U << i);
    }
    for (unsigned i = 0; i < 32U; ++i)
        if (twt_timer_present(btwt_setup_timer + 20U * i))
            snapshot.broadcast_setup_timer_mask |= UINT32_C(1) << i;
    esp32_mquickjs_wifi_twt_tx_cleanup_native();
    esp32_mquickjs_wifi_twt_tx_snapshot(&snapshot.tx);
    esp32_mquickjs_wifi_twt_probe_timer_snapshot(&snapshot.probe_timer);
    esp32_mquickjs_wifi_twt_probe_result_snapshot(&snapshot.probe_result);
    esp32_mquickjs_wifi_twt_probe_wake_snapshot(&snapshot.probe_wake);
    esp32_mquickjs_wifi_twt_setup_timer_snapshot(&snapshot.setup_timer);
    esp32_mquickjs_wifi_twt_setup_results_snapshot(&snapshot.setup_results);
    esp32_mquickjs_wifi_twt_teardown_tx_snapshot(&snapshot.teardown_tx);
    esp32_mquickjs_wifi_twt_information_timer_snapshot(&snapshot.information_timer);
    esp32_mquickjs_wifi_btwt_timer_snapshot(&snapshot.broadcast_timer);
    *output = snapshot;
    return ESP_OK;
}

esp_err_t __real_wifi_sta_itwt_setup_process(void *message);
esp_err_t __real_wifi_sta_btwt_setup_process(void *message);
/* The public C5 wrapper checks established flows, but the native handler picks
 * an unchecked first-free slot from a DIFFERENT eight-entry pending bitmap.
 * Guard that capacity and pending-flow collisions inside the same native task
 * as the actual mutation. This is admission protection, not retirement proof. */
static esp_err_t twt_individual_setup_process(void *message, wifi_itwt_setup_config_t *config,
    uint32_t managed_identity, esp_err_t *driver_error)
{
    /* The reviewed dwell completion enables modem sleep when PS is NONE.
     * Require an explicit caller policy before a managed setup can reach it. */
    if (managed_identity != 0U && pm_get_sleep_type() == 0) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_itwt_options_t options = {.config = *config, .timeout_ms = 1};
    if (!esp32_mquickjs_wifi_itwt_options_valid(&options)) return ESP_ERR_INVALID_ARG;
    if (esp32_mquickjs_wifi_twt_setup_request_closed_native((int16_t)config->twt_id)) return ESP_ERR_INVALID_STATE;
    esp_err_t timer_error = esp32_mquickjs_wifi_twt_setup_timer_error();
    if (timer_error != ESP_OK) return timer_error;
    esp32_mquickjs_wifi_twt_sdk_snapshot_t snapshot;
    (void)esp32_mquickjs_wifi_twt_sdk_snapshot_native(&snapshot);
    if (snapshot.tx.fault != ESP32_MQUICKJS_WIFI_TWT_TX_OK) return ESP_ERR_INVALID_STATE;
    if (snapshot.individual_pending_mask == UINT8_MAX) return ESP_ERR_WIFI_TWT_FULL;
    uint8_t unavailable = snapshot.individual_flow_bitmap;
    for (unsigned i = 0; i < 8U; ++i) {
        if (snapshot.individual_ids[i] == (int16_t)config->twt_id ||
            snapshot.individual_temporary_ids[i] == (int16_t)config->twt_id ||
            snapshot.individual_pending_ids[i] == (int16_t)config->twt_id) return ESP_ERR_INVALID_ARG;
        if (snapshot.individual_pending_mask & (1U << i))
            unavailable |= (uint8_t)(1U << snapshot.individual_pending_flows[i]);
    }
    if (unavailable == UINT8_MAX) return ESP_ERR_WIFI_TWT_FULL;
    unsigned flow = config->flow_id;
    if (unavailable & (1U << flow)) {
        for (flow = 0; flow < 8U && (unavailable & (1U << flow)); ++flow) {}
    }
    /* The SDK already documents in/out flow selection. Reserve a free flow
     * taking pending requests into account before its established-only search. */
    uint32_t identity = managed_identity;
    esp_err_t error = identity != 0U ? ESP_OK :
        esp32_mquickjs_wifi_twt_setup_result_begin_native((int16_t)config->twt_id, &identity);
    if (error != ESP_OK) return error;
    if (managed_identity != 0U && !esp32_mquickjs_wifi_twt_setup_submit_driver_native(config, managed_identity))
        return ESP_ERR_INVALID_STATE;
    config->flow_id = flow;
    error = __real_wifi_sta_itwt_setup_process(message);
    *driver_error = error;
    timer_error = esp32_mquickjs_wifi_twt_setup_timer_error();
    if (timer_error != ESP_OK) error = timer_error;
    if (managed_identity == 0U) esp32_mquickjs_wifi_twt_setup_result_submitted_native(identity, error);
    return error;
}
esp_err_t __wrap_wifi_sta_itwt_setup_process(void *message)
{
    if (message == NULL) return ESP_ERR_INVALID_ARG;
    wifi_itwt_setup_config_t *config;
    memcpy(&config, (const uint8_t *)message + 12, sizeof(config));
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    uint32_t identity = 0;
    esp_err_t error = esp32_mquickjs_wifi_twt_setup_submit_enter_native(config, &identity);
    if (error != ESP_OK) return error;
    esp_err_t driver_error = ESP_OK;
    error = twt_individual_setup_process(message, config, identity, &driver_error);
    if (identity != 0U) esp32_mquickjs_wifi_twt_setup_submit_complete_native(config, identity, driver_error);
    return error;
}
/* The public bTWT wrapper omits an ID check before the lower handler indexes
 * the AP's 32-entry table. Reject before any native lookup, even for non-JS
 * callers of the public API. Preserve the SDK's connected/AP capability checks. */
esp_err_t __wrap_wifi_sta_btwt_setup_process(void *message)
{
    if (message == NULL) return ESP_ERR_INVALID_ARG;
    wifi_btwt_setup_config_t *config;
    memcpy(&config, (const uint8_t *)message + 12, sizeof(config));
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    bool managed = false;
    esp_err_t error = esp32_mquickjs_wifi_btwt_submit_enter_native(config, &managed);
    if (error != ESP_OK) return error;
    esp32_mquickjs_wifi_btwt_options_t options = {.config = *config, .timeout_ms = 1};
    esp_err_t driver_error = ESP_OK;
    if (!esp32_mquickjs_wifi_btwt_options_valid(&options)) error = ESP_ERR_INVALID_ARG;
    else if (managed && pm_get_sleep_type() == 0) error = ESP_ERR_INVALID_STATE;
    else error = esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(config->btwt_id);
    if (error == ESP_OK && managed && !esp32_mquickjs_wifi_btwt_submit_driver_native(config))
        error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK) {
        driver_error = __real_wifi_sta_btwt_setup_process(message);
        error = driver_error != ESP_OK ? driver_error : esp32_mquickjs_wifi_btwt_timer_error();
    }
    if (managed) esp32_mquickjs_wifi_btwt_submit_complete_native(config, driver_error);
    return error;
}
#else
esp_err_t esp32_mquickjs_wifi_twt_sdk_snapshot_native(esp32_mquickjs_wifi_twt_sdk_snapshot_t *output)
{
    return output == NULL ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
}
#endif
#endif
