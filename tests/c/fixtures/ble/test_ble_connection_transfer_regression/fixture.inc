
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#define BLE_HS_EINVAL 1
#define portMAX_DELAY 0
static int s_ble_connection_mutex,connection_lock_depth;
#define xSemaphoreTakeRecursive(m,t) ((void)(m),(void)(t),connection_lock_depth++)
#define xSemaphoreGiveRecursive(m) ((void)(m),assert(connection_lock_depth>0),connection_lock_depth--)
#define BLE_HS_ENOTCONN 2
#define BLE_ERR_REM_USER_CONN_TERM 3
#define BLE_INVALID_CONN_HANDLE UINT16_MAX
#define JS_EXCEPTION -1
#define JS_IsException(v) ((v)==JS_EXCEPTION)
typedef void JSContext;
typedef int JSValue;
typedef struct { unsigned connection_index,connection_generation; int host_code; bool transferred; } esp32_mquickjs_future_driver_state_t;
typedef struct { bool allocated,open,reserved,release_on_disconnect; unsigned generation; int conn_handle,active_gap_state,connect_operation; } ble_connection_slot_t;
static struct { unsigned max_connections; ble_connection_slot_t connections[2]; atomic_int rejected_connection_error; } s_ble={.max_connections=2};
static int result=JS_EXCEPTION,terminate_result,terminated,released;
static JSValue ble_throw_error(void *ctx,const char *code,int rc,int a,int b,int c) { (void)ctx;(void)code;(void)rc;(void)a;(void)b;(void)c;return JS_EXCEPTION; }
static JSValue ble_new_connection_handle(void *ctx,unsigned i,ble_connection_slot_t *s) { (void)ctx;(void)i;(void)s;return result; }
static void ble_gap_event_callback(void) {}
static int ble_gap_set_event_cb(int handle,void (*cb)(void),void *arg) { (void)handle;(void)cb;(void)arg;return 0; }
static void ble_active_state_bind(int *slot,void *state) { (void)slot;(void)state; }
static int ble_gap_terminate(int handle,int reason) { (void)handle;(void)reason;terminated++;return terminate_result; }
static bool esp32_mquickjs_wireless_native_operation_begin(int *state) { *state=1;return true; }
static bool esp32_mquickjs_wireless_native_operation_request_cancel(int *state) { (void)state;return true; }
static bool esp32_mquickjs_wireless_native_operation_complete(int *state) { *state=0;return true; }
static bool esp32_mquickjs_wireless_native_operation_is_quiescent(int *state) { return *state==0; }
static void ble_future_state_release(void *state) { (void)state;released++; }
