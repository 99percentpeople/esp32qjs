"""Deferred production Mesh native owner tests; SDK/allocator boundaries injected.

Do not run during API implementation. The real owner and event-capture source
are compiled together when the consolidated Wi-Fi test phase begins.
"""
import re
import unittest
from pathlib import Path
from test_wireless_control_regression import compile_run

BASE = Path(__file__).resolve().parents[2] / 'components/esp32_mquickjs'


def source():
    clean = lambda text: re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', text, flags=re.M)
    main = (BASE / 'src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_sdk.c').read_text()
    events = (BASE / 'src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_events.inc').read_text()
    main = main.replace('#include "esp32_mquickjs_wifi_mesh_events.inc"', events)
    main = main.replace('#include "esp32_mquickjs_wifi_mesh_scan.inc"',
                        (BASE / 'src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_scan.inc').read_text())
    main = main.replace('#include "esp32_mquickjs_wifi_mesh_controls.inc"',
                        (BASE / 'src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh_controls.inc').read_text())
    return PREFIX + clean((BASE / 'internal/esp32_mquickjs_wifi_mesh_feature.h').read_text()) + clean((BASE / 'internal/esp32_mquickjs_wifi_mesh_sdk.h').read_text()) + clean(main) + NATIVE


class WiFiMeshSdk(unittest.TestCase):
    def test_seal_during_send_defers_deinit_to_the_worker_cleanup_suffix(self):
        code = source().replace(
            'assert(esp32_mquickjs_wifi_mesh_sdk_close(&active)==ESP_ERR_TIMEOUT);',
            'assert(!esp32_mquickjs_wifi_mesh_sdk_seal(&active));assert(!deinits);')
        compile_run(self, code + r'''
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));uint8_t bytes[3]={1,2,3};
 esp32_mquickjs_wifi_mesh_send_t send={.destination=ESP32_MQUICKJS_MESH_TO_ROOT,
  .reliable=true,.protocol=MESH_PROTO_BIN,.bytes=bytes,.length=3};
 other=active;--other.identity;
 assert(esp32_mquickjs_wifi_mesh_sdk_seal(&other)==ESP_ERR_INVALID_STATE&&!s_mesh->status.closing);
 close_during_send=true;assert(!esp32_mquickjs_wifi_mesh_sdk_send(&active,&send));
 assert(s_mesh->status.closing&&!s_mesh->status.busy&&!deinits);
 assert(esp32_mquickjs_wifi_mesh_sdk_inspect(&active)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_mesh_sdk_receive(&active,false)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active)&&deinits==1);
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
}
''')

    def test_allocation_and_parameter_admission_before_native_mutation(self):
        compile_run(self, source() + r'''
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();
 c.receive_queue=15;assert(esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active)==ESP_ERR_INVALID_ARG);
 assert(!allocations&&!native_calls);c=config();
 for(unsigned failure=1;failure<=2;++failure){
  allocations=0;fail_allocation=failure;
  assert(esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active)==ESP_ERR_NO_MEM);
  assert(!active.identity&&!s_mesh&&!s_mesh_reserving&&!live_allocations&&!native_calls);
 }
 fail_allocation=0;assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active));
 assert(esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&other)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active)&&!native_calls);
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
 s_mesh_next_identity=UINT32_MAX;assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active));
 assert(active.identity==UINT32_MAX);assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active));
 assert(esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active)==ESP_ERR_NO_MEM);
}
''')

    def test_native_failure_retains_identity_and_does_not_replay_cleanup(self):
        compile_run(self, source() + r'''
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(2,&c,&active));fail_native=2;
 assert(esp32_mquickjs_wifi_mesh_sdk_start(&active)==-77&&inits==1&&!starts);
 assert(s_mesh->status.closing&&!s_mesh->status.restart_required);
 assert(all_zero(&s_mesh->config,sizeof(s_mesh->config)));
 fail_native=0;assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active)&&deinits==1);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active)&&deinits==1);
 other=active;assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(2,&c,&active));
 assert(esp32_mquickjs_wifi_mesh_sdk_release(&other)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));
 assert(esp32_mquickjs_wifi_mesh_sdk_start(&active)==ESP_ERR_INVALID_STATE&&!s_mesh->status.closing);
 fail_deinit=true;assert(esp32_mquickjs_wifi_mesh_sdk_close(&active)==-91);
 unsigned previous=deinits;assert(esp32_mquickjs_wifi_mesh_sdk_close(&active)==-91&&deinits==previous);
 assert(s_mesh->status.restart_required&&s_mesh->status.cleanup_error==-91);
 assert(esp32_mquickjs_wifi_mesh_sdk_release(&active)==ESP_ERR_INVALID_STATE);
}
''')

    def test_partial_init_failure_is_not_hidden_by_noop_sdk_deinit(self):
        compile_run(self, source() + r'''
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(3,&c,&active));
 fail_native=1;assert(esp32_mquickjs_wifi_mesh_sdk_start(&active)==-77);
 assert(s_mesh->status.restart_required&&!s_mesh->status.initialized);
 assert(esp32_mquickjs_wifi_mesh_sdk_close(&active)==ESP_ERR_INVALID_STATE&&!deinits);
 assert(esp32_mquickjs_wifi_mesh_sdk_start(&active)==ESP_ERR_INVALID_STATE&&inits==1);
 assert(all_zero(&s_mesh->config,sizeof(s_mesh->config)));
}
''')

    def test_full_observation_queue_keeps_native_control_and_exact_commit(self):
        compile_run(self, source() + r'''
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));
 for(unsigned i=0;i<ESP32_MQUICKJS_MESH_EVENT_SLOTS;++i)
  assert(__wrap_esp_mesh_send_event_internal(MESH_EVENT_STARTED,NULL,0)==ESP_ERR_TIMEOUT);
 mesh_event_connected_t joined={.self_layer=3,.connected={.channel=6,.bssid={2,3,4,5,6,7}}};
 esp32_mquickjs_wifi_mesh_sdk_capture(MESH_EVENT_PARENT_CONNECTED,&joined,sizeof(joined));
 assert(s_mesh->status.parent_connected&&s_mesh->status.layer==3&&s_mesh->status.channel==6);
 mesh_event_disconnected_t left={.reason=17};
 esp32_mquickjs_wifi_mesh_sdk_capture(MESH_EVENT_PARENT_DISCONNECTED,&left,sizeof(left));
 assert(!s_mesh->status.parent_connected&&s_mesh->status.dropped_events==2);
 esp32_mquickjs_wifi_mesh_notice_t a,b;
 assert(!esp32_mquickjs_wifi_mesh_sdk_event_copy(&active,&a));
 assert(!esp32_mquickjs_wifi_mesh_sdk_event_copy(&active,&b)&&a.sequence==b.sequence);
 assert(esp32_mquickjs_wifi_mesh_sdk_event_commit(&active,a.sequence+1)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_event_commit(&active,a.sequence));
 assert(esp32_mquickjs_wifi_mesh_sdk_event_commit(&active,a.sequence)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_mesh_sdk_capture(MESH_EVENT_PARENT_CONNECTED,NULL,sizeof(joined));
 assert(!s_mesh->status.parent_known&&s_mesh->status.malformed_events==1);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active));
 assert(!live_allocations&&!lock_depth);
}
''')

    def test_receive_retains_two_lanes_and_conversion_failure_does_not_consume(self):
        compile_run(self, source() + r'''
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_receive(&active,false));
 assert(!esp32_mquickjs_wifi_mesh_sdk_receive(&active,false)&&recv_calls==1);
 assert(!esp32_mquickjs_wifi_mesh_sdk_receive(&active,true)&&recv_calls==2);
 esp32_mquickjs_wifi_mesh_message_t self,ds;uint8_t bytes[MESH_MTU];
 assert(esp32_mquickjs_wifi_mesh_sdk_message_copy(&active,false,&self,bytes,2)==ESP_ERR_INVALID_SIZE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_message_copy(&active,false,&self,bytes,sizeof(bytes)));
 assert(self.length==5&&!memcmp(bytes,"hello",5));
 assert(!esp32_mquickjs_wifi_mesh_sdk_message_copy(&active,true,&ds,bytes,sizeof(bytes)));
 assert(ds.to_ds&&ds.sequence!=self.sequence);
 assert(esp32_mquickjs_wifi_mesh_sdk_message_commit(&active,true,self.sequence)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_message_commit(&active,false,self.sequence));
 assert(esp32_mquickjs_wifi_mesh_sdk_message_commit(&active,false,self.sequence)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_message_commit(&active,true,ds.sequence));
 s_mesh->next_sequence=UINT32_MAX;emit_during_receive=true;
 assert(!esp32_mquickjs_wifi_mesh_sdk_receive(&active,false));
 assert(!esp32_mquickjs_wifi_mesh_sdk_message_copy(&active,false,&self,bytes,sizeof(bytes)));
 assert(self.sequence==UINT32_MAX);assert(!esp32_mquickjs_wifi_mesh_sdk_message_commit(&active,false,self.sequence));
 assert(esp32_mquickjs_wifi_mesh_sdk_receive(&active,false)==ESP_ERR_NO_MEM);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active));
 assert(!live_allocations);
}
''')

    def test_close_during_native_send_keeps_span_until_native_return(self):
        compile_run(self, source() + r'''
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));uint8_t bytes[3]={1,2,3};
 esp32_mquickjs_wifi_mesh_send_t send={.destination=ESP32_MQUICKJS_MESH_TO_ROOT,
  .reliable=true,.protocol=MESH_PROTO_BIN,.bytes=bytes,.length=3};
 close_during_send=true;assert(!esp32_mquickjs_wifi_mesh_sdk_send(&active,&send));
 assert(s_mesh->status.closing&&!s_mesh->status.busy&&!deinits&&s_mesh->status.sends==1);
 assert(esp32_mquickjs_wifi_mesh_sdk_send(&active,&send)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active)&&deinits==1);
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
}
''')


