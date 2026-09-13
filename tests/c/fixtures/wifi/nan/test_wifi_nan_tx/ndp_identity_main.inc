
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 uint32_t identity=0,other=0,native=0;uint8_t ndl[20]={2,3,4,5,6,7};
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7}};
 assert(!esp32_mquickjs_wifi_nan_ndp_begin(7,&req,&identity)&&identity);
 assert(esp32_mquickjs_wifi_nan_ndp_service_busy(7)&&!esp32_mquickjs_wifi_nan_ndp_service_busy(8));
 assert(!esp32_mquickjs_wifi_nan_ndp_request_enter(&other));
 assert(esp32_mquickjs_wifi_nan_ndp_request_enter(&req)==identity);
 assert(!esp32_mquickjs_wifi_nan_ndp_native_claim(ndl,9,&native)&&native==identity);
 esp32_mquickjs_wifi_nan_ndp_native_bound(native,true);
 esp32_mquickjs_wifi_nan_sdk_notice_t notice={.kind=ESP32_MQUICKJS_NAN_SDK_NDP_BOUND,
  .context=identity,.service_id=7,.ndp_id=9,.peer={2,3,4,5,6,7}};
 esp32_mquickjs_wifi_nan_ndp_notice(&notice);
 esp32_mquickjs_wifi_nan_ndp_status_t status;
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&status)&&status.host_bound&&!status.submitted);
 assert(esp32_mquickjs_wifi_nan_ndp_peer_service(ndl,3,7));
 assert(!esp32_mquickjs_wifi_nan_ndp_peer_service(ndl,3,8));
 assert(!esp32_mquickjs_wifi_nan_ndp_peer_service(ndl,4,7));
 wifi_nan_datapath_req_t second={.pub_id=4,.peer_mac={2,3,4,5,6,8}};
 assert(esp32_mquickjs_wifi_nan_ndp_begin(8,&second,&other)==ESP_ERR_INVALID_STATE&&!other);
 /* Complete before ioctl returns; submission must not clear that terminal. */
 notice.kind=ESP32_MQUICKJS_NAN_SDK_NDP_ACCEPTED;notice.status=1;
 esp32_mquickjs_wifi_nan_ndp_notice(&notice);
 esp32_mquickjs_wifi_nan_ndp_request_leave(identity,0);
 esp32_mquickjs_wifi_nan_ndp_submitted(identity,0);
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&status)&&status.accepted&&status.submitted);
 assert(!esp32_mquickjs_wifi_nan_ndp_begin(8,&second,&other)&&other!=identity);
 assert(!esp32_mquickjs_wifi_nan_ndp_mutation(identity,true,&status));
 esp32_mquickjs_wifi_nan_ndp_mutation_done(identity,true,-72);
 assert(esp32_mquickjs_wifi_nan_ndp_mutation(identity,true,&status)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&status)&&status.cleanup_error==-72&&!status.native_deleted);
 assert(esp32_mquickjs_wifi_nan_ndp_deleting(ndl)==identity);
 esp32_mquickjs_wifi_nan_ndp_deleted(identity);
 assert(esp32_mquickjs_wifi_nan_ndp_request_enter(&second)==other);
 memcpy(ndl,second.peer_mac,6);native=0;
 assert(esp32_mquickjs_wifi_nan_ndp_native_claim(ndl,10,&native)==ESP_ERR_INVALID_STATE&&!native);
 esp32_mquickjs_wifi_nan_ndp_request_leave(other,-73);
 esp32_mquickjs_wifi_nan_ndp_submitted(other,-73);
 assert(esp32_mquickjs_wifi_nan_ndp_status(other,&status)&&status.error==-73&&!status.accepted);
 /* Only the caller's physical STOP boundary may reclaim this quarantine. */
 assert(!esp32_mquickjs_wifi_nan_tx_close());assert(!esp32_mquickjs_wifi_nan_tx_open());
 native=0;assert(!esp32_mquickjs_wifi_nan_ndp_native_claim(ndl,11,&native)&&native!=identity&&native!=other);
 esp32_mquickjs_wifi_nan_ndp_native_bound(native,true);
 assert(!esp32_mquickjs_wifi_nan_ndp_status(identity,&status));
 assert(esp32_mquickjs_wifi_nan_ndp_mutation(identity,true,&status)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_nan_ndp_mutation(native,false,&status)==ESP_ERR_INVALID_STATE);
 notice=(esp32_mquickjs_wifi_nan_sdk_notice_t){.kind=ESP32_MQUICKJS_NAN_SDK_NDP_REQUEST,
  .service_id=5,.ndp_id=11,.peer={2,3,4,5,6,8}};
 esp32_mquickjs_wifi_nan_ndp_notice(&notice);
 assert(!esp32_mquickjs_wifi_nan_ndp_mutation(native,false,&status)&&status.service_id==5);
 assert(esp32_mquickjs_wifi_nan_ndp_mutation(native,false,&status)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_nan_tx_close());assert(!esp32_mquickjs_wifi_nan_tx_open());
 s_nan_ndp_next_identity=UINT32_MAX;identity=0;
 assert(!esp32_mquickjs_wifi_nan_ndp_begin(7,&req,&identity)&&identity==UINT32_MAX);
 assert(!esp32_mquickjs_wifi_nan_tx_close());assert(!esp32_mquickjs_wifi_nan_tx_open());
 other=0;assert(esp32_mquickjs_wifi_nan_ndp_begin(8,&second,&other)==ESP_ERR_NO_MEM&&!other);
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live&&!locked);
}
