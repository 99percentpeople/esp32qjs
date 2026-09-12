#include "esp32_mquickjs_wifi_netif.h"
#include "esp_wifi_default.h"

enum {
    MESH_CLOSE_NATIVE, MESH_CLOSE_STOP, MESH_CLOSE_NETIFS, MESH_CLOSE_LEASE,
    MESH_CLOSE_SHUTDOWN, MESH_CLOSE_INIT, MESH_CLOSE_RESTORE_NETIFS,
    MESH_CLOSE_REPLAY, MESH_CLOSE_START, MESH_CLOSE_POST_START,
    MESH_CLOSE_RESTORE_STOP, MESH_CLOSE_RESTORE_RETIRE, MESH_CLOSE_RESTORE_LEASE,
    MESH_CLOSE_RELEASE, MESH_CLOSE_DONE,
};

/* Radio mutex owns this binding. A blocking SDK call pins busy, drops the
 * mutex, and reacquires it before publishing its result. No native callback
 * dereferences this object. It contains no runtime or JS heap pointer. */
struct wifi_radio_mesh {
    esp32_mquickjs_wifi_mesh_radio_status_t status;
    esp32_mquickjs_wifi_mesh_token_t native;
    esp32_mquickjs_wifi_radio_lease_t lease;
    esp_netif_t *sta, *ap;
    esp_err_t sta_detach_error, ap_detach_error, restore_error;
    TaskHandle_t worker;
    wifi_config_t staging, scratch;
    uint8_t close_phase, netifs_ready;
    bool release_ready, mutated, restore_attempted;
};

static bool wifi_radio_mesh_worker_locked(void)
{
    return s_mesh_radio && s_mesh_radio->worker == xTaskGetCurrentTaskHandle();
}

static bool wifi_radio_mesh_exact_locked(const esp32_mquickjs_wifi_mesh_radio_token_t *token)
{
    return token && token->identity && s_mesh_radio &&
        token->identity == s_mesh_radio->status.token.identity &&
        token->generation == s_mesh_radio->status.token.generation &&
        token->identity == s_radio.lifecycle.identity && token->generation == s_radio.lifecycle.generation;
}

static bool wifi_radio_mesh_stop_owner_locked(const esp32_mquickjs_wifi_radio_lease_t *lease)
{
    return wifi_radio_mesh_worker_locked() && wifi_radio_mesh_exact_locked(&s_mesh_radio->status.token) &&
        s_mesh_radio->status.closing && s_mesh_radio->status.native.native_retired &&
        lease->identity == s_mesh_radio->lease.identity && lease->generation == s_mesh_radio->lease.generation;
}

static bool wifi_radio_mesh_release_owner_locked(const esp32_mquickjs_wifi_radio_lease_t *lease)
{
    return wifi_radio_mesh_stop_owner_locked(lease) && s_mesh_radio->release_ready &&
        !s_radio.started && !s_radio.stop_required && !s_mesh_radio->sta && !s_mesh_radio->ap;
}

static bool wifi_radio_mesh_try_lock(void)
{
    return atomic_load_explicit(&s_operation_mutex_state, memory_order_acquire) == 2 &&
        xSemaphoreTake(s_operation_mutex, 0) == pdTRUE;
}

static void wifi_radio_mesh_snapshot_locked(wifi_radio_mesh_t *binding)
{
    if (binding->native.identity)
        (void)esp32_mquickjs_wifi_mesh_sdk_status(&binding->native, &binding->status.native);
    binding->status.reserved_bytes = sizeof(*binding) + binding->status.native.reserved_bytes +
        (s_config_restart.snapshot ? sizeof(*s_config_restart.snapshot) : 0);
    binding->status.restart_required |= binding->status.native.restart_required || s_radio.restart_required;
}

/* Called only after zero-owner admission or after physical retirement of the
 * previous Mesh lease. The same central registry and identity domain are used
 * for both the live Mesh and the temporary restoration START. */
static esp_err_t wifi_radio_mesh_grant_locked(wifi_radio_mesh_t *binding, wifi_mode_t mode)
{
    if (!wifi_radio_mesh_exact_locked(&binding->status.token) || binding->lease.acquired ||
        s_radio.operation.identity || s_radio.wake_locks || s_radio.promiscuous_claimed)
        return ESP_ERR_INVALID_STATE;
    for (unsigned i = 0; i < WIFI_RADIO_MAX_LEASES; ++i)
        if (s_radio.leases[i].identity) return ESP_ERR_INVALID_STATE;
    if (!s_radio.next_lease_identity) return ESP_ERR_NO_MEM;
    taskENTER_CRITICAL(&s_radio.lock);
    binding->lease = (esp32_mquickjs_wifi_radio_lease_t){
        .generation = s_radio.generation, .identity = s_radio.next_lease_identity++,
        .client = ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MESH, .acquired = true};
    s_radio.leases[0] = (wifi_radio_live_lease_t){.identity = binding->lease.identity,
        .client = binding->lease.client, .required_mode = mode};
    s_radio.clients[binding->lease.client]++;
    taskEXIT_CRITICAL(&s_radio.lock);
    binding->release_ready = false;
    return ESP_OK;
}

