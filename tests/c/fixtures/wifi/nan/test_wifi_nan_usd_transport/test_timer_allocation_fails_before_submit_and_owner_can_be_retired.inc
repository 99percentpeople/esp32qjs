
int main(void){
 assert(!esp32qjs_nan_usd_transport_open());begin_message(33);timer_failure=true;
 assert(send_frame(1,7)==ESP_ERR_NO_MEM&&!submits&&!physical.held);
 s_esp32qjs_usd_transport->message_scope=false;
 assert(!esp32_mquickjs_wifi_nan_usd_message_release(33));
 assert(!esp32qjs_nan_usd_transport_free()&&allocations==frees);
}
