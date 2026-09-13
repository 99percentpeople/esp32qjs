
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;typedef unsigned esp_wifi_aci_t;
#define ESP_WIFI_ACI_MAX 4
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 258
#define ESP_ERR_NO_MEM 257
static void *rx_storage[3],*multi_user;
static void *esp_test_tx_statistics[4],*esp_test_tx_tb_statistics[4],*esp_test_tx_fail_statistics[4][6];
static unsigned allocations,live,fail_at,mu_calls;static uint8_t aci_bitmap;
static void *native_alloc(void){++allocations;if(allocations==fail_at)return NULL;void *p=malloc(32);assert(p);++live;return p;}
static void native_free(void *p){assert(p && live);--live;free(p);}
static struct {void (*_free)(void *);} g_wifi_osi_funcs={native_free};
static int esp_test_enable_rx_statistics(void){
    for(unsigned i=0;i<3;++i){rx_storage[i]=native_alloc();if(!rx_storage[i])return ESP_ERR_NO_MEM;}return ESP_OK;}
static void esp_test_disable_rx_statistics(void){for(unsigned i=0;i<3;++i)if(rx_storage[i]){native_free(rx_storage[i]);rx_storage[i]=NULL;}}
static int esp_test_enable_rx_mu_statistics(void){++mu_calls;multi_user=native_alloc();return multi_user?ESP_OK:ESP_ERR_NO_MEM;}
static void esp_test_disable_rx_mu_statistics(void){if(multi_user){native_free(multi_user);multi_user=NULL;}}
static void esp_test_disable_tx_statistics(esp_wifi_aci_t aci){
    if(!(aci_bitmap&(1U<<aci)))return; /* Fixed native partial-cleanup defect. */
    if(esp_test_tx_statistics[aci])native_free(esp_test_tx_statistics[aci]);
    if(esp_test_tx_tb_statistics[aci])native_free(esp_test_tx_tb_statistics[aci]);
    if(esp_test_tx_fail_statistics[aci][0])native_free(esp_test_tx_fail_statistics[aci][0]);
    esp_test_tx_statistics[aci]=esp_test_tx_tb_statistics[aci]=esp_test_tx_fail_statistics[aci][0]=NULL;
    aci_bitmap&=~(1U<<aci);
}
static int esp_test_enable_tx_statistics(esp_wifi_aci_t aci){
    if(aci_bitmap&(1U<<aci))return ESP_OK;
    esp_test_tx_statistics[aci]=native_alloc();if(!esp_test_tx_statistics[aci])return ESP_ERR_NO_MEM;
    esp_test_tx_tb_statistics[aci]=native_alloc();if(!esp_test_tx_tb_statistics[aci])return ESP_ERR_NO_MEM;
    esp_test_tx_fail_statistics[aci][0]=native_alloc();if(!esp_test_tx_fail_statistics[aci][0])return ESP_ERR_NO_MEM;
    for(unsigned i=1;i<6;++i)esp_test_tx_fail_statistics[aci][i]=(char*)esp_test_tx_fail_statistics[aci][0]+i;
    aci_bitmap|=1U<<aci;return ESP_OK;
}
