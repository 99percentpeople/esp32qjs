
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define pdTRUE 1
#define WIFI_DRIVER_EVENT_CONNECT_TIMEOUT 5
static int processed, wakes;
typedef struct { int kind, reason; unsigned status, generation; } esp32_mquickjs_wifi_driver_event_t;
static struct { void *driver_event_queue; atomic_uint dropped_driver_events; } s_wifi_state;
typedef int esp32_mquickjs_runtime_t;
static _Atomic(esp32_mquickjs_runtime_t *) s_wifi_runtime = (void *)1;
static atomic_uint s_wifi_timeout_pending;
static int xQueueSend(void *q, const void *event, int wait) { (void)q; (void)event; (void)wait; return 0; }
static void esp32_mquickjs_notify_activity(void *r) { (void)r; wakes++; }
static void wifi_process_driver_event(const esp32_mquickjs_wifi_driver_event_t *e) { processed = e->kind; }
