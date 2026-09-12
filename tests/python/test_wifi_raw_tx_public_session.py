"""Deferred public Future control over the real native Raw TX Session stack.

The generic Future scheduler and JS conversion are outside this C fixture;
start/poll/cancel/destroy and native ownership are the production functions.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_wifi_raw_tx_session import production_session_code, MAIN as SESSION_MAIN, RAW
from test_wifi_config_controls import structure
from wireless_vm_fixture import extract, INTERNAL


class WiFiRawTxPublicSession(unittest.TestCase):
    def test_future_cancel_record_retirement_flush_and_closed_handle_detach(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = (RAW / 'esp32_mquickjs_wifi_raw_tx_public_session.c').read_text()
        future = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
        code = production_session_code()
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t;\n'
        code += structure(future, 'esp32_mquickjs_future_token_t')
        for name in ['esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t']:
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', future).group(0)
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        start = source.index('typedef esp32_mquickjs_wifi_raw_tx_session_t native_session_t;')
        code += source[start:source.index('#define SET', start)]
        for name in ['handle_retain', 'handle_release', 'handle_snapshot', 'session_future_destroy',
                     'session_future_start', 'session_future_poll', 'session_future_cancel', 'session_future_timeout', 'session_future_expire']:
            code += extract(source, name)
        code += SESSION_MAIN[:SESSION_MAIN.index('int main(void)')]
        with tempfile.TemporaryDirectory() as tmp:
            source_path, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
            source_path.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(source_path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


MAIN = r'''
static esp32_mquickjs_future_driver_state_t *state(session_operation_t operation,session_t *session) {
    esp32_mquickjs_future_driver_state_t *state=heap_caps_calloc(1,sizeof(*state),MALLOC_CAP_8BIT);assert(state);
    state->operation=operation;state->timeout_ms=1000;
    state->options=(options_t){.channel=6,.driver_sequence=true,.capacity=4};
    if(session){assert(api(retain)(session));state->session=session;}
    if(operation==SESSION_SEND){payload_t p=packet();state->bytes=p.data;state->length=p.length;}
    return state;
}
static void begin(esp32_mquickjs_future_driver_state_t *state) {
    assert(session_future_timeout(state)==1000);
    assert(session_future_start(NULL,NULL,(esp32_mquickjs_future_token_t){0},state));
}
int main(void) {
    (void)start_callback;(void)finish_callback;(void)run_job_thread;(void)enqueue;(void)status;
    memset(destination,0x34,6);memset(source_address,0x12,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    /* Cancelled open never starts I/O. Opening cancellation after worker
     * admission transfers ownership to the native close registry. */
    esp32_mquickjs_future_driver_state_t *open=state(SESSION_OPEN,NULL);
    assert(session_future_cancel(open)==ESP32_MQUICKJS_CANCELLED);begin(open);
    assert(!job_count && !acquires);session_future_destroy(open);assert(allocations==frees);
    open=state(SESSION_OPEN,NULL);begin(open);assert(job_count==1);run_job();
    assert(session_future_poll(open)==ESP32_MQUICKJS_FUTURE_READY);
    assert(session_future_cancel(open)==ESP32_MQUICKJS_CANCELLED);session_future_destroy(open);
    tick();run_job();assert(allocations==frees && !live_leases);

    options_t options={.channel=6,.driver_sequence=true,.capacity=4};
    session_t *native=NULL;assert(api(new)(&options,&native)==ESP_OK);tick();run_job();
    session_handle_t *handle=heap_caps_calloc(1,sizeof(*handle),MALLOC_CAP_8BIT);assert(handle);
    handle->references=1;handle->native=native;handle->options=options; /* native caller ref */
    esp32_mquickjs_future_driver_state_t *send=state(SESSION_SEND,native);
    begin(send);assert(send->admitted && !send->bytes && send->wait.send.token.identity);
    tick();run_job();assert(session_future_poll(send)==ESP32_MQUICKJS_FUTURE_PENDING);
    esp32_mquickjs_future_driver_state_t *flush=state(SESSION_FLUSH,native);begin(flush);
    assert(flush->wait.flush.identity && session_future_poll(flush)==ESP32_MQUICKJS_FUTURE_PENDING);
    /* Freeing an admitted send's Future state unregisters only its record.
     * Active frame, driver copy and flush ownership remain. */
    unsigned native_owned=allocations-frees;
    assert(session_future_cancel(send)==ESP32_MQUICKJS_CANCELLED);session_future_destroy(send);
    assert(allocations-frees==native_owned-1 && s_raw_tx.status.operation_active);
    assert(session_future_poll(flush)==ESP32_MQUICKJS_FUTURE_PENDING);
    driver_callback(&callback_info);tick();run_job();
    assert(session_future_poll(flush)==ESP32_MQUICKJS_FUTURE_READY);
    esp32_mquickjs_wifi_raw_tx_flush_status_t result;
    assert(api(flush_status)(native,&flush->wait.flush,&result) && result.pending==0 && result.totals.succeeded==1);
    session_future_destroy(flush);

    /* Close retains its own handle and native reference. Cache/detach releases
     * the old native caller owner while keeping closed status in the JS handle. */
    esp32_mquickjs_future_driver_state_t *close=state(SESSION_CLOSE,native);
    assert(handle_retain(handle));close->handle=handle;begin(close);tick();run_job();
    assert(session_future_poll(close)==ESP32_MQUICKJS_FUTURE_READY);
    native_status_t snapshot;assert(handle_snapshot(handle,&snapshot) && snapshot.closed && !handle->native);
    session_future_destroy(close);assert(handle->references==1);
    assert(handle_snapshot(handle,&snapshot) && snapshot.closed && snapshot.totals.succeeded==1);
    assert(esp32_mquickjs_wifi_raw_tx_sessions_drained() && allocations==frees+1);
    /* Closed handles cannot consume the 8 native registry slots. */
    session_t *next=NULL;assert(api(new)(&options,&next)==ESP_OK);api(request_close)(next);api(release)(next);
    tick();run_job();handle_release(handle);assert(allocations==frees);
    /* Close timeout before Future dispatch still requests native close. An
     * explicit cancellation before dispatch instead prevents that operation. */
    next=NULL;assert(api(new)(&options,&next)==ESP_OK);tick();run_job();
    close=state(SESSION_CLOSE,next);assert(session_future_cancel(close)==ESP32_MQUICKJS_CANCELLED);
    begin(close);assert(!status(next).close_requested);session_future_destroy(close);
    close=state(SESSION_CLOSE,next);session_future_expire(close);
    assert(status(next).close_requested && !close->started);
    session_future_destroy(close);api(release)(next);tick();run_job();assert(allocations==frees);
    assert(!critical_depth && !session_mutex_depth);
    return 0;
}
'''
