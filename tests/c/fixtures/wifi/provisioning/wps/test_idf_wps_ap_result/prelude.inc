
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_TIMEOUT 0x107
#define WPS_OWNER_NONE 0
#define WIFI_EVENT 7
enum{WIFI_EVENT_AP_WPS_RG_PIN=10,WIFI_EVENT_AP_WPS_RG_SUCCESS,WIFI_EVENT_AP_WPS_RG_FAILED,WIFI_EVENT_AP_WPS_RG_TIMEOUT,WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP};
enum{WPS_AP_FAIL_REASON_NORMAL,WPS_AP_FAIL_REASON_CONFIG,WPS_AP_FAIL_REASON_AUTH,WPS_AP_FAIL_REASON_MAX};
typedef struct{uint8_t peer_macaddr[6];} wifi_event_ap_wps_rg_success_t;
typedef struct{int reason;uint8_t peer_macaddr[6];} wifi_event_ap_wps_rg_fail_reason_t;
static void *gWpsSm;
static bool on_native=true,station_held,oom;
static int native_owner,live,clears,posts,post_error;
bool current_task_is_wifi_task(void){return on_native;}
bool esp32qjs_wps_native_held(void){return station_held;}
static int wps_get_owner(void){return native_owner;}
static void *os_zalloc(size_t size){if(oom)return NULL;void *p=calloc(1,size);assert(p);live++;return p;}
static void forced_memzero(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static void bin_clear_free(void *p,size_t n){assert(p && live==1);forced_memzero(p,n);for(size_t i=0;i<n;i++)assert(!((uint8_t*)p)[i]);clears++;live--;free(p);}
static int esp_event_post(int base,int32_t event,const void *data,size_t size,unsigned wait);
