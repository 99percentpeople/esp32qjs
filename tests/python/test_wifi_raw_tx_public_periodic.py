"""Deferred production Future controls over the native periodic/Session stack.

JS conversion and the generic Future scheduler are outside this C fixture.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_wifi_raw_tx_periodic_job import production_periodic_job_code, RAW
from test_wifi_config_controls import structure
from wireless_vm_fixture import extract, INTERNAL


class WiFiRawTxPublicPeriodic(unittest.TestCase):
    def test_cancel_timeout_native_retention_and_retired_handle_detach(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = (RAW / 'esp32_mquickjs_wifi_raw_tx_public_periodic.c').read_text()
        future = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
        code = production_periodic_job_code()
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t;\n'
        code += structure(future, 'esp32_mquickjs_future_token_t')
        for name in ['esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t']:
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', future).group(0)
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        # Independent production translation units use private status_t aliases.
        code += '#define status_t public_periodic_status_t\n#define job_api(name) esp32_mquickjs_wifi_raw_tx_periodic_job_##name\n'
        start = source.index('typedef esp32_mquickjs_wifi_raw_tx_periodic_job_t job_t;')
        code += source[start:source.index('static periodic_handle_t *periodic_handle', start)]
        for name in ['handle_retain', 'handle_release', 'handle_snapshot', 'periodic_destroy',
                     'periodic_request_close', 'periodic_start', 'periodic_poll', 'periodic_cancel',
                     'periodic_timeout', 'periodic_expire']:
            code += extract(source, name)
        code += '#undef status_t\n'
        with tempfile.TemporaryDirectory() as directory:
            source_path, binary = Path(directory) / 'fixture.c', Path(directory) / 'fixture'
            source_path.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(source_path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


MAIN = r'''
static esp32_mquickjs_future_driver_state_t *opening(session_t *session) {
    esp32_mquickjs_future_driver_state_t *state=heap_caps_calloc(1,sizeof(*state),MALLOC_CAP_8BIT);assert(state);
    assert(api(retain)(session));state->session=session;state->timeout_ms=1000;
    state->options=(esp32_mquickjs_wifi_raw_tx_periodic_options_t){.interval_us=1000,.stop_on_error=true};
    payload_t frame=packet();state->bytes=frame.data;state->length=frame.length;
    state->handle=heap_caps_calloc(1,sizeof(*state->handle),MALLOC_CAP_8BIT);assert(state->handle);state->handle->references=1;
    return state;
}
static void begin(esp32_mquickjs_future_driver_state_t *state) {
    assert(periodic_timeout(state)==1000);
    assert(periodic_start(NULL,NULL,(esp32_mquickjs_future_token_t){0},state));drain_workers();
}
int main(void) {
    (void)start_callback;(void)finish_callback;(void)run_job_thread;(void)enqueue;
    memset(destination,0x34,6);memset(source_address,0x12,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    options_t options={.channel=6,.driver_sequence=true,.capacity=4};session_t *session=NULL;
    assert(api(new)(&options,&session)==ESP_OK);tick();drain_workers();
    unsigned baseline=allocations-frees;
    esp32_mquickjs_future_driver_state_t *state=opening(session);
    assert(periodic_cancel(state)==ESP32_MQUICKJS_CANCELLED);begin(state);
    assert(!state->job && !timer_creates && !sends);periodic_destroy(state);assert(allocations-frees==baseline);
    state=opening(session);begin(state);
    assert(periodic_poll(state)==ESP32_MQUICKJS_FUTURE_READY && state->job && !state->bytes && sends==1);
    job_t *job=state->job;assert(job_api(retain)(job));esp_timer_handle_t timer=job->timer;
    /* Future storage can disappear before native transmission finishes. */
    periodic_expire(state);periodic_destroy(state);
    assert(s_raw_tx.status.operation_active && session->periodic_children==1);
    driver_callback(&callback_info);fire_timer(timer);drain_workers();fire_timer(timer);drain_workers();
    esp32_mquickjs_wifi_raw_tx_periodic_job_status_t snapshot;
    assert(job_api(status)(job,&snapshot) && snapshot.retired && !snapshot.worker_busy);
    job_api(release)(job);assert(allocations-frees==baseline);
    /* Model successful finish's ownership transfer, then exercise the actual
     * close state/handle functions without replacing native lifecycle behavior. */
    state=opening(session);begin(state);job=state->job;
    periodic_handle_t *handle=state->handle;handle->job=job;state->job=NULL;state->handle=NULL;
    periodic_destroy(state);timer=job->timer;
    state=heap_caps_calloc(1,sizeof(*state),MALLOC_CAP_8BIT);assert(state);
    state->closing=true;state->timeout_ms=1000;assert(handle_retain(handle));state->handle=handle;
    assert(job_api(retain)(job));state->job=job;
    /* Explicit close cancellation before dispatch leaves the job running. */
    assert(periodic_cancel(state)==ESP32_MQUICKJS_CANCELLED);begin(state);periodic_destroy(state);
    assert(!handle->close_requested && job_api(status)(job,&snapshot) && snapshot.ledger.running);
    state=heap_caps_calloc(1,sizeof(*state),MALLOC_CAP_8BIT);assert(state);
    state->closing=true;state->timeout_ms=1000;assert(handle_retain(handle));state->handle=handle;
    assert(job_api(retain)(job));state->job=job;
    periodic_expire(state);assert(handle->close_requested && !state->started);
    driver_callback(&callback_info);fire_timer(timer);drain_workers();fire_timer(timer);drain_workers();
    assert(handle_snapshot(handle,&snapshot) && snapshot.retired && snapshot.close_requested && !handle->job);
    periodic_destroy(state);assert(handle->references==1);
    assert(handle_snapshot(handle,&snapshot) && snapshot.retired && !snapshot.worker_busy);
    handle_release(handle);assert(allocations-frees==baseline);
    api(request_close)(session);tick();drain_workers();assert(status(session).closed);api(release)(session);
    assert(allocations==frees && !critical_depth && !session_mutex_depth);
    return 0;
}
'''
