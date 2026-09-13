
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
