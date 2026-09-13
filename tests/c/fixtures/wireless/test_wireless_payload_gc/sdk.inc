
#include "esp32_mquickjs_native_pool.h"
#define BLE_ADDR_PUBLIC 0
#define BLE_ADDR_RANDOM 1
#define BLE_ADDR_PUBLIC_ID 2
#define BLE_ADDR_RANDOM_ID 3
#define BLE_HS_ADV_MAX_SZ 31
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES 64
#define BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP 4
#define BLE_HCI_ADV_RPT_EVTYPE_DIR_IND 1
#define BLE_HCI_ADV_RPT_EVTYPE_ADV_IND 0
#define BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND 2
#define BLE_SERVER_EVENT_WRITE 1
#define ESPNOW_ADDRESS_BYTES 6
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 0
static const uint8_t s_broadcast_address[6]={255,255,255,255,255,255};
typedef struct { uint8_t type,val[6]; } ble_addr_t;
typedef struct { unsigned generation,capacity;uint8_t *payloads;esp32_mquickjs_native_pool_t free_slots; } ble_scanner_t;
typedef ble_scanner_t ble_subscription_t;
typedef struct { unsigned generation,pool_index,length,sequence,event_type;int rssi;int64_t timestamp_us;ble_addr_t peer; } ble_scan_event_t;
typedef struct { unsigned subscription_generation,pool_index,length,sequence;int64_t timestamp_us;bool indication; } ble_notification_event_t;
typedef struct { unsigned generation;bool open,allocated;uint16_t conn_handle;JSGCRef queue_ref; } ble_connection_slot_t;
typedef struct { unsigned adapter_generation,index,generation; } ble_connection_ref_t;
static ble_connection_ref_t s_ble_closed_connection_ref;
static struct { unsigned generation,max_connections;ble_connection_slot_t connections[1]; } s_ble;
static unsigned orphans;
static void ble_request_orphan_close(unsigned generation) {assert(generation==s_ble.generation);orphans++;}
typedef struct { unsigned event_capacity,characteristic_count;uint8_t *event_payloads;esp32_mquickjs_native_pool_t free_slots;struct { const char *id; } characteristics[1]; } ble_gatt_server_t;
typedef struct { unsigned adapter_generation,characteristic_index,conn_handle,kind,pool_index,length,sequence,offset;int64_t timestamp_us;bool notify,indicate; } ble_server_event_t;
typedef struct { unsigned generation,sequence,length;int64_t timestamp_us;int rssi;unsigned channel;uint8_t source[6],destination[6];uint8_t payload[64]; } espnow_rx_slot_t;
typedef struct { unsigned generation,receive_capacity,max_payload_bytes;espnow_rx_slot_t *rx_slots;esp32_mquickjs_native_pool_t rx_free; } espnow_session_t;
typedef struct { unsigned generation,slot_index; } espnow_receive_event_t;
static void *esp32_mquickjs_memory_payload_alloc(const char *name,size_t n,int policy) { (void)name;return heap_caps_malloc(n,policy); }
