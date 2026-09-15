"""Prepare the pinned NAN-USD engine inside an immutable Build Context."""
import argparse
from sdk_patches.common.source import function, replace
import hashlib
from pathlib import Path
import re

REVIEWED = {
    'esp_supplicant/src/esp_nan_usd.c': '6503d2942c5aeaf96c75d3ca53f1c6f70e5cf995d80ea4c78d1d6bff69d31c3b',
    'src/common/nan_de.c': 'e91336a41f9bd2f2a31d45c82a6611fbfc97f53483c908d1354140c979a7a094',
    'src/common/nan_de.h': '004505bfe70e809ba891be56e14270ec9813513d153132594ebba0cfe95d19c6',
    'port/eloop.c': '2decdfef8e932070a791d7fd4dfa85251fedbfd6ad06f5a0da22f4b1c6eab9ac',
    'CMakeLists.txt': 'a9568d989bbc5bfb2a2efe3500493b5b6fe9f88fb46e0869e192b7b1a8315217',
}






def patch_engine(source):
    source = replace(source, '#include "esp_nan.h"', '#include "esp_nan.h"\n'
        '#include <stdatomic.h>\n#include <stdint.h>\n'
        '#include "esp32_mquickjs_wifi_nan_usd_sdk.h"\n#include "esp32_mquickjs_wifi_nan_sdk.h"')
    source = replace(source, 'static void *s_nan_usd_data_lock = NULL;',
        'static _Atomic(void *) s_nan_usd_data_lock = NULL;')
    source = replace(source, '#define NAN_USD_DATA_UNLOCK() os_mutex_unlock(s_nan_usd_data_lock)',
        '#define NAN_USD_DATA_UNLOCK() os_mutex_unlock(s_nan_usd_data_lock)\n'
        '#include "esp32_mquickjs_wifi_nan_usd_state.inc"')
    source = function(source, 'esp_nan_usd_deinit', lambda s:
        '#include "esp32_mquickjs_wifi_nan_usd_lifecycle.inc"\n')
    source = function(source, 'esp_nan_usd_init', lambda s: '')
    source = source.replace('if (!g_nan_de)',
        'if (!g_nan_de || s_esp32qjs_usd_closing || s_esp32qjs_usd_error)')
    source = replace(source, 'if (!g_nan_de || !peer_addr)',
        'if (!g_nan_de || s_esp32qjs_usd_closing || s_esp32qjs_usd_error || !peer_addr)')
    # Public native calls can arrive before successful init or during cleanup.
    for name, error in [('esp_nan_usd_publish_internal', '-1'), ('esp_nan_usd_subscribe_internal', '-1'),
                        ('esp_nan_usd_update_publish', 'ESP_ERR_INVALID_STATE'),
                        ('esp_nan_usd_cancel_publish', 'ESP_ERR_INVALID_STATE'),
                        ('esp_nan_usd_cancel_subscribe', 'ESP_ERR_INVALID_STATE'),
                        ('esp_nan_usd_cancel_service', 'ESP_ERR_INVALID_STATE'),
                        ('esp_nan_usd_transmit', 'ESP_ERR_INVALID_STATE')]:
        source = function(source, name, lambda s, error=error: replace(s,
            '    NAN_USD_DATA_LOCK();', '    if (!s_nan_usd_data_lock) return ' + error + ';\n    NAN_USD_DATA_LOCK();'))
    source = function(source, 'esp_nan_de_rx_action', lambda s: replace(s,
        '    if (len < 6)', '    if (!hdr || !payload || len < 6)'))
    # Dispatch only the USD engine's Action/ROC completion. Public global events
    # from unrelated modules must never advance this engine's timers/state.
    source = function(source, 'nan_de_tx_event_handler', lambda s: replace(replace(s,
        '    struct nan_de *nan_de = NULL;', '    if (!event_data) return;\n    struct nan_de *nan_de = NULL;'),
        '        if (evt->status == WIFI_ACTION_TX_DONE)',
        '        if (evt->context != (uint32_t)(uintptr_t)esp_nan_de_rx_action || evt->ifx != WIFI_IF_STA) return;\n'
        '        if (evt->status == WIFI_ACTION_TX_DONE)'))
    source = function(source, 'nan_de_tx_event_handler', lambda s: replace(s,
        '        wifi_event_roc_done_t *evt = (wifi_event_roc_done_t *)event_data;',
        '        wifi_event_roc_done_t *evt = (wifi_event_roc_done_t *)event_data;\n'
        '        if (evt->context != (uint32_t)(uintptr_t)esp_nan_de_rx_action) return;'))
    source = replace(source, 'static void nan_de_tx_event_handler(',
        'int esp_nan_de_rx_action(uint8_t *hdr, uint8_t *payload, size_t len, uint8_t channel);\n\n'
        'static void nan_de_tx_event_handler(')
    for name, limit in [('esp_nan_de_discovery_result', 'ESP_WIFI_MAX_SVC_SSI_LEN'),
                        ('esp_nan_de_replied', 'ESP_WIFI_MAX_SVC_SSI_LEN'),
                        ('esp_nan_de_receive', 'ESP_WIFI_MAX_FUP_SSI_LEN')]:
        source = function(source, name, lambda s, limit=limit: replace(s, '\n{\n',
            '\n{\n    if (!peer_addr || ssi_len > ' + limit + ' || (ssi_len && !ssi)) return;\n'))
    source, count = re.subn(r'esp_event_post\(WIFI_EVENT, (WIFI_EVENT_NAN_\w+), evt, ([^;]+), portMAX_DELAY\);',
        r'esp32qjs_nan_usd_post(\1, evt, \2);', source)
    if count != 3:
        raise ValueError('Expected three USD event posts')
    source = replace(source, '    wpa_hexdump(MSG_INFO, "NAN_RECEIVE", ssi, ssi_len);\n', '')
    # Converting frequency/channel must not silently truncate an invalid input.
    source = replace(source, 'if ((chan >= 36 && chan <= 64) ||\n            (chan >= 100 && chan <= 144) ||\n            (chan >= 149 && chan <= 165))',
        'if (((chan >= 36 && chan <= 64) || (chan >= 100 && chan <= 144)) ?\n'
        '            chan % 4 == 0 : chan >= 149 && chan <= 165 && (chan - 149) % 4 == 0)')
    source = replace(source, 'if (freq >= 2412 && freq <= 2472)',
        'if (freq >= 2412 && freq <= 2472 && (freq - 2407) % 5 == 0)')
    source = replace(source, 'freq <= 5240', 'freq <= 5320')
    source = source.replace('chan <= 165', 'chan <= 177').replace('freq <= 5825', 'freq <= 5885')
    source = replace(source, '        return (freq - 5000) / 5;',
        '        if ((freq - 5000) % 5) return -1;\n'
        '        int channel = (freq - 5000) / 5;\n'
        '        return esp_nan_chan_to_freq(channel) == freq ? channel : -1;')
    # Control completion uses the native producer and ROC done_cb, before any
    # optional default-loop event. Never process a delayed observer a second time.
    source = function(source, 'nan_de_tx_event_handler', lambda s:
        '#include "esp32_mquickjs_wifi_nan_usd_transport.inc"\n')
    source = function(source, 'esp_nan_de_tx', lambda s: replace(replace(s,
        '    int buf_len = buf->used;',
        '    if (!buf || !dst || !bssid || buf->used > ESP_WIFI_MAX_FUP_SSI_LEN + 512U) return ESP_FAIL;\n'
        '    int buf_len = buf->used;'),
        'esp_wifi_action_tx_req(req)', 'esp32qjs_nan_usd_transport_tx(req, freq)'))
    source = function(source, 'esp_nan_de_listen', lambda s:
        replace(s, 'esp_wifi_remain_on_channel(req)', 'esp32qjs_nan_usd_transport_listen(req, freq)'))
    for name in ('esp_nan_de_tx', 'esp_nan_de_listen'):
        source = function(source, name, lambda s: s.replace('os_zalloc(',
            'esp32_mquickjs_memory_wireless_calloc("wifi.nan", 1, ').replace(
            'sizeof(*req) + buf_len);', 'sizeof(*req) + buf_len, ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);').replace(
            'sizeof(wifi_roc_req_t));', 'sizeof(wifi_roc_req_t), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);').replace(
            'os_free(req);', 'esp32_mquickjs_memory_payload_free(req);'))
    for name, ident in [('esp_nan_de_publish_terminated', 'publish_id'),
                        ('esp_nan_de_subscribe_terminated', 'subscribe_id')]:
        source = function(source, name, lambda s, ident=ident: replace(s, '\n{\n',
            '\n{\n    esp32qjs_nan_usd_control(ESP32_MQUICKJS_NAN_SDK_SERVICE_TERMINATED, '
            + ident + ', reason, 0);\n'))
    source = function(source, 'esp_nan_de_tx', lambda s: replace(s,
        '    req->no_ack = false;', '    req->no_ack = (dst[0] & 1) != 0;'))
    # Preserve the caller's discovery type instead of silently using SDK defaults.
    for kind in ('publish', 'subscribe'):
        source = function(source, 'esp_nan_usd_' + kind + '_internal', lambda s, kind=kind: replace(s,
            'const wifi_scan_channel_bitmap_t channel_bitmap)',
            'const wifi_scan_channel_bitmap_t channel_bitmap, const wifi_nan_' + kind + '_cfg_t *config)'))
        source = function(source, 'esp_nan_usd_' + kind, lambda s, kind=kind: replace(s,
            kind + '_cfg->usd_' + kind + '_config.usd_chan_bitmap);',
            kind + '_cfg->usd_' + kind + '_config.usd_chan_bitmap, ' + kind + '_cfg);'))
    source = replace(source, '    pub_params.ttl = ttl;',
        '    pub_params.ttl = ttl;\n'
        '    pub_params.unsolicited = config->type == NAN_PUBLISH_UNSOLICITED;\n'
        '    pub_params.solicited = config->type == NAN_PUBLISH_SOLICITED;\n'
        '    pub_params.fsd = config->fsd_reqd;\n    pub_params.fsd_gas = config->fsd_gas;')
    source = replace(source, '    sub_params.ttl = ttl;',
        '    sub_params.ttl = ttl;\n    sub_params.active = config->type == NAN_SUBSCRIBE_ACTIVE;')
    source = replace(source, '                                buf, NULL, &pub_params, p2p);',
        '                                buf, NULL, &pub_params, p2p);\n'
        '    if (publish_id > 0) esp32qjs_nan_usd_configure_publish(g_nan_de, publish_id, &config->usd_publish_config);')
    # The original subscriber silently discards its entire 5 GHz bitmap. Reuse
    # the publisher's reviewed conversion, including its allocation cleanup.
    pub_start = source.index('    channel_2ghz_bitmap = channel_bitmap.ghz_2_channels;', source.index('static int esp_nan_usd_publish_internal'))
    pub_end = source.index('\n    publish_id = nan_de_publish', pub_start)
    channel_block = source[pub_start:pub_end].replace('pub_params', 'sub_params')
    def subscriber_channels(s):
        s = replace(s, '    uint16_t channel_2ghz_bitmap;',
            '    uint16_t channel_2ghz_bitmap;\n#if CONFIG_SOC_WIFI_SUPPORT_5G\n'
            '    uint8_t bitmap_idx_5g = 1;\n    uint32_t channel_5ghz_bitmap;\n#endif')
        start = s.index('    channel_2ghz_bitmap = channel_bitmap.ghz_2_channels;')
        end = s.index('\n    subscribe_id = nan_de_subscribe', start)
        return s[:start] + channel_block + s[end:]
    source = function(source, 'esp_nan_usd_subscribe_internal', subscriber_channels)
    for suffix in ('publish', 'subscribe', 'update_publish', 'cancel_publish',
                   'cancel_subscribe', 'cancel_service', 'transmit'):
        name = 'esp_nan_usd_' + suffix
        source = function(source, name, lambda s, name=name, suffix=suffix:
            'static ' + replace(s, name + '(', 'esp32qjs_usd_' + suffix + '_native('))
    source += '\n#include "esp32_mquickjs_wifi_nan_usd_commands.inc"\n'
    return source


