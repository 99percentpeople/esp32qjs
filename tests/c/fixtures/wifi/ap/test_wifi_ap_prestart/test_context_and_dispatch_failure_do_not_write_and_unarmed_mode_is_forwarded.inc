
int main(void){
 assert(!__wrap_wifi_mode_set(WIFI_MODE_STA)&&!setters);
 prepare();dispatch_failure=true;esp32_mquickjs_wifi_ap_prestart_result_t result;
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result)==-91);
 assert(result.returned&&!result.entered&&!result.mutated&&!setters&&!ap_starts);
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41));
 prepare();wrong_task=true;
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result)==ESP_ERR_INVALID_STATE);
 assert(!result.mutated&&!setters&&!ap_starts);
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41)&&!native_allocations);
}
