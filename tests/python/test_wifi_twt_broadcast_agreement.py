"""Deferred production broadcast Radio/Future tests with injected SDK scheduling.

Uses the actual lease registry, admission/status/close and shared Future helpers.
SDK outcomes and retirement proof are boundaries, not a substitute for RF, full
Future-core/VM/GC or real retirement-coordinator coverage. Do not run yet.
"""
import unittest
from test_wifi_twt_agreement_radio import agreement_radio_code, MAIN as RADIO_MAIN
from test_wifi_twt_agreement_future import agreement_future_code
from test_wireless_control_regression import compile_run


class WiFiBroadcastAgreement(unittest.TestCase):
    def test_duplicate_stale_tokens_allocation_and_retirement(self):
        code = agreement_radio_code() + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
        compile_run(self, code + RADIO_CASES)

    def test_future_cancel_close_runtime_cleanup_and_native_completion(self):
        code = agreement_future_code()
        compile_run(self, code + FUTURE_CASES)


RADIO_CASES = r'''
int main(void) {
    individual_reset();
    esp32_mquickjs_wifi_btwt_options_t options={.config={.setup_cmd=TWT_REQUEST,.btwt_id=1,.timeout_time_ms=5000},.timeout_ms=6000};
    esp32_mquickjs_wifi_twt_token_t a={0},b={0},stale;
    allocation_fails=true;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a)==ESP_ERR_NO_MEM);
    assert(!a.identity && !broadcast_submits && !individual_owners());allocation_fails=false;
    broadcast_early=true;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a));
    assert(a.identity && broadcast_submits==1 && individual_owners()==1);
    /* Regression: duplicate ID must not return ESP_OK with a zero token. */
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&b)==ESP_ERR_INVALID_STATE);
    assert(!b.identity && broadcast_submits==1 && individual_owners()==1);
    esp32_mquickjs_wifi_radio_lease_t borrowed=s_twt_broadcast[1]->lease;
    wifi_radio_operation_lock();wifi_radio_release_locked(&borrowed);wifi_radio_operation_unlock();assert(borrowed.acquired);
    options.config.btwt_id=2;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&b));
    stale=a;++stale.generation;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_request_close(&stale));
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_close(&stale)==ESP_ERR_INVALID_STATE);
    native_individual.broadcast_id_bitmap=1U<<1;
    broadcast_retire_error=ESP_ERR_NOT_FINISHED;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_close(&a)==ESP_ERR_NOT_FINISHED && broadcast_teardowns==1);
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_close(&a)==ESP_ERR_NOT_FINISHED && broadcast_teardowns==1);
    esp32_mquickjs_wifi_twt_broadcast_radio_state_t status;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_status(&a,&status));
    assert(status.closing && status.dialog_attempts_remaining==254 && status.teardown_attempted);
    stale=a;broadcast_retire_error=0;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&a) && !a.identity && individual_owners()==1);
    options.config.btwt_id=1;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a) && a.identity!=stale.identity);
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_request_close(&stale));
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_status(&stale,&status));
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_close(&stale)==ESP_ERR_INVALID_STATE);
    assert(individual_owners()==2);
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&a));
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&b));
    /* Exhaustion is per physical-boot ID and must precede any new lease/SDK call. */
    broadcast_attempts[1]=255;unsigned submits_before=broadcast_submits;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a)==ESP_ERR_NO_MEM);
    assert(!a.identity && !individual_owners() && broadcast_submits==submits_before);
    options.config.btwt_id=2;broadcast_accept=false;broadcast_submit_error=77;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a)==77 && a.identity);
    unsigned retires_before=broadcast_retires;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&a) && broadcast_retires==retires_before);
    /* Unconfirmed handoff cannot release another owner's channel/Radio lease. */
    broadcast_accept=true;broadcast_submit_error=0;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a));
    s_twt_broadcast[2]->dispatch.handoff_error=78;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_close(&a)==78 && individual_owners()==1);
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_status(&a,&status) && status.cleanup_error==78);
    s_twt_broadcast[2]->dispatch.handoff_error=0;
    broadcast_results[2].native_closed=true;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&a));
    assert(!locks && !critical && !individual_owners());free(s_twt_broadcast);return 0;
}
'''

FUTURE_CASES = r'''
static esp32_mquickjs_future_driver_state_t *capture_broadcast(unsigned slot) {
    esp32_mquickjs_future_driver_state_t *state=calloc(1,sizeof(*state));assert(state);
    state->handle=calloc(1,sizeof(*state->handle));assert(state->handle);
    state->broadcast=true;
    state->broadcast_options=(esp32_mquickjs_wifi_btwt_options_t){.config={.setup_cmd=TWT_REQUEST,.btwt_id=slot,
        .timeout_time_ms=5000},.timeout_ms=6000};
    atomic_init(&state->worker_done,false);atomic_init(&state->cancel_requested,false);return state;
}
int main(void) {
    individual_reset();
    esp32_mquickjs_future_driver_state_t *state=capture_broadcast(1);
    assert(agreement_timeout(state)==6000 && agreement_start(NULL,NULL,0,state));
    assert(agreement_cancel(state)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);work();
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && !broadcast_submits);
    agreement_destroy(state);assert(!individual_owners());
    state=capture_broadcast(1);assert(agreement_start(NULL,NULL,0,state));work();
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING && individual_owners()==1);
    broadcast_results[1].seen=true;broadcast_results[1].complete=true;broadcast_results[1].tx_busy=true;
    broadcast_results[1].event=(wifi_event_sta_btwt_setup_t){.status=BTWT_SETUP_SUCCESS,.setup_cmd=TWT_ACCEPT,
        .btwt_id=1,.min_wake_dura=1,.wake_invl_mant=100};
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);
    broadcast_results[1].tx_busy=false;broadcast_results[1].observation_error=99;
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && !state->error);
    /* Failed object construction still owns native storage after public completion. */
    broadcast_retire_error=ESP_ERR_NOT_FINISHED;agreement_destroy(state);assert(worker);work();
    assert(individual_owners()==1 && !esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy());
    broadcast_retire_error=0;now_us+=100000;
    assert(esp32_mquickjs_wifi_twt_agreement_service());work();
    assert(!individual_owners() && esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy());
    state=capture_broadcast(2);assert(agreement_start(NULL,NULL,0,state));work();
    broadcast_results[2].complete=true;broadcast_results[2].native_error=81;
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && state->error==81);
    agreement_destroy(state);now_us+=100000;(void)esp32_mquickjs_wifi_twt_agreement_service();work();
    state=capture_broadcast(3);assert(agreement_start(NULL,NULL,0,state));work();
    state->close=true;state->submitted=false;
    assert(agreement_start(NULL,NULL,0,state));
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);
    now_us+=100000;assert(esp32_mquickjs_wifi_twt_agreement_service());work();
    assert(agreement_poll(state)==ESP32_MQUICKJS_FUTURE_READY && !state->error);
    agreement_destroy(state);
    assert(!locks && !critical && !individual_owners());free(s_twt_broadcast);return 0;
}
'''
