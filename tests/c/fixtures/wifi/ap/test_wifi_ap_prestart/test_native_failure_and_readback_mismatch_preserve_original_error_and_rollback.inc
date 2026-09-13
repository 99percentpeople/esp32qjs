
int main(void){
 for(int failure=1;failure<=3;failure++){
  prepare();fail_set=failure==1?1:failure==3?3:0;bad_read=failure==2;
  esp32_mquickjs_wifi_ap_prestart_result_t result;
  int error=esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result);
  assert(error==(failure==2?ESP_ERR_INVALID_RESPONSE:-77));
  assert(result.error==error&&result.entered&&result.returned&&result.rollback_attempted);
  assert(!result.verified&&!result.mode_ready&&result.ap_quiesced&&!ap_starts&&mode==WIFI_MODE_STA&&setters==2);
  assert(result.rollback_complete==(failure!=3));
  assert(result.rollback_error==(failure==3?-88:0));
  assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41)&&!native_allocations);
 }
}
