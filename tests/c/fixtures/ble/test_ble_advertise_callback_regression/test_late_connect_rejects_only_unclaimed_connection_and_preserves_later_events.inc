
int main(void) {
    esp32_mquickjs_future_driver_state_t state={0};
    assert(ble_advertise_start(NULL,NULL,1,&state));
    callback_t old_cb=saved_callback;void *old_arg=saved_arg;
    ble_release_advertiser(&s_ble);
    assert(ble_advertise_start(NULL,NULL,2,&state));
    existing=(ble_connection_slot_t){.allocated=true,.open=true,.conn_handle=9};
    struct ble_gap_event event={.type=BLE_GAP_EVENT_CONNECT,.connect={.status=0,.conn_handle=7}};
    old_cb(&event,old_arg);
    assert(terminated==1 && terminated_handle==7 && delivered==0);
    assert(rejected.allocated && rejected.open && rejected.release_on_disconnect);
    assert(existing.open && s_ble.advertiser.active);
    event.connect.conn_handle=9;
    old_cb(&event,old_arg);
    assert(terminated==1); /* Duplicate for an already handed-off live owner. */
    event.type=BLE_GAP_EVENT_DISCONNECT;
    old_cb(&event,old_arg);
    event.type=BLE_GAP_EVENT_MTU;
    old_cb(&event,old_arg);
    assert(delivered==2); /* Connections keep their inherited callback. */
}
