/* Included by the production SDK owner. mesh_enter pins the owner and excludes
 * every other framework SDK call; SDK work never runs under s_mesh_lock. */
static esp_err_t mesh_control_validate(const esp32_mquickjs_wifi_mesh_control_t *c)
{
    const esp32_mquickjs_wifi_mesh_control_data_t *d = c->detail;
    if (!d) return ESP_ERR_INVALID_ARG;
    switch (c->kind) {
    case ESP32_MQUICKJS_MESH_SCAN: return mesh_scan_validate(&d->data.scan);
    case ESP32_MQUICKJS_MESH_SCAN_NEXT:
        if (!d->scan_record) return ESP_ERR_INVALID_ARG;
        /* fall through */
    case ESP32_MQUICKJS_MESH_SCAN_FLUSH:
        if (!d->scan_identity) return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_SET_PARENT: {
        const wifi_sta_config_t *p = &d->data.parent.sta;
        size_t length;
        if (!p->ssid[0] || !p->channel || p->channel > 14 ||
            !mesh_secret_length(p->password, &length) || (length && length < 8) ||
            (p->bssid_set && (!mesh_nonzero_address(p->bssid) || (p->bssid[0] & 1U))) ||
            (d->has_address && !mesh_nonzero_address(d->address.addr)) ||
            (d->number != MESH_ROOT && d->number != MESH_NODE && d->number != MESH_LEAF) ||
            d->duration < 1 || d->duration > ESP32_MQUICKJS_MESH_MAX_NODES ||
            (d->number == MESH_ROOT && d->duration != MESH_ROOT_LAYER) ||
            (d->number == MESH_NODE && d->duration <= MESH_ROOT_LAYER)) return ESP_ERR_INVALID_ARG;
        break;
    }
    case ESP32_MQUICKJS_MESH_SET_ROUTER: {
        size_t length;
        if (!d->data.router.ssid_len || d->data.router.ssid_len > 32 ||
            !mesh_secret_length(d->data.router.password, &length) || (length && length < 8))
            return ESP_ERR_INVALID_ARG;
        break;
    }
    case ESP32_MQUICKJS_MESH_SET_ID:
    case ESP32_MQUICKJS_MESH_HAS_GROUP:
    case ESP32_MQUICKJS_MESH_UPSTREAM_CAPACITY:
        if (!mesh_nonzero_address(d->address.addr)) return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_SUBNET:
        if (!mesh_nonzero_address(d->address.addr) || !c->addresses || !c->capacity ||
            c->capacity > ESP32_MQUICKJS_MESH_MAX_NODES) return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_SET_TYPE:
        if ((unsigned)d->number > MESH_STA) return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_SELF_ORGANIZED:
        if (!d->enabled && d->select_parent) return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_ASSOC_EXPIRY:
        if (d->number < 10 || d->number > INT_MAX / 1000) return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_ROOT_HEALING:
        if (d->number < 0) return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_IE_ENCRYPTION:
        if (d->enabled) {
            if (d->data.encryption.length < 8 || d->data.encryption.length > 64) return ESP_ERR_INVALID_ARG;
            for (unsigned i = 0; i < d->data.encryption.length; ++i)
                if ((unsigned char)d->data.encryption.key[i] < 32 ||
                    (unsigned char)d->data.encryption.key[i] > 126) return ESP_ERR_INVALID_ARG;
        } else if (d->data.encryption.length) return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_WAIVE_ROOT:
        if (d->data.vote.is_rc_specified || d->data.vote.config.attempts < 15 ||
            !isfinite(d->data.vote.percentage) || d->data.vote.percentage <= 0 || d->data.vote.percentage > 1)
            return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_SWITCH_CHANNEL:
        if (d->number < 1 || d->number > 14 || d->duration < 1 || d->duration > UINT8_MAX ||
            (d->has_address && !mesh_nonzero_address(d->address.addr))) return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_DEVICE_DUTY:
        if (d->number < 1 || d->number > 100 ||
            (d->duty_type != MESH_PS_DEVICE_DUTY_REQUEST && d->duty_type != MESH_PS_DEVICE_DUTY_DEMAND))
            return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_NETWORK_DUTY:
        /* The pinned public SDK contract supports ENTIRE only. UPLINK and a
         * nominated root candidate are not implemented public capabilities. */
        if (d->number < 1 || d->number > 100 || d->duration < -1 || d->duration == 0 ||
            d->rule != MESH_PS_NETWORK_DUTY_APPLIED_ENTIRE) return ESP_ERR_INVALID_ARG;
        break;
    case ESP32_MQUICKJS_MESH_SIGNAL_DUTY:
        if (d->number < 0 || d->number > UINT8_MAX - 1) return ESP_ERR_INVALID_ARG;
        break;
    default: break;
    }
    return ESP_OK;
}

