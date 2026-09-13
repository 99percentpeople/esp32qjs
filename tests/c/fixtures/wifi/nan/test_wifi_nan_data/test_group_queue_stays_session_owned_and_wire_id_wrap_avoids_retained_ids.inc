
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 uint8_t ndl[580]={2,3,4,5,6,7};uint32_t identity=data_bind(ndl,255);
 assert(!__wrap_nan_dp_post_tx(NULL,data_buffer(0,true)));
 uint8_t selected=0;s_dp[113]=1;
 assert(!esp32_mquickjs_wifi_nan_ndp_select_native_id(255,&selected)&&selected==2);
 esp32_mquickjs_wifi_nan_ndp_deleted(identity);
 esp32_mquickjs_wifi_nan_ndp_status_t status;
 assert(!esp32_mquickjs_wifi_nan_ndp_retire_native(identity,true,&status));
 assert(esp32_mquickjs_wifi_nan_tx_service_drained(7)); /* group EB has no service ID */
 assert(!esp32_mquickjs_wifi_nan_ndp_select_native_id(255,&selected)&&selected==255);
 assert(!esp32_mquickjs_wifi_nan_ndp_select_native_id(0,&selected)&&selected==2);
 assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT); /* group EB still live */
 ic_ebuf_recycle_tx(buffers[0]);assert(!esp32_mquickjs_wifi_nan_tx_close());
}