static esp_err_t wifi_radio_mesh_dhcp_stop(esp_netif_t *netif, bool client)
{
    if (!netif) return ESP_OK;
    esp_err_t error;
    if (client) error = esp_netif_dhcpc_stop(netif);
    else {
#if CONFIG_LWIP_DHCPS
        error = esp_netif_dhcps_stop(netif);
#else
        return ESP_OK;
#endif
    }
    if (error == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) error = ESP_OK;
    if (error) return error;
    esp_netif_dhcp_status_t state;
    error = client ? esp_netif_dhcpc_get_status(netif, &state) : esp_netif_dhcps_get_status(netif, &state);
    return !error && state != ESP_NETIF_DHCP_STOPPED ? ESP_ERR_INVALID_RESPONSE : error;
}

/* Checked equivalent of the SDK assert-based Mesh factory. Even failed attach
 * can publish the SDK's global netif pointer, so the partial netif is retained
 * and retired by the existing detach/fence helper, never destroyed inline. */
static esp_err_t wifi_radio_mesh_netifs_create_locked(wifi_radio_mesh_t *binding)
{
    esp_netif_t *station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_t *access_point = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if ((station && station != binding->sta) || (access_point && access_point != binding->ap))
        return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp_netif_init();
    if (error) return error;
    for (unsigned i = 0; i < 2; ++i) {
        bool sta = i == 0;
        if (binding->netifs_ready & (1U << i)) continue;
        esp_netif_t **output = sta ? &binding->sta : &binding->ap;
        if (*output) {
            /* Previous failed attach/handler suffix: detach that exact partial
             * object before creating a replacement. The other netif survives. */
            error = esp32_mquickjs_wifi_netif_retire(output,
                sta ? &binding->sta_detach_error : &binding->ap_detach_error);
            if (error) return error;
        }
        esp_netif_inherent_config_t base = *(sta ? ESP_NETIF_BASE_DEFAULT_WIFI_STA : ESP_NETIF_BASE_DEFAULT_WIFI_AP);
        base.flags &= ~(ESP_NETIF_DHCP_CLIENT | ESP_NETIF_DHCP_SERVER);
        esp_netif_config_t config = {.base = &base,
            .stack = sta ? ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA : ESP_NETIF_NETSTACK_DEFAULT_WIFI_AP};
        *output = esp_netif_new(&config);
        if (!*output) return ESP_ERR_NO_MEM;
        error = sta ? esp_netif_attach_wifi_station(*output) : esp_netif_attach_wifi_ap(*output);
        if (!error) error = sta ? esp_wifi_set_default_wifi_sta_handlers() : esp_wifi_set_default_wifi_ap_handlers();
        if (!error) error = wifi_radio_mesh_dhcp_stop(*output, sta);
        if (error) return error;
        binding->netifs_ready |= 1U << i;
    }
    binding->status.netifs_retired = false;
    return ESP_OK;
}

static esp_err_t wifi_radio_mesh_netifs_retire_locked(wifi_radio_mesh_t *binding)
{
    esp_err_t error = esp32_mquickjs_wifi_netif_retire(&binding->ap, &binding->ap_detach_error);
    if (!error) error = esp32_mquickjs_wifi_netif_retire(&binding->sta, &binding->sta_detach_error);
    if (!binding->sta) binding->netifs_ready &= ~1U;
    if (!binding->ap) binding->netifs_ready &= ~2U;
    if (!error) binding->status.netifs_retired = true;
    return error;
}

static esp_err_t wifi_radio_mesh_mode_locked(wifi_mode_t mode)
{
    esp_err_t error = esp_wifi_set_mode(mode);
    wifi_mode_t actual;
    if (!error) error = esp_wifi_get_mode(&actual);
    if (!error && actual != mode) error = ESP_ERR_INVALID_RESPONSE;
    if (!error) {
        taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = mode; taskEXIT_CRITICAL(&s_radio.lock);
    }
    return error;
}

