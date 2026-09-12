"""Fixed-SDK registrar credentials and per-peer EAP ownership corrections.

Does not provide managed operation identity, callback retirement or JS registrar
support. Generated sources remain local to an immutable Build Context.
"""
from __future__ import annotations
import hashlib
from patch_idf_wps import function, patch_rf_builders, replace
from patch_idf_wps_registrar_timers import patch_registrar_timers
from patch_idf_wps_ap_result import patch_ap_result
from patch_idf_wps_ap_close import patch_ap_close
from patch_idf_wps_eapol import patch_eapol
from patch_idf_wps_eapol_retire import patch_eapol_retire
from patch_idf_wps_peer_delays import patch_peer_delays
from patch_idf_wps_ap_init_cleanup import patch_ap_init_cleanup
from patch_idf_wps_ap_input import patch_ap_input
from patch_idf_wps_ap_commands import patch_ap_commands
from patch_idf_eap_control import patch_source as patch_eap_control

REVIEWED = {
    'src/common/ieee802_11_common.c': '70a9a35d3e936dcf64c4a0af37b204f4ec0e03779bcba031d8747955468b5745',
    'src/common/ieee802_11_common.h': 'e4a6b133ab49e37615f63241843dbf83ca2fb7e17d7ad9872e4749fe53ef5b8a',
    'src/common/ieee802_11_defs.h': '25ca88c2ea41b975c380d535c4b1ce33272691878b24c20c52f9c73dccc0f5eb',
    'src/ap/sta_info.h': '32329efc7f96e7821da6f4d78da3a9213bf0d5b27843cf72bffe0534dc689c3f',
    'src/eapol_auth/eapol_auth_sm.h': '2e44902b9a6f66e5246644d23e88a3665776dbf3e9d30286361d3272ba6fba69',
    'esp_supplicant/src/esp_wpa_main.c': 'c4c6ff42611816ef389501174bf84906585f1e81e9ef2369bca0b79d02487d17',
    'esp_supplicant/src/esp_wpa3.c': '2f331f980947e6ba375d1085882d6892a52b2cf120e91eed1e8e04449b4d23fe',
    'esp_supplicant/src/esp_hostap.c': '5a0b9de381c98ca88b91406731b42bc9e7c600eceb67d082a96064940f161beb',
    'src/eap_server/eap.h': 'c66c548a9041fc28ac3051a32bdfaae952b0650d7db014f4e9240c0277745f0a',
    'src/eap_server/eap_i.h': '6b5529cd3b394bb3a81c58b508c34b139e83daa3cca3cc95e4c3901242dbc85a',
    'src/eap_server/eap_server.c': '7dbc5784366367fc0655dedcca181260a4f850b12a0da42ca667789d4c6b8ad0',
    'src/eapol_auth/eapol_auth_sm_i.h': '9a5582b80efd17b7fa57470b7e69239fded94080607315619c20293cbbc6e568',
    'src/ap/sta_info.c': '949734673599b57c71ab983ccd9af36dc5d50f2c4596176ea2202a8a4489f900',
    'src/eap_common/eap_wsc_common.h': '21c30d5dbff0d05165b7d545683bcc96eca0649ae374a000b0bcc200f7759b47',
    'esp_supplicant/src/esp_hostpad_wps.c': '5b38eb880d60ea2b7f69eaba8e11163fbca63a06fb3c06b396ba17383150e533',
    'src/ap/wps_hostapd.c': '180a86d14081dc5461005d10b3bb606d5dc309a5623d98fdf4aa0777c4aaed2a',
    'src/ap/ieee802_1x.c': '7e4942b915a64473d02dfac6ed4d7b76923a4f0af926b3243f4be4f07eabd2fa',
    'src/eap_server/eap_server_wsc.c': '4283d48e85e884d63263f157a248c8d2e9e8a4882e161023f5b9b95d42e93464',
    'src/ap/wps_hostapd.h': 'ab3e1f472eed4e7a2c7bf168b80f75e9675fa551b95397fbfee5476cb6b24049',
    'src/ap/hostapd.h': '8174bef9b4855009c73b74ae14ce65659c2539a803a96e0c97799c2a44ac06dd',
    'src/ap/ap_config.h': 'daf473bf66de36072dfccb33af83317ba5a48be16da9c3a6aca2b0a842a2d40a',
    'src/ap/wpa_auth_i.h': '8e3f4f699d48efd0ab857d017a4399c4c714089c66284d4cdb36e9adc8d7c13c',
    'src/ap/wpa_auth.h': '5ad667bb5119e6c6f0b7365d9b39f318bedabf3e08eb96725e6e4039df61cb0c',
    'src/eap_server/eap_server_methods.c': 'ec6c90a87074ef2229f84b55f64422524d2ba208a7df70009b6b002343cf9ac1',
    'src/eap_server/eap_server_identity.c': '4212ec87190d494ebcc4d382ecfcac3a7941fbda44a2af0e3374d1528d0597b6',
    'src/eapol_auth/eapol_auth_sm.c': '160baef342f938c62bcaeff76ef7023164815a866ba56b8f7ad8ec5c5367fac1',
    'src/wps/wps_registrar.c': 'fe2b3a7a7fa28dd26f66fbf1b5b98c372c465e9ee349274eda8898c09b6fbce8',
    '../esp_wifi/Kconfig': 'e0b378ffd11ff817b6fbb8009c57bbff2ef6ae789fcae3f4755b2e4c2a7578af',
}
OUTPUTS = {
    'esp_wpa_main.c': 'esp_supplicant/src/esp_wpa_main.c',
    'sta_info.c': 'src/ap/sta_info.c',
    'eapol_auth_sm.c': 'src/eapol_auth/eapol_auth_sm.c',
    'esp_hostap.c': 'esp_supplicant/src/esp_hostap.c',
    'wps_registrar.c': 'src/wps/wps_registrar.c',
    'esp_hostpad_wps.c': 'esp_supplicant/src/esp_hostpad_wps.c',
    'wps_hostapd.c': 'src/ap/wps_hostapd.c',
    'ieee802_1x.c': 'src/ap/ieee802_1x.c',
    'eap_server_wsc.c': 'src/eap_server/eap_server_wsc.c',
}

