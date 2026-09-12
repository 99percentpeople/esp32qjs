#include "esp32_mquickjs_wifi_mesh_sdk.h"
#if ESP32_MQUICKJS_WIFI_MESH_AVAILABLE
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_wireless_core.h"
#include "freertos/FreeRTOS.h"
#include <limits.h>
#include <math.h>
#include <string.h>

typedef struct {
    esp32_mquickjs_wifi_mesh_status_t status;
    esp32_mquickjs_wifi_mesh_config_t config;
    esp32_mquickjs_wifi_mesh_notice_t events[ESP32_MQUICKJS_MESH_EVENT_SLOTS];
    esp32_mquickjs_wifi_mesh_message_t messages[2];
    uint8_t *receive_bytes;
    uint32_t next_sequence;
    unsigned event_head, event_count;
    uint32_t send_block_ms;
    bool encrypt_ie;
    uint8_t ie_key_length;
    esp32_mquickjs_wifi_mesh_scan_config_t scan_config;
    esp32_mquickjs_wifi_mesh_scan_record_t *scan_record;
} wifi_mesh_owner_t;

static portMUX_TYPE s_mesh_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_mesh_owner_t *s_mesh;
static uint32_t s_mesh_next_identity = 1;
static bool s_mesh_reserving;

static void mesh_increment(uint32_t *counter) { if (*counter != UINT32_MAX) ++*counter; }
static bool mesh_exact(const esp32_mquickjs_wifi_mesh_token_t *token)
{
    return token && token->identity && s_mesh && token->identity == s_mesh->status.token.identity &&
        token->generation == s_mesh->status.token.generation;
}

static bool mesh_secret_length(const uint8_t *bytes, size_t *length)
{
    *length = 0;
    while (*length < 64 && bytes[*length]) ++*length;
    /* Fixed SDK arrays permit exactly 64 bytes; shorter values are canonical,
     * with no hidden suffix that could disagree with C-string consumers. */
    for (size_t i = *length; i < 64; ++i) if (bytes[i]) return false;
    return true;
}

