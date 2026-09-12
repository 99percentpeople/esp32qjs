"""Transfer EAP destruction to the Wi-Fi task before station unlink.

The intrusive list owns retired storage. The scalar child ledger includes live,
retired and currently destroying records; producer references also cover the
interval between queue publication and the foreign-task eloop registration.
"""
from patch_idf_wps import function, replace

RETIRE = '''static portMUX_TYPE esp32qjs_wps_eapol_mux = portMUX_INITIALIZER_UNLOCKED;
static struct esp32qjs_wps_eapol_record *esp32qjs_wps_eapol_retired;
static uint32_t esp32qjs_wps_eapol_owner;
static uint32_t esp32qjs_wps_eapol_children;
static uint32_t esp32qjs_wps_eapol_producers;
static uint32_t esp32qjs_wps_eapol_wake_identity;
static void esp32qjs_wps_eapol_destroy(struct eapol_state_machine *sm);
static void esp32qjs_wps_eapol_wake(void *identity_arg, void *unused);

/* No SDK calls or destructors execute inside this critical section. */
static bool esp32qjs_wps_eapol_adopt(uint32_t identity)
{
    bool ok;
    portENTER_CRITICAL(&esp32qjs_wps_eapol_mux);
    ok = identity && (!esp32qjs_wps_eapol_owner || esp32qjs_wps_eapol_owner == identity) &&
        esp32qjs_wps_eapol_children != UINT32_MAX;
    if (ok) {
        esp32qjs_wps_eapol_owner = identity;
        ++esp32qjs_wps_eapol_children;
    }
    portEXIT_CRITICAL(&esp32qjs_wps_eapol_mux);
    return ok;
}

static bool esp32qjs_wps_eapol_ref(struct eapol_state_machine *sm)
{
    struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sm);
    portENTER_CRITICAL(&esp32qjs_wps_eapol_mux);
    bool ok = !record->retired && record->references != UINT32_MAX;
    if (ok) ++record->references;
    portEXIT_CRITICAL(&esp32qjs_wps_eapol_mux);
    return ok;
}

static bool esp32qjs_wps_eapol_is_retired(struct eapol_state_machine *sm)
{
    portENTER_CRITICAL(&esp32qjs_wps_eapol_mux);
    bool retired = esp32qjs_wps_eapol_record(sm)->retired;
    portEXIT_CRITICAL(&esp32qjs_wps_eapol_mux);
    return retired;
}

static void esp32qjs_wps_eapol_kick(void)
{
    uint32_t identity = 0;
    portENTER_CRITICAL(&esp32qjs_wps_eapol_mux);
    if (esp32qjs_wps_eapol_retired && !esp32qjs_wps_eapol_wake_identity &&
        esp32qjs_wps_eapol_producers != UINT32_MAX) {
        identity = esp32qjs_wps_eapol_owner;
        esp32qjs_wps_eapol_wake_identity = identity;
        ++esp32qjs_wps_eapol_producers;
    }
    portEXIT_CRITICAL(&esp32qjs_wps_eapol_mux);
    if (!identity) return;
    int error = eloop_register_timeout(0, 0, esp32qjs_wps_eapol_wake,
        (void *)(uintptr_t)identity, NULL);
    portENTER_CRITICAL(&esp32qjs_wps_eapol_mux);
    if (error != ESP_OK && esp32qjs_wps_eapol_wake_identity == identity)
        esp32qjs_wps_eapol_wake_identity = 0;
    --esp32qjs_wps_eapol_producers;
    portEXIT_CRITICAL(&esp32qjs_wps_eapol_mux);
    /* Failed wake allocation retains the list and its child count. Explicit
     * close and the next allocation drain it without requiring another wake. */
}

static void esp32qjs_wps_eapol_unref(struct eapol_state_machine *sm)
{
    portENTER_CRITICAL(&esp32qjs_wps_eapol_mux);
    struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sm);
    if (record->references) --record->references;
    portEXIT_CRITICAL(&esp32qjs_wps_eapol_mux);
    esp32qjs_wps_eapol_kick();
}

void eapol_auth_free(struct eapol_state_machine *sm)
{
    if (!sm) return;
    struct esp32qjs_wps_eapol_record *record = esp32qjs_wps_eapol_record(sm);
    portENTER_CRITICAL(&esp32qjs_wps_eapol_mux);
    if (!record->retired) {
        record->retired = true;
        record->retire_next = esp32qjs_wps_eapol_retired;
        esp32qjs_wps_eapol_retired = record;
    }
    portEXIT_CRITICAL(&esp32qjs_wps_eapol_mux);
    /* Do not touch record after publication; a native drain may free it. */
    esp32qjs_wps_eapol_kick();
}

void esp32qjs_wps_ap_eapol_detach_peer(void *peer)
{
    struct sta_info *sta = peer;
    struct eapol_state_machine *sm = sta->eapol_sm;
    sta->eapol_sm = NULL;
    eapol_auth_free(sm);
}

void esp32qjs_wps_ap_eapol_stop_peer(void *state)
{
    if (state) esp32qjs_wps_eapol_stop_record(state);
}

int esp32qjs_wps_ap_eapol_drain(void *context, uint32_t identity)
{
    if (!current_task_is_wifi_task() || !identity ||
        esp32qjs_wps_ap_result_identity(context) != identity) return ESP_ERR_INVALID_STATE;
    for (;;) {
        struct esp32qjs_wps_eapol_record *record = NULL;
        portENTER_CRITICAL(&esp32qjs_wps_eapol_mux);
        struct esp32qjs_wps_eapol_record **link = &esp32qjs_wps_eapol_retired;
        while (*link) {
            if ((*link)->identity == identity && !(*link)->references) {
                record = *link;
                *link = record->retire_next;
                break;
            }
            link = &(*link)->retire_next;
        }
        portEXIT_CRITICAL(&esp32qjs_wps_eapol_mux);
        if (!record) return ESP_OK;
        /* WPS reset may release registrar PIN locks. Its parent must remain
         * attached until this destructor has returned and the count drops. */
        esp32qjs_wps_eapol_destroy(&record->sm);
        portENTER_CRITICAL(&esp32qjs_wps_eapol_mux);
        --esp32qjs_wps_eapol_children;
        portEXIT_CRITICAL(&esp32qjs_wps_eapol_mux);
    }
}

bool esp32qjs_wps_ap_eapol_drained(uint32_t identity)
{
    if (!current_task_is_wifi_task() || !identity) return false;
    uint32_t cancel = 0;
    portENTER_CRITICAL(&esp32qjs_wps_eapol_mux);
    bool drained = (!esp32qjs_wps_eapol_owner || esp32qjs_wps_eapol_owner == identity) &&
        !esp32qjs_wps_eapol_children && !esp32qjs_wps_eapol_retired &&
        !esp32qjs_wps_eapol_producers;
    if (drained) {
        cancel = esp32qjs_wps_eapol_wake_identity;
        esp32qjs_wps_eapol_wake_identity = 0;
        esp32qjs_wps_eapol_owner = 0;
    }
    portEXIT_CRITICAL(&esp32qjs_wps_eapol_mux);
    if (cancel) eloop_cancel_timeout(esp32qjs_wps_eapol_wake,
        (void *)(uintptr_t)cancel, NULL);
    return drained;
}

static void esp32qjs_wps_eapol_wake(void *identity_arg, void *unused)
{
    (void)unused;
    uint32_t identity = (uint32_t)(uintptr_t)identity_arg;
    if (!current_task_is_wifi_task() || !identity) return;
    portENTER_CRITICAL(&esp32qjs_wps_eapol_mux);
    bool valid = esp32qjs_wps_eapol_wake_identity == identity;
    if (valid) esp32qjs_wps_eapol_wake_identity = 0;
    portEXIT_CRITICAL(&esp32qjs_wps_eapol_mux);
    if (!valid) return;
    /* Closing is allowed here; native_context intentionally rejects closing
     * and would strand exactly the records this callback has to destroy. */
    void *context = hostapd_get_hapd_data();
    if (esp32qjs_wps_ap_result_identity(context) == identity)
        esp32qjs_wps_ap_eapol_drain(context, identity);
}
'''

