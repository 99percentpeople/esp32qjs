
#define BLE_LIFECYCLE_CLOSED 0
#define BLE_LIFECYCLE_FAILED 2
#define BLE_CONNECT_CANCEL_QUIESCE_MS 10
#define ESP_LOGE(...) ((void)0)
static struct { atomic_bool worker_submitted,worker_completed; } s_ble_orphan_close;
static int cleaned,freed,reset,cleanup_result;
static void vTaskDelay(int n) { (void)n;assert(0); }
static int ble_gap_disc_cancel(void) { return 0; }
static int ble_gap_adv_stop(void) { return 0; }
static bool ble_wait_for_pending_connects(int ms) {
    (void)ms;assert(terminated>0);
    return s_ble.connections[0].connect_operation==0;
}
static int ble_cleanup_native(void *ad) { (void)ad;cleaned++;return cleanup_result; }
static void ble_free_pools(void *ad) { (void)ad;freed++; }
static void ble_reset_orphan_close(void) { reset++; }
