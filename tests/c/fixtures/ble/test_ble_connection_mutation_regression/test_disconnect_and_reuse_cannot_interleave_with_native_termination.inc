
int main(void) {
    init();contended=true;
    pthread_t thread;assert(!pthread_create(&thread,NULL,host,NULL));
    ble_terminate_unclaimed_connection(&slot, 1);
    assert(!pthread_join(thread,NULL));
    assert(slot.generation==2 && slot.open && slot.allocated && !slot.release_on_disconnect);
    assert(calls==1);
}