static esp_err_t wifi_radio_mesh_config_locked(wifi_radio_mesh_t *binding, wifi_interface_t interface)
{
    esp_err_t error = esp_wifi_set_config(interface, &binding->staging);
    if (!error) error = esp_wifi_get_config(interface, &binding->scratch);
    /* SDK fills defaults into unspecified fields. Verify the security and
     * network identity that may become visible at the temporary START. */
    if (!error && interface == WIFI_IF_STA &&
        (memcmp(binding->scratch.sta.ssid, binding->staging.sta.ssid, sizeof(binding->staging.sta.ssid)) ||
         memcmp(binding->scratch.sta.password, binding->staging.sta.password, sizeof(binding->staging.sta.password))))
        error = ESP_ERR_INVALID_RESPONSE;
    if (!error && interface == WIFI_IF_AP &&
        (binding->scratch.ap.ssid_len != binding->staging.ap.ssid_len ||
         memcmp(binding->scratch.ap.ssid, binding->staging.ap.ssid, binding->staging.ap.ssid_len) ||
         memcmp(binding->scratch.ap.password, binding->staging.ap.password, sizeof(binding->staging.ap.password)) ||
         binding->scratch.ap.authmode != binding->staging.ap.authmode ||
         binding->scratch.ap.ssid_hidden != binding->staging.ap.ssid_hidden ||
         binding->scratch.ap.max_connection != binding->staging.ap.max_connection))
        error = ESP_ERR_INVALID_RESPONSE;
    esp32_mquickjs_wireless_secure_zero(&binding->scratch, sizeof(binding->scratch));
    esp32_mquickjs_wireless_secure_zero(&binding->staging, sizeof(binding->staging));
    return error;
}

static esp_err_t wifi_radio_mesh_prepare_locked(wifi_radio_mesh_t *binding,
    const esp32_mquickjs_wifi_mesh_config_t *config)
{
    esp_err_t error;
#define PREPARE(name, call) do { binding->status.stage = name; error = (call); if (error) return error; } while (0)
    PREPARE("mesh-checkpoint", wifi_radio_checkpoint_restart_locked(&binding->status.token, binding->status.original_mode));
    binding->status.checkpoint_complete = true;
    PREPARE("mesh-radio-init", wifi_radio_initialize());
    PREPARE("mesh-netifs", wifi_radio_mesh_netifs_create_locked(binding));
    binding->mutated = true;
    binding->status.stage = "mesh-ram";
    error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    taskENTER_CRITICAL(&s_radio.lock);
    s_radio.storage = error ? (wifi_storage_t)-1 : WIFI_STORAGE_RAM;
    taskEXIT_CRITICAL(&s_radio.lock);
    if (error) return error;
    PREPARE("mesh-station-mode", wifi_radio_mesh_mode_locked(WIFI_MODE_STA));
    /* Do not transiently advertise the predecessor AP credentials, or connect
     * the predecessor Station while preparing Mesh's 2.4 GHz driver mode. */
    memset(&binding->staging, 0, sizeof(binding->staging));
    PREPARE("mesh-station-clear", wifi_radio_mesh_config_locked(binding, WIFI_IF_STA));
#if CONFIG_SOC_WIFI_SUPPORT_5G
    PREPARE("mesh-band-station-start", wifi_radio_start_stopped_locked(WIFI_MODE_STA, true));
    wifi_band_mode_t band;
    PREPARE("mesh-band-read", esp_wifi_get_band_mode(&band));
    if (band != WIFI_BAND_MODE_2G_ONLY) {
        PREPARE("mesh-band-events", wifi_radio_begin_events(RADIO_EVENTS_RESTART, WIFI_MODE_STA));
        PREPARE("mesh-band-select", esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY));
        PREPARE("mesh-band-fence", wifi_radio_wait_events());
    }
    PREPARE("mesh-band-verify", esp_wifi_get_band_mode(&band));
    if (band != WIFI_BAND_MODE_2G_ONLY) return ESP_ERR_INVALID_RESPONSE;
    PREPARE("mesh-band-stop", wifi_radio_stop_locked());
