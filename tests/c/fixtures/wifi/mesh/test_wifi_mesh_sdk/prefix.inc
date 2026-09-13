
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_SOC_WIFI_MESH_SUPPORT 1
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define ESP_OK 0
#define ESP_ERR_NO_MEM 257
#define ESP_ERR_INVALID_ARG 258
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_INVALID_SIZE 260
#define ESP_ERR_NOT_FOUND 261
#define ESP_ERR_TIMEOUT 263
#define ESP_ERR_INVALID_RESPONSE 264
#define MESH_MTU 1500
#define MESH_MPS 1472
#define MESH_DATA_P2P 2
#define MESH_DATA_FROMDS 4
#define MESH_DATA_TODS 8
#define MESH_DATA_NONBLOCK 16
#define MESH_DATA_DROP 32
#define MESH_OPT_SEND_GROUP 7
#define MESH_OPT_RECV_DS_ADDR 8
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 1
#define ESP32_MQUICKJS_MEMORY_BUDGET_POOL 2
typedef int esp_err_t,portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned lock_depth;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!lock_depth);++lock_depth;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(lock_depth==1);--lock_depth;}while(0)
typedef enum {MESH_TOPO_TREE,MESH_TOPO_CHAIN} esp_mesh_topology_t;
#define MESH_ROOT_LAYER 1
typedef struct {uint8_t ssid[32],password[64],bssid[6],channel;bool bssid_set;}wifi_sta_config_t;
typedef union {wifi_sta_config_t sta;}wifi_config_t;
typedef struct {uint8_t ssid[33],bssid[6],primary;int8_t rssi;}wifi_ap_record_t;
typedef enum {WIFI_SCAN_TYPE_ACTIVE,WIFI_SCAN_TYPE_PASSIVE}wifi_scan_type_t;
typedef struct {uint8_t*ssid,*bssid,channel;bool show_hidden;wifi_scan_type_t scan_type;
 struct {struct {uint32_t min,max;}active;uint32_t passive;}scan_time;uint8_t home_chan_dwell_time;
 struct {uint16_t ghz_2_channels;uint32_t ghz_5_channels;}channel_bitmap;bool coex_background_scan;}wifi_scan_config_t;
typedef enum {MESH_IDLE,MESH_ROOT,MESH_NODE,MESH_LEAF,MESH_STA} mesh_type_t;
typedef enum {MESH_PROTO_BIN,MESH_PROTO_HTTP,MESH_PROTO_JSON,MESH_PROTO_MQTT,MESH_PROTO_AP,MESH_PROTO_STA} mesh_proto_t;
typedef enum {MESH_TOS_P2P,MESH_TOS_E2E,MESH_TOS_DEF} mesh_tos_t;
typedef enum {WIFI_AUTH_OPEN,WIFI_AUTH_WEP,WIFI_AUTH_WPA_PSK,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA_WPA2_PSK} wifi_auth_mode_t;
typedef union{uint8_t addr[6];struct{struct{uint32_t addr;}ip4;uint16_t port;}mip;}mesh_addr_t;
typedef struct{float percentage;bool is_rc_specified;union{int attempts;mesh_addr_t rc_addr;}config;}mesh_vote_t;
#define MESH_PS_DEVICE_DUTY_REQUEST 1
#define MESH_PS_DEVICE_DUTY_DEMAND 4
#define MESH_PS_NETWORK_DUTY_APPLIED_ENTIRE 0
#define MESH_VOTE_REASON_ROOT_INITIATED 1
typedef struct{int unused;}mesh_crypto_funcs_t;
static const mesh_crypto_funcs_t g_wifi_default_mesh_crypto_funcs={0};
typedef struct{uint8_t ssid[32],ssid_len,bssid[6],password[64];bool allow_router_switch;}mesh_router_t;
typedef struct{uint8_t password[64],max_connection,nonmesh_max_connection;}mesh_ap_cfg_t;
typedef struct{uint8_t channel;bool allow_channel_switch;mesh_addr_t mesh_id;mesh_router_t router;
 mesh_ap_cfg_t mesh_ap;const mesh_crypto_funcs_t*crypto_funcs;}mesh_cfg_t;
