
int main(void){
 assert(!esp_nan_usd_init());post_error=-74;
 NAN_USD_DATA_LOCK();esp32qjs_nan_usd_post(4,NULL,0);NAN_USD_DATA_UNLOCK();
 assert(post_calls==1&&s_esp32qjs_usd_drops==1&&!s_esp32qjs_usd_closing);
 s_esp32qjs_usd_drops=UINT32_MAX;
 NAN_USD_DATA_LOCK();esp32qjs_nan_usd_post(4,NULL,0);NAN_USD_DATA_UNLOCK();
 assert(s_esp32qjs_usd_drops==UINT32_MAX&&!s_esp32qjs_usd_error);
 assert(!esp_nan_usd_deinit());
}