#endif
    PREPARE("mesh-apsta-mode", wifi_radio_mesh_mode_locked(WIFI_MODE_APSTA));
    for (unsigned i = 0; i < 2; ++i) {
        wifi_interface_t interface = i == 0 ? WIFI_IF_STA : WIFI_IF_AP;
        uint8_t protocol;
        wifi_bandwidth_t bandwidth;
        PREPARE("mesh-protocol", esp_wifi_set_protocol(interface, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
        PREPARE("mesh-protocol-read", esp_wifi_get_protocol(interface, &protocol));
        if (protocol != (WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N)) return ESP_ERR_INVALID_RESPONSE;
        PREPARE("mesh-bandwidth", esp_wifi_set_bandwidth(interface, WIFI_BW20));
        PREPARE("mesh-bandwidth-read", esp_wifi_get_bandwidth(interface, &bandwidth));
        if (bandwidth != WIFI_BW20) return ESP_ERR_INVALID_RESPONSE;
    }
    memset(&binding->staging, 0, sizeof(binding->staging));
    wifi_ap_config_t *ap = &binding->staging.ap;
    memcpy(ap->ssid, "mesh-", 5);
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 6; ++i) {
        ap->ssid[5 + i * 2] = hex[config->network.mesh_id.addr[i] >> 4];
        ap->ssid[6 + i * 2] = hex[config->network.mesh_id.addr[i] & 15];
    }
    ap->ssid_len = 17;
    ap->ssid_hidden = 1;
    ap->channel = config->network.channel ? config->network.channel : 1;
    ap->authmode = config->ap_authmode;
    ap->max_connection = config->network.mesh_ap.max_connection;
    ap->beacon_interval = 100;
    ap->dtim_period = 2;
    memcpy(ap->password, config->network.mesh_ap.password, sizeof(ap->password));
    PREPARE("mesh-ap-config", wifi_radio_mesh_config_locked(binding, WIFI_IF_AP));
    PREPARE("mesh-lease", wifi_radio_mesh_grant_locked(binding, WIFI_MODE_APSTA));
    PREPARE("mesh-radio-start", wifi_radio_start_stopped_locked(WIFI_MODE_APSTA, true));
#undef PREPARE
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_mesh_begin(const esp32_mquickjs_wifi_mesh_config_t *config,
    bool allow_ap_restart, esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_radio_status_t *status)
{
    if (!token || token->identity || token->generation || !status) return ESP_ERR_INVALID_ARG;
    *status = (esp32_mquickjs_wifi_mesh_radio_status_t){.stage = "mesh-admission"};
    esp_err_t error = esp32_mquickjs_wifi_mesh_validate_config(config);
    if (error) { status->error = error; return error; }
    wifi_radio_operation_lock();
    wifi_radio_mesh_t *binding = NULL;
    error = ESP_ERR_INVALID_STATE;
    if (WIFI_RADIO_MESH_PENDING || WIFI_RADIO_NAN_PENDING || WIFI_RADIO_DPP_PENDING ||
        WIFI_RADIO_SMARTCONFIG_PENDING || WIFI_RADIO_WPS_PENDING || WIFI_RADIO_EAP_PENDING ||
        s_radio.fault_stage || s_radio.cleanup_stage || s_radio.restart_required ||
        s_radio.started || s_radio.stop_required || s_tx_rate_lease.identity || s_tx_rate_lease.restore_pending ||
        s_interval.owner.identity || s_interval.restore_pending || s_interval.uncertain ||
        s_config_restart.snapshot || s_config_restart.captured || s_policy_restart.owner.identity ||
        s_vendor_ie.start_owner.identity || esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") ||
        esp_netif_get_handle_from_ifkey("WIFI_AP_DEF")) goto done;
    bool cold = !s_radio.driver_owned && s_radio.driver_state == ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;
    if (!cold && !wifi_radio_stop_snapshot_unchanged_locked()) goto done;
    wifi_mode_t mode = cold ? WIFI_MODE_STA : s_radio.effective_mode;
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) goto done;
    if ((mode & WIFI_MODE_AP) && !allow_ap_restart) { error = ESP_ERR_INVALID_ARG; goto done; }
    /* Both live and restoration leases are reserved before SDK mutation. */
    if (!s_radio.next_lease_identity || (!cold && s_radio.next_lease_identity == UINT32_MAX) ||
        s_radio.generation == UINT32_MAX) { error = ESP_ERR_NO_MEM; goto done; }
    binding = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, sizeof(*binding),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!binding) { error = ESP_ERR_NO_MEM; goto done; }
    error = wifi_radio_begin_lifecycle_locked(NULL, NULL, NULL, &binding->status.token);
    if (error) { esp32_mquickjs_memory_payload_free(binding); binding = NULL; goto done; }
    binding->status.cold = cold;
    binding->status.original_mode = mode;
    binding->worker = xTaskGetCurrentTaskHandle();
    s_mesh_radio = binding;
    *token = binding->status.token;
    binding->status.stage = "mesh-native-reserve";
    error = esp32_mquickjs_wifi_mesh_sdk_reserve(s_radio.generation, config, &binding->native);
    if (!error) error = wifi_radio_mesh_prepare_locked(binding, config);
    if (!error) {
        binding->status.stage = "mesh-native-start";
        binding->status.busy = true;
        binding->worker = NULL;
        wifi_radio_operation_unlock();
        error = esp32_mquickjs_wifi_mesh_sdk_start(&binding->native);
        wifi_radio_operation_lock();
        binding->worker = xTaskGetCurrentTaskHandle();
        binding->status.busy = false;
        if (!error && binding->status.closing) error = ESP_ERR_INVALID_STATE;
        if (!error) {
            wifi_mode_t actual;
            error = esp_wifi_get_mode(&actual);
            if (!error && actual != WIFI_MODE_STA && actual != WIFI_MODE_AP && actual != WIFI_MODE_APSTA)
                error = ESP_ERR_INVALID_RESPONSE;
            if (!error) {
                taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = actual; taskEXIT_CRITICAL(&s_radio.lock);
            }
        }
    }
    binding->status.error = error;
    binding->status.ready = !error;
    if (error) binding->status.closing = true;
    else binding->status.stage = NULL;
    wifi_radio_mesh_snapshot_locked(binding);
    if (binding->status.restart_required) {
        (void)wifi_radio_record_fault(binding->status.native.stage ? binding->status.native.stage : binding->status.stage,
            error ? error : ESP_ERR_INVALID_STATE);
        taskENTER_CRITICAL(&s_radio.lock); s_radio.restart_required = true; taskEXIT_CRITICAL(&s_radio.lock);
    }
    binding->worker = NULL;
    *status = binding->status;
