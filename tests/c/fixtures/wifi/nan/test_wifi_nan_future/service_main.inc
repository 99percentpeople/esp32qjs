
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
