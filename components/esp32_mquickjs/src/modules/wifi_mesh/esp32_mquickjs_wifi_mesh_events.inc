/* Included once by the native owner. SDK event capture is synchronous before
 * its default-loop post; no observer storage points into a JS runtime. */
static bool mesh_notice_capture(int32_t id, const void *data, size_t length,
    esp32_mquickjs_wifi_mesh_notice_t *notice)
{
    *notice = (esp32_mquickjs_wifi_mesh_notice_t){.id = id};
#define READ(type) if (!data || length < sizeof(type)) return false; const type *event = data
#define MAC(source) do { memcpy(notice->mac, source, 6); notice->fields |= ESP32_MQUICKJS_MESH_EVENT_MAC; } while (0)
#define CHANNEL(number) do { notice->channel = number; notice->fields |= ESP32_MQUICKJS_MESH_EVENT_CHANNEL; } while (0)
#define LAYER(number) do { notice->layer = number; notice->fields |= ESP32_MQUICKJS_MESH_EVENT_LAYER; } while (0)
#define REASON(number) do { notice->reason = number; notice->fields |= ESP32_MQUICKJS_MESH_EVENT_REASON; } while (0)
#define VALUE(boolean) do { notice->value = boolean; notice->fields |= ESP32_MQUICKJS_MESH_EVENT_BOOLEAN; } while (0)
    switch (id) {
    case MESH_EVENT_PARENT_CONNECTED: {
        READ(mesh_event_connected_t); MAC(event->connected.bssid);
        CHANNEL(event->connected.channel); LAYER(event->self_layer); break;
    }
    case MESH_EVENT_PARENT_DISCONNECTED: {
        READ(mesh_event_disconnected_t); MAC(event->bssid); REASON(event->reason); break;
    }
    case MESH_EVENT_CHILD_CONNECTED: {
        READ(mesh_event_child_connected_t); MAC(event->mac); break;
    }
    case MESH_EVENT_CHILD_DISCONNECTED: {
        READ(mesh_event_child_disconnected_t); MAC(event->mac); break;
    }
    case MESH_EVENT_CHANNEL_SWITCH: {
        READ(mesh_event_channel_switch_t); CHANNEL(event->channel); break;
    }
    case MESH_EVENT_LAYER_CHANGE: {
        READ(mesh_event_layer_change_t); LAYER(event->new_layer); break;
    }
    case MESH_EVENT_TODS_STATE: {
        READ(mesh_event_toDS_state_t); VALUE(*event == MESH_TODS_REACHABLE); break;
    }
    case MESH_EVENT_ROOT_ADDRESS: {
        READ(mesh_event_root_address_t); MAC(event->addr); break;
    }
    case MESH_EVENT_ROOT_SWITCH_REQ: {
        READ(mesh_event_root_switch_req_t); MAC(event->rc_addr.addr); REASON(event->reason); break;
    }
    case MESH_EVENT_VOTE_STARTED: {
        READ(mesh_event_vote_started_t); MAC(event->rc_addr.addr); REASON(event->reason); break;
    }
    case MESH_EVENT_ROUTING_TABLE_ADD:
    case MESH_EVENT_ROUTING_TABLE_REMOVE: {
        READ(mesh_event_routing_table_change_t);
        notice->table_size = event->rt_size_new; notice->table_change = event->rt_size_change;
        notice->fields |= ESP32_MQUICKJS_MESH_EVENT_TABLE; break;
    }
    case MESH_EVENT_ROOT_FIXED: {
        READ(mesh_event_root_fixed_t); VALUE(event->is_fixed); break;
    }
    case MESH_EVENT_NETWORK_STATE: {
        READ(mesh_event_network_state_t); VALUE(event->is_rootless); break;
    }
    case MESH_EVENT_FIND_NETWORK: {
        READ(mesh_event_find_network_t); MAC(event->router_bssid); CHANNEL(event->channel); break;
    }
    case MESH_EVENT_ROUTER_SWITCH: {
        READ(mesh_event_router_switch_t); MAC(event->bssid); CHANNEL(event->channel); break;
    }
    case MESH_EVENT_PS_PARENT_DUTY:
    case MESH_EVENT_PS_CHILD_DUTY:
    case MESH_EVENT_PS_DEVICE_DUTY: {
        READ(mesh_event_ps_duty_t); notice->duty = event->duty;
        notice->fields |= ESP32_MQUICKJS_MESH_EVENT_DUTY;
        if (id == MESH_EVENT_PS_CHILD_DUTY) MAC(event->child_connected.mac);
        break;
    }
    default: break;
    }
#undef READ
#undef MAC
#undef CHANNEL
#undef LAYER
#undef REASON
#undef VALUE
    return true;
}