done:
    if (!binding) status->error = error;
    wifi_radio_operation_unlock();
    return error;
}

/* DHCP policy uses the synchronous native control capture, not the lossy Mesh
 * observation queue. A changing role invalidates the sample. A DHCP lease is
 * local IP readiness, never a claim that the root has Internet connectivity. */
static bool wifi_radio_mesh_ip_parent(const esp32_mquickjs_wifi_mesh_status_t *sample)
{
    /* MESH_STA is a standalone station connected directly to the router.
     * Mesh nodes/leaves route through their parent and must not run DHCP. */
    return sample->native_snapshot_valid && sample->parent_known && sample->parent_connected &&
        !sample->closing && (sample->root || (sample->type_known && sample->type == MESH_STA));
}

static esp_err_t wifi_radio_mesh_network(wifi_radio_mesh_t *binding,
    esp32_mquickjs_wifi_mesh_status_t *sample, esp_netif_ip_info_t *ip, bool *dhcp, bool *ready)
{
    *ip = (esp_netif_ip_info_t){0}; *dhcp = false; *ready = false;
    if (!wifi_radio_mesh_ip_parent(sample)) return wifi_radio_mesh_dhcp_stop(binding->sta, true);
    esp_netif_dhcp_status_t state;
    esp_err_t error = esp_netif_dhcpc_get_status(binding->sta, &state);
    if (!error && state != ESP_NETIF_DHCP_STARTED) error = esp_netif_dhcpc_start(binding->sta);
    if (error == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) error = ESP_OK;
    if (!error) error = esp_netif_dhcpc_get_status(binding->sta, &state);
    if (!error && state != ESP_NETIF_DHCP_STARTED) error = ESP_ERR_INVALID_RESPONSE;
    if (!error) error = esp_netif_get_ip_info(binding->sta, ip);
    esp32_mquickjs_wifi_mesh_status_t after;
    esp_err_t observed = esp32_mquickjs_wifi_mesh_sdk_status(&binding->native, &after);
    if (error || observed || !wifi_radio_mesh_ip_parent(&after) || after.events != sample->events) {
        esp_err_t stopped = wifi_radio_mesh_dhcp_stop(binding->sta, true);
        *ip = (esp_netif_ip_info_t){0};
        return error ? error : observed ? observed : stopped;
    }
    *dhcp = true;
    *ready = ip->ip.addr != 0 && esp_netif_is_netif_up(binding->sta);
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_radio_mesh_poll(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    if (!wifi_radio_mesh_exact_locked(token)) { wifi_radio_operation_unlock(); return ESP_ERR_INVALID_STATE; }
    wifi_radio_mesh_t *binding = s_mesh_radio;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!binding->status.ready || binding->status.closing || binding->status.busy) goto done;
    binding->status.busy = true;
    wifi_radio_operation_unlock();
    error = esp32_mquickjs_wifi_mesh_sdk_inspect(&binding->native);
    esp32_mquickjs_wifi_mesh_status_t native;
    esp_err_t observed = esp32_mquickjs_wifi_mesh_sdk_status(&binding->native, &native);
    esp_netif_ip_info_t ip = {0}; bool dhcp = false, ready = false;
    esp_err_t network = observed ? observed : wifi_radio_mesh_network(binding, &native, &ip, &dhcp, &ready);
    wifi_radio_operation_lock();
    binding->status.busy = false;
    binding->status.network_error = network;
    binding->status.dhcp_started = dhcp;
    binding->status.ip_ready = ready && !binding->status.closing;
    binding->status.ip = binding->status.ip_ready ? ip : (esp_netif_ip_info_t){0};
    if (!error) error = network;
done:
    wifi_radio_mesh_snapshot_locked(binding);
    *status = binding->status;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_mesh_status(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_radio_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (!wifi_radio_mesh_try_lock()) return ESP_ERR_TIMEOUT;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (wifi_radio_mesh_exact_locked(token)) {
        wifi_radio_mesh_snapshot_locked(s_mesh_radio);
        *status = s_mesh_radio->status;
        if (!wifi_radio_mesh_ip_parent(&status->native) || status->closing) {
            status->ip_ready = false;
            status->ip = (esp_netif_ip_info_t){0};
        }
        error = ESP_OK;
    }
    wifi_radio_operation_unlock();
    return error;
}

static esp_err_t wifi_radio_mesh_io(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    const esp32_mquickjs_wifi_mesh_send_t *send, bool to_ds)
{
    wifi_radio_operation_lock();
    if (!wifi_radio_mesh_exact_locked(token) || !s_mesh_radio->status.ready ||
        s_mesh_radio->status.closing || s_mesh_radio->status.busy) {
        wifi_radio_operation_unlock(); return ESP_ERR_INVALID_STATE;
    }
    wifi_radio_mesh_t *binding = s_mesh_radio;
    binding->status.busy = true;
    wifi_radio_operation_unlock();
    esp_err_t error = send ? esp32_mquickjs_wifi_mesh_sdk_send(&binding->native, send) :
        esp32_mquickjs_wifi_mesh_sdk_receive(&binding->native, to_ds);
    wifi_radio_operation_lock();
    binding->status.busy = false;
    wifi_radio_mesh_snapshot_locked(binding);
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_mesh_send(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    const esp32_mquickjs_wifi_mesh_send_t *send)
{
    return send ? wifi_radio_mesh_io(token, send, false) : ESP_ERR_INVALID_ARG;
}

esp_err_t esp32_mquickjs_wifi_radio_mesh_receive(const esp32_mquickjs_wifi_mesh_radio_token_t *token, bool to_ds)
{
    return wifi_radio_mesh_io(token, NULL, to_ds);
}

esp_err_t esp32_mquickjs_wifi_radio_mesh_control(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_control_t *control)
{
    if (!control) return ESP_ERR_INVALID_ARG;
    wifi_radio_operation_lock();
    if (!wifi_radio_mesh_exact_locked(token) || !s_mesh_radio->status.ready ||
        s_mesh_radio->status.closing || s_mesh_radio->status.busy) {
        wifi_radio_operation_unlock(); return ESP_ERR_INVALID_STATE;
    }
    wifi_radio_mesh_t *binding = s_mesh_radio;
    binding->status.busy = true;
    wifi_radio_operation_unlock();
    esp_err_t error = esp32_mquickjs_wifi_mesh_sdk_control(&binding->native, control);
    wifi_radio_operation_lock();
    binding->status.busy = false;
    wifi_radio_mesh_snapshot_locked(binding);
    wifi_radio_operation_unlock();
    return error;
}

/* Copy/commit never run SDK work. They may overlap a pinned send but cannot
 * overlap the final owner release because both hold the Radio mutex. */
#define MESH_COPY(call) do { \
    if (!wifi_radio_mesh_try_lock()) return ESP_ERR_TIMEOUT; \
    esp_err_t error = ESP_ERR_INVALID_STATE; \
    if (wifi_radio_mesh_exact_locked(token) && s_mesh_radio->status.ready && !s_mesh_radio->status.closing) error = (call); \
    wifi_radio_operation_unlock(); return error; \
} while (0)
esp_err_t esp32_mquickjs_wifi_radio_mesh_scan_commit(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    uint32_t scan_identity, uint32_t sequence)
{
    MESH_COPY(esp32_mquickjs_wifi_mesh_sdk_scan_commit(&s_mesh_radio->native, scan_identity, sequence));
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_message_copy(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    bool to_ds, esp32_mquickjs_wifi_mesh_message_t *message, uint8_t *bytes, size_t capacity)
{
    MESH_COPY(esp32_mquickjs_wifi_mesh_sdk_message_copy(&s_mesh_radio->native, to_ds, message, bytes, capacity));
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_message_commit(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    bool to_ds, uint32_t sequence)
{
    MESH_COPY(esp32_mquickjs_wifi_mesh_sdk_message_commit(&s_mesh_radio->native, to_ds, sequence));
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_event_copy(const esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_notice_t *notice)
{
    MESH_COPY(esp32_mquickjs_wifi_mesh_sdk_event_copy(&s_mesh_radio->native, notice));
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_event_commit(const esp32_mquickjs_wifi_mesh_radio_token_t *token, uint32_t sequence)
{
    MESH_COPY(esp32_mquickjs_wifi_mesh_sdk_event_commit(&s_mesh_radio->native, sequence));
}
#undef MESH_COPY

static esp_err_t wifi_radio_mesh_close_locked(wifi_radio_mesh_t *binding)
{
    esp_err_t error = ESP_OK;
    while (binding->close_phase != MESH_CLOSE_DONE) {
        switch (binding->close_phase) {
        case MESH_CLOSE_NATIVE:
            binding->status.cleanup_stage = "mesh-native-retire";
            if (binding->native.identity) {
                binding->status.busy = true;
                binding->worker = NULL;
                wifi_radio_operation_unlock();
                error = esp32_mquickjs_wifi_mesh_sdk_close(&binding->native);
                wifi_radio_operation_lock();
                binding->worker = xTaskGetCurrentTaskHandle();
                binding->status.busy = false;
                wifi_radio_mesh_snapshot_locked(binding);
            } else binding->status.native.native_retired = true;
            break;
        case MESH_CLOSE_STOP: {
            binding->status.cleanup_stage = "mesh-radio-stop";
            /* A checkpoint can retain a known read/fence suffix. Complete it
             * before retiring its temporary driver configuration. */
            if (!binding->status.checkpoint_complete && s_config_restart.snapshot) {
                /* Preserve the original capture fault while finishing known
                 * reads/fences. A healthy-start gate cannot be used here: only
                 * the subsequent physical rebuild may clear that old fault. */
                error = wifi_radio_restart_configs_capture_locked(&binding->status.token, binding->status.original_mode);
                if (!error) error = wifi_radio_stop_locked();
                if (!error) error = wifi_radio_check_stopped_lifecycle_locked(&binding->status.token, true);
                if (!error && !wifi_radio_restart_checkpoint_matches_locked(&binding->status.token, binding->status.original_mode))
                    error = ESP_ERR_INVALID_STATE;
                if (error && s_config_restart.snapshot->capture_phase == RESTART_CAPTURE_FAILED) {
                    binding->status.restart_required = true;
                    taskENTER_CRITICAL(&s_radio.lock); s_radio.restart_required = true; taskEXIT_CRITICAL(&s_radio.lock);
                }
                if (error) break;
                binding->status.checkpoint_complete = true;
            }
            error = wifi_radio_mesh_dhcp_stop(binding->sta, true);
            if (error) break;
            if (s_radio.driver_owned && s_radio.stop_required && !s_radio.stop_submitted) {
                wifi_mode_t actual;
                error = esp_wifi_get_mode(&actual);
                if (error) break;
                if (actual != WIFI_MODE_NULL && actual != WIFI_MODE_STA && actual != WIFI_MODE_AP && actual != WIFI_MODE_APSTA) {
                    error = ESP_ERR_INVALID_RESPONSE; break;
                }
                taskENTER_CRITICAL(&s_radio.lock); s_radio.effective_mode = actual; taskEXIT_CRITICAL(&s_radio.lock);
            }
            error = wifi_radio_stop_lease_locked(binding->lease.acquired ? &binding->lease : NULL, false);
            if (!error) {
                binding->status.stopped = true;
                binding->status.dhcp_started = binding->status.ip_ready = false;
                binding->status.ip = (esp_netif_ip_info_t){0};
            }
            break;
        }
        case MESH_CLOSE_NETIFS:
        case MESH_CLOSE_RESTORE_RETIRE:
            binding->status.cleanup_stage = "mesh-netif-retire";
            error = wifi_radio_mesh_netifs_retire_locked(binding);
            break;
        case MESH_CLOSE_LEASE:
        case MESH_CLOSE_RESTORE_LEASE:
            binding->status.cleanup_stage = "mesh-lease-retire";
            binding->release_ready = true;
            if (binding->lease.acquired) wifi_radio_release_locked(&binding->lease);
            error = binding->lease.acquired ? ESP_ERR_INVALID_STATE : ESP_OK;
            if (!error && !binding->mutated && !s_config_restart.snapshot && !binding->status.cold) {
                binding->status.restored = true;
                binding->close_phase = MESH_CLOSE_RELEASE;
                continue;
            }
            break;
        case MESH_CLOSE_SHUTDOWN:
            binding->status.cleanup_stage = "mesh-radio-deinit";
            error = wifi_radio_shutdown_locked();
            if (error && s_radio.cleanup_stage &&
                (!strcmp(s_radio.cleanup_stage, "deinit") || !strcmp(s_radio.cleanup_stage, "wapi-deinit"))) {
                /* The physical deinit call itself failed, unlike a retained
                 * callback/fence suffix. Repeating it cannot prove ownership. */
                binding->status.restart_required = true;
                taskENTER_CRITICAL(&s_radio.lock); s_radio.restart_required = true; taskEXIT_CRITICAL(&s_radio.lock);
            }
            if (!error && binding->status.cold) {
                binding->status.restored = true;
                binding->close_phase = MESH_CLOSE_RELEASE;
                continue;
            }
            break;
        case MESH_CLOSE_INIT:
            binding->status.cleanup_stage = "mesh-restore-init";
            error = wifi_radio_initialize();
            if (error) binding->restore_error = error;
            break;
        case MESH_CLOSE_RESTORE_NETIFS:
            binding->status.cleanup_stage = "mesh-restore-netifs";
            error = wifi_radio_mesh_netifs_create_locked(binding);
            break;
        case MESH_CLOSE_REPLAY:
            binding->status.cleanup_stage = "mesh-restore-config";
            error = wifi_radio_replay_restart_locked(&binding->status.token, binding->status.original_mode);
            if (error) binding->restore_error = error;
            break;
        case MESH_CLOSE_START:
            binding->status.cleanup_stage = "mesh-restore-start";
            error = wifi_radio_restart_configs_pre_start_locked(&binding->status.token, binding->status.original_mode);
            if (!error) error = wifi_radio_mesh_grant_locked(binding, binding->status.original_mode);
            if (!error) {
                binding->restore_attempted = true;
                error = wifi_radio_restart_configs_start_locked(&binding->status.token, binding->status.original_mode, &binding->lease);
            }
            if (error) binding->restore_error = error;
            break;
        case MESH_CLOSE_POST_START:
            binding->status.cleanup_stage = "mesh-restore-acceptance";
            error = wifi_radio_policy_restart_replay_locked(&binding->status.token, true);
            if (!error && s_policy_restart.completed != s_policy_restart.snapshot.mask) error = ESP_ERR_INVALID_STATE;
            if (!error) error = wifi_radio_restart_configs_post_start_locked(&binding->status.token);
            if (!error) error = wifi_radio_restart_configs_commit_locked(&binding->status.token, binding->status.original_mode);
            if (error) binding->restore_error = error;
            break;
        case MESH_CLOSE_RESTORE_STOP:
            binding->status.cleanup_stage = "mesh-restore-stop";
            error = wifi_radio_stop_lease_locked(&binding->lease, false);
            if (!error) binding->status.restored = true;
            break;
        case MESH_CLOSE_RELEASE:
            binding->status.cleanup_stage = "mesh-storage-retire";
            if (binding->native.identity) error = esp32_mquickjs_wifi_mesh_sdk_release(&binding->native);
            break;
        default:
            return ESP_ERR_INVALID_STATE;
        }
        if (error) return error;
        ++binding->close_phase;
    }
    return ESP_OK;
}

static esp_err_t wifi_radio_mesh_close(esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_radio_status_t *status, bool recover)
{
    if (!token || !status) return ESP_ERR_INVALID_ARG;
    if (!token->identity && !token->generation) return ESP_OK;
    wifi_radio_operation_lock();
    if (!wifi_radio_mesh_exact_locked(token)) { wifi_radio_operation_unlock(); return ESP_ERR_INVALID_STATE; }
    wifi_radio_mesh_t *binding = s_mesh_radio;
    if (recover) {
        wifi_radio_mesh_snapshot_locked(binding);
        if (!binding->restore_error || !binding->status.native.native_retired || binding->status.busy ||
            binding->status.restart_required || !binding->status.checkpoint_complete ||
            !wifi_radio_restart_checkpoint_matches_locked(&binding->status.token, binding->status.original_mode)) {
            *status = binding->status;
            wifi_radio_operation_unlock(); return ESP_ERR_INVALID_STATE;
        }
        if (!s_radio.next_lease_identity || s_radio.generation == UINT32_MAX) {
            *status = binding->status;
            wifi_radio_operation_unlock(); return ESP_ERR_NO_MEM;
        }
        /* Only this explicit call authorizes a fresh physical restore attempt.
         * The old native Mesh is already retired and is never initialized again.
         * Keep the source generation and all secrets frozen through STOP/drain. */
        binding->restore_error = ESP_OK;
        binding->restore_attempted = false;
        binding->status.restored = false;
        binding->close_phase = MESH_CLOSE_STOP;
    }
    binding->status.closing = true;
    binding->status.ready = binding->status.ip_ready = false;
    if (binding->native.identity) (void)esp32_mquickjs_wifi_mesh_sdk_seal(&binding->native);
    esp_err_t error = ESP_ERR_TIMEOUT;
    if (binding->status.busy) goto done;
    binding->worker = xTaskGetCurrentTaskHandle();
    error = binding->restore_error ? binding->restore_error : wifi_radio_mesh_close_locked(binding);
    binding->worker = NULL;
    binding->status.cleanup_error = error;
    if (!error) {
        wifi_radio_restart_configs_discard_locked(token);
        if (s_policy_restart.owner.identity == token->identity && s_policy_restart.owner.generation == token->generation)
            memset(&s_policy_restart, 0, sizeof(s_policy_restart));
        taskENTER_CRITICAL(&s_radio.lock); s_radio.lifecycle = (esp32_mquickjs_wifi_radio_lifecycle_t){0}; taskEXIT_CRITICAL(&s_radio.lock);
        binding->status.cleanup_stage = NULL;
        binding->status.reserved_bytes = 0;
        *status = binding->status;
        *token = (esp32_mquickjs_wifi_mesh_radio_token_t){0};
        s_mesh_radio = NULL;
        esp32_mquickjs_wireless_secure_zero(binding, sizeof(*binding));
        esp32_mquickjs_memory_payload_free(binding);
        wifi_radio_operation_unlock();
        return ESP_OK;
    }
done:
    wifi_radio_mesh_snapshot_locked(binding);
    if (binding->status.native.restart_required || binding->sta_detach_error || binding->ap_detach_error) {
        binding->status.restart_required = true;
        taskENTER_CRITICAL(&s_radio.lock); s_radio.restart_required = true; taskEXIT_CRITICAL(&s_radio.lock);
        (void)wifi_radio_record_fault(binding->status.cleanup_stage ? binding->status.cleanup_stage : "mesh-cleanup", error);
    }
    *status = binding->status;
    wifi_radio_operation_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_radio_mesh_close(esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_radio_status_t *status)
{
    return wifi_radio_mesh_close(token, status, false);
}

esp_err_t esp32_mquickjs_wifi_radio_mesh_recover(esp32_mquickjs_wifi_mesh_radio_token_t *token,
    esp32_mquickjs_wifi_mesh_radio_status_t *status)
{
    return wifi_radio_mesh_close(token, status, true);
}