PREFIX = r'''
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
'''

NATIVE = r'''
static esp32_mquickjs_wifi_mesh_token_t active,other;
static unsigned allocations,fail_allocation,live_allocations,native_calls,fail_native,inits,starts,deinits,recv_calls;
static bool fail_deinit,close_during_send,emit_during_receive,force_nonroot;
static mesh_cfg_t native_config;
static char native_key[64];
static unsigned layer_reads,scan_starts,scan_reads,parent_sets;
static bool native_self_organized=true,scan_fail,scan_ignore_flush,scan_close_during_start;
static int scan_ie_length=4;static uint16_t scan_remaining;static const wifi_scan_config_t*retained_scan_config;
static int native_expiry=30;static bool ignore_expiry;
static struct{void*p;size_t bytes;}owned[8];
static bool all_zero(const void*p,size_t size){const uint8_t*b=p;for(size_t i=0;i<size;++i)if(b[i])return false;return true;}
void *esp32_mquickjs_memory_wireless_calloc(const char*name,size_t n,size_t s,esp32_mquickjs_memory_class_t cls,esp32_mquickjs_memory_budget_role_t role){
 (void)name;(void)cls;(void)role;assert(!lock_depth);if(++allocations==fail_allocation)return NULL;
 void*p=calloc(n,s);assert(p);for(unsigned i=0;i<8;++i)if(!owned[i].p){owned[i].p=p;owned[i].bytes=n*s;break;}
 ++live_allocations;return p;
}
void esp32_mquickjs_memory_payload_free(void*p){if(!p)return;assert(!lock_depth);
 for(unsigned i=0;i<8;++i)if(owned[i].p==p){assert(all_zero(p,owned[i].bytes));owned[i].p=NULL;--live_allocations;free(p);return;}assert(false);
}
void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}
static int step(void){assert(!lock_depth);return ++native_calls==fail_native?-77:0;}
esp_err_t esp_mesh_init(void){++inits;return step();}
esp_err_t esp_mesh_start(void){++starts;return step();}
esp_err_t esp_mesh_deinit(void){++deinits;assert(!lock_depth);return fail_deinit?-91:0;}
#define SETTER(name,type) esp_err_t name(type v){(void)v;return step();}
SETTER(esp_mesh_set_topology,esp_mesh_topology_t)
SETTER(esp_mesh_set_max_layer,int)
SETTER(esp_mesh_set_capacity_num,int)
SETTER(esp_mesh_set_xon_qsize,int)
SETTER(esp_mesh_send_block_time,uint32_t)
SETTER(esp_mesh_set_ap_authmode,wifi_auth_mode_t)
esp_err_t esp_mesh_set_config(const mesh_cfg_t*c){native_config=*c;return step();}
SETTER(esp_mesh_set_ie_crypto_funcs,const mesh_crypto_funcs_t*)
SETTER(esp_mesh_fix_root,bool)
SETTER(esp_mesh_set_type,mesh_type_t)
esp_err_t esp_mesh_enable_ps(void){return step();}esp_err_t esp_mesh_disable_ps(void){return step();}
esp_err_t esp_mesh_set_ie_crypto_key(const char*p,int n){assert(p&&n>=8&&n<=64);int e=step();if(!e){memset(native_key,0,64);memcpy(native_key,p,n);}return e;}
esp_err_t esp_mesh_set_self_organized(bool a,bool b){(void)a;(void)b;return step();}
bool esp_mesh_is_root(void){assert(!lock_depth);return !force_nonroot;}mesh_type_t esp_mesh_get_type(void){return MESH_ROOT;}
int esp_mesh_get_layer(void){++layer_reads;return 1;}int esp_mesh_get_total_node_num(void){return 2;}int esp_mesh_get_routing_table_size(void){return 2;}
esp_err_t esp_mesh_get_tx_pending(mesh_tx_pending_t*p){memset(p,0,sizeof(*p));return step();}
esp_err_t esp_mesh_get_rx_pending(mesh_rx_pending_t*p){memset(p,0,sizeof(*p));return step();}
esp_err_t esp_mesh_send(const mesh_addr_t*to,const mesh_data_t*data,int flags,const mesh_opt_t*opt,int count){
 (void)flags;(void)opt;(void)count;assert(!lock_depth&&!to&&data->size==3&&data->tos==MESH_TOS_P2P);
 if(close_during_send){assert(esp32_mquickjs_wifi_mesh_sdk_close(&active)==ESP_ERR_TIMEOUT);
  assert(esp32_mquickjs_wifi_mesh_sdk_release(&active)==ESP_ERR_INVALID_STATE);assert(!memcmp(data->data,"\1\2\3",3));}
 return 0;
}
esp_err_t esp_mesh_recv(mesh_addr_t*from,mesh_data_t*data,int timeout,int*flags,mesh_opt_t*opt,int count){
 (void)opt;(void)count;assert(!lock_depth&&timeout==0&&data->size==MESH_MTU);++recv_calls;
 memset(from,0,sizeof(*from));memcpy(data->data,"hello",5);data->size=5;data->proto=MESH_PROTO_BIN;*flags=MESH_DATA_P2P;
 if(emit_during_receive)esp32_mquickjs_wifi_mesh_sdk_capture(MESH_EVENT_STARTED,NULL,0);return 0;
}
esp_err_t esp_mesh_recv_toDS(mesh_addr_t*from,mesh_addr_t*to,mesh_data_t*data,int timeout,int*flags,mesh_opt_t*opt,int count){
 memset(to,0,sizeof(*to));int r=esp_mesh_recv(from,data,timeout,flags,opt,count);*flags=MESH_DATA_TODS;return r;
}
esp_err_t __real_esp_mesh_send_event_internal(int32_t id,void*p,size_t n){(void)id;(void)p;(void)n;assert(!lock_depth);return ESP_ERR_TIMEOUT;}
esp_err_t esp_mesh_get_routing_table(mesh_addr_t*p,int bytes,int*count){assert(!lock_depth&&bytes>=(int)sizeof(*p));memset(p,1,sizeof(*p));*count=1;return 0;}
int esp_mesh_get_group_num(void){assert(!lock_depth);return 1;}
esp_err_t esp_mesh_get_group_list(mesh_addr_t*p,int count){assert(!lock_depth&&count==1);memset(p,2,sizeof(*p));return 0;}
esp_err_t esp_mesh_set_group_id(const mesh_addr_t*p,int n){assert(p&&n>0);return step();}
esp_err_t esp_mesh_delete_group_id(const mesh_addr_t*p,int n){assert(p&&n>0);return step();}
esp_err_t esp_mesh_post_toDS_state(bool value){(void)value;return step();}
esp_err_t esp_mesh_connect(void){return step();}esp_err_t esp_mesh_disconnect(void){return step();}
esp_err_t esp_mesh_flush_upstream_packets(void){return step();}
static esp32_mquickjs_wifi_mesh_config_t config(void){
 esp32_mquickjs_wifi_mesh_config_t c={.network={.channel=6,.crypto_funcs=&g_wifi_default_mesh_crypto_funcs,
  .router={.ssid_len=3,.ssid={'a','p','1'}},.mesh_ap={.max_connection=4},.mesh_id={.addr={7,7,7,7,7,7}}},
  .topology=MESH_TOPO_TREE,.type=MESH_IDLE,.ap_authmode=WIFI_AUTH_OPEN,.max_layer=6,.capacity=32,
  .receive_queue=16,.send_block_ms=1000,.vote_percentage=0.9f,.self_organized=true,.encrypt_ie=true,.ie_key_length=8};
 memcpy(c.ie_key,"abcdefgh",8);return c;
}
'''