void esp32_mquickjs_wifi_mesh_sdk_capture(int32_t id, const void *data, size_t length)
{
    if (id < 0 || id >= MESH_EVENT_MAX) return;
    esp32_mquickjs_wifi_mesh_notice_t notice;
    bool valid = mesh_notice_capture(id, data, length, &notice);
    portENTER_CRITICAL(&s_mesh_lock);
    wifi_mesh_owner_t *owner = s_mesh;
    if (!owner || !owner->status.init_attempted || owner->status.native_retired) goto done;
    mesh_increment(&owner->status.events);
    owner->status.native_snapshot_valid = false;
    owner->status.type_known = false;
    if (!valid) {
        mesh_increment(&owner->status.malformed_events);
        if (id == MESH_EVENT_PARENT_CONNECTED || id == MESH_EVENT_PARENT_DISCONNECTED)
            owner->status.parent_known = false;
        goto done;
    }
    /* Native control first. A full or closed observation ring cannot suppress
     * disconnection, STOP, layer, voting or reachability state transitions. */
    if (id == MESH_EVENT_STARTED) owner->status.native_start_seen = true;
    if (id == MESH_EVENT_STOPPED) {
        owner->status.native_stop_seen = true;
        owner->status.started = false;
        owner->status.parent_known = true;
        owner->status.parent_connected = false;
        owner->status.native_snapshot_valid = false;
    }
    if (id == MESH_EVENT_PARENT_CONNECTED || id == MESH_EVENT_PARENT_DISCONNECTED) {
        owner->status.parent_known = true;
        owner->status.parent_connected = id == MESH_EVENT_PARENT_CONNECTED;
        memcpy(owner->status.parent, notice.mac, 6);
        owner->status.native_snapshot_valid = false;
    }
    if (notice.fields & ESP32_MQUICKJS_MESH_EVENT_LAYER) owner->status.layer = notice.layer;
    if (notice.fields & ESP32_MQUICKJS_MESH_EVENT_CHANNEL) owner->status.channel = notice.channel;
    if (id == MESH_EVENT_VOTE_STARTED) owner->status.voting = true;
    if (id == MESH_EVENT_VOTE_STOPPED) owner->status.voting = false;
    if (id == MESH_EVENT_TODS_STATE) owner->status.to_ds_reachable = notice.value;
    if (owner->status.closing) goto done;
    if (owner->event_count == ESP32_MQUICKJS_MESH_EVENT_SLOTS || !owner->next_sequence) {
        mesh_increment(&owner->status.dropped_events); goto done;
    }
    notice.sequence = owner->next_sequence++;
    owner->events[(owner->event_head + owner->event_count++) % ESP32_MQUICKJS_MESH_EVENT_SLOTS] = notice;
    owner->status.queued_events = owner->event_count;
done:
    portEXIT_CRITICAL(&s_mesh_lock);
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_event_copy(const esp32_mquickjs_wifi_mesh_token_t *token,
    esp32_mquickjs_wifi_mesh_notice_t *notice)
{
    if (!notice) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_mesh_lock);
    esp_err_t error = !mesh_exact(token) || s_mesh->status.closing ? ESP_ERR_INVALID_STATE :
        !s_mesh->event_count ? ESP_ERR_NOT_FOUND : ESP_OK;
    if (!error) *notice = s_mesh->events[s_mesh->event_head];
    portEXIT_CRITICAL(&s_mesh_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_mesh_sdk_event_commit(const esp32_mquickjs_wifi_mesh_token_t *token,
    uint32_t sequence)
{
    portENTER_CRITICAL(&s_mesh_lock);
    bool exact = sequence && mesh_exact(token) && !s_mesh->status.closing && s_mesh->event_count &&
        s_mesh->events[s_mesh->event_head].sequence == sequence;
    if (exact) {
        s_mesh->event_head = (s_mesh->event_head + 1) % ESP32_MQUICKJS_MESH_EVENT_SLOTS;
        --s_mesh->event_count; s_mesh->status.queued_events = s_mesh->event_count;
    }
    portEXIT_CRITICAL(&s_mesh_lock);
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}

extern esp_err_t __real_esp_mesh_send_event_internal(int32_t id, void *data, size_t length);
esp_err_t __wrap_esp_mesh_send_event_internal(int32_t id, void *data, size_t length)
{
    esp32_mquickjs_wifi_mesh_sdk_capture(id, data, length);
    return __real_esp_mesh_send_event_internal(id, data, length);
}
