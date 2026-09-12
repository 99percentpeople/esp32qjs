"""Add a retained WPS result owner to the fixed-SDK credential corrections.

Build-local only. Native retirement/IPC/Radio/public Session remain separate.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import re
from patch_idf_wps import OUTPUTS, REVIEWED as CREDENTIAL_REVIEWED, function, replace
from patch_idf_wps import patch_source as credential_patch
from patch_idf_wps_errors import patch_errors
from patch_idf_wps_registrar import REVIEWED as REGISTRAR_REVIEWED, OUTPUTS as REGISTRAR_OUTPUTS
from patch_idf_wps_registrar import patch_source as registrar_patch, patch_shared

OUTPUTS = dict(OUTPUTS, **REGISTRAR_OUTPUTS)

REVIEWED = dict(CREDENTIAL_REVIEWED, **{
    'esp_supplicant/src/esp_dpp_i.h': '91eaba7b7567082ef893ffa2afb3e646cee8d1a3e927763e8876300303ea2f47',
    'src/wps/wps_attr_build.c': 'e4456a7043ae9f82ed6eb69ac85a2a570c242938911757a78ca6f29883dc7eb9',
    'src/wps/wps_common.c': '79e60233efdad8297bc8b7ff81d6a6a9bb176028a239ae42c203df29e4a5815a',
    '../esp_timer/src/ets_timer_legacy.c': 'fb28b0f5b2d15950bde406cd327992678c52300cef39e5f9462259f2fd2d083f',
    '../esp_timer/src/esp_timer.c': '5063132e2fa243d69b85b9bc1c93bb19d79110b265da07b5e6a4b779f66c563e',
    '../esp_timer/include/esp_timer.h': '1a74ad679fdb9ad2ba78b5c4a30ced83fa87d1ac9cc77162f5687b2039ea742b',
    '../esp_wifi/include/esp_private/wifi_os_adapter.h': '08fb73f76da7e6800c42dbff2fb724dff5205e1c8262e969da0f391be21f1eae',
    'esp_supplicant/src/esp_wpas_glue.c': 'e672d977d7a3106181892feacfd38d3ee9a9d6d915498b173c569e6e825d6c06',
    'src/rsn_supp/wpa.c': '8b8861f384bda3c2bb05e8f3d9d3d284d59e4533fa496137973c38b2185b62cf',
    'src/wps/wps_dev_attr.c': '906180a758158a4696075c54987548162c5c201f874d90a574f4191ed760d1da',
    '../esp_wifi/include/esp_private/wifi.h': '6781965f297c9163dddefca2137748d33e7c820786d7e2a54a212c039218d195',
    '../esp_wifi/lib/esp32c3/libnet80211.a': '0fcbed322d3254063b2c191bc8b8004e703ffd640296164a6552676180909a0c',
    '../esp_wifi/lib/esp32s3/libnet80211.a': '265e5c89ac0d2a9444b5b466afee49f631ec549f96774f91a721ab5069c58e20',
    '../esp_wifi/lib/esp32c5/libnet80211.a': '4c86fc1d2f40a933af7f672972978eb7c82651d838fd10584e006438d585793b',
    'port/eloop.c': '2decdfef8e932070a791d7fd4dfa85251fedbfd6ad06f5a0da22f4b1c6eab9ac',
    'src/utils/eloop.h': '6ae5824fffc1afd3ab8afae0c9a6f948543828cec0b2ce323dee0fb88c8d6033',
    'esp_supplicant/src/esp_wifi_driver.h': 'b11d232c5f41a83ecaa19691e8c081e42530da3cea70a5f87462a8ced7b688cb',
    '../esp_wifi/include/esp_wifi_types_generic.h': '8e398a8d22b18c199ea6d48e42784e8a897c22000a09ee9ac7d22cf0f5110e5c',
})
REVIEWED.update(REGISTRAR_REVIEWED)
ROOT = Path(__file__).resolve().parents[1]
NATIVE_HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_wps_sdk.h'
NATIVE_SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_sdk.inc'
AP_RESULT_HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_wps_ap_result.h'
AP_RESULT_SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_result.inc'
AP_NATIVE_HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_wps_ap_sdk.h'
AP_NATIVE_SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_sdk.inc'
TIMERS = ('wifi_station_wps_success', 'wifi_station_wps_timeout', 'wifi_station_wps_msg_timeout',
          'wifi_wps_scan', 'wifi_station_wps_eapol_start_handle', 'wifi_station_wps_post_m8_timeout')


def guard(text: str, name: str, statement: str) -> str:
    body = function(text, name)
    return replace(text, body, body.replace('\n{\n', '\n{\n    ' + statement + '\n', 1))


def track_callback(text: str, name: str, arguments: str, failure: str = '') -> str:
    """Keep the driver's original callback name; count every body exit."""
    body = function(text, name)
    signature = body[:body.index('\n{')]
    implementation = body.replace(name + '(', name + '_body(', 1)
    if not implementation.startswith('static '):
        implementation = 'static ' + implementation
    call = name + '_body(' + arguments + ')'
    wrapper = signature + '\n{\n'
    wrapper += '    if (!esp32qjs_wps_callback_enter()) return' + (' ' + failure if failure else '') + ';\n'
    wrapper += ('    int result = ' if failure else '    ') + call + ';\n'
    wrapper += '    esp32qjs_wps_callback_leave();\n'
    if failure:
        wrapper += '    return result;\n'
    wrapper += '}\n'
    return replace(text, body, implementation + '\n' + wrapper)


