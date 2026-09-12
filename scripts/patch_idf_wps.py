"""Bound WPS credential copies and scrub fixed-SDK credential storage.

Only build-local sources are written. This does not establish WPS operation
identity, event retirement, Radio ownership, or a public provisioning API.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import re

REVIEWED = {
    'esp_supplicant/src/esp_wps.c': 'db8f67e374c170619dafe0265c98eabdf57fc75949260adf9ec7e90021fc3962',
    'esp_supplicant/src/esp_wps_i.h': '05d8b856332d9076a347e044c1b558958423ea63290546f4fd84710ff5b2d398',
    'esp_supplicant/include/esp_wps.h': '49fc70afc627383a359024b1085c6ede2a3857672e5cda549fc91b50f06db91d',
    'src/wps/wps.c': '86dd45a975873e6c26987bf76e9f9ad618bf101438ed8793f2ebd780b9b53d14',
    'src/wps/wps.h': 'e072d60051ff1a08a47bf37f5fe5713bba2f4a484a9214bc3c80a6eaa7e9b371',
    'src/wps/wps_i.h': '3cb4059dbcf1a8f31c6d3ec1343520d402fc815fe28a4ad326cbfd5343f3f8b2',
    'src/wps/wps_enrollee.c': '0ffedf747ee9c3c6a59535c24d4239d8349dba6dd73eccc8b246571af724693e',
    'src/utils/common.c': 'b99ef519c251d958c937fbf67f6df39721ee2f1e2cc9ed008300f59b6050961f',
    'src/utils/wpabuf.c': 'a0e993857735bf33e6c9fada3b048dab797d7804da409ffb73623d890d803a24',
    'port/include/os.h': '0d79eb5a6f84354317b9b871500ca4869b2056797cec2b16867da009ffb43665',
    'CMakeLists.txt': 'a9568d989bbc5bfb2a2efe3500493b5b6fe9f88fb46e0869e192b7b1a8315217',
}
OUTPUTS = {
    'esp_wps.c': 'esp_supplicant/src/esp_wps.c',
    'wps.c': 'src/wps/wps.c',
    'wps_enrollee.c': 'src/wps/wps_enrollee.c',
}


def replace(text: str, old: str, new: str, count: int = 1) -> str:
    if text.count(old) != count:
        raise ValueError('Unexpected WPS source anchor: ' + old)
    return text.replace(old, new)


def function(text: str, name: str) -> str:
    match = re.search(r'^(?:static )?(?:void|int|bool|esp_err_t|struct wpabuf \*) ' + re.escape(name)
                      + r'\([^;{}]*\)\n\{', text, re.M)
    if not match:
        raise ValueError('Missing WPS function: ' + name)
    return text[match.start():text.index('\n}\n', match.start()) + 3]


def patch_rf_builders(text: str, names: tuple[str, ...]) -> str:
    # Zero means an unavailable band, but wps_build_rf_bands treats it as a
    # request to use cached capabilities. Reject it before message work instead.
    for name in names:
        body = function(text, name)
        fixed = replace(body, '\tstruct wpabuf *msg;', '''\tstruct wpabuf *msg;
    int rf_band = wps->wps->rf_band_cb ? wps->wps->rf_band_cb(wps->wps->cb_ctx) : 0;
    if (rf_band <= 0 || (rf_band & ~(WPS_RF_24GHZ | WPS_RF_50GHZ | WPS_RF_60GHZ))) {
        return NULL;
    }''')
        fixed = replace(fixed, 'wps_build_rf_bands(&wps->wps->dev, msg,\n\t\t\t       wps->wps->rf_band_cb(wps->wps->cb_ctx))',
                        'wps_build_rf_bands(&wps->wps->dev, msg, rf_band)')
        text = replace(text, body, fixed)
    return text


STA_RF_BAND = '''static int wps_rf_band_cb(void *ctx)
{
    (void)ctx;
    wifi_band_mode_t band_mode;
    if (esp_wifi_get_band_mode(&band_mode) != ESP_OK) {
        wpa_printf(MSG_ERROR, "WPS: failed to get band mode");
        return 0;
    }
    switch (band_mode) {
    case WIFI_BAND_MODE_2G_ONLY:
        return WPS_RF_24GHZ;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    case WIFI_BAND_MODE_5G_ONLY:
        return WPS_RF_50GHZ;
    case WIFI_BAND_MODE_AUTO:
        return WPS_RF_24GHZ | WPS_RF_50GHZ;
#endif
    default:
        return 0;
    }
}
'''


CREDENTIAL = '''/* SDK parser remains responsible for authentication/encryption policy.
 * Check fixed byte-array bounds before retaining or applying its result. */
static bool esp32qjs_wps_credential_valid(const struct wps_credential *cred)
{
    return cred && cred->ssid_len > 0 &&
        cred->ssid_len <= sizeof(cred->ssid) &&
        cred->key_len <= sizeof(cred->key) &&
        cred->ssid_len <= sizeof(((wifi_config_t *)0)->sta.ssid) &&
        cred->key_len <= sizeof(((wifi_config_t *)0)->sta.password);
}

static int esp32qjs_wps_apply_credential(const struct wps_credential *cred)
{
    if (!esp32qjs_wps_credential_valid(cred)) return ESP_ERR_INVALID_ARG;
    wifi_config_t *config = os_zalloc(sizeof(*config));
    if (!config) return ESP_ERR_NO_MEM;
    int ret = esp_wifi_get_config(WIFI_IF_STA, config);
    if (ret == ESP_OK) {
        /* A shorter replacement must never retain bytes of the old credential.
         * Keep unrelated station/security settings and the SDK's WPS policy. */
        forced_memzero(config->sta.ssid, sizeof(config->sta.ssid));
        forced_memzero(config->sta.password, sizeof(config->sta.password));
        os_memcpy(config->sta.ssid, cred->ssid, cred->ssid_len);
        os_memcpy(config->sta.password, cred->key, cred->key_len);
#ifndef CONFIG_WPS_STRICT
        if (cred->auth_type == WPS_AUTH_WPAPSK && (cred->encr_type & WPS_ENCR_AES)) {
            config->sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
        }
#endif
        config->sta.bssid_set = 0;
        config->sta.sae_pwe_h2e = 0;
        ret = esp_wifi_disconnect();
        if (ret == ESP_OK) ret = esp_wifi_set_config(WIFI_IF_STA, config);
    }
    bin_clear_free(config, sizeof(*config));
    return ret;
}

'''


def patch_source(relative: str, source: bytes) -> bytes:
    if relative not in OUTPUTS.values():
        raise ValueError('Not a WPS patch input: ' + relative)
    if hashlib.sha256(source).hexdigest() != REVIEWED[relative]:
        raise ValueError('Unreviewed SDK WPS source: ' + relative)
    text = source.decode()
    if relative.startswith('src/wps/'):
        # Moving the translation unit must preserve its original local includes.
        for header in ('wps_i.h', 'wps_dev_attr.h'):
            text = replace(text, f'#include "{header}"', f'#include "wps/{header}"')
    if relative == 'src/wps/wps.c':
        # Includes embedded parsed credentials, nonces and derived session keys.
        text = replace(text, 'os_free(data);', 'bin_clear_free(data, sizeof(*data));', 5)
        text = replace(text, 'wpabuf_free(data->last_msg);', 'wpabuf_clear_free(data->last_msg);')
        text = replace(text, '''\t\twpa_hexdump_key(MSG_DEBUG, "WPS: AP PIN dev_password",
\t\t\t\tdata->dev_password, data->dev_password_len);''', '')
        return text.encode()
    if relative == 'src/wps/wps_enrollee.c':
        text = replace(text, 'os_free(cred);', 'bin_clear_free(cred, sizeof(*cred));')
        return patch_rf_builders(text, ('wps_build_m1',)).encode()

    text = replace(text, function(text, 'wps_rf_band_cb'), STA_RF_BAND)
    text = replace(text, 'static int save_credentials_cb(', CREDENTIAL + 'static int save_credentials_cb(')
    # Helper is also used earlier by wps_finish().
    text = replace(text, 'static int wps_finish(void);', 'static int wps_finish(void);\n'
                   'static int esp32qjs_wps_apply_credential(const struct wps_credential *cred);')
    text = replace(text, 'if (!sm || !cred || sm->ap_cred_cnt >= MAX_CRED_COUNT)',
                   'if (!sm || !esp32qjs_wps_credential_valid(cred) || sm->ap_cred_cnt >= MAX_CRED_COUNT)')
    text = replace(text, '    memcpy(creds, cred, sizeof(*creds));', '''    memcpy(creds, cred, sizeof(*creds));
    /* These attributes borrow the parser's input and expire after this callback. */
    creds->cred_attr = NULL;
    creds->cred_attr_len = 0;''')
    text = replace(text, '''    wpa_hexdump_ascii(MSG_DEBUG, "WPS: Received credential - SSID ", cred->ssid, cred->ssid_len);
    wpa_hexdump_ascii_key(MSG_DEBUG, "WPS: Received credential - Key ", cred->key, cred->key_len);''', '')
    old_finish = function(text, 'wps_finish')
    new_finish = old_finish
    start = new_finish.index('        if (sm->ap_cred_cnt == 1) {')
    end = new_finish.index('        /* fill event info */', start)
    new_finish = new_finish[:start] + new_finish[end:]
    new_finish = replace(new_finish, '        sm->state = WPA_FINISH_PROCESS;', '''        /* Preserve the reentrancy guard before disconnect can notify WPS. */
        sm->state = WPA_FINISH_PROCESS;
        /* Do not publish success or cancel the total timer before configuration
         * has succeeded. OOM/driver errors take the existing failure cleanup. */
        if (sm->ap_cred_cnt == 1) {
            ret = esp32qjs_wps_apply_credential(&sm->creds[0]);
            if (ret != ESP_OK) {
                wps_handle_failure(WPS_FAIL_REASON_NORMAL);
                return ret;
            }
        }''')
    text = replace(text, old_finish, new_finish)

    for field, size in (('manufacturer', 'WPS_MAX_MANUFACTURER_LEN'),
                        ('model_name', 'WPS_MAX_MODEL_NAME_LEN'),
                        ('model_number', 'WPS_MAX_MODEL_NAME_LEN'),
                        ('device_name', 'WPS_MAX_DEVICE_NAME_LEN')):
        text = replace(text, f'os_snprintf(dev->{field}, {size}, s_factory_info->{field});',
                       f'os_snprintf(dev->{field}, {size}, "%s", s_factory_info->{field});')
    text = replace(text, 'WPS_MAX_MODEL_NAME_LEN, wps_model_number);',
                   'WPS_MAX_MODEL_NAME_LEN, "%s", wps_model_number);')

    text = replace(text, 'os_free(s_previous_wifi_config);',
                   'bin_clear_free(s_previous_wifi_config, sizeof(*s_previous_wifi_config));', 5)
    text = replace(text, 'wpa_printf(MSG_INFO, "WPS failed, reconnecting to previous AP: %s", (char *)s_previous_wifi_config->sta.ssid);',
                   'wpa_printf(MSG_INFO, "WPS failed, reconnecting to previous AP");')
    text = replace(text, '''wpa_printf(MSG_INFO, "WPS: Stored previous AP to reconnect on failure: %s",
                               (char *)s_previous_wifi_config->sta.ssid);''',
                   'wpa_printf(MSG_INFO, "WPS: Stored previous AP to reconnect on failure");')
    text = replace(text, 'os_free(gWpsSm);', 'bin_clear_free(gWpsSm, sizeof(*gWpsSm));', 2)
    text = replace(text, 'os_free(sm->wps_ctx);', 'bin_clear_free(sm->wps_ctx, sizeof(*sm->wps_ctx));', 2)
    text = replace(text, 'wpabuf_free(sm->wps_ctx->dh_privkey);', 'wpabuf_clear_free(sm->wps_ctx->dh_privkey);', 2)
    text = replace(text, 'wpabuf_free(sm->wps->wps->dh_privkey);', 'wpabuf_clear_free(sm->wps->wps->dh_privkey);')
    # wps_deinit may consult data->wps (the context); keep it alive until then.
    ctx_start = '    if (sm->wps_ctx) {'
    data_cleanup = '''    if (sm->wps) {
        wps_deinit(sm->wps);
        sm->wps = NULL;
    }
'''
    text = replace(text, data_cleanup, '', 2)
    text = replace(text, ctx_start, data_cleanup + ctx_start, 2)
    old_init = function(text, 'wifi_station_wps_init')
    new_init = replace(old_init, '    return ESP_OK;', '    forced_memzero(&cfg, sizeof(cfg));\n    return ESP_OK;')
    new_init = replace(new_init, '    return ESP_FAIL;', '    forced_memzero(&cfg, sizeof(cfg));\n    return ESP_FAIL;', 2)
    new_init = replace(new_init, '        esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_WPS_ER_PIN, &evt, sizeof(evt), OS_BLOCK);',
                       '        esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_WPS_ER_PIN, &evt, sizeof(evt), OS_BLOCK);\n'
                       '        forced_memzero(&evt, sizeof(evt));')
    text = replace(text, old_init, new_init)
    text = replace(text, 'os_memset(&s_wps_success_evt, 0, sizeof(s_wps_success_evt));',
                   'forced_memzero(&s_wps_success_evt, sizeof(s_wps_success_evt));', 2)
    return text.encode()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--component', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.output_dir.resolve().is_relative_to(args.component.resolve()):
        parser.error('output must be outside the shared SDK component')
    try:
        for relative, digest in REVIEWED.items():
            if hashlib.sha256((args.component / relative).read_bytes()).hexdigest() != digest:
                raise ValueError('Unreviewed SDK WPS dependency: ' + relative)
        outputs = {name: patch_source(relative, (args.component / relative).read_bytes())
                   for name, relative in OUTPUTS.items()}
    except ValueError as error:
        parser.error(str(error))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, source in outputs.items():
        output = args.output_dir / name
        if not output.exists() or output.read_bytes() != source:
            output.write_bytes(source)
        print('ESP32QJS WPS credential boundary: reviewed build-local ' + name + ' '
              + hashlib.sha256(source).hexdigest())


if __name__ == '__main__':
    main()
