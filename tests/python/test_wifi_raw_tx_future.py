"""Deferred production one-shot worker/poll/destructor and native broker tests.

Radio calls and the bounded background queue are injected boundaries. The actual
Radio implementation is exercised separately by test_wifi_raw_tx_radio.py.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_wifi_raw_tx_broker import production_code
from test_wifi_tx_rate import rate_code
from test_wifi_config_controls import ROOT, HEADER, structure
from wireless_vm_fixture import extract
from test_wifi_rx_target import INTERNAL, unit

SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx.c'


class WiFiRawTxFuture(unittest.TestCase):
    def test_queue_rejection_cancellation_late_completion_and_cleanup_suffix(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        source = SOURCE.read_text()
        code = production_code('esp32c5/representative')
        code += rate_code('esp32c5/representative', ('wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t'))
        code = code.replace('static int64_t esp_timer_get_time(void)',
                            'static int64_t fake_now=123456789;\nstatic int64_t esp_timer_get_time(void)')
        code = code.replace('return 123456789;', 'return fake_now;')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_raw_tx_lane.h')
        code += unit(SOURCE.with_name('esp32_mquickjs_wifi_raw_tx_lane.c'))
        code += '#include <stdatomic.h>\n'
        code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', HEADER.read_text()).group(0)
        code += structure(HEADER.read_text(), 'esp32_mquickjs_wifi_radio_lease_t')
        code += structure(source, 'raw_tx_options_t')
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        code += source[start:source.index('\n};', start) + 3]
        start = source.index('static portMUX_TYPE s_retired_lock')
        code += source[start:source.index('static bool raw_tx_retired_pending', start)]
        code += BOUNDARIES
        for name in ['raw_tx_retired_pending', 'raw_tx_cleanup_worker', 'raw_tx_service',
                     'esp32_mquickjs_prepare_wifi_raw_tx_runtime_destroy', 'raw_tx_send_worker',
                     'raw_tx_schedule', 'raw_tx_start', 'raw_tx_poll', 'raw_tx_cancel', 'raw_tx_destroy',
                     'raw_tx_timeout', 'raw_tx_resource']:
            code += extract(source, name)
        with tempfile.TemporaryDirectory() as tmp:
            path, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
            path.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


BOUNDARIES = r'''
typedef int JSContext,esp32_mquickjs_runtime_t;
typedef unsigned esp32_mquickjs_future_token_t;
typedef const void *esp32_mquickjs_resource_key_t;
typedef enum {ESP32_MQUICKJS_FUTURE_PENDING,ESP32_MQUICKJS_FUTURE_READY} esp32_mquickjs_future_poll_t;
typedef enum {ESP32_MQUICKJS_CANCEL_REQUESTED} esp32_mquickjs_cancel_result_t;
static struct {void (*function)(void *);void *opaque;} job;
static bool queue_full,cancel_during_acquire;
static unsigned acquires,releases,stops;
static esp_err_t acquire_error,stop_error;
static esp32_mquickjs_future_driver_state_t *running;
/* One-shot fixture has no Sessions; their production owner/worker is exercised
 * by test_wifi_raw_tx_session.py, sharing the same real arbiter and broker. */
static bool esp32_mquickjs_wifi_raw_tx_sessions_service(void) {return false;}
static bool esp32_mquickjs_wifi_raw_tx_sessions_runtime_service(void) {return false;}
static bool esp32_mquickjs_wifi_raw_tx_periodic_jobs_service(void) {return false;}
static void esp32_mquickjs_wifi_raw_tx_sessions_request_close(void) {}
static bool esp32_mquickjs_wifi_raw_tx_sessions_drained(void) {return true;}
static bool esp32_mquickjs_submit_background_worker(void (*function)(void *),void *opaque) {
    assert(!critical_depth);
    if(queue_full || job.function)return false;
    job.function=function;job.opaque=opaque;return true;
}
static void run_job(void) {
    assert(job.function);void (*function)(void *)=job.function;void *opaque=job.opaque;
    job.function=NULL;job.opaque=NULL;function(opaque);
}
static void esp32_mquickjs_memory_payload_free(void *p) {if(p)heap_caps_free(p);}
static esp_err_t esp32_mquickjs_wifi_radio_raw_tx_acquire(esp32_mquickjs_wifi_raw_tx_interface_t interface,
    uint8_t channel,const wifi_tx_rate_config_t *rate,esp32_mquickjs_wifi_radio_lease_t *lease,uint8_t *actual) {
    assert(!rate && !critical_depth && interface==ESP32_MQUICKJS_WIFI_RAW_TX_STATION && channel==6);
    ++acquires;*lease=(esp32_mquickjs_wifi_radio_lease_t){7,123,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX,true};
    *actual=6;
    if(cancel_during_acquire)atomic_store(&running->cancel_requested,true);
    return acquire_error;
}
static esp_err_t esp32_mquickjs_wifi_radio_raw_tx_submit(const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_raw_tx_interface_t interface,bool driver_sequence,const uint8_t *bytes,size_t length,
    token_t *token,esp32_mquickjs_wifi_raw_tx_validation_t *validation,uint8_t *channel) {
    assert(lease->acquired && *channel==6 && !critical_depth);
    esp32_mquickjs_wifi_raw_tx_validation_policy_t policy={.interface=interface,.driver_sequence=driver_sequence};
    esp_err_t error=esp32_mquickjs_wifi_raw_tx_broker_register(lease->generation);
    if(error)return error;
    sending_token=token;
    error=esp32_mquickjs_wifi_raw_tx_broker_submit(lease->generation,lease->identity,bytes,length,&policy,token,validation);
    sending_token=NULL;return error;
}
static bool esp32_mquickjs_wifi_radio_raw_tx_retire(const esp32_mquickjs_wifi_radio_lease_t *lease,token_t *token) {
    assert(lease->acquired && token->radio_lease_identity==lease->identity && !critical_depth);
    return esp32_mquickjs_wifi_raw_tx_broker_retire(token);
}
static esp_err_t esp32_mquickjs_wifi_radio_release_and_stop_idle(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth);
    if(lease->acquired){++releases;memset(lease,0,sizeof(*lease));}
    ++stops;return stop_error;
}
'''

MAIN = r'''
static esp32_mquickjs_future_driver_state_t *state_new(void) {
    esp32_mquickjs_future_driver_state_t *state=heap_caps_malloc(sizeof(*state),MALLOC_CAP_8BIT);
    memset(state,0,sizeof(*state));atomic_init(&state->worker_done,false);atomic_init(&state->cancel_requested,false);
    state->options=(raw_tx_options_t){.channel=6,.driver_sequence=true,.timeout_ms=1000};
    state->bytes=heap_caps_malloc(24,MALLOC_CAP_8BIT);state->length=24;
    memset(state->bytes,0,24);state->bytes[0]=0x80;memset(state->bytes+4,0x34,6);memset(state->bytes+10,0x12,6);
    return state;
}
static void start(esp32_mquickjs_future_driver_state_t *state) {
    running=state;assert(raw_tx_start(NULL,NULL,0,state));
    assert(raw_tx_timeout(state)==1000 && raw_tx_resource(state));
}
static void service(void) {fake_now+=100001;(void)raw_tx_service(NULL,NULL,NULL);}
int main(void) {
    memset(destination,0x34,6);memset(source_address,0x12,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    /* A different native producer holds the shared grant. One-shot must wait
     * before Radio acquire, and cancelling its waiter cannot release the holder. */
    esp32_mquickjs_wifi_raw_tx_lane_token_t other={0};
    assert(esp32_mquickjs_wifi_raw_tx_lane_request(&other)==ESP32_MQUICKJS_WIFI_RAW_TX_LANE_OK);
    assert(esp32_mquickjs_wifi_raw_tx_lane_acquire(&other));
    esp32_mquickjs_future_driver_state_t *waiting=state_new();start(waiting);
    assert(!waiting->submitted && !acquires && waiting->lane.identity);
    raw_tx_cancel(waiting);assert(raw_tx_poll(waiting)==ESP32_MQUICKJS_FUTURE_READY);raw_tx_destroy(waiting);
    esp32_mquickjs_wifi_raw_tx_lane_status_t lane_status;
    esp32_mquickjs_wifi_raw_tx_lane_status(&lane_status);
    assert(lane_status.active_identity==other.identity && !lane_status.waiting);
    assert(esp32_mquickjs_wifi_raw_tx_lane_release(&other));
    /* No worker queue admission means no native I/O and cancel can drain. */
    esp32_mquickjs_future_driver_state_t *state=state_new();queue_full=true;start(state);
    assert(!state->submitted && !acquires && raw_tx_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);
    assert(raw_tx_cancel(state)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(raw_tx_poll(state)==ESP32_MQUICKJS_FUTURE_READY);raw_tx_destroy(state);
    assert(!raw_tx_retired_pending() && allocations==frees);queue_full=false;
    /* Cancellation observed after acquire prevents SDK send but retains cleanup. */
    state=state_new();cancel_during_acquire=true;start(state);run_job();cancel_during_acquire=false;
    assert(!sends && raw_tx_poll(state)==ESP32_MQUICKJS_FUTURE_READY);raw_tx_destroy(state);
    assert(raw_tx_retired_pending());run_job();assert(!raw_tx_retired_pending() && releases==1);
    /* SDK returned; public timeout/destruction releases captured input, not the driver's copy. */
    state=state_new();start(state);run_job();assert(raw_tx_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);
    unsigned before=frees;token_t token=state->native_token;
    raw_tx_cancel(state);assert(raw_tx_poll(state)==ESP32_MQUICKJS_FUTURE_READY);raw_tx_destroy(state);
    assert(frees==before+2 && raw_tx_retired_pending() && allocations==frees+1);
    assert(esp32_mquickjs_wifi_raw_tx_lane_request(&other)==ESP32_MQUICKJS_WIFI_RAW_TX_LANE_OK);
    assert(!esp32_mquickjs_wifi_raw_tx_lane_acquire(&other));
    run_job();assert(raw_tx_retired_pending() && s_retired.token.identity==token.identity);
    assert(!esp32_mquickjs_prepare_wifi_raw_tx_runtime_destroy());
    /* Another captured Future waits for cleanup; it cannot transmit into quarantine. */
    esp32_mquickjs_future_driver_state_t *next=state_new();unsigned old_sends=sends;
    start(next);assert(!next->submitted && sends==old_sends);
    driver_callback(&callback_info);service();assert(job.function);run_job();
    assert(!raw_tx_retired_pending());
    /* The older producer keeps priority over the next one-shot Future. */
    assert(raw_tx_poll(next)==ESP32_MQUICKJS_FUTURE_PENDING && !next->submitted);
    assert(esp32_mquickjs_wifi_raw_tx_lane_acquire(&other));
    assert(esp32_mquickjs_wifi_raw_tx_lane_release(&other));
    assert(raw_tx_poll(next)==ESP32_MQUICKJS_FUTURE_PENDING && next->submitted);run_job();
    driver_callback(&callback_info);assert(raw_tx_poll(next)==ESP32_MQUICKJS_FUTURE_READY);
    assert(next->result.driver_completed && next->result.token.identity!=token.identity);
    /* Result-conversion failure still invokes this destructor and preserves cleanup. */
    stop_error=87;raw_tx_destroy(next);run_job();
    assert(raw_tx_retired_pending() && !s_retired.lease.acquired && !s_retired.token.identity);
    assert(esp32_mquickjs_wifi_raw_tx_lane_request(&other)==ESP32_MQUICKJS_WIFI_RAW_TX_LANE_OK);
    assert(!esp32_mquickjs_wifi_raw_tx_lane_acquire(&other));
    unsigned old_releases=releases;stop_error=0;service();run_job();
    assert(!raw_tx_retired_pending() && releases==old_releases && allocations==frees);
    assert(esp32_mquickjs_wifi_raw_tx_lane_acquire(&other));
    assert(esp32_mquickjs_wifi_raw_tx_lane_release(&other));
    /* Failed partial acquire cleans its exact lease even without a TX token. */
    acquire_error=88;state=state_new();start(state);run_job();
    assert(raw_tx_poll(state)==ESP32_MQUICKJS_FUTURE_READY && state->error==88);
    raw_tx_destroy(state);run_job();acquire_error=0;
    assert(!raw_tx_retired_pending() && allocations==frees);
    /* Synchronous SDK completion also survives deferred Future conversion. */
    synchronous=true;state=state_new();start(state);run_job();
    assert(raw_tx_poll(state)==ESP32_MQUICKJS_FUTURE_READY && state->result.driver_accepted);
    raw_tx_destroy(state);run_job();assert(esp32_mquickjs_prepare_wifi_raw_tx_runtime_destroy());
    assert(allocations==frees && !critical_depth);
    /* Exercise callback clock/thread helpers from the shared production fixture. */
    start_callback();finish_callback();
    assert(esp32_mquickjs_wifi_raw_tx_broker_unregister(7)==ESP_OK);
    assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(7));
    /* Exhaustion fails before a worker/Radio mutation; it is not an endless
     * retry for a busy lane, and teardown must still be able to finish. */
    s_lane.next_identity=0;
    state=state_new();unsigned previous_acquires=acquires;start(state);
    assert(state->schedule_failed && !state->submitted && !job.function);
    assert(raw_tx_poll(state)==ESP32_MQUICKJS_FUTURE_READY && state->error==ESP_ERR_INVALID_STATE);
    assert(!strcmp(state->stage,"lane-identity-exhausted") && acquires==previous_acquires);
    raw_tx_destroy(state);assert(allocations==frees && !raw_tx_retired_pending());
    return 0;
}
'''
