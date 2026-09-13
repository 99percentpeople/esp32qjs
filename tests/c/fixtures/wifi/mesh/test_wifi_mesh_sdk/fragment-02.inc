
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
