"""Deferred production periodic owner + Session/queue/arbiter/broker coverage.

Only timer, allocator, clock, task queue/locks and Radio/SDK calls are injected.
No JS poller is needed to drive the timer path after initial native admission.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_wifi_raw_tx_session import production_session_code, MAIN as SESSION_MAIN, RAW
from test_wifi_rx_target import INTERNAL, unit


def production_periodic_job_code():
    code = production_session_code()
    # This adapter adds an outer job mutex; retain allocator/SDK assertions
    # that forbid either mutex during I/O. No Session->job lock is allowed.
    code = code.replace('assert(!critical_depth && !session_mutex_depth);\n    int error=wait',
                        'assert(!critical_depth && session_mutex_depth<2);\n    int error=wait')
    code = code.replace('assert(session_mutex_depth==1 && !critical_depth);--session_mutex_depth;',
                        'assert(session_mutex_depth>=1 && session_mutex_depth<=2 && !critical_depth);--session_mutex_depth;')
    for name in ['periodic', 'periodic_job']:
        code += unit(INTERNAL / ('esp32_mquickjs_wifi_raw_tx_' + name + '.h'))
    code += TIMERS
    for name in ['periodic', 'periodic_job']:
        code += unit(RAW / ('esp32_mquickjs_wifi_raw_tx_' + name + '.c'))
    code += SESSION_MAIN[:SESSION_MAIN.index('int main(void)')]
    return code


class WiFiRawTxPeriodicJob(unittest.TestCase):
    def test_autonomous_timer_backpressure_close_and_cleanup_suffix(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        code = production_periodic_job_code()
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory) / 'fixture.c', Path(directory) / 'fixture'
            source.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


TIMERS = r'''
#define ESP_TIMER_TASK 0
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 1
static bool in_timer;
static unsigned timer_creates,timer_stops,timer_deletes;
static esp_err_t create_error,start_error,timer_stop_error,delete_error;
typedef struct {void (*callback)(void *);void *arg;bool allocated,active;} fake_timer_t;
typedef fake_timer_t *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;int dispatch_method;const char *name;bool skip_unhandled_events;} esp_timer_create_args_t;
static fake_timer_t timers[8];
static esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *output) {
    assert(!in_timer && !critical_depth && !session_mutex_depth && !*output);++timer_creates;
    assert(args->dispatch_method==ESP_TIMER_TASK && args->skip_unhandled_events);
    if(create_error)return create_error;
    for(unsigned i=0;i<8;++i)if(!timers[i].allocated){timers[i]=(fake_timer_t){args->callback,args->arg,true,false};*output=&timers[i];return ESP_OK;}
    return ESP_ERR_NO_MEM;
}
static esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer,uint64_t us) {
    assert(!in_timer && !critical_depth && !session_mutex_depth && timer->allocated && us==1000);
    if(start_error) return start_error;
    timer->active=true;return ESP_OK;
}
static esp_err_t esp_timer_stop_blocking(esp_timer_handle_t timer,uint32_t wait) {
    assert(!in_timer && !critical_depth && !session_mutex_depth && timer->allocated && wait==1);
    ++timer_stops;timer->active=false;return timer_stop_error;
}
static esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    assert(!in_timer && !critical_depth && !session_mutex_depth && timer->allocated && !timer->active);
    ++timer_deletes;if(delete_error)return delete_error;
    timer->allocated=false;return ESP_OK;
}
static void fire_timer(esp_timer_handle_t timer) {
    assert(timer->allocated && timer->active && !in_timer && !job_count);
    fake_now+=1000;unsigned before_alloc=allocations,before_free=frees;
    in_timer=true;timer->callback(timer->arg);in_timer=false;
    assert(allocations==before_alloc && frees==before_free && job_count<=1);
}
static void drain_workers(void) {unsigned n=0;while(job_count){assert(++n<40);run_job();}}
'''

MAIN = r'''
#define job_api(name) esp32_mquickjs_wifi_raw_tx_periodic_job_##name
static esp32_mquickjs_wifi_raw_tx_periodic_job_status_t job_status(job_t *job) {
    esp32_mquickjs_wifi_raw_tx_periodic_job_status_t result;assert(job_api(status)(job,&result));return result;
}
static job_t *open_job(session_t *session,uint32_t count) {
    payload_t frame=packet();job_t *job=NULL;
    esp32_mquickjs_wifi_raw_tx_periodic_options_t options={.interval_us=1000,.count=count,.stop_on_error=true};
    assert(job_api(new)(session,&frame,&options,&job)==ESP_OK && !frame.data);
    assert(!job_status(job).ready && !job_status(job).retired);
    assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();return job;
}
int main(void) {
    (void)start_callback;(void)finish_callback;(void)run_job_thread;(void)enqueue;
    memset(destination,0x34,6);memset(source_address,0x12,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    options_t options={.channel=6,.driver_sequence=true,.capacity=4,
        .overflow=ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_DROP_OLDEST_BATCH};
    session_t *session=NULL;assert(api(new)(&options,&session)==ESP_OK);tick();drain_workers();
    job_t *job=open_job(session,0);assert(job_status(job).ready && sends==1 && job->result_token.identity);
    assert(session->periodic_children==1);esp_timer_handle_t timer=job->timer;
    /* Timer queues native work without a JS poller; previous SDK packet remains
     * owned. Another deadline is skipped rather than accumulating a second. */
    fire_timer(timer);drain_workers();assert(sends==1 && job_status(job).ledger.submitted==1);
    assert(job_status(job).ledger.skipped_busy==1);
    driver_callback(&callback_info);fire_timer(timer);drain_workers();
    assert(!s_raw_tx.status.operation_active); /* Session worker retires actual completion */
    fire_timer(timer);drain_workers();assert(sends==2 && job_status(job).ledger.completed==1);
    /* Queue saturation in the timer path retains the single native hold. */
    queue_full=true;fire_timer(timer);assert(!job_count && !job_status(job).worker_busy);queue_full=false;
    /* Parent close retains its child reservation through timer deletion, even
     * though queue completion/Radio release may have finished first. */
    api(request_close)(session);driver_callback(&callback_info);delete_error=91;
    fire_timer(timer);drain_workers();assert(!status(session).closed);
    fire_timer(timer);drain_workers();
    assert(job_status(job).ledger.completed==2 && job_status(job).cleanup_error==91);
    assert(job_status(job).timer_quiesced && !job_status(job).retired && !status(session).closed);
    esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t summary;
    esp32_mquickjs_wifi_raw_tx_periodic_jobs_status(&summary);
    assert(summary.live==1 && summary.cleanup_pending==1 && summary.cleanup_error==91);
    assert(summary.error_generation==job_status(job).ledger.generation);
    unsigned before_stops=timer_stops;delete_error=0;fake_now+=100001;
    assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();
    assert(job_status(job).retired && timer_stops==before_stops && !session->periodic_children);
    tick();drain_workers();assert(status(session).closed);
    job_api(close)(job);job_api(release)(job);api(release)(session);assert(allocations==frees);

    /* No callback after timer retirement can refer to a later owner's storage.
     * Failed timer starts also own the stop/delete suffix before Session release. */
    session=NULL;assert(api(new)(&options,&session)==ESP_OK);tick();drain_workers();
    start_error=92;job=open_job(session,1);
    assert(job_status(job).retired && job_status(job).error==92 && !session->periodic_children);
    job_api(release)(job);start_error=0;
    payload_t frame=packet();job=NULL;
    esp32_mquickjs_wifi_raw_tx_periodic_options_t periodic={.interval_us=1000};
    assert(job_api(new)(session,&frame,&periodic,&job)==ESP_OK);
    unsigned before_create=timer_creates;job_api(stop)(job);
    assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();
    assert(job_status(job).retired && timer_creates==before_create);job_api(release)(job);job=NULL;
    /* Nth control allocation, mutex failure and invalid template preserve the
     * caller frame; failed creation never reserves a permanent child. */
    frame=packet();job=NULL;fail_control_at=control_allocations+1;
    assert(job_api(new)(session,&frame,&periodic,&job)==ESP_ERR_NO_MEM && frame.data && !job);
    fail_control_at=0;fail_mutex=true;
    assert(job_api(new)(session,&frame,&periodic,&job)==ESP_ERR_NO_MEM && frame.data && !job);
    fail_mutex=false;frame.data[1]=0x40;
    assert(job_api(new)(session,&frame,&periodic,&job)!=ESP_OK && !session->periodic_children && frame.data);
    esp32_mquickjs_memory_payload_free(frame.data);
    /* Registry and Session reservations are bounded and recover on release. */
    job_t *many[8]={0};
    for(unsigned i=0;i<8;++i) {frame=packet();assert(job_api(new)(session,&frame,&periodic,&many[i])==ESP_OK);}
    frame=packet();assert(job_api(new)(session,&frame,&periodic,&job)!=ESP_OK && frame.data && !job);
    esp32_mquickjs_memory_payload_free(frame.data);assert(session->periodic_children==8);
    for(unsigned i=0;i<8;++i)job_api(stop)(many[i]);
    assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();
    assert(!session->periodic_children);
    for(unsigned i=0;i<8;++i){assert(job_status(many[i]).retired);job_api(release)(many[i]);}
    /* Packet-copy OOM is a no-submit rejection. The template remains owned
     * until timer quiescence; stopOnError retires this failed periodic child. */
    frame=packet();periodic.stop_on_error=true;
    assert(job_api(new)(session,&frame,&periodic,&job)==ESP_OK);unsigned before_send=sends;
    fail_alloc=true;assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();fail_alloc=false;
    assert(job_status(job).retired && job_status(job).error==ESP_ERR_NO_MEM && sends==before_send);
    job_api(release)(job);job=NULL;
    /* End-to-end native proof: an uncertain periodic result cannot retire
     * before deinit. After injected physical proof it drains Session + timer. */
    send_error=89;job=open_job(session,0);timer=job->timer;
    fire_timer(timer);drain_workers();
    assert(job_status(job).ledger.uncertain && !job_status(job).retired && job->result_token.identity);
    assert(!status(session).closed && session->periodic_children==1);
    assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(7));
    tick();drain_workers();fire_timer(timer);drain_workers();tick();drain_workers();
    assert(job_status(job).retired && !job_status(job).ledger.uncertain && job_status(job).ledger.aborted==1);
    assert(!job_status(job).ledger.completed && status(session).closed && !session->periodic_children);
    job_api(release)(job);job=NULL;api(release)(session);session=NULL;assert(allocations==frees);
    send_error=0;assert(api(new)(&options,&session)==ESP_OK);tick();drain_workers();
    /* Boot identities do not wrap through native/JS lifecycle reuse. */
    s_next_job_generation=UINT32_MAX;frame=packet();
    assert(job_api(new)(session,&frame,&periodic,&job)==ESP_OK && job_status(job).ledger.generation==UINT32_MAX);
    job_api(stop)(job);assert(esp32_mquickjs_wifi_raw_tx_periodic_jobs_service());drain_workers();job_api(release)(job);job=NULL;
    frame=packet();assert(job_api(new)(session,&frame,&periodic,&job)!=ESP_OK && frame.data && !session->periodic_children);
    esp32_mquickjs_memory_payload_free(frame.data);
    api(request_close)(session);tick();drain_workers();api(release)(session);assert(allocations==frees);
    esp32_mquickjs_wifi_raw_tx_periodic_jobs_status(&summary);
    assert(!summary.live && !summary.retired && summary.identity_exhausted);
    assert(!critical_depth && !session_mutex_depth && !in_timer);
    return 0;
}
'''
