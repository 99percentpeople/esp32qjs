"""Fixed-SDK EAPOL timer identity and AP/peer lifetime boundaries.

Timers carry AP result identity and a never-reused pending-operation number.
Live resolution uses the real hostapd station table; no detached pointer cache.
"""
from sdk_patches.common.source import function, replace

TIMERS = '''/* Every eapol_auth_alloc allocation embeds the SDK state first. No
 * SDK header/layout changes are required in other translation units. */
struct esp32qjs_wps_eapol_record {
    struct eapol_state_machine sm;
    uint32_t identity;
    uint32_t timers[2];
    int error;
    bool stopped;
};
static uint32_t esp32qjs_wps_eapol_last_timer;
extern bool current_task_is_wifi_task(void);
static void eapol_port_timers_tick(void *eloop_ctx, void *timeout_ctx);
static void esp32qjs_wps_eapol_timer_fire(void *high, void *low);

static struct esp32qjs_wps_eapol_record *esp32qjs_wps_eapol_record(struct eapol_state_machine *sm)
{
    return (struct esp32qjs_wps_eapol_record *)sm;
}

static void esp32qjs_wps_eapol_cancel(struct eapol_state_machine *sm, unsigned index)
{
    struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sm);
    uint32_t timer = record->timers[index];
    record->timers[index] = 0;
    if (timer) eloop_cancel_timeout(esp32qjs_wps_eapol_timer_fire,
        (void *)(uintptr_t)record->identity, (void *)(uintptr_t)timer);
}

static void esp32qjs_wps_eapol_stop_record(struct eapol_state_machine *sm)
{
    struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sm);
    record->stopped = true;
    esp32qjs_wps_eapol_cancel(sm, 0);
    esp32qjs_wps_eapol_cancel(sm, 1);
}

static void esp32qjs_wps_eapol_mark_error(struct eapol_state_machine *sm, int error)
{
    struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sm);
    if (!record->error) record->error = error;
    esp32qjs_wps_eapol_stop_record(sm);
    sm->exit_sm_step_run = true;
}

static void esp32qjs_wps_eapol_fail(struct eapol_state_machine *sm, int error)
{
    struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sm);
    esp32qjs_wps_eapol_mark_error(sm, error);
    /* Preserve the error and peer, without disabling the parent registrar or
     * disconnecting any other peer. Public terminal state remains first-wins. */
    esp32qjs_wps_ap_result_peer_error(record->identity, record->error, sm->addr);
}

static int esp32qjs_wps_eapol_arm(struct eapol_state_machine *sm, unsigned index)
{
    struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sm);
    if (!current_task_is_wifi_task() || index > 1 || record->stopped ||
        !esp32qjs_wps_ap_result_native_context(record->identity)) return ESP_ERR_INVALID_STATE;
    /* A pending step already observes all newly set state flags. */
    if (index == 1 && record->timers[index]) return ESP_OK;
    int error;
    if (esp32qjs_wps_eapol_last_timer == UINT32_MAX) error = ESP_ERR_NO_MEM;
    else {
        uint32_t timer = ++esp32qjs_wps_eapol_last_timer;
        error = eloop_register_timeout(index == 0 ? 1 : 0, 0,
            esp32qjs_wps_eapol_timer_fire, (void *)(uintptr_t)record->identity,
            (void *)(uintptr_t)timer);
        if (error == ESP_OK) {
            esp32qjs_wps_eapol_cancel(sm, index);
            record->timers[index] = timer;
            return ESP_OK;
        }
    }
    esp32qjs_wps_eapol_fail(sm, error);
    return error;
}

static void esp32qjs_wps_eapol_timer_fire(void *high, void *low)
{
    uint32_t identity = (uint32_t)(uintptr_t)high;
    uint32_t timer = (uint32_t)(uintptr_t)low;
    if (!timer) return;
    struct hostapd_data *hapd = esp32qjs_wps_ap_result_activity_enter(identity);
    if (!hapd) return;
    struct sta_info *peer = NULL;
    struct eapol_state_machine *sm = NULL;
    unsigned index = 0;
    bool busy = false;
    uint8_t addr[ETH_ALEN] = {0};
    HOSTAPD_STA_LIST_LOCK(hapd);
    for (struct sta_info *sta = hapd->sta_list; sta && !peer; sta = sta->next) {
        if (!sta->eapol_sm) continue;
        struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sta->eapol_sm);
        if (record->identity != identity || record->stopped) continue;
        for (index = 0; index < 2; ++index) {
            if (record->timers[index] != timer) continue;
            peer = sta;
            os_memcpy(addr, sta->addr, ETH_ALEN);
#ifdef CONFIG_SAE
            if (sta->lock && !os_semphr_take(sta->lock, 0)) busy = true;
#endif
            if (!busy) {
                record->timers[index] = 0;
                sm = sta->eapol_sm;
            }
            break;
        }
    }
    HOSTAPD_STA_LIST_UNLOCK(hapd);
    if (busy) {
        /* Retry the same unexecuted pending operation, with numbers only.
         * The peer may disappear immediately after unlock. Never dereference
         * peer/record in this branch. Cancellation revokes this number. */
        int error = eloop_register_timeout(0, 1000, esp32qjs_wps_eapol_timer_fire, high, low);
        if (error != ESP_OK) {
            /* The foreign task may already have removed/freed the old peer.
             * Re-resolve only the exact ticket while table membership pins
             * its storage; no stale pointer or replacement-by-MAC mutation. */
            HOSTAPD_STA_LIST_LOCK(hapd);
            for (struct sta_info *sta = hapd->sta_list; sta; sta = sta->next) {
                if (!sta->eapol_sm) continue;
                struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sta->eapol_sm);
                if (record->identity == identity &&
                    (record->timers[0] == timer || record->timers[1] == timer)) {
                    esp32qjs_wps_eapol_mark_error(sta->eapol_sm, error);
                    break;
                }
            }
            HOSTAPD_STA_LIST_UNLOCK(hapd);
            esp32qjs_wps_ap_result_peer_error(identity, error, addr);
        }
    } else if (sm) {
        if (index == 0) eapol_port_timers_tick(NULL, sm);
        else eapol_sm_step_cb(sm, NULL);
        esp32qjs_wps_ap_peer_release(hapd, peer);
    }
    esp32qjs_wps_ap_result_callback_leave(identity);
}

int esp32qjs_wps_ap_eapol_stop(void *context, uint32_t identity)
{
    struct hostapd_data *hapd = context;
    if (!hapd || !identity || esp32qjs_wps_ap_result_identity(hapd) != identity)
        return ESP_ERR_INVALID_STATE;
    int error = ESP_OK;
    HOSTAPD_STA_LIST_LOCK(hapd);
    for (struct sta_info *sta = hapd->sta_list; sta; sta = sta->next) {
        if (!sta->eapol_sm) continue;
        struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sta->eapol_sm);
        if (record->identity != identity) { error = ESP_ERR_INVALID_STATE; break; }
        esp32qjs_wps_eapol_stop_record(sta->eapol_sm);
    }
    HOSTAPD_STA_LIST_UNLOCK(hapd);
    return error;
}
'''

