
static void *callback_thread(void *arg) {
    (void)arg;
    struct ble_gap_event event={.type=BLE_GAP_EVENT_ADV_COMPLETE};
    saved_callback(&event,saved_arg);return NULL;
}
int main(void) {
    esp32_mquickjs_future_driver_state_t state={0};
    assert(ble_advertise_start(NULL,NULL,1,&state));
    pthread_t thread;
    assert(!pthread_create(&thread,NULL,callback_thread,NULL));
    pthread_mutex_lock(&schedule_lock);
    while(!entered) pthread_cond_wait(&schedule_cond,&schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    assert(ble_advertise_callback_bind()==NULL);
    ble_release_advertiser(&s_ble);
    assert(!pthread_join(thread,NULL));
    assert(closed==1 && !s_ble_advertise_callback_refs && !s_ble_advertise_callback_identity);
}
