"""Deferred production DPP selection validation; no SDK mutation or RF proof."""
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2] / 'components/esp32_mquickjs'


def unit(path):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', path.read_text(), flags=re.M)


class DppConnection(unittest.TestCase):
    def test_authentication_selection_and_unrepresentable_credentials(self):
        for sae in (0, 1):
            with self.subTest(sae=sae):
                code = '#define CONFIG_ESP_WIFI_ENABLE_WPA3_SAE %d\n' % sae + TYPES
                code += unit(ROOT / 'internal/esp32_mquickjs_wifi_dpp_connection.h')
                code += unit(ROOT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_connection.c')
                compile_run(self, code + MAIN)


ROW = r'''
typedef struct {
 uint8_t ssid[32],ssid_len,password[64],password_len;
 char connector[512];uint16_t connector_len;
 uint8_t net_access_key[128];uint16_t net_access_key_len;
 uint8_t c_sign_key[128];uint16_t c_sign_key_len;
 uint64_t net_access_key_expiry;uint8_t curr_chan,akm;
} esp_dpp_config_data_t;
'''
TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_DPP_SUPPORT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NOT_SUPPORTED 2
typedef int esp_err_t;
enum {ESP_DPP_AKM_UNKNOWN,ESP_DPP_AKM_DPP,ESP_DPP_AKM_PSK,ESP_DPP_AKM_SAE,
 ESP_DPP_AKM_PSK_SAE,ESP_DPP_AKM_SAE_DPP,ESP_DPP_AKM_PSK_SAE_DPP};
enum {WIFI_AUTH_DPP=20,WIFI_AUTH_WPA3_PSK=30,WIFI_AUTH_WPA2_PSK=40,WPA3_SAE_PWE_BOTH=3};
typedef struct {struct {uint8_t ssid[32],password[64],channel;struct {int authmode;}threshold;
 struct {bool capable,required;}pmf_cfg;int sae_pwe_h2e;}sta;}wifi_config_t;
''' + ROW
MAIN = r'''
static esp_dpp_config_data_t row;
static void init(unsigned akm){memset(&row,0,sizeof(row));row.akm=akm;row.ssid_len=32;memset(row.ssid,'S',32);
 row.password_len=8;memset(row.password,'P',8);strcpy(row.connector,"jws");row.connector_len=3;
 row.net_access_key_len=row.c_sign_key_len=1;row.net_access_key[0]=7;row.c_sign_key[0]=9;row.curr_chan=11;}
static void rejects(int expected,esp32_mquickjs_wifi_dpp_auth_t auth){
 wifi_config_t config;memset(&config,0x55,sizeof(config));esp32_mquickjs_wifi_dpp_auth_t chosen=99;
 assert(esp32_mquickjs_wifi_dpp_connection_prepare(&row,auth,&chosen,&config)==expected);
 assert(chosen==ESP32_MQUICKJS_DPP_AUTH_DEFAULT);for(size_t i=0;i<sizeof(config);i++)assert(!((uint8_t*)&config)[i]);}
int main(void){
 wifi_config_t config;esp32_mquickjs_wifi_dpp_auth_t chosen;
 for(unsigned akm=1;akm<=6;akm++){
  init(akm);bool dpp=akm==1||akm==5||akm==6;bool sae=akm==3||akm==4;
  int e=esp32_mquickjs_wifi_dpp_connection_prepare(&row,ESP32_MQUICKJS_DPP_AUTH_DEFAULT,&chosen,&config);
  if(sae&&!CONFIG_ESP_WIFI_ENABLE_WPA3_SAE){assert(e==ESP_ERR_NOT_SUPPORTED);continue;}
  assert(!e&&!config.sta.channel&&!memcmp(config.sta.ssid,row.ssid,32)&&config.sta.pmf_cfg.capable);
  assert(chosen==(dpp?ESP32_MQUICKJS_DPP_AUTH_CONNECTOR:sae?ESP32_MQUICKJS_DPP_AUTH_SAE:ESP32_MQUICKJS_DPP_AUTH_PSK));
  assert(config.sta.pmf_cfg.required==(dpp||sae));
  if(dpp){for(unsigned i=0;i<64;i++)assert(!config.sta.password[i]);assert(config.sta.threshold.authmode==WIFI_AUTH_DPP);}
 }
 init(6);assert(!esp32_mquickjs_wifi_dpp_connection_prepare(&row,ESP32_MQUICKJS_DPP_AUTH_PSK,&chosen,&config));
 assert(chosen==ESP32_MQUICKJS_DPP_AUTH_PSK&&!memcmp(config.sta.password,"PPPPPPPP",8));
 init(1);rejects(ESP_ERR_INVALID_ARG,ESP32_MQUICKJS_DPP_AUTH_PSK);
 init(2);rejects(ESP_ERR_INVALID_ARG,ESP32_MQUICKJS_DPP_AUTH_CONNECTOR);
 init(2);row.password_len=7;rejects(ESP_ERR_INVALID_ARG,0);
 init(2);row.password_len=64;memset(row.password,'F',64);assert(!esp32_mquickjs_wifi_dpp_connection_prepare(&row,0,&chosen,&config));
 row.password[63]='g';rejects(ESP_ERR_INVALID_ARG,0);
 init(3);row.password_len=64;rejects(ESP_ERR_INVALID_ARG,0);
 init(2);row.password[3]=0;rejects(ESP_ERR_INVALID_ARG,0);
 init(1);row.ssid[31]=0;rejects(ESP_ERR_NOT_SUPPORTED,0);
 init(1);row.ssid_len=33;rejects(ESP_ERR_INVALID_ARG,0);
 init(1);row.connector_len=512;rejects(ESP_ERR_INVALID_ARG,0);
 init(1);row.connector[3]='X';rejects(ESP_ERR_INVALID_ARG,0);
 init(1);row.connector[1]=0;rejects(ESP_ERR_INVALID_ARG,0);
 init(1);row.net_access_key_len=0;rejects(ESP_ERR_INVALID_ARG,0);
 init(1);row.c_sign_key_len=129;rejects(ESP_ERR_INVALID_ARG,0);
 init(0);rejects(ESP_ERR_INVALID_ARG,0);
 init(7);rejects(ESP_ERR_INVALID_ARG,0);
 return 0;
}
'''
