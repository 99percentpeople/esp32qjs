
int main(void){
 for(unsigned fail=1;fail<=1;fail++){
  register_calls=unregister_calls=0;fail_register=fail;
  assert(esp_nan_usd_init()==-71&&g_nan_de);
  assert(s_esp32qjs_usd_closing&&s_esp32qjs_usd_handlers==((1U<<(fail-1))-1));
  assert(esp_nan_usd_init()==ESP_ERR_INVALID_STATE&&register_calls==fail);
  assert(!esp_nan_usd_deinit()&&!g_nan_de&&!s_esp32qjs_usd_handlers&&!s_esp32qjs_usd_identity);
  assert(unregister_calls==fail-1);
 }
 register_calls=unregister_calls=0;fail_register=0;
 assert(!esp_nan_usd_init()&&s_esp32qjs_usd_handlers==1);
 fail_unregister=1;assert(esp_nan_usd_deinit()==-72);
 assert(g_nan_de&&s_esp32qjs_usd_handlers==1&&s_esp32qjs_usd_closing);
 unsigned destroyed=engine_frees;fail_unregister=0;
 assert(!esp_nan_usd_deinit()&&unregister_calls==2&&engine_frees==destroyed+1);
 assert(!esp_nan_usd_deinit()&&unregister_calls==2&&engine_frees==destroyed+1);
 mac_error=-73;assert(esp_nan_usd_init()==-73&&!g_nan_de&&s_esp32qjs_usd_identity);
 assert(!esp_nan_usd_deinit());mac_error=0;fail_engine=true;
 assert(esp_nan_usd_init()==ESP_ERR_NO_MEM&&!g_nan_de);
 assert(!esp_nan_usd_deinit());
}
