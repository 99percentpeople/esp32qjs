
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define BLE_HS_EALREADY 2
#define BLE_STOP_ERROR 3
typedef struct { bool transferred; uint32_t connection_generation, adapter_generation; void *ctx; } esp32_mquickjs_future_driver_state_t;
static struct { struct { bool active; uint32_t generation; void *queue,*payloads; int queue_ref,stop_reason; bool queue_rooted; } scanner; } s_ble;
static int freed, orphan, roots;
static void ble_scan_callbacks_quiesce(void) { }
static int ble_gap_disc_cancel(void) { return 1; }
static void ble_release_event_queue(void *ctx, void **q, int *r, bool *rooted) { (void)ctx; (void)q; (void)r; (void)rooted; roots++; }
static void heap_caps_free(void *p) { (void)p; freed++; }
static void ble_future_state_release(esp32_mquickjs_future_driver_state_t *s) { (void)s; }
static void ble_request_orphan_close(uint32_t generation) { (void)generation; orphan++; }
