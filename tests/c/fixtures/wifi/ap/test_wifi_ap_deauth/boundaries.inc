
#include <stdlib.h>
#include <stdatomic.h>
#define ESP_ERR_NOT_FOUND 261
#define ESP_ERR_INVALID_RESPONSE 264
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#ifndef ESP_WIFI_MAX_CONN_NUM
#define ESP_WIFI_MAX_CONN_NUM 15
#endif
static bool on_wifi,fail_alloc,defer,before_dispatch_replace;
static unsigned allocations,frees,ipc_calls,lookups,deauths;
static int ipc_error,lookup_error,deauth_error;
static uint16_t current_aid=1,last_aid;
static uint8_t current_mac[6]={2,0,0,0,0,1},sent_mac[6];
bool current_task_is_wifi_task(void){return on_wifi;}
static void *heap_caps_calloc(size_t n,size_t size,int caps){assert(locks&&!critical&&caps==3);
 if(fail_alloc)return NULL;void *p=calloc(n,size);assert(p);allocations++;return p;}
static void heap_caps_free(void *p){assert(locks&&!critical&&p);frees++;free(p);}
static int esp_wifi_ap_get_sta_aid(const uint8_t mac[6],uint16_t *aid){assert(on_wifi&&!critical);lookups++;
 if(lookup_error)return lookup_error;if(memcmp(mac,current_mac,6))return ESP_ERR_NOT_FOUND;*aid=current_aid;return 0;}
static int esp_wifi_deauth_sta(uint16_t aid){assert(on_wifi&&!critical&&aid&&aid==current_aid);deauths++;
 last_aid=aid;memcpy(sent_mac,current_mac,6);return deauth_error;}
