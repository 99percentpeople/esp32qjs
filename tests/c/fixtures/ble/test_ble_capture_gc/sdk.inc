
#include "mquickjs_priv.h"
#include "esp32_mquickjs_native_pool.h"
#define CONFIG_ESP32_MQUICKJS_BLE_SCAN_QUEUE_LEN 2
#define CONFIG_ESP32_MQUICKJS_BLE_SCAN_QUEUE_MAX_LEN 4
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES 64
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_SERVICES 2
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_CHARACTERISTICS 2
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_DESCRIPTORS 2
#define CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU 247
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS 1
#define BLE_HS_ADV_MAX_SZ 31
#define MALLOC_CAP_INTERNAL 2
#define ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL 2
#define ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST 0
#define BLE_HS_EBUSY 1
#define BLE_HS_EINVAL 2
#define BLE_HS_EMSGSIZE 3
#define BLE_DEFAULT_TIMEOUT_MS 5000
#define BLE_HS_FOREVER INT32_MAX
#define BLE_STOP_RUNNING 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
typedef enum { BLE_OP_SCAN,BLE_OP_SCANNER_CLOSE,BLE_OP_GATT_READ,BLE_OP_GATT_WRITE,BLE_OP_EXCHANGE_MTU,BLE_OP_READ_RSSI,BLE_OP_CONNECTION_CLOSE,BLE_OP_DISCOVER } ble_operation_t;
typedef int esp32_mquickjs_event_queue_t;
typedef struct { int unused; } ble_scan_event_t;
typedef struct {
    JSContext *ctx;ble_operation_t operation;unsigned max_services,max_characteristics,max_descriptors,adapter_generation,connection_generation,connection_index,timeout_ms,duration_ms,attribute_handle,max_bytes,mtu,payload_length,native_identity,callback_refs;
    bool include_descriptors,response,owner_rooted,queue_rooted,gatt_accounted,gatt_started,detached;
    JSGCRef owner_ref,queue_ref;atomic_bool completed;uint8_t *payload;
    struct { unsigned itvl,window,passive,filter_duplicates,limited; } scan_params;
} esp32_mquickjs_future_driver_state_t;
typedef struct { int *services,*characteristics,*descriptors;unsigned service_count,characteristic_count,descriptor_count,discovery_generation;unsigned generation;atomic_uint gatt_pending;_Atomic(esp32_mquickjs_future_driver_state_t *) active_gap_state,active_gatt_state; } ble_connection_slot_t;
typedef struct { unsigned adapter_generation,index,generation; } ble_connection_ref_t;
typedef struct { bool allocated,open,queue_rooted;unsigned generation,capacity,stop_reason;atomic_uint sequence,reports,dropped,malformed;uint8_t *payloads;esp32_mquickjs_native_pool_t free_slots;JSGCRef queue_ref;esp32_mquickjs_event_queue_t *queue; } ble_scanner_t;
typedef struct { unsigned generation,preferred_mtu,max_connections;void *runtime;int lock;struct { bool active; } advertiser;ble_scanner_t scanner;ble_connection_slot_t connections[1]; } ble_adapter_t;
static ble_adapter_t s_ble;
static atomic_uint s_ble_next_scanner_generation=1;
static _Atomic(esp32_mquickjs_future_driver_state_t *) s_ble_open_state,s_ble_server_notify_state;
static ble_connection_ref_t connection_ref={1,0,1};
static ble_adapter_t *ble_adapter_from_value(JSContext *ctx,JSValue v,bool check) { (void)ctx;(void)v;(void)check;return &s_ble; }
static ble_connection_slot_t *ble_connection_from_value(JSContext *ctx,JSValue v,ble_connection_ref_t **ref,bool check) { (void)ctx;(void)v;(void)check;if(ref)*ref=&connection_ref;return &s_ble.connections[0]; }
static ble_scanner_t *ble_scanner_from_value(JSContext *ctx,JSValue v,bool check) { (void)ctx;(void)v;(void)check;return &s_ble.scanner; }
static bool ble_remote_attribute_exists(ble_connection_slot_t *s,uint16_t h,void *out) { (void)s;(void)out;return h==1; }
static bool ble_has_pending_connects(void) { return false; }
static JSValue ble_throw_error(JSContext *ctx,const char *s,int a,int b,int c,int d) { (void)a;(void)b;(void)c;(void)d;return JS_ThrowInternalError(ctx,"%s",s); }
static JSValue ble_scan_event_to_js(JSContext *ctx,const void *event,void *opaque) { (void)ctx;(void)event;(void)opaque;return JS_UNDEFINED; }
static void ble_scan_event_drop(void *event,void *opaque) { (void)event;(void)opaque; }
static int queue_native;
static JSValue esp32_mquickjs_event_queue_new(JSContext *ctx,void *runtime,size_t size,unsigned capacity,int policy,JSValue (*convert)(JSContext *,const void *,void *),void (*drop)(void *,void *),void *close,void *opaque) {
    (void)runtime;(void)size;(void)capacity;(void)policy;(void)convert;(void)drop;(void)close;(void)opaque;return JS_NewObject(ctx);
}
static esp32_mquickjs_event_queue_t *esp32_mquickjs_event_queue_from_value(JSContext *ctx,JSValue v) { (void)ctx;(void)v;return &queue_native; }
static bool esp32_mquickjs_event_queue_dispose(JSContext *ctx,JSValue v) { (void)ctx;(void)v;return true; }
static bool esp32_mquickjs_event_queue_close(void *p) { (void)p;return true; }
static bool esp32_mquickjs_event_queue_discard_all(void *p) { (void)p;return true; }
/* Intrinsic receives a rooted argv pointer, so compaction here is valid. */
static JSValue test_keys(JSContext *ctx,JSValue *self,int argc,JSValue *argv) {
    if(step(ctx))return JS_ThrowOutOfMemory(ctx);return js_object_keys(ctx,self,argc,argv);
}
#define js_object_keys test_keys
