"""Exact peer delay tickets, resolved under the SDK station-table lock."""
from sdk_patches.common.source import function, replace

DELAYS = '''#ifdef CONFIG_WPS_REGISTRAR
/* Allocated with the real SDK station, never a detached pointer registry. */
struct esp32qjs_wps_peer_record {
    struct sta_info sta;
    uint64_t tickets[2]; /* driver removal, delayed EAP deauthentication */
    uint32_t identity;
    int error;
    bool remove_needed;
};
static uint64_t esp32qjs_wps_peer_last_ticket;
extern bool current_task_is_wifi_task(void);
static void esp32qjs_wps_peer_delay_fire(void *high, void *low);

static struct esp32qjs_wps_peer_record *esp32qjs_wps_peer_record(struct sta_info *sta)
{
    return (struct esp32qjs_wps_peer_record *)sta;
}

static void esp32qjs_wps_peer_cancel(struct sta_info *sta, unsigned kind)
{
    struct esp32qjs_wps_peer_record *record = esp32qjs_wps_peer_record(sta);
    uint64_t ticket = record->tickets[kind];
    record->tickets[kind] = 0;
    if (ticket) eloop_cancel_timeout(esp32qjs_wps_peer_delay_fire,
        (void *)(uintptr_t)(ticket >> 32), (void *)(uintptr_t)(uint32_t)ticket);
}

static int esp32qjs_wps_peer_schedule_locked(struct hostapd_data *hapd,
    struct sta_info *sta, unsigned kind)
{
    uint32_t identity = esp32qjs_wps_ap_result_identity(hapd);
    if (!current_task_is_wifi_task() || !identity || kind > 1) return ESP_ERR_INVALID_STATE;
    struct esp32qjs_wps_peer_record *record = esp32qjs_wps_peer_record(sta);
    if (record->identity && record->identity != identity &&
        (record->tickets[0] || record->tickets[1] || record->remove_needed))
        return ESP_ERR_INVALID_STATE;
    /* A driver removal is an obligation even if scheduling fails. Close may
     * finish it later, or transfer it to the existing SAE remove_pending path. */
    record->identity = identity;
    if (kind == 0) {
        record->remove_needed = true;
        esp32qjs_wps_ap_eapol_stop_peer(sta->eapol_sm);
    } else if (!esp32qjs_wps_ap_result_native_context(identity)) return ESP_ERR_INVALID_STATE;
    int error;
    if (esp32qjs_wps_peer_last_ticket == UINT64_MAX) error = ESP_ERR_NO_MEM;
    else {
        uint64_t ticket = ++esp32qjs_wps_peer_last_ticket;
        error = eloop_register_timeout(0, 10000, esp32qjs_wps_peer_delay_fire,
            (void *)(uintptr_t)(ticket >> 32), (void *)(uintptr_t)(uint32_t)ticket);
        if (error == ESP_OK) {
            esp32qjs_wps_peer_cancel(sta, kind);
            record->tickets[kind] = ticket;
            return ESP_OK;
        }
    }
    if (!record->error) record->error = error;
    return error;
}

int esp32qjs_wps_ap_peer_remove_locked(void *context, void *peer)
{
    return esp32qjs_wps_peer_schedule_locked(context, peer, 0);
}

static void esp32qjs_wps_peer_delay_fire(void *high, void *low)
{
    uint64_t ticket = ((uint64_t)(uintptr_t)high << 32) | (uint32_t)(uintptr_t)low;
    if (!ticket || !current_task_is_wifi_task()) return;
    struct hostapd_data *hapd = hostapd_get_hapd_data();
    uint32_t identity = esp32qjs_wps_ap_result_identity(hapd);
    if (!esp32qjs_wps_ap_result_activity_enter(identity)) return;
    struct sta_info *peer = NULL;
    unsigned kind = 0;
    bool busy = false;
    uint8_t addr[ETH_ALEN] = {0};
    HOSTAPD_STA_LIST_LOCK(hapd);
    for (struct sta_info *sta = hapd->sta_list; sta && !peer; sta = sta->next) {
        struct esp32qjs_wps_peer_record *record = esp32qjs_wps_peer_record(sta);
        if (record->identity != identity) continue;
        for (kind = 0; kind < 2; ++kind) {
            if (record->tickets[kind] != ticket) continue;
            peer = sta;
            os_memcpy(addr, sta->addr, ETH_ALEN);
#ifdef CONFIG_SAE
            if (sta->lock && !os_semphr_take(sta->lock, 0)) {
                busy = true;
                if (kind == 0) atomic_store(&sta->remove_pending, true);
            }
#endif
            if (!busy || kind == 0) record->tickets[kind] = 0;
            break;
        }
    }
    HOSTAPD_STA_LIST_UNLOCK(hapd);
    if (peer && busy && kind == 1) {
        int error = eloop_register_timeout(0, 1000, esp32qjs_wps_peer_delay_fire, high, low);
        if (error != ESP_OK) {
            HOSTAPD_STA_LIST_LOCK(hapd);
            for (struct sta_info *sta = hapd->sta_list; sta; sta = sta->next) {
                struct esp32qjs_wps_peer_record *record = esp32qjs_wps_peer_record(sta);
                if (record->identity == identity && record->tickets[1] == ticket) {
                    record->tickets[1] = 0;
                    if (!record->error) record->error = error;
                    break;
                }
            }
            HOSTAPD_STA_LIST_UNLOCK(hapd);
            esp32qjs_wps_ap_result_peer_error(identity, error, addr);
        }
    } else if (peer && !busy) {
        if (kind == 0) ap_free_sta(hapd, peer);
        else {
            int error = esp_wifi_ap_deauth_internal(addr, WLAN_REASON_IEEE_802_1X_AUTH_FAILED);
            if (error != ESP_OK) {
                struct esp32qjs_wps_peer_record *record = esp32qjs_wps_peer_record(peer);
                if (!record->error) record->error = error;
                esp32qjs_wps_ap_result_peer_error(identity, error, addr);
            }
            esp32qjs_wps_ap_peer_release(hapd, peer);
        }
    }
    esp32qjs_wps_ap_result_callback_leave(identity);
}

int esp32qjs_wps_ap_peer_delays_stop(void *context, uint32_t identity)
{
    struct hostapd_data *hapd = context;
    if (!current_task_is_wifi_task() || !identity ||
        esp32qjs_wps_ap_result_identity(hapd) != identity) return ESP_ERR_INVALID_STATE;
    for (;;) {
        struct sta_info *remove = NULL;
        int error = ESP_OK;
        HOSTAPD_STA_LIST_LOCK(hapd);
        for (struct sta_info *sta = hapd->sta_list; sta; sta = sta->next) {
            struct esp32qjs_wps_peer_record *record = esp32qjs_wps_peer_record(sta);
            if (record->identity != identity) {
                if (record->tickets[0] || record->tickets[1] || record->remove_needed)
                    error = ESP_ERR_INVALID_STATE;
                continue;
            }
            esp32qjs_wps_peer_cancel(sta, 1);
            esp32qjs_wps_peer_cancel(sta, 0);
            if (!record->remove_needed) continue;
#ifdef CONFIG_SAE
            if (sta->lock && !os_semphr_take(sta->lock, 0)) {
                atomic_store(&sta->remove_pending, true);
                error = ESP_ERR_INVALID_STATE;
                continue;
            }
#endif
            remove = sta;
            break;
        }
        HOSTAPD_STA_LIST_UNLOCK(hapd);
        if (!remove) return error;
        /* Never recursively acquire the non-recursive table mutex. */
        ap_free_sta(hapd, remove);
    }
}
#endif
'''

