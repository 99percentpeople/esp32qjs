
#define esp32_mquickjs_memory_payload_free heap_caps_free
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#ifdef THREADED
#include <pthread.h>
#include <sched.h>
static pthread_mutex_t state_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t schedule_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t schedule_cond=PTHREAD_COND_INITIALIZER;
static bool entered, resume_callback;
#endif
#define BLE_LIFECYCLE_ACTIVE 1
#define BLE_HS_EINVAL 2
#define BLE_HS_EBUSY 3
#define BLE_STOP_ERROR 4
#define BLE_GAP_EVENT_DISC_COMPLETE 5
#define BLE_GAP_EVENT_DISC 6
typedef void JSContext;
typedef void esp32_mquickjs_runtime_t;
typedef int esp32_mquickjs_future_token_t;
typedef struct { int generation, stop_reason, queue_ref; bool active, queue_rooted; void *queue, *payloads; int64_t started_at_us; } ble_scanner_t;
typedef struct { int lifecycle, generation, own_addr_type, lock; JSContext *ctx; ble_scanner_t scanner; } ble_adapter_t;
typedef struct { int adapter_generation, connection_generation, token, duration_ms, scan_params, host_code; void *runtime; bool started; atomic_bool completed; } esp32_mquickjs_future_driver_state_t;
static ble_adapter_t s_ble = {.lifecycle=BLE_LIFECYCLE_ACTIVE, .generation=1};
struct ble_gap_event { int type; };
typedef int (*callback_t)(struct ble_gap_event *, void *);
static callback_t saved_callback;
static void *saved_arg;
static int immediate, driver_result, driver_calls, delivered, freed, wakes;
static uintptr_t s_ble_scan_next_identity=1, s_ble_scan_callback_identity;
static uint32_t s_ble_scan_callback_refs;
static _Thread_local int lock_depth;
#ifdef THREADED
#define taskENTER_CRITICAL(lock) do { (void)(lock); pthread_mutex_lock(&state_lock); assert(lock_depth++ == 0); } while (0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock); assert(--lock_depth == 0); pthread_mutex_unlock(&state_lock); } while (0)
#else
#define taskENTER_CRITICAL(lock) do { (void)(lock); assert(lock_depth++ == 0); } while (0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock); assert(--lock_depth == 0); } while (0)
#endif
static void vTaskDelay(int ticks) {
    (void)ticks; assert(!lock_depth);
#ifdef THREADED
    assert(freed==0);
    pthread_mutex_lock(&schedule_lock);
    resume_callback=true;
    pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    sched_yield();
#endif
}
static int ble_gap_event_callback(struct ble_gap_event *event, void *arg) {
    (void)arg;
    assert(!lock_depth);
#ifdef THREADED
    pthread_mutex_lock(&schedule_lock);
    entered=true;
    pthread_cond_broadcast(&schedule_cond);
    while (!resume_callback) pthread_cond_wait(&schedule_cond,&schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
#endif
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) s_ble.scanner.active=false;
    else if (s_ble.scanner.active) delivered++;
    return 0;
}
static int ble_gap_disc(int addr, int duration, const int *params, callback_t cb, void *arg) {
    (void)addr; (void)duration; (void)params;
    assert(!lock_depth);
    driver_calls++;
    saved_callback=cb; saved_arg=arg;
    if (immediate) { struct ble_gap_event event={BLE_GAP_EVENT_DISC_COMPLETE}; cb(&event,arg); }
    return driver_result;
}
static void ble_throw_error(void *ctx, const char *code, int rc, int a, int b, int c) { (void)ctx; (void)code; (void)rc; (void)a; (void)b; (void)c; }
static int64_t esp_timer_get_time(void) { return 1; }
static bool esp32_mquickjs_future_wake(void *runtime, int token) { (void)runtime; (void)token; wakes++; return true; }
static void ble_release_event_queue(void *ctx, void **q, int *ref, bool *rooted) { (void)ctx; (void)q; (void)ref; (void)rooted; }
static void heap_caps_free(void *ptr) { assert(s_ble_scan_callback_refs==0); if (ptr) freed++; }