def patch_timer(source):
    source = replace(source, '#include "nan_de.h"', '#include "nan_de.h"\n'
        '#include "esp32_mquickjs_wifi_nan_usd_sdk.h"')
    source = replace(source, '\tunsigned int next_publish_duration;',
        '\tunsigned int next_publish_duration;\n\tuint8_t n_min, n_max, m_min, m_max;')
    source = replace(source, '\tunsigned int num_service;', '\tunsigned int num_service, timer_next;')
    source = replace(source, '\tn = 5 + os_random() % 5;',
        '\tunsigned int minimum = srv->in_multi_chan ? srv->m_min : srv->n_min;\n'
        '\tunsigned int maximum = srv->in_multi_chan ? srv->m_max : srv->n_max;\n'
        '\tif (!minimum) minimum = 5;\n\tif (!maximum) maximum = 10;\n'
        '\tn = minimum + os_random() % (maximum - minimum + 1);')
    source += '''
void esp32qjs_nan_usd_configure_publish(struct nan_de *de, int id, const wifi_nan_usd_config_t *config)
{
    if (!de || !config || id <= 0 || id > NAN_DE_MAX_SERVICE) return;
    struct nan_de_service *srv = de->service[id - 1];
    if (!srv || srv->type != NAN_DE_PUBLISH) return;
    srv->n_min = config->n_min; srv->n_max = config->n_max;
    srv->m_min = config->m_min; srv->m_max = config->m_max;
    nan_de_start_new_publish_state(srv, true);
}
'''
    source = function(source, 'nan_de_timer', lambda s:
        '''static void esp32qjs_nan_usd_subscribe_channel(struct nan_de *de,
    struct nan_de_service *srv, const struct os_reltime *now)
{
    if (srv->type != NAN_DE_SUBSCRIBE || !srv->freq_list || !srv->freq_list[0] ||
        de->listen_freq || de->ext_listen_freq || de->tx_wait_end_freq) return;
    if (!os_reltime_initialized(&srv->next_publish_chan)) srv->multi_chan_idx = 0;
    else {
        if (os_reltime_before(now, &srv->next_publish_chan)) return;
        if (!srv->freq_list[++srv->multi_chan_idx]) srv->multi_chan_idx = 0;
    }
    srv->freq = srv->freq_list[srv->multi_chan_idx];
    srv->next_publish_chan = *now;
    os_reltime_add_ms(&srv->next_publish_chan, 1000);
}

static void nan_de_timer(void *eloop_ctx, void *timeout_ctx);

''' +
        replace(s, 'static void nan_de_timer(', 'static void nan_de_timer_inner(') + '''

static void nan_de_timer(void *eloop_ctx, void *timeout_ctx)
{
    if (!esp32qjs_nan_usd_timer_enter(eloop_ctx, (uint32_t)(uintptr_t)timeout_ctx))
        return;
    nan_de_timer_inner(eloop_ctx, timeout_ctx);
    esp32qjs_nan_usd_timer_leave();
}
''')
    # Each subscriber must actually visit its configured list. Rotate only
    # between native operations; a second service must also get a turn.
    source = function(source, 'nan_de_timer_inner', lambda s: replace(replace(replace(replace(s,
        '\tfor (i = 0; i < NAN_DE_MAX_SERVICE; i++) {\n\t\tstruct nan_de_service *srv = de->service[i];',
        '\tunsigned int first = de->timer_next;\n\tfor (i = 0; i < NAN_DE_MAX_SERVICE; i++) {\n'
        '\t\tunsigned int slot = (first + i) % NAN_DE_MAX_SERVICE;\n'
        '\t\tstruct nan_de_service *srv = de->service[slot];'),
        '\t\tsrv_next = nan_de_srv_time_to_next(de, srv, &now);',
        '\t\tesp32qjs_nan_usd_subscribe_channel(de, srv, &now);\n'
        '\t\tsrv_next = nan_de_srv_time_to_next(de, srv, &now);'),
        '\t\t\tstarted = true;', '\t\t\tstarted = true;\n\t\t\tde->timer_next = (slot + 1) % NAN_DE_MAX_SERVICE;', count=2),
        '\t\t\t\tde->listen_freq = srv->freq;\n\t\t\t\treturn;',
        '\t\t\t\tde->listen_freq = srv->freq;\n\t\t\t\tde->timer_next = (slot + 1) % NAN_DE_MAX_SERVICE;\n\t\t\t\treturn;'))
    source = replace(source, '''\teloop_register_timeout(next / 1000, (next % 1000) * 1000, nan_de_timer,
\t\t\t       de, NULL);''', '''\tif (eloop_register_timeout(next / 1000, (next % 1000) * 1000, nan_de_timer,
\t\tde, (void *)(uintptr_t)esp32qjs_nan_usd_timer_identity(de)) < 0)
\t\tesp32qjs_nan_usd_timer_failed(de);''')
    source = replace(source, '\teloop_register_timeout(0, 0, nan_de_timer, de, NULL);',
        '\tif (eloop_register_timeout(0, 0, nan_de_timer, de,\n'
        '\t\t(void *)(uintptr_t)esp32qjs_nan_usd_timer_identity(de)) < 0)\n'
        '\t\tesp32qjs_nan_usd_timer_failed(de);')
    source = replace(source, 'eloop_cancel_timeout(nan_de_timer, de, NULL);',
        'eloop_cancel_timeout(nan_de_timer, de,\n'
        '\t\t(void *)(uintptr_t)esp32qjs_nan_usd_timer_identity(de));', count=2)
    source = function(source, 'nan_de_tx_sdf', lambda s: replace(replace(replace(s,
        'static void nan_de_tx_sdf(', 'static int nan_de_tx_sdf('), '\t\treturn;', '\t\treturn -1;'),
        '\tnan_de_tx(de, srv->freq, wait_time, dst, de->nmi, a3, buf);\n\twpabuf_free(buf);',
        '\tint result = nan_de_tx(de, srv->freq, wait_time, dst, de->nmi, a3, buf);\n\twpabuf_free(buf);\n\treturn result;'))
    source = replace(source, '\tint result = nan_de_tx(de, srv->freq, wait_time, dst, de->nmi, a3, buf);',
        '\tuint8_t previous = esp32qjs_nan_usd_tx_service_scope(srv->id);\n'
        '\tint result = nan_de_tx(de, srv->freq, wait_time, dst, de->nmi, a3, buf);\n'
        '\tesp32qjs_nan_usd_tx_service_scope(previous);')
    source = replace(source, '\tnan_de_tx(de, srv->freq, 100,',
        '\tuint8_t previous = esp32qjs_nan_usd_tx_service_scope(srv->id);\n'
        '\tnan_de_tx(de, srv->freq, 100,')
    source = replace(source, '\t\t  de->nmi, a3, buf);\n\twpabuf_free(buf);',
        '\t\t  de->nmi, a3, buf);\n\tesp32qjs_nan_usd_tx_service_scope(previous);\n\twpabuf_free(buf);')
    source = function(source, 'nan_de_transmit', lambda s: replace(replace(s,
        '\tnan_de_tx_sdf(de, srv, 100,', '\treturn nan_de_tx_sdf(de, srv, 100,'), '\n\treturn 0;\n', '\n'))
    return source


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--component', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    for path, expected in REVIEWED.items():
        if hashlib.sha256((args.component / path).read_bytes()).hexdigest() != expected:
            raise ValueError('Unreviewed USD input: ' + path)
    outputs = {
        'esp_nan_usd.c': patch_engine((args.component / 'esp_supplicant/src/esp_nan_usd.c').read_text()),
        'nan_de.c': patch_timer((args.component / 'src/common/nan_de.c').read_text())}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, source in outputs.items():
        output = args.output_dir / name
        if not output.exists() or output.read_text() != source:
            output.write_text(source)


if __name__ == '__main__':
    main()
