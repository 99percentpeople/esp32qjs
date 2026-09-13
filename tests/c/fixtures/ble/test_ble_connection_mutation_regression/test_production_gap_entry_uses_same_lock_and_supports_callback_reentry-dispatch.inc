
struct ble_gap_event { int type; };
static int ble_gap_event_callback_locked(struct ble_gap_event *e,void *arg) {
    (void)e;(void)arg;
    /* A callback may synchronously reject a connection using this same lock. */
    driver_result=0;ble_terminate_unclaimed_connection(&slot,1);
    return 42;
}