PREFIX += r'''
esp_err_t esp_mesh_allow_root_conflicts(bool allowed);
int esp_mesh_available_txupQ_num(const mesh_addr_t *addr, uint32_t *xseqno_in);
esp_err_t esp_mesh_fix_root(bool enable);
esp_err_t esp_mesh_get_active_duty_cycle(int* dev_duty, int* dev_duty_type);
int esp_mesh_get_ap_assoc_expire(void);
wifi_auth_mode_t esp_mesh_get_ap_authmode(void);
int esp_mesh_get_ap_connections(void);
int esp_mesh_get_capacity_num(void);
esp_err_t esp_mesh_get_config(mesh_cfg_t *config);
esp_err_t esp_mesh_get_id(mesh_addr_t *id);
esp_err_t esp_mesh_get_ie_crypto_key(char *key, int len);
int esp_mesh_get_max_layer(void);
esp_err_t esp_mesh_get_network_duty_cycle(int* nwk_duty, int* duration_mins, int* dev_duty_type, int* applied_rule);
int esp_mesh_get_non_mesh_connections(void);
int esp_mesh_get_root_healing_delay(void);
esp_err_t esp_mesh_get_router(mesh_router_t *router);
int esp_mesh_get_running_active_duty_cycle(void);
bool esp_mesh_get_self_organized(void);
esp_err_t esp_mesh_get_subnet_nodes_list(const mesh_addr_t *child_mac, mesh_addr_t *nodes, int nodes_num);
esp_err_t esp_mesh_get_subnet_nodes_num(const mesh_addr_t *child_mac, int *nodes_num);
esp_mesh_topology_t esp_mesh_get_topology(void);
int64_t esp_mesh_get_tsf_time(void);
float esp_mesh_get_vote_percentage(void);
int esp_mesh_get_xon_qsize(void);
bool esp_mesh_is_device_active(void);
bool esp_mesh_is_my_group(const mesh_addr_t *addr);
bool esp_mesh_is_ps_enabled(void);
bool esp_mesh_is_root(void);
bool esp_mesh_is_root_conflicts_allowed(void);
bool esp_mesh_is_root_fixed(void);
esp_err_t esp_mesh_ps_duty_signaling(int fwd_times);
esp_err_t esp_mesh_set_active_duty_cycle(int dev_duty, int dev_duty_type);
esp_err_t esp_mesh_set_ap_assoc_expire(int seconds);
esp_err_t esp_mesh_set_id(const mesh_addr_t *id);
esp_err_t esp_mesh_set_ie_crypto_funcs(const mesh_crypto_funcs_t *crypto_funcs);
esp_err_t esp_mesh_set_ie_crypto_key(const char *key, int len);
esp_err_t esp_mesh_set_network_duty_cycle(int nwk_duty, int duration_mins, int applied_rule);
esp_err_t esp_mesh_set_root_healing_delay(int delay_ms);
esp_err_t esp_mesh_set_router(const mesh_router_t *router);
esp_err_t esp_mesh_set_self_organized(bool enable, bool select_parent);
esp_err_t esp_mesh_set_type(mesh_type_t type);
esp_err_t esp_mesh_set_vote_percentage(float percentage);
esp_err_t esp_mesh_switch_channel(const uint8_t *new_bssid, int csa_newchan, int csa_count);
esp_err_t esp_mesh_waive_root(const mesh_vote_t *vote, int reason);
'''

