
int main(void) {
    init();struct ble_gap_event e={0};
    assert(ble_gap_event_callback(&e,NULL)==42);
    assert(calls==1 && slot.release_on_disconnect);
    assert(!pthread_mutex_trylock(&mutex));pthread_mutex_unlock(&mutex);
}
