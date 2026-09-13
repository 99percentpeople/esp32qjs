
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
typedef int esp_err_t;
typedef struct {bool valid;}esp_dpp_config_data_t;
struct dpp_authentication {int unused;};
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_DPP_CONF_TIMEOUT 3
#define ESP_ERR_NOT_FINISHED 4
#define ESP_ERR_INVALID_RESPONSE 5
#define ETH_ALEN 6
#define os_memcmp memcmp
enum {ESP_DPP_AKM_DPP=1,ESP_DPP_AKM_PSK,ESP_DPP_AKM_SAE};
enum {WPA_COMPLETED=9,WPA_KEY_MGMT_DPP=1,WPA_KEY_MGMT_PSK=2,WPA_KEY_MGMT_PSK_SHA256=4,WPA_KEY_MGMT_SAE=8};
static struct {unsigned wpa_state,key_mgmt;uint8_t bssid[6];}gWpaSm;
#define wpa_printf(...) ((void)0)
static struct {struct {uint64_t identity;bool retained,attached,closing,terminal,connection_mode,configuration_installed;}status;}result;
#define s_dpp_result (&result)
static struct {bool bootstrap_done,dpp_listen_ongoing;struct dpp_authentication *dpp_auth;}s_dpp_ctx;
static atomic_bool s_dpp_init_done=true,dpp_shutting_down;
static uint64_t s_dpp_command_identity;
static unsigned locks,installs,failures;
static int install_error;
static bool wifi_task=true;
static bool current_task_is_wifi_task(void){return wifi_task;}
static int dpp_api_lock(void){assert(!locks);locks++;return 0;}
static void dpp_api_unlock(void){assert(locks==1);locks--;}
static bool esp32qjs_dpp_config_valid(const esp_dpp_config_data_t*r){return r&&r->valid;}
static bool esp32qjs_dpp_result_exact(uint64_t id){return id&&id==result.status.identity;}
static int esp_supp_dpp_set_config(const esp_dpp_config_data_t*r){
 assert(locks==1&&s_dpp_command_identity==result.status.identity&&(!r||r->valid));installs++;return install_error;}
static void dpp_abort_failure_locked(int e){assert(locks==1&&e==ESP_ERR_DPP_CONF_TIMEOUT);failures++;dpp_api_unlock();}
