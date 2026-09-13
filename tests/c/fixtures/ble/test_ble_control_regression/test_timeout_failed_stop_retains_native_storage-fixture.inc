
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define BLE_HS_EALREADY 2
#define BLE_STOP_ERROR 3
#define BLE_OP_SCAN 1
#define BLE_OP_SCANNER_CLOSE 2
#define BLE_OP_ADVERTISE 3
#define BLE_OP_ADVERTISER_CLOSE 4
typedef struct { bool transferred; uint32_t connection_generation, adapter_generation; void *ctx; } esp32_mquickjs_future_driver_state_t;
static struct { struct { bool active; uint32_t generation; void *queue,*payloads; int queue_ref,stop_reason; bool queue_rooted; } scanner, advertiser; } s_ble;
static int freed, orphan, roots, stop_result;
static void ble_scan_callbacks_quiesce(void) { }
static void ble_advertise_callbacks_quiesce(void) { }
static void ble_discard_advertiser_connections(void *ad) { (void)ad; }
static int ble_gap_disc_cancel(void) { return stop_result; }
static int ble_gap_adv_stop(void) { return stop_result; }
static void ble_release_event_queue(void *ctx, void **q, int *r, bool *rooted) { (void)ctx; (void)q; (void)r; (void)rooted; roots++; }
static void heap_caps_free(void *p) { (void)p; freed++; }
static void ble_future_state_release(esp32_mquickjs_future_driver_state_t *s) { (void)s; }
static void ble_request_orphan_close(uint32_t generation) { (void)generation; orphan++; }
static void timeout_branch(int operation) { switch(operation) {
