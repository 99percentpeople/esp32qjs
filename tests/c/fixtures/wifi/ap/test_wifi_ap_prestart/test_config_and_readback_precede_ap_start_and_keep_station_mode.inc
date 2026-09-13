
int main(void){
 prepare();esp32_mquickjs_wifi_ap_prestart_result_t result;
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,42,&result)==ESP_ERR_INVALID_STATE&&!setters);
 assert(!esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result));
 assert(result.entered&&result.mutated&&result.verified&&result.mode_ready&&result.returned&&!result.error);
 assert(!result.rollback_attempted&&mode_creates==1&&ap_starts==1&&setters==1&&reads==1);
 assert(stored.ap.ssid[0]=='n'&&mode==WIFI_MODE_APSTA);
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result)==ESP_ERR_INVALID_STATE&&setters==1);
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41)&&!native_allocations);
}
