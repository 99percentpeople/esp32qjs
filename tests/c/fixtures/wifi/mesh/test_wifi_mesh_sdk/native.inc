
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
