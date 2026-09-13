
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define BLE_GAP_EVENT_DISCONNECT 1
#define BLE_GAP_EVENT_ENC_CHANGE 2
#define BLE_CONTROL_SECURITY 2
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define BLE_LIFECYCLE_ACTIVE 1
#define BLE_LIFECYCLE_OPENING 2
#define BLE_LIFECYCLE_CLOSING 3
#define BLE_LIFECYCLE_FAILED 4
#define BLE_CONTROL_DISCONNECTED 1
#define BLE_OP_CONNECTION_CLOSE 1
#define BLE_OP_PAIR 2
#define BLE_HS_ENOTCONN 2
#define BLE_INVALID_CONN_HANDLE UINT16_MAX
typedef struct { int operation,host_code;atomic_bool completed; } esp32_mquickjs_future_driver_state_t;
typedef struct {
    bool open,allocated,reserved,release_on_disconnect;int conn_handle,connect_operation;
    unsigned generation;int security;atomic_uint sequence;atomic_bool terminal_pending;
    _Atomic(esp32_mquickjs_future_driver_state_t *) active_gatt_state;
    void *queue;
} ble_connection_slot_t;
typedef struct { unsigned adapter_generation,connection_generation,sequence;int64_t timestamp_us;int kind,status; } ble_connection_event_t;
static struct { int lifecycle,lock;unsigned generation;atomic_uint dropped_connection_events; } s_ble={.lifecycle=BLE_LIFECYCLE_ACTIVE,.generation=1};
static ble_connection_slot_t connection={.open=true,.allocated=true,.generation=1,.conn_handle=7,.connect_operation=1};
static esp32_mquickjs_future_driver_state_t operation={.operation=BLE_OP_CONNECTION_CLOSE};
static int wakes,published,native_completed;
static bool check_future_at_publish;
struct ble_gap_event { int type;struct { struct { int conn_handle; } conn;int reason; } disconnect;struct { int conn_handle,status; } enc_change; };
struct ble_gap_conn_desc { int sec_state; };
static int ble_gap_conn_find(int h,struct ble_gap_conn_desc *d) { (void)h;d->sec_state=42;return 0; }
static ble_connection_slot_t *ble_find_connection(int handle,uint16_t *out) { if(out) *out=0;return handle==connection.conn_handle ? &connection : NULL; }
static esp32_mquickjs_future_driver_state_t *ble_active_state_acquire(_Atomic(esp32_mquickjs_future_driver_state_t *) *p) { return *p; }
static bool esp32_mquickjs_wireless_native_operation_complete(int *p) { *p=0;return true; }
static int64_t esp_timer_get_time(void) { return 42; }
static bool esp32_mquickjs_event_queue_try_send_from_callback(void *q,const void *e) {
    (void)q;(void)e;published++;
    if(((const ble_connection_event_t *)e)->kind==BLE_CONTROL_DISCONNECTED)
        assert(!connection.open && connection.terminal_pending);
    else assert(connection.security==42);
    if(check_future_at_publish) assert(operation.completed);
    return false;
}
static void ble_future_wake_state(void *p) { (void)p;wakes++; }
static void ble_native_state_complete(void *p) { (void)p;native_completed++; }
static void ble_future_state_drop(void *p) { (void)p; }
