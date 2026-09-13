
static void *callback_thread(void *unused) {
    (void)unused;
    struct ble_gap_event event={BLE_GAP_EVENT_DISC};
    saved_callback(&event,saved_arg);
    return NULL;
}
int main(void) {
    esp32_mquickjs_future_driver_state_t state={.adapter_generation=1,.connection_generation=1};
    s_ble.scanner.generation=1; s_ble.scanner.payloads=(void *)1;
    assert(ble_scan_start(NULL,NULL,1,&state));
    pthread_t thread;
    assert(!pthread_create(&thread,NULL,callback_thread,NULL));
    pthread_mutex_lock(&schedule_lock);
    while (!entered) pthread_cond_wait(&schedule_cond,&schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    assert(ble_scan_callback_bind()==NULL);
    /* The SDK callback is inside the adapter when the runtime closes it. */
    ble_release_scanner(&s_ble);
    assert(!pthread_join(thread,NULL));
    assert(freed==1 && delivered==1 && s_ble_scan_callback_identity==0);
    assert(s_ble_scan_callback_refs==0);
    struct ble_gap_event event={BLE_GAP_EVENT_DISC_COMPLETE};
    saved_callback(&event,saved_arg);
    assert(ble_scan_callback_bind()!=NULL);
}
