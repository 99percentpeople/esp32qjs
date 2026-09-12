"""Deferred actual SDK DPP selection and independent-introduction timeout cases."""
import os
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]


class DppConnectionNative(unittest.TestCase):
    def test_exact_selection_and_timeout_without_old_provisioning_auth(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed SDK')
        sys.path.insert(0, str(ROOT / 'scripts'))
        from patch_idf_dpp import patch_source, function
        original = Path(sdk) / 'components/wpa_supplicant/esp_supplicant/src/esp_dpp.c'
        source = patch_source('esp_supplicant/src/esp_dpp.c', original.read_bytes()).decode()
        code = BOUNDARIES
        for name in ('esp32qjs_dpp_connection_locked', 'esp32qjs_dpp_native_select',
                     'esp32qjs_dpp_native_check_connection', 'peer_disc_timeout'):
            code += function(source, name)
        compile_run(self, code + MAIN)


BOUNDARIES = r'''
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
'''
MAIN = r'''
int main(void){
 result.status.identity=41;result.status.attached=result.status.retained=true;esp_dpp_config_data_t row={true};
 assert(esp32qjs_dpp_native_select(42,&row)==ESP_ERR_INVALID_STATE&&!installs&&!s_dpp_command_identity);
 wifi_task=false;assert(esp32qjs_dpp_native_select(41,&row)==ESP_ERR_INVALID_STATE&&!installs);wifi_task=true;
 row.valid=false;assert(esp32qjs_dpp_native_select(41,&row)==ESP_ERR_INVALID_ARG&&!installs);row.valid=true;
 install_error=9;assert(esp32qjs_dpp_native_select(41,&row)==9&&!result.status.connection_mode&&!s_dpp_command_identity);
 install_error=0;assert(!esp32qjs_dpp_native_select(41,&row)&&result.status.connection_mode&&result.status.configuration_installed);
 assert(!s_dpp_command_identity&&!locks&&esp32qjs_dpp_connection_locked());
 assert(esp32qjs_dpp_native_select(41,NULL)==ESP_ERR_INVALID_STATE&&result.status.configuration_installed);
 const uint8_t bssid[6]={2,3,4,5,6,7};memcpy(gWpaSm.bssid,bssid,sizeof(bssid));
 assert(esp32qjs_dpp_native_check_connection(41,ESP_DPP_AKM_DPP,bssid)==ESP_ERR_NOT_FINISHED);
 gWpaSm.wpa_state=WPA_COMPLETED;gWpaSm.key_mgmt=WPA_KEY_MGMT_PSK;
 assert(esp32qjs_dpp_native_check_connection(41,ESP_DPP_AKM_DPP,bssid)==ESP_ERR_INVALID_RESPONSE);
 assert(!esp32qjs_dpp_native_check_connection(41,ESP_DPP_AKM_PSK,bssid));
 gWpaSm.key_mgmt=WPA_KEY_MGMT_PSK_SHA256;assert(!esp32qjs_dpp_native_check_connection(41,ESP_DPP_AKM_PSK,bssid));
 gWpaSm.key_mgmt=WPA_KEY_MGMT_SAE;assert(!esp32qjs_dpp_native_check_connection(41,ESP_DPP_AKM_SAE,bssid));
 gWpaSm.key_mgmt=WPA_KEY_MGMT_DPP;assert(!esp32qjs_dpp_native_check_connection(41,ESP_DPP_AKM_DPP,bssid));
 gWpaSm.bssid[5]++;assert(esp32qjs_dpp_native_check_connection(41,ESP_DPP_AKM_DPP,bssid)==ESP_ERR_INVALID_RESPONSE);
 assert(esp32qjs_dpp_native_check_connection(42,ESP_DPP_AKM_DPP,bssid)==ESP_ERR_INVALID_STATE);
 assert(esp32qjs_dpp_native_check_connection(41,0,bssid)==ESP_ERR_INVALID_ARG&&!locks);
 peer_disc_timeout(NULL,NULL);assert(failures==1&&!locks);
 struct dpp_authentication old;
 peer_disc_timeout(NULL,&old);assert(failures==1&&!locks);
 result.status.closing=true;peer_disc_timeout(NULL,NULL);assert(failures==1&&!locks);
 result.status.closing=false;result.status.terminal=true;peer_disc_timeout(NULL,NULL);assert(failures==1&&!locks);
 result.status.terminal=false;result.status.configuration_installed=false;peer_disc_timeout(NULL,NULL);assert(failures==1&&!locks);
 result.status.connection_mode=false;assert(!esp32qjs_dpp_native_select(41,NULL));
 assert(result.status.connection_mode&&!result.status.configuration_installed&&!s_dpp_command_identity);
 /* Legacy SDK auth timeout still uses its exact authentication allocation. */
 result.status.retained=false;s_dpp_ctx.dpp_auth=&old;peer_disc_timeout(NULL,&old);assert(failures==2&&!locks);
 return 0;
}
'''
