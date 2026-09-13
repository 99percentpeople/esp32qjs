
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#define portMAX_DELAY 0
static int s_ble_connection_mutex,connection_lock_depth;
#define xSemaphoreTakeRecursive(m,t) ((void)(m),(void)(t),connection_lock_depth++)
#define xSemaphoreGiveRecursive(m) ((void)(m),assert(connection_lock_depth>0),connection_lock_depth--)
#define BLE_HS_ENOTCONN 2
#define BLE_ERR_REM_USER_CONN_TERM 3
#define BLE_INVALID_CONN_HANDLE UINT16_MAX
#define ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST 0
#define BLE_STOP_CONNECTED 1
typedef struct { unsigned generation,connection_index,connection_generation,sequence; int64_t timestamp_us; } ble_advertiser_event_t;
typedef struct { unsigned generation; int queue_ref,stop_reason; bool queue_rooted,active; atomic_uint sequence,incoming,dropped; void *queue; } ble_advertiser_t;
typedef struct { bool open,allocated,reserved,release_on_disconnect; atomic_bool incoming_pending; unsigned generation; atomic_uint incoming_advertiser_generation; int conn_handle,connect_operation; } ble_connection_slot_t;
typedef struct { int lock; bool host_started; void *ctx,*runtime; unsigned max_connections; ble_advertiser_t advertiser; ble_connection_slot_t connections[3]; atomic_int rejected_connection_error; } ble_adapter_t;
static ble_adapter_t s_ble={.max_connections=3,.host_started=true};
static int terminated,terminate_result,quiesced;
static int last_handle,lock_depth;
#define taskENTER_CRITICAL(lock) do { (void)(lock);assert(lock_depth++==0); } while(0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock);assert(--lock_depth==0); } while(0)
static void (*registered_drop)(void *,void *);
static void *registered_opaque;
static bool queue_full;
static ble_advertiser_event_t queued;
static int64_t esp_timer_get_time(void) { return 123; }
static bool esp32_mquickjs_event_queue_try_send_from_callback(void *q,const void *e) {
    (void)q;queued=*(const ble_advertiser_event_t *)e;return !queue_full;
}
static int ble_gap_terminate(int h,int reason) { (void)reason;assert(!lock_depth);terminated++;last_handle=h;return terminate_result; }
static bool esp32_mquickjs_wireless_native_operation_complete(int *s) { *s=0;return true; }
static void ble_advertise_callbacks_quiesce(void) { quiesced++; }
static void ble_release_event_queue(void *ctx,void **q,int *r,bool *rooted) { (void)ctx;(void)q;(void)r;(void)rooted;assert(quiesced); }
static void ble_advertiser_event_to_js(void) {}
static void *esp32_mquickjs_event_queue_new_wireless(const char *owner,void *ctx,void *runtime,size_t size,unsigned capacity,int overflow,void (*convert)(void),void (*drop)(void *,void *),void *unused,void *opaque) {
    (void)owner;(void)ctx;(void)runtime;(void)size;(void)capacity;(void)overflow;(void)convert;(void)unused;
    registered_drop=drop;registered_opaque=opaque;return (void *)1;
}
