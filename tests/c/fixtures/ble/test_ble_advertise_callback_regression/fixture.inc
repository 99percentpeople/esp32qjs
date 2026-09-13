
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#ifdef THREADED
#include <pthread.h>
#include <sched.h>
static pthread_mutex_t state_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t schedule_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t schedule_cond=PTHREAD_COND_INITIALIZER;
static bool entered,resume_callback;
#endif
#define portMAX_DELAY 0
static int s_ble_connection_mutex;
#define xSemaphoreTakeRecursive(m,t) ((void)(m),(void)(t))
#define xSemaphoreGiveRecursive(m) ((void)(m))
#define BLE_LIFECYCLE_ACTIVE 1
#define BLE_HS_EINVAL 9
#define BLE_STOP_ERROR 1
#define BLE_HS_EBUSY 2
#define BLE_HS_ENOTCONN 3
#define BLE_ERR_REM_USER_CONN_TERM 4
#define BLE_GAP_EVENT_ADV_COMPLETE 5
#define BLE_GAP_EVENT_CONNECT 6
#define BLE_GAP_EVENT_DISCONNECT 7
#define BLE_GAP_EVENT_MTU 8
#define BLE_INVALID_CONN_HANDLE UINT16_MAX
typedef void JSContext;
typedef void esp32_mquickjs_runtime_t;
typedef int esp32_mquickjs_future_token_t;
typedef struct { bool allocated,active,queue_rooted; int generation,stop_reason,queue_ref; void *queue; } ble_advertiser_t;
typedef struct { bool allocated,reserved,open,release_on_disconnect; uint16_t conn_handle; } ble_connection_slot_t;
typedef struct { void *ctx; int lock,own_addr_type,lifecycle,generation; ble_advertiser_t advertiser; atomic_int rejected_connection_error; } ble_adapter_t;
typedef struct { void *runtime; int token,adapter_generation,connection_generation; bool started; uint8_t adv_data[31],scan_response[31]; uint8_t adv_data_length,scan_response_length; int duration_ms,adv_params,host_code; atomic_bool completed; } esp32_mquickjs_future_driver_state_t;
static ble_adapter_t s_ble={.lifecycle=BLE_LIFECYCLE_ACTIVE};
struct ble_gap_event { int type; struct { int status; uint16_t conn_handle; } connect; };
typedef int (*callback_t)(struct ble_gap_event *,void *);
static callback_t saved_callback;
static void *saved_arg;
static int immediate,driver_result,data_result,rsp_result,driver_calls,delivered,closed,wakes,terminated,terminate_result;
static uint16_t terminated_handle;
static uintptr_t s_ble_advertise_next_identity=1,s_ble_advertise_callback_identity;
static uint32_t s_ble_advertise_callback_refs;
static _Thread_local int lock_depth;
#ifdef THREADED
#define taskENTER_CRITICAL(lock) do { (void)(lock);pthread_mutex_lock(&state_lock);assert(lock_depth++==0); } while(0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock);assert(--lock_depth==0);pthread_mutex_unlock(&state_lock); } while(0)
#else
#define taskENTER_CRITICAL(lock) do { (void)(lock);assert(lock_depth++==0); } while(0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock);assert(--lock_depth==0); } while(0)
#endif
static void vTaskDelay(int ticks) {
    (void)ticks;assert(!lock_depth);
#ifdef THREADED
    assert(closed==0);
    pthread_mutex_lock(&schedule_lock);
    resume_callback=true;
    pthread_cond_broadcast(&schedule_cond);
    pthread_mutex_unlock(&schedule_lock);
    sched_yield();
#endif
}
static ble_connection_slot_t existing,rejected;
static bool no_slot;
static ble_connection_slot_t *ble_find_connection(uint16_t handle,uint16_t *index) {
    (void)index;
    if(existing.allocated && existing.conn_handle==handle) return &existing;
    if(rejected.allocated && rejected.conn_handle==handle) return &rejected;
    return NULL;
}
static ble_connection_slot_t *ble_reserve_connection(bool central,uint16_t *index) {
    (void)central;(void)index;
    return no_slot ? NULL : &rejected;
}
static int ble_gap_terminate(uint16_t handle,int reason) {
    (void)reason;assert(!lock_depth);terminated++;terminated_handle=handle;return terminate_result;
}
static int ble_gap_event_callback(struct ble_gap_event *event,void *arg) {
    (void)arg;
#ifdef THREADED
    pthread_mutex_lock(&schedule_lock);
    entered=true;
    pthread_cond_broadcast(&schedule_cond);
    while(!resume_callback) pthread_cond_wait(&schedule_cond,&schedule_lock);
    pthread_mutex_unlock(&schedule_lock);
    assert(closed==0);
#endif
    if(event->type==BLE_GAP_EVENT_ADV_COMPLETE) s_ble.advertiser.active=false;
    else delivered++;
    return 0;
}
static int ble_gap_adv_set_data(const uint8_t *p,int n) { (void)p;(void)n;return data_result; }
static int ble_gap_adv_rsp_set_data(const uint8_t *p,int n) { (void)p;(void)n;return rsp_result; }
static int ble_gap_adv_start(int addr,void *peer,int duration,void *params,callback_t cb,void *arg) {
    (void)addr;(void)peer;(void)duration;(void)params;driver_calls++;
    saved_callback=cb;saved_arg=arg;
    if(immediate) { struct ble_gap_event event={.type=BLE_GAP_EVENT_ADV_COMPLETE};cb(&event,arg); }
    return driver_result;
}
static void ble_throw_error(void *ctx,const char *code,int rc,int a,int b,int c) { (void)ctx;(void)code;(void)rc;(void)a;(void)b;(void)c; }
static bool esp32_mquickjs_future_wake(void *r,int t) { (void)r;(void)t;wakes++;return true; }
static void ble_discard_advertiser_connections(void *ad) { (void)ad; }
static void ble_release_event_queue(void *ctx,void **q,int *ref,bool *rooted) { (void)ctx;(void)q;(void)ref;(void)rooted;assert(!s_ble_advertise_callback_refs);closed++; }
