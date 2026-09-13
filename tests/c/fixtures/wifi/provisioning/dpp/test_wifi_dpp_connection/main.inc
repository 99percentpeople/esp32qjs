
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