static esp_err_t mesh_control_execute(wifi_mesh_owner_t *owner, esp32_mquickjs_wifi_mesh_control_t *c)
{
    esp32_mquickjs_wifi_mesh_control_data_t *d = c->detail;
    esp_err_t error = ESP_OK;
    d->completed_steps = 0;
#define CALL(label, expression) do { d->stage = label; error = (expression); if (error) goto done; ++d->completed_steps; } while (0)
    switch (c->kind) {
    case ESP32_MQUICKJS_MESH_CONFIGURATION: {
        esp32_mquickjs_wifi_mesh_config_t *config = &d->data.configuration.config;
        CALL("mesh-get-config", esp_mesh_get_config(&config->network));
        /* The native vtable is never a public value or an owned pointer. */
        config->network.crypto_funcs = NULL;
        CALL("mesh-get-router", esp_mesh_get_router(&config->network.router));
        CALL("mesh-get-id", esp_mesh_get_id(&config->network.mesh_id));
        config->topology = esp_mesh_get_topology();
        config->ap_authmode = esp_mesh_get_ap_authmode();
        int layer = esp_mesh_get_max_layer(), capacity = esp_mesh_get_capacity_num(), queue = esp_mesh_get_xon_qsize();
        int connections = esp_mesh_get_ap_connections(), nonmesh = esp_mesh_get_non_mesh_connections();
        if (layer < 1 || layer > 1000 || capacity < 1 || capacity > ESP32_MQUICKJS_MESH_MAX_NODES ||
            queue < 16 || queue > UINT16_MAX || connections < 0 || connections > 10 || nonmesh < 0 || nonmesh > 10 ||
            config->network.router.ssid_len > 32) { error = ESP_ERR_INVALID_RESPONSE; goto done; }
        config->max_layer = layer; config->capacity = capacity; config->receive_queue = queue;
        config->network.mesh_ap.max_connection = connections; config->network.mesh_ap.nonmesh_max_connection = nonmesh;
        config->fixed_root = esp_mesh_is_root_fixed(); config->self_organized = esp_mesh_get_self_organized();
        config->power_save = esp_mesh_is_ps_enabled(); config->vote_percentage = esp_mesh_get_vote_percentage();
        config->send_block_ms = owner->send_block_ms;
        config->encrypt_ie = owner->encrypt_ie; config->ie_key_length = owner->ie_key_length;
        d->data.configuration.root_conflicts = esp_mesh_is_root_conflicts_allowed();
        d->data.configuration.assoc_expiry = esp_mesh_get_ap_assoc_expire();
        d->data.configuration.root_healing = esp_mesh_get_root_healing_delay();
        if (d->include_secrets && owner->ie_key_length)
            CALL("mesh-get-ie-key", esp_mesh_get_ie_crypto_key(config->ie_key, owner->ie_key_length));
        break;
    }
    case ESP32_MQUICKJS_MESH_SET_PARENT: {
        d->stage = "mesh-parent-manual-required";
        if (esp_mesh_get_self_organized()) { error = ESP_ERR_INVALID_STATE; break; }
        /* Validate before the pinned setter changes type/mode or disconnects.
         * A NODE cannot occupy the final layer; a LEAF may occupy that layer. */
        d->stage = "mesh-parent-layer";
        int max_layer = esp_mesh_get_max_layer();
        if (d->duration > max_layer || (d->number == MESH_NODE && d->duration >= max_layer)) {
            error = ESP_ERR_INVALID_ARG; break;
        }
        if (d->number != MESH_ROOT) {
            bool open = esp_mesh_get_ap_authmode() == WIFI_AUTH_OPEN;
            if (open != (d->data.parent.sta.password[0] == 0)) { error = ESP_ERR_INVALID_ARG; break; }
        }
        CALL("mesh-set-parent", esp_mesh_set_parent(&d->data.parent, d->has_address ? &d->address : NULL,
            (mesh_type_t)d->number, d->duration)); break;
    }
    case ESP32_MQUICKJS_MESH_SCAN: error = mesh_scan_start(owner, d); break;
    case ESP32_MQUICKJS_MESH_SCAN_NEXT: error = mesh_scan_next(owner, d); break;
    case ESP32_MQUICKJS_MESH_SCAN_FLUSH: error = mesh_scan_flush(owner, d); break;
    case ESP32_MQUICKJS_MESH_SET_ROUTER:
        CALL("mesh-set-router", esp_mesh_set_router(&d->data.router)); break;
    case ESP32_MQUICKJS_MESH_SET_ID:
        CALL("mesh-set-id", esp_mesh_set_id(&d->address)); break;
    case ESP32_MQUICKJS_MESH_SET_TYPE:
        CALL("mesh-set-type", esp_mesh_set_type((mesh_type_t)d->number)); break;
    case ESP32_MQUICKJS_MESH_SELF_ORGANIZED:
        CALL("mesh-self-organized", esp_mesh_set_self_organized(d->enabled, d->select_parent)); break;
    case ESP32_MQUICKJS_MESH_FIXED_ROOT:
        CALL("mesh-fixed-root", esp_mesh_fix_root(d->enabled)); break;
    case ESP32_MQUICKJS_MESH_ROOT_CONFLICTS:
        CALL("mesh-root-conflicts", esp_mesh_allow_root_conflicts(d->enabled)); break;
    case ESP32_MQUICKJS_MESH_ASSOC_EXPIRY:
        CALL("mesh-association-expiry", esp_mesh_set_ap_assoc_expire(d->number));
        /* Pinned SDK returns ESP_OK even when its ioctl allocation fails and
         * discards the ioctl result. Do not infer an applied value from that
         * return. A mismatch is unconfirmed; never auto-replay this command. */
        d->stage = "mesh-association-expiry-readback";
        if (esp_mesh_get_ap_assoc_expire() != d->number) error = ESP_ERR_INVALID_RESPONSE;
        break;
    case ESP32_MQUICKJS_MESH_ROOT_HEALING:
        CALL("mesh-root-healing", esp_mesh_set_root_healing_delay(d->number)); break;
    case ESP32_MQUICKJS_MESH_IE_ENCRYPTION:
        if (d->enabled) {
            CALL("mesh-ie-key", esp_mesh_set_ie_crypto_key(d->data.encryption.key, d->data.encryption.length));
            owner->ie_key_length = d->data.encryption.length;
            CALL("mesh-ie-encryption", esp_mesh_set_ie_crypto_funcs(&g_wifi_default_mesh_crypto_funcs));
        } else CALL("mesh-ie-encryption", esp_mesh_set_ie_crypto_funcs(NULL));
        owner->encrypt_ie = d->enabled;
        break;
    case ESP32_MQUICKJS_MESH_WAIVE_ROOT:
        d->stage = "mesh-root-required";
        if (!esp_mesh_is_root()) { error = ESP_ERR_INVALID_STATE; goto done; }
        CALL("mesh-waive-root", esp_mesh_waive_root(&d->data.vote, MESH_VOTE_REASON_ROOT_INITIATED)); break;
    case ESP32_MQUICKJS_MESH_SWITCH_CHANNEL:
        d->stage = "mesh-root-required";
        if (!esp_mesh_is_root()) { error = ESP_ERR_INVALID_STATE; goto done; }
        CALL("mesh-switch-channel", esp_mesh_switch_channel(d->has_address ? d->address.addr : NULL, d->number, d->duration)); break;
    case ESP32_MQUICKJS_MESH_DEVICE_DUTY:
        CALL("mesh-device-duty", esp_mesh_set_active_duty_cycle(d->number, d->duty_type)); break;
    case ESP32_MQUICKJS_MESH_NETWORK_DUTY:
        d->stage = "mesh-root-required";
        if (d->duration == -1 && !esp_mesh_is_root()) { error = ESP_ERR_INVALID_STATE; goto done; }
        CALL("mesh-network-duty", esp_mesh_set_network_duty_cycle(d->number, d->duration, d->rule)); break;
    case ESP32_MQUICKJS_MESH_SIGNAL_DUTY:
        CALL("mesh-duty-signaling", esp_mesh_ps_duty_signaling(d->number)); break;
    case ESP32_MQUICKJS_MESH_SUBNET: {
        int count = 0;
        CALL("mesh-subnet-count", esp_mesh_get_subnet_nodes_num(&d->address, &count));
        if (count < 0 || count > c->capacity) { error = ESP_ERR_INVALID_SIZE; goto done; }
        if (count) CALL("mesh-subnet-list", esp_mesh_get_subnet_nodes_list(&d->address, c->addresses, count));
        c->count = count; break;
    }
    case ESP32_MQUICKJS_MESH_HAS_GROUP:
        d->result = esp_mesh_is_my_group(&d->address); break;
    case ESP32_MQUICKJS_MESH_UPSTREAM_CAPACITY:
        d->number = esp_mesh_available_txupQ_num(&d->address, &d->sequence); break;
    case ESP32_MQUICKJS_MESH_POWER_STATUS:
        d->data.power.enabled = esp_mesh_is_ps_enabled(); d->data.power.active = esp_mesh_is_device_active();
        CALL("mesh-get-device-duty", esp_mesh_get_active_duty_cycle(&d->data.power.device, &d->data.power.device_type));
        CALL("mesh-get-network-duty", esp_mesh_get_network_duty_cycle(&d->data.power.network, &d->data.power.duration,
            &d->data.power.type, &d->data.power.rule));
        d->data.power.running = esp_mesh_get_running_active_duty_cycle(); break;
    case ESP32_MQUICKJS_MESH_TSF_TIME:
        d->time_us = esp_mesh_get_tsf_time();
        if (d->time_us > INT64_C(9007199254740991) || d->time_us < -INT64_C(9007199254740991)) error = ESP_ERR_INVALID_SIZE;
        break;
    default: error = ESP_ERR_INVALID_ARG; break;
    }
done:
    if (!error) d->stage = NULL;
    if (c->kind == ESP32_MQUICKJS_MESH_CONFIGURATION) {
        d->data.configuration.config.network.crypto_funcs = NULL;
        if (error || !d->include_secrets) {
            esp32_mquickjs_wireless_secure_zero(d->data.configuration.config.network.router.password, 64);
            esp32_mquickjs_wireless_secure_zero(d->data.configuration.config.network.mesh_ap.password, 64);
            esp32_mquickjs_wireless_secure_zero(d->data.configuration.config.ie_key, 64);
        }
    } else if (c->kind == ESP32_MQUICKJS_MESH_SET_ROUTER || c->kind == ESP32_MQUICKJS_MESH_IE_ENCRYPTION ||
               c->kind == ESP32_MQUICKJS_MESH_SET_PARENT) {
        /* SDK setters have returned. Do not retain an input secret until GC. */
        esp32_mquickjs_wireless_secure_zero(&d->data, sizeof(d->data));
    }
    return error;
#undef CALL
}