typedef struct{int to_parent,to_parent_p2p,to_child,to_child_p2p,mgmt,broadcast;}mesh_tx_pending_t;
typedef struct{int toDS,toSelf;}mesh_rx_pending_t;
typedef struct{uint8_t*data;uint16_t size;mesh_proto_t proto;mesh_tos_t tos;}mesh_data_t;
typedef struct{uint8_t type;uint16_t len;uint8_t*val;}mesh_opt_t;
enum{MESH_EVENT_STARTED,MESH_EVENT_STOPPED,MESH_EVENT_CHANNEL_SWITCH,MESH_EVENT_CHILD_CONNECTED,
 MESH_EVENT_CHILD_DISCONNECTED,MESH_EVENT_ROUTING_TABLE_ADD,MESH_EVENT_ROUTING_TABLE_REMOVE,
 MESH_EVENT_PARENT_CONNECTED,MESH_EVENT_PARENT_DISCONNECTED,MESH_EVENT_NO_PARENT_FOUND,
 MESH_EVENT_LAYER_CHANGE,MESH_EVENT_TODS_STATE,MESH_EVENT_VOTE_STARTED,MESH_EVENT_VOTE_STOPPED,
 MESH_EVENT_ROOT_ADDRESS,MESH_EVENT_ROOT_SWITCH_REQ,MESH_EVENT_ROOT_SWITCH_ACK,MESH_EVENT_ROOT_ASKED_YIELD,
 MESH_EVENT_ROOT_FIXED,MESH_EVENT_SCAN_DONE,MESH_EVENT_NETWORK_STATE,MESH_EVENT_STOP_RECONNECTION,
 MESH_EVENT_FIND_NETWORK,MESH_EVENT_ROUTER_SWITCH,MESH_EVENT_PS_PARENT_DUTY,MESH_EVENT_PS_CHILD_DUTY,
 MESH_EVENT_PS_DEVICE_DUTY,MESH_EVENT_MAX};
typedef struct{uint8_t bssid[6],channel;} wifi_event_sta_connected_t;
typedef struct{wifi_event_sta_connected_t connected;uint16_t self_layer;}mesh_event_connected_t;
typedef struct{uint8_t bssid[6];int reason;}mesh_event_disconnected_t;
typedef struct{uint8_t mac[6];}mesh_event_child_connected_t,mesh_event_child_disconnected_t;
typedef struct{uint8_t channel;}mesh_event_channel_switch_t;
typedef struct{uint16_t new_layer;}mesh_event_layer_change_t;
typedef enum{MESH_TODS_UNREACHABLE,MESH_TODS_REACHABLE}mesh_event_toDS_state_t;
typedef mesh_addr_t mesh_event_root_address_t;
typedef struct{int reason;mesh_addr_t rc_addr;}mesh_event_root_switch_req_t,mesh_event_vote_started_t;
typedef struct{uint16_t rt_size_new,rt_size_change;}mesh_event_routing_table_change_t;
typedef struct{bool is_fixed;}mesh_event_root_fixed_t;
typedef struct{bool is_rootless;}mesh_event_network_state_t;
typedef struct{uint8_t channel,router_bssid[6];}mesh_event_find_network_t;
typedef wifi_event_sta_connected_t mesh_event_router_switch_t;
typedef struct{uint8_t duty;mesh_event_child_connected_t child_connected;}mesh_event_ps_duty_t;
void *esp32_mquickjs_memory_wireless_calloc(const char*,size_t,size_t,esp32_mquickjs_memory_class_t,esp32_mquickjs_memory_budget_role_t);
void esp32_mquickjs_memory_payload_free(void*);
void esp32_mquickjs_wireless_secure_zero(void*,size_t);
esp_err_t esp_mesh_init(void),esp_mesh_deinit(void),esp_mesh_start(void);
esp_err_t esp_mesh_set_topology(esp_mesh_topology_t),esp_mesh_set_max_layer(int),esp_mesh_set_capacity_num(int);
esp_err_t esp_mesh_set_xon_qsize(int),esp_mesh_send_block_time(uint32_t),esp_mesh_enable_ps(void),esp_mesh_disable_ps(void);
esp_err_t esp_mesh_set_ap_authmode(wifi_auth_mode_t),esp_mesh_set_config(const mesh_cfg_t*);
esp_err_t esp_mesh_set_ie_crypto_key(const char*,int),esp_mesh_set_ie_crypto_funcs(const mesh_crypto_funcs_t*);
esp_err_t esp_mesh_fix_root(bool),esp_mesh_set_type(mesh_type_t),esp_mesh_set_self_organized(bool,bool);
bool esp_mesh_is_root(void);mesh_type_t esp_mesh_get_type(void);
int esp_mesh_get_layer(void),esp_mesh_get_total_node_num(void),esp_mesh_get_routing_table_size(void);
esp_err_t esp_mesh_get_tx_pending(mesh_tx_pending_t*),esp_mesh_get_rx_pending(mesh_rx_pending_t*);
esp_err_t esp_mesh_get_routing_table(mesh_addr_t*,int,int*),esp_mesh_get_group_list(mesh_addr_t*,int);
int esp_mesh_get_group_num(void);
esp_err_t esp_mesh_set_group_id(const mesh_addr_t*,int),esp_mesh_delete_group_id(const mesh_addr_t*,int);
esp_err_t esp_mesh_post_toDS_state(bool),esp_mesh_connect(void),esp_mesh_disconnect(void),esp_mesh_flush_upstream_packets(void);
esp_err_t esp_mesh_send(const mesh_addr_t*,const mesh_data_t*,int,const mesh_opt_t*,int);
esp_err_t esp_mesh_recv(mesh_addr_t*,mesh_data_t*,int,int*,mesh_opt_t*,int);
esp_err_t esp_mesh_recv_toDS(mesh_addr_t*,mesh_addr_t*,mesh_data_t*,int,int*,mesh_opt_t*,int);
