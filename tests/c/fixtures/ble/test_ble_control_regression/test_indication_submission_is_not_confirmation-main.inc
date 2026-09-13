
} }
int main(void) {
    esp32_mquickjs_future_driver_state_t a = {.indication=true, .attribute_handle=42,
        .pending_confirmations=1, .notify_handles={7,0}, .notify_pending={true,false}};
    atomic_store(&s_ble_server_notify_state, &a);
    struct event event = {.type=BLE_GAP_EVENT_NOTIFY_TX, .notify_tx={true,42,7,0}};
    callback(&event);
    assert(!atomic_load(&a.completed));
    assert(atomic_load(&a.pending_confirmations)==1);
    assert(wakes==0);
    event.notify_tx.status=BLE_HS_EDONE;
    event.notify_tx.conn_handle=8;
    callback(&event);
    assert(!atomic_load(&a.completed));
    event.notify_tx.conn_handle=7;
    callback(&event);
    assert(atomic_load(&a.completed));
    assert(atomic_load(&a.pending_confirmations)==0);
    callback(&event);
    assert(atomic_load(&a.pending_confirmations)==0);
    assert(wakes==1);
}