HOST_RELEASE = '''int esp32qjs_wps_ap_hostap_release(void *context)
{
    struct hostapd_data *hapd = context;
    if (!hapd || !esp32qjs_wps_ap_result_deinit_allowed(hapd)) return ESP_ERR_INVALID_STATE;
    uint32_t identity = esp32qjs_wps_ap_result_identity(hapd);
    struct wps_sm *sm = wps_sm_get();
    struct wps_context *wps = hapd->wps;
    if (!wps && sm) wps = sm->wps_ctx;
    if (wps && (!sm || sm->wps_ctx != wps)) return ESP_ERR_INVALID_STATE;
    /* The SDK owner retains wps_ctx across retries after admission is revoked. */
    hapd->wps = NULL;
#ifdef ESP_SUPPLICANT
    ap_for_each_sta(hapd, ap_sta_server_sm_deinit, NULL);
#endif
    int error = esp32qjs_wps_ap_eapol_drain(hapd, identity);
    if (error != ESP_OK) return error;
    if (!esp32qjs_wps_ap_eapol_drained(identity)) return ESP_ERR_INVALID_STATE;
    esp32qjs_wps_eapol_deinit(hapd);
    if (wps) {
        wps_registrar_deinit(wps->registrar);
        wps->registrar = NULL;
        eap_server_unregister_methods();
    }
    return ESP_OK;
}

void hostapd_deinit_wps(struct hostapd_data *hapd)
{
    /* Legacy SDK void boundary cannot convey incomplete cleanup, but must not
     * free its parent on failure. The checked SDK deinit uses the helper. */
    (void)esp32qjs_wps_ap_hostap_release(hapd);
}
'''