CREDENTIAL = '''int hostapd_wps_config_ap(struct hostapd_data *hapd, struct wps_data *wps_data)
{
    struct wps_credential cred = {0};
    struct wps_credential *next = NULL;
    int ret = ESP_ERR_INVALID_ARG;
    if (!hapd || !hapd->conf || !hapd->wpa_auth || !wps_data || !wps_data->wps)
        goto done;
    const struct hostapd_ssid *ssid = &hapd->conf->ssid;
    const struct wpa_auth_config *auth = &hapd->wpa_auth->conf;
    if (!ssid->ssid_len || ssid->ssid_len > sizeof(cred.ssid) || !ssid->wpa_passphrase)
        goto done;
    /* WPS conveys PSK credentials, never reinterpret SAE/OWE/Enterprise as PSK. */
    if (!(auth->wpa_key_mgmt & (WPA_KEY_MGMT_PSK | WPA_KEY_MGMT_PSK_SHA256)))
        goto done;
    if (auth->wpa == WPA_PROTO_WPA) cred.auth_type = WPS_AUTH_WPAPSK;
    else if (auth->wpa == WPA_PROTO_RSN) cred.auth_type = WPS_AUTH_WPA2PSK;
    else if (auth->wpa == (WPA_PROTO_WPA | WPA_PROTO_RSN))
        cred.auth_type = WPS_AUTH_WPAPSK | WPS_AUTH_WPA2PSK;
    else goto done;
    int pairwise = 0;
    if (auth->wpa & WPA_PROTO_WPA) pairwise |= auth->wpa_pairwise;
    if (auth->wpa & WPA_PROTO_RSN) pairwise |= auth->rsn_pairwise;
    if (pairwise & WPA_CIPHER_TKIP) cred.encr_type |= WPS_ENCR_TKIP;
    if (pairwise & WPA_CIPHER_CCMP) cred.encr_type |= WPS_ENCR_AES;
    if (!cred.encr_type) goto done;
    size_t length = os_strnlen(ssid->wpa_passphrase, sizeof(cred.key) + 1);
    if (length < 8 || length > sizeof(cred.key)) goto done;
    if (length == sizeof(cred.key)) {
        for (size_t i = 0; i < length; ++i) {
            unsigned char c = (unsigned char)ssid->wpa_passphrase[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                goto done;
        }
    }
    cred.ssid_len = ssid->ssid_len;
    cred.key_len = length;
    os_memcpy(cred.ssid, ssid->ssid, cred.ssid_len);
    os_memcpy(cred.key, ssid->wpa_passphrase, cred.key_len);
    next = os_malloc(sizeof(*next));
    if (!next) { ret = ESP_ERR_NO_MEM; goto done; }
    os_memcpy(next, &cred, sizeof(*next));
    /* Publish only the fully validated replacement; failure preserves the old
     * credential and context snapshot. Both copies are explicitly owned. */
    bin_clear_free(wps_data->use_cred, sizeof(*wps_data->use_cred));
    wps_data->use_cred = next;
    os_memset(wps_data->wps->ssid, 0, sizeof(wps_data->wps->ssid));
    os_memcpy(wps_data->wps->ssid, cred.ssid, cred.ssid_len);
    wps_data->wps->ssid_len = cred.ssid_len;
    ret = ESP_OK;
done:
    forced_memzero(&cred, sizeof(cred));
    return ret;
}
'''

