
static esp32_mquickjs_wifi_nan_message_tx_status_t message_tx;
static unsigned message_sends;
static bool message_inline;
bool esp32_mquickjs_wifi_nan_tx_message_status(uint32_t id,esp32_mquickjs_wifi_nan_message_tx_status_t*out){
 assert(!lock_depth);if(!native_started||message_tx.identity!=id)return false;*out=message_tx;return true;
}
esp_err_t esp32_mquickjs_wifi_nan_tx_message_release(uint32_t id){
 assert(!lock_depth);if(message_tx.identity!=id)return ESP_ERR_INVALID_STATE;
 if(!message_tx.buffer_retired)return ESP_ERR_TIMEOUT;
 memset(&message_tx,0,sizeof(message_tx));return 0;
}
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
bool esp32_mquickjs_wifi_nan_usd_message_status(uint32_t id,esp32_mquickjs_wifi_nan_message_tx_status_t*out){
 return esp32_mquickjs_wifi_nan_tx_message_status(id,out);
}
esp_err_t esp32_mquickjs_wifi_nan_usd_message_release(uint32_t id){
 return esp32_mquickjs_wifi_nan_tx_message_release(id);
}
#endif
esp_err_t esp32_mquickjs_wifi_radio_nan_message_send(const esp32_mquickjs_wifi_radio_operation_t*op,
 wifi_nan_followup_params_t*params,uint32_t id,uint32_t*context){
 assert(!lock_depth&&native_started&&op->identity&&params->inst_id==7&&params->peer_inst_id==3&&id&&context&&!*context);
 ++message_sends;*context=1234;
 message_tx=(esp32_mquickjs_wifi_nan_message_tx_status_t){.identity=id,.ticket=message_sends,.entered=true,.allocated=true,
  .tx_done=message_inline,.tx_succeeded=message_inline,.completed_us=clock_us};return 0;
}