DEAUTH = '''void ap_sta_delayed_1x_auth_fail_disconnect(struct hostapd_data *hapd,
    struct sta_info *sta)
{
    uint8_t addr[ETH_ALEN] = {0};
    int error = ESP_ERR_INVALID_STATE;
    uint32_t identity = esp32qjs_wps_ap_result_identity(hapd);
    if (!esp32qjs_wps_ap_result_native_context(identity)) return;
    HOSTAPD_STA_LIST_LOCK(hapd);
    for (struct sta_info *live = hapd->sta_list; live; live = live->next) {
        if (live != sta) continue;
        os_memcpy(addr, live->addr, ETH_ALEN);
        error = esp32qjs_wps_peer_schedule_locked(hapd, live, 1);
        break;
    }
    HOSTAPD_STA_LIST_UNLOCK(hapd);
    if (error != ESP_OK) esp32qjs_wps_ap_result_peer_error(identity, error, addr);
    /* An individual EAP failure is not global WPS_STATUS_SUCCESS. */
}
'''

PENDING = '''int ap_sta_pending_delayed_1x_auth_fail_disconnect(struct hostapd_data *hapd,
    struct sta_info *sta)
{
    /* Called by the SDK with peer/table ownership already established. */
    (void)hapd;
    return sta && esp32qjs_wps_peer_record(sta)->tickets[1] != 0;
}
'''