HOST_RF_BAND = '''static int esp32qjs_wps_ap_rf_band_read(int *rf_band)
{
    wifi_band_t band;
    *rf_band = 0;
    int ret = esp_wifi_get_band(&band);
    if (ret != ESP_OK) return ret;
    if (band == WIFI_BAND_2G) *rf_band = WPS_RF_24GHZ;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    else if (band == WIFI_BAND_5G) *rf_band = WPS_RF_50GHZ;
#endif
    else return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

static int hostapd_wps_rf_band_cb(void *ctx)
{
    (void)ctx;
    int rf_band;
    /* APSTA may change band after registrar admission. Query the live band
     * for each protocol message, never substitute a cached 2.4 GHz default.
     * The checked M2/M2D builders reject zero before allocating a message. */
    return esp32qjs_wps_ap_rf_band_read(&rf_band) == ESP_OK ? rf_band : 0;
}
'''

HOST_INIT = '''int hostapd_init_wps(struct hostapd_data *hapd, struct wps_data *wps_data, struct wps_context *wps)
{
    struct wps_registrar_config cfg = {0};
    int ret;
    int rf_band;
    int identity_registered = 0;
    if (!hapd || !hapd->conf || !wps || !wps_data || wps_data->wps != wps ||
        hapd->wps || hapd->eapol_auth || wps->registrar)
        return ESP_ERR_INVALID_STATE;
    /* These are the only server methods in this fixed SDK's registrar build.
     * Do not unregister a pre-existing registration belonging to another owner. */
    if (eap_server_get_eap_method(EAP_VENDOR_IETF, EAP_TYPE_IDENTITY) ||
        eap_server_get_eap_method(EAP_VENDOR_WFA, EAP_VENDOR_TYPE_WSC))
        return ESP_ERR_INVALID_STATE;
    ret = esp32qjs_wps_ap_rf_band_read(&rf_band);
    if (ret != ESP_OK) return ret;
    ret = hostapd_wps_config_ap(hapd, wps_data);
    if (ret != ESP_OK) return ret;
    wps->dev.rf_bands = rf_band;
    wps->event_cb = hostapd_wps_event_cb;
    wps->rf_band_cb = hostapd_wps_rf_band_cb;
    wps->cb_ctx = hapd;
    wps->wps_state = WPS_STATE_CONFIGURED;
    wps->ap_setup_locked = 0;
    wps->ap = 1;
    cfg.set_ie_cb = hostapd_wps_set_ie_cb;
    cfg.reg_success_cb = hostapd_wps_reg_success_cb;
    cfg.cb_ctx = hapd;
    cfg.disable_auto_conf = 1;
    wps->registrar = wps_registrar_init(wps, &cfg);
    if (!wps->registrar) { ret = ESP_FAIL; goto fail; }
    ret = eap_server_identity_register();
    if (ret < 0) goto fail;
    identity_registered = 1;
    ret = eap_server_wsc_register();
    if (ret < 0) goto fail;
    /* EAPOL init may inspect hapd->wps; no callback can run on this same native
     * task before it returns. Publish temporarily, then revoke on failure. */
    hapd->wps = wps;
    ret = ieee802_1x_init(hapd);
    if (ret < 0) { hapd->wps = NULL; goto fail; }
    return ESP_OK;
fail:
    if (identity_registered) eap_server_unregister_methods();
    wps_registrar_deinit(wps->registrar);
    wps->registrar = NULL;
    hostapd_wps_clear_ies(hapd, 1);
    /* The caller owns wps_data, its use_cred and wps. Do not free its context
     * here: wifi_ap_wps_init must unwind them in dependency order. */
    return ret;
}
'''

