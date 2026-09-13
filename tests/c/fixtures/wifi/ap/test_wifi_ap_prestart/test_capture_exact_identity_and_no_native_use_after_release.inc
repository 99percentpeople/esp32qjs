
int main(void){
 wifi_config_t requested={0},previous={0};requested.ap.ssid[0]='n';previous.ap.ssid[0]='o';
 fail_allocation=true;
 assert(esp32_mquickjs_wifi_ap_prestart_prepare(9,41,&requested,&previous,matches)==ESP_ERR_NO_MEM);
 assert(!s_ap_prestart&&!native_allocations&&!mode_creates&&!setters);
 fail_allocation=false;assert(!esp32_mquickjs_wifi_ap_prestart_prepare(9,41,&requested,&previous,matches));
 assert(esp32_mquickjs_wifi_ap_prestart_prepare(9,42,&requested,&previous,matches)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_ap_prestart_release(8,41)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_ap_prestart_release(9,42)==ESP_ERR_INVALID_STATE);
 requested.ap.ssid[0]='x';assert(s_ap_prestart->requested.ap.ssid[0]=='n');
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41));
 assert(!native_allocations&&!setters&&!mode_creates&&!s_ap_prestart);
 assert(esp32_mquickjs_wifi_ap_prestart_release(9,41)==ESP_ERR_INVALID_STATE);
}
