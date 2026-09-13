
int main(void){
 assert(!esp32qjs_nan_usd_transport_open());
 begin_message(11);early=true;assert(!send_frame(1,0xa5));
 esp32qjs_usd_transport_t*t=s_esp32qjs_usd_transport;t->message_scope=false;
 esp32_mquickjs_wifi_nan_message_tx_status_t status;
 assert(esp32_mquickjs_wifi_nan_usd_message_status(11,&status));
 assert(status.tx_done&&status.tx_succeeded&&!status.buffer_retired&&status.completed_us==1234);
 assert(esp32_mquickjs_wifi_nan_usd_message_release(11)==ESP_ERR_NOT_FINISHED);
 busy=false;assert(esp32qjs_nan_usd_transport_service(false)==ESP_ERR_NOT_FINISHED);
 physical.buffer_present=false;physical.recycling=true;
 assert(esp32qjs_nan_usd_transport_service(false)==ESP_ERR_NOT_FINISHED);
 physical.recycling=false;physical.recycled=true;
 assert(!esp32qjs_nan_usd_transport_service(false));
 assert(esp32_mquickjs_wifi_nan_usd_message_status(11,&status)&&status.buffer_retired);
 assert(esp32_mquickjs_wifi_nan_usd_message_release(12)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_nan_usd_message_release(11));
 assert(!esp32qjs_nan_usd_transport_free()&&allocations==frees);
}
