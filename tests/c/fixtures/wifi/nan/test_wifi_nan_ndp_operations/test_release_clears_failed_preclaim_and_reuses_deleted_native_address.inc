
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7}};
 uint32_t failed=0;esp32_mquickjs_wifi_nan_ndp_status_t state;
 request_error=-73;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&failed)==-73&&failed&&host.publisher_id);
 host.own_role=ESP_WIFI_NDP_ROLE_RESPONDER;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release_native(failed,&state)==ESP_ERR_INVALID_STATE);
 assert(host.publisher_id&&esp32_mquickjs_wifi_nan_ndp_service_busy(7));
 host.own_role=ESP_WIFI_NDP_ROLE_INITIATOR;
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_release_native(failed,&state));
 assert(state.error==-73&&state.frames_retired&&!host.publisher_id);
 assert(!esp32_mquickjs_wifi_nan_ndp_service_busy(7));
 request_error=0;uint32_t first=0,second=0;s_ndp_id=255;
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&first));
 assert(first!=failed&&host.ndp_id==255&&!s_ndp_id);
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release_native(first,&state)==ESP_ERR_TIMEOUT);
 esp32qjs_nan_ndp_delete(native_ndl);
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_release_native(first,&state));
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&second));
 assert(second!=first&&host.ndp_id==1&&s_ndp_id==2);
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_release_native(first,&state)==ESP_ERR_INVALID_STATE);
 assert(host.ndp_id==1);esp32qjs_nan_ndp_delete(native_ndl);
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_release_native(second,&state));
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live&&!depth);
}
