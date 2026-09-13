
int main(void) {
    init();slot.generation=2;
    ble_terminate_unclaimed_connection(&slot,1);
    assert(!calls && slot.open && !slot.release_on_disconnect);
    slot.open=false;ble_terminate_unclaimed_connection(&slot,2);
    assert(!calls && !slot.release_on_disconnect);
}
