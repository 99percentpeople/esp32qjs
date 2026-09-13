
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define BLE_GATT_CHR_PROP_BROADCAST 1
#define BLE_GATT_CHR_PROP_READ 2
#define BLE_GATT_CHR_PROP_WRITE 4
#define BLE_GATT_CHR_PROP_WRITE_NO_RSP 8
#define BLE_GATT_CHR_PROP_NOTIFY 16
#define BLE_GATT_CHR_PROP_INDICATE 32
#define BLE_GATT_CHR_PROP_AUTH_SIGN_WRITE 64
#define BLE_CONTROL_DISCONNECTED 1
#define BLE_CONTROL_MTU 2
#define BLE_CONTROL_SECURITY 3
#define BLE_SM_IOACT_DISP 1
#define BLE_SM_IOACT_INPUT 2
#define BLE_SM_IOACT_NUMCMP 3
typedef struct { uint8_t type,val[6]; } ble_addr_t;
struct ble_gap_sec_state { bool bonded,encrypted,authenticated; };
struct ble_store_key_sec { ble_addr_t peer_addr; };
struct ble_store_value_sec { bool sc; };
static int ble_store_read_peer_sec(void *key,struct ble_store_value_sec *value) { (void)key;value->sc=true;return 0; }
#define BLE_UUID_TEXT_MAX 37
#define BLE_HS_ENOMEM 6
typedef struct { int uuid;unsigned start_handle,end_handle,first_characteristic,characteristic_count; } ble_remote_service_t;
typedef struct { int uuid;unsigned properties,declaration_handle,value_handle,first_descriptor,descriptor_count; } ble_remote_characteristic_t;
typedef struct { int uuid;unsigned handle; } ble_remote_descriptor_t;
static void ble_uuid_to_text(const int *uuid,char *out) { (void)uuid;strcpy(out,"00001800-0000-1000-8000-00805f9b34fb"); }
typedef struct { unsigned connection_index;int host_code; } esp32_mquickjs_future_driver_state_t;
static JSValue ble_throw_error(JSContext *ctx,const char *code,int h,int a,int c,int attr) {
    (void)code;(void)h;(void)a;(void)c;(void)attr;return JS_ThrowInternalError(ctx,"fixture host error");
}
typedef struct {
    unsigned service_count,characteristic_count,descriptor_count,discovery_generation;
    ble_remote_service_t services[1];ble_remote_characteristic_t characteristics[1];ble_remote_descriptor_t descriptors[1];
    bool open,central,rssi_valid; uint16_t mtu; int rssi;
    ble_addr_t peer;struct ble_gap_sec_state security;
    unsigned generation;atomic_bool terminal_pending;atomic_uint gatt_pending;_Atomic(void *) active_gatt_state;
} ble_connection_slot_t;
typedef ble_connection_slot_t ble_connection_snapshot_t;
typedef struct {
    unsigned adapter_generation,connection_generation,sequence,mtu,request_id,passkey;
    int kind,status,pairing_action;int64_t timestamp_us,expires_at_us;
} ble_connection_event_t;
static JSValue new_int64(JSContext *ctx,int64_t value) {
    if(step(ctx)) return JS_ThrowOutOfMemory(ctx);
    return JS_NewInt64(ctx,value);
}
#define JS_NewInt64 new_int64
