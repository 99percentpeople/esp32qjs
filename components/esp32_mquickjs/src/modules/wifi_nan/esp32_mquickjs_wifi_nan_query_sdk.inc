/* Included in the hash-reviewed nan_app.c, after nan_i.h. */
#include "esp32_mquickjs_wifi_nan_query.h"
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
static void esp32qjs_nan_query_peer(const struct own_svc_info *own,
    const struct peer_svc_info *peer, struct nan_peer_record *out)
{
    *out = (struct nan_peer_record){
        .own_svc_id = own->svc_id, .peer_svc_id = peer->svc_id, .peer_svc_type = peer->type,
    };
    memcpy(out->peer_nmi, peer->peer_nmi, sizeof(out->peer_nmi));
    /* An NMI can occur in several services. The public SDK single-peer getter
     * uses only NMI; its list getter leaves NDP fields untouched on a service
     * mismatch. Query both through this zeroed, service-qualified copy. */
    for (unsigned i = 0; i < ESP_WIFI_NAN_DATAPATH_MAX_PEERS; ++i) {
        const struct ndl_info *ndl = &s_nan_ctx.ndl[i];
        if (!ndl->ndp_id || memcmp(ndl->peer_nmi, peer->peer_nmi, 6)) continue;
        bool matches = (own->type == ESP_NAN_PUBLISH && ndl->own_role == ESP_NAN_PUBLISH &&
                ndl->publisher_id == own->svc_id) ||
            (own->type == ESP_NAN_SUBSCRIBE && ndl->own_role == ESP_NAN_SUBSCRIBE &&
                ndl->publisher_id == peer->svc_id &&
                esp32_mquickjs_wifi_nan_ndp_peer_service(peer->peer_nmi, peer->svc_id, own->svc_id));
        if (!matches) continue;
        out->ndp_id = ndl->ndp_id;
        memcpy(out->peer_ndi, ndl->peer_ndi, sizeof(out->peer_ndi));
        break;
    }
}

esp_err_t esp32_mquickjs_wifi_nan_sdk_query(const esp32_mquickjs_wifi_nan_query_t *q,
    esp32_mquickjs_wifi_nan_query_result_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!q || q->kind < ESP32_MQUICKJS_NAN_QUERY_SERVICE || q->kind > ESP32_MQUICKJS_NAN_QUERY_PEERS ||
        (q->service_id && q->service_name) ||
        (!q->service_id && !q->service_name && q->kind != ESP32_MQUICKJS_NAN_QUERY_PEER) ||
        (q->service_name && (!q->service_name[0] || strnlen(q->service_name, ESP_WIFI_MAX_SVC_NAME_LEN) == ESP_WIFI_MAX_SVC_NAME_LEN)) ||
        (q->kind == ESP32_MQUICKJS_NAN_QUERY_PEER &&
            ((q->peer[0] & 1) || !memcmp(q->peer, (uint8_t[6]){0}, 6)))) return ESP_ERR_INVALID_ARG;
    if (!s_nan_data_lock) return ESP_ERR_INVALID_STATE;
    esp_err_t error = ESP_ERR_NOT_FOUND;
    NAN_DATA_LOCK();
    for (unsigned i = 0; i < ESP_WIFI_NAN_MAX_SVC_SUPPORTED; ++i) {
        const struct own_svc_info *own = &s_nan_ctx.own_svc[i];
        if (!own->svc_id || (q->service_id && q->service_id != own->svc_id)) continue;
        size_t length = strnlen(own->svc_name, sizeof(own->svc_name));
        if (!length || length == sizeof(own->svc_name) || own->num_peer_records > NAN_MAX_PEERS_RECORD) {
            error = ESP_ERR_INVALID_STATE; break;
        }
        if (q->service_name && strcmp(q->service_name, own->svc_name)) continue;
        const struct peer_svc_info *peer = SLIST_FIRST(&own->peer_list);
        unsigned count = 0;
        bool found = q->kind != ESP32_MQUICKJS_NAN_QUERY_PEER;
        for (; peer && count < NAN_MAX_PEERS_RECORD; peer = SLIST_NEXT(peer, next), ++count) {
            if (!peer->svc_id || peer->own_svc_id != own->svc_id ||
                (peer->type != ESP_NAN_PUBLISH && peer->type != ESP_NAN_SUBSCRIBE)) break;
            if (q->kind == ESP32_MQUICKJS_NAN_QUERY_PEERS) esp32qjs_nan_query_peer(own, peer, &out->peers[count]);
            if (q->kind == ESP32_MQUICKJS_NAN_QUERY_PEER && !found && !memcmp(q->peer, peer->peer_nmi, 6)) {
                esp32qjs_nan_query_peer(own, peer, &out->peers[0]);
                found = true;
            }
        }
        if (peer || count != own->num_peer_records) { error = ESP_ERR_INVALID_STATE; break; }
        if (!found) continue;
        out->service_id = own->svc_id;
        out->peer_count = count;
        memcpy(out->service_name, own->svc_name, length + 1);
        error = ESP_OK;
        break;
    }
    if (error != ESP_OK) memset(out, 0, sizeof(*out));
    NAN_DATA_UNLOCK();
    return error;
}
#endif
