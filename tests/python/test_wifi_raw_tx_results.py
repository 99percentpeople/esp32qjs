"""Deferred exact per-send results over the production native Session stack."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_wifi_raw_tx_session import production_session_code, MAIN as SESSION_MAIN


class WiFiRawTxResults(unittest.TestCase):
    def test_atomic_registration_eviction_completion_unregister_and_stale_tokens(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        # Reuse only allocator/worker driving helpers, not another test scenario.
        code = production_session_code() + SESSION_MAIN[:SESSION_MAIN.index('int main(void)')]
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
            source.write_text(code + MAIN)
            built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                    str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


MAIN = r'''
typedef esp32_mquickjs_wifi_raw_tx_result_record_t record_t;
typedef esp32_mquickjs_wifi_raw_tx_result_token_t result_token_t;
typedef esp32_mquickjs_wifi_raw_tx_result_t result_t;
#define PENDING ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_PENDING
#define COMPLETED ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_COMPLETED
#define DROPPED ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_DROPPED
#define REJECTED ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_REJECTED
#define UNCERTAIN ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_UNCERTAIN
static admission_t observe(session_t *session,record_t *record,result_token_t *token) {
    payload_t frame=packet(),removed[4]={{0}};admission_t admission={0};
    esp32_mquickjs_wifi_raw_tx_validation_t validation;
    assert(api(admit_result)(session,&frame,removed,4,&admission,&validation,record,token)==ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK);
    assert(!frame.data);for(unsigned i=0;i<4;++i)if(removed[i].data)esp32_mquickjs_memory_payload_free(removed[i].data);
    return admission;
}
static result_t result(session_t *session,result_token_t *token) {
    result_t result;assert(api(result_status)(session,token,&result));return result;
}
static void complete_one(void) {
    tick();assert(job_count==1);run_job();assert(s_raw_tx.status.operation_active);
    driver_callback(&callback_info);tick();assert(job_count==1);run_job();
}
int main(void) {
    (void)start_callback;(void)finish_callback;(void)run_job_thread;(void)enqueue;
    memset(destination,0x34,6);memset(source_address,0x12,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    options_t options={.channel=6,.driver_sequence=true,.capacity=1,
        .overflow=ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_DROP_OLDEST_BATCH};
    session_t *session=NULL;assert(api(new)(&options,&session)==ESP_OK);tick();run_job();
    record_t records[8]={{0}},extra_record={0};result_token_t tokens[8]={{0}},extra={0};
    for(unsigned i=0;i<8;++i) {
        admission_t admission=observe(session,&records[i],&tokens[i]);
        assert(admission.first_sequence==i+1 && result(session,&tokens[i]).kind==PENDING);
        for(unsigned j=0;j<i;++j) {
            result_t r=result(session,&tokens[j]);
            assert(r.kind==DROPPED && r.sequence==j+1 && !strcmp(r.stage,"queue-overflow"));
        }
    }
    /* Ready-but-unreleased records count toward the bound. Full registration
     * cannot evict the queued packet or consume the new captured payload. */
    payload_t frame=packet(),removed[4]={{0}};admission_t admission={.first_sequence=99};
    esp32_mquickjs_wifi_raw_tx_validation_t validation;
    assert(api(admit_result)(session,&frame,removed,4,&admission,&validation,&extra_record,&extra)==ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FULL);
    assert(frame.data && !extra.identity && !extra_record.registered && admission.first_sequence==99);
    assert(status(session).totals.admitted==8 && status(session).totals.dropped==7);
    assert(result(session,&tokens[7]).kind==PENDING);esp32_mquickjs_memory_payload_free(frame.data);
    result_token_t stale=tokens[0];assert(api(result_release)(session,&tokens[0]));
    observe(session,&records[0],&tokens[0]);assert(tokens[0].index==stale.index && tokens[0].identity!=stale.identity);
    result_t untouched={.sequence=999};
    assert(!api(result_status)(session,&stale,&untouched) && untouched.sequence==999);
    assert(!api(result_release)(session,&stale));
    complete_one();result_t first=result(session,&tokens[0]);
    assert(first.kind==COMPLETED && first.sequence==9 && first.native.driver_completed && first.channel==6);
    assert(first.native.completion.status==ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS);

    /* The next packet's failed MAC completion must not overwrite this result. */
    assert(api(result_release)(session,&tokens[1]));observe(session,&records[1],&tokens[1]);
    callback_info.tx_status=WIFI_SEND_FAIL;complete_one();
    result_t second=result(session,&tokens[1]),again=result(session,&tokens[0]);
    assert(second.kind==COMPLETED && second.native.completion.status==ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_FAILED);
    assert(first.sequence==again.sequence && first.native.token.identity==again.native.token.identity &&
        first.native.completion.callback_time_us==again.native.completion.callback_time_us &&
        again.native.completion.status==ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS);

    /* Public timeout may unregister before transmission. Poison released
     * storage; later native completion must never access that old record. */
    assert(api(result_release)(session,&tokens[2]));observe(session,&records[2],&tokens[2]);
    assert(api(result_release)(session,&tokens[2]));memset(&records[2],0xa5,sizeof(records[2]));
    callback_info.tx_status=WIFI_SEND_SUCCESS;complete_one();
    for(unsigned i=0;i<sizeof(records[2]);++i)assert(((unsigned char *)&records[2])[i]==0xa5);

    /* Close drops a still queued observation, independently of old completions.
     * Caller can release its Session ref; live results own the closed control. */
    assert(api(result_release)(session,&tokens[3]));observe(session,&records[3],&tokens[3]);
    api(request_close)(session);api(release)(session);tick();run_job();
    assert(status(session).closed);
    assert(result(session,&tokens[3]).kind==DROPPED && !strcmp(result(session,&tokens[3]).stage,"session-close"));
    assert(result(session,&tokens[0]).kind==COMPLETED);
    for(unsigned i=0;i<8;++i)if(tokens[i].identity)assert(api(result_release)(session,&tokens[i]));
    session=NULL;assert(allocations==frees);

    /* Result identity accepts UINT32_MAX once, then fails before queue changes. */
    memset(records,0,sizeof(records));memset(tokens,0,sizeof(tokens));
    assert(api(new)(&options,&session)==ESP_OK);tick();run_job();session->next_result_identity=UINT32_MAX;
    observe(session,&records[0],&tokens[0]);assert(tokens[0].identity==UINT32_MAX);
    frame=packet();memset(removed,0,sizeof(removed));admission=(admission_t){.first_sequence=99};
    assert(api(admit_result)(session,&frame,removed,4,&admission,&validation,&records[1],&tokens[1])==ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_EXHAUSTED);
    assert(frame.data && admission.first_sequence==99 && !tokens[1].identity && status(session).totals.admitted==1);
    esp32_mquickjs_memory_payload_free(frame.data);api(request_close)(session);tick();run_job();
    assert(result(session,&tokens[0]).kind==DROPPED);assert(api(result_release)(session,&tokens[0]));
    api(release)(session);session=NULL;assert(allocations==frees);

    /* A rejection before SDK ownership gives a terminal error and no native
     * token; result release may outlive the caller ref without leaking controls. */
    assert(api(new)(&options,&session)==ESP_OK);tick();run_job();observe(session,&records[0],&tokens[0]);
    fail_alloc=true;tick();run_job();fail_alloc=false;
    result_t rejected=result(session,&tokens[0]);
    assert(rejected.kind==REJECTED && rejected.error==ESP_ERR_NO_MEM && !rejected.native.token.identity);
    assert(status(session).closed && status(session).faulted);
    api(release)(session);assert(api(result_release)(session,&tokens[0]));session=NULL;assert(allocations==frees);

    /* SDK error after token publication finishes the observer as UNCERTAIN,
     * while queue/lease/grant remain pending. Unregister is not an RF cancel. */
    assert(api(new)(&options,&session)==ESP_OK);tick();run_job();observe(session,&records[0],&tokens[0]);
    send_error=89;tick();run_job();result_t uncertain=result(session,&tokens[0]);
    assert(uncertain.kind==UNCERTAIN && uncertain.error==89 && uncertain.native.token.identity);
    assert(status(session).faulted && !status(session).closed && !status(session).totals.settled);
    assert(api(result_release)(session,&tokens[0]));memset(&records[0],0xa5,sizeof(records[0]));
    api(release)(session);session=NULL;driver_callback(&callback_info);tick();run_job();
    for(unsigned i=0;i<sizeof(records[0]);++i)assert(((unsigned char *)&records[0])[i]==0xa5);
    assert(!esp32_mquickjs_wifi_raw_tx_sessions_drained() && allocations==frees+4);
    /* Recovery termination never writes an unregistered result storage. */
    assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(7));tick();run_job();
    assert(allocations==frees);
    for(unsigned i=0;i<sizeof(records[0]);++i)assert(((unsigned char *)&records[0])[i]==0xa5);
    /* A still registered native watcher receives stronger termination evidence.
     * Previously copied public errors are unchanged and no completion is forged. */
    send_error=0;memset(&records[0],0,sizeof(records[0]));
    assert(api(new)(&options,&session)==ESP_OK);tick();run_job();observe(session,&records[0],&tokens[0]);
    send_error=89;tick();run_job();uncertain=result(session,&tokens[0]);
    assert(uncertain.kind==UNCERTAIN);api(release)(session);
    assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(7));tick();run_job();
    result_t terminated=result(session,&tokens[0]);
    assert(terminated.kind==ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_TERMINATED && terminated.native.native_terminated);
    assert(terminated.error==89 && uncertain.kind==UNCERTAIN && status(session).closed);
    assert(status(session).totals.aborted==1 && status(session).totals.settled==1 && !status(session).totals.succeeded);
    assert(api(result_release)(session,&tokens[0]));session=NULL;assert(allocations==frees);
    return 0;
}
'''
