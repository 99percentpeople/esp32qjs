
static int esp_nan_chan_to_freq(int channel){return channel>=1&&channel<=14?2407+channel*5:-1;}
static bool native_wifi;
static unsigned dispatches,native_calls;
bool current_task_is_wifi_task(void){return native_wifi;}
static int eloop_register_timeout_blocking(int(*fn)(void*,void*),void*data,void*ctx){
 assert(!lock_depth&&!native_wifi&&!ctx);++dispatches;native_wifi=true;
 int result=fn(data,ctx);native_wifi=false;return result;
}
static int esp32qjs_usd_publish_native(const void*c){assert(c&&native_wifi&&lock_depth);++native_calls;return 7;}
static int esp32qjs_usd_subscribe_native(const void*c){return esp32qjs_usd_publish_native(c);}
static int esp32qjs_usd_cancel_service_native(int id){assert(id==7&&native_wifi&&lock_depth);++native_calls;return 0;}
static int esp32qjs_usd_cancel_publish_native(int id){return esp32qjs_usd_cancel_service_native(id);}
static int esp32qjs_usd_cancel_subscribe_native(int id){return esp32qjs_usd_cancel_service_native(id);}
static int esp32qjs_usd_update_publish_native(int id,uint8_t*ssi,uint16_t len){(void)ssi;(void)len;return esp32qjs_usd_cancel_service_native(id);}
static int esp32qjs_usd_transmit_native(int id,const uint8_t*ssi,uint16_t len,const uint8_t*peer,uint8_t peer_id){
 (void)ssi;(void)len;(void)peer;(void)peer_id;return esp32qjs_usd_cancel_service_native(id);
}