NATIVE += r'''
SETTER(esp_mesh_set_vote_percentage,float)
SETTER(esp_mesh_set_id,const mesh_addr_t*)
SETTER(esp_mesh_allow_root_conflicts,bool)
esp_err_t esp_mesh_set_ap_assoc_expire(int seconds){int e=step();if(!e&&!ignore_expiry)native_expiry=seconds;return e;}
SETTER(esp_mesh_set_root_healing_delay,int)
SETTER(esp_mesh_ps_duty_signaling,int)
esp_err_t esp_mesh_set_router(const mesh_router_t*r){int e=step();if(!e)native_config.router=*r;return e;}
esp_err_t esp_mesh_get_router(mesh_router_t*r){*r=native_config.router;return step();}
esp_err_t esp_mesh_get_config(mesh_cfg_t*c){*c=native_config;return step();}
esp_err_t esp_mesh_get_id(mesh_addr_t*a){*a=native_config.mesh_id;return step();}
esp_err_t esp_mesh_get_ie_crypto_key(char*key,int n){assert(n>=8&&n<=64);memcpy(key,native_key,n);return step();}
esp_mesh_topology_t esp_mesh_get_topology(void){return MESH_TOPO_TREE;}
wifi_auth_mode_t esp_mesh_get_ap_authmode(void){return WIFI_AUTH_OPEN;}
int esp_mesh_get_max_layer(void){return 6;}int esp_mesh_get_capacity_num(void){return 32;}
int esp_mesh_get_xon_qsize(void){return 16;}int esp_mesh_get_ap_connections(void){return 4;}
int esp_mesh_get_non_mesh_connections(void){return 0;}float esp_mesh_get_vote_percentage(void){return 0.9f;}
int esp_mesh_get_ap_assoc_expire(void){return native_expiry;}int esp_mesh_get_root_healing_delay(void){return 100;}
bool esp_mesh_is_root_fixed(void){return false;}bool esp_mesh_get_self_organized(void){return native_self_organized;}
bool esp_mesh_is_root_conflicts_allowed(void){return true;}
bool esp_mesh_is_ps_enabled(void){return false;}bool esp_mesh_is_device_active(void){return true;}
esp_err_t esp_mesh_waive_root(const mesh_vote_t*v,int reason){assert(v&&!v->is_rc_specified&&reason==MESH_VOTE_REASON_ROOT_INITIATED);return step();}
esp_err_t esp_mesh_switch_channel(const uint8_t*a,int ch,int count){(void)a;assert(ch>0&&count>0);return step();}
esp_err_t esp_mesh_set_active_duty_cycle(int duty,int type){(void)duty;(void)type;return step();}
esp_err_t esp_mesh_set_network_duty_cycle(int duty,int duration,int rule){(void)duty;(void)duration;(void)rule;return step();}
esp_err_t esp_mesh_get_active_duty_cycle(int*duty,int*type){*duty=10;*type=1;return step();}
esp_err_t esp_mesh_get_network_duty_cycle(int*duty,int*duration,int*type,int*rule){*duty=20;*duration=1;*type=0;*rule=0;return step();}
int esp_mesh_get_running_active_duty_cycle(void){return 20;}
esp_err_t esp_mesh_get_subnet_nodes_num(const mesh_addr_t*a,int*n){(void)a;*n=2;return step();}
esp_err_t esp_mesh_get_subnet_nodes_list(const mesh_addr_t*a,mesh_addr_t*n,int count){(void)a;assert(count==2);memset(n,1,count*sizeof(*n));return step();}
bool esp_mesh_is_my_group(const mesh_addr_t*a){return a->addr[0]==1;}
int esp_mesh_available_txupQ_num(const mesh_addr_t*a,uint32_t*s){(void)a;*s=UINT32_MAX;return 3;}
int64_t esp_mesh_get_tsf_time(void){return 1234567890;}
'''

