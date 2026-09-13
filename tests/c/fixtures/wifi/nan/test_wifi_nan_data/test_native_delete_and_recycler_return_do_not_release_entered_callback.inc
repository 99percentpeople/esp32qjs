
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 uint8_t ndl[580]={2,3,4,5,6,7};uint32_t identity=data_bind(ndl,9);
 timer_node=(void*)1;timer_ndl=ndl;
 void *b=data_buffer(0,false);assert(!__wrap_nan_dp_post_tx(timer_node,b));
 uint32_t ticket=esp32_mquickjs_wifi_nan_tx_callback_enter(b);assert(ticket);
 assert(esp32_mquickjs_wifi_nan_tx_callback_allowed(ticket));
 esp32_mquickjs_wifi_nan_ndp_deleted(identity);
 assert(!esp32_mquickjs_wifi_nan_tx_callback_allowed(ticket));
 esp32_mquickjs_wifi_nan_ndp_status_t status;
 assert(esp32_mquickjs_wifi_nan_ndp_retire_native(identity,false,&status)==ESP_ERR_TIMEOUT);
 ic_ebuf_recycle_tx(b);
 assert(esp32_mquickjs_wifi_nan_ndp_retire_native(identity,true,&status)==ESP_ERR_TIMEOUT);
 esp32_mquickjs_wifi_nan_tx_callback_leave(ticket,false);
 assert(!esp32_mquickjs_wifi_nan_ndp_retire_native(identity,true,&status));
 assert(status.identity==identity&&status.frames_retired&&status.native_deleted);
 assert(!esp32_mquickjs_wifi_nan_ndp_service_busy(7));
 uint32_t fresh=data_bind(ndl,9);assert(fresh!=identity);
 assert(esp32_mquickjs_wifi_nan_ndp_retire_native(identity,true,&status)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_nan_tx_callback_allowed(ticket));
 assert(esp32_mquickjs_wifi_nan_ndp_status(fresh,&status)&&!status.native_deleted);
 assert(!esp32_mquickjs_wifi_nan_tx_close());
}
