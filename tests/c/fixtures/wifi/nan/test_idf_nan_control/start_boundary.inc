
typedef int wifi_mode_t;
typedef struct{int channel;}wifi_nan_sync_config_t;
typedef union{wifi_nan_sync_config_t nan;}wifi_config_t;
typedef unsigned EventBits_t;
static void *nan_event_group=(void *)1,*s_nan_data_lock=(void *)2;
static struct{unsigned state;void *nan_netif;}s_nan_ctx={.nan_netif=(void *)3};
static unsigned depth,start_calls,wait_ticks;
static bool s_usd_in_progress;
static int s_esp32qjs_nan_start_error;
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
#define NAN_STARTED_BIT 1
#define WIFI_MODE_NAN 4
#define WIFI_IF_NAN 3
#define ESP_ERR_WIFI_NOT_INIT -90
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_RETURN_ON_ERROR(call,...) do{int e=(call);if(e)return e;}while(0)
#define portMAX_DELAY UINT32_MAX
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
static int esp_wifi_get_mode(int *mode){*mode=1;return 0;}
static int esp_wifi_set_mode(int mode){assert(mode==WIFI_MODE_NAN&&!depth);return 0;}
static int esp_wifi_set_config(int iface,void *cfg){assert(iface==WIFI_IF_NAN&&cfg&&!depth);return 0;}
static int esp_wifi_start(void){assert(!depth);start_calls++;return 0;}
static void os_event_group_clear_bits(void *p,unsigned mask){assert(p==nan_event_group&&mask==1);}
static unsigned os_event_group_wait_bits(void*p,unsigned mask,int a,int b,unsigned ticks){
    assert(p==nan_event_group&&mask==1&&!a&&!b);wait_ticks=ticks;return 0;
}