esp_err_t esp32_mquickjs_wifi_mesh_validate_config(const esp32_mquickjs_wifi_mesh_config_t *c)
{
    if (!c || c->network.crypto_funcs != &g_wifi_default_mesh_crypto_funcs ||
        c->network.channel > 14 || !c->network.router.ssid_len || c->network.router.ssid_len > 32 ||
        (c->topology != MESH_TOPO_TREE && c->topology != MESH_TOPO_CHAIN) ||
        (unsigned)c->type > MESH_STA || !c->max_layer ||
        c->max_layer > (c->topology == MESH_TOPO_TREE ? 25 : 1000) ||
        !isfinite(c->vote_percentage) || c->vote_percentage <= 0 || c->vote_percentage > 1 ||
        !c->capacity || c->capacity > ESP32_MQUICKJS_MESH_MAX_NODES ||
        c->receive_queue < 16 || c->receive_queue > 128 || !c->send_block_ms || c->send_block_ms > 60000 ||
        !c->network.mesh_ap.max_connection || c->network.mesh_ap.max_connection > 10 ||
        c->network.mesh_ap.nonmesh_max_connection > 10 - c->network.mesh_ap.max_connection ||
        (c->ap_authmode != WIFI_AUTH_OPEN && c->ap_authmode != WIFI_AUTH_WPA_PSK &&
         c->ap_authmode != WIFI_AUTH_WPA2_PSK && c->ap_authmode != WIFI_AUTH_WPA_WPA2_PSK))
        return ESP_ERR_INVALID_ARG;
    uint8_t any = 0;
    for (unsigned i = 0; i < 6; ++i) any |= c->network.mesh_id.addr[i];
    if (!any) return ESP_ERR_INVALID_ARG;
    size_t router_length, ap_length;
    if (!mesh_secret_length(c->network.router.password, &router_length) ||
        !mesh_secret_length(c->network.mesh_ap.password, &ap_length) ||
        (router_length && router_length < 8) ||
        (c->ap_authmode == WIFI_AUTH_OPEN ? ap_length != 0 : ap_length < 8)) return ESP_ERR_INVALID_ARG;
    if (c->encrypt_ie) {
        if (c->ie_key_length < 8 || c->ie_key_length > sizeof(c->ie_key)) return ESP_ERR_INVALID_ARG;
        for (unsigned i = 0; i < c->ie_key_length; ++i)
            if ((unsigned char)c->ie_key[i] < 32 || (unsigned char)c->ie_key[i] > 126) return ESP_ERR_INVALID_ARG;
    } else if (c->ie_key_length) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_reserve(uint32_t generation,
    const esp32_mquickjs_wifi_mesh_config_t *config, esp32_mquickjs_wifi_mesh_token_t *token)
{
    if (!generation || !token || token->identity || token->generation) return ESP_ERR_INVALID_ARG;
    esp_err_t error = esp32_mquickjs_wifi_mesh_validate_config(config);
    if (error) return error;
    portENTER_CRITICAL(&s_mesh_lock);
    error = s_mesh || s_mesh_reserving ? ESP_ERR_INVALID_STATE : !s_mesh_next_identity ? ESP_ERR_NO_MEM : ESP_OK;
    uint32_t identity = 0;
    if (!error) { s_mesh_reserving = true; identity = s_mesh_next_identity++; }
    portEXIT_CRITICAL(&s_mesh_lock);
    if (error) return error;
    wifi_mesh_owner_t *owner = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, sizeof(*owner),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    uint8_t *bytes = owner ? esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 2, MESH_MTU,
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_POOL) : NULL;
    if (!bytes) {
        esp32_mquickjs_memory_payload_free(owner);
        portENTER_CRITICAL(&s_mesh_lock); s_mesh_reserving = false; portEXIT_CRITICAL(&s_mesh_lock);
        return ESP_ERR_NO_MEM;
    }
    owner->status.token = (esp32_mquickjs_wifi_mesh_token_t){generation, identity};
    owner->status.reserved_bytes = sizeof(*owner) + 2 * MESH_MTU;
    owner->config = *config;
    owner->send_block_ms = config->send_block_ms;
    owner->receive_bytes = bytes;
    owner->next_sequence = 1;
    portENTER_CRITICAL(&s_mesh_lock);
    s_mesh = owner; s_mesh_reserving = false; *token = owner->status.token;
    portEXIT_CRITICAL(&s_mesh_lock);
    return ESP_OK;
}

static wifi_mesh_owner_t *mesh_enter(const esp32_mquickjs_wifi_mesh_token_t *token, bool running)
{
    portENTER_CRITICAL(&s_mesh_lock);
    wifi_mesh_owner_t *owner = mesh_exact(token) && !s_mesh->status.busy && !s_mesh->status.closing &&
        (!running || s_mesh->status.started) ? s_mesh : NULL;
    if (owner) owner->status.busy = true;
    portEXIT_CRITICAL(&s_mesh_lock);
    return owner;
}

