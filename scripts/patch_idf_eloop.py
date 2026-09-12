"""Rearm the existing SDK eloop timer after a failed native wake delivery."""
import argparse
import hashlib
from pathlib import Path

REVIEWED = '2decdfef8e932070a791d7fd4dfa85251fedbfd6ad06f5a0da22f4b1c6eab9ac'
BEFORE = '''    esp_wifi_ipc_internal(&cfg, false);
'''
AFTER = '''    if (esp_wifi_ipc_internal(&cfg, false) != 0 && eloop_data_lock) {
        /* The timeout list still owns the work. A failed one-shot wake must
         * not strand it. Reuse the existing timer with bounded backoff; a
         * concurrent cancellation/destroy wins through the same mutex.
         * This queues only a wake: eloop_run removes each due timeout once.
         * A late/duplicate wake cannot repeat a timeout or execute it early. */
        ELOOP_LOCK();
        if (eloop_is_running() && !dl_list_empty(&eloop.timeout)) {
            os_timer_disarm(&eloop.eloop_timer);
            os_timer_arm(&eloop.eloop_timer, 10, 0);
        }
        ELOOP_UNLOCK();
    }
'''


def patch_source(data):
    if hashlib.sha256(data).hexdigest() != REVIEWED:
        raise ValueError('Unreviewed SDK eloop source')
    source = data.decode()
    if source.count(BEFORE) != 1:
        raise ValueError('Expected one asynchronous eloop IPC wake')
    return source.replace(BEFORE, AFTER, 1).encode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = patch_source(args.source.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != data:
        args.output.write_bytes(data)
    print('ESP32QJS eloop: retain timeout list and retry failed native wake')


if __name__ == '__main__':
    main()