UPDATE = '''void hostapd_update_wps(struct hostapd_data *hapd)
{
    if (!hapd || !hapd->wps) return;
    struct wps_sm *sm = wps_sm_get();
    if (!sm || !sm->wps || sm->wps_ctx != hapd->wps) return;
    if (hostapd_wps_config_ap(hapd, sm->wps) != ESP_OK) {
        wpa_printf(MSG_ERROR, "WPS: AP credential update rejected");
        return;
    }
    wps_registrar_update_ie(hapd->wps->registrar);
}
'''

EAP_INIT = '''static void * eap_wsc_init(struct eap_sm *sm)
{
    struct wps_sm *owner = wps_sm_get();
    struct wps_config cfg = {0};
    struct eap_wsc_data *data = NULL;
    /* This is the AP registrar. An external registrar cannot use this lane
     * to reconfigure the AP. Bind to the context captured by this EAP server,
     * not whichever global WPS instance happens to exist at callback time. */
    if (!sm || !sm->cfg || !sm->identity ||
        sm->identity_len != WSC_ID_ENROLLEE_LEN ||
        os_memcmp(sm->identity, WSC_ID_ENROLLEE, WSC_ID_ENROLLEE_LEN) ||
        wps_get_owner() != WPS_OWNER_REGISTRAR ||
        wps_get_status() != WPS_STATUS_PENDING ||
        !owner || !owner->wps || !owner->wps_ctx ||
        sm->cfg->wps != owner->wps_ctx ||
        owner->wps->wps != owner->wps_ctx || !owner->wps_ctx->registrar ||
        !owner->wps->use_cred)
        return NULL;
    struct hostapd_data *hapd = hostapd_get_hapd_data();
    if (!hapd || hapd->wps != owner->wps_ctx) return NULL;
    data = os_zalloc(sizeof(*data));
    if (!data) return NULL;
    cfg.registrar = 1;
    cfg.wps = owner->wps_ctx;
    cfg.pbc = owner->wps->pbc;
    cfg.dev_pw_id = owner->wps->dev_pw_id;
    /* PIN selection/locking is performed by the registrar when M1 arrives.
     * Never share transcript, nonces, DH keys or PIN-lock flags across peers. */
    data->wps = wps_init(&cfg);
    forced_memzero(&cfg, sizeof(cfg));
    if (!data->wps) goto fail;
    data->wps->use_cred = os_memdup(owner->wps->use_cred,
                                  sizeof(*owner->wps->use_cred));
    if (!data->wps->use_cred) goto fail;
    os_memcpy(data->wps->peer_dev.mac_addr, sm->peer_addr, ETH_ALEN);
    os_memcpy(data->wps->mac_addr_e, sm->peer_addr, ETH_ALEN);
    data->state = START;
    data->registrar = 1;
    data->fragment_size = WSC_FRAGMENT_SIZE;
    return data;
fail:
    if (data->wps) wps_deinit(data->wps);
    bin_clear_free(data, sizeof(*data));
    return NULL;
}
'''

EAP_RESET = '''static void eap_wsc_reset(struct eap_sm *sm, void *priv)
{
    struct eap_wsc_data *data = priv;
    if (!data) return;
    if (sm && sm->eap_method_priv == data) sm->eap_method_priv = NULL;
    wpabuf_clear_free(data->in_buf);
    wpabuf_clear_free(data->out_buf);
    /* The parent registrar remains live until all peer EAP machines retire.
     * Reset is also called inside EAP steps: never disable/free the parent or
     * the calling EAP machine from this method destructor. */
    if (data->wps) wps_deinit(data->wps);
    bin_clear_free(data, sizeof(*data));
}
'''