static void mesh_stage(wifi_mesh_owner_t *owner, const char *stage)
{
    portENTER_CRITICAL(&s_mesh_lock); owner->status.stage = stage; portEXIT_CRITICAL(&s_mesh_lock);
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_start(const esp32_mquickjs_wifi_mesh_token_t *token)
{
    wifi_mesh_owner_t *owner = mesh_enter(token, false);
    if (!owner) return ESP_ERR_INVALID_STATE;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (owner->status.init_attempted) {
        portENTER_CRITICAL(&s_mesh_lock); owner->status.busy = false; portEXIT_CRITICAL(&s_mesh_lock);
        return ESP_ERR_INVALID_STATE;
    }
    mesh_stage(owner, "mesh-init");
    portENTER_CRITICAL(&s_mesh_lock); owner->status.init_attempted = true; portEXIT_CRITICAL(&s_mesh_lock);
    error = esp_mesh_init();
    portENTER_CRITICAL(&s_mesh_lock);
    owner->status.initialized = !error;
    /* The pinned native init can allocate its boot mutex and alter AP state
     * before returning failure. deinit then returns success without cleaning
     * an unpublished instance. Never mistake that for recoverable cleanup. */
    if (error) owner->status.restart_required = true;
    portEXIT_CRITICAL(&s_mesh_lock);
    if (error) goto done;
#define STEP(stage, call) do { \
    portENTER_CRITICAL(&s_mesh_lock); bool closing = owner->status.closing; portEXIT_CRITICAL(&s_mesh_lock); \
    if (closing) { error = ESP_ERR_INVALID_STATE; goto done; } \
    mesh_stage(owner, stage); error = (call); if (error) goto done; \
} while (0)
    STEP("mesh-topology", esp_mesh_set_topology(owner->config.topology));
    STEP("mesh-max-layer", esp_mesh_set_max_layer(owner->config.max_layer));
    STEP("mesh-capacity", esp_mesh_set_capacity_num(owner->config.capacity));
    STEP("mesh-vote-percentage", esp_mesh_set_vote_percentage(owner->config.vote_percentage));
    STEP("mesh-receive-queue", esp_mesh_set_xon_qsize(owner->config.receive_queue));
    STEP("mesh-send-timeout", esp_mesh_send_block_time(owner->config.send_block_ms));
    STEP("mesh-power-save", owner->config.power_save ? esp_mesh_enable_ps() : esp_mesh_disable_ps());
    STEP("mesh-authmode", esp_mesh_set_ap_authmode(owner->config.ap_authmode));
    STEP("mesh-config", esp_mesh_set_config(&owner->config.network));
    STEP("mesh-ap-connections", esp_mesh_set_ap_connections(owner->config.network.mesh_ap.max_connection));
    if (owner->config.ap_authmode != WIFI_AUTH_OPEN)
        STEP("mesh-ap-password", esp_mesh_set_ap_password(owner->config.network.mesh_ap.password,
            strnlen((const char *)owner->config.network.mesh_ap.password, 64)));
    if (owner->config.encrypt_ie) {
        STEP("mesh-ie-key", esp_mesh_set_ie_crypto_key(owner->config.ie_key, owner->config.ie_key_length));
        owner->encrypt_ie = true; owner->ie_key_length = owner->config.ie_key_length;
    } else STEP("mesh-ie-encryption", esp_mesh_set_ie_crypto_funcs(NULL));
    STEP("mesh-fixed-root", esp_mesh_fix_root(owner->config.fixed_root));
    STEP("mesh-type", esp_mesh_set_type(owner->config.type));
    STEP("mesh-self-organized", esp_mesh_set_self_organized(owner->config.self_organized, false));
    portENTER_CRITICAL(&s_mesh_lock); owner->status.start_attempted = true; portEXIT_CRITICAL(&s_mesh_lock);
    STEP("mesh-start", esp_mesh_start());
    portENTER_CRITICAL(&s_mesh_lock); owner->status.started = true; portEXIT_CRITICAL(&s_mesh_lock);
#undef STEP
done:
    /* Every native setter has returned; this copy is no longer referenced.
     * Keep only non-secret status while native cleanup/Radio restore remain. */
    esp32_mquickjs_wireless_secure_zero(&owner->config, sizeof(owner->config));
    portENTER_CRITICAL(&s_mesh_lock);
    if (error) { owner->status.error = error; owner->status.closing = true; }
    else owner->status.stage = NULL;
    owner->status.busy = false;
    portEXIT_CRITICAL(&s_mesh_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_seal(const esp32_mquickjs_wifi_mesh_token_t *token)
{
    portENTER_CRITICAL(&s_mesh_lock);
    bool exact = mesh_exact(token);
    if (exact) s_mesh->status.closing = true;
    portEXIT_CRITICAL(&s_mesh_lock);
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_close(const esp32_mquickjs_wifi_mesh_token_t *token)
{
    portENTER_CRITICAL(&s_mesh_lock);
    if (!mesh_exact(token)) { portEXIT_CRITICAL(&s_mesh_lock); return ESP_ERR_INVALID_STATE; }
    wifi_mesh_owner_t *owner = s_mesh;
    owner->status.closing = true;
    if (owner->status.busy) { portEXIT_CRITICAL(&s_mesh_lock); return ESP_ERR_TIMEOUT; }
    if (owner->status.restart_required) {
        esp_err_t error = owner->status.cleanup_error ? owner->status.cleanup_error : ESP_ERR_INVALID_STATE;
        portEXIT_CRITICAL(&s_mesh_lock); return error;
    }
    if (owner->status.native_retired) { portEXIT_CRITICAL(&s_mesh_lock); return ESP_OK; }
    owner->status.busy = true;
    owner->status.cleanup_stage = "mesh-deinit";
    bool deinit = owner->status.initialized;
    portEXIT_CRITICAL(&s_mesh_lock);
    /* This SDK's deinit delegates to stop while initialized; stop releases the
     * initialized instance as well as workers/queues. Do not call both, or
     * retry partial native teardown after an unqualified failure. */
    esp_err_t error = deinit ? esp_mesh_deinit() : ESP_OK;
    esp32_mquickjs_wireless_secure_zero(&owner->config, sizeof(owner->config));
    portENTER_CRITICAL(&s_mesh_lock);
    owner->status.cleanup_error = error;
    if (error) owner->status.restart_required = true;
    else {
        owner->status.native_retired = true;
        owner->status.initialized = owner->status.started = false;
        owner->status.parent_known = true; owner->status.parent_connected = false;
        owner->status.cleanup_stage = NULL;
        owner->event_count = 0; owner->status.queued_events = 0;
        memset(owner->messages, 0, sizeof(owner->messages));
        esp32_mquickjs_wireless_secure_zero(owner->receive_bytes, 2 * MESH_MTU);
    }
    owner->status.busy = false;
    portEXIT_CRITICAL(&s_mesh_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_release(esp32_mquickjs_wifi_mesh_token_t *token)
{
    portENTER_CRITICAL(&s_mesh_lock);
    wifi_mesh_owner_t *owner = mesh_exact(token) && s_mesh->status.native_retired && !s_mesh->status.busy ? s_mesh : NULL;
    if (owner) { s_mesh = NULL; *token = (esp32_mquickjs_wifi_mesh_token_t){0}; }
    portEXIT_CRITICAL(&s_mesh_lock);
    if (!owner) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wireless_secure_zero(owner->receive_bytes, 2 * MESH_MTU);
    esp32_mquickjs_memory_payload_free(owner->receive_bytes);
    if (owner->scan_record) {
        esp32_mquickjs_wireless_secure_zero(owner->scan_record, sizeof(*owner->scan_record));
        esp32_mquickjs_memory_payload_free(owner->scan_record);
    }
    esp32_mquickjs_wireless_secure_zero(owner, sizeof(*owner));
    esp32_mquickjs_memory_payload_free(owner);
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_status(const esp32_mquickjs_wifi_mesh_token_t *token,
    esp32_mquickjs_wifi_mesh_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_mesh_lock);
    bool exact = mesh_exact(token);
    if (exact) *status = s_mesh->status;
    portEXIT_CRITICAL(&s_mesh_lock);
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_inspect(const esp32_mquickjs_wifi_mesh_token_t *token)
{
    wifi_mesh_owner_t *owner = mesh_enter(token, true);
    if (!owner) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_mesh_lock);
    uint32_t revision = owner->status.events;
    bool connected = owner->status.parent_known && owner->status.parent_connected;
    portEXIT_CRITICAL(&s_mesh_lock);
    mesh_tx_pending_t tx = {0}; mesh_rx_pending_t rx = {0};
    bool root = esp_mesh_is_root();
    /* SDK requires a parent-connected observation before this getter. */
    mesh_type_t type = connected ? esp_mesh_get_type() : MESH_IDLE;
    int layer = connected ? esp_mesh_get_layer() : 0, nodes = esp_mesh_get_total_node_num(), routes = esp_mesh_get_routing_table_size();
    mesh_addr_t parent = {0}; uint8_t router[6] = {0};
    esp_err_t error = connected ? esp_mesh_get_parent_bssid(&parent) : ESP_OK;
    if (!error) error = esp_mesh_get_router_bssid(router);
    if (!error) error = esp_mesh_get_tx_pending(&tx);
    if (!error) error = esp_mesh_get_rx_pending(&rx);
    portENTER_CRITICAL(&s_mesh_lock);
    owner->status.query_error = error;
    owner->status.native_snapshot_valid = !error && revision != UINT32_MAX &&
        revision == owner->status.events && !owner->status.closing;
    if (owner->status.native_snapshot_valid) {
        memcpy(owner->status.router_bssid, router, 6);
        if (connected) memcpy(owner->status.parent, parent.addr, 6);
        owner->status.root = root; owner->status.type = type; owner->status.type_known = connected;
        owner->status.nodes = nodes; owner->status.routes = routes;
        if (layer >= 0 && layer <= UINT16_MAX) owner->status.layer = layer;
        owner->status.tx_pending = tx; owner->status.rx_pending = rx;
    }
    owner->status.busy = false;
    portEXIT_CRITICAL(&s_mesh_lock);
    return error;
}

static bool mesh_nonzero_address(const uint8_t *address)
{
    uint8_t any = 0;
    for (unsigned i = 0; i < 6; ++i) any |= address[i];
    return any != 0;
}

#include "esp32_mquickjs_wifi_mesh_scan.inc"
#include "esp32_mquickjs_wifi_mesh_controls.inc"

esp_err_t esp32_mquickjs_wifi_mesh_sdk_control(const esp32_mquickjs_wifi_mesh_token_t *token,
    esp32_mquickjs_wifi_mesh_control_t *control)
{
    if (!control || (unsigned)control->kind >= ESP32_MQUICKJS_MESH_CONTROL_COUNT)
        return ESP_ERR_INVALID_ARG;
    if (control->kind >= ESP32_MQUICKJS_MESH_CONFIGURATION) {
        esp_err_t checked = mesh_control_validate(control);
        if (checked) return checked;
    }
    bool table = control->kind <= ESP32_MQUICKJS_MESH_GROUP_DELETE;
    uint16_t limit = control->kind == ESP32_MQUICKJS_MESH_ROUTING_TABLE ?
        ESP32_MQUICKJS_MESH_MAX_NODES : ESP32_MQUICKJS_MESH_MAX_GROUPS;
    if (table && (!control->addresses || !control->capacity || control->capacity > limit))
        return ESP_ERR_INVALID_ARG;
    if (control->kind == ESP32_MQUICKJS_MESH_GROUP_ADD || control->kind == ESP32_MQUICKJS_MESH_GROUP_DELETE) {
        if (!control->count || control->count > control->capacity) return ESP_ERR_INVALID_ARG;
        for (unsigned i = 0; i < control->count; ++i) {
            if (!mesh_nonzero_address(control->addresses[i].addr)) return ESP_ERR_INVALID_ARG;
            for (unsigned j = 0; j < i; ++j)
                if (!memcmp(control->addresses[i].addr, control->addresses[j].addr, 6)) return ESP_ERR_INVALID_ARG;
        }
    }
    wifi_mesh_owner_t *owner = mesh_enter(token, true);
    if (!owner) return ESP_ERR_INVALID_STATE;
    esp_err_t error = mesh_scan_admit(owner, control->kind);
    if (error) {
        portENTER_CRITICAL(&s_mesh_lock); owner->status.busy = false; portEXIT_CRITICAL(&s_mesh_lock);
        return error;
    }
    switch (control->kind) {
    case ESP32_MQUICKJS_MESH_ROUTING_TABLE: {
        int count = 0;
        error = esp_mesh_get_routing_table(control->addresses, control->capacity * sizeof(mesh_addr_t), &count);
        if (!error && (count < 0 || count > control->capacity)) error = ESP_ERR_INVALID_RESPONSE;
        control->count = error ? 0 : count;
        break;
    }
    case ESP32_MQUICKJS_MESH_GROUP_LIST: {
        int count = esp_mesh_get_group_num();
        if (count < 0 || count > control->capacity) error = ESP_ERR_INVALID_SIZE;
        else if (count) error = esp_mesh_get_group_list(control->addresses, count);
        control->count = error ? 0 : count;
        break;
    }
    case ESP32_MQUICKJS_MESH_GROUP_ADD:
        error = esp_mesh_set_group_id(control->addresses, control->count); break;
    case ESP32_MQUICKJS_MESH_GROUP_DELETE:
        error = esp_mesh_delete_group_id(control->addresses, control->count); break;
    case ESP32_MQUICKJS_MESH_TODS_STATE:
        error = esp_mesh_is_root() ? esp_mesh_post_toDS_state(control->value) : ESP_ERR_INVALID_STATE; break;
    case ESP32_MQUICKJS_MESH_CONNECT:
        error = esp_mesh_connect(); break;
    case ESP32_MQUICKJS_MESH_DISCONNECT:
        error = esp_mesh_disconnect(); break;
    case ESP32_MQUICKJS_MESH_FLUSH_UPSTREAM:
        error = esp_mesh_flush_upstream_packets(); break;
    default:
        error = mesh_control_execute(owner, control); break;
    }
    portENTER_CRITICAL(&s_mesh_lock);
    owner->status.busy = false;
    if (control->kind > ESP32_MQUICKJS_MESH_GROUP_LIST) owner->status.native_snapshot_valid = false;
    portEXIT_CRITICAL(&s_mesh_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_send(const esp32_mquickjs_wifi_mesh_token_t *token,
    const esp32_mquickjs_wifi_mesh_send_t *send)
{
    if (!send || !send->bytes || !send->length || send->length > MESH_MPS ||
        (unsigned)send->destination > ESP32_MQUICKJS_MESH_BROADCAST ||
        (unsigned)send->protocol > MESH_PROTO_MQTT ||
        (send->destination == ESP32_MQUICKJS_MESH_TO_ROOT && !send->reliable) ||
        (send->drop_on_root_change && send->destination != ESP32_MQUICKJS_MESH_TO_DS)) return ESP_ERR_INVALID_ARG;
    if (send->destination == ESP32_MQUICKJS_MESH_TO_DS) {
        if (!send->address.mip.ip4.addr || !send->address.mip.port) return ESP_ERR_INVALID_ARG;
    } else if (send->destination != ESP32_MQUICKJS_MESH_TO_ROOT &&
               send->destination != ESP32_MQUICKJS_MESH_BROADCAST && !mesh_nonzero_address(send->address.addr))
        return ESP_ERR_INVALID_ARG;
    wifi_mesh_owner_t *owner = mesh_enter(token, true);
    if (!owner) return ESP_ERR_INVALID_STATE;
    mesh_addr_t destination = send->address;
    const mesh_addr_t *to = &destination;
    mesh_data_t data = {.data = (uint8_t *)send->bytes, .size = send->length,
        .proto = send->protocol, .tos = send->reliable ? MESH_TOS_P2P : MESH_TOS_DEF};
    int flags = MESH_DATA_P2P;
    mesh_opt_t option = {0}; int option_count = 0;
    esp_err_t error = ESP_OK;
    switch (send->destination) {
    case ESP32_MQUICKJS_MESH_TO_ROOT: to = NULL; flags = MESH_DATA_TODS; break;
    case ESP32_MQUICKJS_MESH_TO_DS: flags = MESH_DATA_TODS; break;
    case ESP32_MQUICKJS_MESH_FROM_DS:
        if (!esp_mesh_is_root()) error = ESP_ERR_INVALID_STATE;
        flags = MESH_DATA_FROMDS; break;
    case ESP32_MQUICKJS_MESH_TO_GROUP:
        option.type = MESH_OPT_SEND_GROUP; option.len = sizeof(destination); option.val = destination.addr;
        option_count = 1; break;
    case ESP32_MQUICKJS_MESH_BROADCAST: memset(destination.addr, 0xff, 6); break;
    default: break;
    }
    if (!send->reliable) flags |= MESH_DATA_NONBLOCK;
    if (send->drop_on_root_change) flags |= MESH_DATA_DROP;
    if (!error) error = esp_mesh_send(to, &data, flags, option_count ? &option : NULL, option_count);
    /* SDK copies into its own queue before returning. JS input must nevertheless
     * remain rooted/copied until this call returns, even if its Future ended. */
    portENTER_CRITICAL(&s_mesh_lock);
    if (!error) mesh_increment(&owner->status.sends);
    owner->status.busy = false;
    portEXIT_CRITICAL(&s_mesh_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_receive(const esp32_mquickjs_wifi_mesh_token_t *token, bool to_ds)
{
    wifi_mesh_owner_t *owner = mesh_enter(token, true);
    if (!owner) return ESP_ERR_INVALID_STATE;
    unsigned slot = to_ds ? 1 : 0;
    esp_err_t error = ESP_OK;
    portENTER_CRITICAL(&s_mesh_lock);
    bool pending = owner->messages[slot].sequence != 0;
    bool exhausted = !owner->next_sequence;
    uint32_t sequence = !pending && !exhausted ? owner->next_sequence++ : 0;
    portEXIT_CRITICAL(&s_mesh_lock);
    if (pending) goto done;
    if (exhausted) { error = ESP_ERR_NO_MEM; goto done; }
    if (to_ds && !esp_mesh_is_root()) { error = ESP_ERR_INVALID_STATE; goto done; }
    esp32_mquickjs_wifi_mesh_message_t message = {.to_ds = to_ds};
    mesh_data_t data = {.data = owner->receive_bytes + slot * MESH_MTU, .size = MESH_MTU};
    mesh_opt_t option = {.type = MESH_OPT_RECV_DS_ADDR, .len = sizeof(message.to), .val = message.to.addr};
    error = to_ds ? esp_mesh_recv_toDS(&message.from, &message.to, &data, 0, &message.flags, NULL, 0) :
        esp_mesh_recv(&message.from, &data, 0, &message.flags, &option, 1);
    if (error) goto done;
    if (data.size > MESH_MTU || data.data != owner->receive_bytes + slot * MESH_MTU) {
        error = ESP_ERR_INVALID_RESPONSE; goto done;
    }
    message.length = data.size; message.protocol = data.proto; message.service = data.tos;
    message.has_ds_address = to_ds || (option.type == MESH_OPT_RECV_DS_ADDR && option.len == sizeof(message.to) &&
        (message.flags & MESH_DATA_FROMDS));
    portENTER_CRITICAL(&s_mesh_lock);
    /* Reserve before the destructive SDK read: an event storm cannot consume
     * the last identity after the packet has already left its native queue. */
    if (!owner->status.closing) {
        message.sequence = sequence;
        owner->messages[slot] = message;
        mesh_increment(&owner->status.receives);
    } else error = ESP_ERR_INVALID_STATE;
    portEXIT_CRITICAL(&s_mesh_lock);
done:
    if (error) esp32_mquickjs_wireless_secure_zero(owner->receive_bytes + slot * MESH_MTU, MESH_MTU);
    portENTER_CRITICAL(&s_mesh_lock); owner->status.busy = false; portEXIT_CRITICAL(&s_mesh_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_message_copy(const esp32_mquickjs_wifi_mesh_token_t *token,
    bool to_ds, esp32_mquickjs_wifi_mesh_message_t *message, uint8_t *bytes, size_t capacity)
{
    if (!message || !bytes) return ESP_ERR_INVALID_ARG;
    unsigned slot = to_ds ? 1 : 0;
    portENTER_CRITICAL(&s_mesh_lock);
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (mesh_exact(token) && !s_mesh->status.closing) {
        const esp32_mquickjs_wifi_mesh_message_t *stored = &s_mesh->messages[slot];
        error = !stored->sequence ? ESP_ERR_NOT_FOUND : capacity < stored->length ? ESP_ERR_INVALID_SIZE : ESP_OK;
        if (!error) { *message = *stored; memcpy(bytes, s_mesh->receive_bytes + slot * MESH_MTU, stored->length); }
    }
    portEXIT_CRITICAL(&s_mesh_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_message_commit(const esp32_mquickjs_wifi_mesh_token_t *token,
    bool to_ds, uint32_t sequence)
{
    unsigned slot = to_ds ? 1 : 0;
    portENTER_CRITICAL(&s_mesh_lock);
    bool exact = sequence && mesh_exact(token) && !s_mesh->status.closing && s_mesh->messages[slot].sequence == sequence;
    if (exact) {
        esp32_mquickjs_wireless_secure_zero(s_mesh->receive_bytes + slot * MESH_MTU, s_mesh->messages[slot].length);
        memset(&s_mesh->messages[slot], 0, sizeof(s_mesh->messages[slot]));
    }
    portEXIT_CRITICAL(&s_mesh_lock);
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}

#include "esp32_mquickjs_wifi_mesh_events.inc"
#endif
