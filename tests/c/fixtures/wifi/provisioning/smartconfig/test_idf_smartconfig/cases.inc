
static void run_task(void) {
    assert(scheduled);void (*fn)(void *)=scheduled;void *arg=scheduled_arg;
    scheduled=NULL;scheduled_arg=NULL;fn(arg);
    for(int i=0;i<4;i++)assert(!allocations[i].p);
}
static void reset_boundaries(void) {
    assert(!scheduled && !s_sc_ack.busy);creates=deletes=posts=sends=closes=delay_count=stop_on_delay=0;
    queue_error=create_error=mac_error=socket_error=send_error=0;
    immediate=missing_netif=allocation_error=false;
}
int main(void) {
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,NULL)==ESP_ERR_INVALID_ARG);
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH_AIRKISS,1,phone)==ESP_ERR_INVALID_ARG);
    allocation_error=true;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_NO_MEM && !s_sc_ack.identity);
    reset_boundaries();create_error=true;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_NO_MEM);
    assert(!s_sc_ack.busy && s_sc_ack.error==ESP_ERR_NO_MEM && s_sc_ack.identity==1);

    reset_boundaries();
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);
    uint64_t old=s_sc_ack.identity;
    sc_send_ack_stop();sc_send_ack_stop();
    assert(s_sc_ack.busy && !sc_ack_allowed(old));
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_INVALID_STATE);
    run_task();assert(!s_sc_ack.busy && !posts && !sends && !closes);
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH_V2,3,phone)==ESP_OK && s_sc_ack.identity>old);
    sc_ack_finish(old,ESP_FAIL,123);sc_ack_complete(old);
    assert(s_sc_ack.busy && !s_sc_ack.completed && !posts);
    queue_error=ESP_ERR_TIMEOUT;run_task();
    assert(!s_sc_ack.busy && s_sc_ack.completed && s_sc_ack.error==ESP_OK);
    assert(s_sc_ack.observation_error==ESP_ERR_TIMEOUT && posts==1 && sends==30 && closes==1);

    reset_boundaries();immediate=true;
    assert(sc_send_ack_start(SC_TYPE_AIRKISS,1,phone)==ESP_OK);
    assert(!s_sc_ack.busy && s_sc_ack.completed && deletes==1 && sends==60);
    reset_boundaries();mac_error=ESP_FAIL;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);run_task();
    assert(s_sc_ack.error==ESP_FAIL && !s_sc_ack.completed && !closes);
    reset_boundaries();socket_error=true;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);run_task();
    assert(s_sc_ack.error==ESP_FAIL && s_sc_ack.socket_errno==ENOBUFS && !posts && !closes);
    reset_boundaries();send_error=true;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);run_task();
    assert(s_sc_ack.error==ESP_FAIL && s_sc_ack.socket_errno==EIO && !posts && closes==1);
    reset_boundaries();stop_on_delay=2;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);run_task();
    assert(!s_sc_ack.busy && s_sc_ack.stopping && !sends && !posts && closes==1);
    reset_boundaries();missing_netif=true;stop_on_delay=3;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_OK);run_task();
    assert(!s_sc_ack.busy && !sends && !closes);
    reset_boundaries();
    assert(esp32qjs_smartconfig_ack_reserve(21)==ESP_OK);
    assert(esp32qjs_smartconfig_ack_reserve(22)==ESP_ERR_INVALID_STATE);
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_INVALID_STATE);
    uint64_t receipt=0;
    assert(esp32qjs_smartconfig_ack_start(22,SC_TYPE_ESPTOUCH,1,phone,&receipt)==ESP_ERR_INVALID_STATE && !receipt);
    assert(esp32qjs_smartconfig_ack_start(21,SC_TYPE_ESPTOUCH,1,phone,&receipt)==ESP_OK && receipt);
    sc_send_ack_stop();assert(!s_sc_ack.stopping); /* Foreign legacy stop is not consent. */
    assert(esp32qjs_smartconfig_ack_release(21)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_smartconfig_ack_stop(21,receipt-1)==ESP_ERR_INVALID_STATE && !s_sc_ack.stopping);
    assert(esp32qjs_smartconfig_ack_stop(22,receipt)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_smartconfig_ack_stop(21,receipt)==ESP_OK);
    run_task();assert(!s_sc_ack.busy && !sends);
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_smartconfig_ack_release(22)==ESP_ERR_INVALID_STATE);
    assert(esp32qjs_smartconfig_ack_release(21)==ESP_OK);
    reset_boundaries();create_error=true;receipt=0;
    assert(esp32qjs_smartconfig_ack_reserve(23)==ESP_OK);
    assert(esp32qjs_smartconfig_ack_start(23,SC_TYPE_ESPTOUCH,1,phone,&receipt)==ESP_ERR_NO_MEM && receipt);
    assert(esp32qjs_smartconfig_ack_start(23,SC_TYPE_ESPTOUCH,1,phone,&receipt)==ESP_ERR_INVALID_ARG);
    assert(esp32qjs_smartconfig_ack_stop(23,receipt)==ESP_OK);
    assert(esp32qjs_smartconfig_ack_release(23)==ESP_OK);
    reset_boundaries();s_sc_ack.identity=UINT64_MAX;
    assert(sc_send_ack_start(SC_TYPE_ESPTOUCH,1,phone)==ESP_ERR_NO_MEM && !s_sc_ack.busy && !creates);
    return 0;
}