EAPOL_DEINIT = '''/* The fixed SDK's ieee802_1x_init owns this eap_cfg allocation;
 * eapol_auth_init only borrows it. Call after all per-station EAP machines. */
void esp32qjs_wps_eapol_deinit(struct hostapd_data *hapd)
{
    if (!hapd || !hapd->eapol_auth) return;
    struct eapol_authenticator *auth = hapd->eapol_auth;
    const struct eap_config *cfg = auth->conf.eap_cfg;
    hapd->eapol_auth = NULL;
    eapol_auth_deinit(auth);
    bin_clear_free((void *)cfg, sizeof(*cfg));
}
'''

HOST_DEINIT = '''void hostapd_deinit_wps(struct hostapd_data *hapd)
{
    if (!hapd) return;
    eloop_cancel_timeout(hostapd_wps_reenable_ap_pin, hapd, NULL);
    eloop_cancel_timeout(hostapd_wps_ap_pin_timeout, hapd, NULL);
    struct wps_context *wps = hapd->wps;
    if (!wps) {
        hostapd_wps_clear_ies(hapd, 1);
        return;
    }
    /* Revoke new peer admission first. Existing peers retain their context
     * through cfg until their destructors have released PIN locks and keys. */
    hapd->wps = NULL;
#ifdef ESP_SUPPLICANT
    ap_for_each_sta(hapd, ap_sta_server_sm_deinit, NULL);
#endif
    esp32qjs_wps_eapol_deinit(hapd);
    wps_registrar_deinit(wps->registrar);
    wps->registrar = NULL;
    eap_server_unregister_methods();
    hostapd_wps_clear_ies(hapd, 1);
}
'''


def patch_shared(text: str) -> str:
    old = function(text, 'wps_deinit')
    new = replace(old, '\tdh5_free(data->dh_ctx);', '''#ifdef CONFIG_WPS_REGISTRAR
    bin_clear_free(data->use_cred, sizeof(*data->use_cred));
    data->use_cred = NULL;
#endif
\tdh5_free(data->dh_ctx);''')
    return replace(text, old, new)


