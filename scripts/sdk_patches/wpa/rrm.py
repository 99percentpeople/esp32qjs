"""Build-local fixed-SDK RRM fixes and exact native callback ownership hooks.

The shared SDK is never edited. Helpers live inside rrm.c so their private
supplicant types/timer references use the SDK's own compile configuration.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path

REVIEWED = {
    'src/common/rrm.c': '7c15624aff63e882b65c3438931c7c4af41a1275107a8c040e8cfd7d53f77fb8',
    'esp_supplicant/src/esp_common.c': '78a02287147e73c82e822b7369c1f9dad35b1631028aea0a9e6cc46bfb0d6bb7',
    'src/common/wpa_supplicant_i.h': '80036a44e4f994d07e539bb64ef5374840c6368fb0db55d926e1c32cb6131acf',
    'port/eloop.c': '2decdfef8e932070a791d7fd4dfa85251fedbfd6ad06f5a0da22f4b1c6eab9ac',
}

PRELUDE = '''/* ESP32QJS: only the explicit native request bridge installs this pointer.
 * All accesses execute on the supplicant task; no JS/Radio callbacks here. */
static bool *esp32qjs_rrm_tx_attempted;

'''
HELPERS = '''
/* ESP32QJS fixed-SDK native boundary. Call only on the Wi-Fi/supplicant task.
 * An observation event is independent of this callback/context ownership. */
extern int esp_rrm_send_neighbor_report_request(void);
extern void neighbor_report_recvd_cb(void *ctx, const u8 *report, size_t length);
typedef void (*esp32qjs_rrm_callback_t)(void *, const u8 *, size_t);

int esp32qjs_rrm_query(esp32qjs_rrm_callback_t callback, void *identity)
{
    struct rrm_data *rrm = &g_wpa_supp.rrm;
    if (!callback || !identity) return -EINVAL;
    if (!rrm->notify_neighbor_rep) return rrm->neighbor_rep_cb_ctx ? -EIO : 0;
    return rrm->notify_neighbor_rep == callback && rrm->neighbor_rep_cb_ctx == identity ? 1 : -EBUSY;
}

int esp32qjs_rrm_request(esp32qjs_rrm_callback_t callback, void *identity, bool *tx_attempted)
{
    struct rrm_data *rrm = &g_wpa_supp.rrm;
    if (!tx_attempted) return -EINVAL;
    *tx_attempted = false;
    if (!callback || !identity) return -EINVAL;
    if (esp32qjs_rrm_tx_attempted || rrm->notify_neighbor_rep || rrm->neighbor_rep_cb_ctx) return -EBUSY;
    esp32qjs_rrm_tx_attempted = tx_attempted;
    /* Keep the public SDK's connected-AP capability and SSID construction path.
     * The task cannot deliver an RX/timer callback until this call returns. */
    int result = esp_rrm_send_neighbor_report_request();
    esp32qjs_rrm_tx_attempted = NULL;
    if (result != 0) return result;
    if (rrm->notify_neighbor_rep != neighbor_report_recvd_cb || rrm->neighbor_rep_cb_ctx != NULL) return -EIO;
    rrm->notify_neighbor_rep = callback;
    rrm->neighbor_rep_cb_ctx = identity;
    return 0;
}

int esp32qjs_rrm_cancel(esp32qjs_rrm_callback_t callback, void *identity)
{
    int state = esp32qjs_rrm_query(callback, identity);
    if (state <= 0) return state; /* Idle is idempotent; foreign owner is untouched. */
    struct rrm_data *rrm = &g_wpa_supp.rrm;
    /* Fixed eloop_run dispatches one item at a time. On this task a callback
     * cannot be simultaneously detached/executing behind this cancellation. */
    eloop_cancel_timeout(wpas_rrm_neighbor_rep_timeout_handler, rrm, NULL);
    rrm->notify_neighbor_rep = NULL;
    rrm->neighbor_rep_cb_ctx = NULL;
    return 0;
}

void esp32qjs_rrm_publish_observation(const u8 *report, size_t length)
{
    if ((report && !length) || (!report && length)) return;
    neighbor_report_recvd_cb(NULL, report, length);
}
'''


def patch_source(source: bytes) -> bytes:
    digest = hashlib.sha256(source).hexdigest()
    if digest != REVIEWED['src/common/rrm.c']:
        raise ValueError(f'Unreviewed ESP-IDF rrm.c SHA-256 {digest}')
    text = source.decode()
    anchor = 'static void wpas_rrm_neighbor_rep_timeout_handler('
    if text.count(anchor) != 1: raise ValueError('Expected one SDK RRM timeout handler')
    text = text.replace(anchor, PRELUDE + anchor, 1)
    expected = 'wpa_s->rrm.next_neighbor_rep_token - 1'
    if text.count(expected) != 2: raise ValueError('Expected two SDK RRM response-token uses')
    text = text.replace(expected, '(u8) (wpa_s->rrm.next_neighbor_rep_token - 1)')
    send = '\twpa_s->rrm.next_neighbor_rep_token++;\n\n\tif (wpa_drv_send_action(wpa_s, 0, 0,\n'
    if text.count(send) != 1: raise ValueError('Expected one SDK neighbor-report send')
    text = text.replace(send, send.replace('\tif (wpa_drv_send_action',
        '\tif (esp32qjs_rrm_tx_attempted) *esp32qjs_rrm_tx_attempted = true;\n\tif (wpa_drv_send_action'))
    timer = ('\teloop_register_timeout(RRM_NEIGHBOR_REPORT_TIMEOUT, 0,\n'
             '\t\t\t       wpas_rrm_neighbor_rep_timeout_handler,\n'
             '\t\t\t       &wpa_s->rrm, NULL);')
    if text.count(timer) != 1: raise ValueError('Expected one SDK neighbor-report timer registration')
    text = text.replace(timer, '''\tif (eloop_register_timeout(RRM_NEIGHBOR_REPORT_TIMEOUT, 0,
                               wpas_rrm_neighbor_rep_timeout_handler,
                               &wpa_s->rrm, NULL) != 0) {
        /* The request may already be on air. Retire native callback storage
         * and report failure instead of leaving a permanent pending request. */
        wpa_s->rrm.notify_neighbor_rep = NULL;
        wpa_s->rrm.neighbor_rep_cb_ctx = NULL;
        wpabuf_free(buf);
        return -ENOMEM;
    }''')
    return (text + HELPERS).encode()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--component', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve().is_relative_to(args.component.resolve()):
        parser.error('output must be outside the shared SDK component')
    try:
        for relative, digest in REVIEWED.items():
            if hashlib.sha256((args.component / relative).read_bytes()).hexdigest() != digest:
                raise ValueError('Unreviewed SDK RRM dependency: ' + relative)
        patched = patch_source((args.component / 'src/common/rrm.c').read_bytes())
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != patched:
        args.output.write_bytes(patched)
    print('ESP32QJS RRM fixes/native ownership boundary: reviewed build-local source ' + hashlib.sha256(patched).hexdigest())


if __name__ == '__main__':
    main()
