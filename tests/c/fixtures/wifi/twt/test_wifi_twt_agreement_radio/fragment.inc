
static esp_err_t information_retire_error;
esp_err_t esp32_mquickjs_wifi_twt_sdk_information_reap(uint32_t id){(void)id;assert(!critical);return information_retire_error;}
esp_err_t esp32_mquickjs_wifi_twt_sdk_information_submit(uint32_t id,uint32_t ms,bool resume,uint32_t *out){
    (void)id;(void)ms;(void)resume;(void)out;assert(0);return ESP_ERR_INVALID_STATE;
}
bool esp32_mquickjs_wifi_twt_information_pending(void){return false;}
bool esp32_mquickjs_wifi_twt_information_read(uint32_t id,esp32_mquickjs_wifi_twt_information_result_t *out){
    (void)id;(void)out;assert(0);return false;
}
void esp32_mquickjs_wifi_twt_information_abandon(uint32_t id){(void)id;assert(0);}
