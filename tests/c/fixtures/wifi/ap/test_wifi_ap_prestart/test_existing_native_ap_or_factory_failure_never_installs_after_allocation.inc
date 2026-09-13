
int main(void){
 prepare();g_ic[20]=1;esp32_mquickjs_wifi_ap_prestart_result_t result;
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result)==ESP_ERR_INVALID_STATE);
 assert(!setters&&!mode_creates&&!ap_starts);
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41));
 prepare();fail_create=true;
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result)==-93);
 assert(result.verified&&!result.mode_ready&&result.ap_quiesced&&result.rollback_complete);
 assert(setters==2&&!ap_starts&&stored.ap.ssid[0]=='o');
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41));
}