def patch_peer_delays(relative: str, text: str) -> str:
    if relative == 'esp_supplicant/src/esp_hostap.c':
        text = replace(text, function(text, 'ap_free_sta_timeout'), '')
        body = function(text, 'wpa_ap_remove')
        start = body.index('        HOSTAPD_STA_LIST_UNLOCK(hapd);', body.index('if (wps_get_status()'))
        end = body.index('        return true;\n', start) + len('        return true;\n')
        fixed = body[:start] + '''        int error = esp32qjs_wps_ap_peer_remove_locked(hapd, sta);
        HOSTAPD_STA_LIST_UNLOCK(hapd);
        return error == ESP_OK;
''' + body[end:]
        return replace(text, body, fixed)
    if relative != 'src/ap/sta_info.c': return text
    text = replace(text, '#include "esp_wps_i.h"', '''#include "esp_wps_i.h"
#ifdef CONFIG_WPS_REGISTRAR
#include "esp32_mquickjs_wifi_wps_ap_result.h"
#endif''')
    forward = 'static void ap_sta_delayed_1x_auth_fail_cb(void *eloop_ctx, void *timeout_ctx);'
    text = replace(text, forward, '#ifndef CONFIG_WPS_REGISTRAR\n' + forward + '\n#endif\n\n' + DELAYS)
    text = replace(text, '\tsta = os_zalloc(sizeof(struct sta_info));', '''#ifdef CONFIG_WPS_REGISTRAR
    sta = os_zalloc(sizeof(struct esp32qjs_wps_peer_record));
#else
\tsta = os_zalloc(sizeof(struct sta_info));
#endif''')
    body = function(text, 'ap_free_sta')
    fixed = replace(body, '\tap_sta_hash_del(hapd, sta);', '''#ifdef CONFIG_WPS_REGISTRAR
    esp32qjs_wps_peer_cancel(sta, 0);
    esp32qjs_wps_peer_cancel(sta, 1);
    /* Transfer every EAP reference before the peer disappears from the table. */
    esp32qjs_wps_ap_eapol_detach_peer(sta);
#endif
\tap_sta_hash_del(hapd, sta);''')
    fixed = replace(fixed, '''\tif (ap_sta_pending_delayed_1x_auth_fail_disconnect(hapd, sta))
\t\teloop_cancel_timeout(ap_sta_delayed_1x_auth_fail_cb, hapd, sta);

\tieee802_1x_free_station(hapd, sta);
''', '')
    fixed = replace(fixed, '\tos_free(sta);', '''#ifdef CONFIG_WPS_REGISTRAR
    bin_clear_free(sta, sizeof(struct esp32qjs_wps_peer_record));
#else
\tos_free(sta);
#endif''')
    text = replace(text, body, fixed)
    body = function(text, 'ap_sta_delayed_1x_auth_fail_cb')
    text = replace(text, body, '#ifndef CONFIG_WPS_REGISTRAR\n' + body + '#endif\n')
    for name, fixed in [('ap_sta_delayed_1x_auth_fail_disconnect', DEAUTH),
                        ('ap_sta_pending_delayed_1x_auth_fail_disconnect', PENDING)]:
        body = function(text, name)
        text = replace(text, body, '#ifdef CONFIG_WPS_REGISTRAR\n' + fixed + '#else\n' + body + '#endif\n')
    return text
