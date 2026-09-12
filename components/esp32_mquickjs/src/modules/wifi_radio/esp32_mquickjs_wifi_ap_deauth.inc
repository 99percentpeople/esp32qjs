/* Fixed-SDK Wi-Fi-task transaction. Both public APIs execute inline when
 * already on that task, so no association command can reuse AID between the
 * MAC lookup and the targeted deauth. No driver table offsets are used. */
typedef struct { int (*fn)(void *); void *arg; uint32_t arg_size; } wifi_ap_deauth_ipc_t;
extern int esp_wifi_ipc_internal(wifi_ap_deauth_ipc_t *config, bool sync);
extern bool current_task_is_wifi_task(void);
_Static_assert(sizeof(wifi_ap_deauth_ipc_t) == 12, "review AP deauth IPC ABI");

typedef struct {
    wifi_ap_deauth_ipc_t ipc;
    esp32_mquickjs_wifi_radio_operation_t operation;
    uint8_t address[6];
    uint16_t aid;
    esp_err_t error;
    const char *stage;
    bool requested;
    atomic_bool completed;
} wifi_ap_deauth_t;
static wifi_ap_deauth_t *s_ap_deauth;
static const char s_ap_deauth_fault[] = "ap-deauth-ipc-unconfirmed";

static int wifi_ap_deauth_native(void *opaque)
{
    wifi_ap_deauth_t *call = opaque;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    call->stage = "deauth-task";
    if (!current_task_is_wifi_task()) goto done;
    call->stage = "deauth-address";
    error = esp_wifi_ap_get_sta_aid(call->address, &call->aid);
    if (error == ESP_ERR_NOT_FOUND) { error = ESP_OK; goto done; }
    if (error != ESP_OK) goto done;
    /* Zero is the SDK's broadcast selector, never a targeted client. */
    if (!call->aid || call->aid > ESP_WIFI_MAX_CONN_NUM) { error = ESP_ERR_INVALID_RESPONSE; goto done; }
    call->stage = "deauth-request";
    error = esp_wifi_deauth_sta(call->aid);
    call->requested = error == ESP_OK;
done:
    call->error = error;
    if (error == ESP_OK) call->stage = NULL;
    /* No access to call follows this release. The SDK owns its outer IPC
     * command independently; a late receipt never needs a JS/runtime root. */
    atomic_store_explicit(&call->completed, true, memory_order_release);
    return error;
}

static void wifi_ap_deauth_release_locked(void)
{
    wifi_ap_deauth_t *call = s_ap_deauth;
    taskENTER_CRITICAL(&s_radio.lock);
    if (s_radio.operation.kind == ESP32_MQUICKJS_WIFI_RADIO_OPERATION_AP_DEAUTH &&
        s_radio.operation.identity == call->operation.identity &&
        s_radio.operation.generation == call->operation.generation &&
        s_radio.operation.lease_identity == call->operation.lease_identity) {
        memset(&s_radio.operation, 0, sizeof(s_radio.operation));
        /* A delayed receipt resolves only this dispatch uncertainty. It does
         * not clear unrelated cleanup, driver faults or generation loss. */
        if (s_radio.fault_stage == s_ap_deauth_fault && !s_radio.cleanup_stage && !s_radio.restart_required &&
            s_radio.generation == call->operation.generation && s_radio.driver_owned && s_radio.started &&
            (s_radio.event_live & WIFI_MODE_AP)) {
            s_radio.fault_stage = NULL;
            s_radio.fault_error = ESP_OK;
            s_radio.driver_state = ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        }
    }
    taskEXIT_CRITICAL(&s_radio.lock);
    s_ap_deauth = NULL;
    esp32_mquickjs_wireless_secure_zero(call, sizeof(*call));
    esp32_mquickjs_memory_payload_free(call);
}

bool esp32_mquickjs_wifi_radio_ap_deauth_poll(bool *pending)
{
    wifi_radio_operation_lock();
    bool handled = s_ap_deauth && atomic_load_explicit(&s_ap_deauth->completed, memory_order_acquire);
    if (handled) wifi_ap_deauth_release_locked();
    if (pending) *pending = s_ap_deauth != NULL;
    wifi_radio_operation_unlock();
    return handled;
}

esp_err_t esp32_mquickjs_wifi_radio_ap_deauth(const esp32_mquickjs_wifi_radio_lease_t *lease,
    const uint8_t address[6], bool *requested, bool *handoff_unknown, const char **stage)
{
    if (!address || !requested || !handoff_unknown || !stage || (address[0] & 1U) ||
        !memcmp(address, "\0\0\0\0\0\0", 6)) return ESP_ERR_INVALID_ARG;
    *requested = false;
    *handoff_unknown = false;
    *stage = "deauth-admission";
    wifi_radio_operation_lock();
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (s_ap_deauth || !wifi_radio_lease_valid(lease) ||
        lease->client != ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP ||
        !s_radio.started || !(s_radio.event_live & WIFI_MODE_AP) ||
        s_radio.driver_state != ESP32_MQUICKJS_WIFI_RADIO_STARTED ||
        s_radio.lifecycle.identity || s_radio.operation.identity || s_radio.restart_required ||
        s_radio.fault_stage || s_radio.cleanup_stage) goto done;
    if (!s_radio.next_operation_identity) { error = ESP_ERR_NO_MEM; goto done; }
    *stage = "deauth-allocate";
    wifi_ap_deauth_t *call = esp32_mquickjs_memory_wireless_calloc("wifi.radio", 1, sizeof(*call), ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!call) { error = ESP_ERR_NO_MEM; goto done; }
    memcpy(call->address, address, sizeof(call->address));
    call->ipc = (wifi_ap_deauth_ipc_t){wifi_ap_deauth_native, call, 0};
    atomic_init(&call->completed, false);
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.operation = (esp32_mquickjs_wifi_radio_operation_t){.kind = ESP32_MQUICKJS_WIFI_RADIO_OPERATION_AP_DEAUTH,
        .generation = s_radio.generation, .identity = s_radio.next_operation_identity++, .lease_identity = lease->identity};
    call->operation = s_radio.operation;
    taskEXIT_CRITICAL(&s_radio.lock);
    s_ap_deauth = call;
    *stage = "deauth-dispatch";
    error = esp_wifi_ipc_internal(&call->ipc, true);
    if (atomic_load_explicit(&call->completed, memory_order_acquire)) {
        error = call->error != ESP_OK ? call->error : error;
        *requested = call->requested;
        *stage = error == ESP_OK ? NULL : call->stage ? call->stage : "deauth-dispatch";
        wifi_ap_deauth_release_locked();
    } else if (error == ESP_ERR_NO_MEM || error == ESP_ERR_WIFI_NOT_INIT || error == ESP_ERR_INVALID_ARG) {
        /* Reviewed failures before SDK command ownership transfer. */
        wifi_ap_deauth_release_locked();
    } else {
        if (error == ESP_OK) error = ESP_ERR_INVALID_STATE;
        *handoff_unknown = true;
        *stage = s_ap_deauth_fault;
        (void)wifi_radio_record_fault(s_ap_deauth_fault, error);
        /* Retain one native command and the exact AP owner. The poller only
         * consumes a later receipt; it never dispatches the command again. */
    }
done:
    wifi_radio_operation_unlock();
    return error;
}
