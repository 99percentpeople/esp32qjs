
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
