"""Deferred production NAN Future callbacks with the complete native Session.

Radio, worker dispatch and JS error construction are injected boundaries.
This file is not a second lifecycle implementation and is not RF evidence.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_wifi_nan_session import BASE, PREFIX, BOUNDARY, without_includes
from wireless_vm_fixture import extract
from test_wifi_nan_path import HELPER as PATH_HELPER


class NanFuture(unittest.TestCase):
    def test_pairing_receive_wait_timeout_and_cancel_return_request_to_service(self):
        from test_wifi_nan_bootstrap import PUBLISH_HELPER, BOOTSTRAP_HELPER
        self.compile_case(PUBLISH_HELPER + BOOTSTRAP_HELPER + r'''
int main(void){
 setup();
 esp32_mquickjs_future_driver_state_t*state=calloc(1,sizeof(*state));assert(state);++allocations;
 assert(esp32_mquickjs_wifi_nan_discovery_retain(d));
 state->operation=NAN_PAIRING_RECEIVE;state->discovery=d;state->timeout_ms=10;
 assert(nan_start(NULL,NULL,0,state)&&!queued);
 assert(nan_timeout(state)==10&&nan_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);
 assert(nan_on_timeout(NULL,state,10)==JS_NULL&&!d->status.closing);
 incoming();tick();assert(nan_poll(state)==ESP32_MQUICKJS_FUTURE_READY&&state->pairing);
 uint32_t identity=state->pairing->status.identity;
 assert(nan_cancel_wait(state)==ESP32_MQUICKJS_CANCELLED);nan_destroy(state);
 assert(s->pairing&&!s->pairing->receiving&&!s->pairing->status.closing);
 esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_receive(d,&p)&&p->status.identity==identity);
 esp32_mquickjs_wifi_nan_pairing_unreserve(p);esp32_mquickjs_wifi_nan_pairing_release(p);
 finish();assert(!pair_starts&&!bootstrap_sends);
}
''', pairing=True)

    def test_connection_wait_timeout_cancel_and_receive_reservation_keep_native_owner(self):
        self.compile_case(PATH_HELPER + r'''
int main(void){
 setup(true);incoming();tick();
 esp32_mquickjs_future_driver_state_t state={.operation=NAN_PATH_RECEIVE,.discovery=d,.timeout_ms=10};
 assert(nan_start(NULL,NULL,0,&state)&&state.path);
 assert(nan_poll(&state)==ESP32_MQUICKJS_FUTURE_READY&&!state.path->status.delivered);
 assert(nan_cancel_wait(&state)==ESP32_MQUICKJS_CANCELLED);
 esp32_mquickjs_wifi_nan_path_unreserve(state.path);esp32_mquickjs_wifi_nan_path_release(state.path);state.path=NULL;
 esp32_mquickjs_wifi_nan_path_t*p=NULL;assert(!esp32_mquickjs_wifi_nan_path_receive(d,&p)&&p);
 assert(esp32_mquickjs_wifi_nan_path_deliver(p));
 state=(esp32_mquickjs_future_driver_state_t){.operation=NAN_PATH_READY,.path=p,.timeout_ms=10};
 assert(nan_start(NULL,NULL,0,&state));assert(nan_poll(&state)==ESP32_MQUICKJS_FUTURE_PENDING);
 assert(nan_on_timeout(NULL,&state,10)==-1&&wait_timeout_seen&&!p->status.closing);
 assert(nan_cancel_wait(&state)==ESP32_MQUICKJS_CANCELLED&&!p->status.closing);
 esp32_mquickjs_wifi_nan_path_close(p,false);tick();
 assert(path_responses==1&&!path_response_accept);
 path_native[0].native_deleted=path_native[0].frames_retired=true;tick();
 esp32_mquickjs_wifi_nan_path_release(p);finish();
 /* Exercise shared extracted lifecycle utilities with no external owner. */
 assert(nan_timeout(&state)==10);nan_destroy(NULL);
}
''')

    def test_pairing_wait_timeout_does_not_revoke_confirmation_or_close_owner(self):
        from test_wifi_nan_pairing_session import HELPER
        self.compile_case(HELPER + r'''
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=prepare(30000);
 esp32_mquickjs_future_driver_state_t state={.operation=NAN_PAIRING_READY,.pairing=p,.timeout_ms=10};
 assert(nan_start(NULL,NULL,0,&state));run_queued(NULL);
 assert(nan_on_timeout(NULL,&state,10)==-1&&wait_timeout_seen&&!p->status.closing);
 assert(nan_cancel_wait(&state)==ESP32_MQUICKJS_CANCELLED&&!p->status.closing);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));tick();assert(pair_starts==1);
 state=(esp32_mquickjs_future_driver_state_t){.operation=NAN_PAIRING_CLOSE,.pairing=p,.timeout_ms=10};
 pair_close_error=ESP_ERR_TIMEOUT;assert(nan_start(NULL,NULL,0,&state));run_queued(NULL);
 assert(p->status.closing&&!p->status.retired);
 assert(nan_on_timeout(NULL,&state,10)==-1&&!p->status.retired);
 pair_close_error=0;tick();assert(p->status.retired&&nan_poll(&state)==ESP32_MQUICKJS_FUTURE_READY);
 assert(nan_timeout(&state)==10);nan_destroy(NULL);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
''', pairing=True)

    def test_wait_cancel_timeout_and_close_retains_native_cleanup(self):
        self.compile_case(MAIN)

    def test_service_wait_cancel_and_timeout_do_not_release_native_ownership(self):
        self.compile_case(SERVICE_MAIN)

    def compile_case(self, main, *, pairing=False):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('Host C compiler unavailable')
        source = PREFIX
        if pairing:
            source = source.replace("#define CONFIG_ESP_WIFI_NAN_SECURITY 0", "#define CONFIG_ESP_WIFI_NAN_SECURITY 1")
            source += "\n#define CONFIG_ESP_WIFI_NAN_PAIRING 1\n"
        for name in ('esp32_mquickjs_wifi_nan_pasn_sdk.h', 'esp32_mquickjs_wifi_nan_sdk.h', 'esp32_mquickjs_wifi_nan_ndp.h', 'esp32_mquickjs_wifi_nan_tx.h', 'esp32_mquickjs_wifi_nan_radio.h',
                     'esp32_mquickjs_wifi_nan_session.h', 'esp32_mquickjs_wifi_nan_discovery.h', 'esp32_mquickjs_wifi_nan_message.h', 'esp32_mquickjs_wifi_nan_path.h', 'esp32_mquickjs_wifi_nan_pairing.h'):
            source += without_includes(BASE / 'internal' / name)
        source += BOUNDARY
        if pairing:
            from test_wifi_nan_pairing_session import PAIRING_BOUNDARY
            source += PAIRING_BOUNDARY
        source += without_includes(BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_session.c')
        future = (BASE / 'internal/esp32_mquickjs_future.h').read_text()
        for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
            source += re.search(r'typedef enum \{[^}]*\} ' + name + ';', future).group(0)
        public = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan.c').read_text()
        source += re.search(r'typedef enum \{[^}]*\} nan_operation_t;', public).group(0)
        source += re.search(r'typedef struct \{[^}]*\} nan_path_response_t;', public).group(0)
        start = public.index('struct esp32_mquickjs_future_driver_state {')
        source += public[start:public.index('\n};', start) + 3]
        source += r'''
typedef int JSValue,JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;
typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;
#define JS_NULL 0
static bool wait_timeout_seen;
#if CONFIG_ESP_WIFI_NAN_PAIRING
static JSValue nan_pairing_error(JSContext*ctx,esp32_mquickjs_wifi_nan_pairing_t*p,const char*name,int error,bool timeout){
 (void)ctx;assert(p&&name&&error);wait_timeout_seen=timeout;return -1;
}
#endif
static JSValue nan_path_error(JSContext *ctx,esp32_mquickjs_wifi_nan_path_t *s,const char *name,int error,bool timeout){
 (void)ctx;assert(s&&name&&error);wait_timeout_seen=timeout;return -1;
}
static JSValue nan_message_error(JSContext *ctx,esp32_mquickjs_wifi_nan_message_t *s,int error,bool timeout) {
    (void)ctx;assert(s&&error);wait_timeout_seen=timeout;return -1;
}
static JSValue nan_error(JSContext *ctx,const char *name,esp32_mquickjs_wifi_nan_session_t *s,int error,bool timeout) {
    (void)ctx;assert(name&&s&&error==ESP_ERR_TIMEOUT);wait_timeout_seen=timeout;return -1;
}
static JSValue nan_service_error(JSContext *ctx,const char *name,esp32_mquickjs_wifi_nan_discovery_t *s,int error,bool timeout) {
    (void)ctx;assert(name&&s&&error==ESP_ERR_TIMEOUT);wait_timeout_seen=timeout;return -1;
}
'''
        for name in ('nan_operation_name', 'nan_destroy', 'nan_start', 'nan_poll',
                     'nan_cancel_wait', 'nan_timeout', 'nan_on_timeout'):
            source += extract(public, name)
        with tempfile.TemporaryDirectory() as folder:
            path, executable = Path(folder) / 'case.c', Path(folder) / 'case'
            path.write_text(source + main)
            result = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                str(path), '-o', str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)


MAIN = r'''
static esp32_mquickjs_future_driver_state_t *waiter(esp32_mquickjs_wifi_nan_session_t *s,nan_operation_t op) {
    /* Inject the already-captured owner at the Future boundary. Public capture
     * and runtime Future integration still require concentrated VM validation. */
    esp32_mquickjs_future_driver_state_t *f=calloc(1,sizeof(*f));assert(f);++allocations;
    assert(esp32_mquickjs_wifi_nan_session_retain(s));f->session=s;f->operation=op;f->timeout_ms=10;return f;
}
int main(void) {
    assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_OK);
    wifi_nan_sync_config_t config={.op_channel=6};esp32_mquickjs_wifi_nan_session_t *s=NULL;
    assert(!esp32_mquickjs_wifi_nan_session_create(&config,1000,&s));
    assert(!esp32_mquickjs_wifi_nan_session_activate(s));
    esp32_mquickjs_future_driver_state_t *f=waiter(s,NAN_CLOSE);
    assert(nan_cancel_wait(f)==ESP32_MQUICKJS_CANCELLED&&!s->status.closing&&!begins&&!closes);
    nan_destroy(f);

    f=waiter(s,NAN_READY);assert(nan_timeout(f)==10&&nan_start(NULL,NULL,0,f));
    assert(queued&&nan_poll(f)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(nan_on_timeout(NULL,f,10)==-1&&wait_timeout_seen);
    assert(!s->status.closing&&!s->status.timed_out);nan_destroy(f);
    run_queued(NULL);assert(s->status.ready&&!s->status.closing&&begins==1&&!closes);

    f=waiter(s,NAN_READY);assert(nan_start(NULL,NULL,0,f));
    assert(nan_poll(f)==ESP32_MQUICKJS_FUTURE_READY);
    assert(nan_cancel_wait(f)==ESP32_MQUICKJS_CANCELLED);nan_destroy(f);
    assert(s->status.ready&&!s->status.closing);

    f=waiter(s,NAN_CLOSE);close_error=ESP_ERR_TIMEOUT;assert(nan_start(NULL,NULL,0,f));
    run_queued(NULL);assert(s->status.closing&&!s->status.retired&&closes==1);
    assert(nan_poll(f)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(nan_on_timeout(NULL,f,10)==-1);nan_destroy(f);
    esp32_mquickjs_wifi_nan_session_release(s);s=NULL;
    esp32_mquickjs_wifi_nan_global_status_t status;esp32_mquickjs_wifi_nan_global_status(&status);
    assert(status.active&&status.handles==1&&status.session.closing&&!status.session.timed_out);
    clock_us+=100000;close_error=ESP_OK;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);
    esp32_mquickjs_wifi_nan_global_status(&status);assert(!status.active&&!status.handles&&!status.workers&&!allocations);
}
'''

SERVICE_MAIN = r'''
static esp32_mquickjs_future_driver_state_t *message_waiter(esp32_mquickjs_wifi_nan_discovery_t *d) {
    esp32_mquickjs_future_driver_state_t *f=calloc(1,sizeof(*f));assert(f);++allocations;
    wifi_nan_followup_params_t params={.peer_inst_id=3,.peer_mac={2,3,4,5,6,7}};
    assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&f->message));
    f->operation=NAN_SERVICE_SEND;f->timeout_ms=1000;return f;
}
static esp32_mquickjs_future_driver_state_t *waiter(esp32_mquickjs_wifi_nan_discovery_t *s,nan_operation_t op) {
    esp32_mquickjs_future_driver_state_t *f=calloc(1,sizeof(*f));assert(f);++allocations;
    assert(esp32_mquickjs_wifi_nan_discovery_retain(s));f->discovery=s;f->operation=op;f->timeout_ms=10;return f;
}
static void tick(void){clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);}
int main(void) {
    assert(!esp32_mquickjs_wifi_nan_open_runtime());
    wifi_nan_sync_config_t config={.op_channel=6};esp32_mquickjs_wifi_nan_session_t *s=NULL;
    assert(!esp32_mquickjs_wifi_nan_session_create(&config,1000,&s));
    assert(!esp32_mquickjs_wifi_nan_session_activate(s));tick();
    esp32_mquickjs_wifi_nan_service_config_t cfg={.publish={.service_name="test"}};
    esp32_mquickjs_wifi_nan_discovery_t *d=NULL;
    assert(!esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d));
    esp32_mquickjs_event_queue_t q={.references=1};
    assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));
    esp32_mquickjs_future_driver_state_t *f=waiter(d,NAN_SERVICE_CLOSE);
    assert(nan_cancel_wait(f)==ESP32_MQUICKJS_CANCELLED&&!d->status.closing&&!service_cancels);
    nan_destroy(f);
    f=waiter(d,NAN_SERVICE_READY);assert(nan_timeout(f)==10&&nan_start(NULL,NULL,0,f));
    assert(nan_poll(f)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(nan_on_timeout(NULL,f,10)==-1&&wait_timeout_seen);
    assert(!d->status.closing&&!d->status.timed_out);nan_destroy(f);
    run_queued(NULL);assert(d->status.ready&&q.sent==1&&service_starts==1);
    f=waiter(d,NAN_SERVICE_READY);assert(nan_poll(f)==ESP32_MQUICKJS_FUTURE_READY);nan_destroy(f);
    f=message_waiter(d);assert(nan_cancel_wait(f)==ESP32_MQUICKJS_CANCELLED);
    assert(!message_sends&&!s->message);nan_destroy(f);assert(!s_nan_message_handles);
    f=message_waiter(d);assert(nan_start(NULL,NULL,0,f));run_queued(NULL);
    assert(message_sends==1&&nan_poll(f)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(nan_on_timeout(NULL,f,1000)==-1&&f->message->status.timed_out);
    nan_destroy(f);assert(s_nan_message_handles==1&&s->message&&d->status.send_cleanup_pending);
    message_tx.tx_done=message_tx.tx_succeeded=message_tx.buffer_retired=true;
    tick();assert(!s_nan_message_handles&&!s->message&&d->status.ready);
    service_cancel_pending=true;f=waiter(d,NAN_SERVICE_CLOSE);assert(nan_start(NULL,NULL,0,f));
    run_queued(NULL);assert(d->status.cancelled&&!d->status.retired&&service_cancels==1);
    assert(nan_poll(f)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(nan_cancel_wait(f)==ESP32_MQUICKJS_CANCELLED);nan_destroy(f);
    /* Queue context and registry remain native owners after the public wait
     * and caller reference end; cancellation is never submitted twice. */
    assert(esp32_mquickjs_wifi_nan_discovery_retain(d));
    esp32_mquickjs_wifi_nan_discovery_release(d);
    service_cancel_pending=false;tick();
    assert(d->status.retired&&!d->parent&&q.closed&&service_cancels==1&&s->status.ready);
    esp32_mquickjs_wifi_nan_discovery_queue_closed(d);esp32_mquickjs_wifi_nan_discovery_release(d);
    assert(q.references==1&&!s_nan_discovery_handles);
    esp32_mquickjs_wifi_nan_session_close(s,false);tick();esp32_mquickjs_wifi_nan_session_release(s);
    assert(!allocations&&!s_nan_handles&&!s_nan_workers);
}
'''
