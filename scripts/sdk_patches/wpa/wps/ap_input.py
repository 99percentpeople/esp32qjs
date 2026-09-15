"""AP driver input pinning and WPS association IE ownership.

Composed after the existing Enterprise WPA-main patch; no second competing
translation unit and no change to the shared SDK source tree.
"""
from sdk_patches.common.source import function, replace

ACQUIRE = '''#ifdef CONFIG_WPS_REGISTRAR
extern bool current_task_is_wifi_task(void);
/* Caller receives the existing SDK peer semaphore, acquired while table
 * membership pins storage. Never dereference an unvalidated driver cookie. */
static struct sta_info *esp32qjs_wps_ap_peer_acquire(struct hostapd_data *hapd,
    const void *candidate, const u8 *addr, bool *busy)
{
    struct sta_info *found = NULL;
    *busy = false;
    if (!current_task_is_wifi_task() || hapd != hostapd_get_hapd_data() ||
        !hapd || (!candidate && !addr)) return NULL;
    HOSTAPD_STA_LIST_LOCK(hapd);
    for (struct sta_info *sta = hapd->sta_list; sta; sta = sta->next) {
        if (candidate ? (const void *)sta != candidate : os_memcmp(sta->addr, addr, ETH_ALEN)) continue;
#ifdef CONFIG_SAE
        if (sta->lock && !os_semphr_take(sta->lock, 0)) *busy = true;
#endif
        if (!*busy) found = sta;
        break;
    }
    HOSTAPD_STA_LIST_UNLOCK(hapd);
    return found;
}
#endif
'''

RX = '''bool wpa_ap_rx_eapol(void *hapd_data, void *sm_data, u8 *data, size_t data_len)
{
    struct hostapd_data *hapd = hapd_data;
    if (!sm_data || !data || data_len < sizeof(struct ieee802_1x_hdr)) return false;
    bool busy;
    struct sta_info *sta = esp32qjs_wps_ap_peer_acquire(hapd, sm_data, NULL, &busy);
    if (!sta) return false;
    bool handled = true;
    uint32_t identity = 0;
    /* WPA key frames keep their normal authenticator path, including while
     * registrar close is retrying. Station enrollee never owns AP EAP input. */
    const struct ieee802_1x_hdr *hdr = (const struct ieee802_1x_hdr *)data;
    if (hdr->type != IEEE802_1X_TYPE_EAPOL_KEY && wps_get_owner() == WPS_OWNER_REGISTRAR) {
        identity = esp32qjs_wps_ap_result_identity(hapd);
        if (!esp32qjs_wps_ap_result_activity_enter(identity)) {
            identity = 0;
            handled = false;
        } else esp32qjs_wps_ap_receive_pinned(hapd, sta, data, data_len);
    } else wpa_receive(hapd->wpa_auth, sta->wpa_sm, data, data_len);
    esp32qjs_wps_ap_peer_release(hapd, sta);
    if (identity) esp32qjs_wps_ap_result_callback_leave(identity);
    return handled;
}
'''

ASSOC = '''static void esp32qjs_wps_ap_assoc_clear(struct hostapd_data *hapd, struct sta_info *sta)
{
    ieee802_1x_free_station(hapd, sta);
    wpabuf_free(sta->wps_ie);
    sta->wps_ie = NULL;
}

static int check_n_add_wps_sta(struct hostapd_data *hapd, struct sta_info *sta_info,
    const u8 *ies, u8 ies_len, bool *pmf_enable, int subtype)
{
    (void)pmf_enable;
    if (!sta_info || (!ies && ies_len)) return -1;
    uint32_t identity = esp32qjs_wps_ap_result_identity(hapd);
    int type = esp_wifi_get_wps_type_internal();
    /* Check admission before allocating an IE or touching registrar state. */
    if (wps_get_owner() != WPS_OWNER_REGISTRAR ||
        (type != WPS_TYPE_PBC && type != WPS_TYPE_PIN)) {
        esp32qjs_wps_ap_assoc_clear(hapd, sta_info);
        return 0;
    }
    if (!sta_info || (!ies && ies_len) || !hapd->wps ||
        !esp32qjs_wps_ap_result_activity_enter(identity)) return -1;
    int result = 0;
    bool has_wps = false;
    size_t offset = 0;
    while (offset < ies_len) {
        if ((size_t)ies_len - offset < 2 || ies[offset + 1] > (size_t)ies_len - offset - 2) {
            esp32qjs_wps_ap_result_peer_error(identity, ESP_ERR_INVALID_SIZE, sta_info->addr);
            result = -1;
            goto done;
        }
        if (ies[offset] == WLAN_EID_VENDOR_SPECIFIC && ies[offset + 1] >= 4 &&
            WPA_GET_BE32(ies + offset + 2) == WPS_DEV_OUI_WFA) has_wps = true;
        offset += (size_t)ies[offset + 1] + 2;
    }
    if (!has_wps) {
        esp32qjs_wps_ap_assoc_clear(hapd, sta_info);
        goto done;
    }
    struct wpabuf *next = ieee802_11_vendor_ie_concat(ies, ies_len, WPS_DEV_OUI_WFA);
    if (!next) {
        esp32qjs_wps_ap_result_peer_error(identity, ESP_ERR_NO_MEM, sta_info->addr);
        result = -1;
        goto done;
    }
    if (type == WPS_TYPE_PBC && esp_wps_registrar_check_pbc_overlap(hapd->wps)) {
        wpabuf_free(next);
        result = -1;
        goto done;
    }
    /* The EAP server duplicates assoc_wps_ie in this fixed SDK. Retiring an
     * old EAP object cannot keep a borrowed reference to this station IE. */
    ieee802_1x_free_station(hapd, sta_info);
    wpabuf_free(sta_info->wps_ie);
    sta_info->wps_ie = next;
    sta_info->eapol_sm = ieee802_1x_alloc_eapol_sm(hapd, sta_info);
    if (!sta_info->eapol_sm) {
        wpabuf_free(sta_info->wps_ie);
        sta_info->wps_ie = NULL;
        result = -1;
        goto done;
    }
    if (esp_send_assoc_resp(hapd, sta_info->addr, WLAN_STATUS_SUCCESS, true, subtype) != WLAN_STATUS_SUCCESS) {
        ieee802_1x_free_station(hapd, sta_info);
        wpabuf_free(sta_info->wps_ie);
        sta_info->wps_ie = NULL;
        result = -1;
    }
done:
    esp32qjs_wps_ap_result_callback_leave(identity);
    return result;
}
'''

