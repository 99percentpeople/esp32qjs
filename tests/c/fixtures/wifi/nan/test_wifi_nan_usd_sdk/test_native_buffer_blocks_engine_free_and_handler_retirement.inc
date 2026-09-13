
int main(void){
 assert(!esp_nan_usd_init());transport_busy=true;
 assert(esp_nan_usd_deinit()==ESP_ERR_NOT_FINISHED&&g_nan_de&&!unregister_calls&&!engine_frees);
 assert(esp_nan_usd_init()==ESP_ERR_INVALID_STATE);
 transport_busy=false;assert(!esp_nan_usd_deinit()&&!g_nan_de&&unregister_calls==1&&engine_frees==1);
}
