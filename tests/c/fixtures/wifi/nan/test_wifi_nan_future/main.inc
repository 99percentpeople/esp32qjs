
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