def patch_eapol_retire(relative: str, text: str) -> str:
    if relative == 'src/eapol_auth/eapol_auth_sm.c':
        text = replace(text, '#include "esp_err.h"', '#include "esp_err.h"\n#include "freertos/FreeRTOS.h"')
        text = replace(text, '    bool stopped;\n', '''    bool stopped;
    bool retired;
    uint32_t references;
    struct esp32qjs_wps_eapol_record *retire_next;
''')
        # Definitions follow stop_record; prototypes precede the timer wrapper.
        anchor = 'static void esp32qjs_wps_eapol_mark_error('
        pos = text.index(anchor)
        text = text[:pos] + RETIRE + '\n' + text[pos:]
        text = replace(text, 'index > 1 || record->stopped ||',
                       'index > 1 || record->stopped || esp32qjs_wps_eapol_is_retired(sm) ||')
        text = replace(text, '                sm = sta->eapol_sm;', '''                if (esp32qjs_wps_eapol_ref(sta->eapol_sm)) sm = sta->eapol_sm;
#ifdef CONFIG_SAE
                else if (sta->lock) os_semphr_give(sta->lock);
#endif''')
        text = replace(text, '        esp32qjs_wps_ap_peer_release(hapd, peer);', '''        esp32qjs_wps_ap_peer_release(hapd, peer);
        esp32qjs_wps_eapol_unref(sm);''')
        text = replace(text, '    uint32_t identity_id = esp32qjs_wps_ap_result_identity(eapol->conf.ctx);', '''    if (!current_task_is_wifi_task()) return NULL;
    uint32_t identity_id = esp32qjs_wps_ap_result_identity(eapol->conf.ctx);
    if (esp32qjs_wps_ap_eapol_drain(eapol->conf.ctx, identity_id) != ESP_OK) return NULL;''')
        text = replace(text, '    esp32qjs_wps_eapol_record(sm)->identity = identity_id;', '''    if (!esp32qjs_wps_eapol_adopt(identity_id)) {
        bin_clear_free(sm, sizeof(struct esp32qjs_wps_eapol_record));
        return NULL;
    }
    esp32qjs_wps_eapol_record(sm)->identity = identity_id;''')
        # The first free definition is the queue entry above. Rename only the
        # original SDK destructor, found by its NULL guard and timer cleanup.
        start = text.index('void eapol_auth_free(struct eapol_state_machine *sm)\n{\n\tif (sm == NULL)')
        text = text[:start] + text[start:].replace('void eapol_auth_free(', 'static void esp32qjs_wps_eapol_destroy(', 1)
        text = replace(text, '    esp32qjs_eapol_sm_step_run_body(sm);', '''    if (esp32qjs_wps_eapol_ref(sm)) {
        esp32qjs_eapol_sm_step_run_body(sm);
        esp32qjs_wps_eapol_unref(sm);
    }''')
        return text
    if relative == 'src/ap/wps_hostapd.c':
        return replace(text, function(text, 'hostapd_deinit_wps'), HOST_RELEASE)
    if relative == 'esp_supplicant/src/esp_hostpad_wps.c':
        body = function(text, 'wifi_ap_wps_deinit')
        fixed = replace(body, '    hostapd_deinit_wps(hostapd_get_hapd_data());', '''    int release_error = esp32qjs_wps_ap_hostap_release(hostapd_get_hapd_data());
    if (release_error != ESP_OK) return release_error;''')
        return replace(text, body, fixed)
    return text
