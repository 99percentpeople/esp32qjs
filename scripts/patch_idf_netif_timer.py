"""Generate a build-local fix for the reviewed ESP-IDF netif timer lifetime.

The SDK installation is read-only. A different upstream translation unit must be
reviewed before changing the digest; silently accepting it can reintroduce an
address-reuse bug or apply the workaround on top of an incompatible SDK fix.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

REVIEWED_SHA256 = '0b448749d365832307173ad9c21b692e8a899983d15d2f194766cfa34df4acd9'

CANCEL_HELPER = '''/* ESP32QJS: cancel the exact SDK timer before netif stop/destruction.
 * These callers and the timer execute on the same TCP/IP task. Removing it
 * before releasing the object prevents an old arg from naming a reused address. */
#if CONFIG_LWIP_IPV4 && CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE
static void esp_netif_ip_lost_timer(void *arg);
#endif
static void esp32qjs_netif_cancel_ip_lost_timer(esp_netif_t *esp_netif)
{
#if CONFIG_LWIP_IPV4 && CONFIG_ESP_NETIF_LOST_IP_TIMER_ENABLE
    sys_untimeout(esp_netif_ip_lost_timer, esp_netif);
    esp_netif->timer_running = false;
#else
    (void)esp_netif;
#endif
}

'''


def patch_source(source: bytes) -> bytes:
    digest = hashlib.sha256(source).hexdigest()
    if digest != REVIEWED_SHA256:
        raise ValueError(f'Unreviewed ESP-IDF esp_netif_lwip.c SHA-256 {digest}; '
                         'review its timer lifetime before updating the workaround')
    text = source.decode('utf-8')
    for name in ('esp_netif_destroy_api', 'esp_netif_stop_api'):
        before = (f'static esp_err_t {name}(esp_netif_api_msg_t *msg)\n'
                  '{\n    esp_netif_t *esp_netif = msg->esp_netif;\n')
        if text.count(before) != 1:
            raise ValueError(f'Expected one reviewed {name} definition')
        after = before + '    esp32qjs_netif_cancel_ip_lost_timer(esp_netif);\n'
        if name == 'esp_netif_destroy_api':
            after = CANCEL_HELPER + after
        text = text.replace(before, after, 1)
    return text.encode('utf-8')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error('output must be a build-local copy, not the SDK source')
    try:
        patched = patch_source(args.source.read_bytes())
    except ValueError as exc:
        parser.error(str(exc))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != patched:
        args.output.write_bytes(patched)
    print('ESP32QJS netif timer workaround: reviewed build-local source '
          + hashlib.sha256(patched).hexdigest())


if __name__ == '__main__':
    main()