def patch_source(relative: str, source: bytes) -> bytes:
    if relative not in OUTPUTS.values() or hashlib.sha256(source).hexdigest() != REVIEWED[relative]:
        raise ValueError('Unreviewed SDK WPS registrar source: ' + relative)
    text=source.decode()
    if relative == 'esp_supplicant/src/esp_wpa_main.c':
        return patch_ap_input(patch_eap_control(relative, source).decode()).encode()
    if relative == 'src/eapol_auth/eapol_auth_sm.c':
        return patch_eapol_retire(relative, patch_eapol(relative, text)).encode()
    if relative == 'esp_supplicant/src/esp_hostap.c':
        return patch_peer_delays(relative, patch_eapol(relative, patch_ap_close(relative, text))).encode()
    if relative == 'src/wps/wps_registrar.c':
        text = patch_rf_builders(text, ('wps_build_m2', 'wps_build_m2d'))
        return patch_registrar_timers(text).encode()
    if relative.startswith('src/ap/'):
        for header in ('hostapd.h','ap_config.h','wpa_auth.h','wpa_auth_i.h','sta_info.h','wps_hostapd.h','ieee802_1x.h'):
            text=text.replace(f'#include "{header}"', f'#include "ap/{header}"')
    if relative == 'src/ap/sta_info.c':
        return patch_peer_delays(relative, text).encode()
    if relative == 'src/ap/wps_hostapd.c':
        text=replace(text, '#include "esp_wps_i.h"', '#include "esp_wps_i.h"\n#include "esp_wifi.h"')
        text=replace(text,function(text,'hostapd_wps_rf_band_cb'),HOST_RF_BAND)
        text=replace(text, '#include "eap_server/eap_methods.h"', '#include "eap_server/eap_methods.h"\n#include "eap_common/eap_wsc_common.h"')
        text=replace(text,function(text,'hostapd_free_wps'),'')
        text=replace(text,function(text,'hostapd_wps_config_ap'),CREDENTIAL)
        text=replace(text,function(text,'hostapd_init_wps'),HOST_INIT)
        text=replace(text,function(text,'hostapd_update_wps'),UPDATE)
        text=replace(text,function(text,'hostapd_deinit_wps'),
                     'void esp32qjs_wps_eapol_deinit(struct hostapd_data *hapd);\n\n' + HOST_DEINIT)
        return patch_ap_commands(relative, patch_ap_init_cleanup(relative, patch_eapol_retire(relative, patch_ap_close(relative, patch_ap_result(relative, text))))).encode()
    if relative == 'src/ap/ieee802_1x.c':
        body=function(text,'ieee802_1x_init')
        fixed=replace(body,'\tif (!hapd->eapol_auth)\n\t\treturn -1;', '''\tif (!hapd->eapol_auth) {
        /* eapol_auth_conf_clone borrows eap_cfg; a failed initializer never
         * adopts or frees this allocation. */
        bin_clear_free(eap_cfg, sizeof(*eap_cfg));
        return -1;
    }''')
        fixed=replace(fixed,'\tconf.eap_cfg = eap_cfg;', '''#ifdef CONFIG_WPS_REGISTRAR
    eap_cfg->wps = hapd->wps;
#endif
\tconf.eap_cfg = eap_cfg;''')
        text=replace(text,body,fixed + '\n' + EAPOL_DEINIT)
        text=replace(text,'\tint flags = 0;','''\tint flags = 0;
    if (!hapd || !sta || !hapd->eapol_auth) return NULL;''')
        return patch_eapol_retire(relative, patch_eapol(relative, text)).encode()
    if relative == 'src/eap_server/eap_server_wsc.c':
        for header in ('eap_i.h','eap_methods.h'):
            text=text.replace(f'#include "{header}"', f'#include "eap_server/{header}"')
        text=replace(text,'#include "wps/wps.h"', '#include "wps/wps.h"\n#include "wps/wps_i.h"\n#include "ap/hostapd.h"')
        start=text.index('static void * eap_wsc_init(')
        end=text.index('\n}\n', start)+3
        text=replace(text,text[start:end],EAP_INIT)
        return replace(text,function(text,'eap_wsc_reset'),EAP_RESET).encode()
    body=function(text,'wifi_ap_wps_init')
    fixed=replace(body,'    struct wps_sm *sm = NULL;','    int ret = ESP_FAIL;\n    struct wps_sm *sm = NULL;')
    fixed=replace(fixed,'    if (!hapd || gWpsSm) {','    if (!config || !hapd || gWpsSm) {')
    fixed=replace(fixed,'    if (!gWpsSm) {\n        goto _out;','    if (!gWpsSm) {\n        ret = ESP_ERR_NO_MEM;\n        goto _out;')
    fixed=replace(fixed,'    esp_wifi_get_macaddr_internal(WIFI_IF_AP, mac);','    ret = esp_wifi_get_macaddr_internal(WIFI_IF_AP, mac);\n    if (ret != ESP_OK) goto _err;')
    fixed=replace(fixed,'    if (!sm->wps_ctx) {\n        goto _err;','    if (!sm->wps_ctx) {\n        ret = ESP_ERR_NO_MEM;\n        goto _err;')
    fixed=replace(fixed,'    if (wps_dev_init() != 0) {','    ret = wps_dev_init();\n    if (ret != ESP_OK) {')
    fixed=replace(fixed,'    if (wps_init_cfg_pin(&cfg) < 0) {','    ret = wps_init_cfg_pin(&cfg);\n    if (ret < 0) {')
    fixed=replace(fixed,'    if ((sm->wps = wps_init(&cfg)) == NULL) {         /* alloc wps_data */','    if ((sm->wps = wps_init(&cfg)) == NULL) {         /* alloc wps_data */\n        ret = ESP_ERR_NO_MEM;')
    fixed=replace(fixed,'    hostapd_init_wps(hapd, sm->wps, sm->wps_ctx);','    ret = hostapd_init_wps(hapd, sm->wps, sm->wps_ctx);\n    if (ret != ESP_OK) goto _err;')
    fixed=replace(fixed,'    return ESP_OK;','    forced_memzero(&cfg, sizeof(cfg));\n    return ESP_OK;')
    fixed=replace(fixed,'        esp_event_post(WIFI_EVENT, WIFI_EVENT_AP_WPS_RG_PIN, &evt, sizeof(evt), OS_BLOCK);','        esp_event_post(WIFI_EVENT, WIFI_EVENT_AP_WPS_RG_PIN, &evt, sizeof(evt), OS_BLOCK);\n        forced_memzero(&evt, sizeof(evt));')
    old='''    if (sm->wps) {
        wps_deinit(sm->wps);
        sm->wps = NULL;
    }
'''
    fixed=replace(fixed,old,'')
    fixed=replace(fixed,'_err:\n','''_err:
    if (sm->wps) {
        /* hostapd_init_wps has already retired its partial registrar. */
        sm->wps->registrar = 0;
        wps_deinit(sm->wps);
        sm->wps = NULL;
    }
''')
    fixed=fixed.replace('os_free(sm->wps_ctx);','bin_clear_free(sm->wps_ctx, sizeof(*sm->wps_ctx));').replace('os_free(gWpsSm);','bin_clear_free(gWpsSm, sizeof(*gWpsSm));')
    fixed=replace(fixed,'    return ESP_FAIL;\n_out:\n    return ESP_FAIL;','_out:\n    forced_memzero(&cfg, sizeof(cfg));\n    return ret;')
    text=replace(text,body,fixed)
    body=function(text,'wifi_ap_wps_enable_internal')
    fixed=replace(body,'    enum wps_owner owner;','    enum wps_owner owner;\n    int ret;')
    fixed=replace(fixed,'    if (esp_wifi_get_mode(&mode) != ESP_OK) {','    ret = esp_wifi_get_mode(&mode);\n    if (ret != ESP_OK) {')
    fixed=replace(fixed,'        return ESP_FAIL;','        return ret;', 2)
    fixed=replace(fixed,'    if (config->wps_type == WPS_TYPE_DISABLE) {','    if (!config || (config->wps_type != WPS_TYPE_PBC && config->wps_type != WPS_TYPE_PIN)) {')
    fixed=replace(fixed,'    if (wifi_ap_wps_init(config) != ESP_OK) {','    ret = wifi_ap_wps_init(config);\n    if (ret != ESP_OK) {')
    # Preserve error from each failed initialization step, not an uninitialized ret.
    for call in ('wps_set_factory_info(config)','wps_set_type(config->wps_type)','wps_set_status(WPS_STATUS_DISABLE)'):
        fixed=fixed.replace(f'    if ({call} != ESP_OK) {{',f'    ret = {call};\n    if (ret != ESP_OK) {{')
    fixed=replace(fixed,'    return ESP_FAIL;\n}', '    return ret;\n}')
    text=replace(text,body,fixed)
    body=function(text,'wifi_ap_wps_deinit')
    fixed=body.replace('os_free(sm->wps_ctx);','bin_clear_free(sm->wps_ctx, sizeof(*sm->wps_ctx));').replace('os_free(gWpsSm);','bin_clear_free(gWpsSm, sizeof(*gWpsSm));')
    text=replace(text,body,fixed)
    body=function(text,'wifi_ap_wps_start_internal')
    fixed=replace(body,'    enum wps_owner owner;','    enum wps_owner owner;\n    int ret;')
    fixed=replace(fixed,'    esp_wifi_get_mode(&mode);',
                  '    ret = esp_wifi_get_mode(&mode);\n    if (ret != ESP_OK) return ret;')
    fixed=replace(fixed,'    if (!pin) {',
                  '    if (!gWpsSm || !gWpsSm->wps) return ESP_ERR_WIFI_WPS_SM;\n    if (!pin) {')
    for call in ('wps_set_status(WPS_STATUS_PENDING)',
                 'hostapd_wps_button_pushed(hostapd_get_hapd_data(), NULL)',
                 'hostapd_wps_add_pin(hostapd_get_hapd_data(), pin)'):
        indent='        ' if call.startswith('hostapd_') else '    '
        fixed=replace(fixed,indent + f'if ({call} != ESP_OK) {{',
                      indent + f'ret = {call};\n' + indent + 'if (ret != ESP_OK) {')
    fixed=replace(fixed,'return ESP_FAIL;', 'return ret;', 2)
    text=replace(text,body,fixed)
    return patch_ap_commands(relative, patch_ap_init_cleanup(relative, patch_eapol_retire(relative, patch_ap_close(relative, patch_ap_result(relative, text))))).encode()