def patch_source(relative: str, source: bytes) -> bytes:
    if relative in REGISTRAR_OUTPUTS.values():
        return registrar_patch(relative, source)
    text = credential_patch(relative, source).decode()
    if relative == "src/wps/wps.c":
        text = patch_shared(text)
    if relative != 'esp_supplicant/src/esp_wps.c':
        return text.encode()
    text = replace(text, 'static wps_factory_information_t *s_factory_info = NULL;',
                   'static wps_factory_information_t *s_factory_info = NULL;\n\n'
                   '#include "esp32qjs_wps_native.inc"')
    text = guard(text, 'wifi_wps_enable_internal', '''
#ifdef CONFIG_WPS_REGISTRAR
    if (esp32qjs_wps_ap_result_held()) return ESP_ERR_INVALID_STATE;
#endif''')
    for name in TIMERS:
        text = guard(text, name, 'if (!esp32qjs_wps_timer_exact(data, user_ctx)) return;')
    # Native timer args contain the entire 64-bit ID, never a freed pointer.
    pattern = r'eloop_register_timeout\(([^;\n]+), (' + '|'.join(TIMERS) + r'), NULL, NULL\)'
    text, count = re.subn(pattern, r'esp32qjs_wps_register_timeout(\1, \2)', text)
    if count != 10:
        raise ValueError('Expected ten WPS timer registration sites')
    for name in TIMERS:
        text = text.replace(f'eloop_cancel_timeout({name}, NULL, NULL)',
                            f'eloop_cancel_timeout({name}, ELOOP_ALL_CTX, ELOOP_ALL_CTX)')
    text = replace(text, 'wifi_wps_scan(NULL, NULL);',
                   'wifi_wps_scan(esp32qjs_wps_timer_high(), esp32qjs_wps_timer_low());')

    for name, result in (('wps_parse_scan_result', 'false'), ('wps_sm_rx_eapol', 'ESP_FAIL'),
                         ('wps_start_pending', 'ESP_FAIL'),
                         ('wifi_wps_scan_done', ''), ('wps_sm_notify_deauth', '')):
        text = guard(text, name, 'if (s_wps_native && (s_wps_native->status.closing || '
                     's_wps_native->status.terminal_seen)) return' + (' ' + result if result else '') + ';')

    text = guard(text, 'wifi_wps_enable_internal',
                 'if (s_wps_native && ctx != s_wps_native) return ESP_ERR_INVALID_STATE;')
    text = guard(text, 'wifi_station_wps_start',
                 'if (s_wps_native && data != s_wps_native) return ESP_ERR_INVALID_STATE;')
    for name in ('esp_wifi_wps_enable', 'esp_wifi_wps_start', 'esp_wifi_wps_disable'):
        text = guard(text, name, 'if (esp32qjs_wps_native_held()) return ESP_ERR_INVALID_STATE;')
    text = replace(text, '            wpa_printf(MSG_DEBUG, "WPS: Already enabled");',
                   '            wpa_printf(MSG_DEBUG, "WPS: Already enabled");\n'
                   '            if (esp32qjs_wps_native_held()) ret = ESP_ERR_INVALID_STATE;')

    # Do not mutate type/owner on the API caller thread before native admission.
    old = function(text, 'esp_wifi_wps_disable')
    new = old.replace('    int prev_wps_type;\n', '')
    new = replace(new, '''    prev_wps_type = wps_get_type();
    wps_set_type(WPS_TYPE_DISABLE); /* Notify WiFi task */
    wps_set_owner(WPS_OWNER_NONE);

    ret = eloop_register_timeout_blocking(wifi_wps_disable_internal, NULL, NULL);''',
                  '    ret = eloop_register_timeout_blocking(esp32qjs_wps_unmanaged_disable, NULL, NULL);')
    new = replace(new, '''        wps_set_type(prev_wps_type);
        wps_set_owner(prev_owner);
''', '')
    text = replace(text, old, new)

    text = guard(text, 'wps_send_event_and_disable',
                 'if (s_wps_native) return esp32qjs_wps_native_failure(event_id, event_data, data_len);')
    text = guard(text, 'wps_handle_failure',
                 'if (s_wps_native) return esp32qjs_wps_native_failure('
                 'WIFI_EVENT_STA_WPS_ER_FAILED, &reason_code, sizeof(reason_code));')
    text = guard(text, 'wifi_wps_disable',
                 'if (s_wps_native) return esp32qjs_wps_native_failure('
                 'WIFI_EVENT_STA_WPS_ER_FAILED, NULL, 0);')
    text = replace(text, '        sm->state = WPA_FINISH_PROCESS;',
                   '        sm->state = WPA_FINISH_PROCESS;\n'
                   '        if (s_wps_native) return esp32qjs_wps_native_finish(sm);')
    text = replace(text, '        esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_WPS_ER_PIN, &evt, sizeof(evt), OS_BLOCK);',
                   '        if (!esp32qjs_wps_native_pin(evt.pin_code, sizeof(evt.pin_code)))\n'
                   '            esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_WPS_ER_PIN, &evt, sizeof(evt), OS_BLOCK);')
    text = replace(text, '''        wpa_printf(MSG_INFO, "WPS: connecting to %s, bssid=" MACSTR,
                   (char *)sm->creds[0].ssid, MAC2STR(wifi_config.sta.bssid));''',
                   '        wpa_printf(MSG_INFO, "WPS: connecting for negotiation");')
    text = guard(text, 'wifi_wps_disable_internal',
                 'if (s_wps_native) return esp32qjs_wps_native_stop_sdk();')
    # Managed init failure must leave adopted storage alive until inputs have
    # been stopped. The local callback table is not adopted on registration
    # failure (reviewed native-task ioctl path).
    old = function(text, 'wifi_station_wps_init')
    new = replace(old, '_err:\n', '''_err:
    if (s_wps_native) {
        os_free(wps_cb);
        forced_memzero(&cfg, sizeof(cfg));
        return ESP_FAIL;
    }
''')
    text = replace(text, old, new)
    old = function(text, 'wifi_station_wps_deinit')
    new = replace(old, '\n{\n', '''
{
    if (s_wps_native && (!s_wps_native->status.inputs_stopped ||
        s_wps_native->status.callback_depth || s_wps_native->status.tracking_fault))
        return ESP_ERR_INVALID_STATE;
''')
    new = replace(new, '''    esp_wifi_unset_appie_internal(WIFI_APPIE_WPS_PR);
    esp_wifi_unset_appie_internal(WIFI_APPIE_WPS_AR);
    esp_wifi_set_wps_cb_internal(NULL);''', '''    if (!s_wps_native) {
        esp_wifi_unset_appie_internal(WIFI_APPIE_WPS_PR);
        esp_wifi_unset_appie_internal(WIFI_APPIE_WPS_AR);
        esp_wifi_set_wps_cb_internal(NULL);
    }''')
    new = replace(new, 'wpabuf_free(sm->wsc_frag.in_buf);', 'wpabuf_clear_free(sm->wsc_frag.in_buf);')
    text = replace(text, old, new)
    text = patch_errors(text)
    # Station commands may already be queued when an AP reservation is made.
    # Reject before touching the shared type/status/SM, including direct SDK
    # destructor paths. Ordinary API caller-thread owner checks are too early.
    for name in ('wifi_station_wps_start', 'wifi_wps_disable_internal', 'wifi_station_wps_deinit'):
        text = guard(text, name, '''
#ifdef CONFIG_WPS_REGISTRAR
    if (esp32qjs_wps_ap_result_held()) return ESP_ERR_INVALID_STATE;
#endif''')
    for name in TIMERS:
        text = track_callback(text, name, 'data, user_ctx')
    for name, arguments, failure in (
        ('wps_parse_scan_result', 'scan', 'false'),
        ('wps_sm_rx_eapol', 'src_addr, buf, len', 'ESP_FAIL'),
        ('wps_start_pending', '', 'ESP_FAIL'),
        ('wifi_wps_scan_done', 'arg, status', ''),
        ('wps_sm_notify_deauth', '', ''),
        ('wifi_wps_disable', '', 'ESP_FAIL'),
    ):
        text = track_callback(text, name, arguments, failure)
    # Without a cookie scan/TX callbacks still need a proven drain before reuse.
    # Exact record release is admitted only by the worker's ordered drain.
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
                raise ValueError('Unreviewed SDK WPS native dependency: ' + relative)
        outputs = {name: patch_source(relative, (args.component / relative).read_bytes())
                   for name, relative in OUTPUTS.items()}
        outputs['esp32_mquickjs_wifi_wps_sdk.h'] = NATIVE_HEADER.read_bytes()
        outputs['esp32qjs_wps_native.inc'] = NATIVE_SOURCE.read_bytes()
        outputs['esp32_mquickjs_wifi_wps_ap_result.h'] = AP_RESULT_HEADER.read_bytes()
        outputs['esp32qjs_wps_ap_result.inc'] = AP_RESULT_SOURCE.read_bytes()
        outputs['esp32_mquickjs_wifi_wps_ap_sdk.h'] = AP_NATIVE_HEADER.read_bytes()
        outputs['esp32qjs_wps_ap_native.inc'] = AP_NATIVE_SOURCE.read_bytes()
    except ValueError as error:
        parser.error(str(error))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, source in outputs.items():
        output = args.output_dir / name
        if not output.exists() or output.read_bytes() != source:
            output.write_bytes(source)
        print('ESP32QJS WPS native result: build-local ' + name + ' ' + hashlib.sha256(source).hexdigest())


if __name__ == '__main__':
    main()
