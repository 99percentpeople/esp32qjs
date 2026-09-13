
static esp32_mquickjs_wifi_nan_credential_info_t credential_meta[2];
static uint32_t next_credential=201;
bool esp32_mquickjs_wifi_nan_credential_info(unsigned slot,esp32_mquickjs_wifi_nan_credential_info_t*out){
 *out=(esp32_mquickjs_wifi_nan_credential_info_t){0};if(slot>=2)return false;*out=credential_meta[slot];
 return out->identity&&(!out->expires_us||native_clock<out->expires_us);
}
esp_err_t esp32_mquickjs_wifi_nan_credential_replace(unsigned slot,const uint8_t mac[6],int64_t expires,uint32_t*out){
 assert(locked&&wifi_task&&slot<2);*out=0;if(!next_credential)return ESP_ERR_NO_MEM;
 credential_meta[slot]=(esp32_mquickjs_wifi_nan_credential_info_t){.identity=next_credential++,.expires_us=expires};
 memcpy(credential_meta[slot].peer,mac,6);*out=credential_meta[slot].identity;return 0;
}

static esp32_mquickjs_wifi_nan_pasn_status_t snapshot;
esp_err_t esp32_mquickjs_wifi_nan_pasn_status(esp32_mquickjs_wifi_nan_pasn_status_t*out){*out=snapshot;return 0;}
uint32_t esp32_mquickjs_wifi_nan_pasn_identity(const uint8_t*mac){
 return snapshot.closing||(mac&&memcmp(mac,snapshot.peer,6))?0:snapshot.identity;
}