JOIN_PIN = '''#ifdef CONFIG_WPS_REGISTRAR
    bool peer_busy = false;
    if (*sta) {
        struct sta_info *old = esp32qjs_wps_ap_peer_acquire(hapd, *sta, NULL, &peer_busy);
        if (peer_busy) goto peer_busy;
        if (!old) *sta = NULL;
        else if (!esp_wifi_ap_is_sta_sae_reauth_node(bssid)) {
            ap_free_sta(hapd, old);
            *sta = NULL;
        }
#ifdef CONFIG_SAE
        else if (old->lock) {
            sta_info = old;
            goto process_old_sta;
        }
#endif
    }
    sta_info = esp32qjs_wps_ap_peer_acquire(hapd, NULL, bssid, &peer_busy);
    if (peer_busy) goto peer_busy;
    if (!sta_info) {
        struct sta_info *created = ap_sta_add(hapd, bssid);
        if (!created) goto fail;
        /* ap_sta_add returns after unlocking the table. Revalidate before
         * touching even its semaphore; a foreign SAE task can remove it. */
        sta_info = esp32qjs_wps_ap_peer_acquire(hapd, created, NULL, &peer_busy);
        if (peer_busy) goto peer_busy;
        if (!sta_info) goto fail;
    }
#ifdef CONFIG_SAE
process_old_sta:
#endif
#else
'''


def patch_ap_input(text: str) -> str:
    text = replace(text, '#include "ap/sta_info.h"', '''#include "ap/sta_info.h"
#ifdef CONFIG_WPS_REGISTRAR
#include "esp32_mquickjs_wifi_wps_ap_result.h"
#endif''')
    body = function(text, 'wpa_ap_rx_eapol')
    text = replace(text, body, ACQUIRE + '\n#ifdef CONFIG_WPS_REGISTRAR\n' + RX + '#else\n' + body + '#endif\n')
    text = replace(text, function(text, 'check_n_add_wps_sta'), ASSOC)
    body = function(text, 'hostap_sta_join')
    start = body.index('    if (*sta) {')
    end = body.index('\n#ifdef CONFIG_WPS_REGISTRAR\n    if (check_n_add_wps_sta', start)
    fixed = body[:start] + JOIN_PIN + body[start:end] + '\n#endif\n' + body[end:]
    fixed = replace(fixed, '\n{\n', '''
{
#ifdef CONFIG_WPS_REGISTRAR
    if (!current_task_is_wifi_task()) return false;
#endif
''')
    fixed = replace(fixed, '''    if (!join) {
        return false;
    }''', '''    if (!join || !join->sm || !join->bssid || !join->pmf_enable ||
        !join->pairwise_cipher || (join->wpa_ie_len && !join->wpa_ie)) return false;''')
    fixed = replace(fixed, '''#ifdef CONFIG_SAE
    if (sta_info->lock) {
        os_semphr_give(sta_info->lock);
    }
#endif /* CONFIG_SAE */''', '''#ifdef CONFIG_WPS_REGISTRAR
    esp32qjs_wps_ap_peer_release(hapd, sta_info);
#else
#ifdef CONFIG_SAE
    if (sta_info->lock) os_semphr_give(sta_info->lock);
#endif
#endif''')
    fixed = replace(fixed, 'done:\n    *sta = sta_info;', '''done:
#if defined(CONFIG_WPS_REGISTRAR) && defined(CONFIG_SAE)
    if (sta_info->lock && atomic_load(&sta_info->remove_pending)) {
        if (*sta == sta_info) *sta = NULL;
        goto fail;
    }
#endif
    *sta = sta_info;''')
    fixed = replace(fixed, '\nfail:\n', '''
#ifdef CONFIG_WPS_REGISTRAR
peer_busy:
    /* Busy is not proof that authentication failed. Preserve the original
     * temporary rejection behavior and do not release an unowned semaphore. */
    if (esp_send_assoc_resp(hapd, bssid, WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY,
        rsnxe ? false : true, subtype) == WLAN_STATUS_SUCCESS) return false;
#endif
fail:
''')
    fixed = replace(fixed, '''#ifdef CONFIG_SAE
    if (sta_info && sta_info->lock) {
        os_semphr_give(sta_info->lock);
    }
#endif /* CONFIG_SAE */''', '''#ifdef CONFIG_WPS_REGISTRAR
    if (sta_info) {
#ifdef CONFIG_SAE
        if (sta_info->lock && atomic_load(&sta_info->remove_pending) && *sta == sta_info) *sta = NULL;
#endif
        esp32qjs_wps_ap_peer_release(hapd, sta_info);
    }
#else
#ifdef CONFIG_SAE
    if (sta_info && sta_info->lock) os_semphr_give(sta_info->lock);
#endif
#endif''')
    return replace(text, body, fixed)
