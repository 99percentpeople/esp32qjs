
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_RESPONSE 3
#define ESP_ERR_WIFI_NOT_ASSOC 4
static uint8_t g_ic[64],g_pm_cfg[128],station_storage[256],node_storage[1100];
static bool in_wifi,discard,corrupt;static unsigned policy_writes,offset_writes;static int bitmap=129,native_error;
static bool current_task_is_wifi_task(void){return in_wifi;}
static int eloop_register_timeout_blocking(int (*fn)(void*,void*),void *a,void *b){
    assert(!in_wifi);if(discard)return -1;in_wifi=true;int result=fn(a,b);in_wifi=false;return result;}
static void pm_twt_set_config(wifi_twt_config_t *config){assert(in_wifi);++policy_writes;
    g_pm_cfg[84]=config->post_wakeup_event;g_pm_cfg[85]=config->twt_enable_keep_alive;if(corrupt)g_pm_cfg[84]^=1;}
static int ieee80211_itwt_get_flow_id_status(int *out){assert(in_wifi);*out=bitmap;return native_error;}
static int wifi_sta_itwt_set_target_wake_time_offset_process(void *message){assert(in_wifi);++offset_writes;
    if(native_error)return native_error;uint32_t offset=((uint32_t*)message)[3];
    if(corrupt)++offset;memcpy(node_storage+1060,&offset,4);return 0;}
static void associate(void){const void *p=station_storage;memcpy(g_ic+16,&p,sizeof(p));p=node_storage;
    memcpy(station_storage+228,&p,sizeof(p));uint32_t state=5;memcpy(station_storage+152,&state,4);}
