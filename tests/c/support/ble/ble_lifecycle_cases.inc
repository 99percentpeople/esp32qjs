static esp32_mquickjs_future_driver_state_t *operation(unsigned slot_index)
{
    esp32_mquickjs_future_driver_state_t *s = calloc(1, sizeof(*s));
    assert(s);
    s->connection_index = slot_index;
    s->connection_generation = s_ble.connections[slot_index].generation;
    s->owner_rooted = s->queue_rooted = true;
    return s;
}
int main(void)
{
    s_ble.connections[0].generation = 10;
    s_ble.connections[1].generation = 20;
    esp32_mquickjs_future_driver_state_t *a = operation(0), *b = operation(0);
    assert(ble_gatt_state_bind(&s_ble.connections[0], a));
    void *old_cookie = (void *)(uintptr_t)a->native_identity;
    /* Callback enters, Future times out/destroys, callback is still in flight. */
    esp32_mquickjs_future_driver_state_t *inflight = ble_operation_acquire(old_cookie);
    assert(inflight == a);
    ble_future_state_release(a);
    assert(deleted_roots == 2 && freed == 0);
    assert(!ble_gatt_state_bind(&s_ble.connections[0], b));
    assert(ble_gatt_state_bind(&s_ble.connections[1], operation(1)));
    ble_future_wake_state(inflight);
    assert(wakes == 0);
    ble_future_state_drop(inflight);
    assert(freed == 0); /* Still has native ownership after callback exit. */
    struct ble_gatt_error ok = {0};
    assert(ble_exchange_mtu_callback(7, &ok, 100, old_cookie) == BLE_HS_EAPP);
    assert(freed == 1 && s_ble.connections[0].mtu == 0);
    assert(ble_gatt_state_bind(&s_ble.connections[0], b));
    void *new_cookie = (void *)(uintptr_t)b->native_identity;
    assert(new_cookie != old_cookie);
    ble_exchange_mtu_callback(7, &ok, 120, old_cookie);
    assert(!b->completed && wakes == 0 && s_ble.connections[0].mtu == 0);
    ble_exchange_mtu_callback(7, &ok, 140, new_cookie);
    assert(b->completed && wakes == 1 && s_ble.connections[0].mtu == 140);
    ble_future_state_release(b);
    assert(freed == 2);
    /* Teardown: roots detach first; storage lasts until successful Host barrier. */
    esp32_mquickjs_future_driver_state_t *other = atomic_load(&s_ble.connections[1].active_gatt_state);
    ble_future_state_release(other);
    assert(freed == 2);
    ble_native_states_quiesced();
    assert(freed == 3 && deleted_roots == 6);
    ble_native_states_quiesced();
    assert(freed == 3);
    /* Each discovery callback must abort a detached cookie without cache access. */
    for (unsigned kind = 0; kind < 3; ++kind) {
        esp32_mquickjs_future_driver_state_t *discovery = operation(0);
        assert(ble_gatt_state_bind(&s_ble.connections[0], discovery));
        void *cookie = (void *)(uintptr_t)discovery->native_identity;
        ble_future_state_release(discovery);
        int rc;
        struct ble_gatt_dsc dsc = {0};
        struct ble_gatt_chr chr = {0};
        struct ble_gatt_svc svc = {0};
        if (kind == 0) rc = ble_discover_descriptor_callback(7, &ok, 42, &dsc, cookie);
        else if (kind == 1) rc = ble_discover_characteristic_callback(7, &ok, &chr, cookie);
        else rc = ble_discover_service_callback(7, &ok, &svc, cookie);
        assert(rc == BLE_HS_EAPP && !atomic_load(&s_ble.connections[0].active_gatt_state));
    }
    assert(freed == 6 && deleted_roots == 12 && wakes == 1);
    /* Bounded allocation and permanent ID exhaustion. */
    int owners[ESP32_MQUICKJS_WIRELESS_OPERATION_CAPACITY];
    uint32_t ids[ESP32_MQUICKJS_WIRELESS_OPERATION_CAPACITY];
    for (unsigned i=0; i<ESP32_MQUICKJS_WIRELESS_OPERATION_CAPACITY; i++) {
        ids[i] = esp32_mquickjs_wireless_operation_register(&s_ble_operations, &owners[i]);
        assert(ids[i]);
    }
    assert(!esp32_mquickjs_wireless_operation_register(&s_ble_operations, owners));
    for (unsigned i=0; i<ESP32_MQUICKJS_WIRELESS_OPERATION_CAPACITY; i++) {
        assert(esp32_mquickjs_wireless_operation_remove(&s_ble_operations, ids[i]) == &owners[i]);
        assert(!esp32_mquickjs_wireless_operation_lookup(&s_ble_operations, ids[i]));
    }
    s_ble_operations.last_identity=UINT32_MAX;
    assert(!esp32_mquickjs_wireless_operation_register(&s_ble_operations, owners));
    return 0;
}
