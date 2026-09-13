
static unsigned pair_starts,pair_closes,pair_commits,credential_reads;
static uint32_t seen_credential;
static esp_err_t pair_commit_error,credential_error;
static esp32_mquickjs_wifi_nan_credentials_t cached_credentials;
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_credentials(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint8_t id,esp32_mquickjs_wifi_nan_credentials_t*out){assert(!lock_depth&&t->identity&&id);++credential_reads;*out=cached_credentials;return credential_error;}
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_commit(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint32_t id,uint32_t*out){assert(!lock_depth&&t->identity&&id);++pair_commits;*out=pair_commit_error?0:301;return pair_commit_error;}
static unsigned bootstrap_sends;
static bool bootstrap_response,bootstrap_accept;
esp_err_t esp32_mquickjs_wifi_radio_nan_bootstrap_send(const esp32_mquickjs_wifi_radio_operation_t*op,
 wifi_nan_followup_params_t*params,uint32_t id,uint32_t*context,bool response,bool accept){
 assert(!lock_depth&&native_started&&op->identity&&params->inst_id==7&&params->peer_inst_id==9&&id&&context&&!*context);
 ++bootstrap_sends;bootstrap_response=response;bootstrap_accept=accept;*context=4321;
 message_tx=(esp32_mquickjs_wifi_nan_message_tx_status_t){.identity=id,.ticket=bootstrap_sends,.entered=true,.allocated=true};return 0;
}
static uint32_t seen_pin;
static esp_err_t pair_start_error,pair_close_error;
static esp32_mquickjs_wifi_nan_pasn_status_t pair_native;
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_start(const esp32_mquickjs_wifi_radio_operation_t*t,
 const esp32_mquickjs_wifi_nan_pairing_config_t*c,esp32_mquickjs_wifi_nan_pasn_status_t*out){
 assert(!lock_depth&&t->identity&&c->service_identity&&c->service_id&&c->peer_service_id==9);
 ++pair_starts;seen_pin=c->pincode;seen_credential=c->credential_id;
 pair_native=(esp32_mquickjs_wifi_nan_pasn_status_t){.identity=100+pair_starts,.active=true};
 memcpy(pair_native.peer,c->peer,6);*out=pair_native;return pair_start_error;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_pairing_poll(const esp32_mquickjs_wifi_radio_operation_t*t,
 uint32_t id,bool close,esp32_mquickjs_wifi_nan_pasn_status_t*out){
 assert(!lock_depth&&t->identity&&id==pair_native.identity);
 if(close){++pair_closes;pair_native.closing=true;if(!pair_close_error){pair_native.active=false;pair_native.retired=true;}}
 *out=pair_native;return close?pair_close_error:ESP_OK;
}