PREFIX += r'''
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*,bool),esp_wifi_scan_get_ap_num(uint16_t*);
esp_err_t esp_mesh_scan_get_ap_ie_len(int*),esp_mesh_scan_get_ap_record(wifi_ap_record_t*,void*),esp_mesh_flush_scan_result(void);
esp_err_t esp_mesh_set_parent(const wifi_config_t*,const mesh_addr_t*,mesh_type_t,int);
esp_err_t esp_mesh_get_parent_bssid(mesh_addr_t*),esp_mesh_get_router_bssid(uint8_t*);
esp_err_t esp_mesh_set_ap_connections(int),esp_mesh_set_ap_password(const uint8_t*,int);
'''
NATIVE += r'''
SETTER(esp_mesh_set_ap_connections,int)
esp_err_t esp_mesh_set_ap_password(const uint8_t*p,int n){assert(p&&n>=8&&n<=64);return step();}
esp_err_t esp_mesh_get_parent_bssid(mesh_addr_t*a){assert(!lock_depth);memset(a,1,sizeof(*a));return 0;}
esp_err_t esp_mesh_get_router_bssid(uint8_t*a){assert(!lock_depth);memset(a,2,6);return 0;}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*c,bool block){
 assert(!lock_depth&&block&&c);++scan_starts;retained_scan_config=c;
 assert(c==&s_mesh->scan_config.config);
 if(scan_close_during_start){assert(esp32_mquickjs_wifi_mesh_sdk_close(&active)==ESP_ERR_TIMEOUT);assert(!deinits);}
 if(scan_fail)return ESP_ERR_TIMEOUT;
 scan_remaining=2;return 0;
}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t*n){assert(!lock_depth);*n=scan_remaining;return 0;}
esp_err_t esp_mesh_scan_get_ap_ie_len(int*n){assert(!lock_depth&&scan_remaining);*n=scan_ie_length;return 0;}
esp_err_t esp_mesh_scan_get_ap_record(wifi_ap_record_t*a,void*ie){
 assert(!lock_depth&&scan_remaining);++scan_reads;--scan_remaining;memset(a,0,sizeof(*a));
 a->ssid[0]='a';memset(ie,3,scan_ie_length);return 0;
}
esp_err_t esp_mesh_flush_scan_result(void){assert(!lock_depth);if(!scan_ignore_flush)scan_remaining=0;return 0;}
esp_err_t esp_mesh_set_parent(const wifi_config_t*p,const mesh_addr_t*id,mesh_type_t type,int layer){
 assert(!lock_depth&&p);(void)id;(void)type;(void)layer;++parent_sets;return step();
}
'''
