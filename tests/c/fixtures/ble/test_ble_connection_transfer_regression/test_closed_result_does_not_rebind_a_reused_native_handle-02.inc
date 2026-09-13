
#define JS_TRUE 1
#define JS_CLASS_BLE_CONNECTION 2
static int s_ble_closed_connection_ref;
static JSValue ble_bool_finish(void *ctx,void *s) { (void)ctx;(void)s;return JS_TRUE; }
static void ble_native_state_complete(void *s) { (void)s; }
static void ble_recycle_connection(ble_connection_slot_t *slot) { slot->allocated=false; }
static void ble_retire_handle(void *ctx,int value,int cls,void *ref) { (void)ctx;(void)value;(void)cls;(void)ref; }
