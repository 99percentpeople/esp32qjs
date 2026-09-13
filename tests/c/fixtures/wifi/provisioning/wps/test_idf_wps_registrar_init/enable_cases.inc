
int main(void){
 esp_wps_config_t config={.wps_type=WPS_TYPE_PBC};
 mode_error=101;assert(wifi_ap_wps_enable_internal(&config)==(PATCHED?101:ESP_FAIL) && !initialized);mode_error=0;
 factory_error=102;assert(wifi_ap_wps_enable_internal(&config)==(PATCHED?102:ESP_FAIL) && !initialized);factory_error=0;
 init_error=103;assert(wifi_ap_wps_enable_internal(&config)==(PATCHED?103:ESP_FAIL) && initialized==1);init_error=0;
 assert(wifi_ap_wps_enable_internal(&config)==ESP_OK && initialized==2);return 0;
}
