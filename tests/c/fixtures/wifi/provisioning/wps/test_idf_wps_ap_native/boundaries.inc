
static int wifi_ap_wps_enable_internal(const esp_wps_config_t *config,uint32_t id){
 assert(config && esp32qjs_wps_ap_command_allowed(id));enables++;
 if(enable_error)return enable_error;
 int error=esp32qjs_wps_ap_result_bind(&hapd);if(error)return error;
 gWpsSm=&hapd;native_owner=WPS_OWNER_REGISTRAR;return ESP_OK;
}
static int wifi_ap_wps_start_internal(const unsigned char *pin,uint32_t id){
 assert(!pin && esp32qjs_wps_ap_command_allowed(id));starts++;return ESP_OK;
}
static int wifi_ap_wps_deinit(void){
 assert(esp32qjs_wps_ap_result_deinit_allowed(&hapd));deinits++;
 if(deinit_error)return deinit_error;
 gWpsSm=NULL;esp32qjs_wps_ap_result_detach(&hapd,ESP_OK);return ESP_OK;
}
esp_err_t esp32qjs_wps_ap_factory_release(uint32_t id){
 assert(esp32qjs_wps_ap_result_exact(id) && !gWpsSm && native_owner==WPS_OWNER_NONE);
 factory_calls++;return factory_error;
}
