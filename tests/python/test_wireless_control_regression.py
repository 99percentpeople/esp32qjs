"""Compile production Wi-Fi helpers with SDK boundaries replaced by fixtures."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULES = ROOT/'components/esp32_mquickjs/src/modules'


def function(source, name):
    import re
    match = re.search(r'static [^;{}]*\b' + name + r'\([^;{}]*\)\n\{', source)
    assert match, name
    return source[match.start():source.index('\n}\n', match.start())+3]


def compile_run(case, content):
    compiler = shutil.which('cc')
    if compiler is None:
        case.skipTest('C compiler unavailable')
    with tempfile.TemporaryDirectory() as tmp:
        c = pathlib.Path(tmp)/'fixture.c'
        c.write_text(content)
        result = subprocess.run([compiler, '-std=c11', str(c), '-o', tmp+'/test'], capture_output=True, text=True)
        case.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([tmp+'/test'], capture_output=True, text=True)
        case.assertEqual(result.returncode, 0, result.stderr)


class WirelessControlRegression(unittest.TestCase):
    def test_wifi_control_terminal_survives_full_driver_queue(self):
        body = function((MODULES/'wifi/esp32_mquickjs_wifi.c').read_text(), 'wifi_publish_driver_event_from_callback')
        compile_run(self, r'''
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define pdTRUE 1
#define WIFI_DRIVER_EVENT_CONNECT_TIMEOUT 5
static int processed, wakes;
typedef struct { int kind, reason; unsigned status; } esp32_mquickjs_wifi_driver_event_t;
static struct { void *driver_event_queue; atomic_uint dropped_driver_events; } s_wifi_state;
static void *s_wifi_runtime = (void *)1;
static atomic_bool s_wifi_timeout_pending;
static int xQueueSend(void *q, const void *event, int wait) { (void)q; (void)event; (void)wait; return 0; }
static void esp32_mquickjs_notify_activity(void *r) { (void)r; wakes++; }
static void wifi_process_driver_event(const esp32_mquickjs_wifi_driver_event_t *e) { processed = e->kind; }
''' + body + r'''
int main(void) {
    esp32_mquickjs_wifi_driver_event_t e = {.kind=2};
    assert(wifi_publish_driver_event_from_callback(&e));
    assert(processed==2);
    e.kind=3;
    assert(wifi_publish_driver_event_from_callback(&e));
    assert(processed==3);
    e.kind=WIFI_DRIVER_EVENT_CONNECT_TIMEOUT;
    assert(wifi_publish_driver_event_from_callback(&e));
    assert(atomic_load(&s_wifi_timeout_pending));
}
''')

    def test_wifi_owned_password_is_zeroed_before_destroy(self):
        body = function((MODULES/'wifi/esp32_mquickjs_wifi_future.c').read_text(), 'wifi_future_destroy')
        compile_run(self, r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define WIFI_FUTURE_DISCONNECT 2
typedef struct { bool started, completed; int kind; struct { struct { unsigned char password[64]; } sta; } connect_config; } esp32_mquickjs_future_driver_state_t;
static void esp32_mquickjs_wifi_clear_connect_future(void) {}
static int wifi_future_cancel(esp32_mquickjs_future_driver_state_t *s) { (void)s; return 0; }
static void esp32_mquickjs_wireless_secure_zero(void *p, size_t n) { volatile unsigned char *v=p; while(n--) *v++=0; }
static void heap_caps_free(esp32_mquickjs_future_driver_state_t *s) {
    for (unsigned i=0; i<sizeof(s->connect_config.sta.password); i++) assert(s->connect_config.sta.password[i]==0);
    free(s);
}
''' + body + r'''
int main(void) {
    esp32_mquickjs_future_driver_state_t *s=calloc(1,sizeof(*s));
    memset(s->connect_config.sta.password, 0xa5, 64);
    wifi_future_destroy(s);
}
''')