PEER_RELEASE = '''#ifdef CONFIG_WPS_REGISTRAR
/* Caller owns sta->lock when SAE exists, and an AP activity reference. The
 * SDK deletion convention consumes the held semaphore in ap_free_sta. */
void esp32qjs_wps_ap_peer_release(void *context, void *peer)
{
#ifdef CONFIG_SAE
    struct hostapd_data *hapd = context;
    struct sta_info *sta = peer;
    if (sta->lock) {
        if (atomic_load(&sta->remove_pending)) ap_free_sta(hapd, sta);
        else os_semphr_give(sta->lock);
    }
#else
    (void)context;
    (void)peer;
#endif
}
#endif
'''


def patch_eapol(relative: str, text: str) -> str:
    if relative == 'src/eapol_auth/eapol_auth_sm.c':
        for name in ('eapol_auth_sm.h', 'eapol_auth_sm_i.h'):
            text = replace(text, f'#include "{name}"', f'#include "eapol_auth/{name}"')
        text = replace(text, '#include "eapol_auth/eapol_auth_sm_i.h"', '''#include "eapol_auth/eapol_auth_sm_i.h"
#include "esp_err.h"
#include "ap/hostapd.h"
#include "ap/sta_info.h"
#include "esp32_mquickjs_wifi_wps_ap_result.h"''')
        anchor = 'static void eapol_auth_conf_free(struct eapol_auth_config *conf);'
        text = replace(text, anchor, anchor + '\n\n' + TIMERS)
        text = replace(text, function(text, 'wifi_ap_wps_disable_timeout_handler'), '')
        text = replace(text, '\teloop_register_timeout(0, 0, wifi_ap_wps_disable_timeout_handler, NULL, NULL);',
                       '\tesp32qjs_wps_eapol_fail(sm, sm->authTimeout ? ESP_ERR_TIMEOUT : ESP_FAIL);')
        # All EAPOL timer registrations now resolve through the real peer table.
        text = replace(text, '\teloop_register_timeout(1, 0, eapol_port_timers_tick, eloop_ctx, state);',
                       '\tesp32qjs_wps_eapol_arm(state, 0);')
        text = replace(text, '\teloop_register_timeout(0, 0, eapol_sm_step_cb, sm, NULL);',
                       '\tesp32qjs_wps_eapol_arm(sm, 1);')
        text = replace(text, '\teloop_register_timeout(1, 0, eapol_port_timers_tick, NULL, sm);',
                       '\tesp32qjs_wps_eapol_arm(sm, 0);')
        text = text.replace('eloop_cancel_timeout(eapol_port_timers_tick, NULL, sm)',
                            'esp32qjs_wps_eapol_cancel(sm, 0)')
        text = text.replace('eloop_cancel_timeout(eapol_sm_step_cb, sm, NULL)',
                            'esp32qjs_wps_eapol_cancel(sm, 1)')
        text = replace(text, '\tsm = os_zalloc(sizeof(*sm));', '''    uint32_t identity_id = esp32qjs_wps_ap_result_identity(eapol->conf.ctx);
    if (!esp32qjs_wps_ap_result_native_context(identity_id)) return NULL;
    sm = os_zalloc(sizeof(struct esp32qjs_wps_eapol_record));''')
        text = replace(text, '\tsm->radius_identifier = -1;',
                       '    esp32qjs_wps_eapol_record(sm)->identity = identity_id;\n\tsm->radius_identifier = -1;')
        text = replace(text, '\teapol_auth_initialize(sm);', '''\teapol_auth_initialize(sm);
    if (esp32qjs_wps_eapol_record(sm)->error) {
        eapol_auth_free(sm);
        return NULL;
    }''')
        body = function(text, 'eapol_auth_free')
        fixed = replace(body, '\tos_free(sm);', '\tbin_clear_free(sm, sizeof(struct esp32qjs_wps_eapol_record));')
        text = replace(text, body, fixed)
        body = function(text, 'eapol_sm_step_run')
        implementation = replace(body, 'eapol_sm_step_run(', 'esp32qjs_eapol_sm_step_run_body(')
        wrapper = '''static void eapol_sm_step_run(struct eapol_state_machine *sm)
{
    struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sm);
    uint32_t identity = record->identity;
    if (record->stopped || !esp32qjs_wps_ap_result_activity_enter(identity)) return;
    esp32qjs_eapol_sm_step_run_body(sm);
    esp32qjs_wps_ap_result_callback_leave(identity);
}
'''
        return replace(text, body, implementation + '\n' + wrapper)
    if relative == 'src/ap/ieee802_1x.c':
        text = replace(text, '#include "utils/includes.h"', '''#include "utils/includes.h"
#ifdef CONFIG_WPS_REGISTRAR
#include "esp32_mquickjs_wifi_wps_ap_result.h"
#endif''')
        body = function(text, 'ieee802_1x_receive')
        implementation = replace(body, 'void ieee802_1x_receive(', 'static void esp32qjs_ieee802_1x_receive_body(')
        wrapper = '''void ieee802_1x_receive(struct hostapd_data *hapd, const u8 *sa,
    const u8 *buf, size_t len)
{
#ifdef CONFIG_WPS_REGISTRAR
    uint32_t identity = esp32qjs_wps_ap_result_identity(hapd);
    if (!sa || !buf || !esp32qjs_wps_ap_result_activity_enter(identity)) return;
    HOSTAPD_STA_LIST_LOCK(hapd);
    struct sta_info *sta = ap_get_sta_internal(hapd, sa);
#ifdef CONFIG_SAE
    if (sta && sta->lock && !os_semphr_take(sta->lock, 0)) sta = NULL;
#endif
    HOSTAPD_STA_LIST_UNLOCK(hapd);
    if (sta) {
        esp32qjs_ieee802_1x_receive_body(hapd, sa, buf, len);
        esp32qjs_wps_ap_peer_release(hapd, sta);
    }
    esp32qjs_wps_ap_result_callback_leave(identity);
#else
    esp32qjs_ieee802_1x_receive_body(hapd, sa, buf, len);
#endif
}
'''
        pinned = '''#ifdef CONFIG_WPS_REGISTRAR
void esp32qjs_wps_ap_receive_pinned(void *context, void *peer, const uint8_t *data, size_t size)
{
    struct hostapd_data *hapd = context;
    struct sta_info *sta = peer;
    /* The driver wrapper owns the peer semaphore and AP activity through
     * this whole call. Avoid taking that non-recursive semaphore twice. */
    if (hapd && sta && esp32qjs_wps_ap_result_native_context(esp32qjs_wps_ap_result_identity(hapd)))
        esp32qjs_ieee802_1x_receive_body(hapd, sta->addr, data, size);
}
#endif
'''
        return replace(text, body, PEER_RELEASE + '\n' + implementation + '\n' + pinned + '\n' + wrapper)
    if relative == 'esp_supplicant/src/esp_hostap.c':
        body = function(text, 'ap_free_sta_timeout')
        fixed = replace(body, '\ndone:\n', '\n#ifdef CONFIG_SAE\ndone:\n#endif\n')
        text = replace(text, body, fixed)
        return replace(text, '    if (wps_get_status() == WPS_STATUS_PENDING) {',
                       '    if (wps_get_status() == WPS_STATUS_PENDING || esp32qjs_wps_ap_result_busy(hapd)) {')
    return text
